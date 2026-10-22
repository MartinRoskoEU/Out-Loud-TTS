#include "identity.hpp"
#include "support.hpp"
#include "asset_diagnostics.hpp"
#include "error_logger.hpp"
#include "speech_rate.hpp"
#include "speech_volume.hpp"
#include "outloud/orchestration.hpp"
#include "outloud/pcm16.hpp"
#include <atomic>
#include <condition_variable>
#include <cstdlib>
#include <filesystem>
#include <mutex>
#include <thread>

namespace outloud::sapi {
namespace {
std::atomic<long> objects{0}, locks{0};
HMODULE module = nullptr;
HRESULT logged_result(logging::Event event,HRESULT hr) noexcept {
    if (FAILED(hr)) {
        logging::error({event,hr,HRESULT_FACILITY(hr)==FACILITY_WIN32 ? HRESULT_CODE(hr) : 0UL});
    }
    return hr;
}
HRESULT exception_code(bool publish=true,logging::Event event=logging::Event::synthesis) noexcept {
    const auto log=[&](HRESULT hr,logging::Kind kind,DWORD system=0,logging::Component component=logging::Component::native) {
        if (publish) { logging::error({event,hr,system,component,kind}); }
        return hr;
    };
    try { throw; }
    catch (const AssetError& e) {
        const auto detail=std::string_view(e.what());
        const auto component=detail.starts_with("model: ") ? logging::Component::model :
            detail.starts_with("tokenizer: ") ? logging::Component::tokenizer :
            detail.starts_with("voice: ") ? logging::Component::voice : logging::Component::assets;
        const auto kind=e.code==AssetErrorCode::io ? logging::Kind::asset_io :
            e.code==AssetErrorCode::invalid_data ? logging::Kind::asset_invalid : logging::Kind::asset_compatibility;
        (void)log(asset_hresult(e),kind,e.system_error,component);
        return publish ? report_asset_error(e) : asset_hresult(e);
    }
    catch (const Failure& e) {
        if (publish) { (void)logged_result(event,e.code); } return e.code;
    }
    catch (const std::bad_alloc&) { return log(E_OUTOFMEMORY,logging::Kind::allocation); }
    catch (const std::invalid_argument&) { return log(E_INVALIDARG,logging::Kind::invalid_argument); }
    catch (const std::filesystem::filesystem_error& e) {
        const auto system=static_cast<DWORD>(e.code().value());
        return log(HRESULT_FROM_WIN32(system),logging::Kind::filesystem,system);
    }
    catch (...) { return log(E_FAIL,logging::Kind::unexpected); }
}
std::wstring token_string(ISpObjectToken* token, const wchar_t* name, bool optional = false) {
    wchar_t* raw = nullptr;
    const auto hr = token->GetStringValue(name,&raw);
    if (FAILED(hr)) {
        if (optional && (hr==SPERR_NOT_FOUND || hr==HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND))) { return {}; }
        throw Failure{hr};
    }
    try { std::wstring value(raw ? raw : L""); CoTaskMemFree(raw); return value; }
    catch (...) { CoTaskMemFree(raw); throw; }
}
std::uint64_t number(std::wstring_view text, std::uint64_t fallback) {
    if (text.empty()) { return fallback; }
    std::uint64_t value = 0;
    for (wchar_t digit : text) {
        if (digit<L'0' || digit>L'9' || value>(UINT64_MAX-static_cast<unsigned>(digit-L'0'))/10) { throw Failure{E_INVALIDARG}; }
        value = value*10+static_cast<unsigned>(digit-L'0');
    }
    return value;
}
struct Configuration {
    std::filesystem::path model, tokenizer, voice;
    std::uint64_t seed = 73;
    std::uint32_t tokens = 128;
    cpu::KernelPath path = cpu::KernelPath::automatic;
    bool same_runtime(const Configuration& other) const {
        return model==other.model && tokenizer==other.tokenizer && voice==other.voice && tokens==other.tokens && path==other.path;
    }
};
Configuration read_configuration(ISpObjectToken* token) {
    auto root = std::filesystem::path(token_string(token,L"AssetRoot",true));
    if (root.empty()) {
        std::wstring name(32768,L'\0');
        const auto length = GetModuleFileNameW(module,name.data(),static_cast<DWORD>(name.size()));
        if (!length || length==name.size()) { throw Failure{HRESULT_FROM_WIN32(ERROR_INSUFFICIENT_BUFFER)}; }
        name.resize(length); root = std::filesystem::path(name).parent_path().parent_path();
    }
    if (!root.is_absolute()) { throw Failure{E_INVALIDARG}; }
    auto resolve = [&](const wchar_t* key, const wchar_t* fallback, bool optional) {
        auto name = token_string(token,key,optional);
        if (name.empty()) { name = fallback; }
        std::filesystem::path path(name);
        if (path.empty()) { throw Failure{E_INVALIDARG}; }
        if (!path.is_absolute()) { path = root/path; }
        path = path.lexically_normal();
        const auto native = path.native();
        // Local drive files only: reject device/UNC paths and alternate streams.
        // Absolute managed voices can live anywhere outside the deployment.
        if (native.size()<3 || native[1]!=L':' || native[2]!=L'\\' ||
            native.find(L':',2)!=std::wstring::npos || native.find(L'\0')!=std::wstring::npos) { throw Failure{E_INVALIDARG}; }
        if (std::wstring_view(key)==L"VoicePath" && _wcsicmp(path.extension().c_str(),L".olv")!=0) { throw Failure{E_INVALIDARG}; }
        readable_asset(path,std::wstring_view(key)==L"ModelPath" ? "model" : std::wstring_view(key)==L"TokenizerPath" ? "tokenizer" : "voice");
        return path;
    };
    Configuration c;
    c.model = resolve(L"ModelPath",L"runtime/pocket-english.olp",true);
    c.tokenizer = resolve(L"TokenizerPath",L"runtime/pocket-english-tokenizer.bin",true);
    c.voice = resolve(L"VoicePath",L"",false);
    c.seed = number(token_string(token,L"Seed",true),73);
    const auto count = number(token_string(token,L"MaximumSegmentTokens",true),128);
    if (!count || count > 4095 || count+FlowLMGenerator::estimate_max_gen_len(static_cast<std::size_t>(count)) > 4096) { throw Failure{E_INVALIDARG}; }
    c.tokens = static_cast<std::uint32_t>(count);
    const auto path = token_string(token,L"KernelPath",true);
    if (path==L"Portable") { c.path = cpu::KernelPath::portable; }
    else if (path==L"AVX2") { c.path = cpu::KernelPath::avx2_fma; }
    else if (!path.empty() && path!=L"Auto") { throw Failure{E_INVALIDARG}; }
    return c;
}
struct Resident {
    PocketModelAsset model;
    OutLoudVoice voice;
    Tokenizer tokenizer;
    UtterancePipeline pipeline; // destroyed first: joins workers before assets/tokenizer
    explicit Resident(const Configuration& c)
        : model(load_asset<PocketModelAsset>(c.model,"model")), voice(load_asset<OutLoudVoice>(c.voice,"voice")), tokenizer(load_asset<Tokenizer>(c.tokenizer,"tokenizer")),
          pipeline(model,voice,tokenizer,c.tokens,FlowLMGenerator::estimate_max_gen_len(c.tokens),c.path) {}
};
struct RequestControl {
    ISpTTSEngineSite* site;
    std::stop_source cancellation;
    bool aborted = false;
    bool rate_initialized = false;
    long rate = 0;
    bool volume_initialized = false;
    ULONG volume = 100, fragment_volume = 100;
    unsigned control_errors = 0;
    void control_result(logging::Event event,HRESULT hr,unsigned bit) {
        if (FAILED(hr) && !(control_errors&bit)) {
            control_errors|=bit; (void)logged_result(event,hr);
        }
    }
    bool poll() {
        const auto actions = site->GetActions(); // always Speak caller thread
        if ((actions & SPVES_ABORT) && !aborted) { aborted = true; cancellation.request_stop(); }
        // Read initial site rate even when SAPI has no pending change action.
        if (!rate_initialized || (actions & SPVES_RATE)) {
            long value = 0;
            const auto hr = site->GetRate(&value);
            control_result(logging::Event::rate,hr,1);
            if (SUCCEEDED(hr)) { rate = std::clamp(value,-10L,10L); }
            rate_initialized = true;
        }
        if (!volume_initialized || (actions & SPVES_VOLUME)) {
            USHORT value = 100;
            const auto hr = site->GetVolume(&value);
            control_result(logging::Event::volume,hr,2);
            if (SUCCEEDED(hr)) { volume = (std::min)(ULONG{value},100UL); }
            // A failed query retains the last valid level (initially SAPI's 100).
            volume_initialized = true;
        }
        if (actions & SPVES_SKIP) {
            SPVSKIPTYPE type{}; long count = 0;
            control_result(logging::Event::skip_info,site->GetSkipInfo(&type,&count),4);
            control_result(logging::Event::skip_complete,site->CompleteSkip(0),8);
        }
        return aborted;
    }
};
constexpr auto poll_interval = std::chrono::milliseconds(10);
HRESULT leading_bookmarks(RequestControl& control, const std::vector<std::wstring>& names) {
    if (names.empty()) { return S_OK; }
    ULONGLONG interest = 0;
    auto hr = control.site->GetEventInterest(&interest);
    if (FAILED(hr)) { return logged_result(logging::Event::bookmark_interest,hr); }
    // SPFEI includes reserved flag-check bits; test only the bookmark bit.
    if (!(interest & (1ULL << SPEI_TTS_BOOKMARK))) { return S_OK; }
    for (const auto& name : names) {
        if (control.poll()) { return S_OK; }
        SPEVENT event{};
        event.eEventId = SPEI_TTS_BOOKMARK;
        event.elParamType = SPET_LPARAM_IS_STRING;
        // SAPI supplies ulStreamNum. Offset 0 is relative to this Speak's audio,
        // and events must be queued before their associated audio is written.
        event.ullAudioStreamOffset = 0;
        event.wParam = static_cast<WPARAM>(_wtol(name.c_str()));
        event.lParam = reinterpret_cast<LPARAM>(name.c_str());
        // Microsoft samplettsengine frees its input string after AddEvents:
        // the site copies it. Our owned string remains alive through this call.
        hr = control.site->AddEvents(&event,1);
        if (FAILED(hr)) { return logged_result(logging::Event::bookmark_delivery,hr); }
    }
    return S_OK;
}
// Chatterbox's host checks require every site call on the Speak caller thread.
// One joined coordinator runs the unchanged core; one-frame rendezvous retains
// caller-thread delivery without COM proxies or extra neural/audio workers.
struct Delivery {
    std::mutex mutex;
    std::condition_variable changed;
    std::array<std::int16_t,1920> pcm{};
    bool ready = false, done = false;
    HRESULT output_error = S_OK, native_error = S_OK;
    std::exception_ptr native_failure;
    SynthesisStatus native_status = SynthesisStatus::completed;
    std::stop_token cancellation;
    explicit Delivery(std::stop_token stop) : cancellation(stop) {}
    static void frame(void* context, std::uint64_t, std::size_t, std::uint32_t, std::span<const float> input) {
        auto& d = *static_cast<Delivery*>(context);
        std::unique_lock lock(d.mutex);
        d.changed.wait(lock,[&] { return !d.ready || d.output_error!=S_OK || d.cancellation.stop_requested(); });
        if (d.output_error!=S_OK) { throw Failure{d.output_error}; }
        if (d.cancellation.stop_requested()) { return; }
        convert_pcm16(input,d.pcm);
        if (d.cancellation.stop_requested()) { return; }
        d.ready = true; d.changed.notify_one();
        d.changed.wait(lock,[&] { return !d.ready || d.output_error!=S_OK || d.cancellation.stop_requested(); });
        if (d.output_error!=S_OK) { throw Failure{d.output_error}; }
    }
};
HRESULT write_frame(RequestControl& control, std::span<const std::int16_t> pcm) {
    const auto* bytes = reinterpret_cast<const std::uint8_t*>(pcm.data());
    std::array<std::int16_t,1920> adjusted;
    ULONG delivered = 0;
    const auto total = static_cast<ULONG>(pcm.size_bytes());
    while (delivered<total) {
        if (control.poll()) { return S_OK; } // including every partial retry
        const auto gain = volume_gain(control.volume,control.fragment_volume);
        const auto remaining = (std::min)(total-delivered,static_cast<ULONG>(adjusted.size()*sizeof(std::int16_t)));
        const void* output = bytes+delivered;
        if (gain!=10000) {
            // Always start from unscaled, unwritten samples. A partial Write may
            // be followed by a new volume action; never scale the source in place.
            for (ULONG i=0;i<remaining/2;++i) { adjusted[i]=scale_pcm16(pcm[delivered/2+i],gain); }
            output=adjusted.data();
        }
        ULONG written = 0;
        const auto hr = control.site->Write(output,remaining,&written);
        if (FAILED(hr) || hr==SP_AUDIO_STOPPED) { return hr; }
        if (!written || written>remaining || written%2) { return E_UNEXPECTED; }
        delivered += written;
    }
    // Observe a stop signalled by the last Write before acknowledging the sink;
    // otherwise the coordinator could dispatch the next segment during our wait.
    (void)control.poll();
    return S_OK;
}
HRESULT stream(std::unique_ptr<Resident>& resident, const Configuration& configuration,
        std::string_view text, RequestControl& control) {
    Delivery d(control.cancellation.get_token());
    SpeechRate speech_rate;
    const auto output = [&](std::span<const std::int16_t> pcm) {
        return write_frame(control,pcm);
    };
    std::stop_callback wake(d.cancellation,[&d] {
        { std::lock_guard lock(d.mutex); }
        d.changed.notify_all();
    });
    std::thread coordinator([&] {
        HRESULT hr = S_OK;
        SynthesisStatus status = SynthesisStatus::completed;
        try {
            if (!d.cancellation.stop_requested()) {
                if (!resident) { resident = std::make_unique<Resident>(configuration); }
                if (!d.cancellation.stop_requested()) {
                    resident->pipeline.prepare(text,configuration.seed);
                    if (!d.cancellation.stop_requested()) {
#ifdef OUTLOUD_ERROR_LOGGER_TEST
                        const auto fault=logging::fault();
                        const GenerationObserver observer=[fault](const GenerationCall&) {
                            if (fault==logging::Fault::worker_allocation) { throw std::bad_alloc{}; }
                            throw std::runtime_error("PRIVATE_WORKER_MESSAGE C:\\Users\\private\\secret");
                        };
                        const auto probe=fault==logging::Fault::worker_exception || fault==logging::Fault::worker_allocation ? &observer : nullptr;
                        status = resident->pipeline.run(Delivery::frame,&d,probe,d.cancellation).status;
#else
                        status = resident->pipeline.run(Delivery::frame,&d,nullptr,d.cancellation).status;
#endif
                    }
                }
            }
        }
        catch (...) { d.native_failure=std::current_exception(); hr = exception_code(false); }
        std::lock_guard lock(d.mutex); d.native_error = hr; d.native_status = status; d.done = true; d.changed.notify_one();
    });
    try {
        std::unique_lock lock(d.mutex);
        for (;;) {
            // Stop is deliberately not this predicate: after abort, wait for
            // actual quiescence rather than repeatedly waking on a latched flag.
            d.changed.wait_for(lock,poll_interval,[&] { return d.ready || d.done; });
            lock.unlock(); control.poll(); lock.lock();
            if (d.done) { break; }
            if (!d.ready) { continue; }
            lock.unlock();
            const auto hr = control.aborted ? S_OK : speech_rate.write(d.pcm,control.rate,output);
            lock.lock(); d.ready = false;
            if (hr!=S_OK) { d.output_error = hr; }
            d.changed.notify_one();
        }
    } catch (...) {
        const auto hr = exception_code();
        { std::lock_guard lock(d.mutex); d.output_error = hr; d.ready = false; }
        d.changed.notify_all(); control.cancellation.request_stop(); coordinator.join(); return hr;
    }
    coordinator.join();
    if (d.output_error!=S_OK) { return logged_result(logging::Event::write,d.output_error); }
    if (d.native_error!=S_OK) {
        if (d.native_failure) { try { std::rethrow_exception(d.native_failure); } catch (...) { return exception_code(); } }
        return logged_result(logging::Event::synthesis,d.native_error);
    }
    // Same distinction as Chatterbox: requested SAPI abort is normal S_OK;
    // an unexpected internally cancelled generation would be E_ABORT.
    if (d.native_status==SynthesisStatus::cancelled && !control.aborted) {
        logging::error({logging::Event::synthesis,E_ABORT,0,logging::Component::native,logging::Kind::unexpected_cancellation});
        return E_ABORT;
    }
    // Abort/errors discard the Sonic tail; only successful synthesis flushes it.
    if (!control.poll()) {
        return logged_result(logging::Event::write,speech_rate.finish(output));
    }
    return S_OK;
}
class Engine final : public ISpTTSEngine, public ISpObjectWithToken {
public:
    Engine() noexcept { ++objects; }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** result) override {
        if (!result) { return E_POINTER; } *result = nullptr;
        if (iid==IID_IUnknown || iid==__uuidof(ISpTTSEngine)) { *result = static_cast<ISpTTSEngine*>(this); }
        else if (iid==__uuidof(ISpObjectWithToken)) { *result = static_cast<ISpObjectWithToken*>(this); }
        else { return E_NOINTERFACE; } AddRef(); return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++references_; }
    ULONG STDMETHODCALLTYPE Release() override { const auto n = --references_; if (!n) { delete this; } return n; }
    HRESULT STDMETHODCALLTYPE SetObjectToken(ISpObjectToken* token) override {
        if (!token) { return logged_result(logging::Event::configuration,E_POINTER); }
        try {
            (void)SetErrorInfo(0,nullptr);
            auto c = read_configuration(token);
            std::unique_lock lock(serial_);
            available_.wait(lock,[&] { return !active_; });
            if (!token_ || !c.same_runtime(configuration_)) { resident_.reset(); }
            token->AddRef(); if (token_) { token_->Release(); } token_ = token;
            configuration_ = std::move(c); return S_OK;
        } catch (...) { return exception_code(true,logging::Event::configuration); }
    }
    HRESULT STDMETHODCALLTYPE GetObjectToken(ISpObjectToken** token) override {
        if (!token) { return logged_result(logging::Event::token_access,E_POINTER); } *token = nullptr;
        try { std::lock_guard lock(serial_); if (!token_) { return logged_result(logging::Event::token_access,SPERR_UNINITIALIZED); }
            token_->AddRef(); *token = token_; return S_OK;
        } catch (...) { return exception_code(true,logging::Event::token_access); }
    }
    HRESULT STDMETHODCALLTYPE GetOutputFormat(const GUID*, const WAVEFORMATEX*, GUID* format, WAVEFORMATEX** wave) override {
        if (!format || !wave) { return logged_result(logging::Event::output_format,E_POINTER); } *wave = nullptr;
        const auto memory = static_cast<WAVEFORMATEX*>(CoTaskMemAlloc(sizeof(WAVEFORMATEX)));
        if (!memory) {
            logging::error({logging::Event::output_format,E_OUTOFMEMORY,0,logging::Component::sapi,logging::Kind::allocation});
            return E_OUTOFMEMORY;
        }
        *memory = native_format(); *format = SPDFID_WaveFormatEx; *wave = memory; return S_OK;
    }
    HRESULT STDMETHODCALLTYPE Speak(DWORD, REFGUID format, const WAVEFORMATEX* wave, const SPVTEXTFRAG* fragments, ISpTTSEngineSite* site) override {
        if (!site) { return logged_result(logging::Event::synthesis,E_POINTER); }
        if (format!=SPDFID_WaveFormatEx || !valid_format(wave)) { return logged_result(logging::Event::output_format,SPERR_UNSUPPORTED_FORMAT); }
        AddRef(); struct Keep { Engine* value; ~Keep() { value->Release(); } } keep{this};
        try {
            (void)SetErrorInfo(0,nullptr);
            RequestControl control{site};
            if (control.poll()) { return S_OK; }
            std::unique_lock lock(serial_);
            while (active_) {
                available_.wait_for(lock,poll_interval,[&] { return !active_; });
                lock.unlock(); const bool aborted = control.poll(); lock.lock();
                if (aborted) { return S_OK; }
            }
            active_ = true; lock.unlock();
            struct EndRequest {
                Engine* engine;
                ~EndRequest() {
                    { std::lock_guard guard(engine->serial_); engine->active_ = false; }
                    engine->available_.notify_all();
                }
            } end{this};
            if (!token_) { return logged_result(logging::Event::synthesis,SPERR_UNINITIALIZED); }
            const auto request = collect_request(fragments,{[](void* context) { return static_cast<RequestControl*>(context)->poll(); },&control});
            if (request.cancelled) { return S_OK; }
            if (request.text.empty() && request.bookmarks.empty()) { return S_OK; }
            if (control.poll()) { return S_OK; }
            const auto hr = leading_bookmarks(control,request.bookmarks);
            if (FAILED(hr)) { return hr; } // leading_bookmarks records the failed site operation.
            if (request.text.empty() || control.aborted) { return S_OK; }
            if (!request.bookmarks.empty() && control.poll()) { return S_OK; }
            return render_volume_runs(request,control,[&](std::string_view text) {
                return stream(resident_,configuration_,text,control);
            });
        } catch (...) { return exception_code(); }
    }
private:
    ~Engine() { resident_.reset(); if (token_) { token_->Release(); } --objects; }
    std::atomic<ULONG> references_{1};
    std::mutex serial_;
    std::condition_variable available_;
    bool active_ = false;
    ISpObjectToken* token_ = nullptr;
    Configuration configuration_;
    std::unique_ptr<Resident> resident_;
};
class Factory final : public IClassFactory {
public:
    Factory() noexcept { ++objects; }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** result) override {
        if (!result) { return E_POINTER; } *result = nullptr;
        if (iid!=IID_IUnknown && iid!=IID_IClassFactory) { return E_NOINTERFACE; }
        *result = static_cast<IClassFactory*>(this); AddRef(); return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++references_; }
    ULONG STDMETHODCALLTYPE Release() override { const auto n = --references_; if (!n) { delete this; } return n; }
    HRESULT STDMETHODCALLTYPE CreateInstance(IUnknown* outer, REFIID iid, void** result) override {
        if (!result) { return E_POINTER; } *result = nullptr;
        if (outer) { return CLASS_E_NOAGGREGATION; }
        try { auto engine = new Engine; const auto hr = engine->QueryInterface(iid,result); engine->Release(); return hr; }
        catch (...) { return exception_code(true,logging::Event::create_engine); }
    }
    HRESULT STDMETHODCALLTYPE LockServer(BOOL lock) override {
        if (lock) { ++locks; return S_OK; }
        auto current = locks.load();
        while (current>0) { if (locks.compare_exchange_weak(current,current-1)) { return S_OK; } }
        return E_UNEXPECTED;
    }
private:
    ~Factory() { --objects; }
    std::atomic<ULONG> references_{1};
};
}
HRESULT get_class(REFCLSID clsid, REFIID iid, void** result) noexcept {
    if (!result) { return E_POINTER; } *result = nullptr;
    if (clsid!=engine_clsid) { return CLASS_E_CLASSNOTAVAILABLE; }
    try { auto factory = new Factory; const auto hr = factory->QueryInterface(iid,result); factory->Release(); return hr; }
    catch (...) { return exception_code(true,logging::Event::create_factory); }
}
HRESULT can_unload() noexcept { return objects.load()==0 && locks.load()==0 ? S_OK : S_FALSE; }
void attach(HMODULE value) noexcept { module = value; }
}
extern "C" HRESULT __stdcall DllGetClassObject(REFCLSID clsid, REFIID iid, void** result) { return outloud::sapi::get_class(clsid,iid,result); }
extern "C" HRESULT __stdcall DllCanUnloadNow() { return outloud::sapi::can_unload(); }
BOOL WINAPI DllMain(HINSTANCE module, DWORD reason, LPVOID) {
    if (reason==DLL_PROCESS_ATTACH) { outloud::sapi::attach(module); DisableThreadLibraryCalls(module); }
    return TRUE;
}
