#include "identity.hpp"
#include "support.hpp"
#include "token.hpp"
#include "speech_rate.hpp"
#include "scratch.hpp"
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
            const auto* input = static_cast<const std::uint8_t*>(data);
            nonzero_pcm = nonzero_pcm || std::any_of(input,input+*written,[](auto byte) { return byte!=0; });
            if (!pcm.empty()) {
                if (bytes+*written>pcm.size()) {
                    if (!variable_frames) { return E_UNEXPECTED; }
                    pcm.resize(static_cast<std::size_t>(bytes+*written));
                }
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
    bool nonzero_pcm = false;
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
} // namespace
#include "rust_sapi_checks.hpp"
int wmain(int argc,wchar_t** argv) {
    try {
        if (argc==6 && (std::wstring_view(argv[1])==L"--rust" || std::wstring_view(argv[1])==L"--managed-rust")) { return rust_sapi_checks(argv); }
        std::cerr << "Supply --rust DLL RUNTIME CONFIGURATION PROFILE or --managed-rust DLL RUNTIME DATA_ROOT PROFILE. No registry writes.\n";
        return 2;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
