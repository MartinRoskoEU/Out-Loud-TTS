#pragma once
#include "outloud/generation.hpp"

namespace outloud {
// Pinned PyTorch 2.14.1 CPU AVX2 normal_ semantics for contiguous FP32 [1,32],
// implemented with scalar integer/FP32 operations on every native kernel path.
// Single owner; next() returns already scaled noise valid until next/reseed.
class ProductionNoiseSource final : public NoiseSource {
public:
    explicit ProductionNoiseSource(std::uint64_t seed) noexcept { reseed(seed); }
    void reseed(std::uint64_t seed) noexcept;
    [[nodiscard]] std::span<const float> next() override;
    [[nodiscard]] std::uint64_t consumed() const noexcept { return draws_; }
    struct EngineView {
        std::uint64_t seed;
        std::uint32_t left, position;
        std::span<const std::uint32_t> words;
    };
    // Read only while idle; useful for pinned-state verification.
    [[nodiscard]] EngineView engine_state() const noexcept { return {seed_, left_, position_, state_}; }
private:
    [[nodiscard]] std::uint32_t random() noexcept;
    std::array<std::uint32_t, 624> state_{};
    std::uint64_t seed_ = 0, draws_ = 0;
    std::uint32_t left_ = 1, position_ = 0;
    std::array<float, 32> row_{};
};
} // namespace outloud
