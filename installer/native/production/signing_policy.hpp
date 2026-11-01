#pragma once
#include <string>
#include <stdexcept>
namespace outloud::installer {
inline constexpr bool signing_lifecycle_ready=false; // No runtime override.
inline constexpr wchar_t signing_target[]=L"bin/OutLoudPocketSAPI.dll";
inline constexpr wchar_t key_prefix[]=L"OutLoud.Install.";
struct SigningError:std::runtime_error { explicit SigningError(const char* code):std::runtime_error(code) {} };
inline void signing_require(bool ok,const char* code) { if(!ok) throw SigningError(code); }
inline bool hexadecimal(const std::wstring& s,size_t length) {
    return s.size()==length && s.find_first_not_of(L"0123456789abcdef")==std::wstring::npos;
}
struct SigningOwnership {
    std::wstring id,owner_sid,key_name,thumbprint,certificate_sha256,signed_sha256;
};
inline void validate_ownership(const SigningOwnership& o,const std::wstring& sid) {
    signing_require(hexadecimal(o.id,32) && o.key_name==std::wstring(key_prefix)+o.id &&
        o.owner_sid==sid && sid==L"S-1-5-32-544" && hexadecimal(o.thumbprint,40) &&
        hexadecimal(o.certificate_sha256,64),"ownership_mismatch");
}
struct CertificateFacts {
    bool identity{},validity{},code_signing{},digital_signature{},rsa3072_sha256{},
        key_access{},nonexportable{},signing_only{},admin_system_dacl{},machine_key{},trusted{};
};
inline void validate_certificate(const CertificateFacts& f,bool require_trust) {
    signing_require(f.identity,"certificate_identity_mismatch");
    signing_require(f.validity,"certificate_expired_or_invalid");
    signing_require(f.code_signing && f.digital_signature && f.rsa3072_sha256,"certificate_properties_invalid");
    signing_require(f.key_access,"private_key_inaccessible");
    signing_require(f.nonexportable && f.signing_only && f.admin_system_dacl && f.machine_key,"private_key_policy_invalid");
    if(require_trust) signing_require(f.trusted,"certificate_trust_invalid");
}
// Production and controlled tests instantiate this identical transaction policy.
// Each checkpoint precedes its side effect; failed work is never a verified result.
template<class Operations> void prepare_signing(bool fresh,Operations& op) {
    op.preflight(); op.verify_dependencies();
    if(fresh) {
        op.new_ownership(); op.checkpoint("before_key"); op.generate_key();
        op.checkpoint("before_certificate"); op.generate_certificate();
        validate_ownership(op.ownership(),op.sid());
        validate_certificate(op.certificate_facts(),false);
        op.checkpoint("before_personal_store"); op.add_personal();
        op.checkpoint("before_trust"); op.add_trust();
    } else {
        op.load_ownership(); validate_ownership(op.ownership(),op.sid());
        validate_certificate(op.certificate_facts(),true);
        op.checkpoint("upgrade_reuse"); // Never create a replacement key/certificate.
    }
    validate_certificate(op.certificate_facts(),true);
    op.checkpoint("before_signing"); op.sign_required();
    op.checkpoint("before_verification"); op.verify_required();
    op.checkpoint("verified"); op.commit();
}
}
