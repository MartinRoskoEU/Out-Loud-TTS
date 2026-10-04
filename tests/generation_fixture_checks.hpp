#pragma once
#include "outloud/generation.hpp"
#include "flow_sampler_fixture_checks.hpp"
#include "generation_segment_fixtures.hpp"
#include "generation_tolerances.hpp"

namespace outloud::test {
class GenerationFixture {
public:
    explicit GenerationFixture(const std::filesystem::path& directory)
        : file_(directory / "generation-segment1-f32.bin", generation_fixture_bytes) {
        const auto bytes = file_.bytes();
        neural_require(bytes.size() == generation_fixture_bytes && detail::hex(detail::sha256(bytes)) == generation_fixture_sha,
                       "Generation fixture length/hash mismatch");
        std::uint64_t next = 0;
        for (const auto& c : generation_checkpoints) {
            neural_require(c.offset == next && c.elements > 0 && c.offset <= bytes.size() &&
                           c.elements <= (bytes.size() - c.offset) / 4, "Generation fixture bounds mismatch");
            const auto raw = bytes.subspan(static_cast<std::size_t>(c.offset), static_cast<std::size_t>(c.elements * 4));
            neural_require(detail::hex(detail::sha256(raw)) == c.sha, "Generation checkpoint hash mismatch");
            next += raw.size();
        }
        neural_require(next == bytes.size() && generation_checkpoints.size() == 301, "Generation trailing/missing checkpoint data");
    }
    [[nodiscard]] std::span<const float> values(std::string_view name) const {
        const auto it = std::find_if(generation_checkpoints.begin(), generation_checkpoints.end(), [&](auto& c) { return c.name == name; });
        if (it == generation_checkpoints.end()) { throw std::runtime_error("Unknown generation checkpoint"); }
        return {reinterpret_cast<const float*>(file_.bytes().data() + it->offset), static_cast<std::size_t>(it->elements)};
    }
private:
    detail::MappedFile file_;
};
inline std::string candidate_name(std::uint32_t index) {
    return "candidate" + (index < 10 ? std::string("0") : std::string{}) + std::to_string(index);
}
inline std::span<const float> emitted_values(const GenerationResult& result) {
    static_assert(sizeof(std::array<float, 32>) == 32 * sizeof(float));
    return {result.emitted.empty() ? nullptr : result.emitted.front().data(), result.emitted.size() * 32};
}
struct GenerationEos {
    float reference, native;
    double reference_margin, native_margin;
};
struct GenerationReport {
    std::map<std::string, SamplerError> errors;
    std::map<std::string, SamplerError> reference_feedback;
    std::map<std::string, std::vector<float>> outputs;
    std::vector<GenerationEos> eos;
    GenerationResult timed;
    std::size_t suffix_bytes = 0, backbone_workspace = 0, emitted_capacity_bytes = 0;
};
// Diagnostic only: captured upstream latents drive the existing candidate helpers.
// The production generator has no feedback override and always feeds its own outputs.
inline std::map<std::string, SamplerError> check_reference_feedback(const PocketModelAsset& model,
        const OutLoudVoice& voice, const GenerationFixture& fixture, cpu::KernelPath path) {
    FlowLMBackbone backbone(model, voice, path);
    FlowSampler sampler(model, path);
    auto state = backbone.make_state(129);
    (void)backbone.prefill(neural_tokens, state);
    std::map<std::string, SamplerError> errors;
    DeterministicCandidate candidate;
    for (std::uint32_t i = 0; i < 47; ++i) {
        const auto name = candidate_name(i);
        if (i == 0) { sampler.bos_candidate_into(backbone, state, fixture.values(name + ".noise"), candidate); }
        else { sampler.frame_candidate_into(backbone, state, fixture.values(name + ".input"), fixture.values(name + ".noise"), candidate); }
        neural_require((candidate.backbone.eos_logit > -4.0f) == generation_candidates[i].eos,
                       "Reference-feedback EOS classification differs");
        auto compare = [&](const std::string& field, std::span<const float> values) {
            errors[name + "." + field] = sampler_error(values, fixture.values(name + "." + field));
        };
        compare("hidden", candidate.backbone.last_hidden());
        compare("eos", {&candidate.backbone.eos_logit, 1});
        compare("velocity", candidate.sample.velocity); compare("latent", candidate.sample.latent);
    }
    neural_require(state.offset() == 179, "Diagnostic final offset differs");
    return errors;
}
inline GenerationReport run_generation_checks(const PocketModelAsset& model, const OutLoudVoice& voice,
        const Tokenizer& tokenizer, const GenerationFixture& fixture, cpu::KernelPath path) {
    FlowLMBackbone backbone(model, voice, path);
    FlowSampler sampler(model, path);
    FlowLMGenerator generator(backbone, sampler);
    neural_require(FlowLMGenerator::estimate_max_gen_len(20) == 109, "Source max_gen_len estimate differs");
    const auto prepared = prepare_text(neural_segment_text, tokenizer);
    const auto tokens = tokenizer.encode(prepared.text);
    neural_require(tokens.size() == 20 && std::equal(tokens.begin(), tokens.end(), neural_tokens.begin()) &&
                   prepared.frames_after_eos_guess + 2 == 3 && voice.prefix_length() == 112, "Canonical generation text/voice differ");
    FixtureNoiseSource noise(fixture.values("noise.stream"));
    GenerationReport report;
    std::uint32_t observed = 0;
    std::vector<float> consumed_noise;
    consumed_noise.reserve(48 * 32);
    auto compare = [&](const std::string& name, std::span<const float> values, bool exact = false) {
        const auto reference = fixture.values(name);
        if (exact) { neural_require(same_float_bits(values, reference), "Generation supplied noise/BOS input bytes differ"); }
        neural_require(!report.errors.contains(name), "Duplicate generation comparison");
        report.errors[name] = sampler_error(values, reference);
        report.outputs[name] = {values.begin(), values.end()};
    };
    const auto result = generator.generate_segment(tokens, noise, {}, [&](const GenerationCall& call) {
        const bool prefill = call.input_kind == GenerationInput::text;
        const auto name = prefill ? std::string("prefill") : candidate_name(call.index);
        neural_require(observed++ == (prefill ? 0 : call.index + 1), "Generation noise/candidate order differs");
        consumed_noise.insert(consumed_noise.end(), call.noise.begin(), call.noise.end());
        if (prefill) {
            neural_require(call.offset_before == 112 && call.offset_after == 132 && !call.emitted && !call.latched_eos,
                           "Prefill discarded call scheduling differs");
        } else {
            neural_require(call.index < 47, "Unexpected extra native candidate");
            const auto& source = generation_candidates[call.index];
            const auto reference_eos = fixture.values(name + ".eos")[0];
            report.eos.push_back({reference_eos, call.eos_logit, std::abs(double(reference_eos) + 4), std::abs(double(call.eos_logit) + 4)});
            // Report classification disagreement before any derived latch/emission mismatch.
            if (call.eos != source.eos) {
                std::ostringstream message;
                message << "EOS CLASSIFICATION DIFFERS at " << name << ": native " << call.eos_logit
                        << " vs reference " << reference_eos << " (threshold -4); cannot be tolerated";
                throw std::runtime_error(message.str());
            }
            neural_require(call.offset_before == source.before && call.offset_after == source.after &&
                           call.emitted == source.emitted && call.latched_eos.value_or(UINT32_MAX) ==
                           (source.latched < 0 ? UINT32_MAX : static_cast<std::uint32_t>(source.latched)),
                           "Generation candidate stopping/state metadata differs");
            if (call.index == 0) {
                neural_require(call.input_kind == GenerationInput::bos && call.input_latent.empty(), "First candidate not explicit BOS");
                std::array<float, 32> bos;
                widen_bf16(model.tensor(PocketTensorId::flow_lm_bos_emb).bf16_values, bos);
                compare(name + ".input", bos, true);
            } else {
                neural_require(call.input_kind == GenerationInput::previous_normalized_latent &&
                    same_float_bits(call.input_latent, report.outputs.at(candidate_name(call.index - 1) + ".latent")),
                    "Generation did not feed back its own normalized latent");
                compare(name + ".input", call.input_latent);
            }
        }
        compare(name + ".hidden", call.hidden);
        compare(name + ".eos", {&call.eos_logit, 1});
        compare(name + ".noise", call.noise, true);
        compare(name + ".velocity", call.sample.velocity);
        compare(name + ".latent", call.sample.latent);
    });
    neural_require(result.prefill_offset == 132 && result.candidate_count == 47 && result.noise_draw_count == 48 &&
                   noise.consumed() == 48 && result.emitted.size() == 46 && result.first_eos == 43u && result.final_offset == 179 &&
                   result.termination == GenerationTermination::eos_tail && observed == 48, "Canonical generation summary differs");
    compare("emitted.latents", emitted_values(result));
    compare("noise.stream", consumed_noise, true);
    for (std::uint32_t layer = 0; layer < 6; ++layer) {
        const auto state = generator.state().layer(layer);
        neural_require(state.offset == 179 && state.keys.size() == 67 * 1024 && state.values.size() == 67 * 1024,
                       "Final generated K/V state dimensions differ");
        compare("final.layer" + std::to_string(layer) + ".keys", state.keys);
        compare("final.layer" + std::to_string(layer) + ".values", state.values);
    }
    neural_require(report.errors.size() == 301, "Missing generation comparison");
    FixtureNoiseSource timed_noise(fixture.values("noise.stream"));
    report.timed = generator.generate_segment(tokens, timed_noise); // no observers/comparison/console in loop
    neural_require(same_float_bits(emitted_values(result), emitted_values(report.timed)) && report.timed.final_offset == 179 &&
                   report.timed.candidate_count == 47 && report.timed.first_eos == 43u && timed_noise.consumed() == 48,
                   "Unobserved repeat generation changed output/schedule");
    report.suffix_bytes = generator.state().suffix_storage_bytes();
    report.backbone_workspace = backbone.workspace_storage_bytes();
    report.emitted_capacity_bytes = report.timed.emitted.capacity() * sizeof(std::array<float, 32>);
    return report;
}
inline void write_generation_report(const std::filesystem::path& file, const GenerationReport& report, cpu::KernelPath path) {
    std::ofstream output(file);
    output << std::setprecision(17) << "{\n  \"path\": \"" << cpu::path_name(path) << "\",\n  \"checkpoints\": ";
    write_sampler_errors(output, report.errors);
    output << ",\n  \"reference_feedback_diagnostic\": "; write_sampler_errors(output, report.reference_feedback);
    output << ",\n  \"eos_candidates\": [";
    for (std::size_t i = 0; i < report.eos.size(); ++i) {
        if (i) { output << ','; }
        const auto& e = report.eos[i];
        output << "\n    {\"index\": " << i << ", \"reference\": " << e.reference << ", \"native\": " << e.native
               << ", \"reference_threshold_margin\": " << e.reference_margin << ", \"native_threshold_margin\": " << e.native_margin << '}';
    }
    const auto& timing = report.timed.timing;
    output << "\n  ],\n  \"prefill_ms\": " << timing.prefill_ms << ",\n  \"candidates_ms\": " << timing.candidates_ms
           << ",\n  \"total_ms\": " << timing.total_ms << ",\n  \"mean_candidate_ms\": " << timing.candidates_ms / 47
           << ",\n  \"min_candidate_ms\": " << timing.min_candidate_ms << ",\n  \"max_candidate_ms\": " << timing.max_candidate_ms
           << ",\n  \"partial_latent_stage_rtf\": " << 46 * 80 / timing.total_ms
           << ",\n  \"suffix_bytes\": " << report.suffix_bytes << ",\n  \"backbone_workspace_bytes\": " << report.backbone_workspace
           << ",\n  \"emitted_capacity_bytes\": " << report.emitted_capacity_bytes << "\n}\n";
    neural_require(bool(output), "Cannot write local generation report");
}
inline void enforce_generation_errors(const std::map<std::string, SamplerError>& errors, bool diagnostic = false) {
    neural_require(errors.size() == (diagnostic ? 188 : 301), "Missing generation numerical measurements");
    for (const auto& [name, error] : errors) {
        const auto key = diagnostic ? std::string_view(name).substr(name.find('.') + 1) : std::string_view(name);
        const auto table = diagnostic ? std::span<const GenerationTolerance>(generation_diagnostic_tolerances)
                                      : std::span<const GenerationTolerance>(generation_tolerances);
        const auto it = std::find_if(table.begin(), table.end(), [&](auto& t) { return t.checkpoint == key; });
        neural_require(it != table.end(), "Unrecognized generation tolerance checkpoint");
        if (error.raw.max_absolute > it->absolute || error.raw.rms > it->rms || error.meaningful_relative > it->relative) {
            std::ostringstream message;
            message << "Generation numerical gate failed: " << name << (diagnostic ? " (reference-feedback diagnostic)" : "")
                    << "; abs " << error.raw.max_absolute << "/" << it->absolute << "; RMS " << error.raw.rms << "/" << it->rms
                    << "; relative " << error.meaningful_relative << "/" << it->relative;
            throw std::runtime_error(message.str());
        }
    }
}
} // namespace outloud::test
