#include "outloud/rust_model_selection.hpp"
#include "voice_library_fs.hpp"
#include "voice_json.hpp"
#include "voice_library_security.hpp"
#include <shlobj.h>
#include <intrin.h>
#include <regex>
#include <sstream>
#include <set>

namespace outloud {
#ifdef OUTLOUD_RUST_SELECTION_TEST
namespace testing {
std::filesystem::path selection_root, voice_root;
void selection_roots(std::filesystem::path data,std::filesystem::path voices) {
    selection_root=std::move(data); voice_root=std::move(voices);
}
}
#endif
namespace {
namespace fs = detail::library_fs;
using Json = detail::voice_json::Value;
struct Metadata { std::string_view name, text; };
#include "rust_model_metadata.hpp"
constexpr std::string_view repository = "kyutai/pocket-tts-without-voice-cloning";
constexpr std::string_view runtime_hash = "813cabfc2f8fc7a4e5310deda6025426b3164610dd9f3c3098a6078e5cc1895a";
[[noreturn]] void invalid(const char* message) { throw std::runtime_error(message); }
void cancelled(std::stop_token token) { if (token.stop_requested()) { throw RustSelectionCancelled{}; } }
const Json& member(const Json& value, const char* key) {
    const auto found = value.object.find(key);
    if (value.kind != Json::Kind::object || found == value.object.end()) { invalid("Incomplete managed model metadata."); }
    return found->second;
}
std::string text(const Json& value, const char* key) {
    const auto& result = member(value,key);
    if (result.kind != Json::Kind::string) { invalid("Invalid managed model string."); }
    return result.text;
}
std::uint64_t integer(const Json& value, const char* key) {
    std::uint64_t result;
    if (!detail::voice_json::unsigned_integer(member(value,key), result)) { invalid("Invalid managed model number."); }
    return result;
}
std::string normalized(std::string value) {
    std::erase(value, '\r');
    return value;
}
std::string lower(std::string value) {
    for (auto& ch : value) { if (ch >= 'A' && ch <= 'F') { ch = static_cast<char>(ch - 'A' + 'a'); } }
    return value;
}
std::filesystem::path data_root() {
#ifdef OUTLOUD_RUST_SELECTION_TEST
    if (!testing::selection_root.empty()) { return testing::selection_root; }
#endif
    PWSTR raw = nullptr;
    const auto hr = SHGetKnownFolderPath(FOLDERID_ProgramData, KF_FLAG_DONT_VERIFY, nullptr, &raw);
    if (FAILED(hr)) { CoTaskMemFree(raw); invalid("Cannot resolve shared Out Loud application data."); }
    const auto result = std::filesystem::path(raw) / L"OutLoud/ApplicationData";
    CoTaskMemFree(raw);
    const auto pins=detail::library_security::prepare_root(result,false,false);
    return fs::local(result, VoiceLibraryErrorCode::unsafe_path);
}
std::filesystem::path synthesis_staging(const std::filesystem::path& models) {
#ifdef OUTLOUD_RUST_SELECTION_TEST
    if (!testing::selection_root.empty()) return models/L".staging";
#else
    (void)models;
#endif
    // Request-local temporary configuration is not shared persistent state.
    // Ordinary SAPI callers never need write permission on the shared model store.
    PWSTR raw=nullptr;
    const auto hr=SHGetKnownFolderPath(FOLDERID_LocalAppData,KF_FLAG_DONT_VERIFY,nullptr,&raw);
    if(FAILED(hr)){CoTaskMemFree(raw);invalid("Cannot resolve temporary speech configuration storage.");}
    const auto result=std::filesystem::path(raw)/L"Temp/OutLoud-Synthesis";
    CoTaskMemFree(raw);return fs::local(result,VoiceLibraryErrorCode::unsafe_path);
}
std::string read(const std::filesystem::path& path, std::uint64_t limit) {
    const fs::Pins parents(path.parent_path());
    const auto file = fs::file(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_DELETE);
    const auto bytes = fs::read(file.value,path,limit);
    return {reinterpret_cast<const char*>(bytes.data()),bytes.size()};
}
Json parse(std::string_view bytes) { return detail::voice_json::Parser(bytes, 1048576, 8192, 16384).parse(); }
struct Selection {
    std::filesystem::path root;
    Json model;
    std::string identity, runtime_identity;
};
Selection selected() {
    Selection result; result.root = data_root();
    const auto selection_bytes = read(result.root / L"model-selection.json",4096);
    const auto selection = parse(selection_bytes);
    if (integer(selection,"SchemaVersion") != 1) { invalid("Unsupported Out Loud model selection schema."); }
    const auto& alias = member(selection,"SelectedModelAlias");
    if (alias.kind != Json::Kind::string || alias.text.empty()) { invalid("Explicitly select an installed model in Out Loud Settings before synthesis."); }
    const auto inventory_bytes = read(result.root / L"model-inventory.json",16 * 1024 * 1024);
    const auto inventory = parse(inventory_bytes);
    if (integer(inventory,"SchemaVersion") != 2) { invalid("Synthesis requires Out Loud's schema-2 model inventory."); }
    const auto& models = member(inventory,"Models");
    if (models.kind != Json::Kind::array || models.array.size() > 4096) { invalid("Invalid Out Loud model inventory."); }
    bool found = false;
    std::set<std::string> aliases;
    for (const auto& model : models.array) {
        const auto name = text(model,"Alias");
        if (!aliases.insert(name).second) { invalid("Duplicate model inventory identifiers."); }
        if (name == alias.text) { result.model = model; found = true; }
    }
    if (!found) { invalid("The selected model is no longer installed. No replacement was selected."); }
    const auto runtime_identity = result.root / L"runtime-identity.json";
    if (fs::exists(runtime_identity)) { result.runtime_identity = read(runtime_identity,16384); }
    detail::Sha256 hash;
    for (const auto& part : {selection_bytes,inventory_bytes,result.runtime_identity}) {
        hash.update({reinterpret_cast<const unsigned char*>(part.data()),part.size()});
    }
    result.identity = detail::hex(hash.finish()); return result;
}
void processor() {
    // The bundled official Windows binary targets x86-64-v3, not just AVX2.
    int basic[4]{}; __cpuidex(basic,1,0);
    constexpr int ecx = (1<<0)|(1<<9)|(1<<12)|(1<<13)|(1<<19)|(1<<20)|(1<<22)|(1<<23)|(1<<26)|(1<<27)|(1<<28)|(1<<29);
    int extended[4]{}; __cpuidex(extended,static_cast<int>(0x80000001U),0);
    int seven[4]{}; __cpuidex(seven,7,0);
    if ((basic[2] & ecx) != ecx || (extended[2] & ((1<<0)|(1<<5))) != ((1<<0)|(1<<5)) ||
        (seven[1] & ((1<<3)|(1<<5)|(1<<8))) != ((1<<3)|(1<<5)|(1<<8)) || (_xgetbv(0) & 6) != 6) {
        invalid("The official Pocket TTS runtime requires an x86-64-v3 CPU and enabled AVX state.");
    }
}
std::string digest(HANDLE file, const std::filesystem::path& path, bool sha256,std::stop_token cancellation = {}) {
    BCRYPT_ALG_HANDLE algorithm = nullptr; BCRYPT_HASH_HANDLE hash = nullptr;
    if (BCryptOpenAlgorithmProvider(&algorithm,sha256 ? BCRYPT_SHA256_ALGORITHM : BCRYPT_SHA1_ALGORITHM,nullptr,0) < 0) {
        invalid("Cannot initialize model integrity verification.");
    }
    struct Cleanup { BCRYPT_ALG_HANDLE a; BCRYPT_HASH_HANDLE& h; ~Cleanup() { if (h) { BCryptDestroyHash(h); } BCryptCloseAlgorithmProvider(a,0); } } cleanup{algorithm,hash};
    if (BCryptCreateHash(algorithm,&hash,nullptr,0,nullptr,0,0) < 0) { invalid("Cannot initialize asset digest."); }
    auto append = [&](const unsigned char* data, DWORD size) {
        if (BCryptHashData(hash,const_cast<PUCHAR>(data),size,0) < 0) { invalid("Cannot hash model assets."); }
    };
    if (!sha256) {
        auto header = "blob " + std::to_string(fs::size(file,path)); header += '\0';
        append(reinterpret_cast<const unsigned char*>(header.data()),static_cast<DWORD>(header.size()));
    }
    LARGE_INTEGER zero{};
    if (!SetFilePointerEx(file,zero,nullptr,FILE_BEGIN)) { invalid("Cannot rewind model asset."); }
    std::array<unsigned char,131072> buffer{};
    for (;;) {
        cancelled(cancellation);
        DWORD count = 0;
        if (!ReadFile(file,buffer.data(),static_cast<DWORD>(buffer.size()),&count,nullptr)) { invalid("Cannot read model asset."); }
        if (!count) { break; } append(buffer.data(),count);
    }
    std::array<unsigned char,32> value{}; const ULONG size = sha256 ? 32UL : 20UL;
    if (BCryptFinishHash(hash,value.data(),size,0) < 0) { invalid("Cannot finish asset verification."); }
    return detail::hex({value.data(),size});
}
struct Asset {
    std::string reference, path, revision, hash;
    std::uint64_t size;
    bool weights, sha256;
};
Asset asset(std::string reference, bool weights) {
    const std::string prefix = "hf://" + std::string(repository) + "/";
    if (!reference.starts_with(prefix)) { invalid("The trusted configuration references an unsupported repository."); }
    const auto at = reference.find('@',prefix.size());
    if (at == reference.npos || reference.size() - at != 41) { invalid("The trusted configuration is not revision pinned."); }
    Asset result{reference,reference.substr(prefix.size(),at-prefix.size()),reference.substr(at+1),{},0,weights,false};
    for (const auto& metadata : repositories) {
        if (!metadata.name.ends_with(result.revision)) { continue; }
        const auto repo = parse(metadata.text);
        if (text(repo,"id") != repository || text(repo,"sha") != result.revision) { invalid("Invalid bundled repository authority."); }
        for (const auto& entry : member(repo,"siblings").array) {
            if (text(entry,"rfilename") != result.path) { continue; }
            result.size = integer(entry,"size");
            const auto lfs = entry.object.find("lfs");
            result.sha256 = lfs != entry.object.end() && lfs->second.kind == Json::Kind::object;
            result.hash = result.sha256 ? text(lfs->second,"sha256") : text(entry,"blobId");
            if (!result.size || result.hash.size() != (result.sha256 ? 64 : 40)) { invalid("Invalid bundled file authority."); }
            return result;
        }
    }
    invalid("No trusted repository manifest for this model asset.");
}
struct Plan { std::string yaml; std::vector<Asset> assets; };
Plan plan(const std::string& alias) {
    const auto config = std::find_if(std::begin(configs),std::end(configs),[&](const auto& item) { return item.name == alias; });
    if (config == std::end(configs)) { invalid("The selected model has no validated runtime configuration."); }
    std::string source = normalized(std::string(config->text));
    const std::regex fallback(R"((^|\n)weights_path_without_voice_cloning:[ \t]*([^\r\n]+)\n?)");
    std::smatch match;
    if (!std::regex_search(source,match,fallback)) { invalid("Missing trusted public model variant."); }
    const std::string weights = match[2];
    source = std::regex_replace(source,fallback,"$1");
    source = std::regex_replace(source,std::regex(R"((^|\n)weights_path:[^\n]*)"),"$1weights_path: " + weights);
    const std::regex tokenizer(R"((^|\n)[ \t]*tokenizer_path:[ \t]*([^\r\n]+))");
    if (!std::regex_search(source,match,tokenizer)) { invalid("Missing trusted tokenizer."); }
    Plan result{source,{asset(weights,true),asset(match[2],false)}};
    return result;
}
void replace(std::string& value, const std::string& from, const std::string& to) {
    std::size_t at = 0;
    while ((at = value.find(from,at)) != value.npos) { value.replace(at,from.size(),to); at += to.size(); }
}
Plan updated_plan(Plan trusted,const std::string& alias,const std::string& revision,const Json& receipt) {
    if (receipt.object.size()!=5 || integer(receipt,"SchemaVersion")!=1 || text(receipt,"RepositoryId")!=repository ||
        text(receipt,"Alias")!=alias || text(receipt,"Revision")!=revision ||
        revision.size()!=40 || revision.find_first_not_of("0123456789abcdef")!=revision.npos) {
        invalid("Invalid protected model update receipt.");
    }
    const auto& files=member(receipt,"RequiredFiles");
    if (files.kind!=Json::Kind::array || files.array.size()!=trusted.assets.size()) { invalid("Incomplete model update receipt."); }
    for (auto& expected:trusted.assets) {
        const Json* file=nullptr;
        for (const auto& item:files.array) {
            if (text(item,"RepositoryPath")!=expected.path) { continue; }
            if (file) { invalid("Duplicate model update asset."); } file=&item;
        }
        if (!file || file->object.size()!=8) { invalid("Unknown model update asset schema."); }
        const auto hash=text(*file,"ExpectedHash"),algorithm=text(*file,"HashAlgorithm");
        const bool sha256=algorithm=="SHA256";
        const auto& weights=member(*file,"IsWeights");
        if ((!sha256 && algorithm!="GitBlobSHA1") || (expected.weights && !sha256) ||
            hash.size()!=(sha256?64:40) || hash.find_first_not_of("0123456789abcdef")!=hash.npos || !integer(*file,"SizeBytes") ||
            text(*file,"RepositoryId")!=repository || text(*file,"Revision")!=revision ||
            text(*file,"RelativePath")!=std::string("blobs\\")+(sha256?"sha256\\":"git-blob-sha1\\")+hash ||
            weights.kind!=Json::Kind::literal || weights.text!=(expected.weights?"true":"false")) {
            invalid("Invalid protected model update file identity.");
        }
        const auto reference="hf://"+std::string(repository)+"/"+expected.path+"@"+revision;
        replace(trusted.yaml,expected.reference,reference);
        expected.reference=reference;expected.revision=revision;expected.hash=hash;
        expected.size=integer(*file,"SizeBytes");expected.sha256=sha256;
    }
    return trusted;
}
std::string yaml_path(const std::filesystem::path& path) {
    auto value = fs::utf8(path.native()); replace(value,"'","''"); return "'" + value + "'";
}
} // namespace

#ifdef OUTLOUD_RUST_SELECTION_TEST
namespace testing {
std::filesystem::path default_selection_root() { return data_root(); }
std::filesystem::path default_synthesis_staging() { return synthesis_staging({}); }
std::string model_update_configuration(std::string alias,std::string receipt) {
    const auto json=parse(receipt);
    return updated_plan(plan(alias),alias,text(json,"Revision"),json).yaml;
}
}
#endif

struct RustModelSelection::Impl {
    RustRuntimeSessionOptions options;
    std::string identity;
    std::vector<fs::Pins> parents;
    std::vector<fs::Handle> files;
    fs::Handle operation, configuration;
    std::filesystem::path operation_path;
    void acquire_runtime(const Selection&,const std::filesystem::path&,bool,std::stop_token);
    ~Impl() {
        try {
            if (configuration.value != INVALID_HANDLE_VALUE) { fs::discard(configuration.value,options.configuration); configuration.close(); }
            if (operation.value != INVALID_HANDLE_VALUE) { fs::discard(operation.value,operation_path); }
        } catch (...) { } // Never remove anything outside this owned, pinned operation.
    }
};
namespace {
std::string runtime_digest(const Json& value,const char* key) {
    auto result=lower(text(value,key));
    if (result.size()!=64 || result.find_first_not_of("0123456789abcdef")!=result.npos) {
        invalid("Invalid runtime content identity.");
    }
    return result;
}
} // namespace
void RustModelSelection::Impl::acquire_runtime(const Selection& selection,
    const std::filesystem::path& bundled,bool machine,std::stop_token cancellation) {
    auto& state=*this;
    auto executable=fs::local(bundled,VoiceLibraryErrorCode::unsafe_path);
    std::string expected_hash(runtime_hash);
    std::string license_hash;
    if (!selection.runtime_identity.empty()) {
        const auto active=parse(selection.runtime_identity);
        const auto release=text(active,"ReleaseVersion");
        expected_hash=runtime_digest(active,"ExecutableSha256");
        // Match Settings' bundled-path mapping; other releases are resolved from
        // their existing installation receipt, without a supported-version list.
        if (release=="v4.0.1") {
            if (expected_hash!=runtime_hash) { invalid("The bundled runtime content identity is invalid."); }
        } else {
            if (release.empty() || release.size()>128 || release=="." || release==".." ||
                release.find_first_not_of("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789.+-")!=release.npos) {
                invalid("Invalid runtime installation path identity.");
            }
            auto directory_hash=expected_hash;
            for (auto& ch:directory_hash) { if (ch>='a' && ch<='f') { ch=static_cast<char>(ch-'a'+'A'); } }
            executable=fs::local(selection.root/L"runtimes"/fs::wide(release)/fs::wide(directory_hash)/L"pocket-tts.exe",
                VoiceLibraryErrorCode::unsafe_path);
            state.parents.emplace_back(executable.parent_path());
            const auto receipt_path=executable.parent_path()/L"runtime-installation.json";
            auto receipt_file=fs::file(receipt_path,GENERIC_READ|READ_CONTROL);
            if (machine) { detail::library_security::validate(receipt_file.value,receipt_path,false); }
            const auto bytes=fs::read(receipt_file.value,receipt_path,16384);
            const auto receipt=parse({reinterpret_cast<const char*>(bytes.data()),bytes.size()});
            const auto asset=text(receipt,"AssetName");
            if (text(receipt,"ReleaseVersion")!=release || runtime_digest(receipt,"ExecutableSha256")!=expected_hash ||
                !integer(receipt,"AssetId") ||
                !std::regex_match(asset,std::regex(R"(pocket-tts-[0-9A-Za-z][0-9A-Za-z.+-]*-x86_64-pc-windows-msvc\.zip)")) ||
                text(receipt,"AssetUrl")!="https://github.com/kyutai-labs/pocket-tts/releases/download/"+release+"/"+asset) {
                invalid("The selected runtime differs from its verified installation receipt.");
            }
            (void)runtime_digest(receipt,"ArchiveSha256");
            license_hash=runtime_digest(receipt,"LicenseSha256");
            state.files.push_back(std::move(receipt_file));
        }
    }
    if (license_hash.empty()) { state.parents.emplace_back(executable.parent_path()); }
    auto runtime=fs::file(executable,GENERIC_READ|READ_CONTROL);
    if (machine && !license_hash.empty()) { detail::library_security::validate(runtime.value,executable,false); }
    if (digest(runtime.value,executable,true,cancellation)!=expected_hash) {
        invalid("The selected runtime executable failed its SHA-256 verification.");
    }
    if (!license_hash.empty()) {
        const auto path=executable.parent_path()/L"LICENSE";
        auto license=fs::file(path,GENERIC_READ|READ_CONTROL);
        if (machine) { detail::library_security::validate(license.value,path,false); }
        if (digest(license.value,path,true,cancellation)!=license_hash) {
            invalid("The selected runtime license differs from its installation receipt.");
        }
        state.files.push_back(std::move(license));
    }
    state.files.push_back(std::move(runtime));
    state.options.executable=executable;
}
RustModelSelection::RustModelSelection(std::unique_ptr<Impl> value) : impl_(std::move(value)) {}
RustModelSelection::~RustModelSelection() = default;
const RustRuntimeSessionOptions& RustModelSelection::options() const noexcept { return impl_->options; }
const std::string& RustModelSelection::identity() const noexcept { return impl_->identity; }
std::string RustModelSelection::selected_identity() { return selected().identity; }
void verify_rust_runtime(const std::filesystem::path& bundled) {
    processor();
    const auto path=fs::local(bundled,VoiceLibraryErrorCode::unsafe_path);
    const fs::Pins parents(path.parent_path()); const auto file=fs::file(path);
    if (fs::size(file.value,path)!=22080000 || digest(file.value,path,true)!=runtime_hash) {
        invalid("The Out Loud runtime does not match the official v4.0.1 Windows x64 executable.");
    }
}
std::unique_ptr<RustModelSelection> RustModelSelection::acquire(const std::filesystem::path& bundled,
    const std::filesystem::path& voice, std::string_view id,std::stop_token cancellation) {
    cancelled(cancellation);
    processor();
    auto state = std::make_unique<Impl>();
    state->options.prefer_system_dlls=true;
    if (!fs::id(id)) { invalid("Invalid managed native voice identity."); }
    auto library=default_voice_library_root();
    bool machine=true;
#ifdef OUTLOUD_RUST_SELECTION_TEST
    if (!testing::voice_root.empty()) { library=testing::voice_root; machine=false; }
#endif
    const auto expected_voice=library / L"voices" / fs::wide(id) / L"voice.safetensors";
    if (_wcsicmp(fs::local(voice,VoiceLibraryErrorCode::unsafe_path).c_str(),expected_voice.c_str())!=0) {
        invalid("Rust SAPI requires a protected Out Loud managed native profile.");
    }
    if (machine) { state->parents.push_back(detail::library_security::prepare_root(library,false,false)); }
    state->parents.emplace_back(expected_voice.parent_path());
    for (const auto& path:{expected_voice,expected_voice.parent_path()/L"metadata.json"}) {
        auto file=fs::file(path,GENERIC_READ|READ_CONTROL);
        if (machine) { detail::library_security::validate(file.value,path,false); } state->files.push_back(std::move(file));
    }
    const auto profile=VoiceLibrary(library).inspect_voice(id,cancellation);
    cancelled(cancellation);
    if (profile.health!=VoiceHealth::valid || !profile.metadata || profile.metadata->import_kind!=VoiceImportKind::pocket_native) {
        invalid("The managed native profile is missing, corrupt or unverified.");
    }
    state->options.default_voice=expected_voice;
    const auto root = data_root(); const auto models = root / L"models";
    state->parents.emplace_back(models);
    // Same reader/writer sharing contract as PocketTtsModelStore.AcquireReadAsync.
    state->files.push_back(fs::file(models / L".operations.lock",GENERIC_READ,FILE_SHARE_READ));
    const auto selection = selected(); state->identity = selection.identity;
    const auto& entry = selection.model;
    const auto alias = text(entry,"Alias");
    auto trusted = plan(alias);
    const auto update=entry.object.find("UpdateReceiptSha256");
    if (update!=entry.object.end() && !(update->second.kind==Json::Kind::literal && update->second.text=="null")) {
        const auto hash=runtime_digest(entry,"UpdateReceiptSha256");
        const auto path=models/L"blobs/sha256"/fs::wide(hash);
        state->parents.emplace_back(path.parent_path());
        auto receipt=fs::file(path,GENERIC_READ|READ_CONTROL);
        if (machine) { detail::library_security::validate(receipt.value,path,false); }
        if (fs::size(receipt.value,path)>65536 || digest(receipt.value,path,true,cancellation)!=hash) {
            invalid("The selected model update receipt failed SHA-256 verification.");
        }
        LARGE_INTEGER zero{};
        if (!SetFilePointerEx(receipt.value,zero,nullptr,FILE_BEGIN)) { invalid("Cannot rewind the model update receipt."); }
        const auto bytes=fs::read(receipt.value,path,65536);
        trusted=updated_plan(std::move(trusted),alias,text(entry,"Revision"),
            parse({reinterpret_cast<const char*>(bytes.data()),bytes.size()}));
        state->files.push_back(std::move(receipt));
    }
    if (text(entry,"RepositoryId") != repository || text(entry,"Revision") != trusted.assets[0].revision ||
        normalized(text(entry,"ConfigurationYaml")) != trusted.yaml ||
        !fs::within(fs::local(fs::wide(text(entry,"InstallationDirectory")),VoiceLibraryErrorCode::unsafe_path),models) ||
        _wcsicmp(fs::wide(text(entry,"InstallationDirectory")).c_str(),models.c_str()) != 0) {
        invalid("The selected model inventory does not match its trusted configuration and file manifest.");
    }
    const auto& required = member(entry,"RequiredFiles");
    if (required.kind != Json::Kind::array || required.array.size() != trusted.assets.size()) { invalid("Incomplete selected model manifest."); }
    auto yaml = trusted.yaml;
    for (const auto& expected : trusted.assets) {
        const Json* record = nullptr;
        for (const auto& file : required.array) {
            if (text(file,"RepositoryPath") == expected.path) {
                if (record) { invalid("Duplicate model manifest asset."); } record = &file;
            }
        }
        if (!record) { invalid("Missing required model asset."); }
        const auto inherited = [&](const char* key, const std::string& fallback_value) {
            const auto found = record->object.find(key);
            return found == record->object.end() || (found->second.kind == Json::Kind::literal && found->second.text=="null") ? fallback_value : text(*record,key);
        };
        const auto relative = std::string("blobs\\") + (expected.sha256 ? "sha256\\" : "git-blob-sha1\\") + expected.hash;
        if (text(*record,"RelativePath") != relative || lower(text(*record,"ExpectedHash")) != expected.hash ||
            text(*record,"HashAlgorithm") != (expected.sha256 ? "SHA256" : "GitBlobSHA1") || integer(*record,"SizeBytes") != expected.size ||
            member(*record,"IsWeights").kind != Json::Kind::literal || member(*record,"IsWeights").text != (expected.weights ? "true" : "false") ||
            inherited("RepositoryId",text(entry,"RepositoryId")) != repository || inherited("Revision",text(entry,"Revision")) != expected.revision) {
            invalid("Selected model provenance or integrity metadata differs from its trusted manifest.");
        }
        const auto path = models / fs::wide(relative);
        state->parents.emplace_back(path.parent_path());
        auto file = fs::file(path);
        if (fs::size(file.value,path) != expected.size || digest(file.value,path,expected.sha256,cancellation) != expected.hash) {
            invalid("A selected model asset is missing, corrupt, or unverified.");
        }
        replace(yaml,expected.reference,yaml_path(path)); state->files.push_back(std::move(file));
    }
    for (std::istringstream lines(yaml);;) {
        std::string line; if (!std::getline(lines,line)) { break; }
        const auto start = line.find_first_not_of(" \t");
        if (start != line.npos && line[start] != '#' && (line.find("hf://") != line.npos || line.find("http://") != line.npos || line.find("https://") != line.npos)) {
            invalid("Unresolved remote reference in runtime configuration.");
        }
    }
    state->acquire_runtime(selection,bundled,machine,cancellation);
    const auto staging = synthesis_staging(models);
    state->parents.emplace_back(staging,true);
    state->operation_path = staging / fs::wide(fs::random_id());
    if (!CreateDirectoryW(state->operation_path.c_str(),nullptr)) { invalid("Cannot create private runtime configuration operation."); }
    state->operation = fs::directory(state->operation_path,DELETE);
    state->options.configuration = state->operation_path / L"runtime.yaml";
    {
        auto writer = fs::file(state->options.configuration,GENERIC_WRITE,0,CREATE_NEW,VoiceLibraryErrorCode::destination_io);
        fs::write(writer.value,{reinterpret_cast<const unsigned char*>(yaml.data()),yaml.size()},state->options.configuration);
    }
    state->configuration = fs::file(state->options.configuration,GENERIC_READ|DELETE,FILE_SHARE_READ);
    // Re-read through the retained handle to close the writer-to-reader handoff.
    const auto saved = fs::read(state->configuration.value,state->options.configuration,1024*1024);
    if (std::string_view(reinterpret_cast<const char*>(saved.data()),saved.size()) != yaml) { invalid("The generated local configuration changed."); }
    cancelled(cancellation);
    return std::unique_ptr<RustModelSelection>(new RustModelSelection(std::move(state)));
}
} // namespace outloud
