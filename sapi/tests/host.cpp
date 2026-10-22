#include "identity.hpp"
#include "support.hpp"
#include "token.hpp"
#include "residency.hpp"
#include "speech_rate.hpp"
#include "scratch.hpp"
#include "outloud/pcm16.hpp"
#include "outloud/cancellation.hpp"
#include <array>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <vector>

namespace {
using Clock = std::chrono::steady_clock;
void check(bool value,const char* message) { if (!value) { throw std::runtime_error(message); } }
void hr(HRESULT value,const char* message) { check(value==S_OK,message); }
template<class T> struct Com {
    T* p = nullptr;
    ~Com() { reset(); }
    T* operator->() const { return p; }
    T** put() { reset(); return &p; }
    void reset() { if (p) { p->Release(); p = nullptr; } }
};
constexpr std::wstring_view text = L"The watch captain needs your help before the next caravan arrives. Follow the river to the abandoned tower, speak with the scout waiting at its gate, and bring his report back to the village. Keep your lantern burning, for the path is narrow.";
SPVTEXTFRAG fragment(std::wstring_view value) {
    SPVTEXTFRAG f{}; f.State.eAction = SPVA_Speak; f.State.Volume = 100;
    f.pTextStart = value.data(); f.ulTextLen = static_cast<ULONG>(value.size()); return f;
}
enum class Mode { normal, partial, failed, zero, odd, oversized, stopped };
struct Bookmark { std::wstring name; WPARAM value; };
class Site final : public ISpTTSEngineSite {
public:
    explicit Site(Mode m,bool capture) : mode(m), pcm(capture ? 652800 : 0) {}
    void start() { begun = Clock::now(); }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid,void** out) override {
        if (!out) { return E_POINTER; } *out = nullptr;
        if (iid!=IID_IUnknown && iid!=__uuidof(ISpTTSEngineSite) && iid!=__uuidof(ISpEventSink)) { return E_NOINTERFACE; }
        *out = static_cast<ISpTTSEngineSite*>(this); AddRef(); return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++refs; }
    ULONG STDMETHODCALLTYPE Release() override { const auto n = --refs; if (!n) { delete this; } return n; }
    HRESULT STDMETHODCALLTYPE AddEvents(const SPEVENT* events,ULONG count) override {
        affinity();
        if (!events && count) { return E_POINTER; }
        if (bookmarks.size()>=fail_event_after) { return E_ACCESSDENIED; }
        try {
            for (ULONG i = 0; i < count; ++i) {
                const auto& e = events[i];
                if (bytes || e.eEventId!=SPEI_TTS_BOOKMARK || e.elParamType!=SPET_LPARAM_IS_STRING ||
                    e.ullAudioStreamOffset!=0 || e.ulStreamNum!=0 || !e.lParam) { return E_UNEXPECTED; }
                // Copy synchronously, just like SAPI. Retain no borrowed event
                // or fragment pointer; assertions below run after Speak returns.
                bookmarks.push_back({reinterpret_cast<const wchar_t*>(e.lParam),e.wParam});
            }
            return S_OK;
        } catch (const std::bad_alloc&) { return E_OUTOFMEMORY; }
        catch (...) { return E_FAIL; }
    }
    HRESULT STDMETHODCALLTYPE GetEventInterest(ULONGLONG* out) override {
        affinity(); ++interest_queries;
        if (!out) { return E_POINTER; } *out = event_interest; return interest_result;
    }
    void affinity() { if (GetCurrentThreadId()!=caller) { wrong_thread = true; } }
    DWORD STDMETHODCALLTYPE GetActions() override {
        affinity(); ++actions;
        DWORD flags = 0;
        if (controls && !controls_reported) { flags = SPVES_SKIP|SPVES_RATE|SPVES_VOLUME; controls_reported = true; }
        if (next_rate_change<rate_changes.size() && bytes>=rate_changes[next_rate_change].first) {
            rate = rate_changes[next_rate_change++].second; flags |= SPVES_RATE;
        }
        const bool byte_abort = bytes>=abort_bytes && (!late_abort || ++end_polls>=2);
        if (entry_abort || byte_abort || bookmarks.size()>=abort_events ||
            (timed_abort && Clock::now()-begun>=std::chrono::milliseconds(20))) {
            if (!abort_observable) {
                abort_observable = true;
                observable = timed_abort ? begun+std::chrono::milliseconds(20) : begun;
            }
            ++abort_observations; flags |= SPVES_ABORT;
        }
        return flags;
    }
    HRESULT STDMETHODCALLTYPE GetRate(long* out) override { affinity(); ++rates; if (!out) { return E_POINTER; } *out = rate; return S_OK; }
    HRESULT STDMETHODCALLTYPE GetVolume(USHORT* out) override { affinity(); ++volumes; if (!out) { return E_POINTER; } *out = 100; return S_OK; }
    HRESULT STDMETHODCALLTYPE GetSkipInfo(SPVSKIPTYPE* type,long* count) override { affinity(); ++skips; *type = SPVST_SENTENCE; *count = 1; return S_OK; }
    HRESULT STDMETHODCALLTYPE CompleteSkip(long count) override { affinity(); ++skip_completions; return count==0 ? S_OK : E_UNEXPECTED; }
    HRESULT STDMETHODCALLTYPE Write(const void* data,ULONG count,ULONG* written) override {
        affinity();
        if (abort_observations) { write_after_abort = true; }
        if (!calls) { first_ms = std::chrono::duration<double,std::milli>(Clock::now()-begun).count(); }
        ++calls;
        if (mode==Mode::failed) { return E_ACCESSDENIED; }
        if (mode==Mode::stopped) { return SP_AUDIO_STOPPED; }
        if (!remaining) { ++frames; remaining = variable_frames ? count : 3840; }
        if (count!=remaining) { return E_UNEXPECTED; }
        *written = mode==Mode::partial ? std::min(count,ULONG{126}) : mode==Mode::zero ? 0 : mode==Mode::odd ? 1 : mode==Mode::oversized ? count+2 : count;
        if (*written<=count && *written%2==0) {
            if (!pcm.empty()) {
                if (bytes+*written>pcm.size()) {
                    if (!variable_frames) { return E_UNEXPECTED; }
                    pcm.resize(static_cast<std::size_t>(bytes+*written));
                }
                const auto* input = static_cast<const std::uint8_t*>(data);
                std::copy(input,input+*written,pcm.begin()+static_cast<std::size_t>(bytes));
            }
            bytes += *written; remaining -= *written;
            if (!abort_observable && bytes>=abort_bytes) { abort_observable = true; observable = Clock::now(); }
        }
        return S_OK;
    }
    Mode mode;
    std::vector<Bookmark> bookmarks;
    ULONGLONG event_interest = SPFEI(SPEI_TTS_BOOKMARK);
    HRESULT interest_result = S_OK;
    ULONG interest_queries = 0;
    std::size_t abort_events = SIZE_MAX, fail_event_after = SIZE_MAX;
    std::vector<std::uint8_t> pcm;
    std::uint64_t bytes = 0;
    ULONG calls = 0, frames = 0, remaining = 0, actions = 0;
    DWORD caller = GetCurrentThreadId();
    bool wrong_thread = false;
    bool entry_abort = false, timed_abort = false, abort_observable = false, write_after_abort = false;
    bool controls = false, controls_reported = false;
    bool variable_frames = false;
    long rate = 0;
    std::vector<std::pair<std::uint64_t,long>> rate_changes;
    std::size_t next_rate_change = 0;
    bool late_abort = false;
    ULONG end_polls = 0;
    std::uint64_t abort_bytes = UINT64_MAX;
    ULONG abort_observations = 0, rates = 0, volumes = 0, skips = 0, skip_completions = 0;
    Clock::time_point observable;
    double abort_latency_ms = 0;
    Clock::time_point begun;
    double first_ms = 0, total_ms = 0;
private:
    std::atomic<ULONG> refs{1};
};
double speak(ISpTTSEngine* engine,const WAVEFORMATEX* wave,SPVTEXTFRAG* fragments,Site& site,HRESULT expected=S_OK) {
    site.start(); const auto start = Clock::now();
    const auto result = engine->Speak(0,SPDFID_WaveFormatEx,wave,fragments,&site);
    const auto end = Clock::now();
    check(result==expected,"Speak HRESULT differs");
    check(!site.wrong_thread,"Site called from a different thread");
    site.total_ms = std::chrono::duration<double,std::milli>(end-start).count();
    if (site.abort_observable) { site.abort_latency_ms = std::chrono::duration<double,std::milli>(end-site.observable).count(); }
    return site.total_ms;
}
void validate(const Site& site) {
    check(site.frames==170 && site.bytes==652800 && !site.remaining && site.first_ms<site.total_ms,
          "Canonical samples/frames/immediate streaming differ");
    check(site.actions>0,"SAPI actions not polled");
    check(site.bookmarks.empty(),"Plain canonical text emitted an unsolicited bookmark");
}
void text_checks() {
    auto f = fragment(L"caf\u00e9 \U0001f600");
    check(outloud::sapi::collect_request(&f).text=="caf\xc3\xa9 \xf0\x9f\x98\x80","Unicode conversion failed");
    std::wstring a(1,wchar_t(0xd83d)), b(1,wchar_t(0xde00)); auto one = fragment(a), two = fragment(b); one.pNext = &two;
    check(outloud::sapi::collect_request(&one).text=="\xf0\x9f\x98\x80","Fragment-spanning surrogate pair failed");
    one.pNext = nullptr;
    bool rejected = false; try { (void)outloud::sapi::collect_request(&one); } catch (const outloud::sapi::Failure&) { rejected = true; }
    check(rejected,"Invalid UTF16 accepted");
}
SPVTEXTFRAG bookmark(std::wstring_view name) {
    auto f = fragment(name); f.State.eAction = SPVA_Bookmark; return f;
}
void bookmark_checks(ISpTTSEngine* engine,const WAVEFORMATEX* wave) {
    auto speech = fragment(L"Hello traveler.");
    Site plain(Mode::normal,true); speak(engine,wave,&speech,plain);
    check(plain.bytes>0 && !plain.remaining && plain.bookmarks.empty() && !plain.interest_queries,
          "Plain text bookmark behavior changed");
    const auto same_audio = [&](const Site& site) {
        check(site.bytes==plain.bytes && site.frames==plain.frames && site.pcm==plain.pcm && !site.remaining,
              "Bookmark changed synthesized speech");
    };
    const auto recovery = [&] {
        Site recovered(Mode::normal,true); speak(engine,wave,&speech,recovered); same_audio(recovered);
        check(recovered.bookmarks.empty(),"Bookmark leaked into the next request");
    };
    // Neither a missing bookmark subscription nor an unavailable event method
    // is relevant to a request with no leading bookmarks.
    for (const auto mask : {ULONGLONG{0},ULONGLONG{SPFEI_FLAGCHECK}}) {
        Site no_events(Mode::normal,true); no_events.event_interest = mask;
        no_events.interest_result = E_ACCESSDENIED; no_events.fail_event_after = 0;
        speak(engine,wave,&speech,no_events); same_audio(no_events);
        check(!no_events.interest_queries && no_events.bookmarks.empty(),"Plain speech required event calls");
    }
    // A fragment is a counted substring, not necessarily a terminated name.
    std::wstring source = L"OUTLOUD_TEST_123\"/>trailing XML";
    auto one = bookmark(std::wstring_view(source).substr(0,std::wstring_view(L"OUTLOUD_TEST_123").size())); one.pNext = &speech;
    const auto parsed = outloud::sapi::collect_request(&one);
    check(parsed.text=="Hello traveler." && parsed.bookmarks==std::vector<std::wstring>{L"OUTLOUD_TEST_123"},
          "Bookmark entered TTS text or substring extraction differed");
    Site single(Mode::normal,true); speak(engine,wave,&one,single); same_audio(single);
    source.assign(source.size(),L'x');
    check(single.bookmarks.size()==1 && single.bookmarks[0].name==L"OUTLOUD_TEST_123" && !single.bookmarks[0].value,
          "Single bookmark event or copied lifetime differs");

    std::array<std::wstring_view,5> names{L"Start",L"OUTLOUD:17:3",L"42",L"caf\u00e9 \U0001f600",L"Start"};
    std::array<SPVTEXTFRAG,5> chain{};
    for (std::size_t i=0; i<chain.size(); ++i) {
        chain[i] = bookmark(names[i]); chain[i].pNext = i+1<chain.size() ? &chain[i+1] : &speech;
    }
    Site multiple(Mode::normal,true); speak(engine,wave,chain.data(),multiple); same_audio(multiple);
    check(multiple.bookmarks.size()==names.size(),"Multiple bookmark count differs");
    for (std::size_t i=0; i<names.size(); ++i) {
        check(multiple.bookmarks[i].name==names[i] && multiple.bookmarks[i].value==(i==2 ? WPARAM{42} : 0),
              "Bookmark order, opaque string or numeric wParam differs");
    }
    Site uninterested(Mode::normal,true); uninterested.event_interest = SPFEI_FLAGCHECK;
    speak(engine,wave,chain.data(),uninterested); same_audio(uninterested);
    check(uninterested.bookmarks.empty() && uninterested.interest_queries==1,"Reserved bits mistaken for bookmark interest");
    for (const auto mask : {ULONGLONG{0},ULONGLONG{SPFEI(SPEI_TTS_BOOKMARK)}}) {
        Site status(Mode::normal,true); status.event_interest = mask; status.interest_result = S_FALSE;
        speak(engine,wave,chain.data(),status); same_audio(status);
        check(status.interest_queries==1 && status.bookmarks.size()==(mask ? names.size() : 0),
              "Successful HRESULT status suppressed speech or misread event interest");
    }
    Site zero_interest(Mode::normal,true); zero_interest.event_interest = 0;
    speak(engine,wave,chain.data(),zero_interest); same_audio(zero_interest);
    check(zero_interest.bookmarks.empty() && zero_interest.interest_queries==1,"Zero interest suppressed speech or emitted events");

    Site entry(Mode::normal,false); entry.entry_abort = true;
    speak(engine,wave,chain.data(),entry);
    check(entry.bookmarks.empty() && !entry.calls,"Entry abort emitted bookmark/audio"); recovery();
    Site between(Mode::normal,false); between.abort_events = 1;
    speak(engine,wave,chain.data(),between);
    check(between.bookmarks.size()==1 && between.bookmarks[0].name==L"Start" && !between.calls,
          "Abort between leading bookmarks did not stop emission"); recovery();
    Site partial(Mode::partial,false); partial.abort_bytes = 126;
    speak(engine,wave,chain.data(),partial);
    check(partial.bookmarks.size()==names.size() && partial.bytes==126 && !partial.write_after_abort,
          "Bookmark broke partial Write cancellation"); recovery();
    Site failed(Mode::normal,false); failed.fail_event_after = 1;
    speak(engine,wave,chain.data(),failed,E_ACCESSDENIED);
    check(failed.bookmarks.size()==1 && !failed.calls,"AddEvents failure continued output"); recovery();
    Site interest_failed(Mode::normal,false); interest_failed.interest_result = E_ACCESSDENIED;
    speak(engine,wave,chain.data(),interest_failed,E_ACCESSDENIED);
    check(interest_failed.bookmarks.empty() && !interest_failed.calls,"Interest failure continued output");
    Site write_failed(Mode::failed,false);
    speak(engine,wave,chain.data(),write_failed,E_ACCESSDENIED);
    check(write_failed.bookmarks.size()==names.size() && !write_failed.bytes,"Bookmarked Write failure differed"); recovery();

    auto tail = bookmark(L"mid"); speech.pNext = &tail;
    Site mid(Mode::normal,true); speak(engine,wave,chain.data(),mid); same_audio(mid);
    check(mid.bookmarks.size()==names.size(),"Non-leading bookmark suppressed speech or acquired a false offset");
    for (std::size_t i=0; i<names.size(); ++i) {
        check(mid.bookmarks[i].name==names[i],"Non-leading metadata changed leading bookmark order");
    }
    Site trailing_only(Mode::normal,true); trailing_only.interest_result = E_ACCESSDENIED;
    trailing_only.fail_event_after = 0;
    speak(engine,wave,&speech,trailing_only); same_audio(trailing_only);
    check(!trailing_only.interest_queries && trailing_only.bookmarks.empty(),"Trailing-only bookmark required events or suppressed speech");
    speech.pNext = nullptr;
    auto malformed = bookmark(std::wstring_view(L"a\0b",3));
    Site invalid(Mode::normal,false); speak(engine,wave,&malformed,invalid,E_INVALIDARG);
    malformed.pTextStart = nullptr; speak(engine,wave,&malformed,invalid,E_POINTER);
    check(invalid.bookmarks.empty() && !invalid.calls,"Invalid bookmark produced output");
    auto only = bookmark(L"OUTLOUD:17:3");
    Site no_text(Mode::normal,false); speak(engine,wave,&only,no_text);
    check(no_text.bookmarks.size()==1 && no_text.bookmarks[0].name==L"OUTLOUD:17:3" && !no_text.calls,
          "Bookmark-only request differed");
    recovery();
    std::cout << "PASS: leading bookmarks, exact/order/offset/ownership/interest, failures and cancellation recovery.\n";
}
void real_host_checks(IClassFactory* factory,const TestToken& configuration,std::span<const std::uint8_t> canonical) {
    // Actual Windows parser/site/event queue; the in-memory token instantiates
    // the DLL's factory. No Pocket token/CLSID is registered or looked up.
    check(SUCCEEDED(CoInitializeEx(nullptr,COINIT_MULTITHREADED)),"Cannot initialize COM for real SAPI");
    struct EndCOM { ~EndCOM() { CoUninitialize(); } } end_com;
    Com<TestToken> token; token.p = new TestToken; token->values = configuration.values;
    token->values[L"TestId"] = L"OutLoud_Check_InMemory";
    token->values[L"Language"] = L"409"; token->factory = factory;
    Com<ISpVoice> voice;
    hr(CoCreateInstance(CLSID_SpVoice,nullptr,CLSCTX_INPROC_SERVER,__uuidof(ISpVoice),
        reinterpret_cast<void**>(voice.put())),"Cannot create real SAPI voice");
    Com<IStream> memory; hr(CreateStreamOnHGlobal(nullptr,TRUE,memory.put()),"Cannot create memory output");
    Com<ISpStream> stream;
    hr(CoCreateInstance(CLSID_SpStream,nullptr,CLSCTX_INPROC_SERVER,__uuidof(ISpStream),
        reinterpret_cast<void**>(stream.put())),"Cannot create real SAPI stream");
    auto format = outloud::sapi::native_format();
    hr(stream->SetBaseStream(memory.p,SPDFID_WaveFormatEx,&format),"Cannot set real SAPI format");
    hr(voice->SetOutput(stream.p,FALSE),"Cannot set real SAPI output");
    hr(voice->SetVoice(token.p),"Cannot set in-memory real SAPI voice");
    hr(voice->SetInterest(SPFEI(SPEI_TTS_BOOKMARK),SPFEI(SPEI_TTS_BOOKMARK)),"Cannot set bookmark interest");
    std::uint64_t offset = 0;
    const auto run = [&](const std::wstring& input,const std::vector<std::wstring>& names) {
        hr(voice->Speak(input.c_str(),SPF_IS_XML,nullptr),"Real SAPI Speak failed");
        STATSTG stat{}; hr(memory->Stat(&stat,STATFLAG_NONAME),"Cannot measure real SAPI output");
        check(stat.cbSize.QuadPart>offset && stat.cbSize.QuadPart-offset<=652800,"Real SAPI wrote no/invalid PCM");
        std::vector<std::uint8_t> pcm(static_cast<std::size_t>(stat.cbSize.QuadPart-offset));
        LARGE_INTEGER position{}; position.QuadPart = static_cast<LONGLONG>(offset);
        hr(memory->Seek(position,STREAM_SEEK_SET,nullptr),"Cannot seek real SAPI PCM");
        ULONG read = 0; hr(memory->Read(pcm.data(),static_cast<ULONG>(pcm.size()),&read),"Cannot read real SAPI PCM");
        check(read==pcm.size(),"Real SAPI PCM truncated");
        std::vector<std::wstring> received;
        for (;;) {
            SPEVENT event{}; ULONG count = 0;
            const auto result = voice->GetEvents(1,&event,&count);
            struct Clear {
                SPEVENT& event;
                ~Clear() {
                    if (event.elParamType==SPET_LPARAM_IS_STRING || event.elParamType==SPET_LPARAM_IS_POINTER) {
                        CoTaskMemFree(reinterpret_cast<void*>(event.lParam));
                    } else if (event.elParamType==SPET_LPARAM_IS_TOKEN || event.elParamType==SPET_LPARAM_IS_OBJECT) {
                        if (event.lParam) { reinterpret_cast<IUnknown*>(event.lParam)->Release(); }
                    }
                }
            } clear{event};
            if (result==S_FALSE && !count) { break; }
            hr(result,"Cannot fetch real SAPI event");
            check(count==1 && event.eEventId==SPEI_TTS_BOOKMARK && event.elParamType==SPET_LPARAM_IS_STRING &&
                  event.lParam && event.ullAudioStreamOffset==offset,"Real SAPI bookmark fields differ");
            received.emplace_back(reinterpret_cast<const wchar_t*>(event.lParam));
        }
        check(received==names,"Real SAPI bookmark names/order differ");
        offset = stat.cbSize.QuadPart; return pcm;
    };
    const auto plain = run(L"Hello traveler.",{});
    check(run(L"Hello traveler.<bookmark mark=\"End\"/>",{})==plain,"Real SAPI trailing metadata changed PCM");
    check(run(L"<bookmark mark=\"Start\"/><bookmark mark=\"OUTLOUD_TEST_123\"/>Hello traveler.<bookmark mark=\"End\"/>",
              {L"Start",L"OUTLOUD_TEST_123"})==plain,"Real SAPI leading/trailing metadata changed PCM");
    const auto full = run(std::wstring(text)+L"<bookmark mark=\"End\"/>",{});
    check(full.size()==canonical.size() && std::equal(full.begin(),full.end(),canonical.begin()),
          "Real SAPI canonical PCM differs from native PCM16");
    std::cout << "PASS: real Windows SAPI parser/site/queued bookmarks, trailing metadata, exact canonical PCM.\n";
}
}
#include "stabilization_checks.hpp"
struct SonicReference {
    std::vector<std::uint8_t> pcm;
    std::uint64_t before_flush_bytes = 0;
};
SonicReference sonic_reference(std::span<const std::int16_t> input,long rate) {
    SonicReference result;
    const auto append = [&](std::span<const std::int16_t> pcm) {
        const auto* bytes = reinterpret_cast<const std::uint8_t*>(pcm.data());
        result.pcm.insert(result.pcm.end(),bytes,bytes+pcm.size_bytes());
    };
    // Direct upstream API is the oracle for continuity and complete tail delivery.
    const auto stream = sonicCreateStream(24000,1);
    check(stream!=nullptr,"Reference Sonic allocation failed");
    try {
        sonicSetSpeed(stream,outloud::sapi::rate_tempo(rate));
        const auto drain = [&] {
            std::array<std::int16_t,1920> output;
            int count = 0;
            while ((count=sonicReadShortFromStream(stream,output.data(),static_cast<int>(output.size())))>0) {
                append({output.data(),static_cast<std::size_t>(count)});
            }
        };
        for (std::size_t offset=0;offset<input.size();offset+=1920) {
            const auto block = input.subspan(offset,std::min(std::size_t{1920},input.size()-offset));
            check(sonicWriteShortToStream(stream,block.data(),static_cast<int>(block.size()))!=0,"Reference Sonic write failed");
            drain();
        }
        result.before_flush_bytes=result.pcm.size();
        check(sonicFlushStream(stream)!=0,"Reference Sonic flush failed"); drain();
    } catch (...) { sonicDestroyStream(stream); throw; }
    sonicDestroyStream(stream);
    return result;
}
void rate_checks(ISpTTSEngine* engine,const WAVEFORMATEX* wave,SPVTEXTFRAG* fragments,const wchar_t* reference_path) {
    std::ifstream reference(reference_path,std::ios::binary);
    std::vector<float> floats(326400);
    reference.read(reinterpret_cast<char*>(floats.data()),static_cast<std::streamsize>(floats.size()*sizeof(float)));
    check(reference.gcount()==static_cast<std::streamsize>(floats.size()*sizeof(float)) && reference.peek()==EOF,
          "Rate reference missing or wrong shape");
    std::vector<std::int16_t> original(floats.size()); outloud::convert_pcm16(floats,original);
    const auto* original_bytes = reinterpret_cast<const std::uint8_t*>(original.data());
    std::uint64_t fast_before_flush = 0;
    for (const long rate : {-10L,0L,10L}) {
        Site site(rate==10 ? Mode::partial : Mode::normal,true);
        site.rate=rate; site.variable_frames=rate!=0;
        speak(engine,wave,fragments,site); site.pcm.resize(static_cast<std::size_t>(site.bytes));
        check(site.rates==1 && site.bytes && !site.remaining,"Initial site rate/output delivery differs");
        if (!rate) {
            validate(site); check(site.calls==170,"Zero rate changed direct frame delivery");
            check(site.bytes==original.size()*2 && std::memcmp(site.pcm.data(),original.data(),site.pcm.size())==0,
                  "Zero rate is not bit-identical to original native PCM16");
        } else {
            const auto expected = sonic_reference(original,rate);
            check(site.pcm==expected.pcm,"SAPI rate PCM differs from continuous Sonic including flushed tail");
            const double ratio = double(site.bytes)/(original.size()*2);
            check(rate<0 ? ratio>2.9 && ratio<3.1 : ratio>.32 && ratio<.35,"Rate duration differs from requested tempo");
            if (rate>0) { fast_before_flush=expected.before_flush_bytes; }
        }
        std::cout << "Rate " << rate << ": " << site.bytes/2 << " samples, " << double(site.bytes)/48000.0
                  << " seconds; initial rate, caller thread, completion and PCM checks passed\n";
    }

    // A return to zero must deliver the old buffered tail once, then the exact
    // raw suffix. Repeated finish calls must not duplicate that tail.
    const auto prefix = std::span<const std::int16_t>(original).first(20*1920);
    auto expected_transition = sonic_reference(prefix,-10).pcm;
    expected_transition.insert(expected_transition.end(),original_bytes+prefix.size_bytes(),original_bytes+original.size()*2);
    std::vector<std::uint8_t> transition;
    outloud::sapi::SpeechRate processor;
    const auto sink = [&](std::span<const std::int16_t> pcm) {
        const auto* bytes = reinterpret_cast<const std::uint8_t*>(pcm.data());
        transition.insert(transition.end(),bytes,bytes+pcm.size_bytes()); return S_OK;
    };
    for (std::size_t offset=0;offset<original.size();offset+=1920) {
        hr(processor.write(std::span<const std::int16_t>(original).subspan(offset,1920),offset<prefix.size() ? -10 : 0,sink),
           "Rate-to-zero transition failed");
    }
    hr(processor.finish(sink),"Transition finish failed"); hr(processor.finish(sink),"Repeated finish failed");
    check(transition==expected_transition,"Return to zero lost/duplicated buffered tail or changed raw suffix");

    Site changing(Mode::partial,true); changing.variable_frames=true;
    changing.rate_changes={{20*3840,-10},{30*3840,10},{40*3840,0}};
    speak(engine,wave,fragments,changing); changing.pcm.resize(static_cast<std::size_t>(changing.bytes));
    check(changing.next_rate_change==3 && changing.rates==4 && !changing.remaining && changing.bytes>=3840,
          "Live SAPI rate changes not consumed");
    check(std::memcmp(changing.pcm.data()+changing.pcm.size()-3840,original_bytes+original.size()*2-3840,3840)==0,
          "Live return to zero changed the final raw frame");
    std::cout << "PASS: live 0 -> -10 -> +10 -> 0, partial writes and exact buffered-tail transition\n";

    for (const long rate : {-10L,10L}) {
        Site aborted(Mode::partial,false); aborted.variable_frames=true; aborted.rate=rate;
        // Slow: abort during ordinary delivery. Fast: abort during final flush.
        aborted.abort_bytes=rate<0 ? 126 : fast_before_flush+2;
        speak(engine,wave,fragments,aborted);
        check(aborted.abort_observations && !aborted.write_after_abort &&
              (rate<0 ? aborted.bytes==126 : aborted.bytes>fast_before_flush),"Sonic cancellation wrote after abort or missed flush");
    }
    Site recovered(Mode::normal,true); speak(engine,wave,fragments,recovered); validate(recovered);
    check(std::memcmp(recovered.pcm.data(),original.data(),recovered.pcm.size())==0,"Cancellation leaked Sonic state into next Speak");
    std::cout << "PASS: cancellation during delivery/final flush and bit-identical zero-rate recovery\n";
}

int wmain(int argc,wchar_t** argv) {
    HMODULE module = nullptr; WAVEFORMATEX* wave = nullptr;
    try {
        if (argc==2 && std::wstring_view(argv[1])==L"--input-check") { collection_checks(); return 0; }
        if (argc==3 && std::wstring_view(argv[1])==L"--diagnostics-check") { diagnostic_checks(argv[2]); return 0; }
        check(argc==6 || argc==7,"Usage: outloud-sapi-check DLL model.olp tokenizer.bin voice.olv native-reference.f32 [bench|observe|rate]");
        const bool benchmark = argc==7 && std::wstring_view(argv[6])==L"bench";
        const bool observation = argc==7 && std::wstring_view(argv[6])==L"observe";
        const bool rate_only = argc==7 && std::wstring_view(argv[6])==L"rate";
        check(argc==6 || benchmark || observation || rate_only,"Unknown mode");
        if (!rate_only) { text_checks(); }
        const auto path = std::filesystem::absolute(argv[1]);
        module = LoadLibraryExW(path.c_str(),nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_SYSTEM32);
        check(module!=nullptr,"Cannot load adapter DLL");
        using GetClass = HRESULT (__stdcall*)(REFCLSID,REFIID,void**);
        using CanUnload = HRESULT (__stdcall*)();
        const auto get = reinterpret_cast<GetClass>(GetProcAddress(module,"DllGetClassObject"));
        const auto unload = reinterpret_cast<CanUnload>(GetProcAddress(module,"DllCanUnloadNow"));
        check(get && unload,"Required exports missing"); hr(unload(),"Initial module busy");
        Com<IClassFactory> factory;
        hr(get(outloud::sapi::engine_clsid,IID_IClassFactory,reinterpret_cast<void**>(factory.put())),"Class factory failed");
        check(unload()==S_FALSE,"Factory not counted"); hr(factory->LockServer(TRUE),"LockServer failed");
        Com<ISpTTSEngine> engine; hr(factory->CreateInstance(nullptr,__uuidof(ISpTTSEngine),reinterpret_cast<void**>(engine.put())),"Create engine failed");
        void* unknown = nullptr;
        check(factory->CreateInstance(static_cast<IUnknown*>(factory.p),IID_IUnknown,&unknown)==CLASS_E_NOAGGREGATION && !unknown,"Aggregation accepted");
        Com<ISpObjectWithToken> with;
        hr(engine->QueryInterface(__uuidof(ISpObjectWithToken),reinterpret_cast<void**>(with.put())),"Token interface missing");
        Com<IUnknown> identity1, identity2;
        hr(engine->QueryInterface(IID_IUnknown,reinterpret_cast<void**>(identity1.put())),"IUnknown failed");
        hr(with->QueryInterface(IID_IUnknown,reinterpret_cast<void**>(identity2.put())),"Token IUnknown failed");
        check(identity1.p==identity2.p,"COM identity differs"); identity1.reset(); identity2.reset();
        GUID format{}; hr(engine->GetOutputFormat(nullptr,nullptr,&format,&wave),"Format failed");
        check(format==SPDFID_WaveFormatEx && outloud::sapi::valid_format(wave),"WAVEFORMATEX fields differ");
        Com<TestToken> token; token.p = new TestToken;
        token->values = {{L"AssetRoot",path.parent_path().parent_path().native()},
            {L"ModelPath",std::filesystem::absolute(argv[2]).native()}, {L"TokenizerPath",std::filesystem::absolute(argv[3]).native()},
            {L"VoicePath",std::filesystem::absolute(argv[4]).native()},{L"MaximumSegmentTokens",L"51"},{L"KernelPath",L"Auto"}};
        hr(with->SetObjectToken(token.p),"SetObjectToken failed");
        Com<ISpObjectToken> returned; hr(with->GetObjectToken(returned.put()),"GetObjectToken failed"); check(returned.p==token.p,"Returned token differs"); returned.reset();
        auto first = fragment(text.substr(0,38)), second = fragment(text.substr(38,59)), third = fragment(text.substr(97));
        first.pNext = &second; second.pNext = &third;
        std::uint64_t partial_calls = 0;
        std::array<double,3> first_times{}, totals{};
        struct AbortResult { const char* name; std::uint64_t bytes; ULONG writes, observations; double latency; };
        std::vector<AbortResult> abort_results;
        if (rate_only) {
            rate_checks(engine.p,wave,&first,argv[5]);
        } else if (benchmark || observation) {
            Site warm(Mode::normal,false); speak(engine.p,wave,&first,warm); validate(warm);
            for (std::size_t i = 0; i < (observation ? 1u : 3u); ++i) {
                Site site(Mode::normal,false); speak(engine.p,wave,&first,site); validate(site);
                check(site.calls==170,"Normal Write count differs"); first_times[i] = site.first_ms; totals[i] = site.total_ms;
            }
        } else {
            bookmark_checks(engine.p,wave);
            Site a(Mode::normal,true); speak(engine.p,wave,&first,a); validate(a); check(a.calls==170,"Normal frame Write count differs");
            auto short_text = fragment(L"Hello there.");
            for (const auto mode : {Mode::failed,Mode::zero,Mode::odd,Mode::oversized,Mode::stopped}) {
                Site failed(mode,false);
                speak(engine.p,wave,&short_text,failed,mode==Mode::failed ? E_ACCESSDENIED : mode==Mode::stopped ? SP_AUDIO_STOPPED : E_UNEXPECTED);
            }
            Site b(Mode::partial,true); speak(engine.p,wave,&first,b); validate(b);
            check(a.pcm==b.pcm,"A/B reuse or partial Write changed PCM"); partial_calls = b.calls;
            std::ifstream reference(argv[5],std::ios::binary); check(bool(reference),"Native reference missing");
            std::vector<float> pcm(326400); reference.read(reinterpret_cast<char*>(pcm.data()),static_cast<std::streamsize>(pcm.size()*4));
            check(reference.gcount()==static_cast<std::streamsize>(pcm.size()*4) && reference.peek()==EOF,"Native reference shape differs");
            std::vector<std::int16_t> expected(pcm.size()); outloud::convert_pcm16(pcm,expected);
            check(std::memcmp(expected.data(),a.pcm.data(),652800)==0,"SAPI PCM16 differs from native listening conversion");
            real_host_checks(factory.p,*token.p,a.pcm);
            auto end_bookmark = bookmark(L"End"); third.pNext = &end_bookmark;
            Site real_shape(Mode::normal,true); real_shape.event_interest = 0;
            real_shape.interest_result = E_ACCESSDENIED; real_shape.fail_event_after = 0;
            speak(engine.p,wave,&first,real_shape); validate(real_shape);
            check(real_shape.pcm==a.pcm && real_shape.calls==170 && !real_shape.interest_queries,
                  "Host trailing bookmark changed canonical PCM or required event methods");
            third.pNext = nullptr;
            const auto residency = Residency::capture(argv[2],argv[4],true);
            const auto recover = [&] {
                check(Residency::capture(argv[2],argv[4]).retains(residency),"Cancellation lost resident asset bases/worker IDs");
                Site recovered(Mode::normal,true); speak(engine.p,wave,&first,recovered); validate(recovered);
                check(recovered.pcm==a.pcm,"Cancellation recovery PCM differs");
                check(Residency::capture(argv[2],argv[4]).retains(residency),"Recovery lost resident asset bases/worker IDs");
            };
            struct AbortCase { const char* name; std::uint64_t bytes; bool entry, timed, partial; };
            for (const auto c : std::array<AbortCase,10>{{
                {"entry",0,true,false,false}, {"first_pcm_wait",0,false,true,false},
                {"first_frame",3840,false,false,false}, {"segment1",38400,false,false,false},
                {"before_segment_boundary",45*3840,false,false,false}, {"segment_boundary",46*3840,false,false,false},
                {"segment2",70*3840,false,false,false}, {"near_eos",169*3840,false,false,false},
                {"natural_end",170*3840,false,false,false}, {"partial_write",126,false,false,true}
            }}) {
                Site aborted(c.partial ? Mode::partial : Mode::normal,false);
                aborted.entry_abort = c.entry; aborted.timed_abort = c.timed;
                aborted.late_abort = c.bytes==652800;
                if (!c.entry && !c.timed) { aborted.abort_bytes = c.bytes; }
                speak(engine.p,wave,&first,aborted);
                check(aborted.abort_observations>0 && !aborted.write_after_abort && aborted.bytes==c.bytes,"Abort delivery boundary differs");
                if (c.bytes==3840) { check(aborted.abort_observations>=2,"Repeated abort observation not exercised"); }
                abort_results.push_back({c.name,aborted.bytes,aborted.calls,aborted.abort_observations,aborted.abort_latency_ms});
                std::cout << "Abort " << c.name << ": " << aborted.bytes << " bytes, quiesced in " << aborted.abort_latency_ms << " ms\n";
                recover();
            }
            Site controls(Mode::normal,true); controls.controls = true; controls.rate=2; controls.variable_frames=true;
            speak(engine.p,wave,&first,controls);
            check(controls.rates==1 && controls.volumes==1 && controls.skips==1 && controls.skip_completions==1 && controls.bytes<a.bytes,
                  "Skip/rate/volume policy differs");
            auto unsupported = fragment(L"Text"); unsupported.State.eAction = SPVA_Pronounce;
            Site unused(Mode::normal,false); speak(engine.p,wave,&unsupported,unused,E_NOTIMPL); check(!unused.calls,"Unsupported fragment produced audio");
            std::wstring invalid(1,wchar_t(0xd800)); auto malformed = fragment(invalid);
            speak(engine.p,wave,&malformed,unused,HRESULT_FROM_WIN32(ERROR_NO_UNICODE_TRANSLATION));
            check(!unused.calls,"Invalid UTF16 produced audio");
            unsupported.State.eAction = SPVA_Speak; unsupported.pNext = &unsupported;
            speak(engine.p,wave,&unsupported,unused,E_INVALIDARG); unsupported.pNext = nullptr;
            first_times[0] = a.first_ms; totals[0] = a.total_ms;
        }
        CoTaskMemFree(wave); wave = nullptr;
        with.reset(); engine.reset(); token.reset();
        check(!TestToken::alive,"Token references leaked");
        check(unload()==S_FALSE,"Server lock not counted"); hr(factory->LockServer(FALSE),"Unlock failed");
        check(factory->LockServer(FALSE)==E_UNEXPECTED,"Unbalanced unlock accepted"); factory.reset(); hr(unload(),"Module not unloadable after release");
        FreeLibrary(module); module = nullptr;
        if (rate_only) {
            std::cout << "PASS: focused SAPI rate checks; registration-free, no installation or registry mutations\n";
            return 0;
        }
        const auto directory = std::filesystem::absolute(argv[2]).parent_path().parent_path()/"perf";
        std::filesystem::create_directories(directory);
        std::ofstream report(directory/(observation ? "2n-normal-observation.json" : benchmark ? "2m-sapi-benchmark.json" : "2o-bugfix-sapi-check.json"));
        report << std::setprecision(17) << "{\"registration_free\":true,\"no_registry_mutations\":true,\"frames\":170,\"samples\":326400,\"audio_bytes\":652800,"
            "\"normal_write_calls\":170,\"partial_write_calls\":" << partial_calls << ",\"caller_thread_site\":true,\"module_unloadable\":true,\"warmups\":" << (benchmark || observation ? 1 : 0)
            << ",\"timed_repetitions\":" << (observation ? 1 : benchmark ? 3 : 0) << ",\"first_write_ms\":[";
        for (std::size_t i = 0; i < (benchmark ? 3u : 1u); ++i) { if (i) { report << ','; } report << first_times[i]; }
        report << "],\"total_speak_ms\":[";
        for (std::size_t i = 0; i < (benchmark ? 3u : 1u); ++i) { if (i) { report << ','; } report << totals[i]; }
        report << "],\"stop_source_bytes\":" << sizeof(std::stop_source) << ",\"stop_token_bytes\":" << sizeof(std::stop_token)
               << ",\"msvc_shared_stop_state_bytes\":" << sizeof(std::_Stop_state)
               << ",\"stop_callback_bytes\":" << sizeof(std::stop_callback<void(*)()>)
               << ",\"engine_gate_cv_bytes\":" << sizeof(std::condition_variable) << ",\"aborts\":[";
        for (std::size_t i = 0; i < abort_results.size(); ++i) {
            const auto& r = abort_results[i]; if (i) { report << ','; }
            report << "{\"case\":\"" << r.name << "\",\"bytes\":" << r.bytes << ",\"writes\":" << r.writes
                   << ",\"observations\":" << r.observations << ",\"abort_to_quiesce_ms\":" << r.latency << ",\"same_resident_recovery\":true}";
        }
        report << "],\"leading_bookmark_checks\":" << (benchmark || observation ? "false" : "true")
               << ",\"real_sapi_checks\":" << (benchmark || observation ? "false" : "true") << "}\n"; check(bool(report),"Cannot write report");
        std::cout << "PASS: registration-free SAPI, 170 frames / 652800 bytes, caller-thread streaming, COM/module release"
                  << (observation ? "; one warm-up plus one normal observation.\n" : benchmark ? "; one warm-up plus three timed Speak calls.\n" : "; exact native PCM16, abort/recovery cases, 5270 partial Writes.\n");
        return 0;
    } catch (const std::exception& e) {
        if (wave) { CoTaskMemFree(wave); } if (module) { FreeLibrary(module); }
        std::cerr << e.what() << '\n'; return 1;
    }
}
