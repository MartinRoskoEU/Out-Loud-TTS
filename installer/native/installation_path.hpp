#pragma once
#include <Windows.h>
#include "signing_policy.hpp"
#include <filesystem>
#include <cwctype>
namespace outloud::installer {
// No default directory or product discovery here: Inno supplies the selected app path.
inline std::filesystem::path installation_path(const std::filesystem::path& input) {
    const auto text=input.native();
    signing_require(input.is_absolute() && text.size()>3 && std::iswalpha(text[0]) &&
        text[1]==L':' && (text[2]==L'\\' || text[2]==L'/') && !text.starts_with(L"\\\\") &&
        text.substr(2).find(L':')==std::wstring::npos && text.find_first_of(L"\r\n\"")==std::wstring::npos && text.find(L'\0')==text.npos,"unsafe_installation_path");
    auto result=input.lexically_normal();
    if(!result.has_filename())result=result.parent_path();
    signing_require(result!=result.root_path(),"unsafe_installation_path");
    for(const auto& part:result.relative_path()) {
        const auto name=part.native();
        signing_require(!name.empty() && name.back()!=L'.' && name.back()!=L' ' &&
            name.find_first_of(L"*?<>|")==std::wstring::npos,"unsafe_installation_path");
    }
    return result;
}
inline bool same_installation_path(const std::filesystem::path& left,const std::filesystem::path& right) {
    const auto a=installation_path(left).native(),b=installation_path(right).native();
    return CompareStringOrdinal(a.c_str(),static_cast<int>(a.size()),b.c_str(),static_cast<int>(b.size()),TRUE)==CSTR_EQUAL;
}
}
