#define OUTLOUD_MAINTENANCE_CHECK
#include "signing.cpp"
namespace {
unsigned checks{};
void check(bool v,const char* why){if(!v)throw std::runtime_error(why);++checks;}
template<class F> void refuses(F&& f,const char* why){bool refused=false;try{f();}catch(const SigningError&){refused=true;}check(refused,why);}
struct Fixture {
    std::vector<std::string> calls;std::string fault;bool plan{},voices=true,com=true,data=true,payload=true,certificate=true,host=true;
    void point(const char* s){calls.emplace_back(s);if(fault==s)throw SigningError("fixture_failure");}
    void preflight(){point("preflight");}
    void persist(){point("persist");plan=true;}
    void revalidate(){point("revalidate");}
    void remove_registration(){point("registry");voices=false;com=false;}
    void verify_no_dependencies(){point("dependencies");signing_require(!voices && !com,"fixture_dependency");}
    void remove_data(){point("data");data=false;}
    void remove_payload(){point("payload");payload=false;}
    void remove_signing(){point("signing");certificate=false;}
    void verify_native_removed(){point("verify");signing_require(!data && !payload && !certificate,"fixture_remaining");}
    void await_host(){point("host");check(host && plan,"host and recovery retained until host cleanup");}
};
RegistrationTree token(const fs::path& app,const fs::path& library){
    const std::wstring id(32,L'a');RegistrationTree t;
    for(const auto& [name,value]:std::map<std::wstring,std::wstring>{{L"",L"Voice"},{L"409",L"Voice"},{L"CLSID",outloud::sapi::clsid_text},
        {L"OutLoudManagedOwner",L"OutLoud.ManagedVoice.1"},{L"LibraryId",id},{L"AssetRoot",app.native()},
        {L"ModelPath",(app/L"runtime/pocket-english.olp").native()},{L"TokenizerPath",(app/L"runtime/pocket-english-tokenizer.bin").native()},
        {L"VoicePath",(library/L"voices"/id/L"voice.olv").native()},{L"Seed",L"73"},{L"MaximumSegmentTokens",L"128"},{L"KernelPath",L"Auto"}})
        t.values[name]=registration_string(value);
    for(const auto& [name,value]:std::map<std::wstring,std::wstring>{{L"Name",L"Voice"},{L"Language",L"409"},{L"Vendor",L"Out Loud"}})
        t.children[L"Attributes"].values[name]=registration_string(value);return t;
}
}
int wmain(int argc,wchar_t** argv){
    try{
        check(argc==2,"private fixture parent required");const auto parent=installation_path(argv[1]);safe(parent);check(fs::is_directory(parent),"private parent exists");
        const auto suite=parent/(L"uninstall-"+maintenance_random());check(fs::create_directory(suite),"unique private suite");
        {Fixture f;uninstall_installation(f);check(!f.voices && !f.com && !f.data && !f.payload && !f.certificate,"complete native removal");
            check(f.calls==std::vector<std::string>{"preflight","persist","revalidate","registry","dependencies","data","payload","signing","verify","host"},"production operation ordering");}
        for(const auto* point:{"preflight","persist","revalidate","registry","dependencies","data","payload","signing","verify","host"}){
            Fixture f;f.fault=point;refuses([&]{uninstall_installation(f);},"failure propagated");check(f.host,"failed cleanup never removes host");
            if(f.plan){f.fault.clear();uninstall_installation(f);check(!f.data && !f.payload && !f.certificate,"checked forward retry");}}
        const auto app=suite/L"arbitrary-custom-app",library=suite/L"library";fs::create_directories(app/L"bin");fs::create_directory(library);
        check(maintenance_certificate_source_owned(CERT_SYSTEM_STORE_LOCAL_MACHINE,L"Root",L".Default",true,CERT_SYSTEM_STORE_LOCAL_MACHINE,L"Root"),"recorded machine default root eligible");
        check(maintenance_certificate_source_owned(CERT_SYSTEM_STORE_CURRENT_USER,L"Root",L".LocalMachine",false,CERT_SYSTEM_STORE_LOCAL_MACHINE,L"Root"),"verified machine inheritance is not a separate user certificate");
        check(maintenance_certificate_source_owned(CERT_SYSTEM_STORE_USERS,L"S-1-5-19\\Root",L".LocalMachine",false,CERT_SYSTEM_STORE_LOCAL_MACHINE,L"Root"),"loaded-user inherited machine root is not adopted as a user certificate");
        check(!maintenance_certificate_source_owned(CERT_SYSTEM_STORE_USERS,L"S-1-5-19\\Root",L".Default",true,CERT_SYSTEM_STORE_USERS,L"S-1-5-19\\Root"),"loaded-user independent root copy blocks key deletion");
        check(!maintenance_certificate_source_owned(CERT_SYSTEM_STORE_CURRENT_USER,L"Root",L".Default",true,CERT_SYSTEM_STORE_CURRENT_USER,L"Root"),"independent user root copy blocks key deletion");
        check(!maintenance_certificate_source_owned(CERT_SYSTEM_STORE_LOCAL_MACHINE,L"Root",L".GroupPolicy",false,CERT_SYSTEM_STORE_LOCAL_MACHINE_GROUP_POLICY,L"Root"),"policy root copy remains an independent reference");
        check(!maintenance_certificate_source_owned(CERT_SYSTEM_STORE_LOCAL_MACHINE,L"Root",L"Custom",true,CERT_SYSTEM_STORE_LOCAL_MACHINE,L"Root"),"custom physical store is not adopted");
        check(!maintenance_certificate_source_owned(CERT_SYSTEM_STORE_CURRENT_USER,L"Root",L".LocalMachine",false,CERT_SYSTEM_STORE_CURRENT_USER,L"Root"),"inheritance label alone proves no ownership");
        check(!maintenance_certificate_source_owned(CERT_SYSTEM_STORE_LOCAL_MACHINE,L"Root",L".Default",true,CERT_SYSTEM_STORE_LOCAL_MACHINE,L"My"),"redirected default registry store is not adopted");
        check(!maintenance_certificate_source_owned(CERT_SYSTEM_STORE_LOCAL_MACHINE,L"CA",L".Default",true,CERT_SYSTEM_STORE_LOCAL_MACHINE,L"CA"),"unrelated machine store never owned");
        {MaintenanceCertificateSource source{CERT_SYSTEM_STORE_CURRENT_USER,L"must-not-be-opened",L".Default",true,false};
            check(!maintenance_certificate_source_absent(source,ERROR_ACCESS_DENIED),"access denied is never treated as certificate-store absence");
            check(!maintenance_certificate_source_absent(source,ERROR_INVALID_PARAMETER),"provider error is never treated as certificate-store absence");
            auto parameter=registration_string(L"Root");CERT_PHYSICAL_STORE_INFO info{};info.cbSize=sizeof(info);info.pszOpenStoreProvider=const_cast<char*>(CERT_STORE_PROV_SYSTEM_REGISTRY_W);
            info.dwOpenFlags=CERT_SYSTEM_STORE_LOCAL_MACHINE;info.OpenParameters={static_cast<DWORD>(parameter.bytes.size()),parameter.bytes.data()};
            MaintenanceCertificateEnumeration inventory{CERT_SYSTEM_STORE_LOCAL_MACHINE,L"Root",{},false};
            check(maintenance_enumerate_physical_store(nullptr,0,L".Default",&info,nullptr,&inventory) && inventory.sources.size()==1 && inventory.sources[0].owned,"physical callback captures owned default source");
            info.pszOpenStoreProvider=const_cast<char*>(CERT_STORE_PROV_MEMORY);
            check(!maintenance_enumerate_physical_store(nullptr,0,L"Custom",&info,nullptr,&inventory) && inventory.invalid,"unreviewed physical provider refuses certificate cleanup");
            inventory={CERT_SYSTEM_STORE_LOCAL_MACHINE,L"Root",{},false};info.pszOpenStoreProvider=const_cast<char*>(CERT_STORE_PROV_SYSTEM_REGISTRY_W);info.dwOpenFlags|=CERT_SYSTEM_STORE_RELOCATE_FLAG;
            check(!maintenance_enumerate_physical_store(nullptr,0,L".Default",&info,nullptr,&inventory) && inventory.invalid,"relocated physical store refuses instead of interpreting pointer data");}
        {const auto profile=suite/L"other-user";const auto local=profile/L"AppData/Local";
            auto value=registration_string(L"%USERPROFILE%\\AppData\\Local");value.type=REG_EXPAND_SZ;
            check(same_installation_path(uninstall_profile_localappdata(value,profile),local),"service profile known folder resolves its own USERPROFILE");
            value=registration_string(L"%userprofile%\\AppData\\Local");value.type=REG_EXPAND_SZ;
            check(same_installation_path(uninstall_profile_localappdata(value,profile),local),"Windows variable name matching is case insensitive");
            value=registration_string((suite/L"redirected-local").native());
            check(uninstall_profile_localappdata(value,profile)==suite/L"redirected-local","absolute cached redirect preserved");
            value.type=REG_EXPAND_SZ;check(uninstall_profile_localappdata(value,profile)==suite/L"redirected-local","absolute configured redirect preserved");
            for(const auto* unknown:{L"%LOCALAPPDATA%\\OutLoud",L"%APPDATA%\\Local",L"%USERPROFILE\\AppData\\Local"}){
                value=registration_string(unknown);value.type=REG_EXPAND_SZ;
                refuses([&]{uninstall_profile_localappdata(value,profile);},"unknown per-user expansion refuses instead of using uninstall administrator environment");}
            value=registration_string(L"relative-local");refuses([&]{uninstall_profile_localappdata(value,profile);},"relative profile known folder refused");
            value=registration_string(local.native());value.type=REG_MULTI_SZ;refuses([&]{uninstall_profile_localappdata(value,profile);},"unsupported profile value type refused");
            value.type=REG_SZ;value.bytes.pop_back();refuses([&]{uninstall_profile_localappdata(value,profile);},"truncated profile path refused");}
        const std::wstring name=L"OutLoud_Pocket_"+std::wstring(32,L'a');const auto original=token(app,library);
        uninstall_token_owned(name,original,app,library);check(uninstall_decode_tree(encoded_tree(original))==RegistrationSnapshot(original),"typed token exact roundtrip");
        auto changed=original;changed.values[L"AssetRoot"]=registration_string((suite/L"foreign").native());
        refuses([&]{uninstall_token_owned(name,changed,app,library);},"other installation token preserved");
        changed=original;changed.values[L"OutLoudManagedOwner"]=registration_string(L"foreign");refuses([&]{uninstall_token_owned(name,changed,app,library);},"foreign token marker preserved");
        changed=original;changed.children[L"Extra"]={};refuses([&]{uninstall_token_owned(name,changed,app,library);},"unknown token subtree preserved");
        changed=original;changed.values[L"VoicePath"]=registration_string((suite/L"export.olv").native());refuses([&]{uninstall_token_owned(name,changed,app,library);},"external export never inferred as managed voice");
        refuses([&]{uninstall_decode_tree(encoded_tree(original)+L"00");},"trailing registry bytes refused");
        refuses([&]{uninstall_path_decode(L"0000");},"NUL path refused");
        refuses([&]{uninstall_path_decode(maintenance_root_encoding(app.native()+std::wstring(1,L'\0')+L"suffix"));},"embedded NUL cannot alias a different file");
        const auto id=std::wstring(32,L'a');
        check(uninstall_library_path(library/L"voices"/id/L"metadata.json",library,false),"managed metadata allowlist");
        check(uninstall_library_path(library/L".staging"/id/L"old.olv",library,false),"replacement recovery remains managed data");
        check(uninstall_library_path(library/L".trash"/id/L"retained/voice.olv",library,false),"detached removal backup remains managed data");
        check(!uninstall_library_path(library/L"voices"/id/L"backup.olv",library,false),"export in library remains outside managed allowlist");
        check(!uninstall_library_path(suite/L"source/voice.olv",library,false),"source repository stays outside data authority");
        check(!uninstall_library_path(library/L".staging/foreign/voice.olv",library,false),"unknown transaction directory refused");
        MaintenanceInventory inventory;inventory.installation_id=id;inventory.certificate_hash=std::wstring(64,L'c');inventory.generation=std::wstring(32,L'b');
        inventory.version=L"0.1.0";inventory.library_identity=std::wstring(48,L'd');inventory.library_parent_identity=std::wstring(48,L'e');
        const std::vector<std::wstring> package{std::begin(registration_payload_paths),std::end(registration_payload_paths)};
        for(const auto& p:package)inventory.files[p]={std::wstring(64,L'a'),std::wstring(48,L'b'),42};
        const auto decoded=maintenance_inventory_decode(maintenance_inventory_encode(inventory));
        check(decoded.files==inventory.files && decoded.library_identity==inventory.library_identity && decoded.library_parent_identity==inventory.library_parent_identity,"complete inventory retains data bindings across component updates");
        inventory.library_identity.clear();refuses([&]{maintenance_inventory_decode(maintenance_inventory_encode(inventory));},"unbound inventory refused");
        MaintenanceFiles files(app);files.security=[app](const fs::path& p){safe(p);signing_require(!p.lexically_relative(app).native().starts_with(L".."),"fixture_escape");};
        const auto owned=app/L"bin/updated-engine.dll";write_new(owned,L"development replacement recorded by current inventory");const auto stamp=files.stamp(owned);
        {Handle consumer;pin(owned,consumer);refuses([&]{files.acquire(owned);},"running consumer refuses removal");check(fs::exists(owned),"locked file preserved");}
        write_new(suite/L"export.olv",L"independent backup");files.acquire(owned);uninstall_delete_held(files.held(*stamp));files.release();check(!fs::exists(owned),"checked handle deletes only recorded file");
        check(fs::exists(suite/L"export.olv"),"independent backup preserved");
        {const auto epoch_root=suite/L"epoch";fs::create_directories(epoch_root/L"configuration");
            const auto path=epoch_root/L"configuration/maintenance.lock";write_new(path,L"OutLoud.Maintenance.Lock.1\n",false);
            MaintenanceFiles ordinary(epoch_root);ordinary.security=[](const fs::path& p){safe(p);};ordinary.acquire_epoch(false);
            refuses([&]{uninstall_delete_held(ordinary.epoch.value);},"ordinary maintenance lock has no delete access");
            CloseHandle(ordinary.epoch.value);ordinary.epoch.value=INVALID_HANDLE_VALUE;check(fs::exists(path),"refused lock deletion preserves recovery");
            MaintenanceFiles cleanup(epoch_root);cleanup.security=ordinary.security;cleanup.acquire_epoch(false,true);
            MaintenanceFiles competitor(epoch_root);competitor.security=ordinary.security;
            refuses([&]{competitor.acquire_epoch(false,true);},"removable epoch remains exclusive");
            uninstall_delete_held(cleanup.epoch.value);CloseHandle(cleanup.epoch.value);cleanup.epoch.value=INVALID_HANDLE_VALUE;
            check(!fs::exists(path),"uninstall deletes verified epoch through its original exclusive handle");}
        const auto multi=app/L"bin/multi.dll";write_new(multi,L"foreign link");check(CreateHardLinkW((suite/L"link.dll").c_str(),multi.c_str(),nullptr)!=FALSE,"private hardlink fixture");
        refuses([&]{files.stamp(multi);},"hardlinked resources preserved");
        {WindowsUninstall op(app);op.library=library;op.storage().security=[](const fs::path& p){safe(p);};
            fs::create_directories(library/L"voices"/id);write_new(library/L"voices"/id/L"voice.olv",L"private managed voice");
            write_new(library/L"voices"/id/L"metadata.json",L"private metadata");op.library_inventory(library);
            check(op.plan.files.size()==2,"real library traversal captures managed voice and metadata");
            write_new(library/L"voices"/id/L"backup.olv",L"exported backup");
            refuses([&]{op.library_inventory(library);},"real traversal refuses unknown export before deleting data");
            check(fs::exists(library/L"voices"/id/L"voice.olv") && fs::exists(library/L"voices"/id/L"backup.olv"),"failed preflight preserves all library data");}
        {WindowsUninstall op(app);op.storage().security=files.security;const auto path=app/L"bin/private-dependency.dll";
            write_new(path,L"original expected bytes");const auto before=op.storage().stamp(path);op.plan.files.push_back({0,path,*before});
            fs::remove(path);write_new(path,L"foreign replacement");op.storage().acquire(path);
            refuses([&]{op.remove_group(0);},"production grouped deletion refuses a foreign replacement");check(fs::exists(path),"foreign replacement retained");}
        const fs::path unicode=app/L"custom-ž";check(uninstall_path_decode(maintenance_root_encoding(unicode.native()))==unicode.native(),"Unicode custom directory roundtrip");
        std::wcout<<L"PASS: "<<checks<<L" focused uninstall checks. Private files; simulated registry/certificates only. Evidence: "<<suite.native()<<L"\n";return 0;
    }catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<"\n";return 1;}
}
