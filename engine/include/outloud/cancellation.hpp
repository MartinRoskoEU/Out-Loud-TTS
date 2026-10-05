#pragma once
#include <stop_token>

namespace outloud {
// Failure remains an exception at the existing native boundary. A cancelled
// result describes only a prefix, never a successfully completed utterance.
enum class SynthesisStatus { completed, cancelled };
}
