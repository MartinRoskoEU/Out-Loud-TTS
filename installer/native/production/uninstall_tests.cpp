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
        {L"Backend",L"Rust"},{L"RustRuntimePath",(app/L"runtime/pocket-tts/pocket-tts.exe").native()},
        {L"VoicePath",(library/L"voices"/id/L"voice.safetensors").native()}})
        t.values[name]=registration_string(value);
    for(const auto& [name,value]:std::map<std::wstring,std::wstring>{{L"Name",L"Voice"},{L"Language",L"409"},{L"Vendor",L"Out Loud"}})
        t.children[L"Attributes"].values[name]=registration_string(value);return t;
}
void voice_preflight_checks(){
    // Pure snapshots only: no registry queries, installed files, or path existence probes.
    const fs::path app=L"Q:\\OutLoud-Preflight\\current",old=L"Q:\\OutLoud-Preflight\\previous",
        library=L"Q:\\OutLoud-Preflight\\VoiceLibrary";
    const std::wstring key=L"OutLoud_Pocket_"+std::wstring(32,L'a');
    const auto original=token(app,library);
    const auto inspect=[&](const RegistrationTree& tree,bool supported=true,const std::wstring& name=L""){
        return inspect_voice_preflight_token(name.empty()?key:name,tree,library,supported,L"HKLM64/Speech");
    };
    auto result=inspect(original);
    check(!result.blocked(),"verified global registrations permit fresh setup and maintenance");
    auto existing=original;
    const auto abandoned=token(old,library);
    result=inspect(abandoned);
    check(!result.blocked(),"old application locations are metadata and are not probed");
    check(inspect(original,false).status==L"unsupported_scope","per-user or other Windows token scopes are not adopted");
    existing=original;existing.values[L"RustRuntimePath"]=registration_string(old/L"runtime/pocket-tts/pocket-tts.exe");
    result=inspect(existing);check(!result.blocked(),"different historical runtime locations do not block global setup");
    existing=original;existing.values[L"VoicePath"]=registration_string(old/L"outside.safetensors");
    result=inspect(existing);check(result.status==L"unverified" && result.field==L"VoicePath","voice paths outside the managed library are not trusted");
    check(result.name==L"Voice" && result.token==key,"blocked voice is identified without changing it");
    existing=original;existing.values[L"Unexpected"]=registration_string(L"unknown");
    check(inspect(existing).status==L"unverified","modified token schemas are not silently accepted");
    existing=original;existing.values[L"OutLoudManagedOwner"]=registration_string(L"other");
    check(inspect(existing).status==L"unverified","managed namespace alone does not prove ownership");
    existing=original;existing.values[L"LibraryId"]=registration_string(std::wstring(32,L'b'));
    check(inspect(existing).status==L"unverified","library identity must match the token name");
    existing=original;existing.values[L"CLSID"]=registration_string(L"{other-engine}");
    check(inspect(existing).status==L"unverified","changed CLSID on an Out Loud candidate needs review");
    existing=original;existing.values.erase(L"CLSID");
    check(inspect(existing).status==L"unverified","missing CLSID does not hide an Out Loud candidate");
    existing=original;existing.values.erase(L"OutLoudManagedOwner");
    check(inspect(existing).status==L"unverified","missing ownership marker is never adopted");
    existing=original;existing.values[L"AssetRoot"]=registration_string(L"relative\\OutLoud");
    check(!inspect(existing).blocked(),"inert application metadata is not resolved as a path");
    existing=original;existing.values[L"AssetRoot"]=registration_string(L"\\\\server\\OutLoud");
    check(!inspect(existing).blocked(),"inert remote application metadata causes no network access");
    existing=original;existing.values[L"AssetRoot"].type=REG_BINARY;
    check(inspect(existing).status==L"unverified","incorrect path value types are not trusted");
    RegistrationTree unrelated;unrelated.values[L"CLSID"]=registration_string(L"{another-vendor}");
    check(!inspect(unrelated,true,L"Microsoft_Voice").blocked(),"unrelated Windows voices never block fresh setup");
    unrelated.values[L"CLSID"].type=REG_BINARY;
    check(!inspect(unrelated,true,L"Microsoft_Voice").blocked(),"an unrelated malformed CLSID is not claimed as Out Loud");
    unrelated.values[L"LibraryId"]=registration_string(L"another-library");
    check(!inspect(unrelated,true,L"Other_Voice").blocked(),"generic library metadata is not an ownership indicator");
    check(inspect(RegistrationTree{},true,L"OUTLOUD_POCKET_incomplete").status==L"unverified",
        "case-insensitive managed namespace candidates cannot evade review");
    existing=original;existing.values[L""]=registration_string(L"Voice\n[results]\nvoice_registration=clear");
    existing.values[L"VoicePath"]=registration_string(old/L"outside.safetensors");
    result=inspect(existing);const auto report=voice_preflight_report(result);
    check(report.find(L"\n[results]")==report.npos && report.find(L"voice_name="+maintenance_root_encoding(result.name))!=report.npos,
        "untrusted voice names cannot inject report fields");
    check(report.find(L"voice_registration=unverified\n")!=report.npos && report.find(L"voice_text_encoding=utf16le-hex\n")!=report.npos,
        "preflight outcome and diagnostic text encoding are explicit");
    check(original==token(app,library) && abandoned==token(old,library),"inspection leaves all registration snapshots unchanged");
}
void voice_removal_checks(){
    const fs::path app=L"Q:\\OutLoud-Removal\\installed",old=L"Q:\\OutLoud-Removal\\historical",
        library=L"Q:\\OutLoud-Removal\\VoiceLibrary";
    const std::wstring first=L"OutLoud_Pocket_"+std::wstring(32,L'a'),second=L"OutLoud_Pocket_"+std::wstring(32,L'b');
    const auto current_voice=token(app,library);
    auto historical=token(old,library);historical.values[L"LibraryId"]=registration_string(std::wstring(32,L'b'));
    historical.values[L"VoicePath"]=registration_string(library/L"voices"/std::wstring(32,L'b')/L"voice.safetensors");
    RegistrationTree unrelated;unrelated.values[L"CLSID"]=registration_string(L"{Microsoft-voice-engine}");
    const std::map<std::wstring,RegistrationTree> registry{{first,current_voice},{second,historical},{L"Microsoft_Voice",unrelated}};
    UninstallPlan plan;plan.tokens=uninstall_collect_engine_tokens(registry,library);
    check(plan.tokens.size()==2 && plan.tokens.at(first)==current_voice && plan.tokens.at(second)==historical,
        "all verified current and historical engine dependencies enter the removal snapshot");
    check(!plan.tokens.contains(L"Microsoft_Voice"),"unrelated Windows voices never enter the removal plan");
    check(plan.files.empty() && plan.directories.empty(),"token metadata grants no file or directory cleanup authority");
    uninstall_engine_token_owned(first,current_voice,library);check(true,"current native voice is eligible");
    uninstall_engine_token_owned(second,historical,library);check(true,"historical native voice needs no existing deployment or profile file");
    check(uninstall_decode_tree(encoded_tree(historical))==RegistrationSnapshot(historical),"historical tokens retain exact typed retry snapshots");
    auto spelling=historical;
    for(const auto* field:{L"AssetRoot",L"RustRuntimePath",L"VoicePath"}){
        auto text=maintenance_path_key(registration_field(spelling,field));std::replace(text.begin(),text.end(),L'\\',L'/');
        spelling.values[field]=registration_string(text);
    }
    uninstall_engine_token_owned(second,spelling,library);check(true,"historical metadata preserves Windows case/separator equivalence");
    auto invalid=historical;invalid.values[L"RustRuntimePath"]=registration_string(app/L"runtime/pocket-tts/pocket-tts.exe");
    UninstallVoiceMismatch mismatch;
    uninstall_engine_token_owned(second,invalid,library);check(true,"old runtime metadata does not govern registry cleanup");
    invalid=historical;invalid.values[L"VoicePath"]=registration_string(old/L"external.safetensors");
    refuses([&]{uninstall_engine_token_owned(second,invalid,library,&mismatch);},"external profile paths never inherit the shared library binding");
    check(mismatch.token==second && mismatch.display_name==L"Voice" && mismatch.field==L"VoicePath","external profile metadata identifies the blocked voice");
    invalid=historical;invalid.values[L"AssetRoot"]=registration_string(L"relative\\OutLoud");
    uninstall_engine_token_owned(second,invalid,library);check(true,"relative application metadata is not used for file operations");
    invalid=historical;invalid.values[L"AssetRoot"]=registration_string(L"\\\\server\\OutLoud");
    uninstall_engine_token_owned(second,invalid,library);check(true,"remote application metadata is never accessed");
    invalid=historical;invalid.values[L"AssetRoot"].type=REG_BINARY;
    refuses([&]{uninstall_engine_token_owned(second,invalid,library);},"typed metadata validation is retained");
    invalid=historical;invalid.values[L"OutLoudManagedOwner"]=registration_string(L"someone else");
    refuses([&]{uninstall_engine_token_owned(second,invalid,library);},"another owner is never adopted");
    invalid=historical;invalid.values[L"LibraryId"]=registration_string(std::wstring(32,L'a'));
    refuses([&]{uninstall_engine_token_owned(second,invalid,library);},"profile identity must match the managed token name");
    invalid=historical;invalid.values[L"CLSID"]=registration_string(L"{another-engine}");
    refuses([&]{uninstall_collect_engine_tokens({{second,invalid}},library);},"changed Out Loud candidates cannot be silently left behind");
    invalid=historical;invalid.values.erase(L"CLSID");
    refuses([&]{uninstall_collect_engine_tokens({{second,invalid}},library);},"missing engine identity is preserved and reported");
    invalid=historical;invalid.values[L"Unexpected"]=registration_string(L"unknown");
    refuses([&]{uninstall_engine_token_owned(second,invalid,library);},"unknown token values remain protected");
    invalid=historical;invalid.children[L"Unknown"]={};
    refuses([&]{uninstall_engine_token_owned(second,invalid,library);},"unknown token subtrees remain protected");
    invalid=historical;invalid.values[L"Backend"]=registration_string(L"legacy");
    refuses([&]{uninstall_engine_token_owned(second,invalid,library);},"unsupported backend metadata is not converted or adopted");
    invalid=historical;invalid.children.at(L"Attributes").values[L"Vendor"]=registration_string(L"another vendor");
    refuses([&]{uninstall_engine_token_owned(second,invalid,library);},"vendor proof is retained");
    refuses([&]{uninstall_collect_engine_tokens({{L"Unowned_Custom_Voice",current_voice}},library);},"an engine CLSID alone never proves managed ownership");
    refuses([&]{uninstall_collect_engine_tokens({{L"OutLoud_Pocket_incomplete",RegistrationTree{}}},library);},"incomplete managed candidates stop removal");
    RegistrationTree malformed;malformed.values[L"clsid"]=registration_string(outloud::sapi::clsid_text);
    check(managed_voice_registration_candidate(L"Custom_Voice",malformed),"case-changed engine references are still discovered");
    refuses([&]{uninstall_collect_engine_tokens({{L"Custom_Voice",malformed}},library);},"a case-changed custom engine reference is preserved and blocks removal");
    malformed.values.clear();malformed.values[L"CLSID"]=registration_string(outloud::sapi::clsid_text);malformed.values[L"CLSID"].type=REG_BINARY;
    check(managed_voice_registration_candidate(L"Custom_Voice",malformed),"wrongly typed Out Loud references are review candidates");
    refuses([&]{uninstall_collect_engine_tokens({{L"Custom_Voice",malformed}},library);},"wrongly typed engine references never become deletion proof");
    malformed.values.clear();malformed.values[L"outloudmanagedowner"]=registration_string(L"unknown");
    check(managed_voice_registration_candidate(L"Custom_Voice",malformed),"case-changed ownership claims cannot be silently skipped");
    uninstall_verify_voice_snapshot(registry,plan.tokens,false,"fixture_changed");check(true,"unchanged snapshot permits the existing transaction");
    auto changed=registry;changed.at(second).values[L""]=registration_string(L"changed while uninstalling");
    refuses([&]{uninstall_verify_voice_snapshot(changed,plan.tokens,false,"fixture_changed");},"changed tokens block deletion before transaction writes");
    changed=registry;changed.erase(second);
    refuses([&]{uninstall_verify_voice_snapshot(changed,plan.tokens,false,"fixture_changed");},"unexpected disappearance blocks initial removal");
    uninstall_verify_voice_snapshot(changed,plan.tokens,true,"fixture_changed");check(true,"verified retry accepts only an absent previously planned token");
    changed.at(first).values[L"409"]=registration_string(L"changed");
    refuses([&]{uninstall_verify_voice_snapshot(changed,plan.tokens,true,"fixture_changed");},"retry never accepts changed surviving tokens");
    changed=registry;changed.emplace(L"OutLoud_Pocket_new",RegistrationTree{});
    refuses([&]{uninstall_verify_voice_snapshot(changed,plan.tokens,false,"fixture_changed");},"new malformed candidates cannot race the removal plan");
    changed=registry;changed.emplace(L"Another_Vendor",unrelated);
    uninstall_verify_voice_snapshot(changed,plan.tokens,false,"fixture_changed");check(true,"new unrelated voices do not expand Out Loud's authority");
    for(const auto& [name,t]:plan.tokens){static_cast<void>(t);changed.erase(name);}
    check(changed.size()==2 && changed.at(L"Microsoft_Voice")==unrelated && changed.at(L"Another_Vendor")==unrelated,
        "simulated removal preserves every unrelated registration");
    check(std::none_of(changed.begin(),changed.end(),[](const auto& entry){return managed_voice_registration_candidate(entry.first,entry.second);}),
        "simulated transaction leaves no Out Loud candidate");
    check(!inspect_voice_preflight_token(second,historical,library,true,L"HKLM64/Speech").blocked(),
        "installation and uninstall use the same global registration identity");
    check(inspect_voice_preflight_token(second,historical,library,false,L"HKCU64/Speech").status==L"unsupported_scope",
        "other registry locations remain unsupported and preserved");
    const std::wstring injected=L"OutLoud_Pocket_bad\n[results]\nuninstall=complete";
    refuses([&]{uninstall_engine_token_owned(injected,historical,library,&mismatch);},"unverified token names are never deleted");
    const auto report=uninstall_voice_mismatch_report(mismatch);
    check(report.find(L"\n[results]")==report.npos && report.find(L"blocked_voice="+maintenance_root_encoding(injected))!=report.npos &&
        report.find(L"blocked_voice_encoding=utf16le-hex\n")!=report.npos,"unknown token names cannot inject failure report fields");
    check(registry.at(first)==current_voice && registry.at(second)==historical,"eligibility and guards leave original snapshots unchanged");
}
}
int wmain(int argc,wchar_t** argv){
    try{
        if(argc==2 && std::wstring_view(argv[1])==L"--voice-preflight"){
            voice_preflight_checks();std::cout<<"PASS: "<<checks<<" read-only voice preflight checks; simulated registrations only.\n";return 0;
        }
        if(argc==2 && std::wstring_view(argv[1])==L"--voice-removal"){
            voice_removal_checks();std::cout<<"PASS: "<<checks<<" voice removal checks; simulated registrations only.\n";return 0;
        }
        voice_preflight_checks();
        voice_removal_checks();
        check(argc==2,"private fixture parent required");const auto parent=installation_path(argv[1]);safe(parent);check(fs::is_directory(parent),"private parent exists");
        const auto suite=parent/(L"uninstall-"+maintenance_random());check(fs::create_directory(suite),"unique private suite");
        {
            const auto app=suite/L"owned-app",root=suite/L"shared-model-data";fs::create_directories(app);fs::create_directory(root);
            const std::wstring id=maintenance_random();
            const auto binding=L"OutLoud.WinUI.Data.2\n"+id+L"\n"+maintenance_root_encoding(app.native())+L"\n"+maintenance_directory_identity(root)+L"\n";
            const auto initialize=[&](WindowsUninstall& op){op.plan.owner.installation_id=id;op.plan.owner.root_encoding=maintenance_root_encoding(app.native());};
            write_new(root/L"model-selection.json",L"{\"SchemaVersion\":1,\"SelectedModelAlias\":\"tts\"}");
            {WindowsUninstall op(app);initialize(op);refuses([&]{op.winui_store(root);},"unmarked WinUI store is never adopted");}
            write_new(root/L".outloud-installation.dat",binding);
            {WindowsUninstall op(app);initialize(op);op.winui_store(root);check(op.plan.files.size()==2 && op.plan.files[0].group==4,"bound shared Settings are inventoried");}
            {WindowsUninstall op(app);initialize(op);op.plan.owner.installation_id=maintenance_random();refuses([&]{op.winui_store(root);},"foreign installation binding preserved");}
            write_new(root/L"notes.txt",L"unknown user document");
            {WindowsUninstall op(app);initialize(op);op.winui_store(root);check(op.plan.files.size()==2,"unknown user file does not block known data cleanup");check(fs::exists(root/L"notes.txt"),"unknown file retained");}
            fs::remove(root/L"notes.txt");
            const auto blobs=root/L"models/blobs/sha256";fs::create_directories(blobs);
            const auto temporary=blobs/L"sample";write_new(temporary,L"isolated model bytes");Handle input;pin(temporary,input);
            const auto digest=file_hash(input.value);CloseHandle(input.value);input.value=INVALID_HANDLE_VALUE;fs::rename(temporary,blobs/digest);
            {WindowsUninstall op(app);initialize(op);op.winui_store(root);check(op.plan.files.size()==3,"model blob requires matching content identity");}
            {Handle writable;pin(blobs/digest,writable,true);DWORD written{};const char bad='x';check(WriteFile(writable.value,&bad,1,&written,nullptr) && written==1,"private corruption fixture");}
            {WindowsUninstall op(app);initialize(op);refuses([&]{op.winui_store(root);},"corrupted blob is preserved");}
            fs::remove(blobs/digest);
            const auto linked=blobs/std::wstring(64,L'a');fs::create_hard_link(root/L"model-selection.json",linked);
            {WindowsUninstall op(app);initialize(op);refuses([&]{op.winui_store(root);},"hard-linked shared data cannot be removed");}
            fs::remove(linked);
            check(!uninstall_winui_path(root/L"models/.staging/unknown.partial",root,false),"incomplete model staging stays unverified");
            check(!uninstall_winui_path(root/L"runtimes/.staging"/std::wstring(64,L'a')/L"pocket-tts.exe",root,false),"runtime staging cannot impersonate a verified release");
            check(!uninstall_winui_path(root/L"../outside.txt",root,false),"shared storage path cannot escape");
            {
                const auto download_id=maintenance_random(),partial=maintenance_random()+L".partial";
                const auto staging=root/L"models/.staging",operation=staging/download_id,journal=staging/(download_id+L".json");
                fs::create_directories(operation);write_new(operation/partial,L"incomplete owned download");
                {WindowsUninstall op(app);initialize(op);op.winui_store(root);check(op.plan.files.size()==2,"unrecorded download is left outside deletion plan");}
                const auto record=L"{\"SchemaVersion\":1,\"OperationId\":\""+download_id+L"\",\"Kind\":\"model\",\"StoreIdentity\":\""+
                    maintenance_directory_identity(root)+L"\",\"Files\":[\""+partial+L"\"]}";
                write_new(journal,record);
                {WindowsUninstall op(app);initialize(op);op.winui_store(root);check(op.plan.files.size()==4,"recorded partial download is inventoried");}
                write_new(operation/L"notes.txt",L"unknown file");
                {WindowsUninstall op(app);initialize(op);op.winui_store(root);check(op.plan.files.size()==4,"unknown file in staging does not block owned download cleanup");}
                check(fs::exists(operation/L"notes.txt") && fs::exists(operation/partial),"failed download preflight deletes no files");
                fs::remove(operation/L"notes.txt");fs::remove(journal);
                write_new(journal,L"{\"SchemaVersion\":1,\"OperationId\":\""+download_id+L"\",\"Kind\":\"model\",\"StoreIdentity\":\""+
                    std::wstring(48,L'0')+L"\",\"Files\":[\""+partial+L"\"]}");
                {WindowsUninstall op(app);initialize(op);op.winui_store(root);check(op.plan.files.size()==2,"foreign physical store download record grants no deletion authority");}
                fs::remove(journal);fs::remove(operation/partial);fs::remove(operation);
            }
            {
                const auto download_id=maintenance_random();
                const auto staging=root/L"runtimes/.staging",operation=staging/download_id,journal=staging/(download_id+L".tmp");
                fs::create_directories(operation/L"payload");write_new(operation/L"runtime.zip",L"partial ZIP");
                write_new(operation/L"payload/pocket-tts.exe",L"partially extracted executable");
                write_new(journal,L"{\"SchemaVersion\":1,\"OperationId\":\""+download_id+L"\",\"Kind\":\"runtime\",\"StoreIdentity\":\""+
                    maintenance_directory_identity(root)+L"\",\"Files\":[\"runtime.zip\",\"payload/pocket-tts.exe\",\"payload/LICENSE\",\"payload/runtime-installation.json\"]}");
                {WindowsUninstall op(app);initialize(op);op.winui_store(root);check(op.plan.files.size()==5,"recorded interrupted runtime extraction is inventoried");}
                fs::remove(journal);fs::remove(operation/L"runtime.zip");fs::remove(operation/L"payload/pocket-tts.exe");
                fs::remove(operation/L"payload");fs::remove(operation);
            }
            {
                const auto operation_id=maintenance_random();const auto staging=root/L"models/.staging",operation=staging/operation_id;
                fs::create_directories(operation);write_new(operation/L"value.json",L"{partial metadata");
                const auto journal=staging/(operation_id+L".json");
                write_new(journal,L"{\"SchemaVersion\":1,\"OperationId\":\""+operation_id+L"\",\"Kind\":\"model-selection\",\"StoreIdentity\":\""+
                    maintenance_directory_identity(root)+L"\",\"Files\":[\"value.json\"]}");
                {WindowsUninstall op(app);initialize(op);op.winui_store(root);check(op.plan.files.size()==4,"recorded incomplete metadata is inventoried without parsing its payload");}
                fs::remove(journal);fs::remove(operation/L"value.json");fs::remove(operation);
            }
            fs::remove(root/L".outloud-installation.dat");
            write_new(root/L".outloud-installation.dat",L"OutLoud.WinUI.Data.2\n"+id+L"\n"+
                maintenance_root_encoding(maintenance_path_key(app.native()))+L"\n"+maintenance_directory_identity(root)+L"\n");
            {WindowsUninstall op(app);initialize(op);op.winui_store(root);check(op.plan.files.size()==2,"Windows installation path casing does not invalidate ownership");}
            const auto git=root/L"models/blobs/git-blob-sha1";fs::create_directory(git);
            write_new(git/L"f2ba8f84ab5c1bce84a7b441cb1959cfc7093b7f",L"abc");
            {WindowsUninstall op(app);initialize(op);op.winui_store(root);check(op.plan.files.size()==3,"Git blob SHA1 includes the canonical blob header");}
            const auto sample=suite/L"sample-runtime",license=suite/L"sample-license";
            write_new(sample,L"private runtime fixture");write_new(license,L"private license fixture");Handle runtime_pin,license_pin;pin(sample,runtime_pin);pin(license,license_pin);
            const auto runtime_hash=file_hash(runtime_pin.value),license_hash=file_hash(license_pin.value);
            CloseHandle(runtime_pin.value);runtime_pin.value=INVALID_HANDLE_VALUE;CloseHandle(license_pin.value);license_pin.value=INVALID_HANDLE_VALUE;
            const auto release=root/L"runtimes/v4.1.0"/runtime_hash;fs::create_directories(release);
            fs::rename(sample,release/L"pocket-tts.exe");fs::rename(license,release/L"LICENSE");
            write_new(release/L"runtime-installation.json",L"{\"ReleaseVersion\":\"v4.1.0\",\"ExecutableSha256\":\""+runtime_hash+
                L"\",\"ArchiveSha256\":\""+std::wstring(64,L'a')+L"\",\"AssetId\":1,\"AssetName\":\"pocket-tts-4.1.0-x86_64-pc-windows-msvc.zip\","
                L"\"AssetUrl\":\"https://github.com/kyutai-labs/pocket-tts/releases/download/v4.1.0/pocket-tts-4.1.0-x86_64-pc-windows-msvc.zip\",\"LicenseSha256\":\""+license_hash+L"\"}");
            {WindowsUninstall op(app);initialize(op);op.winui_store(root);check(op.plan.files.size()==6,"verified runtime update, license and receipt are inventoried");}
            fs::remove(release/L"LICENSE");write_new(release/L"LICENSE",L"changed license");
            {WindowsUninstall op(app);initialize(op);refuses([&]{op.winui_store(root);},"unverified runtime license is retained");}
        }
        {
            const auto app=suite/L"shared-app",root=suite/L"shared-data";
            fs::create_directories(app);fs::create_directory(root);
            const std::wstring id=maintenance_random(),physical=maintenance_directory_identity(root);
            const std::vector<std::wstring> shared{L"OutLoud.WinUI.Data.2",id,maintenance_root_encoding(app.native()),physical};
            check(uninstall_winui_binding_valid(shared,id,app,physical),"machine binding has no user SID");
            check(!uninstall_winui_binding_valid(shared,maintenance_random(),app,physical),"another installation cannot own shared storage");
            check(!uninstall_winui_binding_valid(shared,id,suite/L"other-app",physical),"shared installation path remains bound");
            check(!uninstall_winui_binding_valid(shared,id,app,std::wstring(48,L'a')),"shared physical identity remains bound");
            auto unknown=shared;unknown[0]=L"OutLoud.WinUI.Data.3";
            check(!uninstall_winui_binding_valid(unknown,id,app,physical),"unknown machine binding is preserved");
            check(!uninstall_winui_binding_valid({},id,app,physical),"missing machine binding is preserved");
            check(uninstall_winui_path(root/L"update-checks/model-update-checks.json",root,false),"comparison cache has a bounded shared location");
            check(!uninstall_winui_path(root/L"update-checks/pocket-tts.exe",root,false),"comparison cache cannot contain executable payloads");
            check(!uninstall_winui_path(root/L"update-checks/model-selection.json",root,false),"shared selection cannot reside in writable comparison cache");
        }
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
        const std::wstring name=L"OutLoud_Pocket_"+std::wstring(32,L'a');const auto original=token(app,library);
        uninstall_engine_token_owned(name,original,library);check(uninstall_decode_tree(encoded_tree(original))==RegistrationSnapshot(original),"typed token exact roundtrip");
        {
            auto equivalent=original;
            for(const auto* field:{L"AssetRoot",L"RustRuntimePath",L"VoicePath"}){
                auto path=maintenance_path_key(registration_field(equivalent,field));std::replace(path.begin(),path.end(),L'\\',L'/');
                equivalent.values[field]=registration_string(path);
            }
            uninstall_engine_token_owned(name,equivalent,library);check(true,"normal Windows case and separator differences do not block owned voice removal");
            for(const auto* field:{L"VoicePath"}){
                auto foreign=original;const auto elsewhere=suite/L"other-installation"/field;
                foreign.values[field]=registration_string(elsewhere.native());UninstallVoiceMismatch mismatch;
                const auto before=foreign;
                refuses([&]{uninstall_engine_token_owned(name,foreign,library,&mismatch);},"foreign voice path remains protected");
                check(mismatch.token==name && mismatch.display_name==L"Voice" && mismatch.field==field && mismatch.actual==elsewhere &&
                    !mismatch.expected.empty() && foreign==before,"mismatch reports exact token, name, field and paths without changing registration");
                const auto report=uninstall_voice_mismatch_report(mismatch);
                check(report.find(L"\nvoice_field="+std::wstring(field)+L"\n")!=report.npos &&
                    report.find(L"\nvoice_actual="+maintenance_root_encoding(elsewhere.native())+L"\n")!=report.npos &&
                    report.find(L"\nblocked_voice_name="+maintenance_root_encoding(L"Voice")+L"\n")!=report.npos,
                    "native failure report retains ownership mismatch context");
            }
        }
        auto changed=original;changed.values[L"AssetRoot"]=registration_string((suite/L"foreign").native());
        uninstall_engine_token_owned(name,changed,library);check(true,"application location metadata does not govern registry ownership");
        changed=original;changed.values[L"OutLoudManagedOwner"]=registration_string(L"foreign");refuses([&]{uninstall_engine_token_owned(name,changed,library);},"foreign token marker preserved");
        changed=original;changed.children[L"Extra"]={};refuses([&]{uninstall_engine_token_owned(name,changed,library);},"unknown token subtree preserved");
        changed=original;changed.values[L"VoicePath"]=registration_string((suite/L"export.safetensors").native());refuses([&]{uninstall_engine_token_owned(name,changed,library);},"external export never inferred as managed voice");
        refuses([&]{uninstall_decode_tree(encoded_tree(original)+L"00");},"trailing registry bytes refused");
        refuses([&]{uninstall_path_decode(L"0000");},"NUL path refused");
        refuses([&]{uninstall_path_decode(maintenance_root_encoding(app.native()+std::wstring(1,L'\0')+L"suffix"));},"embedded NUL cannot alias a different file");
        const auto id=std::wstring(32,L'a');
        check(uninstall_library_path(library/L"voices"/id/L"metadata.json",library,false),"managed metadata allowlist");
        check(!uninstall_library_path(library/L".staging"/id/L"old.olv",library,false),"unsupported recovery data stays outside deletion authority");
        check(uninstall_library_path(library/L".trash"/id/L"retained/voice.safetensors",library,false),"detached removal backup remains managed data");
        check(!uninstall_library_path(library/L"voices"/id/L"backup.olv",library,false),"export in library remains outside managed allowlist");
        check(!uninstall_library_path(suite/L"source/voice.safetensors",library,false),"source repository stays outside data authority");
        check(!uninstall_library_path(library/L".staging/foreign/voice.safetensors",library,false),"unknown transaction directory refused");
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
            fs::create_directories(library/L"voices"/id);write_new(library/L"voices"/id/L"voice.safetensors",L"private managed voice");
            write_new(library/L"voices"/id/L"metadata.json",L"private metadata");op.library_inventory(library);
            check(op.plan.files.size()==2,"real library traversal captures managed voice and metadata");
            write_new(library/L"voices"/id/L"backup.olv",L"exported backup");
            const auto before=op.plan.files.size();op.library_inventory(library);check(op.plan.files.size()==before+2,"real traversal leaves unknown export outside deletion plan");
            check(fs::exists(library/L"voices"/id/L"voice.safetensors") && fs::exists(library/L"voices"/id/L"backup.olv"),"failed preflight preserves all library data");}
        {WindowsUninstall op(app);op.storage().security=files.security;const auto path=app/L"bin/private-dependency.dll";
            write_new(path,L"original expected bytes");const auto before=op.storage().stamp(path);op.plan.files.push_back({0,path,*before});
            fs::remove(path);write_new(path,L"foreign replacement");op.storage().acquire(path);
            refuses([&]{op.remove_group(0);},"production grouped deletion refuses a foreign replacement");check(fs::exists(path),"foreign replacement retained");}
        {
            const auto root=suite/L"archive-app";fs::create_directories(root/L"configuration");
            fs::create_directories(root/L"bin");write_new(root/L"bin/live.dll",L"current application remains");
            MaintenanceJournal owner;owner.installation_id=maintenance_random();owner.root_encoding=maintenance_root_encoding(root.native());
            owner.operation=MaintenanceOperation::development;owner.components=MaintenanceComponents::engine;owner.host=MaintenanceHost::developer;
            owner.phase=MaintenancePhase::committed;owner.old_version=owner.new_version=L"0.3.0";
            owner.registration_before=owner.registration_after=L"abcd";
            owner.ownership={owner.installation_id,L"S-1-5-32-544",std::wstring(key_prefix)+owner.installation_id,std::wstring(40,L'b'),std::wstring(64,L'c'),std::wstring(64,L'd')};
            for(const auto& p:std::vector<std::wstring>{signing_target,maintenance_metadata[0],maintenance_metadata[1],maintenance_metadata[2]}){
                MaintenanceChange change;change.path=p;change.after=MaintenanceStamp{std::wstring(64,L'a'),std::wstring(48,L'b'),10};
                if(p==signing_target)change.input_hash=std::wstring(64,L'a');owner.changes.push_back(change);}
            const auto security=[&](const fs::path& p){safe(p);signing_require(uninstall_within(p,root),"archive_fixture_escape");};
            const auto archive=[&](unsigned frames){
                auto j=owner;j.transaction_id=maintenance_random();const auto vault=root/L"configuration/maintenance"/j.transaction_id;
                fs::create_directories(vault);write_new(vault/L"terminal.pointer",j.transaction_id+L"\n");
                const auto staged=vault/L"new"/signing_target;fs::create_directories(staged.parent_path());write_new(staged,L"obsolete owned payload");
                MaintenanceFiles inspect(root);inspect.security=security;j.changes.front().after=inspect.stamp(staged);
                const auto text=maintenance_encode(j);
                for(unsigned n=0;n<frames;++n)write_new(vault/(L"state-"+std::to_wstring(n)+L".dat"),text);
                return vault;};
            const auto configure=[&](WindowsArchiveCleanup& cleanup){cleanup.storage().security=security;cleanup.storage().protected_state=false;};
            MaintenanceFiles epoch(root);epoch.security=security;epoch.protected_state=false;epoch.acquire_epoch(true);
            auto vault=archive(3);
            {WindowsArchiveCleanup cleanup(root,owner);configure(cleanup);cleanup.run();}
            check(!fs::exists(vault) && !fs::exists(root/L"configuration/archive-cleanup.dat"),"completed archive and cleanup plan retired");
            check(fs::exists(root/L"bin/live.dll"),"archive retirement never removes current application payload");
            vault=archive(3);
            {WindowsArchiveCleanup cleanup(root,owner);configure(cleanup);unsigned removed{};
                cleanup.before_delete=[&](const fs::path&){if(++removed==2)throw MaintenanceInterrupted{};};
                try{cleanup.run();check(false,"archive interruption injected");}catch(const MaintenanceInterrupted&){}
                check(fs::exists(cleanup.receipt),"interrupted archive cleanup retains durable plan");}
            {WindowsArchiveCleanup cleanup(root,owner);configure(cleanup);cleanup.run();}
            check(!fs::exists(vault),"next maintenance resumes partly deleted archive without original frames");
            vault=archive(2);write_new(vault/L"notes.txt",L"unrelated user content");
            {WindowsArchiveCleanup cleanup(root,owner);configure(cleanup);cleanup.run();}
            check(fs::exists(vault/L"notes.txt") && !fs::exists(vault/L"terminal.pointer"),"unknown archive content survives owned cleanup without blocking it");
            {WindowsArchiveCleanup cleanup(root,owner);configure(cleanup);cleanup.run();}
            fs::remove(vault/L"notes.txt");fs::remove(vault);
            vault=archive(2);
            {Handle consumer;pin(vault/L"state-0.dat",consumer);WindowsArchiveCleanup cleanup(root,owner);configure(cleanup);
                refuses([&]{cleanup.run();},"locked archive stops before first deletion");
                check(fs::exists(vault/L"state-1.dat") && fs::exists(vault/L"terminal.pointer"),"busy archive retains its resources");}
            {WindowsArchiveCleanup cleanup(root,owner);configure(cleanup);cleanup.run();}
            vault=archive(2);
            {WindowsArchiveCleanup cleanup(root,owner);configure(cleanup);cleanup.before_delete=[&](const fs::path&){throw MaintenanceInterrupted{};};
                try{cleanup.run();}catch(const MaintenanceInterrupted&){} }
            {WindowsUninstall removal(root);removal.plan.owner=owner;removal.storage().security=security;removal.archives();
                check(std::any_of(removal.plan.files.begin(),removal.plan.files.end(),[&](const auto& f){return f.path==root/L"configuration/archive-cleanup.dat";}),
                    "uninstall inventories the interrupted archive cleanup plan without mutation");}
            const auto saved=root/L"saved-state.dat";fs::rename(vault/L"state-0.dat",saved);write_new(vault/L"state-0.dat",L"foreign replacement");
            {WindowsArchiveCleanup cleanup(root,owner);configure(cleanup);refuses([&]{cleanup.run();},"changed surviving identity blocks resumed cleanup");}
            check(fs::exists(vault/L"terminal.pointer"),"changed archive preflight deletes no other resource");
            fs::remove(vault/L"state-0.dat");fs::rename(saved,vault/L"state-0.dat");
            auto foreign=owner;foreign.installation_id=maintenance_random();
            {WindowsArchiveCleanup cleanup(root,foreign);configure(cleanup);refuses([&]{cleanup.run();},"foreign installation cannot resume cleanup");}
            {WindowsArchiveCleanup cleanup(root,owner);configure(cleanup);cleanup.run();}
            vault=archive(2);
            {WindowsArchiveCleanup cleanup(root,owner);configure(cleanup);bool arrived=false;
                cleanup.before_delete=[&](const fs::path&){if(!arrived){arrived=true;write_new(vault/L"late.txt",L"late user file");}};cleanup.run();}
            check(fs::exists(vault/L"late.txt") && !fs::exists(vault/L"state-0.dat"),"late unlisted file is preserved without blocking recorded deletion");
            fs::remove(vault/L"late.txt");fs::remove(vault);
            write_new(root/L"personal.txt",L"user content");
            {WindowsUninstall removal(root);removal.storage().security=security;removal.plan.host_executable=root/L"unins000.exe";
                const auto live=root/L"bin/live.dll";removal.add(0,live);removal.directory(live.parent_path());removal.installation_inventory(false);
                check(removal.plan.files.size()==1 && fs::exists(root/L"personal.txt"),"unlisted installation file does not block known payload inventory");}
            fs::remove(root/L"personal.txt");
            vault=archive(2);
            {MaintenanceFiles inspect(root);inspect.security=security;
                const auto committed=maintenance_decode(inspect.read(vault/L"state-0.dat"),{std::begin(registration_payload_paths),std::end(registration_payload_paths)});
                fs::remove(vault/L"terminal.pointer");write_new(root/L"configuration/maintenance.pending",committed.transaction_id+L"\n");
                Handle consumer;pin(vault/L"state-0.dat",consumer);
                WindowsMaintenance completion(root);completion.files.security=security;completion.files.protected_state=false;
                completion.files.vault=vault;completion.active=committed;completion.finish(MaintenanceOutcome::committed);
                const auto report=maintenance_result(completion,MaintenanceOutcome::committed);
                check(completion.archive_cleanup_pending && report.find(L"maintenance=committed\n")!=report.npos && report.find(L"archive_cleanup=pending\n")!=report.npos,
                    "cleanup failure is reported separately from verified installation commit");
                check(!fs::exists(root/L"configuration/maintenance.pending") && fs::exists(root/L"configuration/archive-cleanup.dat"),
                    "cleanup failure preserves retry authority without reopening completed host transaction");}
            {WindowsArchiveCleanup cleanup(root,owner);configure(cleanup);cleanup.run();}
            for(unsigned n=0;n<3;++n){vault=archive(2);WindowsArchiveCleanup cleanup(root,owner);configure(cleanup);cleanup.run();}
            check(fs::is_empty(root/L"configuration/maintenance"),"repeated successful updates do not accumulate archives");
            vault=archive(8193);
            {WindowsUninstall removal(root);removal.plan.owner=owner;removal.storage().security=security;removal.archives();
                check(removal.plan.files.size()>8192 && removal.encode().size()<=16*1024*1024,
                    "legacy large history fits the consistently bounded uninstall plan");}
            {WindowsArchiveCleanup cleanup(root,owner);configure(cleanup);cleanup.run();}
            check(!fs::exists(vault),"existing history above the previous 8192-file limit is cleaned");
        }
        const fs::path unicode=app/L"custom-ž";check(uninstall_path_decode(maintenance_root_encoding(unicode.native()))==unicode.native(),"Unicode custom directory roundtrip");
        std::wcout<<L"PASS: "<<checks<<L" focused uninstall checks. Private files; simulated registry/certificates only. Evidence: "<<suite.native()<<L"\n";return 0;
    }catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<"\n";return 1;}
}
