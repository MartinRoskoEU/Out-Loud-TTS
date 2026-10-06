#pragma once
// Test checker only; production has no probe or interruption injection.
#ifdef OUTLOUD_REPLACEMENT_AUDIT
namespace outloud::detail::replacement_audit {
inline void (*probe)(unsigned, bool) = nullptr;
inline bool leave_pending = false;
inline void point(unsigned step, bool rollback = false) { if (probe) { probe(step,rollback); } }
}
#endif
