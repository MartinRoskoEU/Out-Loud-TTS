#include "identity.hpp"
#include "support.hpp"
#include "outloud/orchestration.hpp"
#include "outloud/pcm16.hpp"
#include <atomic>
#include <condition_variable>
#include <filesystem>
#include <mutex>
#include <thread>

namespace outloud::sapi {
namespace {
std::atomic<long> objects{0}, locks{0};
HMODULE module = nullptr;
HRESULT exception_code() noexcept {
    try { throw; }
    catch (const Failure& e) { return e.code; }
    catch (const std::bad_alloc&) { return E_OUTOFMEMORY; }
    catch (const std::invalid_argument&) { return E_INVALIDARG; }
    catch (const std::filesystem::filesystem_error& e) { return HRESULT_FROM_WIN32(static_cast<DWORD>(e.code().value())); }
    catch (...) { return E_FAIL; }
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
        if (!std::filesystem::is_regular_file(path)) { throw Failure{HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND)}; }
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
        : model(c.model), voice(c.voice), tokenizer(c.tokenizer),
          pipeline(model,voice,tokenizer,c.tokens,FlowLMGenerator::estimate_max_gen_len(c.tokens),c.path) {}
};
// Chatterbox's host checks require every site call on the Speak caller thread.
// One joined coordinator runs the unchanged core; one-frame rendezvous retains
// caller-thread delivery without COM proxies or extra neural/audio workers.
struct Delivery {
    std::mutex mutex;
    std::condition_variable changed;
    std::array<std::int16_t,1920> pcm{};
    bool ready = false, done = false;
    HRESULT output_error = S_OK, native_error = S_OK;
    static void frame(void* context, std::uint64_t, std::size_t, std::uint32_t, std::span<const float> input) {
        auto& d = *static_cast<Delivery*>(context);
        std::unique_lock lock(d.mutex);
        d.changed.wait(lock,[&] { return !d.ready || d.output_error!=S_OK; });
        if (d.output_error!=S_OK) { throw Failure{d.output_error}; }
        convert_pcm16(input,d.pcm);
        d.ready = true; d.changed.notify_one();
        d.changed.wait(lock,[&] { return !d.ready || d.output_error!=S_OK; });
        if (d.output_error!=S_OK) { throw Failure{d.output_error}; }
    }
};
HRESULT write_frame(ISpTTSEngineSite* site, std::span<const std::int16_t> pcm) {
    const auto* bytes = reinterpret_cast<const std::uint8_t*>(pcm.data());
    ULONG delivered = 0;
    const auto total = static_cast<ULONG>(pcm.size_bytes());
    while (delivered<total) {
        ULONG written = 0;
        const auto hr = site->Write(bytes+delivered,total-delivered,&written);
        if (FAILED(hr) || hr==SP_AUDIO_STOPPED) { return hr; }
        if (!written || written>total-delivered || written%2) { return E_UNEXPECTED; }
        delivered += written;
    }
    return S_OK;
}
HRESULT stream(Resident& resident, ISpTTSEngineSite* site) {
    Delivery d;
    std::thread coordinator([&] {
        HRESULT hr = S_OK;
        try { (void)resident.pipeline.run(Delivery::frame,&d); }
        catch (...) { hr = exception_code(); }
        std::lock_guard lock(d.mutex); d.native_error = hr; d.done = true; d.changed.notify_one();
    });
    try {
        std::unique_lock lock(d.mutex);
        for (;;) {
            d.changed.wait(lock,[&] { return d.ready || d.done; });
            if (d.done) { break; }
            lock.unlock();
            const auto hr = write_frame(site,d.pcm);
            lock.lock(); d.ready = false;
            if (hr!=S_OK) { d.output_error = hr; }
            d.changed.notify_one();
        }
    } catch (...) {
        const auto hr = exception_code();
        { std::lock_guard lock(d.mutex); d.output_error = hr; d.ready = false; }
        d.changed.notify_one(); coordinator.join(); return hr;
    }
    coordinator.join();
    return d.output_error!=S_OK ? d.output_error : d.native_error;
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
        if (!token) { return E_POINTER; }
        try {
            auto c = read_configuration(token);
            std::lock_guard lock(serial_);
            if (!token_ || !c.same_runtime(configuration_)) { resident_.reset(); }
            token->AddRef(); if (token_) { token_->Release(); } token_ = token;
            configuration_ = std::move(c); return S_OK;
        } catch (...) { return exception_code(); }
    }
    HRESULT STDMETHODCALLTYPE GetObjectToken(ISpObjectToken** token) override {
        if (!token) { return E_POINTER; } *token = nullptr;
        try { std::lock_guard lock(serial_); if (!token_) { return SPERR_UNINITIALIZED; }
            token_->AddRef(); *token = token_; return S_OK;
        } catch (...) { return exception_code(); }
    }
    HRESULT STDMETHODCALLTYPE GetOutputFormat(const GUID*, const WAVEFORMATEX*, GUID* format, WAVEFORMATEX** wave) override {
        if (!format || !wave) { return E_POINTER; } *wave = nullptr;
        const auto memory = static_cast<WAVEFORMATEX*>(CoTaskMemAlloc(sizeof(WAVEFORMATEX)));
        if (!memory) { return E_OUTOFMEMORY; }
        *memory = native_format(); *format = SPDFID_WaveFormatEx; *wave = memory; return S_OK;
    }
    HRESULT STDMETHODCALLTYPE Speak(DWORD, REFGUID format, const WAVEFORMATEX* wave, const SPVTEXTFRAG* fragments, ISpTTSEngineSite* site) override {
        if (!site) { return E_POINTER; }
        if (format!=SPDFID_WaveFormatEx || !valid_format(wave)) { return SPERR_UNSUPPORTED_FORMAT; }
        AddRef(); struct Keep { Engine* value; ~Keep() { value->Release(); } } keep{this};
        try {
            std::lock_guard lock(serial_);
            if (!token_) { return SPERR_UNINITIALIZED; }
            const auto text = collect_text(fragments);
            if (text.empty()) { return S_OK; }
            if (!resident_) { resident_ = std::make_unique<Resident>(configuration_); }
            resident_->pipeline.prepare(text,configuration_.seed);
            // GetActions/native cancellation integration is deliberately Step 2N.
            return stream(*resident_,site);
        } catch (...) { return exception_code(); }
    }
private:
    ~Engine() { resident_.reset(); if (token_) { token_->Release(); } --objects; }
    std::atomic<ULONG> references_{1};
    std::mutex serial_;
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
        catch (...) { return exception_code(); }
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
    catch (...) { return exception_code(); }
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
