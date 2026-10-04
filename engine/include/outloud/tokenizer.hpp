#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace outloud {

// Fixed pinned Pocket English tokenizer. No Python, JSON, downloads or neural model.
class Tokenizer {
public:
    static constexpr std::string_view source_sha256 =
        "f498428e1eafee50492f7be13dc9bfafcfc12e508cd0eb1b01c92ecd5d8c6687";

    explicit Tokenizer(const std::filesystem::path& asset);
    [[nodiscard]] std::vector<std::uint32_t> encode(std::string_view utf8) const;
    [[nodiscard]] std::string decode(std::span<const std::uint32_t> ids,
                                     bool skip_special_tokens = true) const;

    // Python 3.12 / Unicode 15 preparation properties, exported offline with the vocabulary.
    [[nodiscard]] bool is_space(std::uint32_t cp) const;
    [[nodiscard]] bool is_upper(std::uint32_t cp) const;
    [[nodiscard]] bool is_digit(std::uint32_t cp) const;
    [[nodiscard]] std::string uppercase(std::uint32_t cp) const;

private:
    struct Piece { std::string text; double score; };
    struct Node {
        std::vector<std::pair<unsigned char, std::uint32_t>> edges;
        std::uint32_t id = UINT32_MAX;
    };
    using Ranges = std::vector<std::pair<std::uint32_t, std::uint32_t>>;
    std::vector<Piece> pieces_;
    std::vector<Node> trie_;
    std::array<std::uint32_t, 256> byte_ids_{};
    Ranges spaces_, uppercase_chars_, digits_;
    std::vector<std::pair<std::uint32_t, std::string>> uppercase_map_;
    double unknown_score_ = 0;

    void encode_ordinary(std::string_view text, std::vector<std::uint32_t>& ids) const;
    void encode_piece(std::string_view metaspace_piece, std::vector<std::uint32_t>& ids) const;
};

} // namespace outloud
