#pragma once
#include <array>
#include <compare>
#include <optional>
#include <string>
#include <vector>

namespace outloud::installer {
struct Version {
    std::array<unsigned,4> parts{};
    auto operator<=>(const Version&) const = default;
};
inline std::optional<Version> version(const std::wstring& text) {
    Version result; unsigned part=0; bool digit=false;
    for (const auto c:text) {
        if (c==L'.') { if (!digit || ++part>=4) return {}; digit=false; }
        else if (c>=L'0' && c<=L'9') {
            if (result.parts[part]>6553 || (result.parts[part]==6553 && c>L'5')) return {};
            result.parts[part]=result.parts[part]*10+static_cast<unsigned>(c-L'0'); digit=true;
        } else return {};
    }
    if (!digit || part<2) return {};
    return result;
}
inline std::wstring text(Version v) {
    return std::to_wstring(v.parts[0])+L"."+std::to_wstring(v.parts[1])+L"."+std::to_wstring(v.parts[2])+L"."+std::to_wstring(v.parts[3]);
}
struct Result { std::wstring status, detail, installed; };
struct WindowsFacts { bool failed{}; unsigned major{},minor{},build{},product{},machine{}; };
inline Result evaluate(WindowsFacts f) {
    if (f.failed) return {L"detection_failed",L"Windows identity could not be established.",L""};
    if (f.machine!=0x8664 || f.major!=10 || f.minor!=0 || f.build<22000 || f.product!=1)
        return {L"unsupported",L"Out Loud supports only Windows 11 on native x64 client systems. Windows 10, ARM64, x86 and Server are unsupported.",L""};
    return {L"compatible",L"Windows 11 native x64 client.",std::to_wstring(f.build)};
}
struct VCFacts { bool failed{},other_arch{},files_valid{true}; std::optional<Version> installed; };
inline Result evaluate(VCFacts f,Version minimum) {
    if (f.failed) return {L"detection_failed",L"Cannot reliably read VC runtime installation metadata.",L""};
    if (!f.installed) return {f.other_arch?L"incorrect_architecture":L"missing",L"A registered Microsoft VC v14 x64 runtime is required.",L""};
    if (f.installed->parts[0]!=14) return {L"incompatible_version",L"The registered package is outside the supported VC v14 family.",text(*f.installed)};
    if (*f.installed<minimum) return {L"version_too_old",L"VC x64 runtime is older than the native toolchain's matched redistributable.",text(*f.installed)};
    if (!f.files_valid) return {L"detection_failed",L"Registered VC package has missing, wrong-architecture or outdated System32 runtime files.",text(*f.installed)};
    return {L"compatible",L"Registered VC v14 x64 package and required System32 files verified.",text(*f.installed)};
}
struct DotNetFacts {
    bool failed{},wrong_arch{},host_ok{},files_valid{true},resolution_seen{};
    std::vector<Version> desktop,core;
    std::optional<Version> resolved_desktop,resolved_core;
};
inline Result evaluate(const DotNetFacts& f,Version desktop_min,Version core_min) {
    if (f.failed) return {L"detection_failed",L"Cannot reliably inspect the selected .NET installation.",L""};
    if (f.wrong_arch) return {L"incorrect_architecture",L"The selected .NET installation is not x64.",L""};
    if (f.desktop.empty()) return {L"missing",L"Microsoft.WindowsDesktop.App is missing; Core runtime or SDK alone is insufficient.",L""};
    if (f.core.empty()) return {L"missing",L"Microsoft.NETCore.App is also required by the Desktop application.",L""};
    if (f.host_ok && f.resolved_desktop && f.resolved_core) {
        if (*f.resolved_desktop<desktop_min || *f.resolved_core<core_min || !f.files_valid)
            return {L"detection_failed",L"Resolved .NET framework files or minimum versions are invalid.",L""};
        return {L"compatible",L"hostfxr resolved both frameworks using the application's runtimeconfig and roll-forward policy.",text(*f.resolved_desktop)};
    }
    bool candidate=false,newer=false;
    for (const auto v:f.desktop) {
        candidate|=v.parts[0]==desktop_min.parts[0] && v>=desktop_min;
        newer|=v.parts[0]>desktop_min.parts[0];
    }
    if (!candidate) return {newer?L"incompatible_version":L"version_too_old",L"Installed Desktop versions cannot satisfy this application's framework policy.",L""};
    if (f.resolution_seen) {
        bool core_candidate=false,core_newer=false;
        for (const auto v:f.core) {
            core_candidate|=v.parts[0]==core_min.parts[0] && v>=core_min;
            core_newer|=v.parts[0]>core_min.parts[0];
        }
        return {core_candidate || core_newer?L"incompatible_version":L"version_too_old",L"Installed frameworks do not satisfy the application's requested versions and roll-forward policy.",L""};
    }
    return {L"detection_failed",L"Desktop is present but hostfxr could not resolve a complete compatible framework set.",L""};
}
}
