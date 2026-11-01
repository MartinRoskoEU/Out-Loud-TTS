#pragma once
// Existing isolated checker compilation only; Release production has no state.
namespace outloud::detail::refresh_audit {
enum class Event { metadata, replacement_scan, removal_scan, replacement_journal, removal_journal };
#ifdef OUTLOUD_REPLACEMENT_AUDIT
struct Counts {
    unsigned metadata=0, replacement_scan=0, removal_scan=0,
        replacement_journal=0, removal_journal=0;
};
inline Counts counts;
inline bool legacy_enumeration=false;
inline void event(Event value) noexcept {
    switch (value) {
    case Event::metadata: ++counts.metadata; break;
    case Event::replacement_scan: ++counts.replacement_scan; break;
    case Event::removal_scan: ++counts.removal_scan; break;
    case Event::replacement_journal: ++counts.replacement_journal; break;
    case Event::removal_journal: ++counts.removal_journal; break;
    }
}
#else
inline void event(Event) noexcept {}
#endif
} // namespace outloud::detail::refresh_audit
