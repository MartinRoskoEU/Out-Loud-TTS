#pragma once
#include "outloud/sapi_voices.hpp"
#include "voice_library_fs.hpp"

namespace outloud::sapi {
inline constexpr const wchar_t* runtime_fields[]{L"AssetRoot",L"ModelPath",L"TokenizerPath"};
// Only lexical spellings of absolute, unambiguous local paths are equivalent.
// No filesystem canonicalization, short-name/hard-link/reparse alias resolution
// or parent traversal. Mutation preflight still pins runtime files/ancestors.
inline bool same_runtime_path(const SapiRegistryValue& a,const SapiRegistryValue& b) {
    const auto left=sapi_registry_text(a), right=sapi_registry_text(b);
    if (!left || !right) { return false; }
    try {
        const std::filesystem::path x(*left), y(*right);
        if (!x.is_absolute() || !y.is_absolute()) { return false; }
        const auto xn=detail::library_fs::local(x,VoiceLibraryErrorCode::unsafe_path).native();
        const auto yn=detail::library_fs::local(y,VoiceLibraryErrorCode::unsafe_path).native();
        return CompareStringOrdinal(xn.c_str(),-1,yn.c_str(),-1,TRUE)==CSTR_EQUAL;
    } catch (const VoiceLibraryError&) { return false; }
      catch (const std::invalid_argument&) { return false; }
}
inline bool same_token(SapiTokenTree a,SapiTokenTree b) {
    for (const auto* field:runtime_fields) {
        const auto x=a.values.find(field), y=b.values.find(field);
        if (x==a.values.end() || y==b.values.end() || !same_runtime_path(x->second,y->second)) { return false; }
        a.values.erase(x); b.values.erase(y);
    }
    return a==b; // Every unrelated field/type/subkey still matches exactly.
}
inline void retain_runtime_spelling(SapiTokenTree& target,const SapiTokenTree& stored) {
    for (const auto* field:runtime_fields) { target.values.at(field)=stored.values.at(field); }
}
}
