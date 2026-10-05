#include "outloud/mimi.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace outloud {
namespace {
void require(bool condition, const char* message) {
    if (!condition) { throw std::invalid_argument(message); }
}
void transpose(std::span<const float> input, std::span<float> output, std::size_t rows, std::size_t columns) {
    for (std::size_t r = 0; r < rows; ++r) {
        for (std::size_t c = 0; c < columns; ++c) { output[c * rows + r] = input[r * columns + c]; }
    }
}
}
MimiDecoderState::MimiDecoderState(std::uint32_t maximum_frames) : capacity_(maximum_frames * 16) {
    require(maximum_frames > 0 && maximum_frames <= 4096, "Mimi frame capacity must be 1..4096");
    for (std::size_t i = 0; i < 2; ++i) { keys_[i].resize(std::size_t(capacity_) * 512); values_[i].resize(std::size_t(capacity_) * 512); }
    for (std::size_t i = 0; i < carry_.size(); ++i) { carry_[i].resize(cpu::convolution_state_elements(cpu::mimi_convolutions[i])); }
}
void MimiDecoderState::reset() {
    require(!busy_, "Cannot reset Mimi state during decoding");
    offset_ = 0; valid_ = true;
    for (auto& carry : carry_) { std::fill(carry.begin(), carry.end(), 0.0f); }
    // KV storage is retained; offset hides stale positions, which are overwritten before use.
}
std::span<const float> MimiDecoderState::keys(std::size_t layer) const {
    require(layer < 2 && valid_, "Invalid Mimi KV state");
    return std::span(keys_[layer]).first(std::size_t(offset_) * 512);
}
std::span<const float> MimiDecoderState::values(std::size_t layer) const {
    require(layer < 2 && valid_, "Invalid Mimi KV state");
    return std::span(values_[layer]).first(std::size_t(offset_) * 512);
}
std::span<const float> MimiDecoderState::convolution_state(std::size_t configuration) const {
    require(configuration < 12 && valid_, "Invalid Mimi convolution state"); return carry_[configuration];
}
std::size_t MimiDecoderState::kv_bytes() const {
    std::size_t bytes = 0;
    for (std::size_t i = 0; i < 2; ++i) { bytes += (keys_[i].capacity() + values_[i].capacity()) * 4; } return bytes;
}
std::size_t MimiDecoderState::convolution_bytes() const {
    std::size_t bytes = 0; for (const auto& c : carry_) { bytes += c.capacity() * 4; } return bytes;
}
MimiDecoder::MimiDecoder(PocketModelAsset model, cpu::KernelPath path)
    : model_(std::move(model)), kernels_(path), convolutions_(path), a_(122880), b_(122880), residual_(122880),
      hidden_(8192), norm_(8192), packed_(24576), queries_(8192), keys_(8192), values_(8192),
      attention_(8192), update_(8192), ffn_(32768) {}
std::size_t MimiDecoder::scratch_bytes() const {
    std::size_t bytes = convolutions_.scratch_bytes() + cpu::Kernels::widening_scratch_bytes() + sizeof(denormalized_) + sizeof(projection_);
    for (const auto* x : {&a_, &b_, &residual_, &hidden_, &norm_, &packed_, &queries_, &keys_, &values_, &attention_, &update_, &ffn_}) {
        bytes += x->capacity() * 4;
    }
    return bytes; // PCM frame is separate (1920*4).
}
void MimiDecoder::transformer(MimiDecoderState& state, const MimiObserver& observer) {
    auto tensor = [&](std::uint32_t id) { return model_.tensor(static_cast<PocketTensorId>(id)); };
    auto emit = [&](std::size_t layer, std::string_view field, std::span<const float> x) {
        if (observer) { observer("layer" + std::to_string(layer) + "." + std::string(field), x); }
    };
    if (observer) { observer("transformer.input", hidden_); }
    for (std::uint32_t layer = 0; layer < 2; ++layer) {
        const auto base = 147 + 10 * layer;
        emit(layer, "input", hidden_);
        cpu::layer_norm({hidden_, 16, 512}, tensor(base + 5).bf16_values, tensor(base + 4).bf16_values, {norm_, 16, 512});
        emit(layer, "norm1", norm_);
        kernels_.linear({norm_, 16, 512}, tensor(base + 8), {packed_, 16, 1536});
        emit(layer, "qkv", packed_);
        for (std::size_t t = 0; t < 16; ++t) {
            std::copy_n(packed_.begin() + t * 1536, 512, queries_.begin() + t * 512);
            std::copy_n(packed_.begin() + t * 1536 + 512, 512, keys_.begin() + t * 512);
            std::copy_n(packed_.begin() + t * 1536 + 1024, 512, values_.begin() + t * 512);
        }
        emit(layer, "v", values_);
        cpu::mimi_rope(queries_, keys_, state.offset_);
        emit(layer, "q_rope", queries_); emit(layer, "k_rope", keys_);
        std::copy(keys_.begin(), keys_.end(), state.keys_[layer].begin() + std::size_t(state.offset_) * 512);
        std::copy(values_.begin(), values_.end(), state.values_[layer].begin() + std::size_t(state.offset_) * 512);
        const auto used = std::size_t(state.offset_ + 16) * 512;
        cpu::mimi_attention(kernels_, queries_, std::span(state.keys_[layer]).first(used),
                            std::span(state.values_[layer]).first(used), state.offset_, attention_);
        emit(layer, "attention_context", attention_);
        kernels_.linear({attention_, 16, 512}, tensor(base + 9), {update_, 16, 512});
        emit(layer, "attention_update", update_);
        for (std::size_t i = 0; i < update_.size(); ++i) { update_[i] *= bf16_to_float32(tensor(base).bf16_values[i % 512]); }
        emit(layer, "attention_scale", update_);
        cpu::residual_add(hidden_, update_); emit(layer, "attention_residual", hidden_);
        cpu::layer_norm({hidden_, 16, 512}, tensor(base + 7).bf16_values, tensor(base + 6).bf16_values, {norm_, 16, 512});
        emit(layer, "norm2", norm_);
        kernels_.linear({norm_, 16, 512}, tensor(base + 2), {ffn_, 16, 2048}); emit(layer, "linear1", ffn_);
        cpu::gelu_tanh(ffn_); emit(layer, "gelu", ffn_);
        kernels_.linear({ffn_, 16, 2048}, tensor(base + 3), {update_, 16, 512}); emit(layer, "ffn_update", update_);
        for (std::size_t i = 0; i < update_.size(); ++i) { update_[i] *= bf16_to_float32(tensor(base + 1).bf16_values[i % 512]); }
        emit(layer, "ffn_scale", update_); cpu::residual_add(hidden_, update_); emit(layer, "output", hidden_);
    }
    if (observer) { observer("transformer.output", hidden_); }
}
std::span<const float> MimiDecoder::decode_frame(std::span<const float> latent,
        MimiDecoderState& state, const MimiObserver& observer) {
    require(!busy_ && !state.busy_, "Reentrant Mimi decoding");
    require(latent.size() == 32 && std::all_of(latent.begin(), latent.end(), [](float x) { return std::isfinite(x); }),
            "Mimi requires one finite normalized 32-D latent");
    require(state.valid_ && state.offset_ <= state.capacity_ - 16, "Mimi state needs reset or has exhausted capacity");
    busy_ = state.busy_ = true; state.valid_ = false;
    try {
        auto tensor = [&](std::uint32_t id) { return model_.tensor(static_cast<PocketTensorId>(id)); };
        if (observer) { observer("normalized", latent); }
        const auto mean = tensor(2).bf16_values, std = tensor(3).bf16_values;
        for (std::size_t i = 0; i < 32; ++i) { denormalized_[i] = latent[i] * bf16_to_float32(std[i]) + bf16_to_float32(mean[i]); }
        if (observer) { observer("denormalized", denormalized_); observer("projection.input", denormalized_); }
        auto weight = tensor(167);
        const std::array<std::uint32_t, 2> dimensions = {512, 32}; weight.dimensions = dimensions;
        kernels_.linear({denormalized_, 1, 32}, weight, {projection_, 1, 512});
        if (observer) { observer("projection.output", projection_); }
        auto conv = [&](std::size_t configuration, std::span<const float> in, std::span<float> out) {
            const auto& spec = cpu::mimi_convolutions[configuration];
            if (observer) { observer(std::string(spec.name) + ".input", in); observer(std::string(spec.name) + ".state_before", state.carry_[configuration]); }
            convolutions_.run(model_, configuration, in, state.carry_[configuration], out);
            if (observer) { observer(std::string(spec.name) + ".output", out); observer(std::string(spec.name) + ".state_after", state.carry_[configuration]); }
        };
        auto a = std::span(a_), b = std::span(b_), residual = std::span(residual_);
        conv(0, projection_, a.first(8192));
        transpose(a.first(8192), hidden_, 512, 16);
        transformer(state, observer);
        transpose(hidden_, a.first(8192), 16, 512);
        conv(1, a.first(8192), b.first(8192));
        std::size_t channels = 512, steps = 16;
        for (std::size_t group = 0; group < 3; ++group) {
            auto current = b.first(channels * steps); cpu::elu(current);
            const auto next_channels = channels / 2, next_steps = steps * PocketArchitecture::decoder_ratios[group];
            auto upsampled = a.first(next_channels * next_steps);
            const auto configuration = 2 + 3 * group;
            conv(configuration, current, upsampled);
            std::copy(upsampled.begin(), upsampled.end(), residual.begin());
            cpu::elu(upsampled);
            auto compressed = b.first(next_channels / 2 * next_steps);
            conv(configuration + 1, upsampled, compressed); cpu::elu(compressed);
            auto expanded = a.first(next_channels * next_steps);
            conv(configuration + 2, compressed, expanded);
            cpu::residual_add(expanded, residual.first(expanded.size()));
            if (observer) { observer("decoder.model." + std::to_string(3 + group * 3) + ".output", expanded); }
            std::copy(expanded.begin(), expanded.end(), b.begin());
            channels = next_channels; steps = next_steps;
        }
        auto final = b.first(64 * 1920); cpu::elu(final); conv(11, final, pcm_);
        require(std::all_of(pcm_.begin(), pcm_.end(), [](float x) { return std::isfinite(x); }), "Nonfinite Mimi PCM");
        if (observer) { observer("pcm", pcm_); }
        state.offset_ += 16; state.valid_ = true;
        if (observer) {
            for (std::size_t i = 0; i < 2; ++i) {
                observer("layer" + std::to_string(i) + ".cache_k", state.keys(i));
                observer("layer" + std::to_string(i) + ".cache_v", state.values(i));
            }
        }
        busy_ = state.busy_ = false;
        return pcm_;
    } catch (...) { state.valid_ = false; busy_ = state.busy_ = false; throw; }
}
} // namespace outloud
