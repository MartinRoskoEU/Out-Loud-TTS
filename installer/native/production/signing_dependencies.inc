// Read-only dependency verification, shared by pre-registration checks and --dependencies.
// OS API-set aliases are resolved by the Windows loader without running DLL entry points.
Signature verify_vendor(const fs::path& path,HANDLE file) {
    auto result=verify_signature(path,file);
    if(result.status!=TRUST_E_NOSIGNATURE)return result;
    // Windows components may be catalog-signed rather than embedded-signed.
    HCATADMIN admin{};GUID action=WINTRUST_ACTION_GENERIC_VERIFY_V2;
    signing_require(CryptCATAdminAcquireContext2(&admin,nullptr,BCRYPT_SHA256_ALGORITHM,nullptr,0),"catalog_context_failed");
    HCATINFO catalog{};
    try {
        DWORD size{};signing_require(CryptCATAdminCalcHashFromFileHandle2(admin,file,&size,nullptr,0) && size<=64,"catalog_hash_failed");
        std::vector<BYTE> hash(size);signing_require(CryptCATAdminCalcHashFromFileHandle2(admin,file,&size,hash.data(),0),"catalog_hash_failed");
        catalog=CryptCATAdminEnumCatalogFromHash(admin,hash.data(),size,0,nullptr);
        if(catalog){
            CATALOG_INFO info{};info.cbStruct=sizeof(info);signing_require(CryptCATCatalogInfoFromContext(catalog,&info,0),"catalog_info_failed");
            auto tag=hex(hash.data(),size);for(auto& ch:tag)ch=static_cast<wchar_t>(towupper(ch));
            WINTRUST_CATALOG_INFO member{};member.cbStruct=sizeof(member);member.pcwszCatalogFilePath=info.wszCatalogFile;
            member.pcwszMemberTag=tag.c_str();member.pcwszMemberFilePath=path.c_str();member.hMemberFile=file;
            member.pbCalculatedFileHash=hash.data();member.cbCalculatedFileHash=size;member.hCatAdmin=admin;
            WINTRUST_DATA data{};data.cbStruct=sizeof(data);data.dwUIChoice=WTD_UI_NONE;data.fdwRevocationChecks=WTD_REVOKE_WHOLECHAIN;
            data.dwUnionChoice=WTD_CHOICE_CATALOG;data.pCatalog=&member;data.dwStateAction=WTD_STATEACTION_VERIFY;
            data.dwProvFlags=WTD_REVOCATION_CHECK_CHAIN_EXCLUDE_ROOT|WTD_DISABLE_MD2_MD4|WTD_CACHE_ONLY_URL_RETRIEVAL;
            result.status=WinVerifyTrust(nullptr,&action,&data);
            if(result.status==ERROR_SUCCESS){
                const auto provider=WTHelperProvDataFromStateData(data.hWVTStateData);
                const auto signer=provider?WTHelperGetProvSignerFromChain(provider,0,FALSE,0):nullptr;
                if(signer && signer->csCertChain && signer->pasCertChain[0].pCert){wchar_t name[256]{};
                    CertGetNameStringW(signer->pasCertChain[0].pCert,CERT_NAME_SIMPLE_DISPLAY_TYPE,0,nullptr,name,256);result.publisher=name;}
            }
            data.dwStateAction=WTD_STATEACTION_CLOSE;WinVerifyTrust(nullptr,&action,&data);
        }
    }catch(...){if(catalog)CryptCATAdminReleaseCatalogContext(admin,catalog,0);CryptCATAdminReleaseContext(admin,0);throw;}
    if(catalog)CryptCATAdminReleaseCatalogContext(admin,catalog,0);CryptCATAdminReleaseContext(admin,0);return result;
}
std::vector<std::wstring> pe_dependencies(HANDLE file) {
    LARGE_INTEGER length{},zero{};
    signing_require(GetFileSizeEx(file,&length) && length.QuadPart>0 && length.QuadPart<=128*1024*1024,"dependency_size_invalid");
    signing_require(SetFilePointerEx(file,zero,nullptr,FILE_BEGIN),"dependency_read_failed");
    std::vector<BYTE> bytes(static_cast<size_t>(length.QuadPart));DWORD count{};
    signing_require(ReadFile(file,bytes.data(),static_cast<DWORD>(bytes.size()),&count,nullptr) && count==bytes.size(),"dependency_read_failed");
    const auto read=[&]<class T>(size_t at){signing_require(at<=bytes.size() && sizeof(T)<=bytes.size()-at,"dependency_pe_bounds");T value{};memcpy(&value,bytes.data()+at,sizeof(value));return value;};
    const auto dos=read.operator()<IMAGE_DOS_HEADER>(0);signing_require(dos.e_magic==IMAGE_DOS_SIGNATURE && dos.e_lfanew>0,"dependency_not_pe");
    const auto pe=read.operator()<IMAGE_NT_HEADERS64>(static_cast<size_t>(dos.e_lfanew));
    signing_require(pe.Signature==IMAGE_NT_SIGNATURE && pe.FileHeader.Machine==IMAGE_FILE_MACHINE_AMD64 &&
        pe.OptionalHeader.Magic==IMAGE_NT_OPTIONAL_HDR64_MAGIC && pe.FileHeader.NumberOfSections<=96,"dependency_not_amd64");
    const size_t table=static_cast<size_t>(dos.e_lfanew)+sizeof(DWORD)+sizeof(IMAGE_FILE_HEADER)+pe.FileHeader.SizeOfOptionalHeader;
    const auto offset=[&](DWORD rva)->size_t {
        if(rva<pe.OptionalHeader.SizeOfHeaders){signing_require(rva<bytes.size(),"dependency_pe_bounds");return rva;}
        for(WORD i=0;i<pe.FileHeader.NumberOfSections;++i){const auto section=read.operator()<IMAGE_SECTION_HEADER>(table+sizeof(IMAGE_SECTION_HEADER)*i);
            if(rva>=section.VirtualAddress && rva-section.VirtualAddress<section.SizeOfRawData){
                const size_t at=static_cast<size_t>(section.PointerToRawData)+rva-section.VirtualAddress;
                signing_require(at<bytes.size(),"dependency_pe_bounds");return at;}}
        throw SigningError("dependency_rva_invalid");};
    std::vector<std::wstring> names;
    const auto append=[&](DWORD rva,bool optional=false){size_t at=offset(rva);std::wstring name;
        while(at<bytes.size() && bytes[at] && name.size()<256)name+=static_cast<wchar_t>(bytes[at++]);
        signing_require(at<bytes.size() && !bytes[at] && !name.empty() && name.find_first_of(L"\\/:\r\n\"")==std::wstring::npos &&
            name.size()>4 && (_wcsicmp(name.c_str()+name.size()-4,L".dll")==0 || _wcsicmp(name.c_str()+name.size()-4,L".drv")==0 || _wcsicmp(name.c_str()+name.size()-4,L".exe")==0),"dependency_name_invalid");names.push_back((optional?L"?":L"")+name);};
    const auto imports=pe.OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if(imports.VirtualAddress){size_t at=offset(imports.VirtualAddress);unsigned descriptors{};
        for(;;at+=sizeof(IMAGE_IMPORT_DESCRIPTOR)){const auto d=read.operator()<IMAGE_IMPORT_DESCRIPTOR>(at);if(!d.Name)break;
            signing_require(++descriptors<=512,"dependency_import_limit");append(d.Name);}}
    // Delay descriptors in modern PE32+ images contain RVAs (attributes bit 0).
    const auto delay=pe.OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_DELAY_IMPORT];
    if(delay.VirtualAddress){size_t at=offset(delay.VirtualAddress);unsigned descriptors{};
        for(;;at+=32){const auto attributes=read.operator()<DWORD>(at),name=read.operator()<DWORD>(at+4);if(!name)break;
            signing_require((attributes&1) && ++descriptors<=512,"dependency_delay_import_invalid");append(name,true);}}
    return names;
}
std::vector<std::wstring> pe_dependencies(const fs::path& path) {
    Handle file;file.value=CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
    signing_require(file.value!=INVALID_HANDLE_VALUE,"dependency_inaccessible");return pe_dependencies(file.value);
}
std::wstring dependency_inventory(const fs::path& engine,HANDLE engine_file=INVALID_HANDLE_VALUE) {
    // A signing operation already holds a writable pin. Reopening its path with
    // read-only sharing would conflict with our own pin; inspect the held bytes.
    safe(engine);std::vector<std::wstring> pending=engine_file==INVALID_HANDLE_VALUE?pe_dependencies(engine):pe_dependencies(engine_file);std::set<std::wstring> inspected,aliases;
    wchar_t directory[MAX_PATH]{};signing_require(GetSystemDirectoryW(directory,MAX_PATH)>0,"system_directory_unavailable");
    const fs::path system=directory;const auto winsxs=system.parent_path()/L"WinSxS";std::wstring report=L"[dependencies]\nschema=1\nproject_dll=OutLoudPocketSAPI.dll\n";
    for(size_t next=0;next<pending.size();++next){
        auto alias=pending[next];const bool optional=alias.starts_with(L"?");if(optional)alias.erase(0,1);for(auto& ch:alias)ch=static_cast<wchar_t>(towlower(ch));if(!aliases.insert((optional?L"?":L"")+alias).second)continue;
        signing_require(aliases.size()<=2048,"dependency_closure_limit");
        // No DllMain, TLS callbacks or imports are executed/resolved by this mapping.
        const auto module=LoadLibraryExW(alias.c_str(),nullptr,DONT_RESOLVE_DLL_REFERENCES|LOAD_LIBRARY_SEARCH_SYSTEM32);
        if(!module && optional && GetLastError()==ERROR_MOD_NOT_FOUND){report+=L"optional_unavailable="+alias+L"\n";continue;}
        if(!module)std::wcout<<L"dependency_resolution_failed="<<alias<<L",error="<<GetLastError()<<L"\n";
        signing_require(module!=nullptr,"dependency_resolution_failed");wchar_t name[32768]{};
        const auto n=GetModuleFileNameW(module,name,32768);FreeLibrary(module);
        signing_require(n>0 && n<32768,"dependency_resolution_failed");const fs::path path=name;safe(path);
        bool os_path=_wcsicmp(path.parent_path().c_str(),system.c_str())==0;
        for(auto parent=path.parent_path();!parent.empty() && parent!=parent.parent_path();parent=parent.parent_path())
            if(_wcsicmp(parent.c_str(),winsxs.c_str())==0)os_path=true;
        signing_require(os_path,"dependency_outside_windows_runtime");
        auto normalized=path.native();for(auto& ch:normalized)ch=static_cast<wchar_t>(towlower(ch));if(!inspected.insert(normalized).second)continue;
        Handle file;file.value=CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
        signing_require(file.value!=INVALID_HANDLE_VALUE,"dependency_inaccessible");const auto signature=verify_vendor(path,file.value);
        const bool valid=signature.status==ERROR_SUCCESS && (signature.publisher==L"Microsoft Windows" ||
            signature.publisher==L"Microsoft Corporation" || signature.publisher==L"Microsoft Windows Publisher" || signature.publisher==L"Microsoft Windows Software Compatibility Publisher");
        if(!valid)std::wcout<<L"vendor_check_failed="<<path.filename().native()<<L",status="<<signature.status<<L"\n";
        signing_require(valid,"vendor_signature_invalid");
        report+=L"dll"+std::to_wstring(inspected.size())+L"="+path.filename().native()+L","+file_hash(file.value)+L",valid\n";
        const auto imports=pe_dependencies(path);pending.insert(pending.end(),imports.begin(),imports.end());
    }
    return report+L"vendor_count="+std::to_wstring(inspected.size())+L"\nvendor_signatures=verified\n";
}
