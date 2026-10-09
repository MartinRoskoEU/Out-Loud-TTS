#pragma once
#include "outloud/sapi_voices.hpp"
#include "voice_library_fs.hpp"

namespace outloud::frontend {
// Private field mapping only. ABI packing and helper framing remain with their
// callers; only the mutation protocol carries duplicate-content IDs.
template<class Error> void library_cause(Error& error,const VoiceLibraryError& cause) {
    error.library_code = voice_library_error_name(cause.code());
    error.path = detail::library_fs::utf8(cause.path().wstring());
    if constexpr (requires { error.duplicate_ids; }) { error.duplicate_ids = cause.duplicate_ids(); }
    if (cause.import_error()) {
        error.import_code = voice_import_error_name(cause.import_error()->code());
        error.tensor = cause.import_error()->tensor();
    }
}
template<class Error> Error error_from(const VoiceLibraryError& cause) {
    Error error; error.domain = "library"; error.code = voice_library_error_name(cause.code());
    error.message = cause.what(); error.system_error = cause.system_error(); library_cause(error,cause); return error;
}
template<class Error> Error error_from(const SapiVoiceError& cause) {
    Error error; error.domain = "sapi"; error.code = sapi_voice_error_name(cause.code);
    error.message = cause.what(); error.system_error = cause.system_error;
    if (cause.library_error) { library_cause(error,*cause.library_error); }
    if (cause.validation_error) {
        error.import_code = voice_import_error_name(cause.validation_error->code());
        error.tensor = cause.validation_error->tensor();
        if (error.path.empty()) { error.path = detail::library_fs::utf8(cause.validation_error->path().wstring()); }
    }
    return error;
}
} // namespace outloud::frontend
