#pragma once
#include <stdexcept>
#include <string_view>

namespace outloud::test {
struct ErrorLimits { double max_absolute, rms; };
// Frozen AFTER first measurements against the original upstream fixtures.
// Approximately 2x observed maxima, rounded upward; see docs/NATIVE_FLOWLM.md.
// Relative errors are reported independently: near-zero cancellation makes a
// uniform relative limit misleading. Every checkpoint must meet BOTH bounds.
inline ErrorLimits error_limits(std::string_view name, bool primitive) {
    if (primitive) {
        if (name == "input_linear") { return {2e-9, 2e-10}; }
        if (name == "eos") { return {4e-6, 4e-6}; }
        if (name.ends_with(".qkv")) { return {6e-6, 4e-7}; }
        if (name.ends_with(".out_proj")) { return {6e-7, 6e-8}; }
        if (name.ends_with(".linear1")) { return {4e-6, 4e-7}; }
        if (name.ends_with(".linear2")) { return {2e-6, 2e-7}; }
        if (name.ends_with(".norm1") || name.ends_with(".norm2")) { return {1e-6, 1.1e-7}; }
        if (name.ends_with(".gelu")) { return {3.2e-7, 2.2e-8}; }
        if (name.ends_with("_rope")) { return {5.5e-5, 3e-6}; }
        if (name.ends_with(".attention")) { return {1.2e-6, 9e-8}; }
    } else {
        if (name == "text.embedding") { return {0, 0}; }
        if (name == "final.norm") { return {4.5e-6, 9e-7}; }
        if (name == "final.last_hidden") { return {3e-6, 9e-7}; }
        if (name == "single.last_hidden") { return {4e-6, 1.1e-6}; }
        if (name == "final.eos" || name == "single.eos") { return {1.2e-5, 1.2e-5}; }
        if (name.ends_with(".input")) { return {1.6e-6, 3e-7}; }
        if (name.ends_with(".norm1")) { return {4.5e-6, 9e-7}; }
        if (name.ends_with(".qkv")) { return {1e-5, 9e-7}; }
        if (name.ends_with(".q_rope") || name.ends_with(".k_suffix")) { return {5.5e-5, 3e-6}; }
        if (name.ends_with(".v_suffix")) { return {2.5e-6, 5e-7}; }
        if (name.ends_with(".attention_context")) { return {2.5e-6, 2.5e-7}; }
        if (name.ends_with(".attention_update")) { return {1.2e-6, 1.4e-7}; }
        if (name.ends_with(".attention_residual")) { return {2e-6, 3e-7}; }
        if (name.ends_with(".norm2")) { return {6.5e-6, 1.3e-6}; }
        if (name.ends_with(".linear1")) { return {7e-6, 9e-7}; }
        if (name.ends_with(".gelu")) { return {4.5e-6, 2.5e-7}; }
        if (name.ends_with(".ffn_update")) { return {3e-6, 3e-7}; }
        if (name.ends_with(".output")) { return {4e-6, 4e-7}; }
    }
    throw std::runtime_error("Missing explicit numerical error limits");
}
} // namespace outloud::test
