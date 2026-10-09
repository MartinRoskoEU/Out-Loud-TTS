#pragma once
#include <windows.h>
#include <cstdint>
#include <cstddef>

namespace outloud::sapi::logging {
// Closed vocabularies only: this API cannot accept text, paths or exception messages.
enum class Event { configuration, token_access, output_format, synthesis, write,
    bookmark_interest, bookmark_delivery, rate, volume, skip_info, skip_complete,
    create_engine, create_factory };
enum class Component { sapi, native, assets, model, tokenizer, voice };
enum class Kind { operation, asset_io, asset_invalid, asset_compatibility,
    allocation, invalid_argument, filesystem, unexpected, unexpected_cancellation };
struct Error {
    Event event;
    HRESULT hresult;
    DWORD system_error = 0;
    Component component = Component::sapi;
    Kind kind = Kind::operation;
};
inline constexpr std::uint64_t file_limit = 2 * 1024 * 1024;
inline constexpr DWORD mutex_wait_ms = 10;
inline constexpr std::size_t record_limit = 512;
void error(const Error& record) noexcept;

#ifdef OUTLOUD_ERROR_LOGGER_TEST
// Private test builds only. An unconfigured test build never uses LocalAppData.
enum class Fault { none, directory_access, disk_write, worker_exception, worker_allocation };
void configure(const wchar_t* directory, Fault fault = Fault::none) noexcept;
std::uint64_t attempts() noexcept;
Fault fault() noexcept;
HANDLE test_mutex_handle();
#endif
}
