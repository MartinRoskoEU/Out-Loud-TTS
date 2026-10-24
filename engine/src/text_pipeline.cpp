#include "outloud/text_pipeline.hpp"
#include "utf8.hpp"

#include <algorithm>
#include <array>
#include <stdexcept>

namespace outloud {
namespace {

std::string strip(std::string_view text, const Tokenizer& tokenizer) {
    const auto chars = detail::characters(text);
    std::size_t first = 0, last = chars.size();
    while (first < last && tokenizer.is_space(chars[first].cp)) { ++first; }
    while (last > first && tokenizer.is_space(chars[last - 1].cp)) { --last; }
    if (first == last) { return {}; }
    const auto end = chars[last - 1].offset + chars[last - 1].size;
    return std::string(text.substr(chars[first].offset, end - chars[first].offset));
}

bool closer(std::uint32_t cp) {
    constexpr std::array<std::uint32_t, 8> values = {'"', '\'', 0x201d, 0x2019, ')', ']', 0xbb, ' '};
    return std::find(values.begin(), values.end(), cp) != values.end();
}
bool terminal(std::uint32_t cp) { return cp == '.' || cp == '!' || cp == '?' || cp == 0x2026; }
bool weak(std::uint32_t cp) {
    return cp == ',' || cp == ';' || cp == ':' || cp == '-' || cp == 0x2013 || cp == 0x2014;
}

std::string terminal_punctuation(std::string text, const Tokenizer& tokenizer) {
    auto chars = detail::characters(text);
    std::size_t core_end = chars.size();
    while (core_end > 0 && closer(chars[core_end - 1].cp)) { --core_end; }
    if (core_end == 0 || terminal(chars[core_end - 1].cp)) { return text; }
    if (!weak(chars[core_end - 1].cp)) { return text + '.'; }
    const auto suffix_start = core_end == chars.size() ? text.size() : chars[core_end].offset;
    const auto suffix = strip(std::string_view(text).substr(suffix_start), tokenizer);
    while (core_end > 0 && (weak(chars[core_end - 1].cp) || chars[core_end - 1].cp == ' ')) { --core_end; }
    const auto end = core_end == 0 ? 0 : chars[core_end - 1].offset + chars[core_end - 1].size;
    return text.substr(0, end) + '.' + suffix;
}

bool decimal_boundary(std::span<const std::uint32_t> ids, std::size_t start, const Tokenizer& tokenizer) {
    const auto prefix = detail::characters(tokenizer.decode(ids.first(start)));
    const auto suffix = detail::characters(tokenizer.decode(ids.subspan(start)));
    return prefix.size() >= 2 && prefix.back().cp == '.' && tokenizer.is_digit(prefix[prefix.size() - 2].cp) &&
           !suffix.empty() && tokenizer.is_digit(suffix.front().cp);
}

std::vector<std::size_t> boundaries(std::span<const std::uint32_t> ids,
    std::span<const std::uint32_t> punctuation, const Tokenizer& tokenizer, bool skip_decimals) {
    std::vector<std::size_t> result{0};
    bool previous = false;
    for (std::size_t i = 0; i < ids.size(); ++i) {
        if (std::find(punctuation.begin(), punctuation.end(), ids[i]) != punctuation.end()) { previous = true; }
        else {
            if (previous && !(skip_decimals && decimal_boundary(ids, i, tokenizer))) { result.push_back(i); }
            previous = false;
        }
    }
    result.push_back(ids.size());
    return result;
}

struct Piece { std::size_t count; std::string text; };
std::vector<Piece> split(std::span<const std::uint32_t> ids, std::span<const std::uint32_t> marks,
                         const Tokenizer& tokenizer, bool skip_decimals) {
    const auto indices = boundaries(ids, marks, tokenizer, skip_decimals);
    std::vector<Piece> result;
    for (std::size_t i = 1; i < indices.size(); ++i) {
        result.push_back({indices[i] - indices[i - 1],
                          tokenizer.decode(ids.subspan(indices[i - 1], indices[i] - indices[i - 1]))});
    }
    return result;
}

} // namespace

PreparedText prepare_text(std::string_view utf8, const Tokenizer& tokenizer) {
    auto text = strip(utf8, tokenizer);
    if (text.empty()) { throw std::invalid_argument("Text prompt cannot be empty"); }
    // english_2026-10 enables upstream replace_characters: translate curly
    // apostrophes, join Unicode whitespace, then remove a comma/semicolon/colon
    // immediately after sentence punctuation (including intervening spaces).
    std::string normalized;
    bool pending_space = false;
    for (const auto c : detail::characters(text)) {
        if (tokenizer.is_space(c.cp)) { pending_space = true; continue; }
        if (pending_space && !normalized.empty()) { normalized += ' '; }
        pending_space = false;
        if (c.cp == 0x2018 || c.cp == 0x2019) { normalized += '\''; }
        else { normalized.append(text, c.offset, c.size); }
    }
    const auto chars = detail::characters(normalized);
    text.clear();
    for (std::size_t i = 0; i < chars.size(); ++i) {
        const auto c = chars[i];
        text.append(normalized, c.offset, c.size);
        if (terminal(c.cp)) {
            auto next = i + 1;
            while (next < chars.size() && tokenizer.is_space(chars[next].cp)) { ++next; }
            if (next < chars.size() && (chars[next].cp == ',' || chars[next].cp == ';' || chars[next].cp == ':')) { i = next; }
        }
    }
    std::size_t words = 0;
    bool in_word = false;
    for (const auto c : detail::characters(text)) {
        if (tokenizer.is_space(c.cp)) { in_word = false; }
        else if (!in_word) { ++words; in_word = true; }
    }
    std::size_t offset = 0;
    const auto first = detail::next_character(text, offset);
    if (!tokenizer.is_upper(first.cp)) { text = tokenizer.uppercase(first.cp) + text.substr(offset); }
    return {terminal_punctuation(std::move(text), tokenizer), words <= 4 ? 3u : 1u};
}

TextPipelineResult process_text(std::string_view utf8, const Tokenizer& tokenizer, std::uint32_t max_tokens) {
    if (max_tokens == 0) { throw std::invalid_argument("max_tokens must be positive"); }
    TextPipelineResult result;
    result.prepared_text = prepare_text(utf8, tokenizer).text;
    result.full_token_ids = tokenizer.encode(result.prepared_text);
    const auto whole_ids = tokenizer.encode(strip(result.prepared_text, tokenizer));
    // Exact pinned punctuation IDs, verified offline. Limit is soft for unsplittable long sentences.
    constexpr std::array<std::uint32_t, 4> sentence_marks = {263, 682, 799, 292};
    constexpr std::array<std::uint32_t, 3> fallback_marks = {262, 1230, 3244};
    const auto sentences = split(whole_ids, sentence_marks, tokenizer, true);
    std::vector<Piece> refined;
    for (const auto& sentence : sentences) {
        if (sentence.count > max_tokens) {
            const auto sub_ids = tokenizer.encode(strip(sentence.text, tokenizer));
            auto subs = split(sub_ids, fallback_marks, tokenizer, false);
            if (subs.size() > 1) {
                for (auto& sub : subs) { refined.push_back(std::move(sub)); }
                continue;
            }
        }
        refined.push_back(sentence);
    }
    std::vector<std::string> chunks;
    std::string current;
    std::size_t count = 0;
    for (const auto& piece : refined) {
        if (current.empty()) { current = piece.text; count = piece.count; }
        else if (count + piece.count > max_tokens) {
            chunks.push_back(strip(current, tokenizer));
            current = piece.text; count = piece.count;
        } else { current += ' '; current += piece.text; count += piece.count; }
    }
    if (!current.empty()) { chunks.push_back(strip(current, tokenizer)); }
    for (const auto& chunk : chunks) {
        auto prepared = prepare_text(chunk, tokenizer);
        auto ids = tokenizer.encode(prepared.text);
        result.segments.push_back({std::move(prepared.text), std::move(ids), prepared.frames_after_eos_guess + 2});
    }
    return result;
}

} // namespace outloud
