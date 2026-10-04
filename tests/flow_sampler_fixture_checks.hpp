#pragma once

#include "outloud/flow_sampler.hpp"
#include "flowlm_fixture_checks.hpp"
#include "flow_sampler_fixtures.hpp"
#include "flow_sampler_tolerances.hpp"

namespace outloud::test {
class SamplerFixture {
public:
    explicit SamplerFixture(const std::filesystem::path& directory)
        : file_(directory / "flow-sampler-f32.bin", sampler_fixture_bytes) {
        const auto bytes = file_.bytes();
        neural_require(bytes.size() == sampler_fixture_bytes && detail::hex(detail::sha256(bytes)) == sampler_fixture_sha,
                       "Sampler fixture length/hash mismatch");
        std::uint64_t next = 0;
        for (const auto& c : sampler_checkpoints) {
            neural_require(c.offset == next && c.elements > 0 && c.elements <= 1024 &&
                           c.offset <= bytes.size() && c.elements * 4 <= bytes.size() - c.offset,
                           "Sampler checkpoint shape/bounds mismatch");
            const auto raw = bytes.subspan(static_cast<std::size_t>(c.offset), static_cast<std::size_t>(c.elements * 4));
            neural_require(detail::hex(detail::sha256(raw)) == c.sha, "Sampler checkpoint hash mismatch");
            next += c.elements * 4;
        }
        neural_require(next == bytes.size(), "Sampler fixture trailing data");
    }
    [[nodiscard]] std::span<const float> values(std::string_view name) const {
        const auto it = std::find_if(sampler_checkpoints.begin(), sampler_checkpoints.end(),
                                    [&](const auto& c) { return c.name == name; });
        if (it == sampler_checkpoints.end()) { throw std::runtime_error("Unknown sampler checkpoint"); }
        return {reinterpret_cast<const float*>(file_.bytes().data() + it->offset), static_cast<std::size_t>(it->elements)};
    }
private:
    detail::MappedFile file_;
};
struct SamplerError { NumericalError raw; double meaningful_relative; };
inline SamplerError sampler_error(std::span<const float> values, std::span<const float> expected) {
    const auto raw = numerical_error(values, expected); // validate dimensions/finiteness before indexed access
    double scale = 1;
    for (float value : expected) { scale = std::max(scale, std::abs(double(value))); }
    // Report a useful relative metric AND the unrestricted nonzero-relative metric.
    const double meaningful_minimum = 1e-6 * scale;
    double meaningful = 0;
    for (std::size_t i = 0; i < values.size(); ++i) {
        if (std::abs(double(expected[i])) >= meaningful_minimum) {
            meaningful = std::max(meaningful, std::abs(double(values[i]) - expected[i]) / std::abs(double(expected[i])));
        }
    }
    return {raw, meaningful};
}
struct SamplerReport {
    std::map<std::string, SamplerError> errors, primitives;
    std::map<std::string, std::vector<float>> outputs;
    double sampler_ms = 0, candidate_ms = 0;
};
inline std::map<std::string, SamplerError> check_sampler_primitives(const PocketModelAsset& model,
        const SamplerFixture& fixture, cpu::KernelPath path) {
    cpu::Kernels kernels(path);
    std::map<std::string, SamplerError> errors;
    auto tensor = [&](std::uint32_t id) { return model.tensor(static_cast<PocketTensorId>(id)); };
    for (const auto case_name : {"prefill", "bos"}) {
        const auto c = std::string(case_name) + ".";
        auto compare = [&](const std::string& name, std::span<const float> values, const std::string& reference) {
            errors[name] = sampler_error(values, fixture.values(reference));
        };
        auto linear = [&](const std::string& name, const std::string& input, const std::string& output,
                          std::uint32_t weight, std::uint32_t bias) {
            const auto w = tensor(weight);
            const auto x = fixture.values(input);
            std::vector<float> result(w.dimensions[0]);
            kernels.linear({x, 1, x.size()}, w, {result, 1, result.size()}, tensor(bias).bf16_values);
            compare(name, result, output);
        };
        linear(c + "input.projected", c + "input.noise", c + "input.projected", 11, 10);
        linear(c + "condition.projected", c + "input.hidden", c + "condition.projected", 5, 4);
        for (std::uint32_t index = 0; index < 2; ++index) {
            const auto p = c + (index == 0 ? "time.s." : "time.t.");
            const auto base = 60 + 6 * index;
            std::array<float, 256> features;
            cpu::time_features(static_cast<float>(index), tensor(base).bf16_values, features);
            compare(p + "features", features, p + "features");
            linear(p + "linear1", p + "features", p + "linear1", base + 2, base + 1);
            auto silu = std::vector<float>(fixture.values(p + "linear1").begin(), fixture.values(p + "linear1").end());
            cpu::silu(silu); compare(p + "silu", silu, p + "silu");
            linear(p + "mlp", p + "silu", p + "mlp", base + 4, base + 3);
            std::array<float, 512> norm;
            cpu::sample_variance_norm(fixture.values(p + "mlp"), tensor(base + 5).bf16_values, norm);
            compare(p + "normalized", norm, p + "normalized");
        }
        for (std::uint32_t block = 0; block < 6; ++block) {
            const auto p = c + "block" + std::to_string(block) + ".";
            const auto base = 12 + block * 8;
            std::array<float, 512> norm;
            cpu::sampler_layer_norm(fixture.values(p + "input"), tensor(base + 3).bf16_values, tensor(base + 2).bf16_values, norm);
            compare(p + "norm", norm, p + "norm");
            auto condition_silu = std::vector<float>(fixture.values(c + "condition.combined").begin(), fixture.values(c + "condition.combined").end());
            cpu::silu(condition_silu); compare(p + "condition_silu", condition_silu, p + "condition_silu");
            std::array<float, 1536> parameters;
            kernels.linear({fixture.values(p + "condition_silu"), 1, 512}, tensor(base + 1), {parameters, 1, 1536}, tensor(base).bf16_values);
            for (std::uint32_t part = 0; part < 3; ++part) {
                const auto field = std::array{"shift", "scale", "gate"}[part];
                compare(p + field, std::span(parameters).subspan(part * 512, 512), p + field);
            }
            linear(p + "linear1", p + "modulated", p + "linear1", base + 5, base + 4);
            auto silu = std::vector<float>(fixture.values(p + "linear1").begin(), fixture.values(p + "linear1").end());
            cpu::silu(silu); compare(p + "silu", silu, p + "silu");
            linear(p + "linear2", p + "silu", p + "linear2", base + 7, base + 6);
        }
        std::array<float, 512> norm;
        cpu::sampler_layer_norm(fixture.values(c + "block5.output"), {}, {}, norm);
        compare(c + "final.norm", norm, c + "final.norm");
        auto condition_silu = std::vector<float>(fixture.values(c + "condition.combined").begin(),
                                                fixture.values(c + "condition.combined").end());
        cpu::silu(condition_silu); compare(c + "final.condition_silu", condition_silu, c + "final.condition_silu");
        std::array<float, 1024> parameters;
        kernels.linear({fixture.values(c + "final.condition_silu"), 1, 512}, tensor(7), {parameters, 1, 1024}, tensor(6).bf16_values);
        compare(c + "final.shift", std::span(parameters).first(512), c + "final.shift");
        compare(c + "final.scale", std::span(parameters).last(512), c + "final.scale");
        linear(c + "velocity", c + "final.modulated", c + "velocity", 9, 8);
    }
    return errors;
}
inline SamplerReport run_sampler_checks(const PocketModelAsset& model, const OutLoudVoice& voice,
        const Tokenizer& tokenizer, const SamplerFixture& fixture, cpu::KernelPath path) {
    FlowSampler sampler(model, path);
    SamplerReport report;
    auto observer = [&](std::string label, std::string case_name) -> SamplerObserver {
        return [&, label=std::move(label), case_name=std::move(case_name)](std::string_view stage, std::span<const float> values) {
            const auto reference = fixture.values(case_name + "." + std::string(stage));
            neural_require(values.size() == reference.size(), "Sampler checkpoint dimensions mismatch");
            if (stage == "input.noise" || stage == "time.s.scalar" || stage == "time.t.scalar" ||
                (label == case_name && stage == "input.hidden")) {
                neural_require(same_float_bits(values, reference), "Supplied sampler input is not bit-exact");
            }
            const auto name = label + "." + std::string(stage);
            neural_require(!report.errors.contains(name), "Duplicate sampler checkpoint");
            report.errors[name] = sampler_error(values, reference);
            report.outputs[name] = {values.begin(), values.end()};
        };
    };
    for (const auto case_name : {"prefill", "bos"}) {
        const auto c = std::string(case_name) + ".";
        const auto hidden = fixture.values(c + "input.hidden"), noise = fixture.values(c + "input.noise");
        const auto sample = sampler.sample(hidden, noise, observer(case_name, case_name));
        const auto repeated = sampler.sample(hidden, noise);
        neural_require(same_float_bits(sample.latent, repeated.latent) && same_float_bits(sample.velocity, repeated.velocity),
                       "Sampler repeat is not bit-exact");
        for (std::size_t i = 0; i < 32; ++i) {
            neural_require(std::bit_cast<std::uint32_t>(sample.latent[i]) == std::bit_cast<std::uint32_t>(noise[i] + sample.velocity[i]),
                           "Native one-step LSD addition differs");
        }
    }
    FlowLMBackbone backbone(model, voice, path);
    auto state = backbone.make_state(21);
    const auto prepared = prepare_text(neural_segment_text, tokenizer);
    const auto tokens = tokenizer.encode(prepared.text);
    neural_require(tokens.size() == 20 && std::equal(tokens.begin(), tokens.end(), neural_tokens.begin()), "Canonical sampler tokens differ");
    auto prefill = backbone.prefill(tokens, state);
    neural_require(state.offset() == 132, "Sampler integration requires offset 132");
    const auto discarded = sampler.sample(prefill.last_hidden(), fixture.values("prefill.input.noise"), observer("integrated.prefill", "prefill"));
    (void)discarded;
    const auto candidate = sampler.bos_candidate(backbone, state, fixture.values("bos.input.noise"), observer("integrated.bos", "bos"));
    neural_require(state.offset() == 133 && candidate.backbone.last_hidden().size() == 1024, "BOS candidate final offset differs");
    neural_require(report.errors.size() == 384, "Missing sampler checkpoint");
    const auto sampler_begin = std::chrono::steady_clock::now();
    const auto timed_sample = sampler.sample(fixture.values("bos.input.hidden"), fixture.values("bos.input.noise"));
    report.sampler_ms = elapsed_ms(sampler_begin);
    neural_require(same_float_bits(timed_sample.latent, report.outputs.at("bos.latent")), "Timed sampler changed result");
    state.reset();
    prefill = backbone.prefill(tokens, state);
    const auto candidate_begin = std::chrono::steady_clock::now();
    const auto timed_candidate = sampler.bos_candidate(backbone, state, fixture.values("bos.input.noise"));
    report.candidate_ms = elapsed_ms(candidate_begin);
    neural_require(same_float_bits(timed_candidate.sample.latent, candidate.sample.latent), "Timed candidate changed result");
    return report;
}
inline void write_sampler_errors(std::ostream& output, const std::map<std::string, SamplerError>& errors) {
    bool first = true; output << "{";
    for (const auto& [name, error] : errors) {
        if (!first) { output << ","; } first = false;
        output << "\n    \"" << name << "\": {\"max_absolute\": " << error.raw.max_absolute
               << ", \"max_relative_nonzero\": " << error.raw.max_relative
               << ", \"meaningful_relative\": " << error.meaningful_relative << ", \"rms\": " << error.raw.rms
               << ", \"zero_reference_absolute\": " << error.raw.zero_reference_absolute << "}";
    }
    output << "\n  }";
}
inline void enforce_sampler_errors(const std::map<std::string, SamplerError>& errors) {
    for (const auto& [name, error] : errors) {
        std::string_view stage(name);
        if (stage.starts_with("integrated.")) { stage.remove_prefix(11); }
        stage.remove_prefix(stage.find('.') + 1); // canonical case prefix
        const auto tolerance = std::find_if(sampler_tolerances.begin(), sampler_tolerances.end(),
                                           [&](const auto& t) { return t.stage == stage; });
        neural_require(tolerance != sampler_tolerances.end(), "Unrecognized sampler tolerance stage");
        if (error.raw.max_absolute > tolerance->absolute || error.raw.rms > tolerance->rms ||
            error.meaningful_relative > tolerance->relative) {
            throw std::runtime_error("Sampler numerical gate failed: " + name);
        }
    }
}
inline void write_sampler_report(const std::filesystem::path& path, const SamplerReport& report, cpu::KernelPath kernel_path) {
    std::ofstream output(path);
    output << std::setprecision(17) << "{\n  \"path\": \"" << cpu::path_name(kernel_path) << "\",\n  \"primitives\": ";
    write_sampler_errors(output, report.primitives);
    output << ",\n  \"checkpoints\": "; write_sampler_errors(output, report.errors);
    output << ",\n  \"sampler_ms\": " << report.sampler_ms << ",\n  \"candidate_ms\": " << report.candidate_ms << "\n}\n";
    neural_require(bool(output), "Cannot write local sampler report");
}
} // namespace outloud::test
