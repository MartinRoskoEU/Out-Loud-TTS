#pragma once
#include <Windows.h>
#include <TlHelp32.h>
#include <psapi.h>
#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <set>
#include <stdexcept>
#include <vector>

// Read-only evidence from this checker process, without production hooks or COM
// diagnostics: the two worker IDs and model/voice allocation bases stay fixed.
struct Residency {
    std::set<DWORD> threads;
    std::set<std::uintptr_t> assets;
    static Residency capture(const std::filesystem::path& model,const std::filesystem::path& voice,bool select_workers = false) {
        Residency r;
        std::vector<std::pair<std::uint64_t,DWORD>> compute_threads;
        const auto snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD,0);
        if (snapshot==INVALID_HANDLE_VALUE) { throw std::runtime_error("Thread snapshot failed"); }
        THREADENTRY32 entry{}; entry.dwSize = sizeof(entry);
        if (Thread32First(snapshot,&entry)) {
            do {
                if (entry.th32OwnerProcessID!=GetCurrentProcessId()) { continue; }
                const auto thread = OpenThread(THREAD_QUERY_LIMITED_INFORMATION|SYNCHRONIZE,FALSE,entry.th32ThreadID);
                if (!thread) { continue; }
                // Toolhelp can briefly retain an exited coordinator record after
                // join. Count only live threads, never those signalled complete.
                if (WaitForSingleObject(thread,0)==WAIT_TIMEOUT) {
                    r.threads.insert(entry.th32ThreadID);
                    FILETIME created{}, ended{}, kernel{}, user{};
                    if (entry.th32ThreadID!=GetCurrentThreadId() && GetThreadTimes(thread,&created,&ended,&kernel,&user)) {
                        const auto ticks = [](FILETIME t) { return (std::uint64_t(t.dwHighDateTime)<<32)|t.dwLowDateTime; };
                        compute_threads.emplace_back(ticks(kernel)+ticks(user),entry.th32ThreadID);
                    }
                }
                CloseHandle(thread);
            }
            while (Thread32Next(snapshot,&entry));
        }
        CloseHandle(snapshot);
        if (select_workers) {
            // This isolated host has exactly two compute workers. After two full
            // syntheses their CPU totals dominate non-caller loader/service
            // threads. Preserve THESE IDs, rather than unrelated process threads.
            std::sort(compute_threads.begin(),compute_threads.end(),std::greater<>{});
            if (compute_threads.size()<2 || !compute_threads[1].first) { throw std::runtime_error("Compute worker identities unavailable"); }
            r.threads = {compute_threads[0].second,compute_threads[1].second};
        }
        for (std::uintptr_t address = 0; ;) {
            MEMORY_BASIC_INFORMATION memory{};
            if (!VirtualQuery(reinterpret_cast<const void*>(address),&memory,sizeof(memory))) { break; }
            if (memory.State==MEM_COMMIT && memory.Type==MEM_MAPPED) {
                wchar_t name[32768];
                const auto count = K32GetMappedFileNameW(GetCurrentProcess(),memory.BaseAddress,name,32768);
                if (count) {
                    const auto file = std::filesystem::path(std::wstring_view(name,count)).filename();
                    if (file==model.filename() || file==voice.filename()) {
                        r.assets.insert(reinterpret_cast<std::uintptr_t>(memory.AllocationBase));
                    }
                }
            }
            const auto next = reinterpret_cast<std::uintptr_t>(memory.BaseAddress)+memory.RegionSize;
            if (next<=address) { break; } address = next;
        }
        if (r.assets.size()!=2 || r.threads.size()<(select_workers ? 2u : 3u)) { throw std::runtime_error("Resident mappings/workers missing"); }
        return r;
    }
    bool operator==(const Residency&) const = default;
    bool retains(const Residency& previous) const {
        return assets==previous.assets && std::includes(threads.begin(),threads.end(),previous.threads.begin(),previous.threads.end());
    }
};
