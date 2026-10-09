#include "outloud/generation.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace outloud {
namespace {
void require(bool condition, const char* message) {
    if (!condition) { throw std::invalid_argument(message); }
}
void valid_noise(std::span<const float> row) {
    require(row.size() == 32 && std::all_of(row.begin(), row.end(), [](float x) { return std::isfinite(x); }),
            "Noise row dimensions/nonfinite value");
}
double ms(std::chrono::steady_clock::time_point begin) {
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - begin).count();
}
} // namespace
FixtureNoiseSource::FixtureNoiseSource(std::span<const float> rows) : rows_(rows) {
    require(!rows.empty() && rows.size() % 32 == 0, "Fixture noise dimensions");
    for (std::size_t i = 0; i < rows.size(); i += 32) { valid_noise(rows.subspan(i, 32)); }
}
std::span<const float> FixtureNoiseSource::next() {
    require(consumed_ < rows_.size() / 32, "Fixture noise exhausted / overconsumption");
    const auto row = rows_.subspan(consumed_ * 32, 32);
    valid_noise(row); ++consumed_; return row;
}
void FixtureNoiseSource::finish_segment() {
    require(consumed_ == rows_.size() / 32, "Fixture noise has unconsumed extra draws");
}
GenerationStopPolicy::GenerationStopPolicy(const GenerationOptions& options)
    : threshold_(options.eos_threshold), tail_(options.frames_after_eos) {
    require(std::isfinite(threshold_), "Invalid EOS threshold");
}
bool GenerationStopPolicy::emit(std::uint32_t index, float eos_logit) {
    require(std::isfinite(eos_logit), "Nonfinite EOS logit");
    if (eos_logit > threshold_ && !first_eos_ && index >= 6) { first_eos_ = index; }
    return !first_eos_ || std::uint64_t(index) < std::uint64_t(*first_eos_) + tail_;
}
std::uint32_t FlowLMGenerator::estimate_max_gen_len(std::size_t token_count) {
    require(token_count > 0 && token_count <= 4096, "Invalid segment token count");
    return static_cast<std::uint32_t>(std::ceil((double(token_count) / 3.0 + 2.0) * 12.5));
}
void FlowLMGenerator::reset() {
    require(!running_, "Generator is already running");
    if (state_) { state_->reset(); } failed_ = false;
}
const FlowLMPrefillState& FlowLMGenerator::state() const {
    require(state_.has_value(), "Generator has no segment state");
    (void)state_->offset(); // incomplete state cannot be exposed as valid
    return *state_;
}
GenerationResult FlowLMGenerator::generate_segment(std::span<const std::uint32_t> tokens, NoiseSource& noise,
        const GenerationOptions& options, const GenerationObserver& observer) {
    require(!running_, "Generator is already running");
    require(!failed_, "Generation failed; reset required");
    const auto estimate = estimate_max_gen_len(tokens.size());
    const auto maximum = options.max_gen_len == 0 ? estimate : options.max_gen_len;
    require(maximum > 0 && maximum <= 4096 && tokens.size() <= 4096 - maximum, "Generation suffix capacity exceeded");
    require(std::all_of(tokens.begin(), tokens.end(), [](auto t) { return t < 4001; }), "Invalid generation token ID");
    GenerationStopPolicy stop(options);
    const auto capacity = static_cast<std::uint32_t>(tokens.size()) + maximum;
    if (!state_ || state_->capacity() != capacity) { state_ = backbone_.make_state(capacity); }
    else { state_->reset(); }
    GenerationResult result;
    result.emitted.reserve(maximum);
    candidate_.backbone.normalized.reserve(1024);
    running_ = true;
    try {
        const auto prefill_begin = std::chrono::steady_clock::now();
        {
            const auto before = state_->offset();
            backbone_.prefill_into(tokens,*state_,candidate_.backbone);
            const auto& prefill=candidate_.backbone; // consumed before the next candidate overwrites this result
            const auto row = noise.next(); valid_noise(row); ++result.noise_draw_count;
            const auto sample = sampler_.sample(prefill.last_hidden(), row); // real call, discarded
            result.prefill_offset = state_->offset();
            if (observer) { observer({GenerationInput::text, 0, before, state_->offset(), {}, prefill.last_hidden(), row,
                                      sample, prefill.eos_logit, prefill.eos_logit > options.eos_threshold, false, {}}); }
        }
        result.timing.prefill_ms = ms(prefill_begin);
        std::array<float, 32> previous{};
        result.timing.min_candidate_ms = std::numeric_limits<double>::infinity();
        const auto generation_begin = std::chrono::steady_clock::now();
        for (std::uint32_t index = 0; index < maximum; ++index) {
            const auto begin = std::chrono::steady_clock::now();
            const auto before = state_->offset();
            const auto row = noise.next(); valid_noise(row); ++result.noise_draw_count;
            if (index == 0) { sampler_.bos_candidate_into(backbone_, *state_, row, candidate_); }
            else { sampler_.frame_candidate_into(backbone_, *state_, previous, row, candidate_); }
            ++result.candidate_count;
            const bool emit = stop.emit(index, candidate_.backbone.eos_logit);
            result.first_eos = stop.first_eos();
            if (observer) { observer({index == 0 ? GenerationInput::bos : GenerationInput::previous_normalized_latent,
                index, before, state_->offset(), index == 0 ? std::span<const float>{} : std::span<const float>(previous),
                candidate_.backbone.last_hidden(), row, candidate_.sample, candidate_.backbone.eos_logit,
                candidate_.backbone.eos_logit > options.eos_threshold, emit, result.first_eos}); }
            if (emit) { result.emitted.push_back(candidate_.sample.latent); previous = candidate_.sample.latent; }
            const auto elapsed = ms(begin);
            result.timing.min_candidate_ms = std::min(result.timing.min_candidate_ms, elapsed);
            result.timing.max_candidate_ms = std::max(result.timing.max_candidate_ms, elapsed);
            if (!emit) { result.termination = GenerationTermination::eos_tail; break; }
        }
        result.timing.candidates_ms = ms(generation_begin);
        result.timing.total_ms = result.timing.prefill_ms + result.timing.candidates_ms;
        noise.finish_segment(); // deterministic source enforces exact draw count, including discarded calls
        result.final_offset = state_->offset();
        running_ = false;
        return result;
    } catch (...) {
        state_->invalidate(); failed_ = true; running_ = false; throw;
    }
}
} // namespace outloud
