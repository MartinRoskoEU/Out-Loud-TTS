#pragma once
#include <stdint.h>

#ifdef OUTLOUD_VM_BUILD
#define OUTLOUD_VM_API __declspec(dllexport)
#else
#define OUTLOUD_VM_API __declspec(dllimport)
#endif
#ifdef __cplusplus
#define OUTLOUD_VM_NOEXCEPT noexcept
extern "C" {
#else
#define OUTLOUD_VM_NOEXCEPT
#endif

// ABI v1: Windows x64, __cdecl, pack 8. Every string is NUL-terminated UTF-8.
// All record/string pointers are borrowed from one immutable opaque result.
// Keep that result alive until copying is complete. Free only the result with
// outloud_vm_result_free; never free individual fields or use a CRT allocator.
typedef struct outloud_vm_result outloud_vm_result;
enum outloud_vm_status {
    OUTLOUD_VM_OK = 0, OUTLOUD_VM_ERROR = 1,
    OUTLOUD_VM_INVALID_ARGUMENT = 2, OUTLOUD_VM_OUT_OF_MEMORY = 3,
    OUTLOUD_VM_INTERNAL_ERROR = 4
};
#pragma pack(push,8)
typedef struct outloud_vm_error_v1 {
    const char* domain;
    const char* code;
    const char* message;
    const char* path;
    const char* library_code;
    const char* import_code;
    const char* tensor;
    uint32_t system_error;
    uint32_t reserved;
} outloud_vm_error_v1;
typedef struct outloud_vm_voice_v1 {
    const char* library_id;
    const char* display_name;        // NULL when metadata is unavailable
    const char* health;              // exact native VoiceHealth name
    const char* library_detail;
    const char* sapi_status;         // exact native status; NULL if inspect failed
    const char* sapi_detail;
    const char* compatibility_id;    // NULL when metadata is unavailable
    const char* import_kind;         // pocket_prepared or olv; NULL without metadata
    const outloud_vm_error_v1* sapi_error; // per-row failure, not a fabricated status
    uint32_t metadata_present;
    uint32_t olv_version;            // meaningful only when metadata_present==1
    uint32_t provenance_verified;    // meaningful only when metadata_present==1
    uint32_t reserved;
} outloud_vm_voice_v1;
typedef struct outloud_vm_snapshot_v1 {
    const char* library_root;
    uint64_t voice_count;
    const outloud_vm_voice_v1* voices;
} outloud_vm_snapshot_v1;
// Additive read-only metadata extension; ABI v1 records/exports are unchanged.
typedef struct outloud_vm_content_v1 {
    const char* olv_sha256;
    const char* source_sha256;
    const char* source_filename;
    const char* imported_utc;
} outloud_vm_content_v1;
#pragma pack(pop)

OUTLOUD_VM_API uint32_t __cdecl outloud_vm_abi_version(void) OUTLOUD_VM_NOEXCEPT;
// Uses the existing production known-folder root and machine Registry64 scope.
// No configurable root/scope/mutation export. Each call owns independent state.
// *result starts NULL. OK/ERROR return an owned result. ERROR has a structured
// error and no snapshot. Other statuses return no result.
OUTLOUD_VM_API int32_t __cdecl outloud_vm_read(outloud_vm_result** result) OUTLOUD_VM_NOEXCEPT;
OUTLOUD_VM_API const outloud_vm_snapshot_v1* __cdecl outloud_vm_result_snapshot(const outloud_vm_result* result) OUTLOUD_VM_NOEXCEPT;
OUTLOUD_VM_API const outloud_vm_error_v1* __cdecl outloud_vm_result_error(const outloud_vm_result* result) OUTLOUD_VM_NOEXCEPT;
OUTLOUD_VM_API const outloud_vm_content_v1* __cdecl outloud_vm_result_content(const outloud_vm_result* result, uint64_t row) OUTLOUD_VM_NOEXCEPT;
OUTLOUD_VM_API void __cdecl outloud_vm_result_free(outloud_vm_result* result) OUTLOUD_VM_NOEXCEPT;
#ifdef __cplusplus
}
#endif
