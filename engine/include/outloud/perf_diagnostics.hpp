#pragma once
// Development profiling executable only. Ordinary libraries compile every macro away.
#ifdef OUTLOUD_PERF_DIAGNOSTICS
#include <array>
#include <chrono>
#include <cstdint>
#include <string_view>

namespace outloud::perf {
enum class Domain : unsigned { other, flowlm, sampler, mimi, count };
enum class Stage : unsigned {
    prefill, backbone, sampler, mimi_decode, mimi_transformer, seanet, embedding,
    input_projection, condition_projection, projection, qkv, attention, scores, softmax, values,
    attention_output, ffn1, ffn2, normalization, gelu, elu, silu, rope, residual, eos,
    time_features, time_mlp, adaln_linear, block_linear, final_projection, modulation,
    conv_initial, convt1, convr1, convt2, convr2, convt3, convr3, conv_final, upsample,
    widening, preparation, denormalization, noise, count
};
inline constexpr std::array<std::string_view, unsigned(Stage::count)> names = {
    "prefill", "backbone", "sampler_total", "mimi_decode", "mimi_transformer", "seanet", "embedding",
    "input_projection", "condition_projection", "projection", "qkv", "attention", "attention_scores", "softmax", "attention_values",
    "attention_output", "ffn_linear1", "ffn_linear2", "normalization", "gelu", "elu", "silu", "rope", "residual", "eos",
    "time_features", "time_mlp_linear", "adaln_linear", "block_linear", "final_projection", "modulation",
    "initial_conv1d", "group1_convtranspose", "group1_residual_convs", "group2_convtranspose", "group2_residual_convs",
    "group3_convtranspose", "group3_residual_convs", "final_conv1d", "depthwise_upsample", "bf16_widening", "weight_preparation", "latent_denormalization", "noise_retrieval"
};
inline constexpr std::array<std::string_view, 4> domains = {"other", "flowlm", "sampler", "mimi"};
struct Counter { std::uint64_t calls = 0, inclusive_ns = 0, exclusive_ns = 0; };
inline std::array<Counter, unsigned(Domain::count) * unsigned(Stage::count)> counters{};
inline bool enabled = false;
inline Domain domain = Domain::other;
struct Frame { std::uint64_t start, children; unsigned index; };
inline std::array<Frame, 64> stack{};
inline unsigned depth = 0;
inline std::uint64_t now() {
    return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count());
}
struct DomainScope {
    Domain previous;
    explicit DomainScope(Domain d) : previous(domain) { domain = d; }
    ~DomainScope() { domain = previous; }
};
class Scope {
public:
    explicit Scope(Stage stage) : active_(enabled) {
        if (active_) { stack[depth++] = {now(), 0, unsigned(domain) * unsigned(Stage::count) + unsigned(stage)}; }
    }
    ~Scope() {
        if (!active_) { return; }
        const auto end = now(); const auto f = stack[--depth]; const auto elapsed = end - f.start;
        auto& c = counters[f.index]; ++c.calls; c.inclusive_ns += elapsed; c.exclusive_ns += elapsed - f.children;
        if (depth) { stack[depth-1].children += elapsed; }
    }
private:
    bool active_;
};
inline Stage linear_stage(std::uint32_t id) {
    if (id == 72) { return Stage::input_projection; }
    if (id == 74) { return Stage::eos; }
    if (id >= 77 && id <= 124) {
        switch ((id-77)%8) { case 0:return Stage::ffn1; case 1:return Stage::ffn2; case 6:return Stage::qkv; default:return Stage::attention_output; }
    }
    if (id == 167) { return Stage::projection; }
    if (id >= 147 && id <= 166) {
        switch ((id-147)%10) { case 2:return Stage::ffn1; case 3:return Stage::ffn2; case 8:return Stage::qkv; default:return Stage::attention_output; }
    }
    if (id == 11) { return Stage::input_projection; }
    if (id == 5) { return Stage::condition_projection; }
    if (id == 9) { return Stage::final_projection; }
    if (id == 7) { return Stage::adaln_linear; }
    if (id >= 60 && id <= 71) { return Stage::time_mlp; }
    return (id-12)%8 == 1 ? Stage::adaln_linear : Stage::block_linear;
}
inline Stage conv_stage(std::size_t config) {
    constexpr std::array<Stage, 12> stages = {Stage::upsample, Stage::conv_initial, Stage::convt1, Stage::convr1,
        Stage::convr1, Stage::convt2, Stage::convr2, Stage::convr2, Stage::convt3, Stage::convr3, Stage::convr3, Stage::conv_final};
    return stages[config];
}
} // namespace outloud::perf
#define OUTLOUD_PERF_JOIN_IMPL(a,b) a##b
#define OUTLOUD_PERF_JOIN(a,b) OUTLOUD_PERF_JOIN_IMPL(a,b)
#define OUTLOUD_PERF_DOMAIN(d) ::outloud::perf::DomainScope OUTLOUD_PERF_JOIN(perf_domain_,__LINE__)(::outloud::perf::Domain::d)
#define OUTLOUD_PERF_SCOPE(s) ::outloud::perf::Scope OUTLOUD_PERF_JOIN(perf_scope_,__LINE__)(s)
#define OUTLOUD_PERF_CALL(s,...) do { OUTLOUD_PERF_SCOPE(s); __VA_ARGS__; } while(false)
#else
#define OUTLOUD_PERF_DOMAIN(d)
#define OUTLOUD_PERF_SCOPE(s)
#define OUTLOUD_PERF_CALL(s,...) __VA_ARGS__
#endif
