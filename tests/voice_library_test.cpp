#include "outloud/voice_library.hpp"
#include "asset_io.hpp"
#include "voice_library_fs.hpp"
#include "../frontend/mutation/mutation.hpp"
#include <cstring>
#include <fstream>
#include <iostream>

namespace {
using namespace outloud;
namespace fs=detail::library_fs;
unsigned checks=0;
void require(bool value,const char* message) { ++checks; if (!value) { throw std::runtime_error(message); } }
template<class F> void rejected(F action,const char* message) {
    bool failed=false; try { action(); } catch (const std::exception&) { failed=true; } require(failed,message);
}
std::string bytes(const std::filesystem::path& path) {
    std::ifstream stream(path,std::ios::binary); return {std::istreambuf_iterator<char>(stream),{}};
}
void write(const std::filesystem::path& path,std::string_view data) {
    std::ofstream stream(path,std::ios::binary|std::ios::trunc); stream.write(data.data(),static_cast<std::streamsize>(data.size()));
    require(bool(stream),"Cannot write isolated fixture");
}
std::string profile(std::string header="{\"__metadata__\":{\"opaque\":\"unchanged\"},\"tensor\":{\"dtype\":\"BF16\",\"shape\":[2],\"data_offsets\":[0,4]}}") {
    const auto size=static_cast<std::uint64_t>(header.size());
    std::string data(reinterpret_cast<const char*>(&size),sizeof(size)); data+=header; data.append("\0\1\2\3",4); return data;
}
std::vector<std::uint8_t> request(std::uint32_t operation,const std::vector<std::string>& fields) {
    namespace wire=outloud::frontend::mutation;
    std::vector<std::uint8_t> result;
    const auto number=[&](std::uint32_t value) {
        const auto* data=reinterpret_cast<const std::uint8_t*>(&value); result.insert(result.end(),data,data+4);
    };
    number(wire::magic); number(wire::version); number(operation); number(0);
    for (const auto& field:fields) { number(static_cast<std::uint32_t>(field.size())); result.insert(result.end(),field.begin(),field.end()); }
    const auto size=static_cast<std::uint32_t>(result.size()-16); std::memcpy(result.data()+12,&size,4); return result;
}
}
int main() {
    std::filesystem::path scratch;
    try {
        scratch=std::filesystem::temp_directory_path()/(L"outloud-native-library-"+fs::wide(fs::random_id()));
        require(std::filesystem::create_directory(scratch),"Cannot create isolated test directory");
        const auto input=scratch/L"native.safetensors"; const auto original=profile(); write(input,original);
        namespace wire=outloud::frontend::mutation;
        const std::vector<std::string> fields={fs::utf8(input.native()),"Native fixture"};
        const auto native=wire::decode_request(request(7,fields));
        require(native.kind==wire::Operation::import_native && native.source==input && native.display_name=="Native fixture","Native helper framing changed");
        for (const auto operation : {1u,2u,4u,5u}) {
            rejected([&] { (void)wire::decode_request(request(operation,fields)); },"Legacy helper operation accepted");
        }
        rejected([&] { (void)wire::decode_request(request(7,{fs::utf8((scratch/L"unsupported.olv").native()),"Legacy"})); },"Native helper accepted a legacy file");
        const auto remove=wire::decode_request(request(3,{std::string(32,'a'),"Native fixture",std::string(64,'a'),"valid"}));
        require(remove.kind==wire::Operation::remove && remove.expected_hash==std::string(64,'a'),"Removal snapshot framing changed");
        const auto digest=validate_native_profile(input);
        require(digest==detail::hex(detail::sha256({reinterpret_cast<const unsigned char*>(original.data()),original.size()})),"Native digest changed");
        std::stop_source stop; stop.request_stop();
        rejected([&] { (void)validate_native_profile(input,stop.get_token()); },"Verification ignored cancellation");
        const auto invalid=scratch/L"invalid.safetensors";
        for (const auto header : {"{}", "{\"x\":{\"dtype\":\"F32\",\"shape\":[2],\"data_offsets\":[0,4]}}", "{\"x\":{\"dtype\":\"F32\",\"shape\":[1],\"data_offsets\":[1,5]}}", "{\"__metadata__\":{\"x\":1},\"x\":{\"dtype\":\"F32\",\"shape\":[1],\"data_offsets\":[0,4]}}"}) {
            write(invalid,profile(header)); rejected([&] { (void)validate_native_profile(invalid); },"Malformed container accepted");
        }
        VoiceLibrary library(scratch/L"library"); const auto imported=library.import_native_profile(input,"  Native fixture  ").voice;
        const auto id=imported.library_id; const auto entry=imported.managed_native_path.parent_path();
        require(imported.health==VoiceHealth::valid && imported.metadata->display_name=="Native fixture","Native import/name behavior changed");
        require(imported.metadata->schema_version==2 && imported.metadata->content_sha256()==digest && bytes(imported.managed_native_path)==original,"Native identity or bytes changed");
        const auto metadata=bytes(entry/L"metadata.json");
        require(metadata.find("model")==metadata.npos && metadata.find("revision")==metadata.npos && metadata.find("compatibility")==metadata.npos,"Native metadata acquired a model association");
        rejected([&] { (void)library.import_native_profile(input,"Duplicate"); },"Duplicate content accepted implicitly");
        const auto separate=library.import_native_profile(input,"Separate",DuplicateContentPolicy::allow_separate_entry).voice;
        require(separate.library_id!=id && library.list_voices().size()==2,"Explicit separate entry changed");
        const auto output=scratch/L"export.safetensors";
        VoiceExportOptions options; options.expected_display_name="Native fixture"; options.expected_content_sha256=digest;
        options.expected_destination=inspect_native_export_destination(output);
        require(library.export_native_profile(id,output,options)==digest && bytes(output)==original,"Export changed native bytes");
        rejected([&] { (void)library.export_native_profile(id,output,options); },"Changed destination was overwritten");
        options.replace_existing=true; options.expected_destination=inspect_native_export_destination(output);
        require(library.export_native_profile(id,output,options)==digest,"Confirmed overwrite failed");
        options.expected_content_sha256=std::string(64,'0');
        rejected([&] { (void)library.export_native_profile(id,output,options); },"Stale selected content was exported");
        options.expected_content_sha256=digest; options.expected_display_name="Stale name";
        rejected([&] { (void)library.export_native_profile(id,output,options); },"Stale selected name was exported");
        rejected([&] { (void)library.export_native_profile(id,scratch/L"unsupported.olv"); },"Legacy export accepted");
        rejected([&] { (void)library.export_native_profile(id,entry/L"forbidden.safetensors"); },"Export entered managed storage");
        {
            detail::MappedFile mapping(imported.managed_native_path,1024*1024);
            rejected([&] { auto writer=fs::file(imported.managed_native_path,GENERIC_WRITE,FILE_SHARE_READ); },"File mapping allowed a competing writer");
            rejected([&] { (void)library.remove_voice(id); },"Mapped voice was deleted");
        }
        {
            auto lock=fs::file(library.root()/L".library.lock",GENERIC_READ|GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE);
            OVERLAPPED overlap{};
            require(LockFileEx(lock.value,LOCKFILE_EXCLUSIVE_LOCK|LOCKFILE_FAIL_IMMEDIATELY,0,1,0,&overlap)!=0,"Cannot hold library lock");
            rejected([&] { (void)library.list_voices(); },"Library ignored a concurrent exclusive operation");
        }
        const auto renamed=library.rename_voice(id,"Renamed");
        require(renamed.metadata->content_sha256()==digest && bytes(imported.managed_native_path)==original,"Rename changed profile identity");
        VoiceRemoveOptions removal; removal.expected_display_name="Renamed"; removal.expected_content_sha256=std::string(64,'0'); removal.expected_health="valid";
        rejected([&] { (void)library.remove_voice(id,removal); },"Removal accepted stale content");
        removal.expected_content_sha256=digest;
        std::string registration(64,'a');
        removal.registration_identity=[&](std::string_view) { return registration; };
        removal.remove_registration=[](const LibraryVoice&,std::string_view) { throw std::runtime_error("isolated registry refusal"); };
        rejected([&] { (void)library.remove_voice(id,removal); },"Registration failure did not reject removal");
        require(library.inspect_voice(id).health==VoiceHealth::valid,"Failed registration removal lost the profile");
        removal.remove_registration=[&](const LibraryVoice&,std::string_view) { registration="absent"; };
        (void)library.remove_voice(id,removal);
        require(!std::filesystem::exists(entry),"Coordinated native removal retained an active entry");
        const auto other=separate.managed_native_path.parent_path(); write(other/L"unknown.bin","must be retained");
        rejected([&] { (void)library.remove_voice(separate.library_id); },"Removal deleted unknown data");
        require(bytes(other/L"unknown.bin")=="must be retained","Unknown data changed");
        std::filesystem::remove(other/L"unknown.bin"); // only this check's explicitly created fixture
        (void)library.remove_voice(separate.library_id);
        require(library.list_voices().empty(),"Native removal left active entries");
        const auto legacy=library.root()/L"voices"/std::wstring(32,L'a'); std::filesystem::create_directory(legacy);
        write(legacy/L"metadata.json","{\"schema_version\":1}"); write(legacy/L"voice.olv","untouched legacy data");
        require(library.inspect_voice(std::string(32,'a')).health==VoiceHealth::unsupported_metadata_version,"Unsupported metadata was interpreted");
        rejected([&] { (void)library.remove_voice(std::string(32,'a')); },"Legacy storage was deleted");
        require(bytes(legacy/L"voice.olv")=="untouched legacy data","Legacy data changed");
        require(std::filesystem::equivalent(scratch.parent_path(),std::filesystem::temp_directory_path()) && scratch.filename().native().starts_with(L"outloud-native-library-"),"Scratch cleanup escaped its isolated root");
        std::filesystem::remove_all(scratch); scratch.clear();
        std::cout << "PASS: " << checks << " native profile, byte identity, validation, export, locking and removal checks; isolated temporary storage only.\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << "\nRetained isolated fixture: " << scratch << '\n'; return 1; }
}
