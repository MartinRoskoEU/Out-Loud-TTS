#include "outloud/perf_diagnostics.hpp"
#include "mimi_fixture_checks.hpp"
#include <Windows.h>
#include <cstdlib>
#include <iostream>
#include <new>

namespace { bool audit = false; std::uint64_t allocations = 0; }
void* operator new(std::size_t size) {
    if (void* p = std::malloc(size == 0 ? 1 : size)) { if (audit) { ++allocations; } return p; }
    throw std::bad_alloc();
}
void* operator new[](std::size_t size) { return ::operator new(size); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { std::free(p); }

#include "resident.hpp"

int wmain(int argc, wchar_t** argv) {
    try {
        if (argc > 4) { throw std::invalid_argument("Usage: outloud-perf[-profile] [portable|avx2] [2|3|5] [report-tag]"); }
        const bool avx = argc >= 2 && std::wstring_view(argv[1]) == L"avx2";
        if (argc >= 2 && !avx && std::wstring_view(argv[1]) != L"portable") { throw std::invalid_argument("Unknown kernel path"); }
        std::size_t repetitions = 5;
        if (argc >= 3) {
            const std::wstring_view count(argv[2]);
            if (count == L"2") { repetitions = 2; }
            else if (count == L"3") { repetitions = 3; }
            else if (count != L"5") { throw std::invalid_argument("Timed repetitions must be 2, 3 or 5"); }
        }
        const std::wstring tag = argc == 4 ? argv[3] : L"";
        if (tag.find_first_not_of(L"abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_") != std::wstring::npos) {
            throw std::invalid_argument("Report tag must contain only ASCII letters, digits, hyphen or underscore");
        }
        const std::wstring prefix = tag.empty() ? L"" : tag + L"-";
        const auto path = avx ? outloud::cpu::KernelPath::avx2_fma : outloud::cpu::KernelPath::portable;
        const std::filesystem::path assets = OUTLOUD_ASSET_DIRECTORY, neural = OUTLOUD_NEURAL_DIRECTORY;
        const outloud::PocketModelAsset model(assets / "pocket-english.olp"); const outloud::OutLoudVoice voice(assets / "gnome_male.olv");
        const outloud::test::GenerationFixture fixture(neural);
        Resident context(model, voice, path, fixture.values("noise.stream"));
        (void)context.run(); // Exactly one untimed warm-up, also verifies workload.
        const auto report_directory = neural.parent_path() / "perf"; std::filesystem::create_directories(report_directory);
#ifdef OUTLOUD_PERF_DIAGNOSTICS
        outloud::perf::counters = {}; outloud::perf::enabled = true;
        const auto run = context.run(); outloud::perf::enabled = false;
        std::ofstream out(report_directory / (prefix + (avx ? L"native-avx2-profile.json" : L"native-portable-profile.json")));
        out << std::setprecision(17) << "{\"profiled_run\":"; serialize(out, run); out << ",\"counters\":[";
        bool first = true;
        for (std::size_t d = 0; d < 4; ++d) {
            for (std::size_t s = 0; s < outloud::perf::names.size(); ++s) {
                const auto& c = outloud::perf::counters[d * outloud::perf::names.size() + s]; if (!c.calls) { continue; }
                if (!first) { out << ','; } first = false;
                out << "{\"domain\":\"" << outloud::perf::domains[d] << "\",\"stage\":\"" << outloud::perf::names[s]
                    << "\",\"calls\":" << c.calls << ",\"inclusive_ms\":" << static_cast<double>(c.inclusive_ns)/1e6
                    << ",\"exclusive_ms\":" << static_cast<double>(c.exclusive_ns)/1e6 << '}';
            }
        }
        out << "]}\n";
        std::cout << "One diagnostic profile complete; timings are instrumented, not benchmark results.\n";
#else
        std::array<Run, 5> runs;
        for (std::size_t i = 0; i < repetitions; ++i) {
            runs[i] = context.run();
            std::cout << outloud::cpu::path_name(path) << ' ' << i+1 << " first/total ms " << runs[i].first << ' ' << runs[i].total << '\n';
        }
        std::ofstream out(report_directory / (prefix + (avx ? L"native-avx2-benchmark.json" : L"native-portable-benchmark.json")));
        out << std::setprecision(17) << "{\"warmups\":1,\"timed_repetitions\":" << repetitions << ",\"no_timed_observers_or_profiler\":true,\"runs\":[";
        for (std::size_t i = 0; i < repetitions; ++i) { if (i) { out << ','; } serialize(out, runs[i]); } out << "]}\n";
#endif
        outloud::test::neural_require(bool(out), "Cannot write performance report");
        return 0;
    } catch (const std::exception& e) { audit = false; std::cerr << e.what() << '\n'; return 1; }
}
