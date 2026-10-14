#include "signing_policy.hpp"
#include <iostream>
#include <vector>
#include <algorithm>
using namespace outloud::installer;
struct Double {
    SigningOwnership owned{std::wstring(32,L'a'),L"S-1-5-32-544",std::wstring(key_prefix)+std::wstring(32,L'a'),std::wstring(40,L'b'),std::wstring(64,L'c'),std::wstring(64,L'd')};
    CertificateFacts facts{true,true,true,true,true,true,true,true,true,true,true};
    std::vector<std::string> calls;std::string fail;unsigned generated{},loaded{},trusted{},signed_count{},committed{};
    bool metadata_present=true,certificate_present=true,unrelated_preserved=true,partial_signature=false;
    bool elevated=true,signature_valid=true,expected_signer=true,code_identity=true;
    std::wstring caller=L"first-administrator";
    std::vector<std::wstring> personal{L"unrelated-personal"},roots{L"unrelated-root"};
    void action(const char* name){calls.emplace_back(name);if(fail==name)throw SigningError("injected_failure");}
    void preflight(){action("preflight");signing_require(elevated,"elevated_machine_authority_required");}
    void verify_dependencies(){action("verify_dependencies");}
    void new_ownership(){action("new_ownership");owned.id=std::wstring(31,L'a')+static_cast<wchar_t>(L'0'+generated++);owned.key_name=std::wstring(key_prefix)+owned.id;}
    void generate_key(){action("generate_key");}
    void generate_certificate(){action("generate_certificate");owned.thumbprint=owned.id+std::wstring(8,L'b');owned.certificate_sha256=owned.id+std::wstring(32,L'c');}
    void add_personal(){action("add_personal");personal.push_back(owned.thumbprint);}
    void add_trust(){action("add_trust");roots.push_back(owned.thumbprint);++trusted;}
    void load_ownership(){action("load_ownership");signing_require(metadata_present && certificate_present,"owned_certificate_missing");++loaded;}
    const SigningOwnership& ownership()const{return owned;}
    std::wstring sid()const{return L"S-1-5-32-544";}
    CertificateFacts certificate_facts(){action("certificate_facts");return facts;}
    void checkpoint(const char* phase){action(phase);}
    void sign_required(){action("sign_required");++signed_count;if(fail=="sign_interruption"){partial_signature=true;throw SigningError("signing_interrupted");}}
    void verify_required(){action("verify_required");signing_require(signature_valid && expected_signer && code_identity,"dll_verification_failed");}
    void commit(){action("commit");++committed;}
};
int main(){
    unsigned checks{};const auto check=[&](bool value){if(!value)throw SigningError("assertion_failed");++checks;};
    auto refuses=[&](bool fresh,Double& op){bool failed{};try{prepare_signing(fresh,op);}catch(const SigningError&){failed=true;}check(failed && !op.committed && op.unrelated_preserved);};
    Double fresh;prepare_signing(true,fresh);check(fresh.generated==1 && fresh.loaded==0 && fresh.trusted==1 && fresh.signed_count==1 && fresh.committed==1);
    const auto first=fresh.owned.id,first_certificate=fresh.owned.thumbprint;prepare_signing(true,fresh);
    check(first!=fresh.owned.id && first_certificate!=fresh.owned.thumbprint); // Controlled uniqueness contract, not an OS RNG test.
    check(fresh.personal.front()==L"unrelated-personal" && fresh.roots.front()==L"unrelated-root" && fresh.personal.size()==3 && fresh.roots.size()==3);
    Double upgrade;const auto previous=upgrade.owned;prepare_signing(false,upgrade);
    check(upgrade.generated==0 && upgrade.loaded==1 && upgrade.trusted==0 && upgrade.committed==1 && upgrade.owned.key_name==previous.key_name && upgrade.owned.thumbprint==previous.thumbprint);
    check(std::find(upgrade.calls.begin(),upgrade.calls.end(),"generate_certificate")==upgrade.calls.end());
    for(bool mode:{false,true}){
        for(const auto operation:{"preflight","verify_dependencies","certificate_facts","before_signing","sign_required","before_verification","verify_required","verified","commit"}){
            Double bad;bad.fail=operation;refuses(mode,bad);
        }
        for(unsigned field=0;field<10;++field){Double bad;
            switch(field){case 0:bad.facts.identity=false;break;case 1:bad.facts.validity=false;break;case 2:bad.facts.code_signing=false;break;
                case 3:bad.facts.digital_signature=false;break;case 4:bad.facts.rsa3072_sha256=false;break;case 5:bad.facts.key_access=false;break;
                case 6:bad.facts.nonexportable=false;break;case 7:bad.facts.signing_only=false;break;case 8:bad.facts.admin_system_dacl=false;break;default:bad.facts.machine_key=false;}
            refuses(mode,bad);check(bad.signed_count==0);
        }
        Double no_trust;no_trust.facts.trusted=false;refuses(mode,no_trust);check(no_trust.signed_count==0);
    }
    for(const auto operation:{"new_ownership","before_key","generate_key","before_certificate","generate_certificate","before_personal_store","add_personal","before_trust","add_trust"}){
        Double bad;bad.fail=operation;refuses(true,bad);check(bad.signed_count==0);
    }
    for(const auto operation:{"load_ownership","upgrade_reuse"}){Double bad;bad.fail=operation;refuses(false,bad);check(bad.generated==0);}
    for(unsigned field=0;field<5;++field){Double bad;
        switch(field){case 0:bad.owned.id=L"invalid";break;case 1:bad.owned.owner_sid=L"S-1-5-21-other";break;case 2:bad.owned.key_name=L"development key";break;
            case 3:bad.owned.thumbprint=L"invalid";break;default:bad.owned.certificate_sha256=L"wrong";}
        refuses(false,bad);check(bad.generated==0 && bad.signed_count==0);
    }
    Double other_admin;other_admin.caller=L"different-administrator";prepare_signing(false,other_admin);
    check(other_admin.loaded==1 && other_admin.generated==0 && other_admin.owned.owner_sid==L"S-1-5-32-544");
    for(bool mode:{false,true}){
        Double denied;denied.elevated=false;refuses(mode,denied);check(denied.generated==0 && denied.signed_count==0);
        for(unsigned field=0;field<3;++field){Double invalid;
            if(field==0)invalid.signature_valid=false;else if(field==1)invalid.expected_signer=false;else invalid.code_identity=false;
            refuses(mode,invalid);check(invalid.committed==0 && invalid.signed_count==1);}
    }
    Double missing;missing.certificate_present=false;refuses(false,missing);check(missing.generated==0);
    missing=Double{};missing.metadata_present=false;refuses(false,missing);check(missing.generated==0);
    Double interrupted;interrupted.fail="sign_interruption";refuses(true,interrupted);
    check(interrupted.partial_signature && interrupted.signed_count==1 && interrupted.roots.front()==L"unrelated-root");
    check(std::wstring(signing_target)==L"bin/OutLoudPocketSAPI.dll" && !signing_lifecycle_ready);
    const auto before=std::find(fresh.calls.begin(),fresh.calls.end(),"preflight");
    check(before<std::find(fresh.calls.begin(),fresh.calls.end(),"generate_key") &&
        std::find(fresh.calls.begin(),fresh.calls.end(),"before_trust")<std::find(fresh.calls.begin(),fresh.calls.end(),"sign_required") &&
        std::find(fresh.calls.begin(),fresh.calls.end(),"verify_required")<std::find(fresh.calls.begin(),fresh.calls.end(),"commit"));
    std::cout<<checks<<" controlled signing assertions PASS; no Windows keys, certificates or files signed.\n";
}
