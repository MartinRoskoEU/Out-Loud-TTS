#pragma once

#include "outloud/tokenizer.hpp"

namespace outloud {

struct PreparedText {
    std::string text;
    std::uint32_t frames_after_eos_guess;
};

struct TextSegment {
    std::string text;
    std::vector<std::uint32_t> token_ids;
    std::uint32_t frames_after_eos;
};

struct TextPipelineResult {
    std::string prepared_text;
    std::vector<std::uint32_t> full_token_ids;
    std::vector<TextSegment> segments;
};

[[nodiscard]] PreparedText prepare_text(std::string_view utf8, const Tokenizer& tokenizer);
[[nodiscard]] TextPipelineResult process_text(std::string_view utf8, const Tokenizer& tokenizer,
                                             std::uint32_t max_tokens = 50);

} // namespace outloud
