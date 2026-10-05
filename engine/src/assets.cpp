#include "outloud/perf_diagnostics.hpp"
#include "outloud/assets.hpp"
#include "asset_io.hpp"

#include <algorithm>
#include <limits>
#include <stdexcept>

namespace outloud {
namespace {
using detail::Bytes;
constexpr std::uint64_t model_payload_offset = 256 + 169 * 64;
constexpr std::uint64_t model_file_size = model_payload_offset + pocket_model_payload_bytes;

void require(bool condition, const char* message) {
    if (!condition) { throw std::runtime_error(message); }
}
bool zero(Bytes bytes) { return std::all_of(bytes.begin(), bytes.end(), [](auto b) { return b == 0; }); }
std::uint64_t align(std::uint64_t value) { return (value + 63) & ~std::uint64_t(63); }

void common_header(Bytes bytes, std::string_view magic, AssetDtype dtype) {
    require(bytes.size() >= 256, "Truncated Pocket asset header");
    require(std::string_view(reinterpret_cast<const char*>(bytes.data()), 8) == magic, "Wrong asset magic");
    require(detail::u32(bytes, 8) == 1 && detail::u32(bytes, 12) == 256, "Unsupported asset version/header");
    require(detail::u32(bytes, 16) == 0x01020304, "Wrong asset byte order");
    require(detail::u32(bytes, 20) == 1 && detail::u32(bytes, 24) == 1, "Unsupported Pocket family/architecture");
    require(detail::u32(bytes, 28) == static_cast<std::uint32_t>(dtype), "Wrong asset dtype");
    require(detail::u64(bytes, 160) == bytes.size(), "Asset truncation or trailing bytes");
    require(detail::u64(bytes, 168) == 256, "Wrong table/payload layout");
    require(detail::u32(bytes, 212) == 24000 && detail::u32(bytes, 216) == 1920, "Wrong audio architecture constants");
    require(zero(bytes.subspan(236, 20)), "Unsupported reserved asset header fields");
    require(!zero(bytes.subspan(32, 32)) && !zero(bytes.subspan(64, 32)), "Missing source/compatibility identity");
}

void integrity(Bytes bytes, std::uint64_t payload_offset, std::uint64_t payload_size) {
    require(payload_offset <= bytes.size() && payload_size == bytes.size() - payload_offset, "Invalid payload bounds");
    const auto whole = detail::asset_integrity(bytes);
    require(std::equal(whole.begin(), whole.end(), bytes.begin() + 96), "Asset container SHA256 mismatch");
    const auto payload = detail::sha256(bytes.subspan(static_cast<std::size_t>(payload_offset), static_cast<std::size_t>(payload_size)));
    require(std::equal(payload.begin(), payload.end(), bytes.begin() + 128), "Asset payload SHA256 mismatch");
}
} // namespace

void widen_bf16(std::span<const std::uint16_t> input, std::span<float> output) {
    OUTLOUD_PERF_SCOPE(::outloud::perf::Stage::widening);
    static_assert(sizeof(float) == 4 && std::numeric_limits<float>::is_iec559);
    if (input.size() != output.size()) { throw std::invalid_argument("BF16 widening size mismatch"); }
    for (std::size_t i = 0; i < input.size(); ++i) { output[i] = bf16_to_float32(input[i]); }
}
std::vector<float> ModelTensorView::to_float32() const {
    std::vector<float> result(bf16_values.size());
    widen_bf16(bf16_values, result);
    return result;
}

PocketModelAsset::PocketModelAsset(const std::filesystem::path& path)
    : file_(std::make_shared<detail::MappedFile>(path, model_file_size)) { validate(); }

void PocketModelAsset::validate() const {
    const auto bytes = file_->bytes();
    common_header(bytes, "OLPMOD01", AssetDtype::bf16);
    require(bytes.size() == model_file_size, "Wrong fixed model size");
    require(detail::hex(bytes.subspan(32, 32)) == compatibility_id(), "Unsupported model compatibility ID");
    require(detail::hex(bytes.subspan(64, 32)) == source_sha256(), "Wrong source model SHA256");
    require(detail::hex(bytes.subspan(128, 32)) == pocket_model_payload_sha256, "Wrong pinned model payload identity");
    require(detail::u64(bytes, 176) == model_payload_offset && detail::u64(bytes, 184) == payload_bytes(), "Wrong model payload bounds");
    require(detail::u32(bytes, 192) == tensor_count() && detail::u32(bytes, 196) == 64, "Wrong tensor/record count");
    require(detail::u32(bytes, 200) == 1024 && detail::u32(bytes, 204) == 6 && detail::u32(bytes, 208) == 32,
            "Wrong model dimensions");
    require(detail::u32(bytes, 220) == 0 && detail::u64(bytes, 224) == element_count() && detail::u32(bytes, 232) == 0,
            "Wrong model flags/elements/schema");
    std::uint64_t next = model_payload_offset;
    for (std::size_t i = 0; i < pocket_tensor_schema.size(); ++i) {
        const auto& spec = pocket_tensor_schema[i];
        const auto record = bytes.subspan(256 + i * 64, 64);
        require(detail::u32(record, 0) == i && detail::u32(record, 4) == 1 && detail::u32(record, 8) == spec.rank,
                "Wrong tensor ID/dtype/rank");
        require(detail::u32(record, 12) == 0 && zero(record.subspan(56, 8)), "Unsupported tensor flags");
        for (std::size_t d = 0; d < 4; ++d) { require(detail::u32(record, 16 + d * 4) == spec.dimensions[d], "Wrong tensor dimension"); }
        require(detail::u64(record, 32) == next && detail::u64(record, 40) == spec.elements * 2 &&
                detail::u64(record, 48) == spec.elements, "Wrong tensor offset/size/elements");
        const auto end = next + spec.elements * 2;
        require(end <= bytes.size() && align(end) <= bytes.size(), "Tensor outside asset bounds");
        require(zero(bytes.subspan(static_cast<std::size_t>(end), static_cast<std::size_t>(align(end) - end))),
                "Nonzero tensor alignment padding");
        next = align(end);
    }
    require(next == bytes.size(), "Trailing model payload data");
    integrity(bytes, model_payload_offset, payload_bytes());
}
ModelTensorView PocketModelAsset::tensor(PocketTensorId id) const {
    const auto index = static_cast<std::size_t>(id);
    if (index >= tensor_count()) { throw std::out_of_range("Invalid Pocket tensor ID"); }
    const auto& spec = pocket_tensor_schema[index];
    const auto offset = detail::u64(file_->bytes(), 256 + index * 64 + 32);
    const auto data = reinterpret_cast<const std::uint16_t*>(file_->bytes().data() + offset);
    return {id, spec.name, AssetDtype::bf16, std::span(spec.dimensions).first(spec.rank),
            {data, static_cast<std::size_t>(spec.elements)}};
}

OutLoudVoice::OutLoudVoice(const std::filesystem::path& path)
    : file_(std::make_shared<detail::MappedFile>(path, 1024ULL * 1024 * 1024 + 256)) { validate(); }
void OutLoudVoice::validate() const {
    const auto bytes = file_->bytes();
    common_header(bytes, "OLVPKT01", AssetDtype::float32);
    require(detail::u64(bytes, 176) == 256, "Wrong voice payload offset");
    require(detail::u32(bytes, 192) == 6 && detail::u32(bytes, 200) == 16 && detail::u32(bytes, 204) == 64 &&
            detail::u32(bytes, 208) == 1, "Wrong voice layer/head/batch dimensions");
    require(prefix_length() > 0 && detail::u32(bytes, 220) == 1 && detail::u32(bytes, 232) == 1,
            "Invalid voice prefix, post-RoPE flag or voice schema");
    const std::uint64_t elements = 6ULL * 2 * prefix_length() * 16 * 64;
    require(payload_bytes() == elements * 4 && detail::u64(bytes, 224) == elements, "Wrong voice size/layout");
    integrity(bytes, 256, payload_bytes());
    for (std::size_t offset = 256; offset < bytes.size(); offset += 4) {
        require((detail::u32(bytes, offset) & 0x7f800000) != 0x7f800000, "Non-finite voice cache position");
    }
}
void OutLoudVoice::validate_against(const PocketModelAsset& model) const {
    require(compatibility_id() == model.compatibility_id(), "Voice/model compatibility mismatch");
}
std::uint32_t OutLoudVoice::prefix_length() const { return detail::u32(file_->bytes(), 196); }
std::uint64_t OutLoudVoice::payload_bytes() const { return detail::u64(file_->bytes(), 184); }
std::string OutLoudVoice::source_sha256() const { return detail::hex(file_->bytes().subspan(64, 32)); }
std::string OutLoudVoice::compatibility_id() const { return detail::hex(file_->bytes().subspan(32, 32)); }
std::span<const float> OutLoudVoice::cache(std::uint32_t layer) const {
    if (layer >= layers()) { throw std::out_of_range("Invalid voice layer"); }
    const auto count = std::size_t(2) * prefix_length() * heads() * head_dimension();
    const auto data = reinterpret_cast<const float*>(file_->bytes().data() + 256 + std::size_t(layer) * count * 4);
    return {data, count};
}
std::span<const float> OutLoudVoice::keys(std::uint32_t layer) const {
    const auto kv = cache(layer); return kv.first(kv.size() / 2);
}
std::span<const float> OutLoudVoice::values(std::uint32_t layer) const {
    const auto kv = cache(layer); return kv.subspan(kv.size() / 2);
}
} // namespace outloud
