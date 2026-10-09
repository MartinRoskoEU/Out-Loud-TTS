#include "outloud/perf_diagnostics.hpp"
#include "outloud/flowlm.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace outloud {
namespace {
void require(bool condition, const char* message) {
    if (!condition) { throw std::invalid_argument(message); }
}
void observe(const FlowLMObserver& observer, std::string_view name, std::span<const float> values,
             std::initializer_list<std::uint32_t> dimensions) {
    if (observer) { observer(name, values, {dimensions.begin(), dimensions.size()}); }
}
} // namespace

FlowLMPrefillState::FlowLMPrefillState(OutLoudVoice voice, std::uint32_t capacity)
    : voice_(std::move(voice)), voice_sha_(voice_.source_sha256()), compatibility_(voice_.compatibility_id()), capacity_(capacity) {
    require(capacity > 0 && capacity <= 4096, "Invalid prefill suffix capacity");
    for (auto& layer : layers_) {
        layer.keys.resize(std::size_t(capacity) * 1024);
        layer.values.resize(std::size_t(capacity) * 1024);
    }
    reset();
}
void FlowLMPrefillState::reset() {
    for (auto& layer : layers_) { layer.offset = voice_.prefix_length(); }
    valid_ = true;
}
std::uint32_t FlowLMPrefillState::offset() const {
    require(valid_, "Incomplete prefill state; reset required");
    const auto offset = layers_[0].offset;
    for (const auto& layer : layers_) { require(layer.offset == offset, "Inconsistent layer offsets"); }
    return offset;
}
FlowLMLayerSuffix FlowLMPrefillState::layer(std::uint32_t index) const {
    if (index >= 6) { throw std::out_of_range("Invalid prefill layer index"); }
    const auto count = std::size_t(offset() - voice_.prefix_length()) * 1024;
    return {std::span(layers_[index].keys).first(count), std::span(layers_[index].values).first(count), layers_[index].offset};
}
std::size_t FlowLMPrefillState::suffix_storage_bytes() const { return std::size_t(capacity_) * 6 * 2 * 1024 * sizeof(float); }
std::span<const float> FlowLMResult::last_hidden() const {
    require(!normalized.empty() && normalized.size() % 1024 == 0, "Invalid FlowLM result dimensions");
    return std::span(normalized).last(1024);
}
FlowLMBackbone::FlowLMBackbone(PocketModelAsset model, OutLoudVoice voice, cpu::KernelPath path)
    : model_(std::move(model)), voice_(std::move(voice)), voice_sha_(voice_.source_sha256()), kernels_(path) { voice_.validate_against(model_); }
std::size_t FlowLMBackbone::workspace_storage_bytes() const {
    return (work_.hidden.capacity() + work_.norm.capacity() + work_.packed.capacity() + work_.queries.capacity()
          + work_.attention.capacity() + work_.update.capacity() + work_.ffn.capacity() + work_.scores.capacity())
          * sizeof(float) + cpu::Kernels::widening_scratch_bytes();
}
FlowLMPrefillState FlowLMBackbone::make_state(std::uint32_t suffix_capacity) const {
    return FlowLMPrefillState(voice_, suffix_capacity);
}
ModelTensorView FlowLMBackbone::layer_weight(std::uint32_t layer, std::uint32_t field) const {
    // Step 2C's frozen eight records per layer: linear1/2, norm1 bias/weight,
    // norm2 bias/weight, attention in/out projection. No dynamic name lookup.
    static_assert(static_cast<std::uint32_t>(PocketTensorId::flow_lm_transformer_layers_0_linear1_weight) == 77);
    static_assert(static_cast<std::uint32_t>(PocketTensorId::flow_lm_transformer_layers_5_self_attn_out_proj_weight) == 124);
    require(layer < 6 && field < 8, "Invalid FlowLM layer tensor");
    return model_.tensor(static_cast<PocketTensorId>(77 + layer * 8 + field));
}
std::vector<float> FlowLMBackbone::embed(std::span<const std::uint32_t> tokens) const {
    require(!tokens.empty() && tokens.size() <= 4096, "Invalid prefill token count");
    std::vector<float> result(tokens.size() * 1024);
    embed_into(tokens,result);
    return result;
}
void FlowLMBackbone::embed_into(std::span<const std::uint32_t> tokens, std::span<float> output) const {
    OUTLOUD_PERF_DOMAIN(flowlm); OUTLOUD_PERF_SCOPE(::outloud::perf::Stage::embedding);
    require(!tokens.empty() && tokens.size() <= 4096, "Invalid prefill token count");
    const auto table = model_.tensor(PocketTensorId::flow_lm_conditioner_embed_weight);
    require(output.size()==tokens.size()*1024,"Invalid embedding output dimensions");
    for (std::size_t i = 0; i < tokens.size(); ++i) {
        require(tokens[i] < 4001, "Embedding token ID outside table");
        widen_bf16(table.bf16_values.subspan(std::size_t(tokens[i]) * 1024, 1024), output.subspan(i * 1024, 1024));
    }
}
FlowLMResult FlowLMBackbone::prefill(std::span<const std::uint32_t> tokens, FlowLMPrefillState& state,
                                    const FlowLMObserver& observer) {
    FlowLMResult result;
    prefill_into(tokens,state,result,observer);
    return result;
}
void FlowLMBackbone::prefill_into(std::span<const std::uint32_t> tokens, FlowLMPrefillState& state,
                                 FlowLMResult& result, const FlowLMObserver& observer) {
    OUTLOUD_PERF_DOMAIN(flowlm); OUTLOUD_PERF_SCOPE(::outloud::perf::Stage::prefill);
    require(state.offset() == voice_.prefix_length(), "Text prefill requires fresh voice state");
    require(!tokens.empty() && tokens.size() <= 4096, "Invalid prefill token count");
    const auto size=tokens.size()*1024;
    // Exact reserve avoids vector's geometric growth retaining more than the token limit.
    // Norm is unused until backbone_into copies embeddings into hidden, before its
    // first layer normalization. Reuse that storage without retaining another vector.
    if (work_.norm.capacity()<size) { work_.norm.reserve(size); }
    work_.norm.resize(size);
    embed_into(tokens,work_.norm);
    observe(observer, "text.embedding", work_.norm, {static_cast<std::uint32_t>(tokens.size()), 1024});
    if (result.normalized.capacity()<size) { result.normalized.reserve(size); }
    backbone_into(work_.norm, state, result, observer);
}
FlowLMResult FlowLMBackbone::backbone(std::span<const float> embeddings, FlowLMPrefillState& state,
                                     const FlowLMObserver& observer) {
    FlowLMResult result;
    backbone_into(embeddings, state, result, observer);
    return result;
}
void FlowLMBackbone::backbone_into(std::span<const float> embeddings, FlowLMPrefillState& state,
                                 FlowLMResult& result, const FlowLMObserver& observer) {
    OUTLOUD_PERF_DOMAIN(flowlm); OUTLOUD_PERF_SCOPE(::outloud::perf::Stage::backbone);
    require(!embeddings.empty() && embeddings.size() % 1024 == 0, "Invalid backbone embedding dimensions");
    const auto tokens = embeddings.size() / 1024;
    const auto offset = state.offset();
    require(state.compatibility_ == model_.compatibility_id() && state.voice_sha_ == voice_sha_, "Prefill voice/model state mismatch");
    const auto previous = offset - voice_.prefix_length();
    require(tokens <= state.capacity_ && previous <= state.capacity_ - tokens, "Prefill suffix capacity exceeded");
    require(std::all_of(embeddings.begin(), embeddings.end(), [](float x) { return std::isfinite(x); }),
            "Nonfinite backbone input");
    const auto rows = static_cast<std::uint32_t>(tokens);
    const auto size = embeddings.size();
    auto& [hidden, norm, packed, queries, attention, update, ffn, scores] = work_;
    hidden.assign(embeddings.begin(), embeddings.end()); norm.resize(size); packed.resize(size * 3);
    queries.resize(size); attention.resize(size); update.resize(size); ffn.resize(size * 4);
    scores.reserve(std::size_t(voice_.prefix_length()) + state.capacity()); scores.resize(offset + tokens);
    result.normalized.resize(size); result.eos_logit = 0;
    result.scratch_bytes = (size * 12 + scores.size()) * sizeof(float) + cpu::Kernels::widening_scratch_bytes();
    // A failed computation/observer cannot leave a reusable partial six-layer append.
    state.valid_ = false;
    for (std::uint32_t layer = 0; layer < 6; ++layer) {
        const auto p = "layer" + std::to_string(layer) + ".";
        auto emit = [&](std::string_view name, std::span<const float> values, std::initializer_list<std::uint32_t> shape) {
            if (observer) { observe(observer, p + std::string(name), values, shape); }
        };
        emit("input", hidden, {rows, 1024});
        cpu::layer_norm({hidden, tokens, 1024}, layer_weight(layer, 3).bf16_values,
                        layer_weight(layer, 2).bf16_values, {norm, tokens, 1024});
        emit("norm1", norm, {rows, 1024});
        kernels_.linear({norm, tokens, 1024}, layer_weight(layer, 6), {packed, tokens, 3072});
        emit("qkv", packed, {rows, 3072});
        auto& suffix = state.layers_[layer];
        const auto keys = std::span(suffix.keys).subspan(std::size_t(previous) * 1024, size);
        const auto values = std::span(suffix.values).subspan(std::size_t(previous) * 1024, size);
        for (std::size_t token = 0; token < tokens; ++token) {
            std::copy_n(packed.data() + token * 3072, 1024, queries.data() + token * 1024);
            std::copy_n(packed.data() + token * 3072 + 1024, 1024, keys.data() + token * 1024);
            std::copy_n(packed.data() + token * 3072 + 2048, 1024, values.data() + token * 1024);
        }
        cpu::rope(queries, keys, tokens, offset); // imported prefix is never passed to RoPE
        emit("q_rope", queries, {rows, 16, 64});
        emit("k_suffix", keys, {rows, 16, 64});
        emit("v_suffix", values, {rows, 16, 64});
        cpu::causal_attention(kernels_, queries, tokens,
            {voice_.keys(layer), voice_.values(layer), std::span(suffix.keys).first((previous + tokens) * 1024),
             std::span(suffix.values).first((previous + tokens) * 1024), voice_.prefix_length(), previous}, attention, scores);
        emit("attention_context", attention, {rows, 1024});
        kernels_.linear({attention, tokens, 1024}, layer_weight(layer, 7), {update, tokens, 1024});
        emit("attention_update", update, {rows, 1024});
        cpu::residual_add(hidden, update);
        emit("attention_residual", hidden, {rows, 1024});
        cpu::layer_norm({hidden, tokens, 1024}, layer_weight(layer, 5).bf16_values,
                        layer_weight(layer, 4).bf16_values, {norm, tokens, 1024});
        emit("norm2", norm, {rows, 1024});
        kernels_.linear({norm, tokens, 1024}, layer_weight(layer, 0), {ffn, tokens, 4096});
        emit("linear1", ffn, {rows, 4096});
        cpu::gelu_tanh(ffn);
        emit("gelu", ffn, {rows, 4096});
        kernels_.linear({ffn, tokens, 4096}, layer_weight(layer, 1), {update, tokens, 1024});
        emit("ffn_update", update, {rows, 1024});
        cpu::residual_add(hidden, update);
        emit("output", hidden, {rows, 1024});
    }
    cpu::layer_norm({hidden, tokens, 1024}, model_.tensor(PocketTensorId::flow_lm_out_norm_weight).bf16_values,
                    model_.tensor(PocketTensorId::flow_lm_out_norm_bias).bf16_values, {result.normalized, tokens, 1024});
    observe(observer, "final.norm", result.normalized, {rows, 1024});
    observe(observer, "final.last_hidden", result.last_hidden(), {1024});
    kernels_.linear({result.last_hidden(), 1, 1024}, model_.tensor(PocketTensorId::flow_lm_out_eos_weight),
                    {{&result.eos_logit, 1}, 1, 1}, model_.tensor(PocketTensorId::flow_lm_out_eos_bias).bf16_values);
    observe(observer, "final.eos", {&result.eos_logit, 1}, {1});
    for (auto& layer : state.layers_) { layer.offset = offset + rows; }
    state.valid_ = true;
}
} // namespace outloud
