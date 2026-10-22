#pragma once
#include <sapi.h>
#include <sapiddk.h>
#include <sperror.h>
#include <algorithm>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
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
struct SpeechVolumeRun {
    std::size_t offset, length;
    ULONG volume;
};
struct SpeechRequest {
    std::string text;
    std::vector<std::wstring> bookmarks;
    bool cancelled=false;
    std::vector<SpeechVolumeRun> volume_runs;
};
inline SpeechRequest collect_request(const SPVTEXTFRAG* fragments,CollectionControl control={}) {
    SpeechRequest request;
    std::wstring text;
    std::vector<std::pair<std::size_t,ULONG>> volume_starts;
    std::unordered_set<const SPVTEXTFRAG*> visited;
    std::size_t fragments_seen=0, text_units=0, bookmarks_seen=0, bookmark_units=0;
    for (auto f = fragments; f; f = f->pNext) {
        if (control.aborted()) { return {{},{},true,{}}; }
        collect_add(fragments_seen,1,maximum_fragments);
        if (!visited.insert(f).second) { throw Failure{E_INVALIDARG}; }
        if (f->State.eAction == SPVA_Speak) {
            if (f->ulTextLen && !f->pTextStart) { throw Failure{E_POINTER}; }
            collect_add(text_units,f->ulTextLen,maximum_text_units);
            const auto volume=(std::min)(f->State.Volume,100UL);
            if (f->ulTextLen && (volume_starts.empty() || volume_starts.back().second!=volume)) {
                volume_starts.emplace_back(text.size(),volume);
            }
            for (std::size_t offset=0;offset<f->ulTextLen;) {
                if (control.aborted()) { return {{},{},true,{}}; }
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
                if (control.aborted()) { return {{},{},true,{}}; }
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
    std::vector<WORD> character_types;
    if (volume_starts.size()>1) {
        character_types.resize(text.size());
        if (!GetStringTypeW(CT_CTYPE1,text.data(),static_cast<int>(text.size()),character_types.data())) {
            throw Failure{HRESULT_FROM_WIN32(GetLastError())};
        }
    }
    std::size_t begin=0, bytes=0;
    for (std::size_t i=0;i<volume_starts.size();++i) {
        if (control.aborted()) { return {{},{},true,{}}; }
        auto end=i+1<volume_starts.size() ? volume_starts[i+1].first : text.size();
        // A UTF-16 character cannot have two volumes. If a fragment splits a
        // surrogate pair, keep the complete character at its leading volume.
        if (end && end<text.size() && text[end-1]>=0xd800 && text[end-1]<=0xdbff &&
            text[end]>=0xdc00 && text[end]<=0xdfff) { ++end; }
        if (end<=begin) { continue; }
        const auto length=volume_starts.size()==1 ? request.text.size() : utf8(std::wstring_view(text).substr(begin,end-begin)).size();
        const auto volume=volume_starts[i].second;
        bool whitespace=!character_types.empty();
        for (auto j=begin;whitespace && j<end;++j) {
            whitespace=(character_types[j]&C1_SPACE)!=0 || (text[j]>=0x1c && text[j]<=0x1f);
        }
        // Whitespace has no audible volume. Attach whitespace-only runs to
        // neighboring text, so an XML change around spaces cannot create an
        // empty prompt in the existing text pipeline. Keep every original byte.
        if (whitespace) {
            if (!request.volume_runs.empty()) { request.volume_runs.back().length+=length; }
        } else if (request.volume_runs.empty()) {
            request.volume_runs.push_back({0,bytes+length,volume});
        } else if (request.volume_runs.back().volume==volume) {
            request.volume_runs.back().length+=length;
        } else { request.volume_runs.push_back({bytes,length,volume}); }
        begin=end; bytes+=length;
    }
    if (!request.text.empty() && request.volume_runs.empty()) {
        request.volume_runs.push_back({0,request.text.size(),volume_starts.front().second});
    }
    return request;
}
template<class Control,class Render>
HRESULT render_volume_runs(const SpeechRequest& request,Control& control,Render&& render) {
    for (const auto& run : request.volume_runs) {
        if (control.poll()) { return S_OK; }
        control.fragment_volume=run.volume;
        // Each render drains its Sonic tail before changing the fragment factor.
        // Equal-volume fragments remain one utterance, preserving the old path.
        const auto hr=render(std::string_view(request.text).substr(run.offset,run.length));
        if (hr!=S_OK || control.aborted) { return hr; }
    }
    return S_OK;
}
}
