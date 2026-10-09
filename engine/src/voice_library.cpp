#include "outloud/voice_library.hpp"
#include "voice_library_fs.hpp"
#include "voice_library_security.hpp"
#include "voice_json.hpp"
#include "replacement_audit.hpp"
#include "voice_refresh_audit.hpp"

#include <shlobj.h>
#include <chrono>
#include <iomanip>
#include <sstream>
#include <tuple>
#include <cstring>
#include <set>

namespace outloud {
VoiceLibraryError::VoiceLibraryError(VoiceLibraryErrorCode code,std::string message,
    std::filesystem::path path,std::uint32_t system_error,std::optional<VoiceImportError> import_error,
    std::vector<std::string> duplicate_ids)
    : std::runtime_error(std::move(message)),code_(code),path_(std::move(path)),system_error_(system_error),
      import_error_(std::move(import_error)),duplicate_ids_(std::move(duplicate_ids)) {}
std::string_view voice_library_error_name(VoiceLibraryErrorCode code) noexcept {
    switch (code) {
#define OUTLOUD_LIBRARY_CODE(name) case VoiceLibraryErrorCode::name: return #name
    OUTLOUD_LIBRARY_CODE(invalid_library_root); OUTLOUD_LIBRARY_CODE(library_unavailable);
    OUTLOUD_LIBRARY_CODE(library_busy); OUTLOUD_LIBRARY_CODE(unsafe_path); OUTLOUD_LIBRARY_CODE(invalid_id);
    OUTLOUD_LIBRARY_CODE(voice_not_found); OUTLOUD_LIBRARY_CODE(metadata_corrupt);
    OUTLOUD_LIBRARY_CODE(unsupported_metadata_version); OUTLOUD_LIBRARY_CODE(invalid_display_name);
    OUTLOUD_LIBRARY_CODE(duplicate_content); OUTLOUD_LIBRARY_CODE(import_failed);
    OUTLOUD_LIBRARY_CODE(incompatible_voice); OUTLOUD_LIBRARY_CODE(corrupt_voice);
    OUTLOUD_LIBRARY_CODE(replace_required); OUTLOUD_LIBRARY_CODE(source_io);
    OUTLOUD_LIBRARY_CODE(destination_io); OUTLOUD_LIBRARY_CODE(remove_failed); OUTLOUD_LIBRARY_CODE(internal_failure);
    OUTLOUD_LIBRARY_CODE(library_access_denied); OUTLOUD_LIBRARY_CODE(library_security_invalid);
    OUTLOUD_LIBRARY_CODE(voice_in_use); OUTLOUD_LIBRARY_CODE(replacement_conflict); OUTLOUD_LIBRARY_CODE(replacement_pending);
    OUTLOUD_LIBRARY_CODE(removal_conflict); OUTLOUD_LIBRARY_CODE(removal_partial); OUTLOUD_LIBRARY_CODE(removal_pending);
#undef OUTLOUD_LIBRARY_CODE
    }
    return "internal_failure";
}
std::string_view voice_health_name(VoiceHealth health) noexcept {
    switch (health) {
#define OUTLOUD_HEALTH(name) case VoiceHealth::name: return #name
    OUTLOUD_HEALTH(valid); OUTLOUD_HEALTH(missing_voice); OUTLOUD_HEALTH(corrupt_voice);
    OUTLOUD_HEALTH(metadata_corrupt); OUTLOUD_HEALTH(unsupported_metadata_version);
    OUTLOUD_HEALTH(unsupported_voice_version); OUTLOUD_HEALTH(incompatible_model);
    OUTLOUD_HEALTH(hash_mismatch); OUTLOUD_HEALTH(unsafe_entry); OUTLOUD_HEALTH(unavailable);
    OUTLOUD_HEALTH(replacement_pending); OUTLOUD_HEALTH(removal_pending);
#undef OUTLOUD_HEALTH
    }
    return "unavailable";
}
namespace {
namespace fs=detail::library_fs;
using Code=VoiceLibraryErrorCode;
using detail::Bytes;
using Json=detail::voice_json::Value;
constexpr std::uint64_t maximum_voice=1024ULL*1024*1024+256;
struct ValidationFailure { std::exception_ptr error; };
template<class Function> auto guarded(const std::filesystem::path& path,Function function) {
    try { return function(); }
    catch (const VoiceLibraryError&) { throw; }
    catch (const ValidationFailure& error) { std::rethrow_exception(error.error); }
    catch (const VoiceImportError& error) {
        throw VoiceLibraryError(error.system_error()==ERROR_ACCESS_DENIED ? Code::library_access_denied : Code::import_failed,
            error.what(),error.path(),error.system_error(),error);
    } catch (const std::exception& error) { throw VoiceLibraryError(Code::internal_failure,error.what(),path); }
}
bool space(std::uint32_t cp) {
    return cp==32 || (cp>=9&&cp<=13) || cp==0x85 || cp==0xa0 || cp==0x1680 ||
        (cp>=0x2000&&cp<=0x200a) || cp==0x2028 || cp==0x2029 || cp==0x202f || cp==0x205f || cp==0x3000;
}
std::string display_name(std::string_view text,const std::filesystem::path& path) {
    if (text.empty() || text.size()>1024 || !detail::valid_utf8(text)) { fs::fail(Code::invalid_display_name,"Name must be bounded valid UTF-8",path); }
    const auto chars=detail::characters(text);
    if (chars.size()>256) { fs::fail(Code::invalid_display_name,"Name exceeds 256 Unicode scalars",path); }
    for (const auto& ch : chars) {
        if (ch.cp<32 || (ch.cp>=0x7f&&ch.cp<=0x9f) || ch.cp==0x2028 || ch.cp==0x2029) {
            fs::fail(Code::invalid_display_name,"Name contains unusable control/line characters",path);
        }
    }
    std::size_t first=0,last=chars.size();
    while (first<last && space(chars[first].cp)) { ++first; }
    while (last>first && space(chars[last-1].cp)) { --last; }
    if (first==last) { fs::fail(Code::invalid_display_name,"Name cannot be whitespace only",path); }
    return std::string(text.substr(chars[first].offset,chars[last-1].offset+chars[last-1].size-chars[first].offset));
}
bool hash(std::string_view value) {
    return value.size()==64 && std::all_of(value.begin(),value.end(),[](char ch) { return (ch>='0'&&ch<='9')||(ch>='a'&&ch<='f'); });
}
std::string utc() {
    SYSTEMTIME time{}; GetSystemTime(&time); std::ostringstream out; out<<std::setfill('0')
        <<std::setw(4)<<time.wYear<<'-'<<std::setw(2)<<time.wMonth<<'-'<<std::setw(2)<<time.wDay<<'T'
        <<std::setw(2)<<time.wHour<<':'<<std::setw(2)<<time.wMinute<<':'<<std::setw(2)<<time.wSecond<<'Z';
    return out.str();
}
bool timestamp(std::string_view text) {
    if (text.size()!=20 || text[4]!='-' || text[7]!='-' || text[10]!='T' || text[13]!=':' || text[16]!=':' || text[19]!='Z') { return false; }
    for (std::size_t i=0;i<19;++i) {
        if (i==4||i==7||i==10||i==13||i==16) { continue; }
        if (text[i]<'0'||text[i]>'9') { return false; }
    }
    auto two=[&](std::size_t at) { return unsigned(text[at]-'0')*10+unsigned(text[at+1]-'0'); };
    const auto year=int(two(0)*100+two(2));
    return year>=1601 && std::chrono::year_month_day(std::chrono::year(year),std::chrono::month(two(5)),std::chrono::day(two(8))).ok()
        && two(11)<24 && two(14)<60 && two(17)<60;
}
std::string quote(std::string_view text) {
    std::string out="\"";
    constexpr char hex[]="0123456789abcdef";
    for (unsigned char ch : text) {
        if (ch=='"'||ch=='\\') { out+='\\'; out+=static_cast<char>(ch); }
        else if (ch<32) { out+="\\u00"; out+=hex[ch>>4]; out+=hex[ch&15]; }
        else { out+=static_cast<char>(ch); }
    }
    out+='"'; return out;
}
std::string serialize(const VoiceMetadata& m) {
    return "{\n  \"schema_version\":1,\n  \"library_id\":"+quote(m.library_id)+
        ",\n  \"display_name\":"+quote(m.display_name)+",\n  \"olv_version\":1,\n  \"olv_sha256\":"+quote(m.olv_sha256)+
        ",\n  \"source_sha256\":"+quote(m.source_sha256)+",\n  \"compatibility_id\":"+quote(m.compatibility_id)+
        ",\n  \"imported_utc\":"+quote(m.imported_utc)+",\n  \"source_filename\":"+quote(m.source_filename)+
        ",\n  \"import_kind\":"+quote(m.import_kind==VoiceImportKind::pocket_prepared?"pocket_prepared":"olv")+
        ",\n  \"model_provenance_verified\":false\n}\n";
}
VoiceMetadata metadata(const std::filesystem::path& path,std::string_view expected_id) {
    detail::refresh_audit::event(detail::refresh_audit::Event::metadata);
    auto file=fs::file(path); const auto bytes=fs::read(file.value,path,16*1024);
    Json root;
    try { root=detail::voice_json::Parser({reinterpret_cast<const char*>(bytes.data()),bytes.size()}).parse(); }
    catch (const std::invalid_argument& error) { throw VoiceLibraryError(Code::metadata_corrupt,error.what(),path); }
    auto bad=[&] { fs::fail(Code::metadata_corrupt,"Invalid strict voice metadata schema",path); };
    if (root.kind!=Json::Kind::object) { bad(); }
    auto field=[&](const char* name)->const Json& {
        const auto found=root.object.find(name); if (found==root.object.end()) { bad(); }
        return found->second;
    };
    auto integer=[&](const char* name) {
        std::uint64_t value=0; if (!detail::voice_json::unsigned_integer(field(name),value)) { bad(); } return value;
    };
    auto string=[&](const char* name) {
        const auto& value=field(name); if (value.kind!=Json::Kind::string) { bad(); } return value.text;
    };
    if (integer("schema_version")!=1) { fs::fail(Code::unsupported_metadata_version,"Unsupported metadata version; no automatic migration",path); }
    if (root.object.size()!=11) { bad(); }
    VoiceMetadata result; result.library_id=string("library_id"); result.display_name=string("display_name");
    const auto version=integer("olv_version"); if (!version || version>UINT32_MAX) { bad(); }
    result.olv_version=static_cast<std::uint32_t>(version);
    result.olv_sha256=string("olv_sha256"); result.source_sha256=string("source_sha256"); result.compatibility_id=string("compatibility_id");
    result.imported_utc=string("imported_utc"); result.source_filename=string("source_filename"); const auto kind=string("import_kind");
    const auto& provenance=field("model_provenance_verified");
    if (provenance.kind!=Json::Kind::literal || provenance.text!="false" ||
        result.library_id!=expected_id || !fs::id(result.library_id) || !hash(result.olv_sha256) ||
        !hash(result.source_sha256) || !hash(result.compatibility_id) || !timestamp(result.imported_utc) ||
        result.source_filename.empty() || result.source_filename.size()>1024 ||
        result.source_filename.find_first_of("/\\:\0",0,4)!=std::string::npos) { bad(); }
    try { if (display_name(result.display_name,path)!=result.display_name) { bad(); } }
    catch (const VoiceLibraryError&) { bad(); }
    if (kind=="pocket_prepared") { result.import_kind=VoiceImportKind::pocket_prepared; }
    else if (kind=="olv") { result.import_kind=VoiceImportKind::olv; }
    else { bad(); }
    return result;
}
void save_metadata(const std::filesystem::path& path,const VoiceMetadata& record,bool machine) {
    const auto text=serialize(record);
    detail::library_security::FileCreationSecurity security(machine);
    auto file=fs::file(path,GENERIC_WRITE,0,CREATE_NEW,Code::destination_io,security.get());
    fs::write(file.value,{reinterpret_cast<const unsigned char*>(text.data()),text.size()},path); file.close();
    (void)metadata(path,record.library_id);
}
struct Session {
    fs::Pins root;
    fs::Handle lock;
    std::optional<fs::Pins> voices,staging,trash;
    bool machine;
    Session(const std::filesystem::path& path,bool mutate,bool machine_storage,bool allow_create = true)
        : root(machine_storage ? detail::library_security::prepare_root(path,mutate,mutate&&allow_create) :
            fs::Pins(path,mutate&&allow_create,!mutate||!allow_create)),machine(machine_storage) {
        if (!root.exists) { return; }
        const auto lock_path=root.path/L".library.lock";
        detail::library_security::FileCreationSecurity security(machine&&mutate);
        lock=fs::file(lock_path,mutate ? GENERIC_READ|GENERIC_WRITE : GENERIC_READ,
            FILE_SHARE_READ|FILE_SHARE_WRITE,mutate ? OPEN_ALWAYS : OPEN_EXISTING,Code::library_unavailable,security.get());
        if (machine) { detail::library_security::validate(lock.value,lock_path,false); }
        OVERLAPPED overlap{};
        if (!LockFileEx(lock.value,LOCKFILE_FAIL_IMMEDIATELY|(mutate ? LOCKFILE_EXCLUSIVE_LOCK : 0),0,1,0,&overlap)) {
            fs::fail(GetLastError()==ERROR_LOCK_VIOLATION?Code::library_busy:Code::library_unavailable,"Library is busy or cannot be locked",lock_path,GetLastError());
        }
        const auto child = [&](std::optional<fs::Pins>& pins,const wchar_t* name) {
            const auto location = root.path/name;
            if (machine && mutate && allow_create && !fs::exists(location)) { detail::library_security::create_directory(location); }
            pins.emplace(location,!machine&&mutate&&allow_create,!mutate||!allow_create);
            if (machine && pins->exists) {
                const auto handle = fs::directory(location,READ_CONTROL);
                detail::library_security::validate(handle.value,location,true);
            }
        };
        child(voices,L"voices");
        // Verify existing internal directories even during read operations.
        child(staging,L".staging"); child(trash,L".trash");
    }
};
bool replacement_pending(Session&, std::string_view);
bool removal_pending(Session&, std::string_view);
LibraryVoice inspect(const std::filesystem::path& entry,const std::string& id,bool machine) {
    LibraryVoice result; result.library_id=id;
    try {
        const auto pinned=fs::directory(entry,machine ? READ_CONTROL : 0);
        if (machine) {
            detail::library_security::validate(pinned.value,entry,true);
            const auto record = fs::file(entry/L"metadata.json");
            detail::library_security::validate(record.value,entry/L"metadata.json",false);
        }
        result.metadata=metadata(entry/L"metadata.json",id);
        const auto path=entry/L"voice.olv";
        if (!fs::exists(path)) { result.health=VoiceHealth::missing_voice; result.detail="Managed voice.olv is missing"; return result; }
        const auto file=fs::file(path);
        detail::refresh_audit::event(detail::refresh_audit::Event::olv_mapping);
        const detail::MappedFile mapping(path,maximum_voice); const auto bytes=mapping.bytes();
        if (machine) { detail::library_security::validate(file.value,path,false); }
        if (bytes.size()>=12 && std::string_view(reinterpret_cast<const char*>(bytes.data()),8)=="OLVPKT01" && detail::u32(bytes,8)!=1) {
            result.health=VoiceHealth::unsupported_voice_version; result.detail="Unsupported .olv format version"; return result;
        }
        detail::refresh_audit::event(detail::refresh_audit::Event::olv_validation);
        const auto validated=validate_olv(path);
        const auto& m=*result.metadata;
        if (m.compatibility_id!=pocket_model_compatibility_id) { result.health=VoiceHealth::incompatible_model; result.detail="Metadata declares an incompatible model"; }
        else if (m.olv_version!=validated.format_version || m.olv_sha256!=validated.file_sha256 ||
                 m.source_sha256!=validated.source_sha256 || m.compatibility_id!=validated.compatibility_id) {
            result.health=VoiceHealth::hash_mismatch; result.detail="Managed asset identity disagrees with metadata";
        } else { result.health=VoiceHealth::valid; result.managed_olv_path=path; }
    } catch (const VoiceImportError& error) {
        result.health=error.code()==VoiceImportErrorCode::incompatible_model?VoiceHealth::incompatible_model:VoiceHealth::corrupt_voice;
        result.detail=error.what();
    } catch (const VoiceLibraryError& error) {
        if (error.code()==Code::library_access_denied || error.code()==Code::library_security_invalid) { throw; }
        if (error.code()==Code::unsafe_path) { result.health=VoiceHealth::unsafe_entry; }
        else if (error.code()==Code::unsupported_metadata_version) { result.health=VoiceHealth::unsupported_metadata_version; }
        else if (error.code()==Code::metadata_corrupt || (!result.metadata && error.code()==Code::source_io && fs::absent(error.system_error()))) {
            result.health=VoiceHealth::metadata_corrupt;
        }
        else { result.health=VoiceHealth::unavailable; }
        result.detail=error.what();
    } catch (const std::exception& error) { result.health=result.metadata?VoiceHealth::corrupt_voice:VoiceHealth::metadata_corrupt; result.detail=error.what(); }
    return result;
}
std::filesystem::path entry_path(Session& session,std::string_view id) {
    if (!fs::id(id)) { fs::fail(Code::invalid_id,"Expected 32 lowercase hexadecimal ID characters",session.root.path); }
    if (!session.root.exists || !session.voices->exists) { fs::fail(Code::voice_not_found,"Voice is not in this library",session.root.path); }
    const auto path=session.voices->path/fs::wide(id);
    if (!fs::exists(path)) { fs::fail(Code::voice_not_found,"Voice is not in this library",path); }
    return path;
}
LibraryVoice healthy(Session& session,std::string_view id) {
    if (replacement_pending(session,id)) { fs::fail(Code::replacement_pending,"Recover the interrupted replacement before mutating this voice",session.root.path); }
    if (removal_pending(session,id)) { fs::fail(Code::removal_pending,"Recover interrupted removal before mutating this voice",session.root.path); }
    auto result=inspect(entry_path(session,id),std::string(id),session.machine);
    if (result.health!=VoiceHealth::valid) {
        auto code=Code::corrupt_voice;
        if (result.health==VoiceHealth::incompatible_model) { code=Code::incompatible_voice; }
        else if (result.health==VoiceHealth::metadata_corrupt) { code=Code::metadata_corrupt; }
        else if (result.health==VoiceHealth::unsupported_metadata_version) { code=Code::unsupported_metadata_version; }
        else if (result.health==VoiceHealth::unsafe_entry) { code=Code::unsafe_path; }
        else if (result.health==VoiceHealth::unavailable) { code=Code::source_io; }
        fs::fail(code,
                 "Managed entry is unhealthy; inspect it before this operation",session.voices->path/fs::wide(id));
    }
    return result;
}
// Preflight all expected objects before any deletion. No recursive traversal or arbitrary metadata paths.
void removal_files(const std::filesystem::path& path,std::vector<fs::Handle>& files,bool machine = false) {
    // Keep acquired handles in the caller's already-constructed scope, including exceptional preflight exits.
    for (const auto& name : fs::children(path)) {
        if (name!=L"voice.olv" && name!=L"metadata.json") { fs::fail(Code::remove_failed,"Unknown files retained for manual review",path/name); }
        auto file=fs::file(path/name,GENERIC_READ|DELETE,FILE_SHARE_READ|FILE_SHARE_DELETE,OPEN_EXISTING,Code::remove_failed);
        if (machine) { detail::library_security::validate(file.value,path/name,false); }
        if (fs::info(file.value,path/name,false).dwFileAttributes&FILE_ATTRIBUTE_READONLY) {
            fs::fail(Code::remove_failed,"Read-only files retained",path/name);
        }
        files.push_back(std::move(file));
    }
}
void finish_removal(const std::filesystem::path& path,fs::Handle& directory,std::vector<fs::Handle>& files) {
    for (auto& file : files) { fs::discard(file.value,path); file.close(); }
    fs::discard(directory.value,path); directory.close();
}
void discard_replacement(const std::filesystem::path&,fs::Handle&,bool);
struct Stage {
    std::filesystem::path path;
    std::string id;
    fs::Handle directory;
    bool published=false;
    bool machine;
    explicit Stage(Session& session) : machine(session.machine) {
        for (unsigned attempt=0;attempt<8;++attempt) {
            id=fs::random_id(); path=session.staging->path/fs::wide(id);
            if (fs::exists(session.voices->path/fs::wide(id))) { continue; }
            if (session.machine) {
                if (fs::exists(path)) { continue; }
                if (!detail::library_security::create_directory(path)) { continue; }
                directory=fs::directory(path,DELETE); return;
            }
            if (CreateDirectoryW(path.c_str(),nullptr)) { directory=fs::directory(path,DELETE); return; }
            if (GetLastError()!=ERROR_ALREADY_EXISTS) { fs::fail_access(Code::destination_io,"Cannot create unique staging directory",path,GetLastError()); }
        }
        fs::fail(Code::destination_io,"Random staging/identity collision",path);
    }
    ~Stage() {
        if (!published && directory.value!=INVALID_HANDLE_VALUE) {
            try {
                if (fs::exists(path/L"old.olv") || fs::exists(path/L"old.json")) {
                    if (!fs::exists(path/L"replace.json")) { discard_replacement(path,directory,machine); } return;
                }
                std::vector<fs::Handle> files; removal_files(path,files,machine); finish_removal(path,directory,files);
            } catch (...) {}
        }
    }
};
std::vector<std::string> duplicates(Session& session,std::string_view file_hash) {
    std::vector<std::string> result;
    for (const auto& name : fs::children(session.voices->path)) {
        try {
            const auto id=fs::utf8(name); if (!fs::id(id)) { continue; }
            const auto path=session.voices->path/name; const auto pinned=fs::directory(path);
            const auto file=fs::file(path/L"voice.olv");
            if (validate_olv(path/L"voice.olv").file_sha256==file_hash) { result.push_back(id); }
        } catch (const std::exception&) {} // Unverifiable assets do not establish duplicate content.
    }
    return result;
}
VoiceMetadata new_metadata(const std::string& id,const std::string& name,const OlvInfo& voice,
                           const std::filesystem::path& source,VoiceImportKind kind) {
    VoiceMetadata result; result.library_id=id; result.display_name=name; result.olv_version=voice.format_version;
    result.olv_sha256=voice.file_sha256; result.source_sha256=voice.source_sha256; result.compatibility_id=voice.compatibility_id;
    result.imported_utc=utc(); result.source_filename=fs::utf8(source.filename().native()); result.import_kind=kind; return result;
}
LibraryImportResult publish(Session& session,Stage& stage,const VoiceMetadata& record,DuplicateContentPolicy policy) {
    auto matches=duplicates(session,record.olv_sha256);
    if (!matches.empty() && policy==DuplicateContentPolicy::reject) {
        throw VoiceLibraryError(Code::duplicate_content,"Duplicate .olv content exists; explicitly allow a separate entry",
                                session.root.path,0,{},std::move(matches));
    }
    save_metadata(stage.path/L"metadata.json",record,session.machine);
    if (session.machine) {
        for (const auto* name : {L"metadata.json",L"voice.olv"}) {
            const auto pinned = fs::file(stage.path/name);
            detail::library_security::validate(pinned.value,stage.path/name,false);
        }
    }
    const auto voice=validate_olv(stage.path/L"voice.olv");
    if (voice.file_sha256!=record.olv_sha256 || voice.source_sha256!=record.source_sha256) {
        fs::fail(Code::corrupt_voice,"Staged voice identity changed",stage.path);
    }
    const auto final=session.voices->path/fs::wide(record.library_id);
    fs::rename(stage.directory.value,final,false,Code::destination_io); stage.published=true;
    return {{record.library_id,record,final/L"voice.olv",VoiceHealth::valid,{}},std::move(matches)};
}
void copy_voice(const std::filesystem::path& source,const std::filesystem::path& target,bool machine) {
    const detail::MappedFile mapping(source,maximum_voice);
    detail::library_security::FileCreationSecurity security(machine);
    auto file=fs::file(target,GENERIC_WRITE,0,CREATE_NEW,Code::destination_io,security.get());
    fs::write(file.value,mapping.bytes(),target);
}
#include "voice_library_replace.inc"
#include "voice_library_remove.inc"
#include "voice_library_snapshot.inc"
} // namespace

std::filesystem::path default_voice_library_root() {
    PWSTR raw=nullptr;
    const auto status=SHGetKnownFolderPath(FOLDERID_ProgramData,KF_FLAG_DONT_VERIFY,nullptr,&raw);
    if (FAILED(status)) { CoTaskMemFree(raw); throw VoiceLibraryError(Code::library_unavailable,"Cannot locate ProgramData",{},static_cast<std::uint32_t>(status)); }
    try { const auto path=std::filesystem::path(raw)/L"OutLoud"/L"VoiceLibrary"; CoTaskMemFree(raw); return path; }
    catch (...) { CoTaskMemFree(raw); throw; }
}
VoiceLibrary::VoiceLibrary(std::filesystem::path root) : root_(guarded(root,[&] { return fs::local(root,Code::invalid_library_root); })) {
    // Resolve existing short-name aliases without following reparse points or creating folders.
    const fs::Pins pinned(root_,false,true);
    auto canonical=fs::final_path(pinned.handles.back().value,root_);
    std::size_t component=0;
    for (const auto& part : root_.relative_path()) {
        if (component++>=pinned.handles.size()-1) { canonical/=part; }
    }
    root_=std::move(canonical);
    // Canonicalize only the known-folder base. Explicit isolated-test
    // libraries must not depend on access to installed production entries.
    const auto production=default_voice_library_root();
    const fs::Pins production_base(production.parent_path().parent_path());
    const auto production_path=fs::final_path(production_base.handles.back().value,production_base.path)/L"OutLoud"/L"VoiceLibrary";
    machine_storage_=root_.native().size()==production_path.native().size() && fs::within(root_,production_path);
    if (root_==root_.root_path() || fs::within(root_,root_.root_path()/L"OutLoud")) {
        fs::fail(Code::invalid_library_root,"Library must be separate from drive root/deployment storage",root_);
    }
    for (const auto* folder : {&FOLDERID_ProgramFiles,&FOLDERID_ProgramFilesX86,&FOLDERID_Windows}) {
        PWSTR raw=nullptr;
        const auto status=SHGetKnownFolderPath(*folder,KF_FLAG_DONT_VERIFY,nullptr,&raw);
        bool protected_root=false;
        try { protected_root=SUCCEEDED(status) && fs::within(root_,std::filesystem::path(raw)); }
        catch (...) { CoTaskMemFree(raw); throw; }
        CoTaskMemFree(raw);
        if (protected_root) { fs::fail(Code::invalid_library_root,"Library must not reside in Windows or Program Files",root_); }
    }
}
std::vector<LibraryVoice> VoiceLibrary::list_voices() const {
    return guarded(root_,[&] { Session session(root_,false,machine_storage_); return list(session); });
}
void VoiceLibrary::with_read_snapshot(const std::function<void(std::vector<LibraryVoice>&&)>& consume) const {
    guarded(root_,[&] {
        Session session(root_,false,machine_storage_); auto voices=list(session);
        try { consume(std::move(voices)); }
        catch (...) { throw ValidationFailure{std::current_exception()}; }
    });
}
LibraryVoice VoiceLibrary::inspect_voice(std::string_view id) const {
    return guarded(root_,[&] { Session session(root_,false,machine_storage_); auto voice=inspect(entry_path(session,id),std::string(id),session.machine);
        if (replacement_pending(session,id)) { voice.health=VoiceHealth::replacement_pending; voice.detail="Interrupted replacement needs cleanup_pending recovery; close active consumers first."; }
        if (removal_pending(session,id)) { voice.health=VoiceHealth::removal_pending; voice.detail="Interrupted removal requires authorized token-aware recovery; data retained."; } return voice; });
}
LibraryReplaceResult VoiceLibrary::replace_pocket_voice(std::string_view id,const std::filesystem::path& input,const VoiceReplaceOptions& options) {
    return guarded(input,[&] { Session session(root_,true,machine_storage_); return replace_prepared(session,id,input,options); });
}
LibraryImportResult VoiceLibrary::import_pocket_voice(const std::filesystem::path& input,std::string_view name,
    const VoiceImportOptions& options,DuplicateContentPolicy policy) {
    return guarded(input,[&] {
        const auto normalized=display_name(name,root_); const auto source=fs::local(input,Code::source_io);
        const fs::Pins parents(source.parent_path()); const auto snapshot=fs::file(source);
        if (options.replace_existing) { fs::fail(Code::replace_required,"Managed imports never replace entries; Step 3A replacement must be false",root_); }
        Session session(root_,true,machine_storage_); Stage stage(session);
        const auto converted=convert_pocket_voice_to_olv(source,stage.path/L"voice.olv",options);
        if (machine_storage_) { detail::library_security::protect_created_file(stage.path/L"voice.olv"); }
        return publish(session,stage,new_metadata(stage.id,normalized,converted.voice,source,VoiceImportKind::pocket_prepared),policy);
    });
}
LibraryImportResult VoiceLibrary::import_olv(const std::filesystem::path& input,std::string_view name,DuplicateContentPolicy policy) {
    return guarded(input,[&] {
        const auto normalized=display_name(name,root_); const auto source=fs::local(input,Code::source_io);
        const fs::Pins parents(source.parent_path()); const auto snapshot=fs::file(source); const auto voice=validate_olv(source);
        Session session(root_,true,machine_storage_); Stage stage(session); copy_voice(source,stage.path/L"voice.olv",machine_storage_);
        return publish(session,stage,new_metadata(stage.id,normalized,voice,source,VoiceImportKind::olv),policy);
    });
}
OlvInfo VoiceLibrary::export_olv(std::string_view id,const std::filesystem::path& output,bool replace) const {
    return guarded(output,[&] {
        Session session(root_,false,machine_storage_); const auto voice=healthy(session,id);
        const auto entry=fs::directory(voice.managed_olv_path.parent_path()); const auto source=fs::file(voice.managed_olv_path);
        const auto destination=fs::local(output,Code::destination_io); const fs::Pins parent(destination.parent_path());
        const auto physical_root=fs::final_path(session.root.handles.back().value,root_);
        const auto physical_parent=fs::final_path(parent.handles.back().value,parent.path);
        if (fs::within(physical_parent,physical_root)) { fs::fail(Code::destination_io,"Export destination must be outside this library",destination); }
        const auto extension=destination.extension().native();
        if (CompareStringOrdinal(extension.c_str(),static_cast<int>(extension.size()),L".olv",4,TRUE)!=CSTR_EQUAL) {
            fs::fail(Code::destination_io,"Export destination must end in .olv",destination);
        }
        if (fs::exists(destination)) {
            const auto target=fs::file(destination);
            if (!replace) { fs::fail(Code::replace_required,"Destination exists; explicit replacement required",destination); }
        }
        const auto temporary=parent.path/(L".outloud-export-"+fs::wide(fs::random_id())+L".tmp");
        auto owner=fs::file(temporary,GENERIC_WRITE|DELETE,0,CREATE_NEW,Code::destination_io);
        const auto original_identity=fs::info(owner.value,temporary,false);
        auto same_file=[&](HANDLE handle) {
            const auto current=fs::info(handle,temporary,false);
            return current.dwVolumeSerialNumber==original_identity.dwVolumeSerialNumber && current.nFileIndexHigh==original_identity.nFileIndexHigh &&
                current.nFileIndexLow==original_identity.nFileIndexLow;
        };
        try {
            const detail::MappedFile mapping(voice.managed_olv_path,maximum_voice);
            fs::write(owner.value,mapping.bytes(),temporary);
            // Verify the copied bytes with the production loader after dropping the writer handle.
            owner.close();
            const auto validated=validate_olv(temporary);
            if (validated.file_sha256!=voice.metadata->olv_sha256) { fs::fail(Code::corrupt_voice,"Export copy identity mismatch",temporary); }
            owner=fs::file(temporary,DELETE,0,OPEN_EXISTING,Code::destination_io);
            if (!same_file(owner.value)) { fs::fail(Code::destination_io,"Export temporary identity changed",temporary); }
            fs::rename(owner.value,destination,replace,Code::destination_io); return validated;
        } catch (...) {
            // Handle-based deletion cannot follow a substituted path to another file.
            if (owner.value!=INVALID_HANDLE_VALUE) {
                try { if (same_file(owner.value)) { fs::discard(owner.value,temporary); } } catch (...) {}
            }
            else {
                try {
                    auto cleanup=fs::file(temporary,DELETE,0,OPEN_EXISTING,Code::destination_io);
                    if (same_file(cleanup.value)) { fs::discard(cleanup.value,temporary); }
                } catch (...) {}
            }
            throw;
        }
    });
}
LibraryVoice VoiceLibrary::rename_voice(std::string_view id,std::string_view name) {
    return guarded(root_,[&] {
        const auto normalized=display_name(name,root_); Session session(root_,true,machine_storage_); auto voice=healthy(session,id);
        const auto entry=fs::directory(voice.managed_olv_path.parent_path()); const auto source=fs::file(voice.managed_olv_path);
        if (validate_olv(voice.managed_olv_path).file_sha256!=voice.metadata->olv_sha256) {
            fs::fail(Code::corrupt_voice,"Managed voice changed before rename",voice.managed_olv_path);
        }
        Stage stage(session); voice.metadata->display_name=normalized;
        const auto temporary=stage.path/L"metadata.json"; save_metadata(temporary,*voice.metadata,machine_storage_);
        if (machine_storage_) {
            const auto record = fs::file(temporary); detail::library_security::validate(record.value,temporary,false);
        }
        auto owner=fs::file(temporary,DELETE,0,OPEN_EXISTING,Code::destination_io);
        fs::rename(owner.value,voice.managed_olv_path.parent_path()/L"metadata.json",true,Code::destination_io);
        return voice;
    });
}
VoiceRemovalResult VoiceLibrary::remove_voice(std::string_view id) {
    return remove_voice(id,{});
}
VoiceRemovalResult VoiceLibrary::remove_voice(std::string_view id,const VoiceRemoveOptions& options) {
    return guarded(root_,[&] { Session session(root_,true,machine_storage_); return remove_prepared(session,id,options); });
}
RemovalRecoveryResult VoiceLibrary::recover_removal(std::string_view id,const RemovalRegistrationIdentity& query) {
    return guarded(root_,[&] {
        if (!fs::id(id)) { fs::fail(Code::invalid_id,"Invalid recovery library ID",root_); }
        Session session(root_,true,machine_storage_,false); const auto journals=removal_transactions(session,id);
        if (journals.empty()) { return RemovalRecoveryResult{}; }
        auto directory=fs::directory(journals[0],DELETE|READ_CONTROL);
        return recover_remove(session,journals[0],directory,query);
    });
}
LibraryCleanupResult VoiceLibrary::cleanup_pending(const RemovalRegistrationIdentity& query) {
    return guarded(root_,[&] {
        Session session(root_,true,machine_storage_,false); LibraryCleanupResult result;
        if (!session.root.exists) { return result; }
        for (const auto& folder : {L".staging",L".trash"}) {
            fs::Pins parent(root_/folder,false,true); if (!parent.exists) { continue; }
            for (const auto& name : fs::children(parent.path)) {
                const auto path=parent.path/name;
                try {
                    if (!fs::id(fs::utf8(name))) { fs::fail(Code::removal_pending,"Unknown pending directory name retained",path); }
                    auto directory=fs::directory(path,DELETE|READ_CONTROL);
                    if (machine_storage_) { detail::library_security::validate(directory.value,path,true); }
                    if (folder==std::wstring_view(L".trash")) {
                        if (fs::exists(path/L"remove.json")) {
                            const auto journal=load_remove(path/L"remove.json",machine_storage_);
                            if (!removal_transactions(session,journal.id,path).empty()) { fs::fail(Code::removal_pending,"Multiple removal journals for one ID; retained",path); }
                            const auto recovered=recover_remove(session,path,directory,query);
                            if (recovered.cleanup_pending) { fs::fail(Code::removal_pending,recovered.detail.c_str(),path); }
                        } else if (fs::children(path).empty()) { fs::discard(directory.value,path); directory.close(); }
                        else { fs::fail(Code::removal_pending,"Unjournaled trash outcome is unknown; files retained",path); }
                        ++result.cleaned; continue;
                    }
                    if (fs::exists(path/L"replace.json")) {
                        recover_replacement(session,path,directory); ++result.cleaned; continue;
                    }
                    if (fs::exists(path/L"commit")) {
                        discard_replacement(path,directory,machine_storage_); ++result.cleaned; continue;
                    }
                    std::vector<fs::Handle> files; removal_files(path,files,machine_storage_);
                    finish_removal(path,directory,files); ++result.cleaned;
                } catch (const VoiceLibraryError& error) { result.pending.push_back(path); result.problems.push_back(error); }
                catch (const ValidationFailure&) {
                    result.pending.push_back(path); result.problems.emplace_back(Code::removal_pending,"Registration inspection failed; retained for authorized reconciliation",path);
                } catch (const std::exception& error) {
                    result.pending.push_back(path); result.problems.emplace_back(Code::removal_pending,error.what(),path);
                }
            }
        }
        return result;
    });
}
} // namespace outloud
