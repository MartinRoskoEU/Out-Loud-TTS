#pragma once
#include "outloud/rust_runtime_session.hpp"
#include <memory>
#include <exception>
#include <string>

namespace outloud {
class RustSelectionCancelled final : public std::exception {
public:
    const char* what() const noexcept override { return "Rust asset preparation cancelled"; }
};
// One synthesis resident owns this lease before starting Rust and releases it
// AFTER stopping Rust. No voice-to-model association or provenance is recorded.
class RustModelSelection {
public:
    // Resolve the machine-wide explicit WinUI selection, schema-2 inventory,
    // and active Out Loud runtime. bundled_runtime is this app's deployed copy.
    static std::unique_ptr<RustModelSelection> acquire(const std::filesystem::path& bundled_runtime,
        const std::filesystem::path& managed_voice, std::string_view library_id, std::stop_token cancellation = {});
    ~RustModelSelection();
    RustModelSelection(const RustModelSelection&) = delete;
    [[nodiscard]] const RustRuntimeSessionOptions& options() const noexcept;
    [[nodiscard]] const std::string& identity() const noexcept;
    // Read only. Used between utterances to invalidate a resident after Settings
    // changes. Missing/invalid selection fails; never chooses a replacement.
    static std::string selected_identity();
private:
    struct Impl;
    explicit RustModelSelection(std::unique_ptr<Impl>);
    std::unique_ptr<Impl> impl_;
};
// Pins/validates identity and the x86-64-v3 CPU before any execution. No selection,
// download, metadata initialization, or user-data mutation.
void verify_rust_runtime(const std::filesystem::path& bundled_runtime);
} // namespace outloud
