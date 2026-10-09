#include "outloud/tokenizer.hpp"
#include "utf8.hpp"
#include "asset_io.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <fstream>
#include <cerrno>
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <limits>
#include <stdexcept>

namespace outloud {
namespace {

constexpr std::string_view metaspace = "\xe2\x96\x81";
constexpr std::uint32_t missing = UINT32_MAX;

class Reader {
public:
    explicit Reader(std::span<const unsigned char> bytes) : bytes_(bytes) {}
    std::span<const unsigned char> take(std::size_t count) {
        if (count > bytes_.size() - cursor_) { throw AssetError(AssetErrorCode::invalid_data,"Truncated tokenizer asset"); }
        const auto result = bytes_.subspan(cursor_, count);
        cursor_ += count;
        return result;
    }
    std::uint32_t u32() {
        const auto b = take(4);
        return std::uint32_t(b[0]) | (std::uint32_t(b[1]) << 8) |
               (std::uint32_t(b[2]) << 16) | (std::uint32_t(b[3]) << 24);
    }
    double f64() {
        static_assert(sizeof(double) == 8 && std::numeric_limits<double>::is_iec559);
        const auto b = take(8);
        std::uint64_t bits = 0;
        for (std::size_t i = 0; i < 8; ++i) { bits |= std::uint64_t(b[i]) << (8 * i); }
        return std::bit_cast<double>(bits);
    }
    std::string string(std::uint32_t maximum) {
        const auto length = u32();
        if (length > maximum) { throw AssetError(AssetErrorCode::invalid_data,"Oversized tokenizer string"); }
        const auto b = take(length);
        std::string result(reinterpret_cast<const char*>(b.data()), b.size());
        if (!detail::valid_utf8(result)) { throw AssetError(AssetErrorCode::invalid_data,"Invalid asset UTF-8"); }
        return result;
    }
    bool finished() const { return cursor_ == bytes_.size(); }
private:
    std::span<const unsigned char> bytes_;
    std::size_t cursor_ = 0;
};

bool in_ranges(const std::vector<std::pair<std::uint32_t, std::uint32_t>>& ranges, std::uint32_t cp) {
    const auto it = std::upper_bound(ranges.begin(), ranges.end(), cp,
        [](std::uint32_t value, const auto& range) { return value < range.first; });
    return it != ranges.begin() && cp <= std::prev(it)->second;
}

} // namespace

Tokenizer::Tokenizer(const std::filesystem::path& asset) {
    std::ifstream file(asset, std::ios::binary | std::ios::ate);
    if (!file) { throw AssetError(AssetErrorCode::io,"Cannot open local tokenizer asset",
        errno==ENOENT ? ERROR_FILE_NOT_FOUND : errno==EACCES ? ERROR_ACCESS_DENIED : ERROR_READ_FAULT); }
    const auto length = file.tellg();
    if (length < 88 || length > 1024 * 1024) { throw AssetError(AssetErrorCode::invalid_data,"Invalid tokenizer asset size"); }
    std::vector<unsigned char> bytes(static_cast<std::size_t>(length));
    file.seekg(0);
    file.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    if (!file) { throw AssetError(AssetErrorCode::io,"Cannot read tokenizer asset",ERROR_READ_FAULT); }
    Reader header(bytes);
    const auto magic = header.take(8);
    if (std::string_view(reinterpret_cast<const char*>(magic.data()), magic.size()) != "OLPTOK01" ||
        header.u32() != 1 || header.u32() != 4000 || header.u32() != 0) {
        throw AssetError(AssetErrorCode::invalid_data,"Unsupported Pocket tokenizer header");
    }
    if (header.u32() != bytes.size() - 88 || detail::hex(header.take(32)) != source_sha256) {
        throw AssetError(AssetErrorCode::invalid_data,"Tokenizer asset identity mismatch");
    }
    const auto expected_digest = header.take(32);
    const auto payload = header.take(bytes.size() - 88);
    const auto digest = detail::sha256(payload);
    if (!std::equal(digest.begin(), digest.end(), expected_digest.begin())) {
        throw AssetError(AssetErrorCode::invalid_data,"Tokenizer asset payload SHA256 mismatch");
    }
    Reader reader(payload);
    pieces_.reserve(4000);
    double min_score = std::numeric_limits<double>::infinity();
    for (std::uint32_t i = 0; i < 4000; ++i) {
        auto text = reader.string(64);
        const auto score = reader.f64();
        if (text.empty() || !std::isfinite(score)) { throw AssetError(AssetErrorCode::invalid_data,"Invalid vocabulary entry"); }
        min_score = std::min(min_score, score);
        pieces_.push_back({std::move(text), score});
    }
    unknown_score_ = min_score - 10.0;
    const std::array<std::string_view, 4> specials = {"<unk>", "<s>", "</s>", "<pad>"};
    for (std::size_t i = 0; i < specials.size(); ++i) {
        if (pieces_[i].text != specials[i]) { throw AssetError(AssetErrorCode::invalid_data,"Invalid special token map"); }
    }
    constexpr char upper_hex[] = "0123456789ABCDEF";
    for (std::uint32_t b = 0; b < 256; ++b) {
        std::string name = "<0x";
        name += upper_hex[b >> 4]; name += upper_hex[b & 15]; name += '>';
        if (pieces_[b + 4].text != name) { throw AssetError(AssetErrorCode::invalid_data,"Missing byte fallback token"); }
        byte_ids_[b] = b + 4;
    }
    if (reader.string(16) != "15.0.0") { throw AssetError(AssetErrorCode::invalid_data,"Unsupported preparation Unicode version"); }
    auto read_ranges = [&reader](Ranges& ranges) {
        const auto count = reader.u32();
        if (count > 10000) { throw AssetError(AssetErrorCode::invalid_data,"Oversized Unicode property table"); }
        for (std::uint32_t i = 0; i < count; ++i) {
            const auto first = reader.u32(), last = reader.u32();
            if (first > last || last > 0x10ffff || (!ranges.empty() && first <= ranges.back().second)) {
                throw AssetError(AssetErrorCode::invalid_data,"Invalid Unicode property ranges");
            }
            ranges.emplace_back(first, last);
        }
    };
    read_ranges(spaces_); read_ranges(uppercase_chars_); read_ranges(digits_);
    const auto upper_count = reader.u32();
    if (upper_count > 5000) { throw AssetError(AssetErrorCode::invalid_data,"Oversized uppercase table"); }
    for (std::uint32_t i = 0; i < upper_count; ++i) {
        const auto cp = reader.u32();
        auto replacement = reader.string(12);
        if (cp > 0x10ffff || (cp >= 0xd800 && cp <= 0xdfff) || replacement.empty() ||
            (!uppercase_map_.empty() && cp <= uppercase_map_.back().first)) {
            throw AssetError(AssetErrorCode::invalid_data,"Invalid uppercase mapping");
        }
        uppercase_map_.emplace_back(cp, std::move(replacement));
    }
    if (!reader.finished()) { throw AssetError(AssetErrorCode::invalid_data,"Trailing tokenizer asset data"); }

    trie_.emplace_back();
    for (std::uint32_t id = 0; id < pieces_.size(); ++id) {
        std::uint32_t index = 0;
        for (const unsigned char byte : pieces_[id].text) {
            auto& edges = trie_[index].edges;
            const auto it = std::lower_bound(edges.begin(), edges.end(), byte,
                [](const auto& edge, unsigned char value) { return edge.first < value; });
            if (it != edges.end() && it->first == byte) { index = it->second; }
            else {
                const auto child = static_cast<std::uint32_t>(trie_.size());
                edges.insert(it, {byte, child});
                trie_.emplace_back();
                index = child;
            }
        }
        if (trie_[index].id != missing) { throw AssetError(AssetErrorCode::invalid_data,"Duplicate vocabulary piece"); }
        trie_[index].id = id;
    }
}

bool Tokenizer::is_space(std::uint32_t cp) const { return in_ranges(spaces_, cp); }
bool Tokenizer::is_upper(std::uint32_t cp) const { return in_ranges(uppercase_chars_, cp); }
bool Tokenizer::is_digit(std::uint32_t cp) const { return in_ranges(digits_, cp); }
std::string Tokenizer::uppercase(std::uint32_t cp) const {
    const auto it = std::lower_bound(uppercase_map_.begin(), uppercase_map_.end(), cp,
        [](const auto& entry, std::uint32_t value) { return entry.first < value; });
    return it != uppercase_map_.end() && it->first == cp ? it->second : detail::from_scalar(cp);
}

void Tokenizer::encode_piece(std::string_view text, std::vector<std::uint32_t>& ids) const {
    struct Best { double score = 0; std::size_t start = SIZE_MAX; std::uint32_t id = 0; };
    std::vector<Best> best(text.size() + 1);
    for (std::size_t start = 0; start < text.size();) {
        auto after_char = start;
        const auto character = detail::next_character(text, after_char);
        bool has_single = false;
        std::uint32_t node = 0;
        // Prefix traversal is shortest-first; strict > retains the earlier equal-scoring path.
        for (std::size_t end = start; end < text.size(); ++end) {
            const auto byte = static_cast<unsigned char>(text[end]);
            const auto& edges = trie_[node].edges;
            const auto edge = std::lower_bound(edges.begin(), edges.end(), byte,
                [](const auto& entry, unsigned char value) { return entry.first < value; });
            if (edge == edges.end() || edge->first != byte) { break; }
            node = edge->second;
            if (trie_[node].id == missing) { continue; }
            const auto id = trie_[node].id;
            const auto score = pieces_[id].score + best[start].score;
            auto& target = best[end + 1];
            if (target.start == SIZE_MAX || score > target.score) { target = {score, start, id}; }
            if (end + 1 - start == character.size) { has_single = true; }
        }
        if (!has_single) {
            const auto score = unknown_score_ + best[start].score;
            auto& target = best[after_char];
            if (target.start == SIZE_MAX || score > target.score) { target = {score, start, 0}; }
        }
        start = after_char;
    }
    struct Selected { std::size_t start, end; std::uint32_t id; };
    std::vector<Selected> selected;
    for (auto end = text.size(); end > 0;) {
        const auto& b = best[end];
        if (b.start == SIZE_MAX) { throw std::runtime_error("Unreachable Unigram path"); }
        selected.push_back({b.start, end, b.id});
        end = b.start;
    }
    std::reverse(selected.begin(), selected.end());
    for (std::size_t i = 0; i < selected.size();) {
        if (selected[i].id != 0) { ids.push_back(selected[i++].id); continue; }
        const auto start = selected[i].start;
        auto end = selected[i++].end;
        while (i < selected.size() && selected[i].id == 0) { end = selected[i++].end; }
        // Fuse consecutive unknown scalars first, then byte-fallback their original UTF-8 bytes.
        for (std::size_t b = start; b < end; ++b) { ids.push_back(byte_ids_[static_cast<unsigned char>(text[b])]); }
    }
}

void Tokenizer::encode_ordinary(std::string_view text, std::vector<std::uint32_t>& ids) const {
    if (text.empty()) { return; }
    // The Prepend normalizer always inserts a marker into each non-special span.
    std::string normalized(metaspace);
    for (std::size_t offset = 0; offset < text.size();) {
        const auto c = detail::next_character(text, offset);
        if (c.cp == ' ') { normalized += metaspace; }
        else { normalized.append(text.substr(c.offset, c.size)); }
    }
    // Metaspace splits at each marker, merging it with the next content.
    std::size_t start = 0;
    while (start < normalized.size()) {
        const auto next = normalized.find(metaspace, start + metaspace.size());
        const auto end = next == std::string::npos ? normalized.size() : next;
        encode_piece(std::string_view(normalized).substr(start, end - start), ids);
        start = end;
    }
}

std::vector<std::uint32_t> Tokenizer::encode(std::string_view text) const {
    if (!detail::valid_utf8(text)) { throw std::invalid_argument("Tokenizer requires valid UTF-8"); }
    std::vector<std::uint32_t> ids;
    std::size_t start = 0;
    while (start < text.size()) {
        std::size_t match = std::string_view::npos;
        std::uint32_t special = 0;
        for (std::uint32_t i = 0; i < 4; ++i) {
            const auto found = text.find(pieces_[i].text, start);
            if (found < match || (found == match && found != std::string_view::npos &&
                                 pieces_[i].text.size() > pieces_[special].text.size())) {
                match = found; special = i;
            }
        }
        if (match == std::string_view::npos) { encode_ordinary(text.substr(start), ids); break; }
        encode_ordinary(text.substr(start, match - start), ids);
        ids.push_back(special);
        start = match + pieces_[special].text.size();
    }
    return ids;
}

std::string Tokenizer::decode(std::span<const std::uint32_t> ids, bool skip_special_tokens) const {
    std::string output, pending_bytes;
    auto flush = [&]() {
        if (detail::valid_utf8(pending_bytes)) { output += pending_bytes; }
        else { for (std::size_t i = 0; i < pending_bytes.size(); ++i) { output += "\xef\xbf\xbd"; } }
        pending_bytes.clear();
    };
    std::size_t token_index = 0;
    for (const auto id : ids) {
        if (id >= pieces_.size() || (skip_special_tokens && id < 4)) { continue; }
        std::string transformed;
        for (std::size_t offset = 0; offset < pieces_[id].text.size();) {
            const auto c = detail::next_character(pieces_[id].text, offset);
            if (c.cp == 0x2581) {
                if (token_index != 0) { transformed += ' '; }
            } else { transformed.append(pieces_[id].text, c.offset, c.size); }
        }
        ++token_index;
        // Only the fixed 256 fallback entries have this spelling in the pinned vocabulary.
        if (id >= 4 && id < 260) { pending_bytes += static_cast<char>(id - 4); }
        else { flush(); output += transformed; }
    }
    flush();
    return output;
}

} // namespace outloud
