#include "mutation.hpp"
#include "voice_library_fs.hpp"
#include "windows_deployment_trust.hpp"
#include <shellapi.h>
#include <aclapi.h>
#include <array>
#include <charconv>
#include <cstring>

namespace {
namespace mutation = outloud::frontend::mutation;
using Handle = outloud::detail::library_fs::Handle;
void require(bool value) { if (!value) { throw std::runtime_error("Helper transport or authorization failure."); } }
bool administrator() {
    SID_IDENTIFIER_AUTHORITY authority = SECURITY_NT_AUTHORITY; PSID sid = nullptr; BOOL member = FALSE;
    if (!AllocateAndInitializeSid(&authority,2,SECURITY_BUILTIN_DOMAIN_RID,DOMAIN_ALIAS_RID_ADMINS,0,0,0,0,0,0,&sid)) { return false; }
    const auto ok = CheckTokenMembership(nullptr,sid,&member); FreeSid(sid); return ok && member;
}
std::filesystem::path image(HANDLE process) {
    std::array<wchar_t,32768> buffer{}; DWORD size = static_cast<DWORD>(buffer.size());
    require(QueryFullProcessImageNameW(process,0,buffer.data(),&size)!=0); return std::wstring(buffer.data(),size);
}
void transfer(HANDLE pipe,HANDLE parent,std::span<std::uint8_t> bytes,bool writing) {
    while (!bytes.empty()) {
        Handle event(CreateEventW(nullptr,TRUE,FALSE,nullptr)); require(event.value!=nullptr);
        OVERLAPPED overlapped{}; overlapped.hEvent = event.value; DWORD transferred = 0;
        const auto started = writing ? WriteFile(pipe,bytes.data(),static_cast<DWORD>(bytes.size()),nullptr,&overlapped) :
            ReadFile(pipe,bytes.data(),static_cast<DWORD>(bytes.size()),nullptr,&overlapped);
        if (!started) {
            require(GetLastError()==ERROR_IO_PENDING);
            const HANDLE waits[]{event.value,parent};
            const auto waited = WaitForMultipleObjects(2,waits,FALSE,15000);
            if (waited!=WAIT_OBJECT_0) { CancelIoEx(pipe,&overlapped); (void)GetOverlappedResult(pipe,&overlapped,&transferred,TRUE); require(false); }
        }
        require(GetOverlappedResult(pipe,&overlapped,&transferred,TRUE)!=0 && transferred>0 && transferred<=bytes.size());
        bytes = bytes.subspan(transferred);
    }
}
struct WorkerTrustPolicy {
    void require(bool value,const char* reason){if(!value)throw std::runtime_error(reason);}
    bool api(BOOL value,const char*){return value!=FALSE;}
    void security(PSECURITY_DESCRIPTOR){}
    void owner(const std::wstring&){}
    void ace(DWORD,ACE_HEADER*){}
    void principal(const std::wstring&){}
    std::wstring certificate_hash(PCCERT_CONTEXT certificate,ALG_ID algorithm){
        std::array<BYTE,32> bytes{};DWORD length=static_cast<DWORD>(bytes.size());
        require(CryptHashCertificate(0,algorithm,0,certificate->pbCertEncoded,certificate->cbCertEncoded,bytes.data(),&length)!=0,"certificate_hash_failed");
        return outloud::detail::library_fs::wide(outloud::detail::hex({bytes.data(),length}));
    }
};
struct TrustedWorker {
    using Pins=outloud::detail::library_fs::Pins;
    using File=outloud::detail::library_fs::Handle;
    std::vector<Pins> parents;
    std::vector<File> files;
    WorkerTrustPolicy policy;
    void validate(HANDLE handle,bool ancestor=false){
        PSECURITY_DESCRIPTOR descriptor=nullptr;
        require(GetSecurityInfo(handle,SE_FILE_OBJECT,OWNER_SECURITY_INFORMATION|DACL_SECURITY_INFORMATION,
            nullptr,nullptr,nullptr,nullptr,&descriptor)==ERROR_SUCCESS);
        struct Free { PSECURITY_DESCRIPTOR p;~Free(){LocalFree(p);} } free{descriptor};
        DWORD error=ERROR_SUCCESS;
        const bool volume=ancestor && outloud::detail::deployment_trust::actual_volume_root(handle,error);
        outloud::detail::deployment_trust::require_admin_descriptor(descriptor,ancestor,ancestor,volume,policy);
    }
    TrustedWorker(const std::filesystem::path& application,const std::filesystem::path& helper){
        namespace fs=outloud::detail::library_fs;
        const auto root=application.parent_path();parents.emplace_back(root,false,false,READ_CONTROL);
        wchar_t filesystem[MAX_PATH]{};
        require(GetDriveTypeW(root.root_path().c_str())==DRIVE_FIXED &&
            GetVolumeInformationW(root.root_path().c_str(),nullptr,0,nullptr,nullptr,nullptr,filesystem,MAX_PATH) && _wcsicmp(filesystem,L"NTFS")==0);
        // Request ACL-read access when the ancestor pins are first opened.
        for(size_t i=0;i<parents.front().handles.size();++i)
            validate(parents.front().handles[i].value,i+1<parents.front().handles.size());
        // Deny replacement of every managed/native dependency until the worker exits.
        // The existing deployment authority admits only SYSTEM/Administrators writers.
        for(const auto& entry:std::filesystem::recursive_directory_iterator(root)){
            if(entry.is_directory()){
                auto directory=fs::directory(entry.path(),READ_CONTROL);validate(directory.value);
                parents.emplace_back(entry.path());
            }else{
                auto file=fs::file(entry.path(),GENERIC_READ|READ_CONTROL);validate(file.value);files.push_back(std::move(file));
            }
        }
        auto own=fs::file(helper);const auto signer=outloud::detail::deployment_trust::verify_signature(helper,own.value,policy);
        require(signer.status==ERROR_SUCCESS && !signer.certificate_sha256.empty());
        for(const auto& path:{application,root/L"OutLoud.WinUI.dll"}){
            auto file=fs::file(path);
            const auto signature=outloud::detail::deployment_trust::verify_signature(path,file.value,policy);
            require(signature.status==ERROR_SUCCESS && signature.certificate_sha256==signer.certificate_sha256);
        }
    }
};
int shared_data_worker(HANDLE pipe,const std::filesystem::path& application,const std::filesystem::path& helper,DWORD parent_id) {
    // Reuse the installed managed services. Only this authenticated pipe is inherited;
    // the caller cannot supply executable paths, shell commands, or storage roots.
    TrustedWorker trusted(application,helper);
    SIZE_T size=0;
    InitializeProcThreadAttributeList(nullptr,1,0,&size);
    std::vector<unsigned char> attributes(size);
    auto list=reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(attributes.data());
    require(InitializeProcThreadAttributeList(list,1,0,&size)!=0);
    struct Cleanup { LPPROC_THREAD_ATTRIBUTE_LIST p; ~Cleanup(){DeleteProcThreadAttributeList(p);} } cleanup{list};
    require(SetHandleInformation(pipe,HANDLE_FLAG_INHERIT,HANDLE_FLAG_INHERIT)!=0);
    require(UpdateProcThreadAttribute(list,0,PROC_THREAD_ATTRIBUTE_HANDLE_LIST,&pipe,sizeof(pipe),nullptr,nullptr)!=0);
    STARTUPINFOEXW startup{};startup.StartupInfo.cb=sizeof(startup);startup.lpAttributeList=list;
    startup.StartupInfo.dwFlags=STARTF_USESHOWWINDOW;startup.StartupInfo.wShowWindow=SW_HIDE;
    auto command=L"\""+application.native()+L"\" --machine-data-worker "+
        std::to_wstring(reinterpret_cast<std::uintptr_t>(pipe))+L" "+std::to_wstring(parent_id);
    PROCESS_INFORMATION process{};
    const auto created=CreateProcessW(application.c_str(),command.data(),nullptr,nullptr,TRUE,
        EXTENDED_STARTUPINFO_PRESENT|CREATE_NO_WINDOW,nullptr,application.parent_path().c_str(),&startup.StartupInfo,&process);
    const auto cleared=SetHandleInformation(pipe,HANDLE_FLAG_INHERIT,0);
    require(created!=0);Handle child(process.hProcess),thread(process.hThread);require(cleared!=0);
    require(WaitForSingleObject(child.value,INFINITE)==WAIT_OBJECT_0);
    DWORD result=1;require(GetExitCodeProcess(child.value,&result)!=0);return static_cast<int>(result);
}
}
int WINAPI wWinMain(HINSTANCE,HINSTANCE,PWSTR,int) {
    try {
        require(SetDefaultDllDirectories(LOAD_LIBRARY_SEARCH_SYSTEM32)!=0 && administrator());
        int count = 0; wchar_t** arguments = CommandLineToArgvW(GetCommandLineW(),&count); require(arguments!=nullptr);
        struct Free { wchar_t** value; ~Free() { LocalFree(value); } } free{arguments};
        require(count==3);
        const std::wstring endpoint(arguments[1]), parent_text(arguments[2]);
        const bool storage=endpoint.starts_with(L"OutLoud.Storage.");
        const std::wstring_view prefix = storage?L"OutLoud.Storage.":L"OutLoud.Import.";
        require(endpoint.starts_with(prefix) && endpoint.size()==prefix.size()+32);
        for (const auto character : std::wstring_view(endpoint).substr(prefix.size())) { require((character>=L'0' && character<=L'9') || (character>=L'a' && character<=L'f')); }
        require(!parent_text.empty() && parent_text.size()<=10);
        DWORD parent_id = 0;
        for (const auto character : parent_text) { require(character>=L'0' && character<=L'9' && parent_id<=429496729U); const auto next = parent_id*10+static_cast<DWORD>(character-L'0'); require(next>=parent_id); parent_id = next; }
        require(parent_id!=0);
        Handle parent(OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION|SYNCHRONIZE,FALSE,parent_id)); require(parent.value!=nullptr);
        DWORD own_session = 0, parent_session = 0;
        require(ProcessIdToSessionId(GetCurrentProcessId(),&own_session) && ProcessIdToSessionId(parent_id,&parent_session) && own_session==parent_session);
        const auto parent_image = image(parent.value), own_image = image(GetCurrentProcess());
        require(_wcsicmp(parent_image.filename().c_str(),L"OutLoud.WinUI.exe")==0 &&
            _wcsicmp(parent_image.parent_path().c_str(),own_image.parent_path().c_str())==0);
        const auto path = L"\\\\.\\pipe\\"+endpoint;
        Handle pipe(CreateFileW(path.c_str(),GENERIC_READ|GENERIC_WRITE,0,nullptr,OPEN_EXISTING,
            FILE_FLAG_OVERLAPPED|SECURITY_SQOS_PRESENT|SECURITY_IDENTIFICATION,nullptr));
        require(pipe.value!=INVALID_HANDLE_VALUE);
        ULONG server = 0; require(GetNamedPipeServerProcessId(pipe.value,&server)!=0 && server==parent_id);
        if(storage)return shared_data_worker(pipe.value,parent_image,own_image,parent_id);
        std::vector<std::uint8_t> bytes(16); transfer(pipe.value,parent.value,bytes,false);
        std::uint32_t length = 0; std::memcpy(&length,bytes.data()+12,4); require(length<=mutation::limit-16);
        bytes.resize(16+length); transfer(pipe.value,parent.value,std::span(bytes).subspan(16),false);
        mutation::Result result;
        try {
            const auto request = mutation::decode_request(bytes);
            // The authenticated WinUI caller uses only native import/removal.
            require(request.kind==mutation::Operation::import_native || request.kind==mutation::Operation::remove);
            auto started = mutation::encode_result({mutation::Outcome::started,{},{}}); transfer(pipe.value,parent.value,started,true);
            // Parent must still be alive at dispatch. Once dispatched, finish the
            // two native operations rather than forcibly kill during mutation.
            require(WaitForSingleObject(parent.value,0)==WAIT_TIMEOUT);
            const auto resolve=[&] {
                auto runtime=mutation::resolve_machine_runtime();
                // This application's own deployed baseline, never another app,
                // the caller's PATH, or a development/cache fallback.
                runtime.rust_runtime=parent_image.parent_path()/L"runtime"/L"pocket-tts"/L"pocket-tts.exe";
                return runtime;
            };
            result = mutation::import_and_install(request,outloud::VoiceLibrary(),outloud::windows_sapi_voice_registry(),resolve);
        } catch (const std::exception&) {
            result.error.domain = "helper"; result.error.code = "invalid_request"; result.error.message = "Voice mutation request was malformed or unsupported.";
        }
        auto response = mutation::encode_result(result); transfer(pipe.value,parent.value,response,true);
        return 0;
    } catch (...) { return 1; } // No exception crosses the process entry boundary.
}
