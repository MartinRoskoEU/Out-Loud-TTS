#pragma once
#include "voice_manager.h"
#include "outloud/sapi_voices.hpp"

namespace outloud::frontend {
// Private C++ seam, not exported by the production DLL. Tests supply an isolated
// VoiceLibrary and fake registry; the snapshot/packing/error path is identical.
int32_t read_injected(VoiceLibrary library,std::shared_ptr<SapiVoiceRegistry> registry,
    outloud_vm_result** result,
    MachineVoiceLibraryPolicy policy = MachineVoiceLibraryPolicy::production) noexcept;
} // namespace outloud::frontend
