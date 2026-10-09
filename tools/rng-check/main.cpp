#include "outloud/production_noise.hpp"
#include "asset_io.hpp"
#include "rng_reference.hpp"
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <new>
#include <sstream>
#include <string_view>

namespace { bool audit = false; std::uint64_t allocations = 0; }
void* operator new(std::size_t size) {
    if (void* p = std::malloc(size ? size : 1)) { if (audit) { ++allocations; } return p; }
    throw std::bad_alloc();
}
void* operator new[](std::size_t size) { return ::operator new(size); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { std::free(p); }

namespace {
void require(bool condition, const char* text) { if (!condition) { throw std::runtime_error(text); } }
void state_equal(const outloud::ProductionNoiseSource& rng, std::span<const std::uint8_t> bytes) {
    auto state = rng.engine_state();
    std::uint64_t seed; std::uint32_t left, position;
    std::memcpy(&seed, bytes.data(), 8); std::memcpy(&left, bytes.data()+8, 4); std::memcpy(&position, bytes.data()+12, 4);
    require(seed == state.seed && left == state.left && position == state.position &&
        std::memcmp(state.words.data(), bytes.data()+16, 624*4) == 0, "MT19937 engine progression differs");
}
}
int wmain(int argc, wchar_t** argv) {
    try {
        const bool check_only = argc == 2 && std::wstring_view(argv[1]) == L"--check-only";
        require(argc == 1 || check_only, "Usage: outloud-rng-check [--check-only]");
        const std::filesystem::path directory = OUTLOUD_RNG_DIRECTORY;
        outloud::detail::MappedFile fixture(directory / "rng-reference.bin", outloud::test::rng_fixture_bytes);
        const auto bytes = fixture.bytes();
        require(bytes.size() == outloud::test::rng_fixture_bytes &&
            outloud::detail::hex(outloud::detail::sha256(bytes)) == outloud::test::rng_fixture_sha, "RNG reference identity differs");
        outloud::ProductionNoiseSource rng(73);
        std::size_t checked = 0;
        for (const auto& c : outloud::test::rng_cases) {
            rng.reseed(c.seed); state_equal(rng, bytes.subspan(c.state_offset, 2512));
            allocations = 0; audit = true;
            for (std::uint32_t i = 0; i < c.draws; ++i) {
                const auto row = rng.next();
                const auto expected = reinterpret_cast<const float*>(bytes.data() + c.noise_offset + std::size_t(i)*128);
                if (std::memcmp(row.data(), expected, 128)) {
                    audit = false;
                    for (std::size_t j = 0; j < 32; ++j) {
                        if (std::memcmp(&row[j], &expected[j], 4)) {
                            std::uint32_t actual_bits, expected_bits;
                            std::memcpy(&actual_bits, &row[j], 4); std::memcpy(&expected_bits, &expected[j], 4);
                            std::ostringstream message;
                            message << "Normal bits differ: seed " << c.seed << " draw " << i << " element " << j
                                    << " native 0x" << std::hex << actual_bits << " torch 0x" << expected_bits;
                            throw std::runtime_error(message.str());
                        }
                    }
                }
                require(row.size() == 32 && rng.consumed() == i+1, "RNG draw count/shape differs");
                for (float value : row) { require(std::isfinite(value), "Nonfinite RNG value"); }
                state_equal(rng, bytes.subspan(c.state_offset + std::size_t(i+1)*2512, 2512));
                ++checked;
            }
            audit = false; require(allocations == 0, "RNG draw allocated");
        }
        // Explicit reseed validates all canonical draws again.
        rng.reseed(73); allocations = 0; audit = true;
        for (std::size_t i = 0; i < 48; ++i) {
            const auto row = rng.next();
            require(std::memcmp(row.data(), bytes.data()+i*128, 128) == 0, "Reseed changed canonical bits");
        }
        audit = false; require(allocations == 0, "Reseed draws allocated");
        rng.reseed(73);
        const auto first = rng.next(); std::array<float, 32> saved;
        std::copy(first.begin(), first.end(), saved.begin()); rng.reseed(74);
        require(std::memcmp(rng.next().data(), saved.data(), 128) != 0, "Different seed failed to change output");
        if (check_only) {
            std::cout << "PASS: exact PyTorch noise bits, engine states, reseed and zero draw allocations; "
                      << checked << " draws. Timing deliberately not run.\n";
            return 0;
        }
        // A single 48-draw timing: small diagnostic, no statistical campaign.
        rng.reseed(73); allocations = 0; audit = true;
        const auto start = std::chrono::steady_clock::now();
        for (unsigned i = 0; i < 48; ++i) { (void)rng.next(); }
        const auto end = std::chrono::steady_clock::now(); audit = false;
        require(rng.consumed() == 48 && allocations == 0, "Timed RNG draw count/allocation differs");
        const auto cost = std::chrono::duration<double, std::micro>(end-start).count();
        const auto report_name = outloud::cpu::optimized_available() ? "native-rng-check-avx2-enabled.json" : "native-rng-check-portable.json";
        std::ofstream report(directory / report_name);
        report << std::setprecision(17) << "{\"exact_pytorch_bits\":true,\"checked_draws\":" << checked
               << ",\"engine_states_exact\":true,\"reseed_exact\":true,\"per_draw_allocations\":0,\"state_bytes\":" << sizeof(rng)
               << ",\"rng_48_draws_us\":" << cost << ",\"rng_us_per_draw\":" << cost/48 << "}\n";
        require(bool(report), "Cannot write RNG report");
        std::cout << "EXACT PYTORCH RNG PARITY ACHIEVED: " << checked << " draws and all engine states; reseed exact; zero draw allocations.\n"
                  << "State bytes " << sizeof(rng) << "; 48 draws " << cost << " us; per draw " << cost/48 << " us.\n";
        return 0;
    } catch (const std::exception& e) { audit = false; std::cerr << e.what() << '\n'; return 1; }
}
