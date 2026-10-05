#pragma once
#include "outloud/cpu/mimi_kernels.hpp"
#include "outloud/mimi.hpp"
#include "generation_fixture_checks.hpp"
#include "mimi_decoder_fixtures.hpp"
#include "mimi_tolerances.hpp"

namespace outloud::test {
class MimiFixture {
public:
    explicit MimiFixture(const std::filesystem::path& directory)
        : file_(directory / "mimi-decoder-f32.bin", mimi_fixture_bytes) {
        const auto bytes = file_.bytes();
        neural_require(bytes.size() == mimi_fixture_bytes && detail::hex(detail::sha256(bytes)) == mimi_fixture_sha,
                       "Mimi fixture length/hash mismatch");
        std::uint64_t next = 0;
        for (const auto& c : mimi_checkpoints) {
            neural_require(c.offset == next && c.offset <= bytes.size() && c.elements <= (bytes.size() - c.offset) / 4,
                           "Mimi fixture bounds mismatch");
            neural_require(detail::hex(detail::sha256(bytes.subspan(static_cast<std::size_t>(c.offset),
                           static_cast<std::size_t>(c.elements * 4)))) == c.sha, "Mimi checkpoint hash mismatch");
            next += c.elements * 4;
        }
        neural_require(next == bytes.size(), "Mimi fixture trailing data");
    }
    [[nodiscard]] std::span<const float> values(std::string_view name) const {
        const auto it = std::find_if(mimi_checkpoints.begin(), mimi_checkpoints.end(), [&](const auto& c) { return c.name == name; });
        if (it == mimi_checkpoints.end()) { throw std::runtime_error("Unknown Mimi checkpoint: " + std::string(name)); }
        return {reinterpret_cast<const float*>(file_.bytes().data() + it->offset), static_cast<std::size_t>(it->elements)};
    }
private:
    detail::MappedFile file_;
};
inline std::string mimi_frame(std::size_t frame) {
    return "frame" + (frame < 10 ? std::string("0") : std::string{}) + std::to_string(frame) + ".";
}
inline void enforce_mimi_errors(const std::map<std::string, SamplerError>& errors) {
    for (const auto& [name, e] : errors) {
        auto stage = name;
        const auto frame = stage.find("frame");
        if (frame != std::string::npos) { stage.erase(frame, 8); }
        const auto t = std::find_if(mimi_tolerances.begin(), mimi_tolerances.end(), [&](const auto& v) { return v.stage == stage; });
        neural_require(t != mimi_tolerances.end(), "Unknown Mimi numerical stage");
        if (e.raw.max_absolute > t->absolute || e.raw.rms > t->rms || e.meaningful_relative > t->relative) {
            throw std::runtime_error("Mimi numerical gate failed: " + name);
        }
    }
}
inline std::map<std::string, SamplerError> check_mimi_convolutions(const PocketModelAsset& model,
        const MimiFixture& fixture, cpu::KernelPath path) {
    cpu::MimiConvolutionKernels kernels(path);
    std::map<std::string, SamplerError> errors;
    for (std::size_t i = 0; i < cpu::mimi_convolutions.size(); ++i) {
        const auto& n = cpu::mimi_convolutions[i];
        const auto it = std::find_if(mimi_reference_convolutions.begin(), mimi_reference_convolutions.end(),
                                     [&](const auto& r) { return r.name == n.name; });
        neural_require(it != mimi_reference_convolutions.end(), "Missing upstream convolution configuration");
        const auto& r = *it;
        neural_require(n.name == r.name && n.transpose == r.transpose && n.input == r.input && n.output == r.output &&
                       n.kernel == r.kernel && n.stride == r.stride && r.dilation == 1 && n.groups == r.groups &&
                       n.weight == r.weight && n.bias == r.bias, "Native convolution differs from executed upstream configuration");
    }
    for (const auto frame : {0, 1, 2, 23, 45}) {
        for (std::size_t i = 0; i < cpu::mimi_convolutions.size(); ++i) {
            const auto p = mimi_frame(frame) + std::string(cpu::mimi_convolutions[i].name);
            const auto input = fixture.values(p + ".input"), before = fixture.values(p + ".state_before");
            std::vector<float> carry(before.begin(), before.end()), output(fixture.values(p + ".output").size());
            kernels.run(model, i, input, carry, output);
            errors["primitive." + p + ".output"] = sampler_error(output, fixture.values(p + ".output"));
            if (!carry.empty()) { errors["primitive." + p + ".state_after"] = sampler_error(carry, fixture.values(p + ".state_after")); }
        }
    }
    return errors;
}
struct MimiReport {
    std::map<std::string, SamplerError> errors;
    std::map<std::string, std::vector<float>> outputs;
    std::vector<float> reference_pcm, native_pcm;
    SamplerError native_error{};
    double first_ms = 0, total_ms = 0, generation_ms = 0, prefill_ms = 0, first_candidate_ms = 0;
    double correlation = 0, peak_difference = 0, native_correlation = 0, native_peak_difference = 0;
    std::size_t kv_bytes = 0, conv_bytes = 0, scratch_bytes = 0;
};
inline std::pair<double, double> waveform_statistics(std::span<const float> x, std::span<const float> y) {
    neural_require(x.size() == y.size() && !x.empty(), "Invalid waveform statistics");
    double mx = 0, my = 0, xx = 0, yy = 0, xy = 0, px = 0, py = 0;
    for (std::size_t i = 0; i < x.size(); ++i) { mx += x[i]; my += y[i]; px = std::max(px, std::abs(double(x[i]))); py = std::max(py, std::abs(double(y[i]))); }
    mx /= x.size(); my /= x.size();
    for (std::size_t i = 0; i < x.size(); ++i) { const auto a = x[i] - mx, b = y[i] - my; xx += a*a; yy += b*b; xy += a*b; }
    return {xy / std::sqrt(xx * yy), std::abs(px - py)};
}
inline MimiReport run_mimi_checks(const PocketModelAsset& model, const OutLoudVoice& voice,
        const MimiFixture& fixture, const GenerationFixture& generation, cpu::KernelPath path, bool measure = false) {
    MimiReport report;
    report.errors = check_mimi_convolutions(model, fixture, path); // Always primitives first.
    if (!measure) { enforce_mimi_errors(report.errors); }
    cpu::Kernels kernels(path);
    for (const auto frame : {0, 1, 2, 23, 45}) {
        const auto prefix = mimi_frame(frame);
        std::array<float, 512> projected;
        auto weight = model.tensor(PocketTensorId::mimi_quantizer_output_proj_weight);
        const std::array<std::uint32_t, 2> dimensions = {512, 32}; weight.dimensions = dimensions;
        kernels.linear({fixture.values(prefix + "projection.input"), 1, 32}, weight, {projected, 1, 512});
        report.errors["primitive." + prefix + "projection.output"] = sampler_error(projected, fixture.values(prefix + "projection.output"));
        for (std::size_t layer = 0; layer < 2; ++layer) {
            const auto p = mimi_frame(frame) + "layer" + std::to_string(layer) + ".";
            std::vector<float> context(8192);
            cpu::mimi_attention(kernels, fixture.values(p + "q_rope"), fixture.values(p + "cache_k"),
                                fixture.values(p + "cache_v"), frame * 16, context);
            report.errors["primitive." + p + "attention_context"] = sampler_error(context, fixture.values(p + "attention_context"));
        }
    }
    if (!measure) { enforce_mimi_errors(report.errors); }
    const auto latents = fixture.values("normalized.latents");
    neural_require(same_float_bits(latents, generation.values("emitted.latents")), "Case A latents differ from frozen Step 2F");
    MimiDecoder decoder(model, path);
    MimiDecoderState state(109);
    report.reference_pcm.reserve(88320);
    for (std::size_t frame = 0; frame < 46; ++frame) {
        neural_require(state.offset() == frame * 16, "Wrong pre-decode offset");
        const auto input = latents.subspan(frame * 32, 32);
        MimiObserver observer;
        if (frame == 0 || frame == 1 || frame == 2 || frame == 23 || frame == 45) {
            const auto prefix = mimi_frame(frame);
            observer = [&, prefix](std::string_view name, std::span<const float> values) {
                const auto key = prefix + std::string(name);
                const auto expected = fixture.values(key);
                if (values.empty()) { neural_require(expected.empty(), "Empty convolution carry differs"); return; }
                if (name == "normalized") { neural_require(same_float_bits(values, input), "Decoder changed normalized latent"); }
                report.errors[key] = sampler_error(values, expected);
                report.outputs[key] = {values.begin(), values.end()};
            };
        }
        const auto pcm = decoder.decode_frame(input, state, observer);
        neural_require(pcm.size() == 1920 && state.offset() == (frame + 1) * 16 && state.valid(), "Mimi frame length/offset/state differs");
        report.reference_pcm.insert(report.reference_pcm.end(), pcm.begin(), pcm.end());
        report.errors[mimi_frame(frame) + "pcm_all"] = sampler_error(pcm, fixture.values("pcm.complete").subspan(frame * 1920, 1920));
    }
    neural_require(report.reference_pcm.size() == 88320 && state.offset() == 736, "Canonical PCM count differs");
    report.errors["pcm.complete"] = sampler_error(report.reference_pcm, fixture.values("pcm.complete"));
    if (!measure) { enforce_mimi_errors(report.errors); }
    report.outputs["pcm.complete"] = report.reference_pcm;
    std::tie(report.correlation, report.peak_difference) = waveform_statistics(report.reference_pcm, fixture.values("pcm.complete"));
    report.kv_bytes = state.kv_bytes(); report.conv_bytes = state.convolution_bytes(); report.scratch_bytes = decoder.scratch_bytes();
    state.reset();
    // The timed decoder loop contains no observers, comparisons, output or PCM copies.
    const auto start = std::chrono::steady_clock::now();
    (void)decoder.decode_frame(latents.first(32), state);
    const auto first_end = std::chrono::steady_clock::now();
    for (std::size_t frame = 1; frame < 46; ++frame) { (void)decoder.decode_frame(latents.subspan(frame * 32, 32), state); }
    const auto end = std::chrono::steady_clock::now();
    report.first_ms = std::chrono::duration<double, std::milli>(first_end - start).count();
    report.total_ms = std::chrono::duration<double, std::milli>(end - start).count();
    state.reset();
    neural_require(same_float_bits(decoder.decode_frame(latents.first(32), state), std::span(report.reference_pcm).first(1920)),
                   "Mimi reset changed first frame bits");
    FlowLMBackbone backbone(model, voice, path);
    FlowSampler sampler(model, path);
    FlowLMGenerator generator(backbone, sampler);
    FixtureNoiseSource noise(generation.values("noise.stream"));
    const auto native = generator.generate_segment(neural_tokens, noise);
    neural_require(native.emitted.size() == 46 && native.candidate_count == 47, "Case B generation count differs");
    report.generation_ms = native.timing.total_ms; report.prefill_ms = native.timing.prefill_ms;
    auto first_state = backbone.make_state(129);
    const auto prefill = backbone.prefill(neural_tokens, first_state);
    (void)sampler.sample(prefill.last_hidden(), generation.values("noise.stream").first(32));
    const auto candidate_start = std::chrono::steady_clock::now();
    (void)sampler.bos_candidate(backbone, first_state, generation.values("noise.stream").subspan(32, 32));
    const auto candidate_end = std::chrono::steady_clock::now();
    report.first_candidate_ms = std::chrono::duration<double, std::milli>(candidate_end - candidate_start).count();
    state.reset(); report.native_pcm.reserve(88320);
    for (const auto& latent : native.emitted) {
        const auto pcm = decoder.decode_frame(latent, state);
        neural_require(pcm.size() == 1920, "Case B frame count differs");
        report.native_pcm.insert(report.native_pcm.end(), pcm.begin(), pcm.end());
    }
    report.native_error = sampler_error(report.native_pcm, fixture.values("pcm.complete"));
    std::tie(report.native_correlation, report.native_peak_difference) = waveform_statistics(report.native_pcm, fixture.values("pcm.complete"));
    return report;
}
} // namespace outloud::test
