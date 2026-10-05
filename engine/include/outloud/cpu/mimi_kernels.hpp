#pragma once
#include "outloud/cpu/kernels.hpp"
#include <vector>

namespace outloud::cpu {
// Fixed executed decoder configurations, channel-major [channels,time].
struct MimiConvolutionSpec {
    std::string_view name;
    std::uint32_t input, output, kernel, stride, groups, weight;
    int bias;
    bool transpose;
};
inline constexpr std::array<MimiConvolutionSpec, 12> mimi_convolutions = {{
    {"upsample.convtr", 512, 512, 32, 16, 512, 168, -1, true},
    {"decoder.model.0", 512, 512, 7, 1, 1, 126, 125, false},
    {"decoder.model.2", 512, 256, 12, 6, 1, 130, 129, true},
    {"decoder.model.3.block.1", 256, 128, 3, 1, 1, 132, 131, false},
    {"decoder.model.3.block.3", 128, 256, 1, 1, 1, 134, 133, false},
    {"decoder.model.5", 256, 128, 10, 5, 1, 136, 135, true},
    {"decoder.model.6.block.1", 128, 64, 3, 1, 1, 138, 137, false},
    {"decoder.model.6.block.3", 64, 128, 1, 1, 1, 140, 139, false},
    {"decoder.model.8", 128, 64, 8, 4, 1, 142, 141, true},
    {"decoder.model.9.block.1", 64, 32, 3, 1, 1, 144, 143, false},
    {"decoder.model.9.block.3", 32, 64, 1, 1, 1, 146, 145, false},
    {"decoder.model.11", 64, 1, 3, 1, 1, 128, 127, false},
}};
[[nodiscard]] std::size_t convolution_state_elements(const MimiConvolutionSpec& spec);
class MimiConvolutionKernels {
public:
    explicit MimiConvolutionKernels(KernelPath path = KernelPath::portable);
    void run(const PocketModelAsset& model, std::size_t configuration, std::span<const float> input,
             std::span<float> carry, std::span<float> output);
    [[nodiscard]] std::size_t scratch_bytes() const;
private:
    Kernels kernels_;
    std::vector<float> windows_;
    std::array<float, 4096> row_{};
};
void mimi_rope(std::span<float> queries, std::span<float> keys, std::uint32_t offset);
[[nodiscard]] bool mimi_visible(std::uint32_t query, std::uint32_t key) noexcept;
void mimi_attention(const Kernels& kernels, std::span<const float> queries,
                    std::span<const float> keys, std::span<const float> values,
                    std::uint32_t offset, std::span<float> output);
void elu(std::span<float> values);
} // namespace outloud::cpu
