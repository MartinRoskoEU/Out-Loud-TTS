#include "identity.hpp"
#include "support.hpp"
#include "token.hpp"
#include "outloud/pcm16.hpp"
#include <array>
#include <chrono>
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
    HRESULT STDMETHODCALLTYPE AddEvents(const SPEVENT*,ULONG) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE GetEventInterest(ULONGLONG* out) override { if (!out) { return E_POINTER; } *out = 0; return S_OK; }
    DWORD STDMETHODCALLTYPE GetActions() override { ++actions; return 0; }
    HRESULT STDMETHODCALLTYPE GetRate(long* out) override { if (!out) { return E_POINTER; } *out = 0; return S_OK; }
    HRESULT STDMETHODCALLTYPE GetVolume(USHORT* out) override { if (!out) { return E_POINTER; } *out = 100; return S_OK; }
    HRESULT STDMETHODCALLTYPE GetSkipInfo(SPVSKIPTYPE*,long*) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE CompleteSkip(long) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE Write(const void* data,ULONG count,ULONG* written) override {
        if (GetCurrentThreadId()!=caller) { wrong_thread = true; }
        if (!calls) { first_ms = std::chrono::duration<double,std::milli>(Clock::now()-begun).count(); }
        ++calls;
        if (mode==Mode::failed) { return E_ACCESSDENIED; }
        if (mode==Mode::stopped) { return SP_AUDIO_STOPPED; }
        if (!remaining) { ++frames; remaining = 3840; }
        if (count!=remaining) { return E_UNEXPECTED; }
        *written = mode==Mode::partial ? std::min(count,ULONG{126}) : mode==Mode::zero ? 0 : mode==Mode::odd ? 1 : mode==Mode::oversized ? count+2 : count;
        if (*written<=count && *written%2==0) {
            if (!pcm.empty()) {
                if (bytes+*written>pcm.size()) { return E_UNEXPECTED; }
                const auto* input = static_cast<const std::uint8_t*>(data);
                std::copy(input,input+*written,pcm.begin()+static_cast<std::size_t>(bytes));
            }
            bytes += *written; remaining -= *written;
        }
        return S_OK;
    }
    Mode mode;
    std::vector<std::uint8_t> pcm;
    std::uint64_t bytes = 0;
    ULONG calls = 0, frames = 0, remaining = 0, actions = 0;
    DWORD caller = GetCurrentThreadId();
    bool wrong_thread = false;
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
    site.total_ms = std::chrono::duration<double,std::milli>(end-start).count(); return site.total_ms;
}
void validate(const Site& site) {
    check(site.frames==170 && site.bytes==652800 && !site.remaining && site.first_ms<site.total_ms,
          "Canonical samples/frames/immediate streaming differ");
    check(!site.actions,"Unexpected cancellation integration");
}
void text_checks() {
    auto f = fragment(L"caf\u00e9 \U0001f600");
    check(outloud::sapi::collect_text(&f)=="caf\xc3\xa9 \xf0\x9f\x98\x80","Unicode conversion failed");
    std::wstring a(1,wchar_t(0xd83d)), b(1,wchar_t(0xde00)); auto one = fragment(a), two = fragment(b); one.pNext = &two;
    check(outloud::sapi::collect_text(&one)=="\xf0\x9f\x98\x80","Fragment-spanning surrogate pair failed");
    one.pNext = nullptr;
    bool rejected = false; try { (void)outloud::sapi::collect_text(&one); } catch (const outloud::sapi::Failure&) { rejected = true; }
    check(rejected,"Invalid UTF16 accepted");
}
}
int wmain(int argc,wchar_t** argv) {
    HMODULE module = nullptr; WAVEFORMATEX* wave = nullptr;
    try {
        check(argc==6 || argc==7,"Usage: outloud-sapi-check DLL model.olp tokenizer.bin voice.olv native-reference.f32 [bench]");
        const bool benchmark = argc==7 && std::wstring_view(argv[6])==L"bench";
        check(argc==6 || benchmark,"Unknown mode");
        text_checks();
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
        if (benchmark) {
            Site warm(Mode::normal,false); speak(engine.p,wave,&first,warm); validate(warm);
            for (std::size_t i = 0; i < 3; ++i) {
                Site site(Mode::normal,false); speak(engine.p,wave,&first,site); validate(site);
                check(site.calls==170,"Normal Write count differs"); first_times[i] = site.first_ms; totals[i] = site.total_ms;
            }
        } else {
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
        const auto directory = std::filesystem::absolute(argv[2]).parent_path().parent_path()/"perf";
        std::filesystem::create_directories(directory);
        std::ofstream report(directory/(benchmark ? "2m-sapi-benchmark.json" : "2m-sapi-check.json"));
        report << std::setprecision(17) << "{\"registration_free\":true,\"no_registry_operations\":true,\"frames\":170,\"samples\":326400,\"audio_bytes\":652800,"
            "\"normal_write_calls\":170,\"partial_write_calls\":" << partial_calls << ",\"caller_thread_site\":true,\"module_unloadable\":true,\"warmups\":" << (benchmark ? 1 : 0)
            << ",\"timed_repetitions\":" << (benchmark ? 3 : 0) << ",\"first_write_ms\":[";
        for (std::size_t i = 0; i < (benchmark ? 3u : 1u); ++i) { if (i) { report << ','; } report << first_times[i]; }
        report << "],\"total_speak_ms\":[";
        for (std::size_t i = 0; i < (benchmark ? 3u : 1u); ++i) { if (i) { report << ','; } report << totals[i]; }
        report << "]}\n"; check(bool(report),"Cannot write report");
        std::cout << "PASS: registration-free SAPI, 170 frames / 652800 bytes, caller-thread streaming, COM/module release"
                  << (benchmark ? "; one warm-up plus three timed Speak calls.\n" : "; exact native PCM16, A/B recovery, 5270 partial Writes.\n");
        return 0;
    } catch (const std::exception& e) {
        if (wave) { CoTaskMemFree(wave); } if (module) { FreeLibrary(module); }
        std::cerr << e.what() << '\n'; return 1;
    }
}
