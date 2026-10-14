#include "prerequisites.hpp"
#include <windows.h>
#include <shlobj.h>
#include <hostfxr.h>
#include <bcrypt.h>
#include <wintrust.h>
#include <softpub.h>
#include "runtime_install.hpp"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
namespace fs=std::filesystem;
using namespace outloud::installer;
namespace {
void require(bool ok,const char* message) { if (!ok) throw std::runtime_error(message); }
struct Key { HKEY value{}; ~Key() { if(value) RegCloseKey(value); } };
struct Module { HMODULE value{}; ~Module() { if(value) FreeLibrary(value); } };
void safe(const fs::path& path) {
    require(path.is_absolute() && path.native().find(L"\\\\")!=0 && path.native().find(L':',2)==std::wstring::npos,"Expected a local absolute path.");
    for(auto current=path;!current.empty();current=current.parent_path()) {
        const auto attributes=GetFileAttributesW(current.c_str());
        if(attributes==INVALID_FILE_ATTRIBUTES) {
            const auto error=GetLastError(); require(error==ERROR_FILE_NOT_FOUND || error==ERROR_PATH_NOT_FOUND,"Path inspection failed.");
        } else require(!(attributes&FILE_ATTRIBUTE_REPARSE_POINT),"Reparse point refused.");
        if(current==current.parent_path()) break;
    }
}
unsigned machine(const fs::path& file) {
    safe(file); std::ifstream input(file,std::ios::binary); require(input.good(),"Cannot read runtime image.");
    IMAGE_DOS_HEADER dos{}; input.read(reinterpret_cast<char*>(&dos),sizeof(dos));
    require(input.good() && dos.e_magic==IMAGE_DOS_SIGNATURE && dos.e_lfanew>=static_cast<LONG>(sizeof(dos)) && dos.e_lfanew<1024*1024,"Invalid runtime PE header.");
    input.seekg(dos.e_lfanew); DWORD signature{}; IMAGE_FILE_HEADER header{};
    input.read(reinterpret_cast<char*>(&signature),sizeof(signature)); input.read(reinterpret_cast<char*>(&header),sizeof(header));
    require(input.good() && signature==IMAGE_NT_SIGNATURE,"Invalid runtime PE signature."); return header.Machine;
}
Version file_version(const fs::path& path) {
    DWORD unused=0; const auto size=GetFileVersionInfoSizeW(path.c_str(),&unused); require(size!=0,"Runtime version resource missing.");
    std::vector<unsigned char> data(size); require(GetFileVersionInfoW(path.c_str(),0,size,data.data())!=0,"Cannot read runtime version.");
    VS_FIXEDFILEINFO* info{}; UINT length{};
    require(VerQueryValueW(data.data(),L"\\",reinterpret_cast<void**>(&info),&length)!=0 && length>=sizeof(*info) && info->dwSignature==0xfeef04bd,"Invalid runtime version resource.");
    return {{{HIWORD(info->dwFileVersionMS),LOWORD(info->dwFileVersionMS),HIWORD(info->dwFileVersionLS),LOWORD(info->dwFileVersionLS)}}};
}
std::optional<std::wstring> registry_text(REGSAM view,const std::wstring& subkey,const wchar_t* name) {
    Key key; const auto opened=RegOpenKeyExW(HKEY_LOCAL_MACHINE,subkey.c_str(),0,KEY_READ|view,&key.value);
    if(opened==ERROR_FILE_NOT_FOUND) return {}; require(opened==ERROR_SUCCESS,"Registry key access failed.");
    DWORD type{},bytes{}; const auto queried=RegQueryValueExW(key.value,name,nullptr,&type,nullptr,&bytes);
    if(queried==ERROR_FILE_NOT_FOUND) return {}; require(queried==ERROR_SUCCESS && type==REG_SZ && bytes>=sizeof(wchar_t) && bytes<=32768,"Malformed registry string.");
    std::vector<wchar_t> value(bytes/sizeof(wchar_t)+1);
    require(RegQueryValueExW(key.value,name,nullptr,&type,reinterpret_cast<BYTE*>(value.data()),&bytes)==ERROR_SUCCESS,"Registry string changed.");
    return std::wstring(value.data());
}
std::optional<Version> vc_registration(REGSAM view,const wchar_t* arch) {
    Key key; const auto sub=std::wstring(L"SOFTWARE\\Microsoft\\VisualStudio\\14.0\\VC\\Runtimes\\")+arch;
    const auto opened=RegOpenKeyExW(HKEY_LOCAL_MACHINE,sub.c_str(),0,KEY_READ|view,&key.value);
    if(opened==ERROR_FILE_NOT_FOUND) return {}; require(opened==ERROR_SUCCESS,"VC registry access failed.");
    const auto number=[&](const wchar_t* name) {
        DWORD value{},type{},size=sizeof(value);
        require(RegQueryValueExW(key.value,name,nullptr,&type,reinterpret_cast<BYTE*>(&value),&size)==ERROR_SUCCESS && type==REG_DWORD && size==sizeof(value),"Malformed VC package metadata."); return value;
    };
    if(number(L"Installed")!=1) return {};
    Version v{{number(L"Major"),number(L"Minor"),number(L"Bld"),number(L"Rbld")}};
    const auto supplied=registry_text(view,sub,L"Version");
    require(supplied && supplied->size()>1,"Missing VC version metadata.");
    const auto parsed=version(supplied->front()==L'v'?supplied->substr(1):*supplied);
    require(parsed && *parsed==v,"VC version fields disagree."); return v;
}
WindowsFacts windows() {
    WindowsFacts facts;
    OSVERSIONINFOEXW os{}; os.dwOSVersionInfoSize=sizeof(os);
#pragma warning(push)
#pragma warning(disable:4996)
    const auto ok=GetVersionExW(reinterpret_cast<OSVERSIONINFOW*>(&os));
#pragma warning(pop)
    USHORT process{},native{};
    if(!ok || !IsWow64Process2(GetCurrentProcess(),&process,&native)) { facts.failed=true; return facts; }
    facts.major=os.dwMajorVersion; facts.minor=os.dwMinorVersion; facts.build=os.dwBuildNumber;
    facts.product=os.wProductType; facts.machine=native; return facts;
}
VCFacts vc(Version minimum) {
    VCFacts facts;
    try {
        for(const auto view:{KEY_WOW64_64KEY,KEY_WOW64_32KEY}) {
            const auto installed=vc_registration(view,L"x64");
            if(installed && (!facts.installed || *installed>*facts.installed)) facts.installed=installed;
        }
        if(!facts.installed) {
            for(const auto view:{KEY_WOW64_64KEY,KEY_WOW64_32KEY})
                facts.other_arch|=vc_registration(view,L"x86").has_value() || vc_registration(view,L"arm64").has_value();
            return facts;
        }
        wchar_t system[MAX_PATH]{}; require(GetSystemDirectoryW(system,MAX_PATH)!=0,"Cannot resolve System32.");
        for(const auto name:{L"msvcp140.dll",L"msvcp140_atomic_wait.dll",L"vcruntime140.dll",L"vcruntime140_1.dll"}) {
            const auto path=fs::path(system)/name;
            facts.files_valid&=machine(path)==IMAGE_FILE_MACHINE_AMD64 && file_version(path)>=minimum;
        }
    } catch(...) { facts.failed=true; }
    return facts;
}
std::optional<std::wstring> environment(const wchar_t* name) {
    const auto size=GetEnvironmentVariableW(name,nullptr,0);
    if(!size) { require(GetLastError()==ERROR_ENVVAR_NOT_FOUND,"Environment query failed."); return {}; }
    std::vector<wchar_t> value(size); require(GetEnvironmentVariableW(name,value.data(),size)>0,"Environment changed."); return std::wstring(value.data());
}
fs::path default_dotnet(bool x86=false) {
    PWSTR raw{}; require(SUCCEEDED(SHGetKnownFolderPath(x86?FOLDERID_ProgramFilesX86:FOLDERID_ProgramFiles,0,nullptr,&raw)),"Cannot resolve Program Files.");
    const fs::path result=fs::path(raw)/L"dotnet"; CoTaskMemFree(raw); return result;
}
fs::path dotnet_root() {
    // Match apphost precedence; never find dotnet.exe through the caller's PATH.
    for(const auto name:{L"DOTNET_ROOT_X64",L"DOTNET_ROOT"}) {
        if(const auto supplied=environment(name)) {
            const auto root=fs::absolute(*supplied); safe(root);
            // Apphost ignores environment roots whose directory does not exist.
            if(fs::is_directory(root)) return root;
        }
    }
    if(const auto root=registry_text(KEY_WOW64_32KEY,L"SOFTWARE\\dotnet\\Setup\\InstalledVersions\\x64",L"InstallLocation")) return *root;
    return default_dotnet();
}
std::vector<Version> versions(const fs::path& path) {
    safe(path); std::vector<Version> result;
    if(!fs::exists(path)) return result;
    for(const auto& entry:fs::directory_iterator(path)) {
        safe(entry.path()); if(entry.is_directory()) if(const auto v=version(entry.path().filename().wstring())) result.push_back(*v);
    }
    return result;
}
struct Resolution { DotNetFacts* facts; fs::path desktop,core; bool failed{}; };
void __cdecl resolved(const hostfxr_resolve_frameworks_result* result,void* context) noexcept {
    auto& output=*static_cast<Resolution*>(context);
    try {
        require(result!=nullptr,"No framework resolution result.");
        output.facts->resolution_seen=true;
        for(size_t i=0;i<result->resolved_count;++i) {
            const auto& f=result->resolved_frameworks[i]; require(f.name && f.resolved_version && f.resolved_path,"Malformed resolved framework.");
            if(std::wstring(f.name)==L"Microsoft.WindowsDesktop.App") { output.facts->resolved_desktop=version(f.resolved_version); output.desktop=f.resolved_path; }
            if(std::wstring(f.name)==L"Microsoft.NETCore.App") { output.facts->resolved_core=version(f.resolved_version); output.core=f.resolved_path; }
        }
    } catch(...) { output.failed=true; }
}
void __cdecl quiet_error(const wchar_t*) {} // Exit/status diagnostics remain structured.
DotNetFacts dotnet(const fs::path& config,std::int32_t& host_code) {
    DotNetFacts facts;
    try {
        safe(config); require(fs::is_regular_file(config),"Runtime configuration missing.");
        const auto root=dotnet_root(); safe(root);
        if(!fs::exists(root/L"dotnet.exe")) {
            facts.wrong_arch=!versions(default_dotnet(true)/L"shared/Microsoft.WindowsDesktop.App").empty(); return facts;
        }
        if(machine(root/L"dotnet.exe")!=IMAGE_FILE_MACHINE_AMD64) { facts.wrong_arch=true; return facts; }
        facts.desktop=versions(root/L"shared/Microsoft.WindowsDesktop.App"); facts.core=versions(root/L"shared/Microsoft.NETCore.App");
        if(facts.desktop.empty() || facts.core.empty() || std::none_of(facts.desktop.begin(),facts.desktop.end(),[](Version v) { return v.parts[0]>=10; })) return facts;
        const auto fxr_versions=versions(root/L"host/fxr"); if(fxr_versions.empty()) return facts;
        const auto newest=*std::max_element(fxr_versions.begin(),fxr_versions.end());
        auto name=text(newest); if(newest.parts[3]==0) name=name.substr(0,name.rfind(L'.'));
        const auto path=root/L"host/fxr"/name/L"hostfxr.dll";
        require(machine(path)==IMAGE_FILE_MACHINE_AMD64,"Hostfxr is not x64.");
        Module module{LoadLibraryExW(path.c_str(),nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_SYSTEM32)};
        require(module.value!=nullptr,"Cannot load installed hostfxr.");
        const auto resolve=reinterpret_cast<hostfxr_resolve_frameworks_for_runtime_config_fn>(GetProcAddress(module.value,"hostfxr_resolve_frameworks_for_runtime_config"));
        const auto writer=reinterpret_cast<hostfxr_set_error_writer_fn>(GetProcAddress(module.value,"hostfxr_set_error_writer"));
        require(resolve && writer,"Installed hostfxr lacks the .NET 10 framework-resolution API.");
        const auto previous=writer(quiet_error);
        const hostfxr_initialize_parameters parameters{sizeof(parameters),nullptr,root.c_str()};
        Resolution resolution{&facts,{},{},false};
        host_code=resolve(config.c_str(),&parameters,resolved,&resolution); writer(previous);
        require(GetModuleHandleW(L"coreclr.dll")==nullptr,"Framework detection unexpectedly loaded the CLR.");
        facts.host_ok=host_code==0; require(!resolution.failed,"Framework resolution callback failed.");
        if(facts.host_ok) {
            require(!resolution.desktop.empty() && !resolution.core.empty(),"Required framework resolution result missing.");
            facts.files_valid=machine(resolution.desktop/L"PresentationNative_cor3.dll")==IMAGE_FILE_MACHINE_AMD64 &&
                machine(resolution.core/L"coreclr.dll")==IMAGE_FILE_MACHINE_AMD64 && fs::is_regular_file(resolution.desktop/L"PresentationFramework.dll");
        }
    } catch(...) { facts.failed=true; }
    return facts;
}
std::wstring read_policy(const fs::path& path,const wchar_t* key) {
    safe(path); require(fs::is_regular_file(path),"Requirements file missing.");
    wchar_t value[256]{}; const auto length=GetPrivateProfileStringW(L"requirements",key,L"",value,256,path.c_str());
    require(length>0 && length<255,"Requirements value missing or too long."); return value;
}
std::wstring ini(const Result& win,const Result& crt,const Result& net,Version minimum,std::int32_t code) {
    std::wostringstream out; out<<L"[results]\nschema=1\nwindows="<<win.status<<L"\nvc="<<crt.status<<L"\ndotnet="<<net.status<<L"\n";
    out<<L"windows_detail="<<win.detail<<L"\nwindows_build="<<win.installed<<L"\nvc_detail="<<crt.detail<<L"\nvc_installed="<<crt.installed<<L"\nvc_minimum="<<text(minimum);
    out<<L"\ndotnet_detail="<<net.detail<<L"\ndotnet_installed="<<net.installed<<L"\nhostfxr_code="<<code<<L"\n";
    out<<L"vc_action="<<acquisition_action(L"vc",crt.status)<<L"\ndotnet_action="<<acquisition_action(L"dotnet",net.status)<<L"\n"; return out.str();
}
void write_report(const fs::path& path,const std::wstring& report) {
    safe(path); const auto handle=CreateFileW(path.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
    require(handle!=INVALID_HANDLE_VALUE,"Report destination exists or cannot be created.");
    const std::wstring data=L"\xfeff"+report; DWORD written{};
    const auto bytes=static_cast<DWORD>(data.size()*sizeof(wchar_t));
    const auto ok=WriteFile(handle,data.data(),bytes,&written,nullptr); CloseHandle(handle);
    require(ok && written==bytes,"Cannot write diagnostic report.");
}
#include "packages.inc"
}
int wmain(int argc,wchar_t** argv) {
    try {
        if(argc>1 && (std::wstring(argv[1])==L"--verify-package" || std::wstring(argv[1])==L"--ensure-package"))
            return package_command(argc,argv);
        require(argc==5 || argc==7,"Usage: OutLoudPrerequisites --requirements file.ini --runtime-config app.runtimeconfig.json [--report new-file.ini]");
        require(std::wstring(argv[1])==L"--requirements" && std::wstring(argv[3])==L"--runtime-config","Invalid detector arguments.");
        if(argc==7) require(std::wstring(argv[5])==L"--report","Invalid report argument.");
        const fs::path policy=argv[2],config=argv[4];
        const auto minimum=version(read_policy(policy,L"vc_minimum")); require(minimum.has_value(),"Invalid VC requirement.");
        const auto desktop_min=version(read_policy(policy,L"desktop_minimum")),core_min=version(read_policy(policy,L"core_minimum"));
        require(desktop_min && core_min && desktop_min->parts[0]==10 && core_min->parts[0]==10,"Invalid Desktop requirements.");
        const auto win=evaluate(windows());
        const auto crt=evaluate(vc(*minimum),*minimum); std::int32_t host_code=-1;
        const auto net=evaluate(dotnet(config,host_code),*desktop_min,*core_min);
        const auto report=ini(win,crt,net,*minimum,host_code); std::wcout<<report;
        if(argc==7) write_report(argv[6],report);
        if(win.status==L"detection_failed") return 2;
        if(win.status!=L"compatible") return 3;
        if(crt.status==L"detection_failed" || net.status==L"detection_failed") return 2;
        return crt.status==L"compatible" && net.status==L"compatible"?0:1;
    } catch(const std::exception& error) { std::cerr<<"detection_failed: "<<error.what()<<"\n"; return 2; }
}
