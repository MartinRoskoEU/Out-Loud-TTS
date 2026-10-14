// Included in the bootstrap's private namespace to reuse its read-only file,
// version, architecture, INI and report helpers. No dependency on target SDKs.
struct FilePin {
    HANDLE value=INVALID_HANDLE_VALUE;
    ~FilePin() { if(value!=INVALID_HANDLE_VALUE) CloseHandle(value); }
    FilePin()=default;
    FilePin(const FilePin&)=delete;
    FilePin& operator=(const FilePin&)=delete;
};
struct TrustState {
    GUID action=WINTRUST_ACTION_GENERIC_VERIFY_V2;
    WINTRUST_DATA data{};
    ~TrustState() {
        if(data.hWVTStateData) { data.dwStateAction=WTD_STATEACTION_CLOSE; WinVerifyTrust(nullptr,&action,&data); }
    }
};
struct PackagePolicy {
    std::wstring component,hash,product,signer_cn,organization,architecture;
    Version file_ver;
    unsigned image_machine{};
    unsigned long long bytes{};
};
std::wstring package_value(const fs::path& manifest,const std::wstring& component,const wchar_t* key) {
    safe(manifest); require(fs::is_regular_file(manifest),"Package manifest missing.");
    wchar_t buffer[1024]{};
    const auto length=GetPrivateProfileStringW(component.c_str(),key,L"",buffer,1024,manifest.c_str());
    require(length>0 && length<1023,"Package manifest field missing or too long."); return buffer;
}
PackagePolicy package_policy(const fs::path& manifest,const std::wstring& component) {
    require(component==L"vc" || component==L"dotnet","Unknown package component.");
    PackagePolicy p; p.component=component;
    p.hash=package_value(manifest,component,L"sha256");
    require(p.hash.size()==64 && p.hash.find_first_not_of(L"0123456789abcdef")==std::wstring::npos,"Invalid pinned package hash.");
    p.product=package_value(manifest,component,L"product_name");
    p.signer_cn=package_value(manifest,component,L"signer_cn"); p.organization=package_value(manifest,component,L"signer_organization");
    require(p.organization==L"Microsoft Corporation" && (p.signer_cn==L"Microsoft Corporation" || p.signer_cn==L".NET"),"Unexpected package publisher policy.");
    p.architecture=package_value(manifest,component,L"architecture");
    require(p.architecture==L"x64" && p.product.find(L"(x64)")!=std::wstring::npos,"Only pinned x64 runtime bundles are supported.");
    const auto v=version(package_value(manifest,component,L"file_version")); require(v.has_value(),"Invalid package file version."); p.file_ver=*v;
    const auto size=package_value(manifest,component,L"bytes"); size_t read{}; p.bytes=std::stoull(size,&read);
    require(read==size.size() && p.bytes>0 && p.bytes<=256ull*1024*1024,"Invalid pinned package size.");
    require(package_value(manifest,component,L"bootstrap_machine")==L"014c","Unexpected bootstrap architecture policy.");
    p.image_machine=IMAGE_FILE_MACHINE_I386; return p;
}
std::wstring sha256(HANDLE file) {
    BCRYPT_ALG_HANDLE algorithm{}; BCRYPT_HASH_HANDLE hash{};
    require(BCryptOpenAlgorithmProvider(&algorithm,BCRYPT_SHA256_ALGORITHM,nullptr,0)>=0,"SHA-256 provider unavailable.");
    try {
        require(BCryptCreateHash(algorithm,&hash,nullptr,0,nullptr,0,0)>=0,"SHA-256 initialization failed.");
        std::array<unsigned char,65536> buffer{}; DWORD read{}; LARGE_INTEGER start{};
        require(SetFilePointerEx(file,start,nullptr,FILE_BEGIN)!=0,"Cannot rewind package.");
        for(;;) {
            require(ReadFile(file,buffer.data(),static_cast<DWORD>(buffer.size()),&read,nullptr)!=0,"Package read failed.");
            if(!read) break;
            require(BCryptHashData(hash,buffer.data(),read,0)>=0,"SHA-256 update failed.");
        }
        std::array<unsigned char,32> digest{};
        require(BCryptFinishHash(hash,digest.data(),static_cast<ULONG>(digest.size()),0)>=0,"SHA-256 completion failed.");
        BCryptDestroyHash(hash); hash=nullptr; BCryptCloseAlgorithmProvider(algorithm,0); algorithm=nullptr;
        std::wstring result;
        for(const auto byte:digest) { result+=L"0123456789abcdef"[byte>>4]; result+=L"0123456789abcdef"[byte&15]; }
        return result;
    } catch(...) { if(hash) BCryptDestroyHash(hash); if(algorithm) BCryptCloseAlgorithmProvider(algorithm,0); throw; }
}
std::wstring certificate_attribute(PCCERT_CONTEXT cert,const char* oid) {
    const auto size=CertGetNameStringW(cert,CERT_NAME_ATTR_TYPE,0,const_cast<char*>(oid),nullptr,0);
    require(size>1 && size<1024,"Signer certificate identity missing.");
    std::vector<wchar_t> value(size);
    require(CertGetNameStringW(cert,CERT_NAME_ATTR_TYPE,0,const_cast<char*>(oid),value.data(),size)==size,"Signer identity query failed.");
    return value.data();
}
std::wstring product_name(const fs::path& path) {
    DWORD unused{}; const auto size=GetFileVersionInfoSizeW(path.c_str(),&unused); require(size!=0,"Package version resource missing.");
    std::vector<unsigned char> buffer(size);
    require(GetFileVersionInfoW(path.c_str(),0,size,buffer.data())!=0,"Package version resource failed.");
    struct Translation { WORD language,codepage; }; Translation* translations{}; UINT length{};
    require(VerQueryValueW(buffer.data(),L"\\VarFileInfo\\Translation",reinterpret_cast<void**>(&translations),&length)!=0 && length>=sizeof(Translation),"Package resource translation missing.");
    wchar_t key[128]{};
    swprintf_s(key,L"\\StringFileInfo\\%04x%04x\\ProductName",translations[0].language,translations[0].codepage);
    wchar_t* value{};
    require(VerQueryValueW(buffer.data(),key,reinterpret_cast<void**>(&value),&length)!=0 && length>1,"Package product identity missing.");
    return value;
}
PackageFacts verify_package(const PackagePolicy& p,const fs::path& path,FilePin& pin) {
    safe(path); PackageFacts facts;
    pin.value=CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
    require(pin.value!=INVALID_HANDLE_VALUE,"Cannot pin downloaded package against writes/deletion.");
    LARGE_INTEGER bytes{}; require(GetFileSizeEx(pin.value,&bytes)!=0,"Package size query failed.");
    facts.size=static_cast<unsigned long long>(bytes.QuadPart)==p.bytes;
    if(!facts.size) return facts;
    facts.hash=sha256(pin.value)==p.hash; if(!facts.hash) return facts;
    WINTRUST_FILE_INFO info{}; info.cbStruct=sizeof(info); info.pcwszFilePath=path.c_str(); info.hFile=pin.value;
    TrustState trust; trust.data.cbStruct=sizeof(trust.data); trust.data.dwUIChoice=WTD_UI_NONE;
    trust.data.fdwRevocationChecks=WTD_REVOKE_WHOLECHAIN;
    trust.data.dwUnionChoice=WTD_CHOICE_FILE; trust.data.pFile=&info; trust.data.dwStateAction=WTD_STATEACTION_VERIFY;
    trust.data.dwProvFlags=WTD_REVOCATION_CHECK_CHAIN_EXCLUDE_ROOT;
    facts.signature=WinVerifyTrust(nullptr,&trust.action,&trust.data)==ERROR_SUCCESS;
    if(!facts.signature) return facts;
    const auto provider=WTHelperProvDataFromStateData(trust.data.hWVTStateData);
    const auto signer=provider?WTHelperGetProvSignerFromChain(provider,0,FALSE,0):nullptr;
    require(signer && signer->csCertChain>0 && signer->pasCertChain[0].pCert,"Trusted signer unavailable.");
    const auto certificate=signer->pasCertChain[0].pCert;
    facts.publisher=certificate_attribute(certificate,szOID_COMMON_NAME)==p.signer_cn &&
        certificate_attribute(certificate,szOID_ORGANIZATION_NAME)==p.organization;
    if(!facts.publisher) return facts;
    const auto product=product_name(path);
    facts.architecture=machine(path)==p.image_machine && product.find(L"(x64)")!=std::wstring::npos && p.architecture==L"x64";
    facts.version=file_version(path)==p.file_ver && product==p.product;
    return facts;
}
struct RuntimeOperations {
    PackagePolicy package;
    fs::path file,requirements,config,log;
    bool consented{};
    FilePin pin;
    std::wstring verification=L"not_attempted";
    std::wstring detected_status=L"detection_failed";
    std::uint32_t vendor_exit=0xffffffff;
    std::wstring detect() {
        if(package.component==L"vc") {
            const auto minimum=version(read_policy(requirements,L"vc_minimum")); require(minimum.has_value(),"Invalid VC floor.");
            detected_status=evaluate(vc(*minimum),*minimum).status; return detected_status;
        }
        const auto desktop=version(read_policy(requirements,L"desktop_minimum")),core=version(read_policy(requirements,L"core_minimum"));
        require(desktop && core,"Invalid framework floors."); std::int32_t code=-1;
        detected_status=evaluate(dotnet(config,code),*desktop,*core).status; return detected_status;
    }
    bool consent() const { return consented; }
    bool acquire() const { safe(file); return fs::is_regular_file(file); } // Inno performs the progress/cancellable download.
    PackageFacts verify() { const auto facts=verify_package(package,file,pin); verification=package_decision(facts); return facts; }
    std::uint32_t execute() {
        require(evaluate(windows()).status==L"compatible","Unsupported Windows system.");
        require(IsUserAnAdmin()!=FALSE,"Runtime installation requires administrator privileges.");
        safe(log); require(!fs::exists(log),"Vendor log destination already exists.");
        // The same read pin remains held from hashing through process completion.
        auto command=L"\""+file.native()+L"\" /install /passive /norestart /log \""+log.native()+L"\"";
        STARTUPINFOW start{}; start.cb=sizeof(start); PROCESS_INFORMATION process{};
        if(!CreateProcessW(file.c_str(),command.data(),nullptr,nullptr,FALSE,0,nullptr,file.parent_path().c_str(),&start,&process)) return vendor_exit;
        CloseHandle(process.hThread);
        const auto waited=WaitForSingleObject(process.hProcess,INFINITE);
        DWORD code{}; const auto got=GetExitCodeProcess(process.hProcess,&code); CloseHandle(process.hProcess);
        require(waited==WAIT_OBJECT_0 && got!=FALSE,"Cannot determine vendor installer completion.");
        vendor_exit=code; return vendor_exit;
    }
};
int package_command(int argc,wchar_t** argv) {
    if(std::wstring(argv[1])==L"--verify-package") {
        require(argc==5 || argc==7,"Usage: --verify-package manifest.ini component package.exe [--report new.ini]");
        if(argc==7) require(std::wstring(argv[5])==L"--report","Invalid report argument.");
        const auto policy=package_policy(argv[2],argv[3]); FilePin pin;
        const auto result=package_decision(verify_package(policy,argv[4],pin));
        const auto report=L"[results]\nschema=1\npackage="+result+L"\n";
        std::wcout<<report; if(argc==7) write_report(argv[6],report);
        return result==L"verified"?0:1;
    }
    require(argc==14,"Usage: --ensure-package manifest.ini component package.exe --requirements file.ini --runtime-config app.json --consented --log new.log --report new.ini");
    require(std::wstring(argv[5])==L"--requirements" && std::wstring(argv[7])==L"--runtime-config" &&
        std::wstring(argv[9])==L"--consented" && std::wstring(argv[10])==L"--log" && std::wstring(argv[12])==L"--report","Invalid runtime installation arguments.");
    require(evaluate(windows()).status==L"compatible","Unsupported Windows system.");
    const fs::path report_path=argv[13]; safe(report_path);
    require(!fs::exists(report_path) && fs::is_directory(report_path.parent_path()),"Report destination already exists or parent missing.");
    RuntimeOperations operations;
    operations.package=package_policy(argv[2],argv[3]); operations.file=argv[4];
    operations.requirements=argv[6]; operations.config=argv[8]; operations.consented=true; operations.log=argv[11];
    const auto result=ensure_runtime(operations.package.component,operations);
    const auto report=L"[results]\nschema=1\ninstallation="+result+L"\nverification="+operations.verification+
        L"\npost_status="+operations.detected_status+L"\nvendor_exit="+std::to_wstring(operations.vendor_exit)+L"\n";
    std::wcout<<report; write_report(report_path,report);
    return result==L"verified"?0:1;
}
