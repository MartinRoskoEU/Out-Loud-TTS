#include "mutation.hpp"
#include "voice_library_fs.hpp"
#include <shellapi.h>
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
}
int WINAPI wWinMain(HINSTANCE,HINSTANCE,PWSTR,int) {
    try {
        require(SetDefaultDllDirectories(LOAD_LIBRARY_SEARCH_SYSTEM32)!=0 && administrator());
        int count = 0; wchar_t** arguments = CommandLineToArgvW(GetCommandLineW(),&count); require(arguments!=nullptr);
        struct Free { wchar_t** value; ~Free() { LocalFree(value); } } free{arguments};
        require(count==3);
        const std::wstring endpoint(arguments[1]), parent_text(arguments[2]);
        constexpr std::wstring_view prefix = L"OutLoud.Import.";
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
        require(_wcsicmp(parent_image.filename().c_str(),L"OutLoud.VoiceManager.exe")==0 &&
            _wcsicmp(parent_image.parent_path().c_str(),own_image.parent_path().c_str())==0);
        const auto path = L"\\\\.\\pipe\\"+endpoint;
        Handle pipe(CreateFileW(path.c_str(),GENERIC_READ|GENERIC_WRITE,0,nullptr,OPEN_EXISTING,
            FILE_FLAG_OVERLAPPED|SECURITY_SQOS_PRESENT|SECURITY_IDENTIFICATION,nullptr));
        require(pipe.value!=INVALID_HANDLE_VALUE);
        ULONG server = 0; require(GetNamedPipeServerProcessId(pipe.value,&server)!=0 && server==parent_id);
        std::vector<std::uint8_t> bytes(16); transfer(pipe.value,parent.value,bytes,false);
        std::uint32_t length = 0; std::memcpy(&length,bytes.data()+12,4); require(length<=mutation::limit-16);
        bytes.resize(16+length); transfer(pipe.value,parent.value,std::span(bytes).subspan(16),false);
        mutation::Result result;
        try {
            const auto request = mutation::decode_request(bytes);
            auto started = mutation::encode_result({mutation::Outcome::started,{},{}}); transfer(pipe.value,parent.value,started,true);
            // Parent must still be alive at dispatch. Once dispatched, finish the
            // two native operations rather than forcibly kill during mutation.
            require(WaitForSingleObject(parent.value,0)==WAIT_TIMEOUT);
            result = mutation::import_and_install(request,outloud::VoiceLibrary(),outloud::windows_sapi_voice_registry(),mutation::resolve_machine_runtime);
        } catch (const std::exception&) {
            result.error.domain = "helper"; result.error.code = "invalid_request"; result.error.message = "Import request was malformed or unsupported.";
        }
        auto response = mutation::encode_result(result); transfer(pipe.value,parent.value,response,true);
        return 0;
    } catch (...) { return 1; } // No exception crosses the process entry boundary.
}
