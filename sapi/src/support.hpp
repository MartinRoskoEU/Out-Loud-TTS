#pragma once
#include <sapi.h>
#include <sapiddk.h>
#include <sperror.h>
#include <limits>
#include <stdexcept>
#include <string>
#include <unordered_set>
#include <vector>

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
struct SpeechRequest {
    std::string text;
    std::vector<std::wstring> bookmarks;
};
inline SpeechRequest collect_request(const SPVTEXTFRAG* fragments) {
    SpeechRequest request;
    std::wstring text;
    std::unordered_set<const SPVTEXTFRAG*> visited;
    for (auto f = fragments; f; f = f->pNext) {
        if (!visited.insert(f).second) { throw Failure{E_INVALIDARG}; }
        if (f->State.eAction == SPVA_Speak) {
            if (f->ulTextLen && !f->pTextStart) { throw Failure{E_POINTER}; }
            if (f->ulTextLen) { text.append(f->pTextStart,f->ulTextLen); }
        } else if (f->State.eAction == SPVA_Bookmark) {
            // Unsupported non-leading bookmarks remain metadata, as before
            // Step 2O. Rejecting the whole request here suppresses valid speech
            // when a host appends a bookmark. Do not invent an audio offset.
            if (!text.empty()) { continue; }
            if (f->ulTextLen && !f->pTextStart) { throw Failure{E_POINTER}; }
            std::wstring name;
            if (f->ulTextLen) { name.assign(f->pTextStart,f->ulTextLen); }
            // SPEVENT carries a NUL-terminated string, so embedded NULs cannot
            // round-trip exactly. Fragment data itself need not be terminated.
            if (name.find(L'\0') != std::wstring::npos) { throw Failure{E_INVALIDARG}; }
            request.bookmarks.push_back(std::move(name));
        } else if (f->State.eAction == SPVA_Section ||
                   f->State.eAction == SPVA_ParseUnknownTag ||
                   (f->State.eAction == SPVA_Silence && !f->State.SilenceMSecs)) {
            // Chatterbox metadata policy: never speak tag contents.
        } else { throw Failure{E_NOTIMPL}; }
    }
    // Convert once after concatenation, so a surrogate pair can span fragments.
    request.text = utf8(text);
    return request;
}
}
