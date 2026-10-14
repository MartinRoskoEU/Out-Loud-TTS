#pragma once
#include "signing_policy.hpp"
#include "../../sapi/src/identity.hpp"
#include <map>
#include <vector>
#include <optional>
#include <cstdint>
namespace outloud::installer {
inline constexpr bool registration_lifecycle_ready=false; // No runtime override.
inline constexpr wchar_t registration_owner[]=L"OutLoudPocketSAPI-2M";
inline constexpr wchar_t registration_description[]=L"Out Loud Pocket SAPI Engine";
inline std::wstring registration_path(){return std::wstring(L"SOFTWARE\\Classes\\CLSID\\")+outloud::sapi::clsid_text;}
struct RegistrationValue {
    uint32_t type{}; std::vector<uint8_t> bytes;
    bool operator==(const RegistrationValue&)const=default;
};
struct RegistrationTree {
    std::map<std::wstring,RegistrationValue> values;
    std::map<std::wstring,RegistrationTree> children;
    bool operator==(const RegistrationTree&)const=default;
};
using RegistrationSnapshot=std::optional<RegistrationTree>;
inline RegistrationValue registration_string(const std::wstring& text){
    RegistrationValue value{1,{}}; // REG_SZ, explicit UTF-16LE including terminator.
    for(auto c:text){signing_require(c!=0 && static_cast<uint32_t>(c)<=65535,"registry_string_invalid");
        value.bytes.push_back(static_cast<uint8_t>(c));value.bytes.push_back(static_cast<uint8_t>(c>>8));}
    value.bytes.insert(value.bytes.end(),{0,0});return value;
}
inline std::wstring registration_text(const RegistrationValue& value){
    signing_require(value.type==1 && value.bytes.size()>=2 && value.bytes.size()%2==0 &&
        value.bytes[value.bytes.size()-1]==0 && value.bytes[value.bytes.size()-2]==0,"registration_malformed");
    std::wstring result;
    for(size_t i=0;i+2<value.bytes.size();i+=2){const auto c=static_cast<wchar_t>(value.bytes[i]|(value.bytes[i+1]<<8));
        signing_require(c!=0,"registration_malformed");result+=c;}
    return result;
}
inline std::wstring registration_field(const RegistrationTree& tree,const wchar_t* name){
    const auto found=tree.values.find(name);signing_require(found!=tree.values.end(),"registration_malformed");return registration_text(found->second);
}
inline std::wstring encoded_tree(const RegistrationSnapshot& snapshot){
    if(!snapshot)return L"absent";
    // Exact raw type/data/name capture; no expanded strings or normalized paths.
    std::vector<uint8_t> bytes;
    auto number=[&](uint32_t n){for(unsigned i=0;i<4;++i)bytes.push_back(static_cast<uint8_t>(n>>(i*8)));};
    auto name=[&](const std::wstring& s){const auto value=registration_string(s);number(static_cast<uint32_t>(value.bytes.size()));bytes.insert(bytes.end(),value.bytes.begin(),value.bytes.end());};
    auto visit=[&](auto&& self,const RegistrationTree& tree)->void {
        number(static_cast<uint32_t>(tree.values.size()));
        for(const auto& [n,v]:tree.values){name(n);number(v.type);number(static_cast<uint32_t>(v.bytes.size()));bytes.insert(bytes.end(),v.bytes.begin(),v.bytes.end());}
        number(static_cast<uint32_t>(tree.children.size()));for(const auto& [n,t]:tree.children){name(n);self(self,t);}
    };visit(visit,*snapshot);std::wstring result;for(auto value:bytes){result+=L"0123456789abcdef"[value>>4];result+=L"0123456789abcdef"[value&15];}return result;
}
struct RegistrationIdentity {std::wstring installation_id,certificate_sha256,server;};
inline std::wstring registration_link_target(const RegistrationValue& link){
    signing_require(link.type==6 && link.bytes.size()>=2 && link.bytes.size()<=1024 && link.bytes.size()%2==0,
        "registry_link_or_unexpected_link_value"); // REG_LINK, exact Windows-managed target only.
    std::wstring target;
    for(size_t i=0;i<link.bytes.size();i+=2)target+=static_cast<wchar_t>(link.bytes[i]|(link.bytes[i+1]<<8));
    if(!target.empty() && target.back()==0)target.pop_back();
    return target;
}
inline std::wstring registration_user_class_hive(const RegistrationValue& link,const std::wstring& current_sid){
    const auto target=registration_link_target(link);const auto hive=current_sid+L"_Classes";
    signing_require(target==L"\\Registry\\User\\"+hive,"registry_link_or_unexpected_link_value");return hive;
}
inline std::wstring registration_system_user_hive(const RegistrationValue& link,const std::wstring& name){
    // The only root alias accepted for read-only loaded-user inspection. Open
    // the physical hive afterward; never follow arbitrary registry links.
    signing_require(name==L"S-1-5-18" && registration_link_target(link)==L"\\Registry\\User\\.Default",
        "registry_link_or_unexpected_link_value");return L".DEFAULT";
}
inline RegistrationTree desired_registration(const RegistrationIdentity& identity){
    signing_require(hexadecimal(identity.installation_id,32) && hexadecimal(identity.certificate_sha256,64) &&
        identity.server.size()>3 && identity.server[1]==L':' && identity.server[2]==L'\\' &&
        identity.server.find_first_of(L"\r\n\"")==std::wstring::npos,"registration_identity_invalid");
    RegistrationTree root;
    root.values={{L"",registration_string(registration_description)},
        {L"OutLoudPocketOwner",registration_string(registration_owner)},
        {L"OutLoudInstallationId",registration_string(identity.installation_id)},
        {L"OutLoudCertificateSHA256",registration_string(identity.certificate_sha256)}};
    root.children[L"InprocServer32"].values={{L"",registration_string(identity.server)},{L"ThreadingModel",registration_string(L"Both")}};
    return root;
}
enum class RegistrationDisposition { absent,owned,conflict,malformed };
// A matching CLSID/owner alone never proves installation ownership.
inline RegistrationDisposition inspect_registration(const RegistrationSnapshot& snapshot,const RegistrationIdentity& identity){
    if(!snapshot)return RegistrationDisposition::absent;
    try {
        const auto& root=*snapshot;
        if(root.children.size()!=1 || !root.children.contains(L"InprocServer32"))return RegistrationDisposition::malformed;
        const auto& server=root.children.at(L"InprocServer32");
        if(!server.children.empty() || server.values.size()!=2 || registration_field(server,L"ThreadingModel")!=L"Both" ||
            registration_field(server,L"").empty())return RegistrationDisposition::malformed;
        for(const auto& [name,value]:root.values){static_cast<void>(name);static_cast<void>(registration_text(value));}
        if(!root.values.contains(L"OutLoudPocketOwner"))return RegistrationDisposition::conflict;
        if(registration_field(root,L"OutLoudPocketOwner")!=registration_owner)return RegistrationDisposition::conflict;
        if(registration_field(root,L"")!=registration_description)return RegistrationDisposition::malformed;
        if(!root.values.contains(L"OutLoudInstallationId") || !root.values.contains(L"OutLoudCertificateSHA256"))return RegistrationDisposition::conflict;
        if(root.values.size()!=4)return RegistrationDisposition::malformed;
        if(!hexadecimal(identity.installation_id,32) || !hexadecimal(identity.certificate_sha256,64))return RegistrationDisposition::conflict;
        if(registration_field(root,L"OutLoudInstallationId")!=identity.installation_id ||
            registration_field(root,L"OutLoudCertificateSHA256")!=identity.certificate_sha256)return RegistrationDisposition::conflict;
        // Actual server file identity is checked separately with pinned handles.
        return RegistrationDisposition::owned;
    }catch(const SigningError&){return RegistrationDisposition::malformed;}
}
inline void require_registration_disposition(RegistrationDisposition disposition){
    if(disposition==RegistrationDisposition::conflict)throw SigningError("registration_conflict");
    if(disposition==RegistrationDisposition::malformed)throw SigningError("registration_malformed");
}
// Operations are supplied by the Windows adapter or the in-memory test registry.
// A transaction failure publishes nothing; a post-commit failure leaves its
// durable before-state for later checked recovery, never a blind registry delete.
template<class Operations> void register_engine(Operations& op){
    op.preconditions();
    const auto before=op.capture();const auto disposition=inspect_registration(before,op.identity());
    require_registration_disposition(disposition);op.check_overrides();
    if(disposition==RegistrationDisposition::owned){op.verify_server(*before);op.verify_post();return;}
    op.capture_recovery(before);op.exchange(before,desired_registration(op.identity()));
    op.verify_post();op.commit_recovery();
}
}
