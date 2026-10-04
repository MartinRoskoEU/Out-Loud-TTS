#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <initializer_list>
#include <memory>
#include <span>
#include <string>

namespace outloud::detail {
using Bytes = std::span<const unsigned char>;

class Sha256 {
public:
    Sha256();
    ~Sha256();
    Sha256(const Sha256&) = delete;
    Sha256& operator=(const Sha256&) = delete;
    void update(Bytes bytes);
    [[nodiscard]] std::array<unsigned char, 32> finish();
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

class MappedFile {
public:
    explicit MappedFile(const std::filesystem::path& path, std::uint64_t maximum_size);
    ~MappedFile();
    MappedFile(const MappedFile&) = delete;
    MappedFile& operator=(const MappedFile&) = delete;
    [[nodiscard]] Bytes bytes() const { return {data_, size_}; }
private:
    void* file_ = nullptr;
    void* mapping_ = nullptr;
    const unsigned char* data_ = nullptr;
    std::size_t size_ = 0;
};

[[nodiscard]] std::array<unsigned char, 32> sha256_parts(std::initializer_list<Bytes> parts);
[[nodiscard]] std::array<unsigned char, 32> sha256(Bytes bytes);
[[nodiscard]] std::array<unsigned char, 32> asset_integrity(Bytes bytes);
[[nodiscard]] std::string hex(Bytes bytes);
[[nodiscard]] std::uint32_t u32(Bytes bytes, std::size_t offset);
[[nodiscard]] std::uint64_t u64(Bytes bytes, std::size_t offset);
} // namespace outloud::detail
