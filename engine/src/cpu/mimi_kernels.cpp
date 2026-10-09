#include "outloud/perf_diagnostics.hpp"
#include "outloud/cpu/mimi_kernels.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace outloud::cpu {
namespace {
void require(bool condition, const char* message) {
    if (!condition) { throw std::invalid_argument(message); }
}
std::size_t checked_product(std::size_t a,std::size_t b) {
    if (b && a>std::numeric_limits<std::size_t>::max()/b) { throw std::length_error("Mimi phase weight size overflow"); }
    return a*b;
}
}
std::size_t convolution_state_elements(const MimiConvolutionSpec& s) {
    return (s.transpose ? s.output : s.input) * (s.kernel - s.stride);
}
MimiConvolutionKernels::MimiConvolutionKernels(PocketModelAsset model,KernelPath path)
    : model_(std::move(model)),kernels_(path),windows_(368640) {
    std::size_t total=0;
    for (std::size_t i=0;i<mimi_convolutions.size();++i) {
        const auto& s=mimi_convolutions[i];
        if (!s.transpose || s.groups!=1) { continue; }
        require(s.stride>0 && s.kernel==2*s.stride,"Unsupported Mimi transpose phase layout");
        const auto tensor=model_.tensor(static_cast<PocketTensorId>(s.weight));
        const auto count=checked_product(checked_product(s.input,s.output),s.kernel);
        require(tensor.dimensions.size()==3 && tensor.dimensions[0]==s.input && tensor.dimensions[1]==s.output &&
                tensor.dimensions[2]==s.kernel && tensor.bf16_values.size()==count,"Mimi transpose weight shape mismatch");
        if (count>std::numeric_limits<std::size_t>::max()-total) { throw std::length_error("Mimi phase weight total overflow"); }
        phase_offsets_[i]=total; total+=count;
    }
    require(total<=phase_weights_.max_size() && total<=std::numeric_limits<std::size_t>::max()/sizeof(float),"Mimi phase weight allocation overflow");
    phase_weights_.resize(total);
    for (std::size_t i=0;i<mimi_convolutions.size();++i) {
        const auto& s=mimi_convolutions[i];
        if (!s.transpose || s.groups!=1) { continue; }
        const auto weight=model_.tensor(static_cast<PocketTensorId>(s.weight)).bf16_values;
        const auto width=std::size_t(s.input)*2;
        for (std::size_t c=0;c<s.output;++c) {
            for (std::size_t p=0;p<s.stride;++p) {
                auto row=std::span(phase_weights_).subspan(phase_offsets_[i]+(c*s.stride+p)*width,width);
                for (std::size_t in=0;in<s.input;++in) {
                    const auto w=(in*s.output+c)*s.kernel+p;
                    row[in*2]=bf16_to_float32(weight[w]);
                    row[in*2+1]=bf16_to_float32(weight[w+s.stride]);
                }
            }
        }
    }
}
std::size_t MimiConvolutionKernels::scratch_bytes() const {
    return windows_.capacity() * 4 + sizeof(row_) + Kernels::widening_scratch_bytes();
}
void MimiConvolutionKernels::run(std::size_t configuration,
        std::span<const float> input, std::span<float> carry, std::span<float> output) {
    require(configuration < mimi_convolutions.size(), "Unknown Mimi convolution");
    const auto& s = mimi_convolutions[configuration];
    OUTLOUD_PERF_SCOPE(::outloud::perf::conv_stage(configuration));
    const std::size_t steps = input.size() / s.input;
    require(steps > 0 && steps <= 1920 && input.size() == steps * s.input &&
            carry.size() == convolution_state_elements(s) &&
            output.size() == steps * s.output * (s.transpose ? s.stride : 1) &&
            input.data() != output.data(), "Mimi convolution dimensions/alias mismatch");
    const auto weight = model_.tensor(static_cast<PocketTensorId>(s.weight)).bf16_values;
    const auto bias = s.bias < 0 ? std::span<const std::uint16_t>{}
                               : model_.tensor(static_cast<PocketTensorId>(s.bias)).bf16_values;
    const auto add = [&](std::size_t c) { return bias.empty() ? 0.0f : bf16_to_float32(bias[c]); };
    if (!s.transpose) {
        const auto width = s.input * s.kernel, history = s.kernel - 1;
        require(steps >= history && steps * width <= windows_.size(), "Mimi convolution window capacity");
        for (std::size_t t = 0; t < steps; ++t) {
            for (std::size_t c = 0; c < s.input; ++c) {
                for (std::size_t k = 0; k < s.kernel; ++k) {
                    const auto p = t + k;
                    windows_[t * width + c * s.kernel + k] = p < history
                        ? carry[c * history + p] : input[c * steps + p - history];
                }
            }
        }
        for (std::size_t c = 0; c < s.output; ++c) {
            auto row = std::span(row_).first(width);
            widen_bf16(weight.subspan(c * width, width), row);
            kernels_.dot_rows(row, {std::span(windows_).first(steps * width), steps, width},
                              output.subspan(c * steps, steps), 1, add(c));
        }
        for (std::size_t c = 0; c < s.input; ++c) {
            for (std::size_t p = 0; p < history; ++p) { carry[c * history + p] = input[c * steps + steps - history + p]; }
        }
        return;
    }
    // All executed transposes have kernel=2*stride, dilation=1, padding=0.
    // Weight is [Cin,Cout/groups,kernel], not Conv1d's [Cout,Cin,kernel].
    const auto length = steps * s.stride;
    if (s.groups == s.input) {
        require(steps == 1, "Mimi upsample accepts one latent");
        for (std::size_t c = 0; c < s.input; ++c) {
            for (std::size_t p = 0; p < s.stride; ++p) {
                output[c * length + p] = input[c] * bf16_to_float32(weight[c * s.kernel + p]) + carry[c * s.stride + p];
                carry[c * s.stride + p] = input[c] * bf16_to_float32(weight[c * s.kernel + p + s.stride]);
            }
        }
        return;
    }
    const auto width = s.input * 2;
    require((steps + 1) * width <= windows_.size(), "Mimi transpose window capacity");
    for (std::size_t t = 0; t <= steps; ++t) {
        for (std::size_t c = 0; c < s.input; ++c) {
            windows_[t * width + c * 2] = t < steps ? input[c * steps + t] : 0.0f;
            windows_[t * width + c * 2 + 1] = t > 0 ? input[c * steps + t - 1] : 0.0f;
        }
    }
    for (std::size_t c = 0; c < s.output; ++c) {
        for (std::size_t p = 0; p < s.stride; ++p) {
            const auto row=std::span<const float>(phase_weights_).subspan(phase_offsets_[configuration]+(c*s.stride+p)*width,width);
            kernels_.dot_rows(row, {std::span(windows_).first(steps * width), steps, width},
                              output.subspan(c * length + p, length - p), s.stride, add(c));
            output[c * length + p] += carry[c * s.stride + p];
            const float tail = kernels_.dot(row, std::span(windows_).subspan(steps * width, width)) + add(c);
            carry[c * s.stride + p] = tail - add(c); // Source explicitly subtracts the tail bias.
        }
    }
}
void mimi_rope(std::span<float> queries, std::span<float> keys, std::uint32_t offset) {
    OUTLOUD_PERF_SCOPE(::outloud::perf::Stage::rope);
    require(queries.size() == 8192 && keys.size() == 8192 && offset <= 65520, "Mimi RoPE dimensions/offset");
    const float scale = static_cast<float>(-std::log(10000.0) * 2.0 / 64.0);
    for (std::size_t t = 0; t < 16; ++t) {
        for (std::size_t p = 0; p < 32; ++p) {
            const float angle = std::exp(static_cast<float>(p) * scale) * (static_cast<float>(offset) + static_cast<float>(t));
            const float co = std::cos(angle), si = std::sin(angle);
            for (std::size_t h = 0; h < 8; ++h) {
                const auto i = t * 512 + h * 64 + p * 2;
                for (auto x : {queries, keys}) {
                    const float re = x[i], im = x[i + 1];
                    x[i] = re * co - im * si; x[i + 1] = re * si + im * co;
                }
            }
        }
    }
}
bool mimi_visible(std::uint32_t query, std::uint32_t key) noexcept {
    return key <= query && query - key < 250;
}
void mimi_attention(const Kernels& kernels, std::span<const float> queries,
        std::span<const float> keys, std::span<const float> values, std::uint32_t offset, std::span<float> output) {
    OUTLOUD_PERF_SCOPE(::outloud::perf::Stage::attention);
    require(queries.size() == 8192 && output.size() == 8192 && keys.size() == (std::size_t(offset) + 16) * 512 &&
            values.size() == keys.size(), "Mimi attention dimensions");
    std::array<float, 250> scores;
    for (std::uint32_t t = 0; t < 16; ++t) {
        const auto end = offset + t + 1, start = end > 250 ? end - 250 : 0;
        for (std::size_t h = 0; h < 8; ++h) {
            float maximum = -std::numeric_limits<float>::infinity();
            OUTLOUD_PERF_CALL(::outloud::perf::Stage::scores, {
            for (auto p = start; p < end; ++p) {
                scores[p - start] = kernels.dot(queries.subspan(t * 512 + h * 64, 64), keys.subspan(p * 512 + h * 64, 64)) * 0.125f;
                maximum = std::max(maximum, scores[p - start]);
            }
            });
            OUTLOUD_PERF_CALL(::outloud::perf::Stage::softmax, {
            float sum = 0;
            for (auto p = start; p < end; ++p) { scores[p - start] = std::exp(scores[p - start] - maximum); sum += scores[p - start]; }
            require(std::isfinite(sum) && sum > 0, "Nonfinite Mimi softmax");
            for (auto p = start; p < end; ++p) { scores[p - start] /= sum; }
            });
            auto result = output.subspan(t * 512 + h * 64, 64);
            std::fill(result.begin(), result.end(), 0.0f);
            OUTLOUD_PERF_CALL(::outloud::perf::Stage::values, {
            for (auto p = start; p < end; ++p) {
                const auto v = values.subspan(p * 512 + h * 64, 64);
                for (std::size_t d = 0; d < 64; ++d) { result[d] += scores[p - start] * v[d]; }
            }
            });
        }
    }
}
void elu(std::span<float> values) {
    OUTLOUD_PERF_SCOPE(::outloud::perf::Stage::elu);
    for (auto& x : values) { if (x < 0) { x = std::exp(x) - 1.0f; } }
}
} // namespace outloud::cpu
