#pragma once
#include "asset_diagnostics.hpp"
#include "asset_io.hpp"
#include "outloud/assets.hpp"
#include "outloud/tokenizer.hpp"
#include "scratch.hpp"

void collection_checks() {
    using namespace outloud::sapi;
    text_checks();
    const auto reject=[](const auto& action,HRESULT expected) {
        try { action(); } catch (const Failure& error) { check(error.code==expected,"Collection error classification differs"); return; }
        throw std::runtime_error("Oversized collection accepted");
    };
    const auto too_large=HRESULT_FROM_WIN32(ERROR_BUFFER_OVERFLOW);
    std::wstring text_limit(maximum_text_units,L'x'); auto exact=fragment(text_limit);
    check(collect_request(&exact).text.size()==maximum_text_units,"Exact text limit rejected or truncated");
    auto extra=fragment(L"x"); exact.pNext=&extra;
    reject([&] { (void)collect_request(&exact); },too_large); exact.pNext=nullptr;
    // A lying length must be rejected before reading its one-character buffer.
    extra.ulTextLen=ULONG_MAX; reject([&] { (void)collect_request(&extra); },too_large);
    std::size_t total=SIZE_MAX;
    reject([&] { collect_add(total,1,SIZE_MAX); },too_large);
    total=1; reject([&] { collect_add(total,SIZE_MAX,SIZE_MAX); },too_large);
    std::vector<SPVTEXTFRAG> chain(maximum_fragments+1);
    for (std::size_t i=0;i<chain.size();++i) {
        chain[i].State.eAction=SPVA_Section; chain[i].pNext=i+1<chain.size() ? &chain[i+1] : nullptr;
    }
    chain[maximum_fragments-1].pNext=nullptr;
    check(collect_request(chain.data()).text.empty(),"Exact fragment limit rejected");
    chain[maximum_fragments-1].pNext=&chain.back(); reject([&] { (void)collect_request(chain.data()); },too_large);
    chain.resize(maximum_bookmarks+1);
    for (std::size_t i=0;i<chain.size();++i) {
        chain[i]=bookmark(L"b"); chain[i].pNext=i+1<chain.size() ? &chain[i+1] : nullptr;
    }
    chain[maximum_bookmarks-1].pNext=nullptr;
    check(collect_request(chain.data()).bookmarks.size()==maximum_bookmarks,"Exact bookmark count rejected");
    chain[maximum_bookmarks-1].pNext=&chain.back(); reject([&] { (void)collect_request(chain.data()); },too_large);
    std::wstring metadata(maximum_bookmark_units,L'b'); auto mark=bookmark(metadata);
    check(collect_request(&mark).bookmarks[0]==metadata,"Exact metadata limit rejected");
    auto tail=bookmark(L"b"); mark.pNext=&tail; reject([&] { (void)collect_request(&mark); },too_large);
    auto spoken=fragment(L"ordinary quest text"); spoken.pNext=&mark;
    reject([&] { (void)collect_request(&spoken); },too_large); // Ignored trailing metadata is bounded too.
    unsigned polls=0;
    const auto cancelled=collect_request(&exact,{[](void* p) { return ++*static_cast<unsigned*>(p)==3; },&polls});
    check(cancelled.cancelled && cancelled.text.empty() && cancelled.bookmarks.empty(),"Collection abort retained partial speech");
    auto cycle=fragment(L"cycle"); cycle.pNext=&cycle;
    reject([&] { (void)collect_request(&cycle); },E_INVALIDARG);
    auto nul=bookmark(std::wstring_view(L"a\0b",3)); reject([&] { (void)collect_request(&nul); },E_INVALIDARG);
    std::cout << "PASS: collection normal/Unicode, exact and excessive limits, overflow, cycle and cancellation; no inference.\n";
}

void diagnostic_checks(const std::filesystem::path& dll) {
    using namespace outloud;
    using namespace outloud::sapi;
    hr(CoInitializeEx(nullptr,COINIT_MULTITHREADED),"Diagnostic COM initialization failed");
    struct Uninit { ~Uninit() { CoUninitialize(); } } uninit;
    test::Scratch scratch;
    const auto absent=scratch.path/L"missing.asset", invalid=scratch.path/L"invalid.asset";
    { std::ofstream file(invalid,std::ios::binary); file << "invalid asset"; }
    const auto expect=[&](const auto& action,HRESULT expected) {
        try { action(); } catch (const AssetError& error) { check(asset_hresult(error)==expected,"Asset HRESULT classification differs"); return; }
        throw std::runtime_error("Invalid asset accepted");
    };
    expect([&] { detail::MappedFile file(absent,1024); },HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND));
    expect([&] { Tokenizer tokenizer(absent); },HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND));
    expect([&] { detail::MappedFile file(scratch.path,1024); },E_ACCESSDENIED);
    expect([&] { detail::MappedFile file(invalid,1); },HRESULT_FROM_WIN32(ERROR_INVALID_DATA));
    expect([&] { (void)load_asset<PocketModelAsset>(invalid,"model"); },HRESULT_FROM_WIN32(ERROR_INVALID_DATA));
    expect([&] { (void)load_asset<Tokenizer>(invalid,"tokenizer"); },HRESULT_FROM_WIN32(ERROR_INVALID_DATA));
    const auto locked=CreateFileW(invalid.c_str(),GENERIC_READ,0,nullptr,OPEN_EXISTING,0,nullptr);
    check(locked!=INVALID_HANDLE_VALUE,"Cannot lock private fixture");
    try { expect([&] { detail::MappedFile file(invalid,1024); },HRESULT_FROM_WIN32(ERROR_SHARING_VIOLATION)); }
    catch (...) { CloseHandle(locked); throw; } CloseHandle(locked);
    check(asset_hresult(AssetError(AssetErrorCode::io,"map failure",ERROR_NOT_ENOUGH_MEMORY))==HRESULT_FROM_WIN32(ERROR_NOT_ENOUGH_MEMORY),"Mapping failure lost Win32 reason");
    check(asset_hresult(AssetError(AssetErrorCode::compatibility,"mismatch"))==HRESULT_FROM_WIN32(ERROR_REVISION_MISMATCH),"Compatibility HRESULT differs");
    const auto module=LoadLibraryExW(std::filesystem::absolute(dll).c_str(),nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_SYSTEM32);
    check(module!=nullptr,"Cannot load private test DLL");
    struct Free { HMODULE module; ~Free() { FreeLibrary(module); } } free{module};
    using GetClass=HRESULT (__stdcall*)(REFCLSID,REFIID,void**);
    const auto get=reinterpret_cast<GetClass>(GetProcAddress(module,"DllGetClassObject")); check(get!=nullptr,"Missing COM export");
    Com<IClassFactory> factory; hr(get(engine_clsid,IID_IClassFactory,reinterpret_cast<void**>(factory.put())),"Diagnostic factory failed");
    Com<ISpTTSEngine> engine; hr(factory->CreateInstance(nullptr,__uuidof(ISpTTSEngine),reinterpret_cast<void**>(engine.put())),"Diagnostic engine failed");
    Com<ISpObjectWithToken> with; hr(engine->QueryInterface(__uuidof(ISpObjectWithToken),reinterpret_cast<void**>(with.put())),"Token interface missing");
    Com<TestToken> token; token.p=new TestToken;
    token->values={{L"AssetRoot",scratch.path.wstring()},{L"ModelPath",invalid.wstring()},
        {L"TokenizerPath",invalid.wstring()},{L"VoicePath",(scratch.path/L"invalid.olv").wstring()}};
    std::filesystem::copy_file(invalid,scratch.path/L"invalid.olv");
    const auto check_info=[](const wchar_t* role) {
        Com<IErrorInfo> info; hr(GetErrorInfo(0,info.put()),"Asset diagnostic missing on caller"); BSTR description=nullptr;
        hr(info->GetDescription(&description),"Asset diagnostic description missing");
        const std::wstring value(description ? description : L""); SysFreeString(description);
        check(value.find(role)!=value.npos && value.find(L"invalid.asset")==value.npos,"Diagnostic lacks role or exposes path");
    };
    for (const auto* field:{L"ModelPath",L"TokenizerPath"}) {
        token->values[field]=absent.wstring();
        check(with->SetObjectToken(token.p)==HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND),"Missing asset COM HRESULT differs");
        check_info(field==std::wstring_view(L"ModelPath") ? L"model" : L"tokenizer"); token->values[field]=invalid.wstring();
    }
    hr(with->SetObjectToken(token.p),"Invalid fixture token setup failed");
    auto speech=fragment(L"private speech must not appear in diagnostics"); Site site(Mode::normal,false); const auto wave=native_format();
    speak(engine.p,&wave,&speech,site,HRESULT_FROM_WIN32(ERROR_INVALID_DATA)); check_info(L"model");
    check(!site.calls,"Invalid runtime produced audio");
    std::cout << "PASS: missing/inaccessible/invalid assets, tokenizer, mapping/compatibility HRESULTs and caller COM diagnostics; no inference/registry.\n";
}
