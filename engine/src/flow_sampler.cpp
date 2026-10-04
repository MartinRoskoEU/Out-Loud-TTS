#include "outloud/flow_sampler.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace outloud {
namespace {
void finite_input(std::span<const float> values, std::size_t expected) {
    if (values.size() != expected || !std::all_of(values.begin(), values.end(), [](float v) { return std::isfinite(v); })) {
        throw std::invalid_argument("Sampler input dimensions/nonfinite value");
    }
}
void observe(const SamplerObserver& observer, std::string_view name, std::span<const float> values) {
    if (observer) { observer(name, values); }
}
struct Scratch {
    std::array<float, 512> x, y, norm, h, update, s, t, condition_silu;
    std::array<float, 1536> parameters;
    std::array<float, 256> features;
};
static_assert(sizeof(Scratch) == FlowSampler::activation_scratch_bytes());
} // namespace
FlowSampler::FlowSampler(PocketModelAsset model, cpu::KernelPath path) : model_(std::move(model)), kernels_(path) {}
FlowSample FlowSampler::sample(std::span<const float> hidden, std::span<const float> supplied_noise,
                               const SamplerObserver& observer) {
    finite_input(hidden, 1024); finite_input(supplied_noise, 32);
    observe(observer, "input.hidden", hidden); observe(observer, "input.noise", supplied_noise);
    Scratch work;
    auto tensor = [&](std::uint32_t id) { return model_.tensor(static_cast<PocketTensorId>(id)); };
    auto linear = [&](std::span<const float> input, std::uint32_t weight, std::uint32_t bias, std::span<float> output) {
        kernels_.linear({input, 1, input.size()}, tensor(weight), {output, 1, output.size()}, tensor(bias).bf16_values);
    };
    linear(supplied_noise, 11, 10, work.x);
    observe(observer, "input.projected", work.x);
    linear(hidden, 5, 4, work.y);
    observe(observer, "condition.projected", work.y);
    auto time_embedding = [&](std::uint32_t index, std::span<float> output) {
        const float value = static_cast<float>(index); // s=0, t=1 only
        const std::uint32_t base = 60 + 6 * index;
        const auto p = index == 0 ? "time.s." : "time.t.";
        auto emit = [&](std::string_view name, std::span<const float> values) {
            if (observer) { observe(observer, std::string(p) + std::string(name), values); }
        };
        emit("scalar", {&value, 1});
        cpu::time_features(value, tensor(base).bf16_values, work.features);
        emit("features", work.features);
        linear(work.features, base + 2, base + 1, work.norm);
        emit("linear1", work.norm);
        cpu::silu(work.norm); emit("silu", work.norm);
        linear(work.norm, base + 4, base + 3, output); emit("mlp", output);
        cpu::sample_variance_norm(output, tensor(base + 5).bf16_values, output);
        emit("normalized", output);
    };
    time_embedding(0, work.s); time_embedding(1, work.t);
    for (std::size_t i = 0; i < 512; ++i) { work.y[i] = work.y[i] + (work.s[i] + work.t[i]) / 2.0f; }
    observe(observer, "condition.combined", work.y);
    static_assert(static_cast<std::uint32_t>(PocketTensorId::flow_lm_flow_net_res_blocks_0_adaLN_modulation_1_bias) == 12);
    static_assert(static_cast<std::uint32_t>(PocketTensorId::flow_lm_flow_net_res_blocks_5_mlp_2_weight) == 59);
    for (std::uint32_t block = 0; block < 6; ++block) {
        const auto base = 12 + block * 8;
        const auto p = "block" + std::to_string(block) + ".";
        auto emit = [&](std::string_view name, std::span<const float> values) {
            if (observer) { observe(observer, p + std::string(name), values); }
        };
        emit("input", work.x);
        work.condition_silu = work.y; cpu::silu(work.condition_silu); emit("condition_silu", work.condition_silu);
        linear(work.condition_silu, base + 1, base, work.parameters);
        const auto shift = std::span(work.parameters).first(512);
        const auto scale = std::span(work.parameters).subspan(512, 512);
        const auto gate = std::span(work.parameters).subspan(1024, 512);
        emit("shift", shift); emit("scale", scale); emit("gate", gate);
        cpu::sampler_layer_norm(work.x, tensor(base + 3).bf16_values, tensor(base + 2).bf16_values, work.norm);
        emit("norm", work.norm);
        for (std::size_t i = 0; i < 512; ++i) { work.h[i] = work.norm[i] * (1.0f + scale[i]) + shift[i]; }
        emit("modulated", work.h);
        linear(work.h, base + 5, base + 4, work.norm); emit("linear1", work.norm);
        cpu::silu(work.norm); emit("silu", work.norm);
        linear(work.norm, base + 7, base + 6, work.update); emit("linear2", work.update);
        for (std::size_t i = 0; i < 512; ++i) { work.update[i] = gate[i] * work.update[i]; }
        emit("gated_update", work.update);
        cpu::residual_add(work.x, work.update); emit("output", work.x);
    }
    work.condition_silu = work.y; cpu::silu(work.condition_silu);
    observe(observer, "final.condition_silu", work.condition_silu);
    const auto final_parameters = std::span(work.parameters).first(1024);
    linear(work.condition_silu, 7, 6, final_parameters);
    const auto shift = final_parameters.first(512), scale = final_parameters.last(512);
    observe(observer, "final.shift", shift); observe(observer, "final.scale", scale);
    cpu::sampler_layer_norm(work.x, {}, {}, work.norm); observe(observer, "final.norm", work.norm);
    for (std::size_t i = 0; i < 512; ++i) { work.h[i] = work.norm[i] * (1.0f + scale[i]) + shift[i]; }
    observe(observer, "final.modulated", work.h);
    FlowSample result;
    linear(work.h, 9, 8, result.velocity); observe(observer, "velocity", result.velocity);
    for (std::size_t i = 0; i < 32; ++i) { result.latent[i] = supplied_noise[i] + result.velocity[i]; }
    observe(observer, "latent", result.latent);
    return result;
}
DeterministicCandidate FlowSampler::bos_candidate(FlowLMBackbone& backbone, FlowLMPrefillState& state,
        std::span<const float> supplied_noise, const SamplerObserver& observer) {
    DeterministicCandidate result;
    candidate_into(backbone, state, {}, supplied_noise, result, observer);
    return result;
}
DeterministicCandidate FlowSampler::frame_candidate(FlowLMBackbone& backbone, FlowLMPrefillState& state,
        std::span<const float> previous, std::span<const float> noise, const SamplerObserver& observer) {
    finite_input(previous, 32);
    DeterministicCandidate result;
    candidate_into(backbone, state, previous, noise, result, observer);
    return result;
}
void FlowSampler::bos_candidate_into(FlowLMBackbone& backbone, FlowLMPrefillState& state,
                                    std::span<const float> noise, DeterministicCandidate& result) {
    candidate_into(backbone, state, {}, noise, result, {});
}
void FlowSampler::frame_candidate_into(FlowLMBackbone& backbone, FlowLMPrefillState& state,
        std::span<const float> previous, std::span<const float> noise, DeterministicCandidate& result) {
    finite_input(previous, 32);
    candidate_into(backbone, state, previous, noise, result, {});
}
void FlowSampler::candidate_into(FlowLMBackbone& backbone, FlowLMPrefillState& state, std::span<const float> input,
        std::span<const float> supplied_noise, DeterministicCandidate& result, const SamplerObserver& observer) {
    finite_input(supplied_noise, 32); // reject before mutating backbone state
    std::array<float, 32> bos;
    std::array<float, 1024> projected;
    if (input.empty()) { widen_bf16(model_.tensor(PocketTensorId::flow_lm_bos_emb).bf16_values, bos); input = bos; }
    kernels_.linear({input, 1, 32}, model_.tensor(PocketTensorId::flow_lm_input_linear_weight), {projected, 1, 1024});
    backbone.backbone_into(projected, state, result.backbone);
    result.sample = sample(result.backbone.last_hidden(), supplied_noise, observer);
}
} // namespace outloud
