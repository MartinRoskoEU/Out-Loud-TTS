#pragma once

#include "outloud/text_pipeline.hpp"

#include <algorithm>
#include <iostream>

namespace outloud::test {
struct Case {
    std::string name;
    std::string text;
    std::string prepared_text;
    std::vector<std::uint32_t> raw_token_ids;
    std::string decoded_raw_text;
    std::vector<std::uint32_t> full_token_ids;
    std::vector<TextSegment> segments;
};
}

#include "native_text_fixtures.hpp"

namespace outloud::test {

inline bool check(const Case& expected, const Tokenizer& tokenizer, bool verbose = false) {
    const auto result = process_text(expected.text, tokenizer);
    const auto raw = tokenizer.encode(expected.text);
    bool good = result.prepared_text == expected.prepared_text && raw == expected.raw_token_ids &&
                tokenizer.decode(raw) == expected.decoded_raw_text &&
                result.full_token_ids == expected.full_token_ids && result.segments.size() == expected.segments.size();
    if (verbose) { std::cout << "Segments: " << result.segments.size() << '\n'; }
    for (std::size_t i = 0; i < std::min(result.segments.size(), expected.segments.size()); ++i) {
        const auto& actual = result.segments[i];
        const auto& wanted = expected.segments[i];
        const bool match = actual.text == wanted.text && actual.token_ids == wanted.token_ids &&
                           actual.frames_after_eos == wanted.frames_after_eos;
        good = good && match;
        if (verbose) { std::cout << '[' << i + 1 << "] tokens=" << actual.token_ids.size() << (match ? " PASS\n" : " FAIL\n"); }
    }
    if (verbose) {
        std::cout << "Full token IDs: " << (result.full_token_ids == expected.full_token_ids ? "PASS\n" : "FAIL\n");
        bool text_match = result.segments.size() == expected.segments.size();
        for (std::size_t i = 0; i < std::min(result.segments.size(), expected.segments.size()); ++i) {
            text_match = text_match && result.segments[i].text == expected.segments[i].text;
        }
        std::cout << "Segment text: " << (text_match ? "PASS\n" : "FAIL\n");
    }
    if (!good) {
        std::cerr << "Fixture mismatch: " << expected.name << '\n';
        std::cerr << "Prepared: " << result.prepared_text << '\n';
        std::cerr << "Raw IDs: "; for (auto id : raw) { std::cerr << id << ' '; }
        std::cerr << "\nFull IDs: "; for (auto id : result.full_token_ids) { std::cerr << id << ' '; }
        std::cerr << '\n';
        for (const auto& segment : result.segments) {
            std::cerr << "Segment: " << segment.text << " (tail " << segment.frames_after_eos << ")\n";
            for (auto id : segment.token_ids) { std::cerr << id << ' '; }
            std::cerr << '\n';
        }
    }
    return good;
}

} // namespace outloud::test
