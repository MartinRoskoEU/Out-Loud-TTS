#pragma once
#include <sapi.h>
#include <sapiddk.h>
#include <sperror.h>
#include <algorithm>
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
// UTF-16 units: comfortably above the UI limit, bounded even for other hosts.
inline constexpr std::size_t maximum_text_units=262144, maximum_fragments=8192;
inline constexpr std::size_t maximum_bookmarks=1024, maximum_bookmark_units=65536;
inline void collect_add(std::size_t& total,std::size_t amount,std::size_t limit) {
    if (total>limit || amount>limit-total) { throw Failure{HRESULT_FROM_WIN32(ERROR_BUFFER_OVERFLOW)}; }
    total+=amount;
}
struct CollectionControl {
    bool (*poll)(void*)=nullptr;
    void* context=nullptr;
    bool aborted() const { return poll && poll(context); }
};
struct SpeechRequest {
    std::string text;
    std::vector<std::wstring> bookmarks;
    bool cancelled=false;
};
inline SpeechRequest collect_request(const SPVTEXTFRAG* fragments,CollectionControl control={}) {
    SpeechRequest request;
    std::wstring text;
    std::unordered_set<const SPVTEXTFRAG*> visited;
    std::size_t fragments_seen=0, text_units=0, bookmarks_seen=0, bookmark_units=0;
    for (auto f = fragments; f; f = f->pNext) {
        if (control.aborted()) { return {{},{},true}; }
        collect_add(fragments_seen,1,maximum_fragments);
        if (!visited.insert(f).second) { throw Failure{E_INVALIDARG}; }
        if (f->State.eAction == SPVA_Speak) {
            if (f->ulTextLen && !f->pTextStart) { throw Failure{E_POINTER}; }
            collect_add(text_units,f->ulTextLen,maximum_text_units);
            for (std::size_t offset=0;offset<f->ulTextLen;) {
                if (control.aborted()) { return {{},{},true}; }
                const auto count=(std::min)(std::size_t{4096},static_cast<std::size_t>(f->ulTextLen)-offset);
                text.append(f->pTextStart+offset,count); offset+=count;
            }
        } else if (f->State.eAction == SPVA_Bookmark) {
            collect_add(bookmarks_seen,1,maximum_bookmarks);
            collect_add(bookmark_units,f->ulTextLen,maximum_bookmark_units);
            // Unsupported non-leading bookmarks remain metadata, as before
            // Step 2O. Rejecting the whole request here suppresses valid speech
            // when a host appends a bookmark. Do not invent an audio offset.
            if (!text.empty()) { continue; }
            if (f->ulTextLen && !f->pTextStart) { throw Failure{E_POINTER}; }
            std::wstring name;
            for (std::size_t offset=0;offset<f->ulTextLen;) {
                if (control.aborted()) { return {{},{},true}; }
                const auto count=(std::min)(std::size_t{4096},static_cast<std::size_t>(f->ulTextLen)-offset);
                name.append(f->pTextStart+offset,count); offset+=count;
            }
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
