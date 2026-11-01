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
// Inno writes a quoted executable, optionally followed by exactly " /LOG"
// when UninstallLogging=yes. This is validation only; never execute this string.
inline std::filesystem::path installation_uninstall_executable(const std::wstring& command,
        const std::filesystem::path& root,const char* reason) {
    signing_require(command.size()>2 && command.front()==L'"',reason);
    const auto closing=command.find(L'"',1);
    signing_require(closing!=command.npos && closing>1,reason);
    const auto suffix=command.substr(closing+1);
    signing_require(suffix.empty() || suffix==L" /LOG",reason);
    const auto executable=installation_path(command.substr(1,closing-1));
    const auto name=executable.filename().native();
    signing_require(same_installation_path(executable.parent_path(),root) && name.size()==12 &&
        name.starts_with(L"unins") && name.ends_with(L".exe") &&
        name.substr(5,3).find_first_not_of(L"0123456789")==name.npos,reason);
    return executable;
}
}
