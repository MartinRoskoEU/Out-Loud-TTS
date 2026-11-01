// Default checks use controlled children and fragmented WAV fixtures.
// --runtime checks lifecycle; --pcm also checks the new reader with a short text.
#include "outloud/rust_runtime_session.hpp"
#include "rust_pcm_checks.hpp"
#include <Windows.h>
#include <tlhelp32.h>
#include <winhttp.h>
#include <array>
#include <atomic>
#include <filesystem>
#include <fstream>
#include <future>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>

namespace {
using namespace std::chrono_literals;
namespace fs = std::filesystem;
void check(bool condition, const char* message) { if (!condition) { throw std::runtime_error(message); } }
struct Handle {
    HANDLE value = nullptr;
    ~Handle() { if (value && value != INVALID_HANDLE_VALUE) { CloseHandle(value); } }
};
struct Scratch {
    fs::path path;
    Scratch() {
        std::array<wchar_t, 32768> temp{};
        const auto count = GetTempPathW(static_cast<DWORD>(temp.size()), temp.data());
        check(count && count < temp.size(), "Cannot locate temporary directory");
        path = fs::path(temp.data()) / (L"outloud-session-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64()));
        check(fs::create_directory(path), "Cannot create private test directory");
    }
    ~Scratch() { std::error_code ignored; fs::remove_all(path, ignored); }
};
std::string text(const fs::path& path) {
    std::ifstream input(path);
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}
void write(const fs::path& path, const std::string& value) { std::ofstream output(path); output << value; check(bool(output), "Cannot write private fixture"); }
struct Internet {
    HINTERNET value;
    explicit Internet(HINTERNET handle) : value(handle) { check(value != nullptr, "Cannot open health request"); }
    ~Internet() { WinHttpCloseHandle(value); }
};
void health(std::uint16_t port) {
    Internet http(WinHttpOpen(L"OutLoud-SessionCheck", WINHTTP_ACCESS_TYPE_NO_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0));
    check(WinHttpSetTimeouts(http.value, 5000, 5000, 5000, 5000) != FALSE, "Cannot set health timeouts");
    Internet connection(WinHttpConnect(http.value, L"127.0.0.1", port, 0));
    Internet request(WinHttpOpenRequest(connection.value, L"GET", L"/health", nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, 0));
    DWORD disabled = WINHTTP_DISABLE_REDIRECTS | WINHTTP_DISABLE_COOKIES | WINHTTP_DISABLE_AUTHENTICATION;
    check(WinHttpSetOption(request.value, WINHTTP_OPTION_DISABLE_FEATURE, &disabled, sizeof(disabled)) != FALSE, "Cannot restrict health request");
    check(WinHttpSendRequest(request.value, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) != FALSE &&
        WinHttpReceiveResponse(request.value, nullptr) != FALSE, "Health request failed");
    DWORD status = 0, size = sizeof(status);
    check(WinHttpQueryHeaders(request.value, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX,
        &status, &size, WINHTTP_NO_HEADER_INDEX) != FALSE && status == 200, "Health did not return HTTP 200");
}
DWORD runtime_child(const fs::path& executable) {
    Handle snapshot{CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0)};
    check(snapshot.value != INVALID_HANDLE_VALUE, "Cannot inspect runtime child");
    PROCESSENTRY32W entry{}; entry.dwSize = sizeof(entry);
    DWORD found = 0;
    for (BOOL more = Process32FirstW(snapshot.value, &entry); more; more = Process32NextW(snapshot.value, &entry)) {
        if (entry.th32ParentProcessID != GetCurrentProcessId()) { continue; }
        Handle process{OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, entry.th32ProcessID)};
        check(process.value != nullptr, "Cannot inspect owned child image");
        std::array<wchar_t, 32768> name{}; DWORD size = static_cast<DWORD>(name.size());
        check(QueryFullProcessImageNameW(process.value, 0, name.data(), &size) != FALSE, "Cannot identify owned child image");
        if (_wcsicmp(name.data(), executable.c_str()) == 0) {
            check(found == 0, "More than one owned runtime process"); found = entry.th32ProcessID;
        }
    }
    check(found != 0, "No owned runtime process found");
    return found;
}
int real_runtime(wchar_t** args, bool read_pcm) {
    outloud::RustRuntimeSession session;
    const outloud::RustRuntimeSessionOptions options{fs::path(args[2]), fs::path(args[3]), fs::path(args[4])};
    const auto started = std::chrono::steady_clock::now();
    session.start(options); // Includes the component's own health check.
    check(session.is_ready() && !session.exit_code(), "Real runtime was not ready after start");
    const auto port = session.port();
    const DWORD pid = runtime_child(options.executable);
    Handle process{OpenProcess(SYNCHRONIZE, FALSE, pid)};
    check(process.value != nullptr, "Cannot retain runtime process handle");
    const auto startup_ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started).count();
    std::cout << "Official runtime ready: PID=" << pid << " port=" << port << " startup_ms=" << startup_ms << std::endl;
    for (unsigned i = 0; i < 3; ++i) {
        health(port);
        check(session.is_ready() && session.port() == port && !session.exit_code() && runtime_child(options.executable) == pid &&
            WaitForSingleObject(process.value, 0) == WAIT_TIMEOUT, "Session was not reused");
        std::cout << "Health HTTP 200; same session/process: check " << i + 1 << std::endl;
        if (i != 2) { std::this_thread::sleep_for(500ms); }
    }
    if (read_pcm) {
        std::uint64_t samples = 0;
        const auto sink = [](void* context, std::span<const std::int16_t> pcm) {
            check(!pcm.empty() && pcm.size() <= 1920, "Invalid live PCM block size");
            *static_cast<std::uint64_t*>(context) += pcm.size();
        };
        const auto pcm = session.read_pcm("Hello from Out Loud.", sink, &samples);
        check(pcm.samples == samples && samples != 0 && pcm.blocks != 0 && session.is_ready() &&
            session.port() == port && runtime_child(options.executable) == pid, "PCM reader did not preserve the session");
        health(port);
        std::cout << "Native reader request passed: samples=" << pcm.samples << " blocks=" << pcm.blocks
            << "; same runtime remains healthy." << std::endl;
    }
    const auto stopping = std::chrono::steady_clock::now();
    session.stop();
    check(WaitForSingleObject(process.value, 1000) == WAIT_OBJECT_0 && !session.is_ready(), "Runtime did not exit cleanly");
    session.stop(); // Idempotent after the real process has stopped.
    std::cout << "Owned job stopped; process exit confirmed; stop_ms="
        << std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - stopping).count()
        << (read_pcm ? "; one reader integration request completed." : "; no synthesis requests sent.") << std::endl;
    return 0;
}
int cancellation_checks(wchar_t** args) {
    using outloud::SynthesisStatus;
    outloud::RustRuntimeSession session;
    const outloud::RustRuntimeSessionOptions options{fs::path(args[2]), fs::path(args[3]), fs::path(args[4])};
    session.start(options);
    const auto original_pid = runtime_child(options.executable);
    Handle original{OpenProcess(SYNCHRONIZE, FALSE, original_pid)};
    check(original.value != nullptr, "Cannot retain cancellation process handle");
    std::uint64_t samples = 0;
    const auto sink = [](void* context, std::span<const std::int16_t> pcm) { *static_cast<std::uint64_t*>(context) += pcm.size(); };
    std::stop_source already_stopped;
    already_stopped.request_stop();
    const auto skipped = session.read_pcm("Hello.", sink, &samples, already_stopped.get_token());
    check(skipped.status == SynthesisStatus::cancelled && !skipped.samples && !samples && session.is_ready() &&
        runtime_child(options.executable) == original_pid, "Pre-cancellation changed the warm session");
    std::stop_source late;
    const auto completed = session.read_pcm("Hello from Out Loud.", sink, &samples, late.get_token());
    check(completed.status == SynthesisStatus::completed && completed.samples == samples && samples != 0, "Normal request failed");
    late.request_stop();
    check(session.is_ready() && runtime_child(options.executable) == original_pid, "Late cancellation affected a completed request");
    std::cout << "Pre-cancel and late-cancel checks passed; completed synthesis preserved the warm process." << std::endl;

    struct Progress {
        std::promise<void> first;
        std::atomic<unsigned> callbacks{0};
        std::atomic<bool> active{false};
    } progress;
    auto first = progress.first.get_future();
    std::stop_source stop;
    const auto streaming_sink = [](void* context, std::span<const std::int16_t>) {
        auto& state = *static_cast<Progress*>(context);
        state.active = true;
        if (state.callbacks.fetch_add(1) == 0) { state.first.set_value(); }
        state.active = false;
    };
    std::string long_text;
    for (unsigned i = 0; i < 20; ++i) { long_text += "The reader is processing a cancellation check while the model is still generating speech. "; }
    auto reading = std::async(std::launch::async, [&] { return session.read_pcm(long_text, streaming_sink, &progress, stop.get_token()); });
    check(first.wait_for(5s) == std::future_status::ready, "No initial PCM during cancellation check");
    std::this_thread::sleep_for(10ms);
    const auto cancelling = std::chrono::steady_clock::now();
    stop.request_stop();
    check(reading.wait_for(5s) == std::future_status::ready, "Active HTTP request did not cancel promptly");
    const auto cancelled = reading.get();
    std::cout << "Cancellation outcome: status=" << static_cast<int>(cancelled.status) << " blocks=" << cancelled.blocks
        << " callbacks=" << progress.callbacks.load() << " active=" << progress.active.load() << " ready=" << session.is_ready()
        << " process_wait=" << WaitForSingleObject(original.value, 0) << std::endl;
    check(cancelled.status == SynthesisStatus::cancelled && cancelled.blocks == progress.callbacks && !progress.active &&
        !session.is_ready() && WaitForSingleObject(original.value, 0) == WAIT_OBJECT_0, "Cancelled request did not quiesce and stop its process");
    const auto calls = progress.callbacks.load();
    std::this_thread::sleep_for(20ms);
    check(progress.callbacks == calls, "PCM callback occurred after cancellation returned");
    std::cout << "Active request cancelled and process exit confirmed in "
        << std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - cancelling).count()
        << " ms; delivered prefix blocks=" << cancelled.blocks << std::endl;

    session.start(options);
    const auto second_pid = runtime_child(options.executable);
    check(second_pid != original_pid, "Cancellation reused the interrupted process");
    Handle second{OpenProcess(SYNCHRONIZE, FALSE, second_pid)};
    check(second.value != nullptr, "Cannot retain restarted process handle");
    struct HeldCallback {
        std::promise<void> entered;
        std::shared_future<void> release;
        unsigned calls = 0;
    } held;
    auto entered = held.entered.get_future();
    std::promise<void> release;
    held.release = release.get_future().share();
    std::stop_source held_stop;
    auto held_reading = std::async(std::launch::async, [&] {
        return session.read_pcm(long_text, [](void* context, std::span<const std::int16_t>) {
            auto& state = *static_cast<HeldCallback*>(context);
            ++state.calls; state.entered.set_value(); state.release.wait();
        }, &held, held_stop.get_token());
    });
    struct Release { std::promise<void>& promise; bool done = false; ~Release() { if (!done) { promise.set_value(); } } } gate{release};
    check(entered.wait_for(5s) == std::future_status::ready, "Callback lifetime check never entered its sink");
    held_stop.request_stop();
    check(held_reading.wait_for(20ms) == std::future_status::timeout, "Request returned with its sink still active");
    release.set_value(); gate.done = true;
    check(held_reading.wait_for(5s) == std::future_status::ready, "Request did not finish after its sink returned");
    const auto held_result = held_reading.get();
    check(held_result.status == SynthesisStatus::cancelled && held.calls == 1 && held_result.blocks == 1 &&
        !session.is_ready() && WaitForSingleObject(second.value, 0) == WAIT_OBJECT_0, "Held callback cancellation was unsafe");
    std::cout << "Cancellation waited for the in-flight PCM callback, then stopped the owned process." << std::endl;

    session.start(options);
    const auto clean_pid = runtime_child(options.executable);
    check(clean_pid != second_pid, "Callback interruption reused its process");
    samples = 0;
    const auto clean = session.read_pcm("Hello again.", sink, &samples);
    check(clean.status == SynthesisStatus::completed && clean.samples == samples && samples != 0 && session.is_ready() &&
        runtime_child(options.executable) == clean_pid, "Clean request after cancellation failed");
    // The old stop source remains harmless even while another session exists.
    stop.request_stop(); held_stop.request_stop();
    check(session.is_ready(), "A stale request cancellation stopped the new session");
    Handle clean_process{OpenProcess(SYNCHRONIZE, FALSE, clean_pid)};
    check(clean_process.value != nullptr, "Cannot retain final process handle");
    bool failed = false;
    try {
        (void)session.read_pcm("Hello.", [](void*, std::span<const std::int16_t>) { throw std::runtime_error("intentional sink failure"); });
    } catch (const std::runtime_error& error) { failed = std::string_view(error.what()) == "intentional sink failure"; }
    check(failed && !session.is_ready() && WaitForSingleObject(clean_process.value, 0) == WAIT_OBJECT_0,
        "Genuine sink failure was lost or left the runtime alive");
    session.stop();
    std::cout << "Clean restart/synthesis and genuine-failure cleanup passed; no audio files saved." << std::endl;
    return 0;
}
std::string start_failure(outloud::RustRuntimeSession& session, const outloud::RustRuntimeSessionOptions& options) {
    try { session.start(options); }
    catch (const std::exception& error) { return error.what(); }
    throw std::runtime_error("Unexpected startup success");
}
int child(int count, wchar_t** args) {
    // Verify the official argument shape and quoting, even with spaced Unicode paths.
    if (count != 10 || std::wstring_view(args[2]) != L"--config" || std::wstring_view(args[4]) != L"--default-voice" ||
        std::wstring_view(args[6]) != L"--host" || std::wstring_view(args[7]) != L"127.0.0.1" ||
        std::wstring_view(args[8]) != L"--port" || std::wstring_view(args[9]) != L"0" || !fs::is_regular_file(args[5])) { return 90; }
    // The owner must remove inherited credentials without altering the parent.
    if (GetEnvironmentVariableW(L"HF_TOKEN", nullptr, 0) != 0) { return 91; }
    const fs::path configuration(args[3]);
    const auto mode = text(configuration);
    if (mode == "exit") { return 23; }
    if (mode == "noise") {
        const std::string block(4096, 'x'); DWORD written = 0;
        for (unsigned i = 0; i < 32; ++i) {
            WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), block.data(), static_cast<DWORD>(block.size()), &written, nullptr);
            WriteFile(GetStdHandle(STD_ERROR_HANDLE), block.data(), static_cast<DWORD>(block.size()), &written, nullptr);
        }
        return 24;
    }
    // A grandchild inherits the job automatically, but no diagnostic handles.
    STARTUPINFOW startup{}; startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    std::wstring command = L"\"" + std::wstring(args[0]) + L"\" --descendant";
    if (!CreateProcessW(args[0], command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW,
        nullptr, nullptr, &startup, &process)) { return 92; }
    Handle root_thread{process.hThread}, descendant{process.hProcess};
    write(configuration.parent_path() / L"child.pid", std::to_string(process.dwProcessId));
    // Publish the root PID too, allowing the parent to retain both process handles.
    write(configuration.parent_path() / L"root.pid", std::to_string(GetCurrentProcessId()));
    Sleep(INFINITE);
    return 0;
}
} // namespace

int wmain(int count, wchar_t** args) {
    if (count > 1 && std::wstring_view(args[1]) == L"serve") { return child(count, args); }
    if (count > 1 && std::wstring_view(args[1]) == L"--descendant") { Sleep(INFINITE); return 0; }
    try {
        if (count == 5 && std::wstring_view(args[1]) == L"--cancel") { return cancellation_checks(args); }
        if (count == 5 && (std::wstring_view(args[1]) == L"--runtime" || std::wstring_view(args[1]) == L"--pcm")) {
            return real_runtime(args, std::wstring_view(args[1]) == L"--pcm");
        }
        check(count == 1, "Usage: outloud-rust-session-tests [--runtime|--pcm|--cancel <exe> <local.yaml> <voice.safetensors>]");
        rust_pcm_checks::run();
        std::cout << "Incremental PCM checks passed (fragmentation, format, truncation, sample alignment, bounded delivery).\n";
        Scratch scratch;
        const auto executable = scratch.path / L"runtime with spaces.exe";
        fs::copy_file(fs::absolute(args[0]), executable);
        const auto configuration = scratch.path / L"local model \u017e.yaml";
        const auto voice = scratch.path / L"local voice \u017e.safetensors";
        write(voice, "fixture"); write(configuration, "exit");
        outloud::RustRuntimeSessionOptions options{executable, configuration, voice, 2s};
        outloud::RustRuntimeSession session;
        check(!session.is_ready() && !session.exit_code(), "Empty session reports a running process");
        std::stop_source pre_cancelled;
        pre_cancelled.request_stop();
        const auto skipped = session.read_pcm("No request should be sent.", [](void*, std::span<const std::int16_t>) {
            throw std::runtime_error("Pre-cancelled request delivered PCM");
        }, nullptr, pre_cancelled.get_token());
        check(skipped.status == outloud::SynthesisStatus::cancelled && !skipped.samples && !skipped.blocks && !session.is_ready(),
            "Pre-cancelled request tried to use a session");
        session.start({},pre_cancelled.get_token());
        check(!session.is_ready(), "Pre-cancelled startup created a session");
        session.stop(); session.stop();
        auto invalid = options; invalid.configuration = L"relative.yaml";
        check(start_failure(session, invalid).find("absolute local") != std::string::npos, "Relative config accepted");
        invalid = options; invalid.executable = scratch.path / L"missing.exe";
        (void)start_failure(session, invalid);
        invalid = options; invalid.startup_timeout = 0ms;
        (void)start_failure(session, invalid);
        invalid = options; invalid.executable = voice; // Existing, non-executable input: CreateProcess failure.
        (void)start_failure(session, invalid);
        check(start_failure(session, options).find("code 23") != std::string::npos, "Child exit code or command quoting lost");
        check(!session.is_ready(), "Failed startup left ready state");
        write(configuration, "noise");
        check(start_failure(session, options).find("code 24") != std::string::npos, "Diagnostic pipes deadlocked");
        write(configuration, "hang");
        auto starting = std::async(std::launch::async, [&] { return start_failure(session, options); });
        const auto deadline = std::chrono::steady_clock::now() + 1500ms;
        while ((!fs::exists(scratch.path / L"child.pid") || !fs::exists(scratch.path / L"root.pid")) && std::chrono::steady_clock::now() < deadline) {
            std::this_thread::sleep_for(10ms);
        }
        const auto child_id = std::stoul(text(scratch.path / L"child.pid"));
        const auto root_id = std::stoul(text(scratch.path / L"root.pid"));
        Handle descendant{OpenProcess(SYNCHRONIZE, FALSE, child_id)}, root{OpenProcess(SYNCHRONIZE, FALSE, root_id)};
        check(descendant.value && root.value, "Cannot retain child process handles");
        check(starting.get().find("timed out") != std::string::npos, "Missing readiness timeout");
        check(WaitForSingleObject(root.value, 1000) == WAIT_OBJECT_0 && WaitForSingleObject(descendant.value, 1000) == WAIT_OBJECT_0,
            "Startup cleanup left owned processes running");
        session.stop(); session.stop();
        write(configuration, "exit");
        check(start_failure(session, options).find("code 23") != std::string::npos, "Session cannot restart after cleanup");
        std::cout << "Rust session lifecycle checks passed (validation, launch failure, exit, pipe drain, timeout, descendant cleanup, restart).\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n'; return 1;
    }
}
