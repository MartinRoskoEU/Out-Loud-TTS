#include "outloud/rust_model_selection.hpp"
#include "outloud/voice_library.hpp"
#include "outloud/sapi_voices.hpp"
#include "voice_library_fs.hpp"
#include "voice_json.hpp"
#include <fstream>
#include <iostream>
#include <process.h>
#include <thread>
#include <shlobj.h>

namespace outloud::testing {
void selection_roots(std::filesystem::path,std::filesystem::path);
std::filesystem::path default_selection_root();
std::filesystem::path default_synthesis_staging();
}
namespace {
namespace fs=outloud::detail::library_fs;
using Json=outloud::detail::voice_json::Value;
unsigned checks=0;
void check(bool value,const char* message) { if (!value) { throw std::runtime_error(message); } ++checks; }
template<class F> void rejected(F function,const char* message) {
    bool failed=false; try { function(); } catch (const std::exception&) { failed=true; }
    check(failed,message);
}
std::string quote(std::string_view value) {
    std::string out="\"";
    for (const auto ch:value) {
        if (ch=='"' || ch=='\\') { out+='\\'; out+=ch; }
        else if (ch=='\n') { out+="\\n"; } else if (ch=='\r') { out+="\\r"; }
        else if (ch=='\t') { out+="\\t"; } else { out+=ch; }
    }
    return out+'"';
}
std::string json(const Json& value) {
    if (value.kind==Json::Kind::string) { return quote(value.text); }
    if (value.kind==Json::Kind::array || value.kind==Json::Kind::object) {
        const bool object=value.kind==Json::Kind::object;
        std::string result=object ? "{" : "["; bool first=true;
        const auto append=[&](std::string item) { if (!first) { result+=','; } first=false; result+=item; };
        if (object) { for (const auto& [key,item]:value.object) { append(quote(key)+":"+json(item)); } }
        else { for (const auto& item:value.array) { append(json(item)); } }
        return result+(object ? '}' : ']');
    }
    return value.text;
}
std::string read(const std::filesystem::path& path) {
    const auto file=fs::file(path,GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_DELETE); const auto bytes=fs::read(file.value,path,16*1024*1024);
    return {reinterpret_cast<const char*>(bytes.data()),bytes.size()};
}
void write(const std::filesystem::path& path,std::string_view value) {
    std::ofstream file(path,std::ios::binary|std::ios::trunc); file.write(value.data(),static_cast<std::streamsize>(value.size()));
    if (!file) { throw std::runtime_error("Cannot write isolated test fixture."); }
}
void profile(const std::filesystem::path& path,bool compatible=true) {
    std::string header="{"; std::uint64_t offset=0;
    const unsigned layers=compatible ? 6 : 1;
    for (unsigned i=0;i<layers;++i) {
        if (i) { header+=','; }
        const auto name="transformer.layers."+std::to_string(i)+".self_attn/";
        header+=quote(name+"cache")+":{\"dtype\":\"F32\",\"shape\":[2,1,1,16,64],\"data_offsets\":["+std::to_string(offset)+","+std::to_string(offset+8192)+"]},";
        offset+=8192;
        header+=quote(name+"offset")+":{\"dtype\":\"I64\",\"shape\":[1],\"data_offsets\":["+std::to_string(offset)+","+std::to_string(offset+8)+"]}";
        offset+=8;
    }
    header+='}'; while (header.size()%8) { header+=' '; }
    std::ofstream file(path,std::ios::binary); const auto size=static_cast<std::uint64_t>(header.size());
    file.write(reinterpret_cast<const char*>(&size),8); file<<header;
    std::array<char,8192> zeros{}; const std::uint64_t one=1;
    for (unsigned i=0;i<layers;++i) { file.write(zeros.data(),zeros.size()); file.write(reinterpret_cast<const char*>(&one),8); }
}
class Registry final:public outloud::SapiVoiceRegistry {
public:
    std::map<std::wstring,outloud::SapiTokenTree> tokens;
    std::vector<std::wstring> keys() override { std::vector<std::wstring> result; for (const auto& [key,value]:tokens) { (void)value; result.push_back(key); } return result; }
    std::optional<outloud::SapiTokenTree> read(std::wstring_view key) override { const auto at=tokens.find(std::wstring(key)); return at==tokens.end() ? std::nullopt : std::optional(at->second); }
    void verify_engine(const std::filesystem::path&) override { }
    void exchange(std::wstring_view key,const std::optional<outloud::SapiTokenTree>& expected,const std::optional<outloud::SapiTokenTree>& replacement) override {
        check(read(key)==expected,"Registry compare/exchange changed");
        if (replacement) { tokens[std::wstring(key)]=*replacement; } else { tokens.erase(std::wstring(key)); }
    }
};
void sink(void* context,std::span<const std::int16_t> samples) { *static_cast<std::uint64_t*>(context)+=samples.size(); }
}
int wmain(int argc,wchar_t** argv) {
    try {
        if(argc==2 && std::wstring_view(argv[1])==L"--storage-contract"){
            PWSTR raw=nullptr;
            check(SUCCEEDED(SHGetKnownFolderPath(FOLDERID_ProgramData,KF_FLAG_DONT_VERIFY,nullptr,&raw)),"machine known folder resolves");
            const auto expected=std::filesystem::path(raw)/L"OutLoud/ApplicationData";CoTaskMemFree(raw);
            const auto actual=outloud::testing::default_selection_root();
            check(actual==expected,"SAPI selection uses shared application data");
            check(SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData,KF_FLAG_DONT_VERIFY,nullptr,&raw)),"temporary caller known folder resolves");
            const auto temporary=std::filesystem::path(raw)/L"Temp/OutLoud-Synthesis";CoTaskMemFree(raw);
            check(outloud::testing::default_synthesis_staging()==temporary,"speech configurations remain caller-private temporary files");
            check(!fs::within(temporary,actual),"speech needs no shared persistent write access");
            std::cout<<checks<<" shared storage checks passed\n";return 0;
        }
        if (argc!=3 && argc!=5) { throw std::runtime_error("Supply own bundled runtime and Out Loud schema-2 inventory; optionally SAPI checker and private DLL."); }
        const auto scratch=std::filesystem::temp_directory_path()/(L"outloud-selection-"+fs::wide(fs::random_id()));
        std::filesystem::create_directory(scratch);
        const auto data=scratch/L"data", models=data/L"models", voices=scratch/L"voices";
        std::filesystem::create_directories(models/L".staging"); write(models/L".operations.lock","");
        auto inventory=outloud::detail::voice_json::Parser(read(argv[2]),1048576,8192,16384).parse();
        check(inventory.object.at("SchemaVersion").text=="2","Source inventory not schema 2");
        auto& entries=inventory.object.at("Models").array;
        const auto found=std::find_if(entries.begin(),entries.end(),[](const auto& item) { return item.object.at("Alias").text=="english_2026-10"; });
        check(found!=entries.end(),"Install English in Out Loud before this opt-in check");
        auto entry=*found;
        for (const auto& asset:entry.object.at("RequiredFiles").array) {
            const auto relative=fs::wide(asset.object.at("RelativePath").text);
            const auto source=std::filesystem::path(fs::wide(entry.object.at("InstallationDirectory").text))/relative;
            const auto target=models/relative;
            std::filesystem::create_directories(target.parent_path()); std::filesystem::copy_file(source,target);
        }
        entry.object.at("InstallationDirectory").text=fs::utf8(models.native()); entries={entry};
        const auto good_inventory=json(inventory);
        write(data/L"model-inventory.json",good_inventory);
        const std::string good_selection="{\"SchemaVersion\":1,\"SelectedModelAlias\":\"english_2026-10\"}";
        write(data/L"model-selection.json",good_selection);
        outloud::testing::selection_roots(data,voices);
        const auto source=scratch/L"unrelated-name.safetensors"; profile(source);
        outloud::VoiceLibrary library(voices); const auto imported=library.import_native_profile(source,"Native fixture").voice;
        check(imported.health==outloud::VoiceHealth::valid && !imported.managed_native_path.empty(),"Native import has no profile path");
        const auto metadata=read(imported.managed_native_path.parent_path()/L"metadata.json");
        check(metadata.find("model")==metadata.npos && metadata.find("revision")==metadata.npos && metadata.find("provenance")==metadata.npos,"Voice acquired model provenance");
        const auto output=scratch/L"export.safetensors";
        check(library.export_native_profile(imported.library_id,output)==imported.metadata->profile_sha256 && read(output)==read(source),"Native export changed bytes");
        rejected([&] { (void)library.export_native_profile(imported.library_id,scratch/L"wrong.olv"); },"Native export allowed OLV");
        auto registry=std::make_shared<Registry>();
        outloud::SapiVoiceRuntime runtime{scratch,scratch/L"bin/OutLoudPocketSAPI.dll",std::filesystem::absolute(argv[1])};
        outloud::SapiVoiceManager manager(library,runtime,registry,outloud::MachineVoiceLibraryPolicy::development_override);
        (void)manager.install(imported.library_id);
        check(manager.install(imported.library_id)==outloud::SapiTokenChange::already_installed,"Native install was not idempotent");
        check(manager.inspect(imported.library_id).status==outloud::SapiTokenStatus::correct,"Native registration reconciliation failed");
        (void)library.rename_voice(imported.library_id,"Renamed native fixture");
        check(manager.inspect(imported.library_id).status==outloud::SapiTokenStatus::stale_name,"Native rename was not detected");
        check(manager.update(imported.library_id)==outloud::SapiTokenChange::updated,"Native token rename failed");
        const auto token=registry->tokens.begin()->second;
        check(outloud::sapi_registry_text(token.values.at(L"Backend"))==L"Rust" && !token.values.contains(L"ModelPath") && !token.values.contains(L"RustConfigPath"),"Token acquired a fixed model");
        auto lease=outloud::RustModelSelection::acquire(argv[1],imported.managed_native_path,imported.library_id);
        const auto yaml=read(lease->options().configuration);
        check(yaml.find("hf://")==yaml.npos && yaml.find("https://")==yaml.npos,"Local YAML contains remote assets");
        rejected([&] { auto lock=fs::file(models/L".operations.lock",GENERIC_READ|GENERIC_WRITE,0); },"Model removal could race a live resident");
        rejected([&] { auto writer=fs::file(imported.managed_native_path,GENERIC_WRITE,FILE_SHARE_READ); },"Voice replacement could race a live resident");
        rejected([&] { auto writer=fs::file(lease->options().configuration,GENERIC_WRITE,FILE_SHARE_READ); },"Configuration replacement could race a live resident");
        std::uint64_t samples=0;
        outloud::RustRuntimeSession session;
        session.start(lease->options()); check(session.is_ready(),"Official runtime did not accept independent native profile");
        const auto pcm=session.read_pcm("This is a local model selection check.",sink,&samples);
        check(samples>0 && pcm.samples==samples,"Existing official PCM stream failed"); session.stop();
        const auto config_path=lease->options().configuration;
        const auto first_identity=lease->identity(); lease.reset(); check(!std::filesystem::exists(config_path),"Owned YAML was not cleaned");
        {
            // Isolated installation receipt with a different release label. The
            // executable is the existing official runtime, not a downloaded update.
            const std::string hash="813cabfc2f8fc7a4e5310deda6025426b3164610dd9f3c3098a6078e5cc1895a";
            const std::string license_hash="36516aefdc84c5d5a1e7485425913a22dbda69eb1930c5e84d6ae4972b5194b9";
            const std::string release="v99.0.0", asset="pocket-tts-99.0.0-x86_64-pc-windows-msvc.zip";
            auto directory_hash=hash;
            for (auto& ch:directory_hash) { if (ch>='a' && ch<='f') ch=static_cast<char>(ch-'a'+'A'); }
            const auto directory=data/L"runtimes"/fs::wide(release)/fs::wide(directory_hash);
            std::filesystem::create_directories(directory);
            const auto updated=directory/L"pocket-tts.exe", license=directory/L"LICENSE", receipt=directory/L"runtime-installation.json";
            std::filesystem::copy_file(argv[1],updated);
            std::filesystem::copy_file(std::filesystem::path(argv[1]).parent_path()/L"LICENSE",license);
            const auto identity="{\"ReleaseVersion\":"+quote(release)+",\"ExecutableSha256\":"+quote(hash)+"}";
            const auto installation="{\"ReleaseVersion\":"+quote(release)+",\"ExecutableSha256\":"+quote(hash)+
                ",\"ArchiveSha256\":"+quote(std::string(64,'0'))+",\"AssetId\":1,\"AssetName\":"+quote(asset)+
                ",\"AssetUrl\":"+quote("https://github.com/kyutai-labs/pocket-tts/releases/download/"+release+"/"+asset)+
                ",\"LicenseSha256\":"+quote(license_hash)+"}";
            write(receipt,installation);write(data/L"runtime-identity.json",identity);
            auto updated_lease=outloud::RustModelSelection::acquire(scratch/L"missing-baseline.exe",imported.managed_native_path,imported.library_id);
            check(updated_lease->options().executable==updated,"SAPI ignored the Settings runtime or rejected its release label");
            check(updated_lease->identity()!=first_identity,"Runtime selection did not invalidate the resident identity");
            for (const auto& path:{updated,license,receipt}) {
                rejected([&] { auto writer=fs::file(path,GENERIC_WRITE,FILE_SHARE_READ); },"Updated runtime resources were not leased");
            }
            session.start(updated_lease->options());
            check(session.is_ready(),"Selected installed runtime did not start");session.stop();updated_lease.reset();
            const auto license_bytes=read(license);write(license,license_bytes+"changed");
            rejected([&] { (void)outloud::RustModelSelection::acquire(argv[1],imported.managed_native_path,imported.library_id); },"Changed runtime license was accepted");
            write(license,license_bytes);
            std::filesystem::rename(receipt,directory/L"saved-receipt.json");
            rejected([&] { (void)outloud::RustModelSelection::acquire(argv[1],imported.managed_native_path,imported.library_id); },"Missing runtime receipt fell back to baseline");
            std::filesystem::rename(directory/L"saved-receipt.json",receipt);
            write(updated,"changed executable");
            rejected([&] { (void)outloud::RustModelSelection::acquire(argv[1],imported.managed_native_path,imported.library_id); },"Changed selected executable fell back to baseline");
            write(data/L"runtime-identity.json","{\"ReleaseVersion\":\"../outside\",\"ExecutableSha256\":"+quote(hash)+"}");
            rejected([&] { (void)outloud::RustModelSelection::acquire(argv[1],imported.managed_native_path,imported.library_id); },"Runtime identity escaped protected storage");
            std::filesystem::remove(data/L"runtime-identity.json");
            check(outloud::RustModelSelection::selected_identity()==first_identity,"Runtime fixture changed the original selection");
        }
        {
            std::stop_source cancellation;
            std::jthread cancel([&] { std::this_thread::sleep_for(std::chrono::milliseconds(2)); cancellation.request_stop(); });
            bool stopped=false;
            try { auto pending=outloud::RustModelSelection::acquire(argv[1],imported.managed_native_path,imported.library_id,cancellation.get_token()); }
            catch (const outloud::RustSelectionCancelled&) { stopped=true; }
            check(stopped,"Asset verification did not observe cancellation");
            auto writer=fs::file(models/L".operations.lock",GENERIC_READ|GENERIC_WRITE,0);
            check(writer.value!=INVALID_HANDLE_VALUE,"Cancelled preparation retained its model lease");
        }
        if (argc==5) {
            check(_wspawnl(_P_WAIT,argv[3],argv[3],L"--managed-rust",argv[4],argv[1],data.c_str(),imported.managed_native_path.c_str(),nullptr)==0,
                "Managed SAPI streaming/cancellation checks failed");
        }
        write(data/L"model-selection.json","{\"SchemaVersion\":1,\"SelectedModelAlias\":null}");
        rejected([&] { (void)outloud::RustModelSelection::acquire(argv[1],imported.managed_native_path,imported.library_id); },"Absent selection selected a replacement");
        write(data/L"model-selection.json",good_selection);
        check(outloud::RustModelSelection::selected_identity()==first_identity,"Selection identity was not stable");
        auto invalid_inventory=inventory; invalid_inventory.object.at("Models").array[0].object.at("ConfigurationYaml").text+="\nmalicious_path: https://example.invalid/model\n";
        write(data/L"model-inventory.json",json(invalid_inventory));
        rejected([&] { (void)outloud::RustModelSelection::acquire(argv[1],imported.managed_native_path,imported.library_id); },"Editable inventory YAML bypassed official authority");
        write(data/L"model-inventory.json",good_inventory);
        auto invalid_manifest=inventory; invalid_manifest.object.at("Models").array[0].object.at("RequiredFiles").array[0].object.at("ExpectedHash").text=std::string(64,'0');
        write(data/L"model-inventory.json",json(invalid_manifest));
        rejected([&] { (void)outloud::RustModelSelection::acquire(argv[1],imported.managed_native_path,imported.library_id); },"Editable inventory hash became a trust authority");
        write(data/L"model-inventory.json",good_inventory);
        const auto bad_source=scratch/L"other-revision.safetensors"; profile(bad_source,false);
        const auto incompatible=library.import_native_profile(bad_source,"Structurally different").voice;
        auto incompatible_lease=outloud::RustModelSelection::acquire(argv[1],incompatible.managed_native_path,incompatible.library_id);
        rejected([&] { session.start(incompatible_lease->options()); },"Official runtime accepted a missing attention layer");
        session.stop(); incompatible_lease.reset();
        (void)manager.remove(imported.library_id); (void)library.remove_voice(imported.library_id);
        (void)library.remove_voice(incompatible.library_id);
        std::filesystem::remove_all(scratch);
        std::cout<<"PASS: "<<checks<<" native-profile, Settings selection, inventory trust, leases, official runtime startup/PCM/structural rejection, registration and unchanged export checks.\n";
        return 0;
    } catch (const outloud::VoiceLibraryError& error) {
        std::cerr<<"FAIL: "<<error.what()<<" path="<<fs::utf8(error.path().native())<<" win32="<<error.system_error()<<'\n'; return 1;
    } catch (const std::exception& error) { std::cerr<<"FAIL: "<<error.what()<<'\n'; return 1; }
}
