#pragma once
#include "mutation.hpp"
#include <cstring>

// Included by the existing bridge checker; all native writes stay in Scratch
// and FakeRegistry. Never execute the administrative helper in tests.
void mutation_checks(const std::filesystem::path& prepared,const std::filesystem::path& engine,
    const outloud::SapiVoiceRuntime& runtime,const std::filesystem::path& scratch) {
    namespace m = outloud::frontend::mutation;
    const auto number = [](std::vector<std::uint8_t>& data,std::uint32_t value) {
        const auto* bytes = reinterpret_cast<const std::uint8_t*>(&value); data.insert(data.end(),bytes,bytes+4);
    };
    const auto text_value = [&](std::vector<std::uint8_t>& data,const std::string& text) { number(data,static_cast<std::uint32_t>(text.size())); data.insert(data.end(),text.begin(),text.end()); };
    const auto source = scratch/L"hlas-\u017e-\U0001f3b5.safetensors";
    std::filesystem::copy_file(prepared,source);
    std::vector<std::uint8_t> request;
    number(request,m::magic); number(request,m::version); number(request,static_cast<std::uint32_t>(m::Operation::import)); number(request,0);
    text_value(request,outloud::detail::library_fs::utf8(source.wstring())); text_value(request,"Hlas \xc5\xbe \xf0\x9f\x8e\xb5");
    const auto size = static_cast<std::uint32_t>(request.size()-16); std::memcpy(request.data()+12,&size,4);
    const auto decoded = m::decode_request(request);
    check(decoded.source==source && decoded.display_name=="Hlas \xc5\xbe \xf0\x9f\x8e\xb5","Mutation request Unicode lost.");
    const auto rejects = [&](const std::vector<std::uint8_t>& data) {
        try { (void)m::decode_request(data); } catch (...) { return true; } return false;
    };
    auto bad = request; bad[8] = 9; check(rejects(bad),"Unsupported operation accepted.");
    bad = request; bad[4] = 9; check(rejects(bad),"Unsupported protocol version accepted.");
    bad = request; bad.pop_back(); check(rejects(bad),"Truncated request accepted.");
    bad = request; bad.push_back(0); check(rejects(bad),"Trailing request bytes accepted.");
    bad = request; bad.back() = 0; check(rejects(bad),"Embedded NUL accepted.");
    bad = request; bad.back() = 0xff; check(rejects(bad),"Invalid UTF-8 accepted.");
    check(rejects(std::vector<std::uint8_t>(m::limit+1)),"Oversized request accepted.");
    outloud::VoiceLibrary library(scratch/L"mutation-library"); auto registry = std::make_shared<FakeRegistry>(); registry->fixture_setup = true;
    const auto resolve = [&] { return runtime; };
    const auto result = m::import_and_install(decoded,library,registry,resolve,outloud::MachineVoiceLibraryPolicy::development_override);
    check(result.outcome==m::Outcome::success && outloud::detail::library_fs::id(result.library_id) && registry->writes==1,"Prepared voice import + machine install failed.");
    const auto voice = library.inspect_voice(result.library_id);
    check(voice.health==outloud::VoiceHealth::valid && voice.metadata->display_name==decoded.display_name &&
        voice.metadata->import_kind==outloud::VoiceImportKind::pocket_prepared,"Prepared import metadata changed.");
    const auto encoded = m::encode_result(result);
    std::uint32_t length = 0; std::memcpy(&length,encoded.data()+12,4);
    check(length==encoded.size()-16 && encoded[8]==1,"Result framing incorrect.");
    const auto duplicate = m::import_and_install(decoded,library,registry,resolve,outloud::MachineVoiceLibraryPolicy::development_override);
    check(duplicate.outcome==m::Outcome::failed && duplicate.library_id.empty() && duplicate.error.code=="duplicate_content" &&
        duplicate.error.duplicate_ids==std::vector<std::string>{result.library_id} && registry->writes==1,"Duplicate behavior or IDs lost.");
    const auto invalid = scratch/L"invalid.safetensors"; { std::ofstream file(invalid,std::ios::binary); file << "not safetensors"; }
    const auto failure = m::import_and_install({invalid,"Invalid"},library,registry,resolve,outloud::MachineVoiceLibraryPolicy::development_override);
    check(failure.outcome==m::Outcome::failed && failure.library_id.empty() && !failure.error.import_code.empty() && registry->writes==1,"Invalid source cause/state lost.");
    outloud::VoiceLibrary partial_library(scratch/L"partial-library"); auto denied = std::make_shared<FakeRegistry>(); denied->fixture_setup = true; denied->deny = true;
    const auto partial = m::import_and_install(decoded,partial_library,denied,resolve,outloud::MachineVoiceLibraryPolicy::development_override);
    check(partial.outcome==m::Outcome::partial && partial.error.code=="registry_access_denied" && partial.error.system_error==5 &&
        partial_library.inspect_voice(partial.library_id).health==outloud::VoiceHealth::valid && denied->writes==0,"Partial success rolled back or lost structured error.");
    outloud::VoiceLibrary missing_library(scratch/L"missing-runtime-library");
    const auto missing = m::import_and_install(decoded,missing_library,registry,[]() -> outloud::SapiVoiceRuntime {
        throw outloud::SapiVoiceError(outloud::SapiVoiceErrorCode::engine_registration_missing,"Missing machine runtime.");
    },outloud::MachineVoiceLibraryPolicy::development_override);
    check(missing.outcome==m::Outcome::partial && missing.error.code=="engine_registration_missing" &&
        missing_library.inspect_voice(missing.library_id).health==outloud::VoiceHealth::valid,"Runtime resolution failure did not preserve partial import.");
    const auto deployment = scratch/L"runtime-discovery"; std::filesystem::create_directories(deployment/L"bin");
    const auto dll = deployment/L"bin/OutLoudPocketSAPI.dll"; std::filesystem::copy_file(engine,dll);
    const auto discovered = m::runtime_from_registration(outloud::sapi_registry_string(dll.wstring()));
    check(discovered.asset_root==deployment && discovered.model==deployment/L"runtime/pocket-english.olp" &&
        discovered.tokenizer==deployment/L"runtime/pocket-english-tokenizer.bin","Registered deployment resolver changed layout.");
    try { (void)m::runtime_from_registration(outloud::sapi_registry_string(engine.wstring())); check(false,"Arbitrary DLL layout accepted."); }
    catch (const outloud::SapiVoiceError& error) { check(error.code==outloud::SapiVoiceErrorCode::invalid_runtime,"Wrong layout error."); }
    try { (void)m::runtime_from_registration(std::nullopt); check(false,"Missing registration accepted."); }
    catch (const outloud::SapiVoiceError& error) { check(error.code==outloud::SapiVoiceErrorCode::engine_registration_malformed,"Wrong registration error."); }
}
