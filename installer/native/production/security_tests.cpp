// Existing isolated checker: exercise the shared security predicates directly.
#define OUTLOUD_MAINTENANCE_CHECK 1
#include "signing.cpp"

namespace {
unsigned checks=0;
void check(bool value,const char* message) { ++checks; if(!value)throw std::runtime_error(message); }
template<class F> void refuses(F action,const char* message) {
    bool failed=false;try{action();}catch(const SigningError&){failed=true;}check(failed,message);
}
void descriptor(const wchar_t* text,bool ancestor=false,bool replacement=false,bool root=false) {
    Local value;
    check(ConvertStringSecurityDescriptorToSecurityDescriptorW(text,SDDL_REVISION_1,
        reinterpret_cast<PSECURITY_DESCRIPTOR*>(&value.value),nullptr)!=0,"Cannot create isolated descriptor");
    require_admin_descriptor(value.value,ancestor,replacement,root);
}
DWORD rights(PACL acl,const wchar_t* sid){
    Local identity;check(ConvertStringSidToSidW(sid,&identity.value)!=0,"Cannot create isolated trustee");
    TRUSTEE_W trustee{};BuildTrusteeWithSidW(&trustee,identity.value);DWORD mask{};
    check(GetEffectiveRightsFromAclW(acl,&trustee,&mask)==ERROR_SUCCESS,"Cannot inspect isolated effective rights");
    GENERIC_MAPPING mapping{FILE_GENERIC_READ,FILE_GENERIC_WRITE,FILE_GENERIC_EXECUTE,FILE_ALL_ACCESS};MapGenericMask(&mask,&mapping);return mask;
}
void maintenance_acl_checks(){
    descriptor(maintenance_directory_acl);
    Local original,updated,repeated;
    check(ConvertStringSecurityDescriptorToSecurityDescriptorW(L"O:BAG:BAD:P(A;OICI;FA;;;SY)(A;OICI;FA;;;BA)",SDDL_REVISION_1,
        reinterpret_cast<PSECURITY_DESCRIPTOR*>(&original.value),nullptr)!=0,"Cannot create previous maintenance descriptor");
    PACL acl{};BOOL present{},defaulted{};check(GetSecurityDescriptorDacl(original.value,&present,&acl,&defaulted) && present,"Missing fixture DACL");
    maintenance_readable_acl(acl,updated);acl=static_cast<PACL>(updated.value);
    const auto users=rights(acl,L"S-1-5-32-545");
    check((users&(FILE_GENERIC_READ|FILE_GENERIC_EXECUTE))==(FILE_GENERIC_READ|FILE_GENERIC_EXECUTE),"Maintenance users cannot browse/read");
    check(!(users&(FILE_WRITE_DATA|FILE_APPEND_DATA|FILE_WRITE_EA|FILE_WRITE_ATTRIBUTES|FILE_DELETE_CHILD|DELETE|WRITE_DAC|WRITE_OWNER)),"Maintenance users gained modification rights");
    check(rights(acl,L"S-1-5-32-544")==FILE_ALL_ACCESS && rights(acl,L"S-1-5-18")==FILE_ALL_ACCESS,"Administrator/SYSTEM rights changed");
    void* entry{};check(GetAce(acl,0,&entry) && (static_cast<ACE_HEADER*>(entry)->AceFlags&(OBJECT_INHERIT_ACE|CONTAINER_INHERIT_ACE))==
        (OBJECT_INHERIT_ACE|CONTAINER_INHERIT_ACE),"Maintenance read access is not inherited");
    maintenance_readable_acl(acl,repeated);check(static_cast<PACL>(repeated.value)->AceCount==acl->AceCount,"Repeated maintenance grants accumulate ACEs");
}
}
int main() {
    fs::path scratch;
    try {
        for(const auto* path:{L"C:\\OutLoud",L"C:\\Out Loud\\WinUI"})
            check(installation_path(path).is_absolute(),"Valid local deployment path rejected");
        for(const auto* path:{L"C:\\",L"relative",L"\\\\server\\share\\OutLoud",L"C:\\OutLoud:file",L"C:\\OutLoud. ",L"C:\\bad?name",L"C:\\bad\"name"})
            refuses([&]{static_cast<void>(installation_path(path));},"Unsafe path accepted");
        descriptor(L"O:BAG:BAD:P(A;;FA;;;BA)(A;;FA;;;SY)(A;;FR;;;BU)");
        maintenance_acl_checks();
        refuses([&]{descriptor(L"O:BUG:BAD:P(A;;FA;;;BA)(A;;FA;;;SY)");},"User owner accepted");
        refuses([&]{descriptor(L"O:BAG:BAD:P(A;;FA;;;BA)(A;;FA;;;SY)(A;;FW;;;BU)");},"Ordinary user write rights accepted");
        refuses([&]{descriptor(L"O:BAG:BAD:P(A;;FA;;;BA)(A;;FA;;;SY)(A;;WD;;;BU)",true,true);},"Ordinary user replacement rights accepted");
        descriptor(L"O:BAG:BAD:P(A;;FA;;;BA)(A;;FA;;;SY)(A;IO;FW;;;BU)");
        descriptor(L"O:BAG:BAD:P(A;;FA;;;BA)(A;;FA;;;SY)(A;;SD;;;BU)",true,true,true);
        refuses([&]{descriptor(L"O:BAG:BAD:P(A;;FA;;;BA)(A;;FA;;;SY)(A;;SD;;;BU)",true,true,false);},"Unverified volume-root exception accepted");
        wchar_t temp[32768]{};check(GetTempPathW(32768,temp)>0,"Cannot locate isolated scratch directory");
        scratch=fs::path(temp)/(L"outloud-inspector-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64()));
        check(fs::create_directory(scratch),"Cannot create isolated scratch directory");
        const auto file=scratch/L"fixture.txt",link=scratch/L"linked.txt";
        write_new(file,L"unchanged bytes");
        {
            Handle pinned;pin(file,pinned);
            check(file_hash(pinned.value).size()==64,"SHA-256 identity missing");
            Handle writer;writer.value=CreateFileW(file.c_str(),GENERIC_WRITE,FILE_SHARE_READ,nullptr,OPEN_EXISTING,0,nullptr);
            check(writer.value==INVALID_HANDLE_VALUE,"Pinned file allowed a competing writer");
            check(!DeleteFileW(file.c_str()),"Pinned file allowed deletion");
        }
        check(CreateHardLinkW(link.c_str(),file.c_str(),nullptr)!=0,"Cannot create isolated hard-link fixture");
        refuses([&]{Handle pinned;pin(file,pinned);},"Hard-linked file accepted");
        check(fs::remove(link),"Cannot remove isolated hard-link fixture");
        {
            Handle directory;directory.value=CreateFileW(scratch.c_str(),READ_CONTROL|FILE_READ_ATTRIBUTES,
                FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_EXISTING,FILE_FLAG_BACKUP_SEMANTICS|FILE_FLAG_OPEN_REPARSE_POINT,nullptr);
            check(directory.value!=INVALID_HANDLE_VALUE,"Cannot inspect isolated directory");
            DWORD error{};check(!actual_volume_root(directory.value,error),"Ordinary directory acquired volume-root authority");
        }
        wchar_t own[32768]{};check(GetModuleFileNameW(nullptr,own,32768)>0,"Cannot locate checker executable");
        check(!pe_dependencies(fs::path(own)).empty(),"Existing AMD64 PE dependency reader failed");
        check(fs::remove(file) && fs::remove(scratch),"Cannot remove owned scratch fixtures");scratch.clear();
        std::cout<<"PASS: "<<checks<<" focused path, owner/ACL, file identity/pinning, hash and PE checks; no machine mutation.\n";
        return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<"\nRetained isolated scratch: "<<scratch<<"\n";return 1;}
}
