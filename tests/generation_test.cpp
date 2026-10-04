#include "generation_fixture_checks.hpp"

#include <cstdlib>
#include <iostream>
#include <limits>
#include <new>

// Test-only allocation counter. Observe C++ allocation requests during the unobserved AR loop.
namespace { bool audit_allocations = false; std::size_t allocation_count = 0; }
void* operator new(std::size_t size) {
    if (void* value = std::malloc(size == 0 ? 1 : size)) {
        if (audit_allocations) { ++allocation_count; } return value;
    }
    throw std::bad_alloc();
}
void* operator new[](std::size_t size) { return ::operator new(size); }
void operator delete(void* value) noexcept { std::free(value); }
void operator delete[](void* value) noexcept { std::free(value); }
void operator delete(void* value, std::size_t) noexcept { std::free(value); }
void operator delete[](void* value, std::size_t) noexcept { std::free(value); }

namespace {
using outloud::test::neural_require;
template<class Function> void rejected(Function function, std::string_view reason) {
    try { function(); }
    catch (const std::exception& error) {
        neural_require(std::string_view(error.what()).find(reason) != std::string_view::npos, "Generation rejected for unexpected reason");
        return;
    }
    throw std::runtime_error("Invalid generation/noise/state accepted");
}
void stop_policy() {
    outloud::GenerationStopPolicy stop({});
    for (std::uint32_t i = 0; i < 6; ++i) {
        neural_require(stop.emit(i, 100) && !stop.first_eos(), "EOS latched before step 6");
    }
    neural_require(stop.emit(6, -4) && !stop.first_eos(), "EOS threshold must be strict > -4");
    neural_require(stop.emit(7, -3.99f) && stop.first_eos() == 7u, "First accepted EOS did not latch");
    neural_require(stop.emit(8, 10) && stop.first_eos() == 7u, "Later EOS overwrote first latch");
    neural_require(stop.emit(9, -100) && !stop.emit(10, -100) && stop.first_eos() == 7u, "EOS tail/stop-before-emission differs");
    outloud::GenerationOptions zero_tail; zero_tail.frames_after_eos = 0;
    outloud::GenerationStopPolicy immediate(zero_tail);
    neural_require(!immediate.emit(6, -3) && immediate.first_eos() == 6u, "Zero tail must discard first accepted EOS candidate");
    zero_tail.frames_after_eos = UINT32_MAX;
    outloud::GenerationStopPolicy huge_tail(zero_tail);
    neural_require(huge_tail.emit(6, 0) && huge_tail.emit(UINT32_MAX, 0), "EOS tail arithmetic overflow");
    zero_tail.eos_threshold = std::numeric_limits<float>::infinity();
    rejected([&] { outloud::GenerationStopPolicy invalid(zero_tail); }, "threshold");
    rejected([&] { (void)stop.emit(11, std::numeric_limits<float>::quiet_NaN()); }, "Nonfinite EOS");
    neural_require(outloud::FlowLMGenerator::estimate_max_gen_len(20) == 109 &&
                   outloud::FlowLMGenerator::estimate_max_gen_len(39) == 188, "Upstream maximum estimate changed");
}
void noise_edges(const outloud::test::GenerationFixture& fixture) {
    const auto rows = fixture.values("noise.stream");
    outloud::FixtureNoiseSource source(rows);
    for (std::size_t i = 0; i < 48; ++i) {
        neural_require(outloud::test::same_float_bits(source.next(), rows.subspan(i * 32, 32)), "Noise ordering/scaling changed");
    }
    source.finish_segment();
    rejected([&] { (void)source.next(); }, "exhausted");
    neural_require(source.consumed() == 48, "Rejected overconsumption advanced source");
    outloud::FixtureNoiseSource extra(rows);
    (void)extra.next(); rejected([&] { extra.finish_segment(); }, "unconsumed");
    rejected([&] { outloud::FixtureNoiseSource malformed(rows.first(31)); }, "dimensions");
    rejected([] { outloud::FixtureNoiseSource empty({}); }, "dimensions");
    std::array<float, 32> nonfinite{}; nonfinite[5] = std::numeric_limits<float>::infinity();
    rejected([&] { outloud::FixtureNoiseSource invalid(nonfinite); }, "nonfinite");
}
class AllocationNoise final : public outloud::NoiseSource {
public:
    explicit AllocationNoise(std::span<const float> rows) : fixture_(rows) {}
    std::span<const float> next() override {
        const bool first_candidate = fixture_.consumed() == 1;
        const auto row = fixture_.next();
        if (first_candidate) { allocation_count = 0; audit_allocations = true; }
        return row;
    }
    void finish_segment() override {
        fixture_.finish_segment(); audit_allocations = false;
        neural_require(allocation_count == 0, "Unobserved candidates allocated C++ heap storage");
    }
private:
    outloud::FixtureNoiseSource fixture_;
};
class MalformedNoise final : public outloud::NoiseSource {
public:
    std::span<const float> next() override { return values; }
    std::array<float, 31> values{};
};
void generation_edges(const outloud::PocketModelAsset& model, const outloud::OutLoudVoice& voice,
                      const outloud::test::GenerationFixture& fixture, outloud::cpu::KernelPath path) {
    outloud::FlowLMBackbone backbone(model, voice, path);
    outloud::FlowSampler sampler(model, path);
    outloud::FlowLMGenerator generator(backbone, sampler);
    const auto rows = fixture.values("noise.stream");
    outloud::GenerationOptions maximum; maximum.max_gen_len = 8; maximum.eos_threshold = 1e6f;
    outloud::FixtureNoiseSource exhausted(rows.first(64));
    rejected([&] { (void)generator.generate_segment(outloud::test::neural_tokens, exhausted, maximum); }, "exhausted");
    neural_require(exhausted.consumed() == 2, "Failure draw count changed");
    rejected([&] { (void)generator.state(); }, "reset required");
    outloud::FixtureNoiseSource valid(rows.first(9 * 32));
    rejected([&] { (void)generator.generate_segment(outloud::test::neural_tokens, valid, maximum); }, "reset required");
    generator.reset();
    neural_require(generator.state().offset() == 112 && generator.state().layer(0).keys.empty(), "Reset did not restore voice prefix");
    std::uint32_t calls = 0;
    std::array<float, 32> last;
    const auto result = generator.generate_segment(outloud::test::neural_tokens, valid, maximum, [&](const outloud::GenerationCall& call) {
        ++calls;
        if (call.input_kind == outloud::GenerationInput::text) {
            neural_require(!call.emitted && !call.latched_eos && call.offset_after == 132, "Prefill sample was not discarded");
        } else if (call.input_kind == outloud::GenerationInput::bos) {
            neural_require(call.index == 0 && call.input_latent.empty(), "BOS candidate input differs");
        } else {
            neural_require(outloud::test::same_float_bits(call.input_latent, last), "Feedback denormalized or teacher-forced native latent");
        }
        last = call.sample.latent;
    });
    neural_require(result.termination == outloud::GenerationTermination::maximum_length && !result.first_eos &&
                   result.candidate_count == 8 && result.noise_draw_count == 9 && result.emitted.size() == 8 &&
                   result.final_offset == 140 && calls == 9, "Max-length safety scheduling differs");
    AllocationNoise no_allocations(rows.first(9 * 32));
    const auto repeat = generator.generate_segment(outloud::test::neural_tokens, no_allocations, maximum);
    neural_require(outloud::test::same_float_bits(outloud::test::emitted_values(result), outloud::test::emitted_values(repeat)),
                   "Reset segment changed deterministic output");
    AllocationNoise canonical_noise(rows);
    const auto canonical = generator.generate_segment(outloud::test::neural_tokens, canonical_noise);
    neural_require(canonical.first_eos == 43u && canonical.candidate_count == 47 && canonical.emitted.size() == 46 &&
                   canonical.noise_draw_count == 48 && canonical.final_offset == 179, "Canonical unobserved allocation-audit scheduling differs");
    outloud::GenerationOptions early; early.max_gen_len = 12; early.eos_threshold = -1e6f;
    outloud::FixtureNoiseSource early_noise(rows.first(11 * 32));
    const auto tail = generator.generate_segment(outloud::test::neural_tokens, early_noise, early);
    neural_require(tail.first_eos == 6u && tail.candidate_count == 10 && tail.noise_draw_count == 11 &&
                   tail.emitted.size() == 9 && tail.final_offset == 142 && tail.termination == outloud::GenerationTermination::eos_tail,
                   "Final discarded tail candidate did not consume sampler/noise/state");
    outloud::FixtureNoiseSource unused(rows.first(10 * 32));
    rejected([&] { (void)generator.generate_segment(outloud::test::neural_tokens, unused, maximum); }, "unconsumed");
    rejected([&] { (void)generator.state(); }, "reset required"); generator.reset();
    MalformedNoise malformed;
    rejected([&] { (void)generator.generate_segment(outloud::test::neural_tokens, malformed, maximum); }, "dimensions");
    rejected([&] { (void)generator.state(); }, "reset required"); generator.reset();
    outloud::FixtureNoiseSource observer_noise(rows.first(64));
    rejected([&] { (void)generator.generate_segment(outloud::test::neural_tokens, observer_noise, maximum,
        [](const outloud::GenerationCall& call) {
            if (call.input_kind == outloud::GenerationInput::bos) { throw std::runtime_error("Interrupted observer"); }
        }); }, "observer");
    rejected([&] { (void)generator.state(); }, "reset required"); generator.reset();
    outloud::GenerationOptions one; one.max_gen_len = 1;
    outloud::FixtureNoiseSource reentrant_noise(rows.first(64));
    const auto single = generator.generate_segment(outloud::test::neural_tokens, reentrant_noise, one,
        [&](const outloud::GenerationCall&) {
            rejected([&] { generator.reset(); }, "already running");
            rejected([&] { (void)generator.generate_segment(outloud::test::neural_tokens, reentrant_noise, one); }, "already running");
        });
    neural_require(single.candidate_count == 1 && single.final_offset == 133, "Reentrancy guard changed state");
    rejected([&] { (void)generator.generate_segment({}, malformed); }, "token count");
    const std::array<std::uint32_t, 1> bad_token{4001};
    rejected([&] { (void)generator.generate_segment(bad_token, malformed); }, "token ID");
    one.max_gen_len = UINT32_MAX;
    rejected([&] { (void)generator.generate_segment(outloud::test::neural_tokens, malformed, one); }, "capacity");
    neural_require(generator.state().offset() == 133, "Preflight rejection mutated completed state");
    auto state = backbone.make_state(21);
    std::array<float, 31> invalid_previous{};
    rejected([&] { (void)sampler.frame_candidate(backbone, state, invalid_previous, rows.first(32)); }, "dimensions");
    neural_require(state.offset() == 112, "Malformed previous latent mutated backbone state");
}
} // namespace
int wmain(int argc, wchar_t** argv) {
    try {
        if (argc != 3) { throw std::invalid_argument("Usage: outloud-generation-tests assets neural-fixtures"); }
        const std::filesystem::path assets(argv[1]);
        const outloud::PocketModelAsset model(assets / "pocket-english.olp");
        const outloud::OutLoudVoice voice(assets / "gnome_male.olv");
        const outloud::test::GenerationFixture fixture(argv[2]);
        stop_policy(); noise_edges(fixture);
        generation_edges(model, voice, fixture, outloud::cpu::KernelPath::portable);
        if (outloud::cpu::optimized_available()) { generation_edges(model, voice, fixture, outloud::cpu::KernelPath::avx2_fma); }
        std::cout << "Noise ordering / exhaustion / unused draws: PASS\nEOS threshold / minimum / first latch / tail / maximum: PASS\n"
                     "Discarded prefill and final candidate / normalized feedback: PASS\nFailure/reset/reentrancy: PASS\n"
                     "Unobserved candidate C++ allocations: 0\nRESULT: PASS\n";
        return 0;
    } catch (const std::exception& error) { audit_allocations = false; std::cerr << error.what() << '\n'; return 1; }
}
