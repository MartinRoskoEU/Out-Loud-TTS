#include "asset_io.hpp"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <bcrypt.h>

#include <algorithm>
#include <stdexcept>
#include <vector>

namespace outloud::detail {

MappedFile::MappedFile(const std::filesystem::path& path, std::uint64_t maximum_size) {
    const auto file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                                  FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
    if (file == INVALID_HANDLE_VALUE) { throw std::runtime_error("Cannot open local asset read-only"); }
    LARGE_INTEGER length{};
    if (!GetFileSizeEx(file, &length) || length.QuadPart <= 0 || static_cast<std::uint64_t>(length.QuadPart) > maximum_size) {
        CloseHandle(file);
        throw std::runtime_error("Invalid or oversized asset file");
    }
    const auto mapping = CreateFileMappingW(file, nullptr, PAGE_READONLY, 0, 0, nullptr);
    if (!mapping) { CloseHandle(file); throw std::runtime_error("Cannot map local asset"); }
    const auto data = static_cast<const unsigned char*>(MapViewOfFile(mapping, FILE_MAP_READ, 0, 0, 0));
    if (!data) { CloseHandle(mapping); CloseHandle(file); throw std::runtime_error("Cannot view local asset"); }
    file_ = file; mapping_ = mapping; data_ = data; size_ = static_cast<std::size_t>(length.QuadPart);
}
MappedFile::~MappedFile() {
    if (data_) { UnmapViewOfFile(data_); }
    if (mapping_) { CloseHandle(mapping_); }
    if (file_) { CloseHandle(file_); }
}

struct Sha256::Impl {
    std::vector<unsigned char> object;
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    bool finished = false;
    ~Impl() {
        if (hash) { BCryptDestroyHash(hash); }
        if (algorithm) { BCryptCloseAlgorithmProvider(algorithm, 0); }
    }
};
namespace {
void check(NTSTATUS status) {
    if (status < 0) { throw std::runtime_error("Windows SHA256 operation failed"); }
}
}
Sha256::Sha256() : impl_(std::make_unique<Impl>()) {
    check(BCryptOpenAlgorithmProvider(&impl_->algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0));
    DWORD object_length = 0, received = 0;
    check(BCryptGetProperty(impl_->algorithm, BCRYPT_OBJECT_LENGTH,
        reinterpret_cast<PUCHAR>(&object_length), sizeof(object_length), &received, 0));
    impl_->object.resize(object_length);
    check(BCryptCreateHash(impl_->algorithm, &impl_->hash, impl_->object.data(), object_length, nullptr, 0, 0));
}
Sha256::~Sha256() = default;
void Sha256::update(Bytes bytes) {
    if (impl_->finished) { throw std::logic_error("SHA256 already finished"); }
    while (!bytes.empty()) {
        const auto chunk = std::min<std::size_t>(bytes.size(), 16 * 1024 * 1024);
        check(BCryptHashData(impl_->hash, const_cast<PUCHAR>(bytes.data()), static_cast<ULONG>(chunk), 0));
        bytes = bytes.subspan(chunk);
    }
}
std::array<unsigned char, 32> Sha256::finish() {
    if (impl_->finished) { throw std::logic_error("SHA256 already finished"); }
    std::array<unsigned char, 32> result{};
    check(BCryptFinishHash(impl_->hash, result.data(), static_cast<ULONG>(result.size()), 0));
    impl_->finished = true;
    return result;
}
std::array<unsigned char, 32> sha256_parts(std::initializer_list<Bytes> parts) {
    Sha256 hash;
    for (auto part : parts) { hash.update(part); }
    return hash.finish();
}
std::array<unsigned char, 32> sha256(Bytes bytes) { return sha256_parts({bytes}); }
std::array<unsigned char, 32> asset_integrity(Bytes bytes) {
    if (bytes.size() < 256) { throw std::runtime_error("Truncated asset header"); }
    const std::array<unsigned char, 32> zero{};
    return sha256_parts({bytes.first(96), zero, bytes.subspan(128)});
}
std::string hex(Bytes bytes) {
    constexpr char alphabet[] = "0123456789abcdef";
    std::string result;
    result.reserve(bytes.size() * 2);
    for (auto b : bytes) { result += alphabet[b >> 4]; result += alphabet[b & 15]; }
    return result;
}
std::uint32_t u32(Bytes bytes, std::size_t offset) {
    if (offset > bytes.size() || bytes.size() - offset < 4) { throw std::runtime_error("Truncated asset field"); }
    std::uint32_t result = 0;
    for (std::size_t i = 0; i < 4; ++i) { result |= std::uint32_t(bytes[offset + i]) << (8 * i); }
    return result;
}
std::uint64_t u64(Bytes bytes, std::size_t offset) {
    if (offset > bytes.size() || bytes.size() - offset < 8) { throw std::runtime_error("Truncated asset field"); }
    std::uint64_t result = 0;
    for (std::size_t i = 0; i < 8; ++i) { result |= std::uint64_t(bytes[offset + i]) << (8 * i); }
    return result;
}
} // namespace outloud::detail
