#include "flow_sampler_fixture_checks.hpp"

#include <iostream>
#include <limits>

namespace outloud {
struct FlowSamplerTestAccess {
    static std::span<const float> time(const FlowSampler& sampler,unsigned index) { return sampler.time_.at(index); }
};
}

namespace {
using outloud::test::neural_require;
template<class Function> void rejected(Function function, std::string_view reason) {
    try { function(); }
    catch (const std::exception& error) {
        neural_require(std::string_view(error.what()).find(reason) != std::string_view::npos,
                       "Sampler rejected for unexpected reason"); return;
    }
    throw std::runtime_error("Invalid sampler input/state accepted");
}
void schema(const outloud::PocketModelAsset& model) {
    std::uint32_t count = 0;
    auto check = [&](std::uint32_t id, std::string name, std::initializer_list<std::uint32_t> dimensions) {
        const auto tensor = model.tensor(static_cast<outloud::PocketTensorId>(id));
        neural_require(tensor.id == static_cast<outloud::PocketTensorId>(id) &&
                       tensor.name == "flow_lm.flow_net." + name && tensor.dtype == outloud::AssetDtype::bf16 &&
                       tensor.dimensions.size() == dimensions.size() &&
                       std::equal(tensor.dimensions.begin(), tensor.dimensions.end(), dimensions.begin()),
                       "Sampler tensor ID/name/dtype/dimensions mismatch");
        std::size_t elements = 1;
        for (auto d : dimensions) { elements *= d; }
        neural_require(tensor.bf16_values.size() == elements, "Sampler BF16 payload dimensions mismatch");
        ++count;
    };
    check(4, "cond_embed.bias", {512}); check(5, "cond_embed.weight", {512, 1024});
    check(6, "final_layer.adaLN_modulation.1.bias", {1024});
    check(7, "final_layer.adaLN_modulation.1.weight", {1024, 512});
    check(8, "final_layer.linear.bias", {32}); check(9, "final_layer.linear.weight", {32, 512});
    check(10, "input_proj.bias", {512}); check(11, "input_proj.weight", {512, 32});
    for (std::uint32_t block = 0; block < 6; ++block) {
        const auto base = 12 + 8 * block;
        const auto p = "res_blocks." + std::to_string(block) + ".";
        check(base, p + "adaLN_modulation.1.bias", {1536});
        check(base + 1, p + "adaLN_modulation.1.weight", {1536, 512});
        check(base + 2, p + "in_ln.bias", {512}); check(base + 3, p + "in_ln.weight", {512});
        check(base + 4, p + "mlp.0.bias", {512}); check(base + 5, p + "mlp.0.weight", {512, 512});
        check(base + 6, p + "mlp.2.bias", {512}); check(base + 7, p + "mlp.2.weight", {512, 512});
    }
    for (std::uint32_t time = 0; time < 2; ++time) {
        const auto base = 60 + 6 * time;
        const auto p = "time_embed." + std::to_string(time) + ".";
        check(base, p + "freqs", {128}); check(base + 1, p + "mlp.0.bias", {512});
        check(base + 2, p + "mlp.0.weight", {512, 256}); check(base + 3, p + "mlp.2.bias", {512});
        check(base + 4, p + "mlp.2.weight", {512, 512}); check(base + 5, p + "mlp.3.alpha", {512});
    }
    neural_require(count == 68, "Missing sampler tensor schema check");
}
void primitive_edges(const outloud::PocketModelAsset& model) {
    std::array<float, 5> silu{-100, -1, 0, 1, 100};
    outloud::cpu::silu(silu);
    neural_require(std::abs(silu[0]) < 1e-35f && std::abs(silu[1] + 0.2689414214f) < 1e-7f &&
                   silu[2] == 0 && std::abs(silu[3] - 0.7310585786f) < 1e-7f && silu[4] == 100,
                   "SiLU zero/sign/saturation mismatch");
    std::array<float, 512> x, output;
    std::array<std::uint16_t, 512> alpha, scale, bias;
    alpha.fill(0x3f80); scale.fill(0x4000); bias.fill(0x3f00); // BF16 1, 2, 0.5
    for (std::size_t i = 0; i < x.size(); ++i) { x[i] = i % 2 == 0 ? 1.0f : 3.0f; }
    outloud::cpu::sample_variance_norm(x, alpha, output);
    const auto denominator = std::sqrt(512.0 / 511.0 + 1e-5);
    for (std::size_t i = 0; i < x.size(); ++i) {
        neural_require(std::abs(output[i] - x[i] / denominator) < 2e-6,
                       "Sample correction 1 / no mean subtraction mismatch");
    }
    // Constant nonzero input retains its value and divides by sqrt(eps), unlike conventional RMS or LayerNorm.
    x.fill(2); outloud::cpu::sample_variance_norm(x, alpha, output);
    neural_require(std::abs(output[0] - 2 / std::sqrt(1e-5)) < 1e-4, "Sample-variance norm epsilon/centering mismatch");
    for (std::size_t i = 0; i < x.size(); ++i) { x[i] = i % 2 == 0 ? 0.0f : 0.002f; }
    outloud::cpu::sampler_layer_norm(x, scale, bias, output);
    const auto population_std = std::sqrt(1e-6 + 1e-6);
    for (std::size_t i = 0; i < x.size(); ++i) {
        neural_require(std::abs(output[i] - ((double(x[i]) - 0.001) / population_std * 2 + 0.5)) < 2e-6,
                       "Population LayerNorm affine/epsilon mismatch");
    }
    outloud::cpu::sampler_layer_norm(x, {}, {}, output);
    neural_require(std::abs(output[0] + 1 / std::sqrt(2.0)) < 1e-6, "Final non-affine LayerNorm mismatch");
    auto in_place = x;
    outloud::cpu::sampler_layer_norm(in_place, {}, {}, in_place);
    neural_require(outloud::test::same_float_bits(output, in_place), "In-place sampler LayerNorm differs");
    outloud::cpu::sample_variance_norm(x, alpha, output);
    in_place = x; outloud::cpu::sample_variance_norm(in_place, alpha, in_place);
    neural_require(outloud::test::same_float_bits(output, in_place), "In-place time norm differs");
    rejected([&] { outloud::cpu::sample_variance_norm(std::span(x).first(511), alpha, output); }, "dimensions");
    rejected([&] { outloud::cpu::sampler_layer_norm(x, alpha, {}, output); }, "dimensions");
    rejected([&] { outloud::cpu::sampler_layer_norm(x, {}, {}, std::span(output).first(511)); }, "dimensions");
    const auto frequencies = model.tensor(outloud::PocketTensorId::flow_lm_flow_net_time_embed_1_freqs).bf16_values;
    std::array<float, 256> features, changed;
    outloud::cpu::time_features(0, frequencies, features);
    for (std::size_t i = 0; i < 128; ++i) {
        neural_require(features[i] == 1 && std::bit_cast<std::uint32_t>(features[i + 128]) == 0,
                       "Time feature COS then SIN / zero time mismatch");
    }
    outloud::cpu::time_features(1, frequencies, features);
    std::array<std::uint16_t, 128> edited;
    std::copy(frequencies.begin(), frequencies.end(), edited.begin()); edited[0] = 0x4000;
    outloud::cpu::time_features(1, edited, changed);
    neural_require(changed[0] != features[0] && changed[128] != features[128] &&
                   std::equal(changed.begin() + 1, changed.begin() + 128, features.begin() + 1) &&
                   std::equal(changed.begin() + 129, changed.end(), features.begin() + 129),
                   "Time features ignored stored BF16 frequencies");
    rejected([&] { outloud::cpu::time_features(1, frequencies.first(127), features); }, "dimensions");
    rejected([&] { outloud::cpu::time_features(std::numeric_limits<float>::quiet_NaN(), frequencies, features); }, "value");
}
void invalid_states(const outloud::PocketModelAsset& model, const outloud::OutLoudVoice& voice,
                    const outloud::test::SamplerFixture& fixture) {
    outloud::FlowSampler sampler(model);
    neural_require(sampler.selected_path() == outloud::cpu::KernelPath::portable, "Sampler default must be portable");
    const auto hidden = fixture.values("bos.input.hidden"), noise = fixture.values("bos.input.noise");
    rejected([&] { (void)sampler.sample(hidden.first(1023), noise); }, "dimensions");
    rejected([&] { (void)sampler.sample(hidden, noise.first(31)); }, "dimensions");
    std::array<float, 32> bad_noise;
    std::copy(noise.begin(), noise.end(), bad_noise.begin()); bad_noise[0] = std::numeric_limits<float>::quiet_NaN();
    rejected([&] { (void)sampler.sample(hidden, bad_noise); }, "nonfinite");
    std::array<float, 1024> bad_hidden{}; bad_hidden[1] = std::numeric_limits<float>::infinity();
    rejected([&] { (void)sampler.sample(bad_hidden, noise); }, "nonfinite");
    outloud::FlowLMBackbone backbone(model, voice);
    auto state = backbone.make_state(21);
    (void)backbone.prefill(outloud::test::neural_tokens, state);
    rejected([&] { (void)sampler.bos_candidate(backbone, state, noise.first(31)); }, "dimensions");
    rejected([&] { (void)sampler.bos_candidate(backbone, state, bad_noise); }, "nonfinite");
    neural_require(state.offset() == 132, "Invalid supplied noise advanced state");
    auto full = backbone.make_state(20);
    (void)backbone.prefill(outloud::test::neural_tokens, full);
    rejected([&] { (void)sampler.bos_candidate(backbone, full, noise); }, "capacity");
    neural_require(full.offset() == 132, "Exhausted capacity advanced state");
    auto poisoned = backbone.make_state(21);
    rejected([&] { (void)backbone.prefill(outloud::test::neural_tokens, poisoned,
        [](std::string_view name, auto, auto) {
            if (name == "layer0.k_suffix") { throw std::runtime_error("Interrupted observer"); }
        }); }, "observer");
    rejected([&] { (void)sampler.bos_candidate(backbone, poisoned, noise); }, "reset required");
    const outloud::FlowSampler automatic(model, outloud::cpu::KernelPath::automatic);
    neural_require(automatic.selected_path() == (outloud::cpu::optimized_available() ? outloud::cpu::KernelPath::avx2_fma
                                                                                  : outloud::cpu::KernelPath::portable),
                   "Sampler automatic path dispatch mismatch");
    if (!outloud::cpu::optimized_available()) {
        rejected([&] { outloud::FlowSampler unavailable(model, outloud::cpu::KernelPath::avx2_fma); }, "unavailable");
    }
    std::map<std::string, outloud::test::SamplerError> failures;
    failures["bos.latent"] = {{1, 0, 0, 0}, 0};
    rejected([&] { outloud::test::enforce_sampler_errors(failures); }, "numerical gate");
    failures["bos.latent"] = {{0, 0, 1, 0}, 0};
    rejected([&] { outloud::test::enforce_sampler_errors(failures); }, "numerical gate");
    failures["bos.latent"] = {{0, 0, 0, 0}, 1};
    rejected([&] { outloud::test::enforce_sampler_errors(failures); }, "numerical gate");
}
void cached_conditioning(const outloud::PocketModelAsset& model,const outloud::test::SamplerFixture& fixture,outloud::cpu::KernelPath path) {
    using namespace outloud;
    FlowSampler sampler(model,path); cpu::Kernels kernels(path);
    for (unsigned index=0;index<2;++index) {
        // Only the original tiny time-MLP primitive, independently recomputed for exact cache verification.
        const auto base=60+6*index; auto tensor=[&](unsigned id) { return model.tensor(static_cast<PocketTensorId>(id)); };
        std::array<float,256> features; std::array<float,512> norm,output;
        cpu::time_features(static_cast<float>(index),tensor(base).bf16_values,features);
        kernels.linear({features,1,256},tensor(base+2),{norm,1,512},tensor(base+1).bf16_values);
        cpu::silu(norm); kernels.linear({norm,1,512},tensor(base+4),{output,1,512},tensor(base+3).bf16_values);
        cpu::sample_variance_norm(output,tensor(base+5).bf16_values,output);
        neural_require(test::same_float_bits(output,FlowSamplerTestAccess::time(sampler,index)),"Cached time embedding differs bitwise");
    }
    std::array<float,512> expected_condition;
    for (const auto* label:{"prefill","bos","prefill"}) {
        unsigned sites=0; const std::string prefix=std::string(label)+'.';
        const auto hidden=fixture.values(prefix+"input.hidden"), noise=fixture.values(prefix+"input.noise");
        const auto sampled=sampler.sample(hidden,noise,[&](std::string_view name,std::span<const float> values) {
            if (name=="condition.combined") { std::copy(values.begin(),values.end(),expected_condition.begin()); cpu::silu(expected_condition); }
            if (name.ends_with("condition_silu")) {
                neural_require(test::same_float_bits(values,expected_condition),"Reused condition SiLU differs"); ++sites;
            }
        });
        const auto plain=sampler.sample(hidden,noise);
        neural_require(sites==7 && test::same_float_bits(plain.latent,sampled.latent),"Condition site coverage/observed result differs");
        rejected([&] { (void)sampler.sample(hidden,noise,[](auto name,auto) {
            if (name=="condition.combined") { throw std::runtime_error("Condition observer failure"); }
        }); },"observer failure");
        neural_require(test::same_float_bits(sampler.sample(hidden,noise).latent,plain.latent),"Sampler failed-request recovery differs");
    }
    std::cout << cpu::path_name(path) << ": both cached time embeddings bitwise, 7 condition sites/sample, alternating hidden/failure reuse PASS\n";
}
} // namespace
int wmain(int argc, wchar_t** argv) {
    try {
        if (argc != 3) { throw std::invalid_argument("Usage: outloud-flow-sampler-tests assets neural-fixtures"); }
        const std::filesystem::path assets(argv[1]);
        const outloud::PocketModelAsset model(assets / "pocket-english.olp");
        const outloud::OutLoudVoice voice(assets / "gnome_male.olv");
        const outloud::test::SamplerFixture fixture(argv[2]);
        schema(model); primitive_edges(model); invalid_states(model, voice, fixture);
        cached_conditioning(model,fixture,outloud::cpu::KernelPath::portable);
        if (outloud::cpu::optimized_available()) { cached_conditioning(model,fixture,outloud::cpu::KernelPath::avx2_fma); }
        std::cout << "68 BF16 tensor IDs/names/shapes: PASS\nSiLU / sample-variance norm / population LayerNorm: PASS\n"
                     "Stored frequency usage / COS-SIN order: PASS\nInvalid dimensions/state / gate rejection: PASS\nRESULT: PASS\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
