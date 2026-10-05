#pragma once
#include <sapi.h>
#include <sapiddk.h>
#include <sperror.h>
#include <limits>
#include <stdexcept>
#include <string>
#include <unordered_set>

namespace outloud::sapi {
struct Failure { HRESULT code; };
inline WAVEFORMATEX native_format() noexcept { return {WAVE_FORMAT_PCM,1,24000,48000,2,16,0}; }
inline bool valid_format(const WAVEFORMATEX* f) noexcept {
    return f && f->wFormatTag==WAVE_FORMAT_PCM && f->nChannels==1 && f->nSamplesPerSec==24000 &&
        f->nAvgBytesPerSec==48000 && f->nBlockAlign==2 && f->wBitsPerSample==16 && f->cbSize==0;
}
inline std::string utf8(std::wstring_view text) {
    if (text.empty()) { return {}; }
    if (text.size() > static_cast<std::size_t>(INT_MAX)) { throw Failure{E_INVALIDARG}; }
    const auto size = static_cast<int>(text.size());
    const auto count = WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,text.data(),size,nullptr,0,nullptr,nullptr);
    if (!count) { throw Failure{HRESULT_FROM_WIN32(GetLastError())}; }
    std::string result(static_cast<std::size_t>(count),'\0');
    if (WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,text.data(),size,result.data(),count,nullptr,nullptr) != count) {
        throw Failure{HRESULT_FROM_WIN32(GetLastError())};
    }
    return result;
}
inline std::string collect_text(const SPVTEXTFRAG* fragments) {
    std::wstring text;
    std::unordered_set<const SPVTEXTFRAG*> visited;
    for (auto f = fragments; f; f = f->pNext) {
        if (!visited.insert(f).second) { throw Failure{E_INVALIDARG}; }
        if (f->State.eAction == SPVA_Speak) {
            if (f->ulTextLen && !f->pTextStart) { throw Failure{E_POINTER}; }
            if (f->ulTextLen) { text.append(f->pTextStart,f->ulTextLen); }
        } else if (f->State.eAction == SPVA_Bookmark || f->State.eAction == SPVA_Section ||
                   f->State.eAction == SPVA_ParseUnknownTag ||
                   (f->State.eAction == SPVA_Silence && !f->State.SilenceMSecs)) {
            // Chatterbox metadata policy: never speak tag/bookmark contents.
        } else { throw Failure{E_NOTIMPL}; }
    }
    // Convert once after concatenation, so a surrogate pair can span fragments.
    return utf8(text);
}
}
