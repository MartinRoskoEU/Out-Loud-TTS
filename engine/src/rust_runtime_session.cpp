#include "outloud/rust_runtime_session.hpp"
#include "rust_pcm_reader.hpp"
#include <Windows.h>
#include <winhttp.h>
#include <algorithm>
#include <array>
#include <condition_variable>
#include <cwchar>
#include <exception>
#include <mutex>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <vector>

namespace outloud {
namespace {
using Clock = std::chrono::steady_clock;
constexpr DWORD poll_ms = 20;
constexpr auto cleanup_timeout = std::chrono::seconds(5);
void require(bool success, const char* operation) {
    if (!success) { throw std::system_error(static_cast<int>(GetLastError()), std::system_category(), operation); }
}
struct Handle {
    HANDLE value = nullptr;
    Handle() = default;
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
    ~Handle() { reset(); }
    void reset(HANDLE next = nullptr) noexcept {
        if (value && value != INVALID_HANDLE_VALUE) { CloseHandle(value); }
        value = next;
    }
};
struct Internet {
    HINTERNET value;
    explicit Internet(HINTERNET handle) : value(handle) { require(value != nullptr, "Open runtime health connection"); }
    ~Internet() { if (value) { WinHttpCloseHandle(value); } }
    Internet(const Internet&) = delete;
    Internet& operator=(const Internet&) = delete;
};
struct Attributes {
    std::vector<unsigned char> storage;
    LPPROC_THREAD_ATTRIBUTE_LIST value = nullptr;
    explicit Attributes(DWORD count) {
        SIZE_T size = 0;
        InitializeProcThreadAttributeList(nullptr, count, 0, &size);
        storage.resize(size);
        auto* candidate = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(storage.data());
        require(InitializeProcThreadAttributeList(candidate, count, 0, &size) != FALSE, "Initialize runtime process attributes");
        value = candidate;
    }
    ~Attributes() { if (value) { DeleteProcThreadAttributeList(value); } }
    Attributes(const Attributes&) = delete;
    Attributes& operator=(const Attributes&) = delete;
};
void local_file(const std::filesystem::path& path) {
    const auto& name = path.native();
    if (!path.is_absolute() || name.size() < 3 || name[1] != L':' || (name[2] != L'\\' && name[2] != L'/') ||
        name.find(L':', 2) != name.npos || name.find(L'\0') != name.npos) {
        throw std::invalid_argument("Runtime, config and default voice must be absolute local file paths");
    }
    const auto attributes = GetFileAttributesW(path.c_str());
    require(attributes != INVALID_FILE_ATTRIBUTES, "Read runtime session input attributes");
    if (attributes & FILE_ATTRIBUTE_DIRECTORY) { throw std::invalid_argument("Runtime session input is a directory"); }
}
// Windows CRT argument quoting, also used by Studio's native operation owner.
std::wstring quote(std::wstring_view value) {
    std::wstring result(1, L'"');
    std::size_t slashes = 0;
    for (const auto c : value) {
        if (c == L'\\') { ++slashes; continue; }
        result.append(c == L'"' ? slashes * 2 + 1 : slashes, L'\\');
        result += c;
        slashes = 0;
    }
    result.append(slashes * 2, L'\\');
    return result + L'"';
}
std::vector<wchar_t> environment() {
    struct Strings {
        wchar_t* value = GetEnvironmentStringsW();
        ~Strings() { if (value) { FreeEnvironmentStringsW(value); } }
    } strings;
    require(strings.value != nullptr, "Read runtime environment");
    constexpr std::array removed{L"HF_HOME", L"HF_HUB_CACHE", L"HUGGINGFACE_HUB_CACHE", L"HF_ASSETS_CACHE", L"HF_XET_CACHE",
        L"XDG_CACHE_HOME", L"TRANSFORMERS_CACHE", L"PYTORCH_TRANSFORMERS_CACHE", L"PYTORCH_PRETRAINED_BERT_CACHE",
        L"HF_TOKEN", L"HUGGING_FACE_HUB_TOKEN", L"HF_HUB_DISABLE_IMPLICIT_TOKEN"};
    std::vector<std::wstring> entries;
    bool home = false;
    std::wstring profile;
    for (auto* entry = strings.value; *entry; entry += std::wcslen(entry) + 1) {
        std::wstring text(entry);
        const auto equal = text.find(L'=', text.front() == L'=' ? 1 : 0);
        const auto key = text.substr(0, equal);
        if (std::any_of(removed.begin(), removed.end(), [&](const auto* name) { return _wcsicmp(key.c_str(), name) == 0; })) { continue; }
        if (_wcsicmp(key.c_str(), L"HOME") == 0) {
            if (equal + 1 == text.size()) { continue; }
            home = true;
        }
        if (_wcsicmp(key.c_str(), L"USERPROFILE") == 0 && equal != text.npos) { profile = text.substr(equal + 1); }
        entries.push_back(std::move(text));
    }
    if (!home && !profile.empty()) { entries.push_back(L"HOME=" + profile); }
    entries.emplace_back(L"HF_HUB_DISABLE_IMPLICIT_TOKEN=1");
    std::sort(entries.begin(), entries.end(), [](const auto& a, const auto& b) { return _wcsicmp(a.c_str(), b.c_str()) < 0; });
    std::vector<wchar_t> block;
    for (const auto& entry : entries) { block.insert(block.end(), entry.begin(), entry.end()); block.push_back(L'\0'); }
    block.push_back(L'\0');
    return block;
}
std::uint16_t diagnostic_port(std::string_view text) {
    constexpr std::string_view prefix = "Server running on http://127.0.0.1:";
    std::size_t position = 0;
    while ((position = text.find(prefix, position)) != text.npos) {
        position += prefix.size();
        auto end = position;
        unsigned value = 0;
        while (end < text.size() && text[end] >= '0' && text[end] <= '9') {
            value = (std::min)(65536U, value * 10 + static_cast<unsigned>(text[end++] - '0'));
        }
        if (end != position && end < text.size() && value > 0 && value <= 65535 &&
            (text[end] == ' ' || text[end] == '\r' || text[end] == '\n' || text[end] == '\t' || text[end] == '\x1b')) {
            return static_cast<std::uint16_t>(value);
        }
    }
    return 0;
}
void check_health(std::uint16_t port) {
    Internet session(WinHttpOpen(L"OutLoud-RuntimeSession", WINHTTP_ACCESS_TYPE_NO_PROXY,
        WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0));
    // Per-phase bounds, not a total HTTP deadline. No response body is needed.
    require(WinHttpSetTimeouts(session.value, 5000, 5000, 5000, 5000) != FALSE, "Set runtime health timeouts");
    Internet connection(WinHttpConnect(session.value, L"127.0.0.1", port, 0));
    Internet request(WinHttpOpenRequest(connection.value, L"GET", L"/health", nullptr,
        WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, 0));
    DWORD disabled = WINHTTP_DISABLE_REDIRECTS | WINHTTP_DISABLE_COOKIES | WINHTTP_DISABLE_AUTHENTICATION;
    require(WinHttpSetOption(request.value, WINHTTP_OPTION_DISABLE_FEATURE, &disabled, sizeof(disabled)) != FALSE,
        "Restrict runtime health request");
    require(WinHttpSendRequest(request.value, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) != FALSE,
        "Send runtime health request");
    require(WinHttpReceiveResponse(request.value, nullptr) != FALSE, "Receive runtime health response");
    DWORD status = 0, size = sizeof(status);
    require(WinHttpQueryHeaders(request.value, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
        WINHTTP_HEADER_NAME_BY_INDEX, &status, &size, WINHTTP_NO_HEADER_INDEX) != FALSE, "Read runtime health status");
    if (status != 200) { throw std::runtime_error("Rust runtime health check failed (HTTP " + std::to_string(status) + ")"); }
}

struct RequestCancelled {};
// The public reader remains blocking, but its HTTP operations are asynchronous.
// Only the caller issues/closes handles; the stop callback merely wakes its wait.
// Thus close never races a WinHTTP API invocation or touches a synchronous read.
class PCMRequest {
    struct State {
        std::mutex mutex;
        std::condition_variable changed;
        bool done = false, closed = false, cancelled = false, finished = false;
        DWORD error = 0, bytes = 0;
        std::array<std::uint8_t, 3840> buffer{};
        std::string form;
    };
    struct Wake {
        std::shared_ptr<State> state;
        void operator()() const noexcept {
            std::lock_guard lock(state->mutex);
            if (!state->finished) { state->cancelled = true; }
            state->changed.notify_all();
        }
    };
    // WinHTTP owns this reference until its final HANDLE_CLOSING notification.
    // Pending I/O buffers stay alive even if a bounded cleanup wait fails.
    struct Context { std::shared_ptr<State> state; };
    std::shared_ptr<State> state_ = std::make_shared<State>();
    std::stop_callback<Wake> cancellation_;
    Internet http_, connection_, request_;
    DWORD_PTR context_ = 0;

    static void CALLBACK status(HINTERNET, DWORD_PTR context, DWORD notification, void* info, DWORD length) noexcept {
        auto* owner = reinterpret_cast<Context*>(context);
        if (!owner) { return; }
        auto state = owner->state;
        {
            std::lock_guard lock(state->mutex);
            if (notification == WINHTTP_CALLBACK_STATUS_HANDLE_CLOSING) { state->closed = true; }
            else if (notification == WINHTTP_CALLBACK_STATUS_REQUEST_ERROR) {
                state->error = info && length >= sizeof(WINHTTP_ASYNC_RESULT)
                    ? static_cast<WINHTTP_ASYNC_RESULT*>(info)->dwError : ERROR_INVALID_DATA;
                state->done = true;
            } else if (notification == WINHTTP_CALLBACK_STATUS_SENDREQUEST_COMPLETE ||
                notification == WINHTTP_CALLBACK_STATUS_HEADERS_AVAILABLE || notification == WINHTTP_CALLBACK_STATUS_READ_COMPLETE) {
                state->bytes = notification == WINHTTP_CALLBACK_STATUS_READ_COMPLETE ? length : 0;
                state->done = true;
            }
            state->changed.notify_all();
        }
        if (notification == WINHTTP_CALLBACK_STATUS_HANDLE_CLOSING) { delete owner; }
    }
    template<class Operation>
    void perform(Operation&& operation) {
        check();
        { std::lock_guard lock(state_->mutex); state_->done = false; state_->error = 0; state_->bytes = 0; }
        // Never hold the mutex here: completion callbacks can run inline.
        if (!operation()) {
            const auto error = GetLastError();
            check();
            if (error != ERROR_IO_PENDING) {
                throw std::system_error(static_cast<int>(error), std::system_category(), "Start Rust HTTP operation");
            }
        }
        std::unique_lock lock(state_->mutex);
        if (!state_->changed.wait_for(lock, std::chrono::seconds(305), [&] { return state_->done || state_->cancelled; })) {
            throw std::runtime_error("Rust HTTP completion timed out");
        }
        if (state_->cancelled) { throw RequestCancelled{}; }
        if (state_->error) { throw std::system_error(static_cast<int>(state_->error), std::system_category(), "Rust HTTP operation failed"); }
    }
    void close() {
        if (!request_.value) { return; }
        require(WinHttpCloseHandle(request_.value) != FALSE, "Close Rust HTTP request");
        request_.value = nullptr;
        std::unique_lock lock(state_->mutex);
        if (!state_->changed.wait_for(lock, cleanup_timeout, [&] { return state_->closed; })) {
            throw std::runtime_error("Rust HTTP callback cleanup is unconfirmed");
        }
    }
public:
    PCMRequest(std::uint16_t port, std::stop_token cancellation)
        : cancellation_(cancellation, Wake{state_}),
          http_(WinHttpOpen(L"OutLoud-RuntimeSession", WINHTTP_ACCESS_TYPE_NO_PROXY,
              WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, WINHTTP_FLAG_ASYNC)),
          connection_(WinHttpConnect(http_.value, L"127.0.0.1", port, 0)),
          request_(WinHttpOpenRequest(connection_.value, L"POST", L"/tts", nullptr,
              WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, 0)) {
        require(WinHttpSetTimeouts(http_.value, 5000, 5000, 5000, 300000) != FALSE, "Set Rust synthesis timeouts");
        // Set on the request too: it was created before session timeouts changed.
        require(WinHttpSetTimeouts(request_.value, 5000, 5000, 5000, 300000) != FALSE, "Set Rust request timeouts");
        DWORD disabled = WINHTTP_DISABLE_REDIRECTS | WINHTTP_DISABLE_COOKIES | WINHTTP_DISABLE_AUTHENTICATION;
        require(WinHttpSetOption(request_.value, WINHTTP_OPTION_DISABLE_FEATURE, &disabled, sizeof(disabled)) != FALSE,
            "Restrict Rust synthesis request");
        auto owner = std::make_unique<Context>(Context{state_});
        auto context = reinterpret_cast<DWORD_PTR>(owner.get());
        require(WinHttpSetOption(request_.value, WINHTTP_OPTION_CONTEXT_VALUE, &context, sizeof(context)) != FALSE,
            "Set Rust HTTP callback context");
        require(WinHttpSetStatusCallback(request_.value, status, WINHTTP_CALLBACK_FLAG_SENDREQUEST_COMPLETE |
            WINHTTP_CALLBACK_FLAG_HEADERS_AVAILABLE | WINHTTP_CALLBACK_FLAG_READ_COMPLETE |
            WINHTTP_CALLBACK_FLAG_REQUEST_ERROR | WINHTTP_CALLBACK_FLAG_HANDLES, 0) != WINHTTP_INVALID_STATUS_CALLBACK,
            "Set Rust HTTP completion callback");
        context_ = reinterpret_cast<DWORD_PTR>(owner.release());
    }
    ~PCMRequest() { try { close(); } catch (...) { /* Explicit finish reports uncertain cleanup. */ } }
    void check() const {
        std::lock_guard lock(state_->mutex);
        if (state_->cancelled) { throw RequestCancelled{}; }
    }
    void send(std::string form) {
        state_->form = std::move(form);
        perform([&] { return WinHttpSendRequest(request_.value,
            L"Content-Type: application/x-www-form-urlencoded; charset=utf-8\r\n", static_cast<DWORD>(-1),
            state_->form.data(), static_cast<DWORD>(state_->form.size()), static_cast<DWORD>(state_->form.size()), context_); });
        perform([&] { return WinHttpReceiveResponse(request_.value, nullptr); });
        check();
        DWORD code = 0, size = sizeof(code);
        require(WinHttpQueryHeaders(request_.value, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
            WINHTTP_HEADER_NAME_BY_INDEX, &code, &size, WINHTTP_NO_HEADER_INDEX) != FALSE, "Read Rust synthesis status");
        if (code != 200) { throw std::runtime_error("Rust synthesis failed (HTTP " + std::to_string(code) + ")"); }
    }
    std::size_t read(std::span<std::uint8_t> destination) {
        if (destination.size() > state_->buffer.size()) { throw std::invalid_argument("Rust HTTP read exceeds fixed buffer"); }
        perform([&] { return WinHttpReadData(request_.value, state_->buffer.data(), static_cast<DWORD>(destination.size()), nullptr); });
        if (state_->bytes > destination.size()) { throw std::runtime_error("Rust HTTP returned an oversized read"); }
        std::copy_n(state_->buffer.data(), state_->bytes, destination.data());
        return state_->bytes;
    }
    bool finish() {
        close();
        std::lock_guard lock(state_->mutex);
        // This is the completion boundary. Later stop signals cannot change the
        // outcome or affect another request. stop_callback destruction joins any
        // notification already running before its context is released.
        state_->finished = true;
        return state_->cancelled;
    }
};
} // namespace

std::string detail::rust_tts_form(std::string_view utf8) {
    if (utf8.empty() || utf8.size() > static_cast<std::size_t>((std::numeric_limits<int>::max)()) ||
        utf8.size() > ((std::numeric_limits<DWORD>::max)() - 5ULL) / 3) {
        throw std::invalid_argument("Rust synthesis requires nonempty UTF-8 text within the HTTP request size limit");
    }
    require(MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, utf8.data(), static_cast<int>(utf8.size()), nullptr, 0) != 0,
        "Validate Rust synthesis UTF-8 text");
    std::string form = "text=";
    constexpr char hex[] = "0123456789ABCDEF";
    for (const unsigned char c : utf8) {
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
            c == '-' || c == '_' || c == '.' || c == '~') { form += static_cast<char>(c); }
        else if (c == ' ') { form += '+'; }
        else { form += '%'; form += hex[c >> 4]; form += hex[c & 15]; }
    }
    return form;
}

struct RustRuntimeSession::Impl {
    Handle job, process, output, error;
    mutable std::mutex mutex;
    std::condition_variable changed;
    std::uint16_t detected_port = 0;
    bool exited = false, healthy = false;
    DWORD monitor_error = 0;
    std::jthread monitor;

    ~Impl() {
        // Also covers startup exceptions and failed explicit cleanup. A monitor
        // uses only nonblocking pipe reads and short process waits, so joining
        // never relies on descendants closing their stdout/stderr handles.
        job.reset();
        monitor.request_stop();
        if (monitor.joinable()) { monitor.join(); }
    }
    bool running() const {
        if (!process.value) { return false; }
        const auto waited = WaitForSingleObject(process.value, 0);
        require(waited != WAIT_FAILED, "Query runtime process");
        return waited == WAIT_TIMEOUT;
    }
    void drain(HANDLE pipe, std::string& tail) {
        // Bound work per pipe so noisy output cannot starve exit monitoring.
        std::array<char, 2048> buffer{};
        for (unsigned i = 0; i < 16; ++i) {
            DWORD available = 0, count = 0;
            if (!PeekNamedPipe(pipe, nullptr, 0, nullptr, &available, nullptr)) {
                if (GetLastError() == ERROR_BROKEN_PIPE) { return; }
                require(false, "Peek runtime diagnostics");
            }
            if (!available) { return; }
            require(ReadFile(pipe, buffer.data(), (std::min)(available, static_cast<DWORD>(buffer.size())), &count, nullptr) != FALSE,
                "Read runtime diagnostics");
            tail.append(buffer.data(), count);
            if (const auto port = diagnostic_port(tail)) {
                std::lock_guard lock(mutex);
                if (!detected_port) { detected_port = port; changed.notify_all(); }
            }
            if (tail.size() > 512) { tail.erase(0, tail.size() - 512); }
        }
    }
    void watch(std::stop_token stop) noexcept {
        try {
            std::string out_tail, error_tail;
            while (!stop.stop_requested()) {
                drain(output.value, out_tail); drain(error.value, error_tail);
                const auto waited = WaitForSingleObject(process.value, poll_ms);
                require(waited != WAIT_FAILED, "Wait for runtime process");
                if (waited == WAIT_OBJECT_0) {
                    std::lock_guard lock(mutex);
                    exited = true; changed.notify_all(); return;
                }
            }
        } catch (const std::system_error& failure) {
            std::lock_guard lock(mutex);
            monitor_error = static_cast<DWORD>(failure.code().value()); changed.notify_all();
        } catch (...) {
            std::lock_guard lock(mutex);
            monitor_error = ERROR_NOT_ENOUGH_MEMORY; changed.notify_all();
        }
    }
    void launch(const RustRuntimeSessionOptions& options) {
        job.value = CreateJobObjectW(nullptr, nullptr);
        require(job.value != nullptr, "Create runtime job");
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
        limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        require(SetInformationJobObject(job.value, JobObjectExtendedLimitInformation, &limits, sizeof(limits)) != FALSE,
            "Set runtime job lifetime");
        SECURITY_ATTRIBUTES security{sizeof(security), nullptr, TRUE};
        Handle output_writer, error_writer, input;
        require(CreatePipe(&output.value, &output_writer.value, &security, 0) != FALSE, "Create runtime stdout pipe");
        require(CreatePipe(&error.value, &error_writer.value, &security, 0) != FALSE, "Create runtime stderr pipe");
        require(SetHandleInformation(output.value, HANDLE_FLAG_INHERIT, 0) != FALSE, "Restrict runtime stdout inheritance");
        require(SetHandleInformation(error.value, HANDLE_FLAG_INHERIT, 0) != FALSE, "Restrict runtime stderr inheritance");
        input.value = CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &security, OPEN_EXISTING, 0, nullptr);
        require(input.value != INVALID_HANDLE_VALUE, "Open runtime stdin");
        Attributes attributes(options.prefer_system_dlls ? 3U : 2U);
        DWORD64 mitigation = PROCESS_CREATION_MITIGATION_POLICY_IMAGE_LOAD_PREFER_SYSTEM32_ALWAYS_ON;
        if (options.prefer_system_dlls) {
            require(UpdateProcThreadAttribute(attributes.value,0,PROC_THREAD_ATTRIBUTE_MITIGATION_POLICY,
                &mitigation,sizeof(mitigation),nullptr,nullptr)!=FALSE,"Prefer Windows system runtime libraries");
        }
        std::array handles{input.value, output_writer.value, error_writer.value};
        require(UpdateProcThreadAttribute(attributes.value, 0, PROC_THREAD_ATTRIBUTE_JOB_LIST, &job.value,
            sizeof(job.value), nullptr, nullptr) != FALSE, "Attach runtime job at process creation");
        require(UpdateProcThreadAttribute(attributes.value, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST, handles.data(),
            sizeof(handles), nullptr, nullptr) != FALSE, "Restrict runtime inherited handles");
        STARTUPINFOEXW startup{};
        startup.StartupInfo.cb = sizeof(startup);
        startup.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
        startup.StartupInfo.hStdInput = input.value;
        startup.StartupInfo.hStdOutput = output_writer.value;
        startup.StartupInfo.hStdError = error_writer.value;
        startup.lpAttributeList = attributes.value;
        auto command = quote(options.executable.native()) + L" serve --config " + quote(options.configuration.native()) +
            L" --default-voice " + quote(options.default_voice.native()) + L" --host 127.0.0.1 --port 0";
        auto child_environment = environment();
        PROCESS_INFORMATION information{};
        require(CreateProcessW(options.executable.c_str(), command.data(), nullptr, nullptr, TRUE,
            CREATE_NO_WINDOW | CREATE_SUSPENDED | CREATE_UNICODE_ENVIRONMENT | EXTENDED_STARTUPINFO_PRESENT,
            child_environment.data(), options.executable.parent_path().c_str(), &startup.StartupInfo, &information) != FALSE,
            "Start Rust runtime");
        process.value = information.hProcess;
        Handle primary_thread; primary_thread.value = information.hThread;
        require(ResumeThread(primary_thread.value) != static_cast<DWORD>(-1), "Resume Rust runtime");
        monitor = std::jthread([this](std::stop_token stop) { watch(stop); });
    }
    void stop() {
        if (!job.value) { return; }
        JOBOBJECT_BASIC_ACCOUNTING_INFORMATION accounting{};
        const auto active = [&] {
            require(QueryInformationJobObject(job.value, JobObjectBasicAccountingInformation, &accounting, sizeof(accounting), nullptr) != FALSE,
                "Query runtime job processes");
            return accounting.ActiveProcesses;
        };
        if (active()) { require(TerminateJobObject(job.value, 1) != FALSE, "Terminate runtime job"); }
        const auto deadline = Clock::now() + cleanup_timeout;
        while (active()) {
            if (Clock::now() >= deadline) { throw std::runtime_error("Rust runtime job cleanup timed out; termination is unconfirmed"); }
            std::this_thread::sleep_for(std::chrono::milliseconds(poll_ms));
        }
        if (process.value) {
            const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - Clock::now()).count();
            const auto waited = WaitForSingleObject(process.value, remaining > 0 ? static_cast<DWORD>(remaining) : 0);
            require(waited != WAIT_FAILED, "Wait for Rust runtime exit");
            if (waited != WAIT_OBJECT_0) { throw std::runtime_error("Rust runtime root exit is unconfirmed"); }
        }
    }
};

RustRuntimeSession::RustRuntimeSession() = default;
RustRuntimeSession::~RustRuntimeSession() = default;
void RustRuntimeSession::start(const RustRuntimeSessionOptions& options, std::stop_token cancellation) {
    if (impl_) { throw std::logic_error("Stop the existing Rust runtime session before starting another"); }
    if (cancellation.stop_requested()) { return; }
    if (options.startup_timeout <= std::chrono::milliseconds::zero() || options.startup_timeout > std::chrono::hours(24)) {
        throw std::invalid_argument("Rust runtime startup timeout must be positive and at most 24 hours");
    }
    local_file(options.executable); local_file(options.configuration); local_file(options.default_voice);
    const auto deadline = Clock::now() + options.startup_timeout;
    impl_ = std::make_unique<Impl>();
    try {
        impl_->launch(options);
        {
            auto& state = *impl_;
            std::stop_callback wake(cancellation,[&state] {
                { std::lock_guard lock(state.mutex); }
                state.changed.notify_all();
            });
            std::unique_lock lock(state.mutex);
            if (!state.changed.wait_until(lock, deadline, [&] {
                return cancellation.stop_requested() || state.detected_port || state.exited || state.monitor_error;
            })) { throw std::runtime_error("Rust runtime server readiness timed out"); }
            if (!cancellation.stop_requested() && state.monitor_error) {
                throw std::system_error(static_cast<int>(state.monitor_error), std::system_category(), "Rust runtime diagnostics failed");
            }
        }
        if (cancellation.stop_requested()) { stop(); return; }
        if (const auto code = exit_code()) { throw std::runtime_error("Rust runtime exited during startup (code " + std::to_string(*code) + ")"); }
        check_health(impl_->detected_port);
        if (cancellation.stop_requested()) { stop(); return; }
        if (const auto code = exit_code()) { throw std::runtime_error("Rust runtime exited during health check (code " + std::to_string(*code) + ")"); }
        std::lock_guard lock(impl_->mutex);
        if (impl_->monitor_error) {
            throw std::system_error(static_cast<int>(impl_->monitor_error), std::system_category(), "Rust runtime diagnostics failed during health check");
        }
        impl_->healthy = true;
    } catch (...) {
        // Cleanup failure takes precedence and retains the job for a stop retry.
        // Otherwise preserve the original startup/health error.
        stop();
        throw;
    }
}
void RustRuntimeSession::stop() {
    if (!impl_) { return; }
    impl_->healthy = false;
    impl_->stop();
    impl_.reset();
}
bool RustRuntimeSession::is_ready() const {
    if (!impl_ || !impl_->healthy || !impl_->running()) { return false; }
    std::lock_guard lock(impl_->mutex);
    return impl_->monitor_error == 0;
}
std::uint16_t RustRuntimeSession::port() const {
    if (!is_ready()) { throw std::runtime_error("Rust runtime session is not ready"); }
    return impl_->detected_port;
}
std::optional<std::uint32_t> RustRuntimeSession::exit_code() const {
    if (!impl_ || !impl_->process.value || impl_->running()) { return std::nullopt; }
    DWORD code = 0;
    require(GetExitCodeProcess(impl_->process.value, &code) != FALSE, "Read Rust runtime exit code");
    return code;
}
RustPCMResult RustRuntimeSession::read_pcm(std::string_view utf8, RustPCMSink sink, void* context, std::stop_token cancellation) {
    if (!sink) { throw std::invalid_argument("Rust synthesis requires a PCM sink"); }
    if (cancellation.stop_requested()) { return {0, 0, SynthesisStatus::cancelled}; }
    const auto endpoint = port(); // Reuses the existing prepared process/default voice.
    auto form = detail::rust_tts_form(utf8);
    RustPCMResult result;
    std::exception_ptr failure;
    bool cancelled = false;
    try {
        PCMRequest request(endpoint, cancellation);
        try {
            request.send(std::move(form));
            const auto source = [](void* owner, std::span<std::uint8_t> destination) { return static_cast<PCMRequest*>(owner)->read(destination); };
            struct Delivery { PCMRequest& request; RustPCMSink sink; void* context; RustPCMResult& result; } delivery{request, sink, context, result};
            const auto pcm = [](void* owner, std::span<const std::int16_t> samples) {
                auto& d = *static_cast<Delivery*>(owner);
                d.request.check();
                d.sink(d.context, samples);
                d.result.samples += samples.size(); ++d.result.blocks;
                d.request.check();
            };
            (void)detail::read_rust_wav(source, &request, pcm, &delivery);
            request.check();
            if (!is_ready()) { throw std::runtime_error("Rust runtime stopped or lost diagnostics during synthesis"); }
        } catch (const RequestCancelled&) { cancelled = true; }
        catch (...) { failure = std::current_exception(); }
        cancelled = request.finish() || cancelled; // Drains WinHTTP callbacks, even after parser/sink failure.
    } catch (const RequestCancelled&) { cancelled = true; }
    catch (...) { failure = std::current_exception(); } // Cleanup failure takes precedence.
    if (failure || cancelled) {
        stop(); // Client disconnect/health cannot establish inference quiescence.
        if (failure) { std::rethrow_exception(failure); }
        result.status = SynthesisStatus::cancelled;
    }
    return result;
}
} // namespace outloud
