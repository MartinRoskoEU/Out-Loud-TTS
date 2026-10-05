#include "mimi_fixture_checks.hpp"
#include <cstdlib>
#include <iostream>
#include <limits>
#include <new>

namespace { bool audit = false; std::size_t allocations = 0; }
void* operator new(std::size_t size) {
    if (void* p = std::malloc(size == 0 ? 1 : size)) { if (audit) { ++allocations; } return p; }
    throw std::bad_alloc();
}
void* operator new[](std::size_t size) { return ::operator new(size); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { std::free(p); }

namespace {
using outloud::test::neural_require;
template<class Function> void rejected(Function f, std::string_view reason) {
    try { f(); } catch (const std::exception& e) {
        neural_require(std::string_view(e.what()).find(reason) != std::string_view::npos, "Mimi rejected for unexpected reason"); return;
    }
    throw std::runtime_error("Mimi accepted invalid input/state");
}
void attention_window(outloud::cpu::KernelPath path) {
    outloud::cpu::Kernels kernels(path);
    neural_require(outloud::cpu::mimi_visible(300, 51) && !outloud::cpu::mimi_visible(300, 50) &&
                   outloud::cpu::mimi_visible(300, 300) && !outloud::cpu::mimi_visible(300, 301), "Mimi context off by one");
    std::vector<float> q(8192, 0), k(316 * 512, 0), v(316 * 512), output(8192);
    for (std::size_t p = 0; p < 316; ++p) {
        std::fill_n(v.begin() + p * 512, 512, static_cast<float>(p));
        if (p < 51) {
            std::fill_n(v.begin() + p * 512, 512, std::numeric_limits<float>::quiet_NaN());
            std::fill_n(k.begin() + p * 512, 512, std::numeric_limits<float>::quiet_NaN());
        }
    }
    outloud::cpu::mimi_attention(kernels, q, k, v, 300, output);
    neural_require(std::abs(output[0] - 175.5f) < 0.001f, "Mimi attention read masked old positions");
    const auto first = std::vector<float>(output.begin(), output.begin() + 512);
    std::fill(v.begin() + 301 * 512, v.end(), 10000.0f);
    outloud::cpu::mimi_attention(kernels, q, k, v, 300, output);
    neural_require(outloud::test::same_float_bits(first, std::span(output).first(512)), "Mimi attention read future positions");
    rejected([&] { outloud::cpu::mimi_attention(kernels, std::span(q).first(1), k, v, 300, output); }, "dimensions");
}
void lifecycle(const outloud::PocketModelAsset& model, const outloud::test::MimiFixture& fixture,
               outloud::cpu::KernelPath path) {
    outloud::MimiDecoder decoder(model, path);
    outloud::MimiDecoderState state(109);
    const auto latents = fixture.values("normalized.latents");
    const auto first = decoder.decode_frame(latents.first(32), state);
    std::vector<float> saved(first.begin(), first.end());
    const auto offset = state.offset();
    rejected([&] { (void)decoder.decode_frame(latents.first(31), state); }, "32-D");
    rejected([&] { (void)decoder.decode_frame({}, state); }, "32-D");
    std::array<float, 32> invalid{};
    for (const auto bad : {std::numeric_limits<float>::infinity(), -std::numeric_limits<float>::infinity(), std::numeric_limits<float>::quiet_NaN()}) {
        invalid[17] = bad; rejected([&] { (void)decoder.decode_frame(invalid, state); }, "finite");
    }
    neural_require(state.valid() && state.offset() == offset, "Rejected input mutated Mimi state");
    (void)decoder.decode_frame(latents.subspan(32, 32), state);
    state.reset();
    neural_require(state.offset() == 0 && state.keys(0).empty(), "Mimi reset retained logical KV positions");
    for (std::size_t i = 0; i < 12; ++i) {
        const auto c = state.convolution_state(i);
        neural_require(std::all_of(c.begin(), c.end(), [](float x) { return x == 0; }), "Mimi reset retained carry");
    }
    neural_require(outloud::test::same_float_bits(decoder.decode_frame(latents.first(32), state), saved), "Mimi reset changed PCM bytes");
    state.reset();
    rejected([&] { (void)decoder.decode_frame(latents.first(32), state, [](auto, auto) { throw std::runtime_error("observer"); }); }, "observer");
    neural_require(!state.valid(), "Failed observer left usable partial Mimi state");
    rejected([&] { (void)decoder.decode_frame(latents.first(32), state); }, "reset");
    state.reset();
    bool reentered = false;
    (void)decoder.decode_frame(latents.first(32), state, [&](auto name, auto) {
        if (name != "normalized") { return; }
        reentered = true;
        rejected([&] { state.reset(); }, "during decoding");
        rejected([&] { (void)decoder.decode_frame(latents.first(32), state); }, "Reentrant");
    });
    neural_require(reentered && state.valid(), "Mimi observer reentrancy check failed");
    outloud::MimiDecoderState one(1);
    (void)decoder.decode_frame(latents.first(32), one);
    rejected([&] { (void)decoder.decode_frame(latents.first(32), one); }, "capacity");
    neural_require(one.offset() == 16 && one.valid(), "Capacity rejection invalidated completed state");
    rejected([] { outloud::MimiDecoderState zero(0); }, "capacity");
    rejected([] { outloud::MimiDecoderState huge(4097); }, "capacity");
    state.reset(); allocations = 0; audit = true;
    try {
        for (std::size_t i = 0; i < 46; ++i) { (void)decoder.decode_frame(latents.subspan(i * 32, 32), state); }
    } catch (...) { audit = false; throw; }
    audit = false;
    neural_require(allocations == 0, "Unobserved Mimi loop allocated heap memory");
    neural_require(state.offset() == 736 && state.keys(0).size() == 736 * 512 && state.values(1).size() == 736 * 512,
                   "Mimi linear cache truncated old positions");
    std::cout << outloud::cpu::path_name(path) << ": context/reset/failure/reentrancy/capacity PASS; 46-frame C++ allocations " << allocations << '\n';
}
}
int wmain(int argc, wchar_t** argv) {
    try {
        if (argc != 3) { throw std::invalid_argument("Usage: outloud-mimi-tests ASSETS NEURAL"); }
        const outloud::PocketModelAsset model(std::filesystem::path(argv[1]) / "pocket-english.olp");
        const outloud::test::MimiFixture fixture(argv[2]);
        neural_require(outloud::MimiDecoder(model).selected_path() == outloud::cpu::KernelPath::portable, "Mimi default is not portable");
        for (const auto path : {outloud::cpu::KernelPath::portable, outloud::cpu::KernelPath::avx2_fma}) {
            if (path == outloud::cpu::KernelPath::avx2_fma && !outloud::cpu::optimized_available()) { continue; }
            outloud::test::enforce_mimi_errors(outloud::test::check_mimi_convolutions(model, fixture, path));
            attention_window(path); lifecycle(model, fixture, path);
        }
        rejected([] { outloud::test::SamplerError e{}; e.raw.max_absolute = 1; outloud::test::enforce_mimi_errors({{"pcm.complete", e}}); }, "gate failed");
        std::cout << "Mimi regressions PASS\n"; return 0;
    } catch (const std::exception& e) { audit = false; std::cerr << e.what() << '\n'; return 1; }
}
