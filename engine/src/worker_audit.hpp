#pragma once
// Development executable supplies thread-local allocation counters. Neither
// these counters nor trace timestamps exist in the ordinary runtime library.
#ifdef OUTLOUD_WORKER_AUDIT
#include <cstdint>
namespace outloud::development {
extern thread_local bool audit;
extern thread_local std::uint64_t allocations;
}
#endif
