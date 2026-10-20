#pragma once
#include "signing_policy.hpp"
#include "registration_policy.hpp"
#include <algorithm>
#include <optional>
#include <set>
#include <string_view>
#include <exception>
#include <array>
namespace outloud::installer {
// A03.12 shared lifecycle. Standalone signing/registration paths remain disabled.
inline constexpr bool maintenance_lifecycle_ready=true;
enum class MaintenanceOperation { fresh,release,repair,development };
enum class MaintenanceComponents { engine,voice_manager,both,package };
enum class MaintenanceHost { inno,developer };
enum class MaintenancePhase { preparing,signing,ready,publishing,registration,metadata,verifying,awaiting_host,committed,rolling_back,rolled_back };
struct MaintenanceStamp {
    std::wstring hash,identity; uint64_t size{};
    bool operator==(const MaintenanceStamp&)const=default;
};
using MaintenanceFile=std::optional<MaintenanceStamp>;
inline bool operator==(const SigningOwnership& a,const SigningOwnership& b){
    return a.id==b.id && a.owner_sid==b.owner_sid && a.key_name==b.key_name && a.thumbprint==b.thumbprint &&
        a.certificate_sha256==b.certificate_sha256 && a.signed_sha256==b.signed_sha256;
}
struct MaintenanceChange {
    std::wstring path,input_hash; MaintenanceFile before,after,backup;
    bool operator==(const MaintenanceChange&)const=default;
};
struct MaintenanceJournal {
    std::wstring transaction_id,installation_id,root_encoding,old_version,new_version;
    MaintenanceOperation operation{};MaintenanceComponents components{};MaintenanceHost host{};
    MaintenancePhase phase=MaintenancePhase::preparing;
    SigningOwnership ownership;
    std::wstring registration_before,registration_after,resource_phase,library_identity,library_parent_identity;
    // Only explicit full-package developer upgrades own these host snapshots.
    std::wstring host_before,host_after;
    MaintenanceFile bootstrap_detector,bootstrap_requirements,bootstrap_report;
    std::vector<MaintenanceChange> changes;
    bool operator==(const MaintenanceJournal&)const=default;
};
inline bool maintenance_development_upgrade(const MaintenanceJournal& j){
    return j.operation==MaintenanceOperation::development && j.components==MaintenanceComponents::package;
}
inline std::array<unsigned,3> maintenance_version_parts(const std::wstring& v){
    std::array<unsigned,3> result{};size_t start{};
    for(size_t i=0;i<3;++i){const auto end=v.find(L'.',start);const auto part=v.substr(start,end==v.npos?v.size()-start:end-start);
        signing_require(!part.empty() && part.size()<=5 && part.find_first_not_of(L"0123456789")==part.npos &&
            (part.size()==1 || part.front()!=L'0') && ((i==2)==(end==v.npos)),"maintenance_version_invalid");
        result[i]=static_cast<unsigned>(std::stoul(part));signing_require(result[i]<=65535,"maintenance_version_invalid");start=end+1;}
    return result;
}
inline std::vector<std::wstring> maintenance_selection(MaintenanceComponents components,const std::vector<std::wstring>& package){
    const std::vector<std::wstring> engine{signing_target};
    const std::vector<std::wstring> manager{L"bin/OutLoudVoiceManagerNative.dll",L"bin/OutLoudVoiceImportHelper.exe",
        L"bin/OutLoud.VoiceManager.exe",L"bin/OutLoud.VoiceManager.dll",L"bin/OutLoud.VoiceManager.deps.json",L"bin/OutLoud.VoiceManager.runtimeconfig.json"};
    auto result=components==MaintenanceComponents::package?package:std::vector<std::wstring>{};
    if(components==MaintenanceComponents::engine || components==MaintenanceComponents::both)result.insert(result.end(),engine.begin(),engine.end());
    if(components==MaintenanceComponents::voice_manager || components==MaintenanceComponents::both)result.insert(result.end(),manager.begin(),manager.end());
    std::sort(result.begin(),result.end());
    signing_require(!result.empty() && std::adjacent_find(result.begin(),result.end())==result.end(),"maintenance_selection_invalid");
    for(const auto& path:result)signing_require(std::find(package.begin(),package.end(),path)!=package.end(),"maintenance_selection_not_packaged");
    return result;
}
inline constexpr const wchar_t* maintenance_metadata[]={L"configuration/signing.ini",L"configuration/registration.ini",L"configuration/inventory.dat"};
inline constexpr size_t maintenance_inventory_limit=128;
inline bool maintenance_is_metadata(const std::wstring& p){
    return std::find(std::begin(maintenance_metadata),std::end(maintenance_metadata),p)!=std::end(maintenance_metadata);
}
inline std::wstring maintenance_path_key(std::wstring p){
    for(auto& c:p)if(c>=L'A' && c<=L'Z')c+=L'a'-L'A';return p;
}
// Historical inventories are protected receipts, not an incoming package
// allowlist. Still restrict them to canonical, non-aliased application paths;
// configuration/recovery state and the managed voice library are never payload.
inline void maintenance_payload_path(const std::wstring& p){
    signing_require(!p.empty() && p.size()<MAX_PATH && p.find_first_not_of(L"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789_./-")==p.npos,
        "maintenance_payload_path_invalid");
    const auto slash=p.find(L'/');
    signing_require(slash==p.npos || p.substr(0,slash)==L"bin" || p.substr(0,slash)==L"runtime" || p.substr(0,slash)==L"licenses",
        "maintenance_payload_path_invalid");
    size_t at=0;
    while(at<p.size()){
        const auto end=p.find(L'/',at);const auto part=p.substr(at,end==p.npos?p.size()-at:end-at);
        signing_require(!part.empty() && part.front()!=L'.' && part.back()!=L'.',"maintenance_payload_path_invalid");
        const auto stem=maintenance_path_key(part.substr(0,part.find(L'.')));
        signing_require(stem!=L"con" && stem!=L"prn" && stem!=L"aux" && stem!=L"nul" &&
            !(stem.size()==4 && (stem.starts_with(L"com") || stem.starts_with(L"lpt")) && stem[3]>=L'1' && stem[3]<=L'9'),"maintenance_payload_path_invalid");
        if(end==p.npos)break;at=end+1;signing_require(at<p.size(),"maintenance_payload_path_invalid");
    }
    const auto key=maintenance_path_key(p);
    signing_require(key!=L"configuration" && key!=L"bin" && key!=L"runtime" && key!=L"licenses" && key!=L"voicelibrary",
        "maintenance_payload_path_invalid");
}
inline void maintenance_payload_paths(const std::vector<std::wstring>& paths,size_t limit=maintenance_inventory_limit){
    signing_require(!paths.empty() && paths.size()<=limit,"maintenance_inventory_paths_invalid");
    std::set<std::wstring> keys;
    for(const auto& p:paths){maintenance_payload_path(p);signing_require(keys.insert(maintenance_path_key(p)).second,"maintenance_inventory_paths_invalid");}
    for(const auto& p:keys)for(auto at=p.find(L'/');at!=p.npos;at=p.find(L'/',at+1))
        signing_require(!keys.contains(p.substr(0,at)),"maintenance_inventory_paths_invalid");
    signing_require(std::find(paths.begin(),paths.end(),signing_target)!=paths.end(),"maintenance_inventory_engine_missing");
}
inline void maintenance_stamp(const MaintenanceFile& file){
    if(file)signing_require(hexadecimal(file->hash,64) && hexadecimal(file->identity,48),"maintenance_file_stamp_invalid");
}
inline void validate_maintenance(const MaintenanceJournal& j,const std::vector<std::wstring>& package){
    signing_require(j.library_identity.empty() || hexadecimal(j.library_identity,48),"maintenance_library_identity_invalid");
    signing_require(j.library_parent_identity.empty() || hexadecimal(j.library_parent_identity,48),"maintenance_library_parent_identity_invalid");
    maintenance_stamp(j.bootstrap_detector);maintenance_stamp(j.bootstrap_requirements);maintenance_stamp(j.bootstrap_report);
    signing_require(hexadecimal(j.transaction_id,32) && hexadecimal(j.installation_id,32) &&
        !j.root_encoding.empty() && j.root_encoding.size()<131072 && j.root_encoding.size()%4==0 &&
        j.root_encoding.find_first_not_of(L"0123456789abcdef")==std::wstring::npos,"maintenance_identity_invalid");
    signing_require(static_cast<unsigned>(j.operation)<=3 && static_cast<unsigned>(j.components)<=3 &&
        static_cast<unsigned>(j.host)<=1 && static_cast<unsigned>(j.phase)<=10,"maintenance_enum_invalid");
    signing_require(j.operation!=MaintenanceOperation::development || (j.host==MaintenanceHost::developer &&
        (maintenance_development_upgrade(j)?maintenance_version_parts(j.old_version)<maintenance_version_parts(j.new_version):
            j.old_version==j.new_version)),"maintenance_development_identity_change");
    signing_require(j.operation==MaintenanceOperation::development ||
        (j.host==MaintenanceHost::inno && j.components==MaintenanceComponents::package),"maintenance_host_invalid");
    signing_require(!j.new_version.empty() && j.new_version.size()<32 &&
        j.new_version.find_first_not_of(L"0123456789.")==std::wstring::npos,"maintenance_version_invalid");
    signing_require(j.operation!=MaintenanceOperation::repair || j.old_version==j.new_version,"maintenance_repair_version_change");
    signing_require(j.operation!=MaintenanceOperation::fresh || (j.old_version.empty() && j.registration_before==L"absent"),"maintenance_fresh_identity_invalid");
    // Full-package journals contain their own incoming list via input_hash.
    // This also reads older terminal archives after the package changes.
    auto wanted=j.components==MaintenanceComponents::package?std::vector<std::wstring>{}:maintenance_selection(j.components,package);
    std::vector<std::wstring> incoming,retired;
    for(const auto& c:j.changes)if(!maintenance_is_metadata(c.path)){
        maintenance_payload_path(c.path);
        if(!c.input_hash.empty())incoming.push_back(c.path);
        else {
            signing_require(j.components==MaintenanceComponents::package && j.operation!=MaintenanceOperation::fresh && !c.after &&
                (c.before || j.operation==MaintenanceOperation::repair),"maintenance_removal_invalid");retired.push_back(c.path);
        }
    }
    if(j.components==MaintenanceComponents::package){maintenance_payload_paths(incoming);wanted=incoming;wanted.insert(wanted.end(),retired.begin(),retired.end());
        maintenance_payload_paths(wanted,2*maintenance_inventory_limit);}
    wanted.insert(wanted.end(),std::begin(maintenance_metadata),std::end(maintenance_metadata));std::sort(wanted.begin(),wanted.end());
    std::vector<std::wstring> found;
    for(const auto& change:j.changes){
        found.push_back(change.path);maintenance_stamp(change.before);maintenance_stamp(change.after);maintenance_stamp(change.backup);
        if(maintenance_is_metadata(change.path))signing_require(change.input_hash.empty(),"maintenance_metadata_input_invalid");
        signing_require(hexadecimal(change.input_hash,64) || change.input_hash.empty(),"maintenance_input_hash_invalid");
        if(change.backup)signing_require(change.before && change.before->hash==change.backup->hash && change.before->size==change.backup->size,"maintenance_backup_invalid");
        if(j.operation==MaintenanceOperation::fresh)signing_require(!change.before,"maintenance_fresh_adoption_refused");
        if(j.phase>=MaintenancePhase::ready && j.phase<=MaintenancePhase::committed)
            signing_require((change.after || (!maintenance_is_metadata(change.path) && change.input_hash.empty())) &&
                (!change.before || change.backup),"maintenance_not_prepared");
    }
    std::sort(found.begin(),found.end());signing_require(found==wanted,"maintenance_changes_invalid");
    signing_require(j.ownership.id==j.installation_id && j.ownership.owner_sid==L"S-1-5-32-544" &&
        j.ownership.key_name==std::wstring(key_prefix)+j.installation_id,"maintenance_ownership_invalid");
    if(j.phase>=MaintenancePhase::ready && j.phase<=MaintenancePhase::committed){validate_ownership(j.ownership,j.ownership.owner_sid);
        signing_require(hexadecimal(j.ownership.signed_sha256,64),"maintenance_signing_receipt_invalid");}
    const auto tree=[](const std::wstring& value){return value==L"absent" || (!value.empty() && value.size()<=131072 && value.size()%2==0 &&
        value.find_first_not_of(L"0123456789abcdef")==std::wstring::npos);};
    signing_require(tree(j.registration_before) && tree(j.registration_after),"maintenance_registry_snapshot_invalid");
    signing_require(maintenance_development_upgrade(j)?
        (tree(j.host_before) && tree(j.host_after) && j.host_before!=L"absent" && j.host_after!=L"absent" && j.host_before!=j.host_after):
        (j.host_before.empty() && j.host_after.empty()),"maintenance_host_snapshot_invalid");
    signing_require(j.operation==MaintenanceOperation::fresh || j.registration_before==j.registration_after,"maintenance_update_registration_changed");
}
// Explicit simulated process death bypasses normal unwinding rollback in tests.
// No production CLI exposes this exception or any failure-injection switch.
struct MaintenanceInterrupted {};
enum class MaintenanceOutcome { committed,awaiting_host,rolled_back };
// Recovery reads actual identities, never trusts a phase counter as proof of work.
template<class Operations> MaintenanceOutcome recover_maintenance(Operations& op,MaintenanceJournal& j,bool confirm_host=false){
    op.authenticate_recovery(j);validate_maintenance(j,op.package());
    if(j.phase==MaintenancePhase::committed){op.verify_host(j);op.verify_committed(j);return MaintenanceOutcome::committed;}
    if(j.phase==MaintenancePhase::rolled_back){op.verify_original(j);return MaintenanceOutcome::rolled_back;}
    if(confirm_host && j.phase==MaintenancePhase::awaiting_host){
        op.verify_host(j);op.verify_committed(j);j.phase=MaintenancePhase::committed;op.save(j);return MaintenanceOutcome::committed;
    }
    // Check EVERY file, metadata and registry before the first restore. A foreign
    // edit even in the last row must not trigger an earlier partial rollback.
    op.validate_recovery_resources(j);
    for(const auto& c:j.changes){const auto current=op.current(c.path);
        const bool moved_original=!current && c.before && op.original(c.path)==c.before;
        signing_require(current==c.before || (c.after && current==c.after) || moved_original,"maintenance_recovery_foreign_file");
        if(current!=c.before && c.before)signing_require(c.backup && op.backup(c.path)==c.backup &&
            op.original(c.path)==c.before,"maintenance_recovery_backup_invalid");}
    j.phase=MaintenancePhase::rolling_back;op.save(j);
    op.restore_registration(j); // Checked exact state only; never touch voices.
    for(auto it=j.changes.rbegin();it!=j.changes.rend();++it)if(op.current(it->path)!=it->before)op.restore(*it);
    op.restore_resources(j);op.verify_original(j);j.phase=MaintenancePhase::rolled_back;op.save(j);
    return MaintenanceOutcome::rolled_back;
}
template<class Operations> MaintenanceOutcome reconcile_maintenance(Operations& op,MaintenanceJournal& j){
    if(j.host==MaintenanceHost::inno && j.phase==MaintenancePhase::awaiting_host){
        op.authenticate_recovery(j);validate_maintenance(j,op.package());op.verify_committed(j);
        // Inno may have partly written or rolled back its host. Rerun host work
        // using this already verified payload; don't roll it back beneath Inno.
        return MaintenanceOutcome::awaiting_host;
    }
    return recover_maintenance(op,j);
}
template<class Operations> MaintenanceOutcome maintain_installation(Operations& op){
    // No public mutation before all current ownership/files/candidates are verified.
    auto j=op.preflight();validate_maintenance(j,op.package());
    std::vector<std::wstring> incoming;for(const auto& c:j.changes)if(!c.input_hash.empty())incoming.push_back(c.path);
    std::sort(incoming.begin(),incoming.end());
    signing_require(incoming==maintenance_selection(j.components,op.package()),"maintenance_incoming_package_invalid");
    try{
        op.begin(j);
        op.stage(j);j.phase=MaintenancePhase::signing;op.save(j);op.sign_staged(j);
        op.prepare_metadata(j);op.backup_all(j);op.lock_resources(j);op.revalidate(j);
        j.phase=MaintenancePhase::ready;validate_maintenance(j,op.package());op.save(j);
        j.phase=MaintenancePhase::publishing;op.save(j);
        for(const auto& c:j.changes)if(std::find(std::begin(maintenance_metadata),std::end(maintenance_metadata),c.path)==std::end(maintenance_metadata))op.publish(c);
        j.phase=MaintenancePhase::registration;op.save(j);op.publish_registration(j);
        j.phase=MaintenancePhase::metadata;op.save(j);
        for(const auto& c:j.changes)if(std::find(std::begin(maintenance_metadata),std::end(maintenance_metadata),c.path)!=std::end(maintenance_metadata))op.publish(c);
        j.phase=MaintenancePhase::verifying;op.save(j);op.verify_committed(j);
        if(j.host==MaintenanceHost::inno || maintenance_development_upgrade(j)){
            j.phase=MaintenancePhase::awaiting_host;op.save(j);return MaintenanceOutcome::awaiting_host;}
        op.verify_host(j);j.phase=MaintenancePhase::committed;op.save(j);return MaintenanceOutcome::committed;
    }catch(const MaintenanceInterrupted&){throw;}catch(...){
        const auto original=std::current_exception();
        // Reload the last flushed journal. It may be committed even if the caller
        // observed a later reporting failure; recovery must not undo that commit.
        try{op.release_for_recovery();j=op.load();static_cast<void>(recover_maintenance(op,j));}
        catch(...){throw SigningError("maintenance_failed_recovery_required");}
        std::rethrow_exception(original);
    }
}
}
