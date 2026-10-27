// Observation only: enabled solely by --installation-inspect. Never authorizes
// access, repairs permissions, or changes a validation predicate.
struct InstallationDiagnostic {
    std::string phase="arguments",reason="none",api,category="validation";
    fs::path path;
    DWORD win32_error{},attributes{},reparse_tag{},reparse_error{};
    bool has_win32_error{},has_attributes{},has_reparse_tag{};
    std::wstring owner,sddl,ace_sid,exception;
    DWORD security_description_error{},ace_index{},ace_type{},ace_flags{},ace_mask{};
    bool has_ace{};
    int volume_root_verified=-1;
    DWORD volume_root_error{};
};
InstallationDiagnostic* installation_diagnostic{};
struct InstallationDiagnosticScope {
    InstallationDiagnostic* previous=installation_diagnostic;
    explicit InstallationDiagnosticScope(InstallationDiagnostic& value){installation_diagnostic=&value;}
    ~InstallationDiagnosticScope(){installation_diagnostic=previous;}
};
void inspection_stage(const char* phase,const fs::path& path){
    if(installation_diagnostic){*installation_diagnostic=InstallationDiagnostic{};
        installation_diagnostic->phase=phase;installation_diagnostic->path=path;}
}
void inspection_path(const fs::path& path){if(installation_diagnostic)installation_diagnostic->path=path;}
void inspection_error(const char* api,DWORD error){
    if(installation_diagnostic){installation_diagnostic->api=api;installation_diagnostic->win32_error=error;
        installation_diagnostic->has_win32_error=true;}
}
bool inspection_api(BOOL result,const char* api){
    if(!installation_diagnostic)return result!=FALSE;
    const auto saved=GetLastError();
    if(!result)inspection_error(api,saved);
    SetLastError(saved);return result!=FALSE;
}
DWORD inspection_status(DWORD result,const char* api){if(result!=ERROR_SUCCESS)inspection_error(api,result);return result;}
void inspection_reparse(const fs::path& path,DWORD attributes){
    if(!installation_diagnostic)return;
    auto& d=*installation_diagnostic;d.attributes=attributes;d.has_attributes=true;
    if(!(attributes&FILE_ATTRIBUTE_REPARSE_POINT))return;
    const auto saved=GetLastError();
    Handle file;file.value=CreateFileW(path.c_str(),FILE_READ_ATTRIBUTES,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,
        nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT|FILE_FLAG_BACKUP_SEMANTICS,nullptr);
    FILE_ATTRIBUTE_TAG_INFO info{};
    if(file.value==INVALID_HANDLE_VALUE)d.reparse_error=GetLastError();
    else if(!GetFileInformationByHandleEx(file.value,FileAttributeTagInfo,&info,sizeof(info)))d.reparse_error=GetLastError();
    else {d.reparse_tag=info.ReparseTag;d.has_reparse_tag=true;}
    SetLastError(saved);
}
void inspection_security(PSECURITY_DESCRIPTOR descriptor){
    if(!installation_diagnostic)return;
    const auto saved=GetLastError();Local text;
    if(ConvertSecurityDescriptorToStringSecurityDescriptorW(descriptor,SDDL_REVISION_1,
        OWNER_SECURITY_INFORMATION|DACL_SECURITY_INFORMATION,reinterpret_cast<LPWSTR*>(&text.value),nullptr))
        installation_diagnostic->sddl=static_cast<wchar_t*>(text.value);
    else installation_diagnostic->security_description_error=GetLastError();
    SetLastError(saved);
}
void inspection_ace(DWORD index,const ACE_HEADER* header){
    if(!installation_diagnostic)return;
    auto& d=*installation_diagnostic;d.has_ace=true;d.ace_index=index;d.ace_type=header->AceType;
    d.ace_flags=header->AceFlags;d.ace_mask=0;d.ace_sid.clear();
    if(header->AceType==ACCESS_ALLOWED_ACE_TYPE)d.ace_mask=reinterpret_cast<const ACCESS_ALLOWED_ACE*>(header)->Mask;
}
std::wstring inspection_ascii(const std::wstring& text){
    std::wstring result;constexpr wchar_t digits[]=L"0123456789abcdef";
    for(const auto ch:text){if(ch>=32 && ch<127)result+=ch;
        else {result+=L"\\u";for(int shift=12;shift>=0;shift-=4)result+=digits[(static_cast<unsigned>(ch)>>shift)&15];}}
    return result;
}
std::wstring inspection_wide(const std::string& text){return std::wstring(text.begin(),text.end());}
std::wstring inspection_hex(DWORD value){std::wostringstream text;text<<L"0x"<<std::hex<<value;return text.str();}
std::wstring inspection_fields(const InstallationDiagnostic& d,const std::wstring& prefix=L"inspection_"){
    std::wstring result;
    const auto field=[&](const wchar_t* key,const std::wstring& value){result+=prefix+key+L"="+inspection_ascii(value)+L"\n";};
    field(L"category",inspection_wide(d.category));field(L"phase",inspection_wide(d.phase));
    field(L"reason",inspection_wide(d.reason));field(L"path",d.path.native());
    // Exact UTF-16 spelling is retained even if a path contains literal \\u text.
    std::wstring exact;constexpr wchar_t digits[]=L"0123456789abcdef";
    for(const auto ch:d.path.native())for(int shift=12;shift>=0;shift-=4)exact+=digits[(static_cast<unsigned>(ch)>>shift)&15];
    field(L"path_utf16",exact);field(L"api",inspection_wide(d.api));
    field(L"win32_error",d.has_win32_error?std::to_wstring(d.win32_error):L"not_applicable");
    if(d.volume_root_verified>=0)field(L"volume_root_verified",d.volume_root_verified?L"true":L"false");
    if(d.volume_root_error)field(L"volume_root_query_error",std::to_wstring(d.volume_root_error));
    if(d.has_attributes)field(L"attributes",inspection_hex(d.attributes));
    if(d.has_reparse_tag)field(L"reparse_tag",inspection_hex(d.reparse_tag));
    if(d.reparse_error)field(L"reparse_query_error",std::to_wstring(d.reparse_error));
    if(!d.owner.empty())field(L"owner_sid",d.owner);
    if(!d.sddl.empty())field(L"sddl",d.sddl);
    if(d.security_description_error)field(L"sddl_query_error",std::to_wstring(d.security_description_error));
    if(d.has_ace){field(L"ace_index",std::to_wstring(d.ace_index));field(L"ace_type",std::to_wstring(d.ace_type));
        field(L"ace_flags",inspection_hex(d.ace_flags));field(L"ace_mask",inspection_hex(d.ace_mask));field(L"ace_sid",d.ace_sid);}
    if(!d.exception.empty())field(L"exception",d.exception);
    return result;
}
void inspection_stderr(const std::wstring& text){
    // ASCII transport avoids console code-page loss in Inno's captured streams.
    std::string bytes;for(const auto ch:text)bytes+=static_cast<char>(ch);
    std::cerr<<bytes<<std::flush;
}
