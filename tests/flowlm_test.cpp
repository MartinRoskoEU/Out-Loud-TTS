#include "flowlm_fixture_checks.hpp"

#include <iostream>
#include <limits>

namespace {
using outloud::test::neural_require;
template<class Function> void rejected(Function function, std::string_view reason) {
    try { function(); }
    catch (const std::exception& error) {
        neural_require(std::string_view(error.what()).find(reason) != std::string_view::npos,
                       "Rejected for unexpected reason"); return;
    }
    throw std::runtime_error("Invalid compute input/state accepted");
}
void primitive_edges(outloud::cpu::KernelPath path) {
    outloud::cpu::Kernels kernels(path);
    std::array<float, 9> a{1,2,3,4,5,6,7,8,9}, b{9,8,7,6,5,4,3,2,1};
    neural_require(kernels.dot(a, b) == 165.0f, "FP32 dot tail/reduction incorrect");
    rejected([&] { (void)kernels.dot(a, std::span(b).first(8)); }, "dimension");
    rejected([] { outloud::cpu::Kernels invalid(static_cast<outloud::cpu::KernelPath>(99)); }, "path");
    std::vector<float> q(2 * 1024), suffix_k(q.size()), suffix_v(q.size()), output(q.size());
    std::vector<float> prefix_k(1024), prefix_v(1024, 2.0f), scratch(3);
    std::fill_n(suffix_v.begin(), 1024, 4.0f);
    std::fill(suffix_v.begin() + 1024, suffix_v.end(), 6.0f);
    const outloud::cpu::AttentionCacheView cache{prefix_k, prefix_v, suffix_k, suffix_v, 1, 0};
    outloud::cpu::causal_attention(kernels, q, 2, cache, output, scratch);
    for (std::size_t i = 0; i < 1024; ++i) {
        neural_require(output[i] == 3.0f && output[1024 + i] == 4.0f,
                       "Attention did not include prefix/current or exclude future");
    }
    const auto first = output;
    std::fill(suffix_k.begin() + 1024, suffix_k.end(), 10000.0f);
    std::fill(suffix_v.begin() + 1024, suffix_v.end(), 1e10f);
    outloud::cpu::causal_attention(kernels, q, 2, cache, output, scratch);
    neural_require(std::equal(output.begin(), output.begin() + 1024, first.begin()) && output[1024] > 1e9f,
                   "Future K/V altered an earlier query");
    // No finite context cutoff: a prefix value at absolute key zero remains visible.
    neural_require(outloud::cpu::causal_visible(131, 0) && outloud::cpu::causal_visible(112, 112) &&
                   !outloud::cpu::causal_visible(112, 113), "Wrong absolute causal positions");
    std::vector<float> identity(1024);
    for (std::size_t i = 0; i < identity.size(); ++i) { identity[i] = static_cast<float>(i) / 1024; }
    auto rotated_q = identity, rotated_k = identity;
    outloud::cpu::rope(rotated_q, rotated_k, 1, 0);
    neural_require(rotated_q == identity && rotated_k == identity, "RoPE position zero must be identity");
    outloud::cpu::rope(rotated_q, rotated_k, 1, 112);
    neural_require(rotated_q != identity && rotated_q == rotated_k, "RoPE absolute offset ignored");
    rejected([&] { outloud::cpu::rope(rotated_q, rotated_k, 2, 112); }, "dimensions");
    rejected([&] { outloud::cpu::rope(rotated_q, rotated_k, 1, UINT32_MAX); }, "positions");
    rejected([&] { outloud::cpu::causal_attention(kernels, q, 2, cache, output, std::span(scratch).first(1)); }, "dimensions");
    rejected([&] { outloud::cpu::residual_add(a, std::span(b).first(8)); }, "dimension");
    std::array<std::uint16_t, 8> norm_weight{};
    rejected([&] { outloud::cpu::layer_norm({a, 1, 9}, norm_weight, norm_weight, {b, 1, 9}); }, "dimensions");
    std::array<float, 3> gelu{-10.0f, 0.0f, 10.0f};
    outloud::cpu::gelu_tanh(gelu);
    neural_require(gelu[0] == 0 && gelu[1] == 0 && gelu[2] == 10, "GELU saturation/zero incorrect");
}

void invalid_states(const outloud::PocketModelAsset& model, const outloud::OutLoudVoice& voice,
                    const std::filesystem::path& assets) {
    outloud::FlowLMBackbone backbone(model, voice);
    auto state = backbone.make_state(20);
    rejected([&] { (void)backbone.make_state(0); }, "capacity");
    rejected([&] { (void)backbone.make_state(4097); }, "capacity");
    rejected([&] { (void)state.layer(6); }, "layer");
    const std::array<std::uint32_t, 1> bad_token{4001};
    rejected([&] { (void)backbone.embed(bad_token); }, "token ID");
    rejected([&] { (void)backbone.embed({}); }, "token count");
    std::array<float, 1023> wrong_size{};
    rejected([&] { (void)backbone.backbone(wrong_size, state); }, "dimensions");
    std::array<float, 1024> nonfinite{}; nonfinite[0] = std::numeric_limits<float>::quiet_NaN();
    rejected([&] { (void)backbone.backbone(nonfinite, state); }, "Nonfinite");
    const auto embeddings = backbone.embed(outloud::test::neural_tokens);
    auto too_small = backbone.make_state(19);
    rejected([&] { (void)backbone.backbone(embeddings, too_small); }, "capacity");
    neural_require(too_small.offset() == 112, "Rejected input mutated state");
    const outloud::FlowLMObserver fail = [](std::string_view name, auto, auto) {
        if (name == "layer0.k_suffix") { throw std::runtime_error("Intentional observer interruption"); }
    };
    rejected([&] { (void)backbone.prefill(outloud::test::neural_tokens, state, fail); }, "observer interruption");
    rejected([&] { (void)state.offset(); }, "reset required");
    state.reset();
    const auto result = backbone.prefill(outloud::test::neural_tokens, state);
    neural_require(state.offset() == 132 && result.last_hidden().size() == 1024, "Reset did not restore voice state");
    rejected([&] { (void)backbone.prefill(outloud::test::neural_tokens, state); }, "fresh voice state");
    rejected([&] { (void)backbone.backbone(result.last_hidden(), state); }, "capacity");
    outloud::cpu::Kernels kernels;
    const auto w = model.tensor(outloud::PocketTensorId::flow_lm_input_linear_weight);
    std::array<float, 31> input{}; std::array<float, 1024> output{};
    rejected([&] { kernels.linear({input, 1, 31}, w, {output, 1, 1024}); }, "dimensions");
    if (std::filesystem::exists(assets / "undead_male.olv")) {
        const outloud::OutLoudVoice other_voice(assets / "undead_male.olv");
        const outloud::FlowLMBackbone other(model, other_voice);
        auto other_state = other.make_state(20);
        rejected([&] { (void)backbone.backbone(embeddings, other_state); }, "state mismatch");
    }
}
void reusable_prefill(const outloud::PocketModelAsset& model,const outloud::OutLoudVoice& voice,outloud::cpu::KernelPath path) {
    outloud::FlowLMBackbone backbone(model,voice,path);
    auto state=backbone.make_state(20); outloud::FlowLMResult reused;
    std::size_t capacity=0;
    for (const auto count:{20U,1U,7U,20U}) {
        const auto tokens=std::span(outloud::test::neural_tokens).first(count);
        state.reset(); const auto expected=backbone.prefill(tokens,state);
        state.reset(); backbone.prefill_into(tokens,state,reused);
        neural_require(outloud::test::same_float_bits(expected.normalized,reused.normalized) &&
            std::bit_cast<std::uint32_t>(expected.eos_logit)==std::bit_cast<std::uint32_t>(reused.eos_logit) &&
            state.offset()==voice.prefix_length()+count,"Reusable prefill contents/offset differ");
        if (!capacity) { capacity=reused.normalized.capacity(); }
        neural_require(reused.normalized.capacity()==capacity,"Shorter prefill discarded retained output capacity");
    }
    state.reset();
    rejected([&] { backbone.prefill_into(outloud::test::neural_tokens,state,reused,[](auto name,auto,auto) {
        if (name=="layer0.k_suffix") { throw std::runtime_error("Reusable observer failure"); }
    }); },"observer failure");
    state.reset(); backbone.prefill_into(outloud::test::neural_tokens,state,reused);
    neural_require(reused.normalized.capacity()==capacity && state.offset()==voice.prefix_length()+20,"Reusable prefill did not recover");
    std::cout << outloud::cpu::path_name(path) << ": prefill_into lengths 20/1/7/20, owned-result equality and failure/reset PASS\n";
}
} // namespace

int wmain(int argc, wchar_t** argv) {
    try {
        if (argc != 4) { throw std::invalid_argument("Usage: outloud-flowlm-tests assets tokenizer neural-fixtures"); }
        const std::filesystem::path assets(argv[1]);
        const outloud::PocketModelAsset model(assets / "pocket-english.olp");
        const outloud::OutLoudVoice voice(assets / "gnome_male.olv");
        const outloud::Tokenizer tokenizer(argv[2]);
        const outloud::test::NeuralFixture fixture(argv[3]);
        primitive_edges(outloud::cpu::KernelPath::portable);
        auto portable_primitives = outloud::test::check_real_primitives(model, voice, fixture, outloud::cpu::KernelPath::portable);
        outloud::test::enforce_errors(portable_primitives, true);
        auto portable = outloud::test::run_prefill_checks(model, voice, tokenizer, fixture, outloud::cpu::KernelPath::portable);
        portable.primitives = std::move(portable_primitives);
        outloud::test::enforce_report(portable);
        invalid_states(model, voice, assets);
        reusable_prefill(model,voice,outloud::cpu::KernelPath::portable);
        const outloud::cpu::Kernels automatic(outloud::cpu::KernelPath::automatic);
        neural_require(automatic.selected_path() == (outloud::cpu::optimized_available() ? outloud::cpu::KernelPath::avx2_fma
                                                                                      : outloud::cpu::KernelPath::portable),
                       "Automatic feature dispatch incorrect");
        if (outloud::cpu::optimized_available()) {
            primitive_edges(outloud::cpu::KernelPath::avx2_fma);
            auto optimized_primitives = outloud::test::check_real_primitives(model, voice, fixture, outloud::cpu::KernelPath::avx2_fma);
            outloud::test::enforce_errors(optimized_primitives, true);
            auto optimized = outloud::test::run_prefill_checks(model, voice, tokenizer, fixture, outloud::cpu::KernelPath::avx2_fma);
            optimized.primitives = std::move(optimized_primitives);
            outloud::test::enforce_report(optimized);
            outloud::test::enforce_path_agreement(outloud::test::compare_paths(portable, optimized));
            reusable_prefill(model,voice,outloud::cpu::KernelPath::avx2_fma);
            std::cout << "AVX2/FMA and portable agreement: PASS\n";
        } else {
            rejected([] { outloud::cpu::Kernels unavailable(outloud::cpu::KernelPath::avx2_fma); }, "unavailable");
            std::cout << "AVX2/FMA: NOT AVAILABLE; portable fallback: PASS\n";
        }
        std::cout << "Exact embedding / immutable prefix / appended post-RoPE K/V / offset 132: PASS\n"
                     "62 isolated primitives / 90 backbone checkpoints: PASS\n"
                     "Causal visibility / invalid dimensions / state rejection: PASS\nRESULT: PASS\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
