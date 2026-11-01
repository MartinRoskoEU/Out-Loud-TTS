// Read-only installation check. Reuse the existing token reader and strict native
// schema/library-path validator; finding a candidate never authorizes adoption or removal.
struct VoiceRegistrationPreflight {
    std::wstring status=L"clear",scope,token,name,field;
    std::wstring reason;
    fs::path actual,expected;
    bool blocked()const{return status!=L"clear";}
};
std::optional<std::wstring> voice_preflight_text(const RegistrationTree& tree,const wchar_t* field){
    const auto found=tree.values.find(field);if(found==tree.values.end())return {};
    try{return registration_text(found->second);}catch(const SigningError&){return {};}
}
VoiceRegistrationPreflight inspect_voice_preflight_token(const std::wstring& name,const RegistrationTree& tree,
    const fs::path& library,bool supported_scope,const std::wstring& scope){
    if(!managed_voice_registration_candidate(name,tree))return {};
    VoiceRegistrationPreflight result;result.scope=scope;result.token=name;
    result.name=voice_preflight_text(tree,L"").value_or(L"");
    if(!supported_scope){result.status=L"unsupported_scope";return result;}
    UninstallVoiceMismatch mismatch;
    try{
        // Validate the global voice identity without resolving old application or
        // runtime paths. Installation leaves the existing registration unchanged.
        uninstall_engine_token_owned(name,tree,library,&mismatch);
        return {};
    }catch(const SigningError& error){
        result.status=L"unverified";
        for(const auto c:std::string(error.what()))result.reason+=static_cast<wchar_t>(c);
        result.field=mismatch.field;result.actual=mismatch.actual;result.expected=mismatch.expected;return result;
    }
}
std::wstring voice_preflight_report(const VoiceRegistrationPreflight& result){
    return L"voice_registration="+result.status+L"\nvoice_text_encoding=utf16le-hex\nvoice_scope="+
        maintenance_root_encoding(result.scope)+L"\nvoice_token="+maintenance_root_encoding(result.token)+
        L"\nvoice_name="+maintenance_root_encoding(result.name)+L"\nvoice_field="+result.field+
        L"\nvoice_actual="+maintenance_root_encoding(result.actual.native())+L"\nvoice_expected="+
        maintenance_root_encoding(result.expected.native())+L"\nvoice_reason="+result.reason+L"\n";
}
VoiceRegistrationPreflight inspect_voice_registrations(){
    VoiceRegistrationPreflight result;
    const auto library=maintenance_library();
    const auto scan=[&](HKEY hive,REGSAM view,const wchar_t* category,bool supported,const std::wstring& scope){
        result.scope=scope;
        for(const auto& [name,tree]:uninstall_tokens(hive,view,nullptr,category)){
            auto candidate=inspect_voice_preflight_token(name,tree,library,supported,scope);
            if(candidate.blocked()){result=std::move(candidate);return false;}
        }
        return true;
    };
    try{
        if(!scan(HKEY_LOCAL_MACHINE,KEY_WOW64_64KEY,L"Speech",true,L"HKLM64/Speech"))return result;
        for(const auto view:{KEY_WOW64_64KEY,KEY_WOW64_32KEY})for(const auto hive:{HKEY_LOCAL_MACHINE,HKEY_CURRENT_USER})
            for(const auto* speech:{L"Speech",L"Speech_OneCore"}){
                if(hive==HKEY_LOCAL_MACHINE && view==KEY_WOW64_64KEY && std::wstring(speech)==L"Speech")continue;
                const auto scope=std::wstring(hive==HKEY_LOCAL_MACHINE?L"HKLM":L"HKCU")+
                    (view==KEY_WOW64_64KEY?L"64/":L"32/")+speech;
                if(!scan(hive,view,speech,false,scope))return result;
            }
        // Match the existing uninstall inspection scope. Never load offline hives.
        for(DWORD i=0;;++i){wchar_t sid[257]{};DWORD length=256;
            const auto status=RegEnumKeyExW(HKEY_USERS,i,sid,&length,nullptr,nullptr,nullptr,nullptr);
            if(status==ERROR_NO_MORE_ITEMS)break;registry_ok(status);signing_require(i<4096,"uninstall_user_inventory_limit");
            RegistryHandle user;result.scope=L"HKU/"+std::wstring(sid,length);
            signing_require(registry_user_hive(std::wstring(sid,length),0,user),"uninstall_user_inventory_raced");
            for(const auto view:{KEY_WOW64_64KEY,KEY_WOW64_32KEY})for(const auto* speech:{L"Speech",L"Speech_OneCore"}){
                const auto scope=L"HKU/"+std::wstring(sid,length)+(view==KEY_WOW64_64KEY?L"/64/":L"/32/")+speech;
                if(!scan(user.value,view,speech,false,scope))return result;
            }
        }
        return {};
    }catch(const SigningError& error){
        result.status=L"inspection_failed";
        for(const auto c:std::string(error.what()))result.reason+=static_cast<wchar_t>(c);
        return result;
    }
}
std::optional<VoiceRegistrationPreflight> voice_preflight_failure;
std::wstring voice_registration_inspection(){
    return voice_preflight_report(inspect_voice_registrations());
}
void require_voice_registration_preflight(){
    auto result=inspect_voice_registrations();
    if(result.blocked()){
        voice_preflight_failure=std::move(result);
        throw SigningError("maintenance_voice_registration_conflict");
    }
}
