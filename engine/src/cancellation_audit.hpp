#pragma once
// Focused deterministic barriers ONLY in the existing native correctness host.
// Compiled out of every production library/DLL and normal observation tool.
#ifdef OUTLOUD_UTTERANCE_CHECK
namespace outloud::development {
enum class CancellationPoint { before_prefill, after_prefill, candidate_complete,
    full_queue, empty_queue, before_decode, pcm_ready, segment_boundary };
void cancellation_point(CancellationPoint point, bool may_block);
}
#define OUTLOUD_CANCEL_POINT(point, may_block) outloud::development::cancellation_point( \
    outloud::development::CancellationPoint::point, may_block)
#else
#define OUTLOUD_CANCEL_POINT(...) do {} while (false)
#endif
