#pragma once

#include "outloud/pocket_model_schema.hpp"

#include <bit>
#include <cstddef>
#include <filesystem>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace outloud {
namespace detail { class MappedFile; }

enum class AssetDtype : std::uint32_t { bf16 = 1, float32 = 2 };

// Schema 1's fixed non-weight architectural constants; no runtime JSON/YAML.
struct PocketArchitecture {
    static constexpr std::uint32_t sample_rate = 24000, channels = 1, frame_samples = 1920;
    static constexpr std::uint32_t flow_dim = 1024, flow_layers = 6, flow_heads = 16, head_dim = 64;
    static constexpr std::uint32_t latent_dim = 32, flow_ffn = 4096, rope_period = 10000;
    static constexpr std::uint32_t sampler_dim = 512, sampler_layers = 6, time_features = 256;
    static constexpr std::uint32_t mimi_dim = 512, mimi_layers = 2, mimi_heads = 8;
    static constexpr std::uint32_t mimi_ffn = 2048, mimi_context = 250, mimi_steps_per_latent = 16;
    static constexpr std::array<std::uint32_t, 3> decoder_ratios = {6, 5, 4};
};

[[nodiscard]] inline float bf16_to_float32(std::uint16_t bits) noexcept {
    return std::bit_cast<float>(std::uint32_t(bits) << 16);
}
void widen_bf16(std::span<const std::uint16_t> input, std::span<float> output);

struct ModelTensorView {
    PocketTensorId id;
    std::string_view name;
    AssetDtype dtype;
    std::span<const std::uint32_t> dimensions;
    std::span<const std::uint16_t> bf16_values;
    [[nodiscard]] std::vector<float> to_float32() const;
};

// These assets own immutable read-only mappings. Views are valid while an owning asset lives.
class PocketModelAsset {
public:
    explicit PocketModelAsset(const std::filesystem::path& path);
    void validate() const;
    [[nodiscard]] ModelTensorView tensor(PocketTensorId id) const;
    [[nodiscard]] static constexpr std::size_t tensor_count() { return pocket_tensor_schema.size(); }
    [[nodiscard]] static constexpr std::uint32_t version() { return 1; }
    [[nodiscard]] static constexpr std::uint64_t element_count() { return 99719330; }
    [[nodiscard]] static constexpr std::uint64_t payload_bytes() { return pocket_model_payload_bytes; }
    [[nodiscard]] static constexpr std::string_view source_sha256() { return pocket_model_source_sha256; }
    [[nodiscard]] static constexpr std::string_view compatibility_id() { return pocket_model_compatibility_id; }
private:
    std::shared_ptr<detail::MappedFile> file_;
};

class OutLoudVoice {
public:
    explicit OutLoudVoice(const std::filesystem::path& path);
    void validate() const;
    void validate_against(const PocketModelAsset& model) const;
    [[nodiscard]] std::uint32_t prefix_length() const;
    [[nodiscard]] static constexpr std::uint32_t version() { return 1; }
    [[nodiscard]] static constexpr std::uint32_t schema_version() { return 1; }
    [[nodiscard]] static constexpr std::uint32_t layers() { return 6; }
    [[nodiscard]] static constexpr std::uint32_t heads() { return 16; }
    [[nodiscard]] static constexpr std::uint32_t head_dimension() { return 64; }
    [[nodiscard]] std::uint64_t payload_bytes() const;
    [[nodiscard]] std::string source_sha256() const;
    [[nodiscard]] std::string compatibility_id() const;
    [[nodiscard]] std::span<const float> cache(std::uint32_t layer) const;
    [[nodiscard]] std::span<const float> keys(std::uint32_t layer) const;
    [[nodiscard]] std::span<const float> values(std::uint32_t layer) const;
private:
    std::shared_ptr<detail::MappedFile> file_;
};
} // namespace outloud
