#include "registration_policy.hpp"
#include "installation_path.hpp"
#include <iostream>
#include <functional>
using namespace outloud::installer;
namespace {
unsigned checks{};
void check(bool ok){++checks;if(!ok)throw std::runtime_error("registration assertion failed");}
RegistrationIdentity test_identity(){return {std::wstring(32,L'a'),std::wstring(64,L'b'),L"E:\\Applications\\Out Loud \u017dilina\\bin\\OutLoudPocketSAPI.dll"};}
struct Fake {
    RegistrationSnapshot registry;RegistrationTree voices,unrelated;
    RegistrationIdentity id=test_identity();RegistrationSnapshot journal;bool checkpoint{},committed{},post{};
    bool override_present{},read_denied{},write_denied{},race{},post_failure{},receipt_failure{},wrong_file{};
    int fail_write=-1;unsigned writes{},exchanges{};std::string invalid_precondition;
    Fake(){voices.values[L"user.olv"]=registration_string(L"do not touch");unrelated.values[L"other"]=registration_string(L"preserve");}
    RegistrationIdentity identity()const{return id;}
    void preconditions(){if(!invalid_precondition.empty())throw SigningError(invalid_precondition.c_str());static_cast<void>(desired_registration(id));}
    RegistrationSnapshot capture(){if(read_denied)throw SigningError("registry_access_denied");return registry;}
    void check_overrides(){signing_require(!override_present,"registration_override_conflict");}
    void verify_server(const RegistrationTree& tree){signing_require(!wrong_file && registration_field(tree.children.at(L"InprocServer32"),L"")==id.server,"registration_server_identity_mismatch");}
    void capture_recovery(const RegistrationSnapshot& before){checkpoint=true;journal=before;}
    void exchange(const RegistrationSnapshot& before,const RegistrationTree& desired){
        ++exchanges;if(write_denied)throw SigningError("registry_access_denied");
        if(race){registry=desired;registry->values[L"OutLoudPocketOwner"]=registration_string(L"racing third party");}
        signing_require(registry==before,"registration_changed");
        // All six writes occur in a private transaction. No failed partial copy is published.
        RegistrationTree transaction;
        for(const auto& [name,value]:desired.values){if(static_cast<int>(writes++)==fail_write)throw SigningError("registry_io_failed");transaction.values[name]=value;}
        for(const auto& [name,value]:desired.children.at(L"InprocServer32").values){if(static_cast<int>(writes++)==fail_write)throw SigningError("registry_io_failed");transaction.children[L"InprocServer32"].values[name]=value;}
        if(fail_write==6)throw SigningError("registry_commit_failed");registry=transaction;
    }
    void verify_post(){signing_require(!post_failure,"registration_post_verification_failed");
        signing_require(inspect_registration(registry,id)==RegistrationDisposition::owned,"registration_post_verification_failed");
        check_overrides();verify_server(*registry);post=true;}
    void commit_recovery(){signing_require(!receipt_failure,"registration_receipt_commit_failed");committed=true;}
};
void expect_failure(Fake& op,const char* code,bool unchanged=true){
    const auto before=op.registry,voices=RegistrationSnapshot(op.voices),unrelated=RegistrationSnapshot(op.unrelated);
    bool failed=false;try{register_engine(op);}catch(const SigningError& e){failed=true;check(std::string(e.what())==code);}
    check(failed);check(!op.committed);check(RegistrationSnapshot(op.voices)==voices);check(RegistrationSnapshot(op.unrelated)==unrelated);
    if(unchanged)check(op.registry==before);
}
}
int main(){try {
    check(!registration_lifecycle_ready);check(registration_path()==std::wstring(L"SOFTWARE\\Classes\\CLSID\\")+outloud::sapi::clsid_text);
    check(installation_path(L"E:\\Applications\\Out Loud \u017dilina\\")==L"E:\\Applications\\Out Loud \u017dilina");
    check(installation_path(L"D:\\Apps\\other\\..\\OutLoud")==L"D:\\Apps\\OutLoud");
    check(same_installation_path(L"E:\\Apps\\Out Loud \u017dilina",L"e:\\apps\\OUT LOUD \u017eilina"));
    check(!same_installation_path(L"E:\\Apps\\OutLoud",L"D:\\Apps\\OutLoud"));
    for(const auto path:{L"relative",L"C:\\",L"\\\\server\\share\\OutLoud",L"C:\\Apps\\OutLoud:stream",L"C:\\Apps\\bad.\\OutLoud",L"C:\\Apps\\bad \"name"}){
        bool rejected=false;try{static_cast<void>(installation_path(path));}catch(const SigningError&){rejected=true;}check(rejected);
    }
    auto link=registration_string(L"\\Registry\\User\\S-1-5-21-123_Classes");link.type=6;
    check(registration_user_class_hive(link,L"S-1-5-21-123")==L"S-1-5-21-123_Classes");
    link.bytes.resize(link.bytes.size()-2);check(registration_user_class_hive(link,L"S-1-5-21-123")==L"S-1-5-21-123_Classes");
    for(int i=0;i<4;++i){auto bad=link;
        if(i==0)bad.type=1;if(i==1)bad.bytes.pop_back();if(i==2)bad.bytes[0]='x';if(i==3)bad.bytes.resize(1026);
        bool rejected=false;try{static_cast<void>(registration_user_class_hive(bad,L"S-1-5-21-123"));}
        catch(const SigningError& e){rejected=true;check(std::string(e.what())=="registry_link_or_unexpected_link_value");}check(rejected);}
    bool other_sid=false;try{static_cast<void>(registration_user_class_hive(link,L"S-1-5-21-456"));}catch(const SigningError&){other_sid=true;}check(other_sid);
    auto system_link=registration_string(L"\\Registry\\User\\.Default");system_link.type=6;
    check(registration_system_user_hive(system_link,L"S-1-5-18")==L".DEFAULT");
    system_link.bytes.resize(system_link.bytes.size()-2);check(registration_system_user_hive(system_link,L"S-1-5-18")==L".DEFAULT");
    for(const auto& name:{L".DEFAULT",L"S-1-5-19",L"S-1-5-21-123"}){
        bool rejected=false;try{static_cast<void>(registration_system_user_hive(system_link,name));}catch(const SigningError&){rejected=true;}check(rejected);}
    for(const auto& target:{L"\\Registry\\User\\S-1-5-21-123",L"\\Registry\\Machine\\SOFTWARE",L"\\Registry\\User\\.Default\\SOFTWARE"}){
        auto bad=registration_string(target);bad.type=6;bool rejected=false;try{static_cast<void>(registration_system_user_hive(bad,L"S-1-5-18"));}catch(const SigningError&){rejected=true;}check(rejected);}
    for(const auto malformed:{0,1,2}){auto bad=system_link;if(malformed==0)bad.type=1;if(malformed==1)bad.bytes.pop_back();if(malformed==2)bad.bytes.insert(bad.bytes.end(),{0,0,0,0});
        bool rejected=false;try{static_cast<void>(registration_system_user_hive(bad,L"S-1-5-18"));}catch(const SigningError&){rejected=true;}check(rejected);}
    {auto bad=system_link;bad.bytes.insert(bad.bytes.end(),{0,0,'x',0});bool rejected=false;
        try{static_cast<void>(registration_system_user_hive(bad,L"S-1-5-18"));}catch(const SigningError&){rejected=true;}check(rejected);}
    Fake fresh;const auto voices=fresh.voices,unrelated=fresh.unrelated;register_engine(fresh);
    check(fresh.registry==RegistrationSnapshot(desired_registration(fresh.id)));check(fresh.writes==6);check(fresh.checkpoint && !fresh.journal);
    check(fresh.post && fresh.committed);check(fresh.voices==voices && fresh.unrelated==unrelated);
    check(registration_field(fresh.registry->children.at(L"InprocServer32"),L"ThreadingModel")==L"Both");
    check(registration_field(*fresh.registry,L"OutLoudPocketOwner")==registration_owner);
    Fake owned;owned.registry=desired_registration(owned.id);register_engine(owned);
    check(owned.writes==0 && owned.exchanges==0 && !owned.checkpoint && owned.post);
    Fake unbound;unbound.registry=desired_registration(unbound.id);
    unbound.registry->values.erase(L"OutLoudInstallationId");unbound.registry->values.erase(L"OutLoudCertificateSHA256");
    expect_failure(unbound,"registration_conflict");check(unbound.writes==0 && !unbound.checkpoint);
    Fake third;third.registry=desired_registration(third.id);third.registry->values[L"OutLoudPocketOwner"]=registration_string(L"someone else");
    expect_failure(third,"registration_conflict");
    Fake no_owner;no_owner.registry=desired_registration(no_owner.id);no_owner.registry->values.erase(L"OutLoudPocketOwner");expect_failure(no_owner,"registration_conflict");
    for(const auto& field:{L"OutLoudInstallationId",L"OutLoudCertificateSHA256"}){Fake other;other.registry=desired_registration(other.id);other.registry->values[field]=registration_string(L"different");expect_failure(other,"registration_conflict");}
    for(const auto& field:{L"",L"OutLoudPocketOwner",L"OutLoudInstallationId",L"OutLoudCertificateSHA256"}){
        Fake type;type.registry=desired_registration(type.id);type.registry->values[field].type=4;expect_failure(type,"registration_malformed");}
    for(const auto& field:{L"",L"ThreadingModel"}){Fake type;type.registry=desired_registration(type.id);type.registry->children.at(L"InprocServer32").values[field].type=2;expect_failure(type,"registration_malformed");}
    Fake extra;extra.registry=desired_registration(extra.id);extra.registry->children[L"unexpected"]={};expect_failure(extra,"registration_malformed");
    Fake missing;missing.registry=desired_registration(missing.id);missing.registry->children.clear();expect_failure(missing,"registration_malformed");
    Fake terminator;terminator.registry=desired_registration(terminator.id);terminator.registry->values[L""].bytes.pop_back();expect_failure(terminator,"registration_malformed");
    Fake thread;thread.registry=desired_registration(thread.id);thread.registry->children.at(L"InprocServer32").values[L"ThreadingModel"]=registration_string(L"Apartment");expect_failure(thread,"registration_malformed");
    Fake path;path.registry=desired_registration(path.id);path.registry->children.at(L"InprocServer32").values[L""]=registration_string(L"C:\\foreign.dll");expect_failure(path,"registration_server_identity_mismatch");
    Fake physical;physical.registry=desired_registration(physical.id);physical.wrong_file=true;expect_failure(physical,"registration_server_identity_mismatch");
    for(const auto& code:{"unsupported_windows","registration_prerequisites_missing","license_consent_missing","registration_payload_invalid",
        "file_inaccessible_or_locked","unsafe_installation_path","owned_certificate_missing","certificate_trust_invalid",
        "dll_signature_verification_failed","vendor_signature_invalid","registration_signed_identity_mismatch","registration_recovery_required"}){
        Fake prerequisites;prerequisites.invalid_precondition=code;expect_failure(prerequisites,code);check(prerequisites.writes==0 && !prerequisites.checkpoint);}
    Fake override;override.override_present=true;expect_failure(override,"registration_override_conflict");check(override.writes==0);
    Fake read;read.read_denied=true;expect_failure(read,"registry_access_denied");
    Fake denied;denied.write_denied=true;expect_failure(denied,"registry_access_denied");check(denied.checkpoint);
    for(int i=0;i<=6;++i){Fake partial;partial.fail_write=i;expect_failure(partial,i==6?"registry_commit_failed":"registry_io_failed");check(partial.checkpoint && !partial.post);}
    Fake verification;verification.post_failure=true;expect_failure(verification,"registration_post_verification_failed",false);check(verification.registry.has_value() && verification.checkpoint);
    Fake receipt;receipt.receipt_failure=true;expect_failure(receipt,"registration_receipt_commit_failed",false);check(receipt.post && receipt.checkpoint);
    Fake race;race.race=true;expect_failure(race,"registration_changed",false);check(registration_field(*race.registry,L"OutLoudPocketOwner")==L"racing third party");
    std::cout<<checks<<" registration assertions passed (in-memory registry only).\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<"\n";return 1;}}
