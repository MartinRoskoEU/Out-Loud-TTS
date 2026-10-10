#pragma once
#include "voice_manager.h"
#include "outloud/sapi_voices.hpp"

namespace outloud::frontend {
// Private C++ seam, not exported by the production DLL. Tests supply an isolated
// VoiceLibrary and fake registry; the snapshot/packing/error path is identical.
int32_t read_injected(VoiceLibrary library,std::shared_ptr<SapiVoiceRegistry> registry,
    outloud_vm_result** result,
    MachineVoiceLibraryPolicy policy = MachineVoiceLibraryPolicy::production) noexcept;
int32_t export_injected(VoiceLibrary, std::string_view id, std::string_view name, std::string_view hash,
    const std::filesystem::path& destination, std::string_view destination_identity, outloud_vm_result**) noexcept;
} // namespace outloud::frontend
