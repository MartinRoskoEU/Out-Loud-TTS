#pragma once

#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace outloud::detail {

struct Character { std::uint32_t cp; std::size_t offset; std::size_t size; };

inline Character next_character(std::string_view text, std::size_t& offset) {
    const auto begin = offset;
    const auto first = static_cast<unsigned char>(text[offset++]);
    std::uint32_t cp = first;
    std::size_t size = 1;
    if (first >= 0xc2 && first <= 0xdf) { size = 2; cp &= 0x1f; }
    else if (first >= 0xe0 && first <= 0xef) { size = 3; cp &= 0x0f; }
    else if (first >= 0xf0 && first <= 0xf4) { size = 4; cp &= 0x07; }
    else if (first >= 0x80) { throw std::invalid_argument("Invalid UTF-8 input"); }
    if (begin + size > text.size()) { throw std::invalid_argument("Truncated UTF-8 input"); }
    for (std::size_t i = 1; i < size; ++i) {
        const auto byte = static_cast<unsigned char>(text[offset++]);
        if ((byte & 0xc0) != 0x80) { throw std::invalid_argument("Invalid UTF-8 continuation"); }
        cp = (cp << 6) | (byte & 0x3f);
    }
    if ((size == 2 && cp < 0x80) || (size == 3 && cp < 0x800) ||
        (size == 4 && cp < 0x10000) || (cp >= 0xd800 && cp <= 0xdfff) || cp > 0x10ffff) {
        throw std::invalid_argument("Invalid UTF-8 scalar");
    }
    return {cp, begin, size};
}

inline std::vector<Character> characters(std::string_view text) {
    std::vector<Character> result;
    for (std::size_t i = 0; i < text.size();) { result.push_back(next_character(text, i)); }
    return result;
}

inline bool valid_utf8(std::string_view text) {
    try {
        for (std::size_t i = 0; i < text.size();) { (void)next_character(text, i); }
        return true;
    } catch (const std::invalid_argument&) { return false; }
}

inline std::string from_scalar(std::uint32_t cp) {
    std::string result;
    if (cp < 0x80) { result += static_cast<char>(cp); }
    else if (cp < 0x800) {
        result += static_cast<char>(0xc0 | (cp >> 6));
        result += static_cast<char>(0x80 | (cp & 0x3f));
    } else if (cp < 0x10000) {
        result += static_cast<char>(0xe0 | (cp >> 12));
        result += static_cast<char>(0x80 | ((cp >> 6) & 0x3f));
        result += static_cast<char>(0x80 | (cp & 0x3f));
    } else {
        result += static_cast<char>(0xf0 | (cp >> 18));
        result += static_cast<char>(0x80 | ((cp >> 12) & 0x3f));
        result += static_cast<char>(0x80 | ((cp >> 6) & 0x3f));
        result += static_cast<char>(0x80 | (cp & 0x3f));
    }
    return result;
}

} // namespace outloud::detail
