#pragma once

#include "outloud/assets.hpp"
#include "asset_io.hpp"

#include <algorithm>
#include <stdexcept>

#include "asset_test_fixtures.hpp"

namespace outloud::test {
inline void asset_require(bool condition, const char* message) {
    if (!condition) { throw std::runtime_error(message); }
}
template<class T> detail::Bytes bytes_of(std::span<const T> values) {
    return {reinterpret_cast<const unsigned char*>(values.data()), values.size_bytes()};
}
inline void check_bf16(const std::filesystem::path& fixture) {
    const detail::MappedFile expected(fixture, 65536 * 4);
    const auto bytes = expected.bytes();
    asset_require(bytes.size() == 65536 * 4 && detail::hex(detail::sha256(bytes)) == bf16_fixture_sha,
                  "Wrong PyTorch BF16 fixture identity");
    for (std::uint32_t i = 0; i < 65536; ++i) {
        const auto actual = bf16_to_float32(static_cast<std::uint16_t>(i));
        asset_require(std::bit_cast<std::uint32_t>(actual) == detail::u32(bytes, std::size_t(i) * 4),
                      "BF16 widening differs from PyTorch bits");
    }
}
inline void check_model(const PocketModelAsset& model) {
    static_assert(std::endian::native == std::endian::little);
    detail::Sha256 original, widened;
    std::uint64_t count = 0;
    for (const auto& spec : pocket_tensor_schema) {
        const auto view = model.tensor(spec.id);
        asset_require(view.dtype == AssetDtype::bf16 && view.name == spec.name &&
                      view.dimensions.size() == spec.rank && view.bf16_values.size() == spec.elements,
                      "Wrong native tensor view");
        asset_require(std::equal(view.dimensions.begin(), view.dimensions.end(), spec.dimensions.begin()), "Wrong tensor shape");
        asset_require(reinterpret_cast<std::uintptr_t>(view.bf16_values.data()) % 64 == 0, "Tensor is not 64-byte aligned");
        original.update(bytes_of(view.bf16_values));
        const auto f32 = view.to_float32();
        widened.update(bytes_of(std::span<const float>(f32)));
        count += view.bf16_values.size();
    }
    asset_require(count == model.element_count(), "Wrong total model elements");
    asset_require(detail::hex(original.finish()) == model_raw_sha, "Exported BF16 bytes differ from source");
    asset_require(detail::hex(widened.finish()) == model_f32_sha, "Full model widening differs from PyTorch");
    auto shape = [&](PocketTensorId id, std::initializer_list<std::uint32_t> wanted) {
        const auto dimensions = model.tensor(id).dimensions;
        asset_require(std::equal(dimensions.begin(), dimensions.end(), wanted.begin(), wanted.end()), "Known tensor dimension mismatch");
    };
    shape(PocketTensorId::flow_lm_conditioner_embed_weight, {4001, 1024});
    shape(PocketTensorId::flow_lm_input_linear_weight, {1024, 32});
    shape(PocketTensorId::mimi_quantizer_output_proj_weight, {512, 32, 1});
    shape(PocketTensorId::mimi_upsample_convtr_convtr_weight, {512, 1, 32});
    shape(PocketTensorId::flow_lm_flow_net_time_embed_0_freqs, {128});
}
inline void check_voice(const OutLoudVoice& voice, const PocketModelAsset& model, const VoiceExpected& expected) {
    voice.validate_against(model);
    asset_require(voice.prefix_length() == expected.prefix && voice.source_sha256() == expected.source_sha,
                  "Voice identity/prefix mismatch");
    detail::Sha256 original;
    for (std::uint32_t layer = 0; layer < 6; ++layer) {
        const auto cache = voice.cache(layer);
        asset_require(cache.size() == std::size_t(2) * expected.prefix * 16 * 64, "Wrong K/V cache view");
        asset_require(voice.keys(layer).size() == cache.size() / 2 && voice.values(layer).data() == cache.data() + cache.size() / 2,
                      "Wrong key/value layout");
        asset_require(reinterpret_cast<std::uintptr_t>(cache.data()) % 64 == 0, "Voice cache alignment");
        original.update(bytes_of(cache));
    }
    asset_require(detail::hex(original.finish()) == expected.payload_sha, "K/V bytes differ from original safetensors");
}
} // namespace outloud::test
