#pragma once
#include "outloud/sapi_voices.hpp"
#include <windows.h>

namespace outloud::detail {
struct VoiceRegistryLocation { HKEY hive; REGSAM view; };
inline VoiceRegistryLocation voice_registry_location(SapiVoiceScope scope) {
    if (scope==SapiVoiceScope::machine) { return {HKEY_LOCAL_MACHINE,KEY_WOW64_64KEY}; }
    if (scope==SapiVoiceScope::user) { return {HKEY_CURRENT_USER,KEY_WOW64_64KEY}; }
    throw SapiVoiceError(SapiVoiceErrorCode::unsupported_scope,"Unknown registry scope; no fallback.");
}
} // namespace outloud::detail
