#pragma once
#include <cstdint>
#include <string>
#include <stdexcept>
namespace outloud::installer {
inline std::wstring acquisition_action(const std::wstring& component,const std::wstring& status) {
    if(component!=L"vc" && component!=L"dotnet") throw std::runtime_error("Unknown runtime component.");
    if(status==L"compatible") return L"skip";
    if(status==L"missing" || status==L"version_too_old" || status==L"incorrect_architecture" ||
        (component==L"dotnet" && status==L"incompatible_version")) return L"install";
    return L"block"; // Never repair uncertain metadata or downgrade an unknown VC family.
}
inline std::wstring install_decision(std::uint32_t code,const std::wstring& post_status) {
    if(code==1602) return L"cancelled";
    if(code==3010 || code==1641) return L"restart_required";
    if((code==0 || code==1638) && post_status==L"compatible") return L"verified";
    return L"failed"; // Exit zero alone, or an unrecognized error, cannot authorize progress.
}
struct PackageFacts { bool size{},hash{},signature{},publisher{},architecture{},version{}; };
inline std::wstring package_decision(PackageFacts f) {
    if(!f.size) return L"invalid_size";
    if(!f.hash) return L"invalid_hash";
    if(!f.signature) return L"invalid_signature";
    if(!f.publisher) return L"unexpected_publisher";
    if(!f.architecture) return L"incorrect_architecture";
    if(!f.version) return L"unexpected_version";
    return L"verified";
}
// Shared orchestration policy. Production Inno supplies operations; tests supply
// side-effect-free doubles. Detection statuses are the existing A03.3 contract.
template<class Operations> std::wstring ensure_runtime(const std::wstring& component,Operations& op) {
    auto status=op.detect();
    const auto action=acquisition_action(component,status);
    if(action==L"skip") return L"verified";
    if(action==L"block") return L"failed";
    if(!op.consent()) return L"cancelled";
    if(!op.acquire()) return L"acquisition_failed";
    if(package_decision(op.verify())!=L"verified") return L"verification_failed";
    // Recheck after download: another installer may already have updated the runtime.
    status=op.detect();
    if(status==L"compatible") return L"verified";
    if(acquisition_action(component,status)!=L"install") return L"failed";
    const auto code=op.execute();
    status=op.detect(); // Mandatory, including nonzero exit and reboot-required outcomes.
    return install_decision(code,status);
}
}
