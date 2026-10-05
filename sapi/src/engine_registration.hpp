#pragma once
#include "outloud/sapi_voices.hpp"

namespace outloud::detail {
// Shared by the real Registry64 adapter and isolated registration fixtures.
// Reads/pins local files only; never touches registry or deployment contents.
void validate_engine_registration(const std::optional<SapiRegistryValue>& registered,
    const std::filesystem::path& selected_dll);
}
