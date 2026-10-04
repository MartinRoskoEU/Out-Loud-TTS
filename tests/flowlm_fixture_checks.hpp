#pragma once

#include "outloud/flowlm.hpp"
#include "outloud/text_pipeline.hpp"
#include "asset_io.hpp"
#include "flowlm_prefill_fixtures.hpp"
#include "flowlm_tolerances.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <map>
#include <sstream>
#include <stdexcept>

namespace outloud::test {
inline void neural_require(bool condition, const char* message) {
    if (!condition) { throw std::runtime_error(message); }
}
inline bool same_float_bits(std::span<const float> first, std::span<const float> second) {
    return first.size() == second.size() &&
           (first.empty() || std::memcmp(first.data(), second.data(), first.size_bytes()) == 0);
}
class NeuralFixture {
public:
    explicit NeuralFixture(const std::filesystem::path& directory)
        : file_(directory / "flowlm-prefill-f32.bin", neural_fixture_bytes) {
        const auto bytes = file_.bytes();
        neural_require(bytes.size() == neural_fixture_bytes && detail::hex(detail::sha256(bytes)) == neural_fixture_sha,
                       "Wrong neural fixture length/hash");
        std::uint64_t next = 0;
        for (const auto& c : neural_checkpoints) {
            std::uint64_t elements = 1;
            neural_require(c.rank > 0 && c.rank <= 3, "Invalid checkpoint rank");
            for (std::uint32_t d = 0; d < c.rank; ++d) { elements *= c.shape[d]; }
            neural_require(c.offset == next && c.bytes == elements * 4 && c.offset <= bytes.size() &&
                           c.bytes <= bytes.size() - c.offset, "Invalid checkpoint bounds/shape");
            const auto raw = bytes.subspan(static_cast<std::size_t>(c.offset), static_cast<std::size_t>(c.bytes));
            neural_require(detail::hex(detail::sha256(raw)) == c.sha, "Checkpoint SHA256 mismatch");
            next += c.bytes;
        }
        neural_require(next == bytes.size(), "Trailing neural fixture data");
    }
    [[nodiscard]] const NeuralCheckpoint& specification(std::string_view name) const {
        const auto it = std::find_if(neural_checkpoints.begin(), neural_checkpoints.end(),
                                    [&](const auto& c) { return c.name == name; });
        if (it == neural_checkpoints.end()) { throw std::runtime_error("Unknown neural checkpoint"); }
        return *it;
    }
    [[nodiscard]] std::span<const float> values(std::string_view name) const {
        const auto& c = specification(name);
        return {reinterpret_cast<const float*>(file_.bytes().data() + c.offset), static_cast<std::size_t>(c.bytes / 4)};
    }
private:
    detail::MappedFile file_;
};

struct NumericalError {
    double max_absolute = 0, max_relative = 0, rms = 0, zero_reference_absolute = 0;
};
inline NumericalError numerical_error(std::span<const float> actual, std::span<const float> expected) {
    neural_require(!actual.empty() && actual.size() == expected.size(), "Checkpoint element count mismatch");
    NumericalError error;
    double squared = 0;
    for (std::size_t i = 0; i < actual.size(); ++i) {
        neural_require(std::isfinite(actual[i]) && std::isfinite(expected[i]), "Nonfinite checkpoint comparison");
        const double difference = std::abs(double(actual[i]) - expected[i]);
        error.max_absolute = std::max(error.max_absolute, difference);
        if (expected[i] != 0) { error.max_relative = std::max(error.max_relative, difference / std::abs(double(expected[i]))); }
        else { error.zero_reference_absolute = std::max(error.zero_reference_absolute, difference); }
        squared += difference * difference;
    }
    error.rms = std::sqrt(squared / double(actual.size()));
    return error;
}

struct PrefillReport {
    std::map<std::string, NumericalError> checkpoints, primitives;
    std::map<std::string, std::vector<float>> outputs;
    double embedding_ms = 0, prefill_ms = 0, single_ms = 0;
    std::size_t suffix_bytes = 0, scratch_bytes = 0;
    float eos = 0;
};
inline double elapsed_ms(std::chrono::steady_clock::time_point begin) {
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - begin).count();
}
inline std::map<std::string, NumericalError> check_real_primitives(
        const PocketModelAsset& model, const OutLoudVoice& voice, const NeuralFixture& fixture, cpu::KernelPath path) {
    cpu::Kernels kernels(path);
    std::map<std::string, NumericalError> errors;
    auto linear = [&](std::string_view label, std::string_view input, std::string_view output, PocketTensorId weight,
                      std::span<const std::uint16_t> bias = {}) {
        const auto w = model.tensor(weight);
        const auto x = fixture.values(input);
        const auto rows = x.size() / w.dimensions[1];
        std::vector<float> result(rows * w.dimensions[0]);
        kernels.linear({x, rows, w.dimensions[1]}, w, {result, rows, w.dimensions[0]}, bias);
        errors.emplace(label, numerical_error(result, fixture.values(output)));
    };
    linear("input_linear", "kernel.input_linear.input", "kernel.input_linear.output", PocketTensorId::flow_lm_input_linear_weight);
    // Each projection uses untouched PyTorch input activations, isolating its reduction error.
    for (std::uint32_t layer = 0; layer < 6; ++layer) {
        const auto p = "layer" + std::to_string(layer) + ".";
        auto weight = [&](std::uint32_t field) { return static_cast<PocketTensorId>(77 + 8 * layer + field); };
        linear(p + "qkv", p + "norm1", p + "qkv", weight(6));
        linear(p + "out_proj", p + "attention_context", p + "attention_update", weight(7));
        linear(p + "linear1", p + "norm2", p + "linear1", weight(0));
        linear(p + "linear2", p + "gelu", p + "ffn_update", weight(1));
        const auto x = fixture.values(p + "input");
        std::vector<float> norm(x.size());
        cpu::layer_norm({x, 20, 1024}, model.tensor(weight(3)).bf16_values, model.tensor(weight(2)).bf16_values, {norm, 20, 1024});
        errors.emplace(p + "norm1", numerical_error(norm, fixture.values(p + "norm1")));
        cpu::layer_norm({fixture.values(p + "attention_residual"), 20, 1024}, model.tensor(weight(5)).bf16_values,
                        model.tensor(weight(4)).bf16_values, {norm, 20, 1024});
        errors.emplace(p + "norm2", numerical_error(norm, fixture.values(p + "norm2")));
        std::vector<float> gelu(fixture.values(p + "linear1").begin(), fixture.values(p + "linear1").end());
        cpu::gelu_tanh(gelu);
        errors.emplace(p + "gelu", numerical_error(gelu, fixture.values(p + "gelu")));
        const auto qkv = fixture.values(p + "qkv");
        std::vector<float> q(20 * 1024), k(q.size()), v(q.size());
        for (std::size_t token = 0; token < 20; ++token) {
            std::copy_n(qkv.data() + token * 3072, 1024, q.data() + token * 1024);
            std::copy_n(qkv.data() + token * 3072 + 1024, 1024, k.data() + token * 1024);
            std::copy_n(qkv.data() + token * 3072 + 2048, 1024, v.data() + token * 1024);
        }
        cpu::rope(q, k, 20, 112);
        errors.emplace(p + "q_rope", numerical_error(q, fixture.values(p + "q_rope")));
        errors.emplace(p + "k_rope", numerical_error(k, fixture.values(p + "k_suffix")));
        neural_require(same_float_bits(v, fixture.values(p + "v_suffix")), "Packed V layout mismatch");
        std::vector<float> attention(20 * 1024), scratch(132);
        cpu::causal_attention(kernels, fixture.values(p + "q_rope"), 20,
            {voice.keys(layer), voice.values(layer), fixture.values(p + "k_suffix"), fixture.values(p + "v_suffix"), 112, 0},
            attention, scratch);
        errors.emplace(p + "attention", numerical_error(attention, fixture.values(p + "attention_context")));
    }
    linear("eos", "final.last_hidden", "final.eos", PocketTensorId::flow_lm_out_eos_weight,
           model.tensor(PocketTensorId::flow_lm_out_eos_bias).bf16_values);
    return errors;
}

inline void enforce_errors(const std::map<std::string, NumericalError>& errors, bool primitive) {
    for (const auto& [name, error] : errors) {
        const auto limit = error_limits(name, primitive);
        if (error.max_absolute > limit.max_absolute || error.rms > limit.rms) {
            std::ostringstream message;
            message << std::scientific << "Numerical regression at " << name << ": max absolute "
                    << error.max_absolute << " (limit " << limit.max_absolute << "), RMS " << error.rms
                    << " (limit " << limit.rms << ")";
            throw std::runtime_error(message.str());
        }
    }
}
inline void enforce_report(const PrefillReport& report) {
    // 1 embedding + 6*14 layer stages + 3 final stages + 2 single-position stages.
    neural_require(report.checkpoints.size() == 90 && report.primitives.size() == 62, "Missing numerical checkpoints");
    enforce_errors(report.checkpoints, false);
    enforce_errors(report.primitives, true);
}
inline std::map<std::string, NumericalError> compare_paths(const PrefillReport& portable, const PrefillReport& optimized) {
    neural_require(portable.outputs.size() == optimized.outputs.size(), "CPU paths captured different checkpoints");
    std::map<std::string, NumericalError> agreement;
    for (const auto& [name, values] : optimized.outputs) {
        agreement[name] = numerical_error(values, portable.outputs.at(name));
    }
    return agreement;
}
inline void enforce_path_agreement(const std::map<std::string, NumericalError>& agreement) {
    // First measurements: max abs 3.815e-6; nonscalar RMS 3.893e-7.
    for (const auto& [name, error] : agreement) {
        neural_require(error.max_absolute <= 8e-6 && error.rms <= (name.ends_with(".eos") ? 8e-6 : 8e-7),
                       "Portable/AVX2 numerical disagreement");
    }
}

inline PrefillReport run_prefill_checks(const PocketModelAsset& model, const OutLoudVoice& voice,
        const Tokenizer& tokenizer, const NeuralFixture& fixture, cpu::KernelPath path) {
    neural_require(model.compatibility_id() == neural_compatibility_id && voice.source_sha256() == neural_voice_sha &&
                   voice.prefix_length() == 112, "Neural model/voice identity mismatch");
    voice.validate_against(model);
    detail::Sha256 prefix;
    for (std::uint32_t i = 0; i < 6; ++i) {
        const auto values = voice.cache(i);
        prefix.update({reinterpret_cast<const unsigned char*>(values.data()), values.size_bytes()});
    }
    neural_require(detail::hex(prefix.finish()) == neural_voice_payload_sha, "Original voice prefix mismatch");
    const auto prepared = prepare_text(neural_segment_text, tokenizer);
    const auto tokens = tokenizer.encode(prepared.text);
    neural_require(tokens.size() == 20 && std::equal(tokens.begin(), tokens.end(), neural_tokens.begin()), "Native segment token mismatch");
    FlowLMBackbone backbone(model, voice, path);
    auto state = backbone.make_state(21);
    PrefillReport report;
    const auto embedding_begin = std::chrono::steady_clock::now();
    const auto embedding = backbone.embed(tokens);
    report.embedding_ms = elapsed_ms(embedding_begin);
    report.checkpoints.emplace("text.embedding", numerical_error(embedding, fixture.values("text.embedding")));
    neural_require(same_float_bits(embedding, fixture.values("text.embedding")), "Embedding must be bit-exact");
    const FlowLMObserver observer = [&](std::string_view name, std::span<const float> values, std::span<const std::uint32_t> shape) {
        const auto& specification = fixture.specification(name);
        neural_require(shape.size() == specification.rank && std::equal(shape.begin(), shape.end(), specification.shape.begin()),
                       "Checkpoint shape/layout mismatch");
        report.checkpoints[std::string(name)] = numerical_error(values, fixture.values(name));
        report.outputs[std::string(name)] = {values.begin(), values.end()};
    };
    const auto result = backbone.prefill(tokens, state, observer);
    neural_require(state.offset() == 132, "Final canonical offset must be 132");
    for (std::uint32_t i = 0; i < 6; ++i) {
        const auto suffix = state.layer(i);
        const auto p = "layer" + std::to_string(i) + ".";
        neural_require(suffix.offset == 132 && suffix.keys.size() == 20 * 1024 && suffix.values.size() == suffix.keys.size(),
                       "Wrong mutable K/V suffix dimensions");
        neural_require(same_float_bits(suffix.keys, report.outputs.at(p + "k_suffix")) &&
                       same_float_bits(suffix.values, report.outputs.at(p + "v_suffix")),
                       "Stored K/V differs from post-RoPE append");
    }
    report.eos = result.eos_logit;
    report.suffix_bytes = state.suffix_storage_bytes();
    report.scratch_bytes = result.scratch_bytes;
    // No observer/reference work inside informational timings.
    state.reset();
    const auto prefill_begin = std::chrono::steady_clock::now();
    const auto timed = backbone.prefill(tokens, state);
    report.prefill_ms = elapsed_ms(prefill_begin);
    neural_require(same_float_bits(timed.normalized, result.normalized) &&
                   std::bit_cast<std::uint32_t>(timed.eos_logit) == std::bit_cast<std::uint32_t>(result.eos_logit),
                   "Native bitwise repeat differs");
    const auto projected = fixture.values("kernel.input_linear.output");
    const auto single_begin = std::chrono::steady_clock::now();
    const auto single = backbone.backbone(projected, state);
    report.single_ms = elapsed_ms(single_begin);
    neural_require(state.offset() == 133, "Single deterministic backbone offset must be 133");
    report.checkpoints.emplace("single.last_hidden", numerical_error(single.last_hidden(), fixture.values("single.last_hidden")));
    report.checkpoints.emplace("single.eos", numerical_error({&single.eos_logit, 1}, fixture.values("single.eos")));
    report.outputs["single.last_hidden"] = {single.last_hidden().begin(), single.last_hidden().end()};
    report.outputs["single.eos"] = {single.eos_logit};
    return report;
}

inline void write_error_map(std::ostream& output, const std::map<std::string, NumericalError>& errors) {
    bool first = true;
    output << "{";
    for (const auto& [name, error] : errors) {
        if (!first) { output << ","; } first = false;
        output << "\n    \"" << name << "\": {\"max_absolute\": " << error.max_absolute
               << ", \"max_relative_nonzero\": " << error.max_relative << ", \"rms\": " << error.rms
               << ", \"zero_reference_absolute\": " << error.zero_reference_absolute << "}";
    }
    output << "\n  }";
}
inline void write_report(const std::filesystem::path& path, const PrefillReport& report, cpu::KernelPath kernel_path) {
    std::ofstream output(path);
    neural_require(bool(output), "Cannot write local parity report");
    output << std::setprecision(17) << "{\n  \"path\": \"" << cpu::path_name(kernel_path) << "\",\n  \"primitives\": ";
    write_error_map(output, report.primitives);
    output << ",\n  \"checkpoints\": "; write_error_map(output, report.checkpoints);
    output << ",\n  \"embedding_ms\": " << report.embedding_ms << ",\n  \"prefill_ms\": " << report.prefill_ms
           << ",\n  \"single_backbone_ms\": " << report.single_ms << ",\n  \"suffix_bytes\": " << report.suffix_bytes
           << ",\n  \"scratch_bytes\": " << report.scratch_bytes << ",\n  \"eos_logit\": " << report.eos << "\n}\n";
    neural_require(bool(output), "Cannot finish local parity report");
}
} // namespace outloud::test
