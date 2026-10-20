#pragma once
#include "maintenance.hpp"
namespace outloud::installer {
// Length-framed ASCII journal. Unicode root is UTF-16LE hex; no raw paths/keys.
// Duplicate/trailing/truncated fields cannot be silently accepted as INI defaults.
struct MaintenanceWriter {
    std::wstring text=L"OutLoud.Maintenance.2\n";
    void put(const std::wstring& s){signing_require(s.size()<=131072,"maintenance_field_limit");
        for(auto c:s)signing_require(c>=32 && c<127,"maintenance_field_encoding");text+=std::to_wstring(s.size())+L":"+s+L"\n";}
    void number(uint64_t n){put(std::to_wstring(n));}
    void file(const MaintenanceFile& f){number(f?1:0);if(f){put(f->hash);put(f->identity);number(f->size);}}
};
struct MaintenanceReader {
    std::wstring_view text;size_t cursor{};
    explicit MaintenanceReader(std::wstring_view t):text(t){constexpr std::wstring_view magic=L"OutLoud.Maintenance.2\n";
        signing_require(t.size()<=1024*1024 && t.starts_with(magic),"maintenance_journal_invalid");cursor=magic.size();}
    std::wstring get(){const auto colon=text.find(L':',cursor);signing_require(colon!=text.npos && colon>cursor && colon-cursor<=6,"maintenance_journal_invalid");
        size_t n{};for(size_t i=cursor;i<colon;++i){signing_require(text[i]>=L'0' && text[i]<=L'9',"maintenance_journal_invalid");n=n*10+text[i]-L'0';}
        signing_require(n<=131072 && colon+1+n<text.size() && text[colon+1+n]==L'\n',"maintenance_journal_invalid");
        const auto part=text.substr(colon+1,n);for(auto c:part)signing_require(c>=32 && c<127,"maintenance_journal_invalid");
        cursor=colon+2+n;return std::wstring(part);}
    uint64_t number(){const auto s=get();signing_require(!s.empty() && s.size()<=20,"maintenance_journal_invalid");uint64_t n{};
        for(auto c:s){signing_require(c>=L'0' && c<=L'9' && n<=(UINT64_MAX-static_cast<unsigned>(c-L'0'))/10,"maintenance_journal_invalid");n=n*10+c-L'0';}return n;}
    MaintenanceFile file(){const auto present=number();signing_require(present<=1,"maintenance_journal_invalid");
        if(!present)return {};MaintenanceStamp f;f.hash=get();f.identity=get();f.size=number();maintenance_stamp(f);return f;}
};
inline std::wstring maintenance_encode(const MaintenanceJournal& j){
    MaintenanceWriter w;if(maintenance_development_upgrade(j))w.text=L"OutLoud.Maintenance.3\n";
    w.put(j.transaction_id);w.put(j.installation_id);w.put(j.root_encoding);w.put(j.old_version);w.put(j.new_version);
    w.number(static_cast<unsigned>(j.operation));w.number(static_cast<unsigned>(j.components));w.number(static_cast<unsigned>(j.host));w.number(static_cast<unsigned>(j.phase));
    for(const auto* s:{&j.ownership.id,&j.ownership.owner_sid,&j.ownership.key_name,&j.ownership.thumbprint,&j.ownership.certificate_sha256,&j.ownership.signed_sha256,
        &j.registration_before,&j.registration_after,&j.resource_phase,&j.library_identity,&j.library_parent_identity})w.put(*s);
    w.file(j.bootstrap_detector);w.file(j.bootstrap_requirements);w.file(j.bootstrap_report);
    w.number(j.changes.size());for(const auto& c:j.changes){w.put(c.path);w.put(c.input_hash);w.file(c.before);w.file(c.after);w.file(c.backup);}
    if(maintenance_development_upgrade(j)){w.put(j.host_before);w.put(j.host_after);}return w.text;
}
inline MaintenanceJournal maintenance_decode(const std::wstring& text,const std::vector<std::wstring>& package){
    const bool upgrade=text.starts_with(L"OutLoud.Maintenance.3\n");
    auto compatible=text;if(upgrade)compatible[20]=L'2';
    MaintenanceReader r(compatible);MaintenanceJournal j;j.transaction_id=r.get();j.installation_id=r.get();j.root_encoding=r.get();j.old_version=r.get();j.new_version=r.get();
    const auto op=r.number(),components=r.number(),host=r.number(),phase=r.number();
    signing_require(op<=3 && components<=3 && host<=1 && phase<=10,"maintenance_enum_invalid");
    j.operation=static_cast<MaintenanceOperation>(op);j.components=static_cast<MaintenanceComponents>(components);j.host=static_cast<MaintenanceHost>(host);j.phase=static_cast<MaintenancePhase>(phase);
    for(auto* s:{&j.ownership.id,&j.ownership.owner_sid,&j.ownership.key_name,&j.ownership.thumbprint,&j.ownership.certificate_sha256,&j.ownership.signed_sha256,
        &j.registration_before,&j.registration_after,&j.resource_phase,&j.library_identity,&j.library_parent_identity})*s=r.get();
    j.bootstrap_detector=r.file();j.bootstrap_requirements=r.file();j.bootstrap_report=r.file();
    const auto count=r.number();signing_require(count<=2*maintenance_inventory_limit+std::size(maintenance_metadata),"maintenance_changes_invalid");
    for(uint64_t i=0;i<count;++i){MaintenanceChange c;c.path=r.get();c.input_hash=r.get();c.before=r.file();c.after=r.file();c.backup=r.file();j.changes.push_back(std::move(c));}
    signing_require(upgrade==maintenance_development_upgrade(j),"maintenance_journal_version_invalid");
    if(upgrade){j.host_before=r.get();j.host_after=r.get();}
    signing_require(r.cursor==r.text.size(),"maintenance_journal_trailing_data");validate_maintenance(j,package);return j;
}
inline std::wstring maintenance_root_encoding(const std::wstring& root){
    std::wstring result;for(auto c:root){for(unsigned shift:{0u,8u}){const auto byte=static_cast<unsigned>(c)>>shift;result+=L"0123456789abcdef"[(byte>>4)&15];result+=L"0123456789abcdef"[byte&15];}}return result;
}
}
