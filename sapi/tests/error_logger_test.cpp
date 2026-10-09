// Reuse the registration-free token/site/COM owners, not the full host campaign.
#define wmain existing_sapi_host_main
#include "host.cpp"
#undef wmain
#include "error_logger.hpp"
#include <future>
#include <regex>
#include <set>
#include <thread>

namespace {
namespace log = outloud::sapi::logging;
std::string contents(const std::filesystem::path& path) {
    std::ifstream file(path,std::ios::binary);
    return {std::istreambuf_iterator<char>(file),std::istreambuf_iterator<char>()};
}
void seed(const std::filesystem::path& path,std::size_t bytes,char value) {
    std::ofstream file(path,std::ios::binary|std::ios::trunc);
    const std::string block(4096,value);
    while (bytes) { const auto count=(std::min)(bytes,block.size()); file.write(block.data(),static_cast<std::streamsize>(count)); bytes-=count; }
    check(bool(file),"Cannot seed private log");
}
void clean_records(const std::string& records) {
    check(!records.empty() && records.back()=='\n',"Missing or incomplete log record");
    const std::regex pattern(R"(^\[[0-9]{4}-[0-9]{2}-[0-9]{2}T[0-9]{2}:[0-9]{2}:[0-9]{2}\.[0-9]{3}Z\] \[ERROR\] \[[A-Za-z]+\] \[[A-Z_]+\] \[HRESULT=0x[0-9A-F]{8} Win32=[0-9]+ Kind=[0-9]+ PID=[0-9]+ TID=[0-9]+\] [A-Za-z /.-]+\.$)");
    std::istringstream lines(records); std::string line;
    while (std::getline(lines,line)) {
        check(line.size()+1<log::record_limit && std::regex_match(line,pattern),"Interleaved, oversized or malformed log record");
    }
    for (const auto* forbidden : {"Hello traveler", "PRIVATE_WORKER_MESSAGE", "C:\\", "safetensors", "private-user", "secret", "bookmark-private"}) {
        check(records.find(forbidden)==records.npos,"Log exposes private data");
    }
}
void check_bounds(const std::filesystem::path& directory) {
    std::uint64_t total=0; unsigned count=0;
    for (const auto& item : std::filesystem::directory_iterator(directory)) {
        check(item.path().filename()==L"engine.log" || item.path().filename()==L"engine.1.log" || item.path().filename()==L"engine.2.log","Unexpected archive/file");
        const auto bytes=item.file_size(); check(bytes<=log::file_limit,"Log exceeds 2 MiB"); total+=bytes; ++count;
    }
    check(count<=3 && total<=3*log::file_limit,"Log set exceeds bounds");
}
void preserved_error_test(const std::filesystem::path& path,log::Fault fault) {
    log::configure(path.c_str(),fault);
    Com<ICreateErrorInfo> create; hr(CreateErrorInfo(create.put()),"Cannot create private error info");
    hr(create->SetDescription(const_cast<wchar_t*>(L"original COM diagnostic")),"Cannot set private error info");
    Com<IErrorInfo> original; hr(create->QueryInterface(IID_IErrorInfo,reinterpret_cast<void**>(original.put())),"No error interface");
    hr(SetErrorInfo(0,original.p),"Cannot publish private error info");
    SetLastError(12345);
    log::error({log::Event::synthesis,E_OUTOFMEMORY,ERROR_NOT_ENOUGH_MEMORY,log::Component::native,log::Kind::allocation});
    check(GetLastError()==12345,"Logger replaced original Win32 error");
    Com<IErrorInfo> returned; hr(GetErrorInfo(0,returned.put()),"Logger lost original IErrorInfo");
    check(returned.p==original.p,"Logger replaced original IErrorInfo identity");
}
int child_writer(const wchar_t* directory,const wchar_t* gate) {
    const auto event=OpenEventW(SYNCHRONIZE,FALSE,gate);
    check(event && WaitForSingleObject(event,10000)==WAIT_OBJECT_0,"Child start gate failed"); CloseHandle(event);
    log::configure(directory);
    for (unsigned i=0;i<32;++i) { log::error({log::Event::write,E_ACCESSDENIED,i}); }
    return 0;
}
void file_checks(const std::filesystem::path& scratch) {
    const auto empty=scratch/L"untouched";
    log::configure(empty.c_str()); check(!std::filesystem::exists(empty) && log::attempts()==0,"Logger initialized eagerly");
    log::error({log::Event::configuration,HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND),ERROR_FILE_NOT_FOUND,log::Component::model,log::Kind::asset_io});
    const auto record=contents(empty/L"engine.log"); clean_records(record);
    check(record.find("[Model] [TOKEN_CONFIGURATION]")!=record.npos && record.find("HRESULT=0x80070002 Win32=2")!=record.npos,"Error fields differ");

    const auto rotated=scratch/L"rotated"; std::filesystem::create_directory(rotated);
    seed(rotated/L"engine.log",static_cast<std::size_t>(log::file_limit-1),'A');
    seed(rotated/L"engine.1.log",7,'B'); seed(rotated/L"engine.2.log",7,'C');
    log::configure(rotated.c_str()); log::error({log::Event::write,E_FAIL});
    check(std::filesystem::file_size(rotated/L"engine.1.log")==log::file_limit-1,"Current archive not retained");
    check(contents(rotated/L"engine.2.log")==std::string(7,'B'),"Oldest not replaced by previous archive");
    clean_records(contents(rotated/L"engine.log")); check_bounds(rotated);
    for (const auto* name : {L"engine.log",L"engine.1.log",L"engine.2.log"}) { seed(rotated/name,static_cast<std::size_t>(log::file_limit+1),'X'); }
    log::error({log::Event::write,E_FAIL}); check_bounds(rotated); clean_records(contents(rotated/L"engine.log"));

    preserved_error_test(scratch/L"denied",log::Fault::directory_access);
    check(!std::filesystem::exists(scratch/L"denied"),"Denied logger created files");
    preserved_error_test(scratch/L"disk-full",log::Fault::disk_write);
    check(std::filesystem::file_size(scratch/L"disk-full"/L"engine.log")==0,"Disk failure wrote data");
    seed(scratch/L"not-a-directory",1,'X');
    preserved_error_test(scratch/L"not-a-directory"/L"logs",log::Fault::none);
    check(std::filesystem::is_regular_file(scratch/L"not-a-directory"),"Logger changed blocking file");
    log::configure(empty.c_str());
    const auto locked=CreateFileW((empty/L"engine.log").c_str(),GENERIC_READ,0,nullptr,OPEN_EXISTING,0,nullptr);
    check(locked!=INVALID_HANDLE_VALUE,"Cannot lock private log");
    log::error({log::Event::write,E_FAIL}); CloseHandle(locked);
    check(contents(empty/L"engine.log")==record,"Open failure damaged existing record");

    const auto contended=scratch/L"contended"; log::configure(contended.c_str());
    std::promise<void> ready,finish; auto finished=finish.get_future();
    std::thread holder([&] {
        const auto mutex=log::test_mutex_handle();
        check(mutex && WaitForSingleObject(mutex,1000)==WAIT_OBJECT_0,"Cannot acquire test mutex");
        ready.set_value(); finished.wait(); ReleaseMutex(mutex); CloseHandle(mutex);
    });
    ready.get_future().wait(); log::error({log::Event::write,E_FAIL});
    const bool dropped=!std::filesystem::exists(contended);
    finish.set_value(); holder.join(); check(dropped,"Busy mutex did not discard event");

    const auto threaded=scratch/L"threaded"; log::configure(threaded.c_str());
    std::vector<std::thread> threads;
    for (unsigned t=0;t<4;++t) {
        threads.emplace_back([t] { for (unsigned i=0;i<32;++i) { log::error({log::Event::write,E_ACCESSDENIED,1000+t*32+i}); } });
    }
    for (auto& thread : threads) { thread.join(); }
    check(log::attempts()==128,"Thread attempts lost");
    clean_records(contents(threaded/L"engine.log")); check_bounds(threaded);

    const auto multiprocess=scratch/L"multiprocess";
    wchar_t executable[32768]{}; check(GetModuleFileNameW(nullptr,executable,32768)>0,"Cannot find test executable");
    const auto gate=L"Local\\OutLoud.LoggerTest."+std::to_wstring(GetCurrentProcessId());
    const auto event=CreateEventW(nullptr,TRUE,FALSE,gate.c_str()); check(event!=nullptr,"Cannot create child gate");
    std::vector<HANDLE> processes;
    for (unsigned i=0;i<4;++i) {
        auto command=L"\""+std::wstring(executable)+L"\" --writer \""+multiprocess.native()+L"\" \""+gate+L"\"";
        STARTUPINFOW startup{}; startup.cb=sizeof(startup); PROCESS_INFORMATION process{};
        check(CreateProcessW(executable,command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,nullptr,&startup,&process)!=FALSE,"Cannot start test writer");
        CloseHandle(process.hThread); processes.push_back(process.hProcess);
    }
    SetEvent(event);
    for (const auto process : processes) {
        const auto wait=WaitForSingleObject(process,10000); DWORD code=1;
        check(wait==WAIT_OBJECT_0 && GetExitCodeProcess(process,&code) && !code,"Child logger failed"); CloseHandle(process);
    }
    CloseHandle(event);
    const auto concurrent=contents(multiprocess/L"engine.log"); clean_records(concurrent); check_bounds(multiprocess);
    std::set<std::string> pids; const std::regex pid("PID=([0-9]+)");
    for (std::sregex_iterator it(concurrent.begin(),concurrent.end(),pid),end; it!=end; ++it) { pids.insert((*it)[1]); }
    check(pids.size()==4,"Not all concurrent processes produced records");
    std::cout << "PASS: lazy/error fields/privacy, 2 MiB rotation/oversize bounds, directory/open/disk failures, original Win32/IErrorInfo, mutex timeout, 4 threads/4 processes.\n";
}
using Configure = void (__stdcall*)(const wchar_t*,unsigned);
using Attempts = std::uint64_t (__stdcall*)();
struct Adapter {
    HMODULE module;
    Com<IClassFactory> factory;
    Com<ISpTTSEngine> engine;
    Com<ISpObjectWithToken> with;
    Com<TestToken> token;
    HRESULT (__stdcall* unload)();
    Configure configure=nullptr;
    Attempts attempts=nullptr;
    explicit Adapter(const std::filesystem::path& dll) : module(LoadLibraryExW(dll.c_str(),nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_SYSTEM32)) {
        check(module!=nullptr,"Cannot load logger test adapter");
        const auto get=reinterpret_cast<HRESULT (__stdcall*)(REFCLSID,REFIID,void**)>(GetProcAddress(module,"DllGetClassObject"));
        unload=reinterpret_cast<HRESULT (__stdcall*)()>(GetProcAddress(module,"DllCanUnloadNow")); check(get && unload,"Missing COM exports");
        configure=reinterpret_cast<Configure>(GetProcAddress(module,"OutLoudLoggerTestConfigure"));
        attempts=reinterpret_cast<Attempts>(GetProcAddress(module,"OutLoudLoggerTestAttempts"));
        hr(get(outloud::sapi::engine_clsid,IID_IClassFactory,reinterpret_cast<void**>(factory.put())),"Cannot create factory");
        hr(factory->CreateInstance(nullptr,__uuidof(ISpTTSEngine),reinterpret_cast<void**>(engine.put())),"Cannot create engine");
        hr(engine->QueryInterface(__uuidof(ISpObjectWithToken),reinterpret_cast<void**>(with.put())),"No token interface"); token.p=new TestToken;
    }
    ~Adapter() { token.reset(); with.reset(); engine.reset(); factory.reset(); FreeLibrary(module); }
    void assets(const std::filesystem::path& model,const std::filesystem::path& tokenizer,const std::filesystem::path& voice) {
        token->values={{L"AssetRoot",model.parent_path().wstring()},{L"ModelPath",model.wstring()},
            {L"TokenizerPath",tokenizer.wstring()},{L"VoicePath",voice.wstring()},{L"Seed",L"73"},{L"KernelPath",L"Auto"}};
        hr(with->SetObjectToken(token.p),"Cannot assign valid assets");
    }
    std::vector<std::uint8_t> speech() {
        const auto wave=outloud::sapi::native_format(); auto f=fragment(L"Hello traveler."); Site site(Mode::normal,true);
        speak(engine.p,&wave,&f,site); check(site.bytes && site.bytes<=site.pcm.size(),"Short speech missing");
        site.pcm.resize(static_cast<std::size_t>(site.bytes)); return site.pcm;
    }
};
void adapter_checks(const std::filesystem::path& dll,const std::filesystem::path& model,
        const std::filesystem::path& tokenizer,const std::filesystem::path& voice,
        const std::filesystem::path& scratch,const std::filesystem::path& baseline) {
    Adapter adapter(dll); check(adapter.configure && adapter.attempts,"Private test exports missing");
    const auto quiet=scratch/L"successful";
    adapter.configure(quiet.c_str(),0); adapter.assets(model,tokenizer,voice);
    const auto expected=adapter.speech(); check(adapter.speech()==expected,"Repeated deterministic PCM differs");
    const auto wave=outloud::sapi::native_format(); auto spoken=fragment(L"Hello traveler.");
    Site entry(Mode::normal,false); entry.entry_abort=true; speak(adapter.engine.p,&wave,&spoken,entry);
    Site streaming(Mode::normal,false); streaming.abort_bytes=3840; speak(adapter.engine.p,&wave,&spoken,streaming);
    Site stopped(Mode::stopped,false); speak(adapter.engine.p,&wave,&spoken,stopped,SP_AUDIO_STOPPED);
    check(adapter.speech()==expected,"Cancellation recovery PCM differs");
    check(!adapter.attempts() && !std::filesystem::exists(quiet),"Success/cancel/stop entered logger or created files");
    if (!baseline.empty()) {
        Adapter old(baseline); old.assets(model,tokenizer,voice);
        check(!old.configure && old.speech()==expected,"Pre-logger/updated short PCM differs");
    }
    unsigned error_number=0;
    const auto one_error=[&](const auto& operation,const char* field,log::Fault fault=log::Fault::none) {
        const auto path=scratch/(L"error-"+std::to_wstring(++error_number));
        adapter.configure(path.c_str(),static_cast<unsigned>(fault)); operation();
        check(adapter.attempts()==1,"Error logged more/less than once");
        const auto records=contents(path/L"engine.log");
        if (fault==log::Fault::directory_access || fault==log::Fault::disk_write) { check(records.empty(),"Failure injection wrote a record"); }
        else { clean_records(records); check(records.find(field)!=records.npos && std::count(records.begin(),records.end(),'\n')==1,"Error classification/duplicate record differs"); }
    };
    const auto missing=scratch/L"private-user-secret-missing.olv";
    for (const auto* field : {L"ModelPath",L"TokenizerPath",L"VoicePath"}) {
        const auto original=adapter.token->values[field]; adapter.token->values[field]=missing.wstring();
        one_error([&] { check(adapter.with->SetObjectToken(adapter.token.p)==HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND),"Missing asset HRESULT changed"); },"HRESULT=0x80070002 Win32=2");
        adapter.token->values[field]=original;
    }
    const auto invalid=scratch/L"private-user-secret.olv"; seed(invalid,7,'X');
    for (const auto* field : {L"ModelPath",L"TokenizerPath",L"VoicePath"}) {
        const auto original=adapter.token->values[field]; adapter.token->values[field]=invalid.wstring();
        hr(adapter.with->SetObjectToken(adapter.token.p),"Cannot assign readable corrupt asset");
        one_error([&] { Site site(Mode::normal,false); speak(adapter.engine.p,&wave,&spoken,site,HRESULT_FROM_WIN32(ERROR_INVALID_DATA)); },"Kind=2");
        adapter.token->values[field]=original;
    }
    const auto missing_with_info=[&] {
        adapter.token->values[L"ModelPath"]=missing.wstring();
        check(adapter.with->SetObjectToken(adapter.token.p)==HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND),"Logger failure changed HRESULT");
        Com<IErrorInfo> info; hr(GetErrorInfo(0,info.put()),"Logger failure lost asset IErrorInfo"); BSTR raw=nullptr;
        hr(info->GetDescription(&raw),"Missing error description"); const std::wstring value(raw ? raw : L""); SysFreeString(raw);
        check(value.find(L"model")!=value.npos && value.find(L"private-user")==value.npos,"Original asset diagnostic changed");
        adapter.token->values[L"ModelPath"]=model.wstring();
    };
    one_error(missing_with_info,"",log::Fault::directory_access);
    one_error(missing_with_info,"",log::Fault::disk_write);
    adapter.configure(quiet.c_str(),0); adapter.assets(model,tokenizer,voice);
    one_error([&] { Site failed(Mode::failed,false); speak(adapter.engine.p,&wave,&spoken,failed,E_ACCESSDENIED); },"[SITE_WRITE] [HRESULT=0x80070005 Win32=5");
    auto mark=bookmark(L"bookmark-private"); mark.pNext=&spoken;
    one_error([&] { Site site(Mode::normal,false); site.interest_result=E_ACCESSDENIED; speak(adapter.engine.p,&wave,&mark,site,E_ACCESSDENIED); },"[BOOKMARK_INTEREST]");
    one_error([&] { Site site(Mode::normal,false); site.fail_event_after=0; speak(adapter.engine.p,&wave,&mark,site,E_ACCESSDENIED); },"[BOOKMARK_DELIVERY]");
    one_error([&] { Site site(Mode::normal,false); speak(adapter.engine.p,&wave,&spoken,site,E_FAIL); },"Unexpected native exception.",log::Fault::worker_exception);
    one_error([&] { Site site(Mode::normal,false); speak(adapter.engine.p,&wave,&spoken,site,E_OUTOFMEMORY); },"Resource allocation failed.",log::Fault::worker_allocation);
    adapter.configure(quiet.c_str(),0); check(adapter.speech()==expected,"Failed worker recovery PCM differs");
    check(!adapter.attempts() && !std::filesystem::exists(quiet),"Recovery entered logger");
    adapter.token.reset(); adapter.with.reset(); adapter.engine.reset(); adapter.factory.reset(); hr(adapter.unload(),"Logged DLL cannot unload");
    std::cout << "PASS: actual asset/SAPI/worker/resource failures once, unchanged HRESULT/IErrorInfo, private messages excluded, short deterministic old/new PCM, silent success/entry/stream abort/audio-stop/recovery, DLL unload. Short PCM bytes=" << expected.size() << ".\n";
}
}
int wmain(int argc,wchar_t** argv) {
    try {
        if (argc==4 && std::wstring_view(argv[1])==L"--writer") { return child_writer(argv[2],argv[3]); }
        check(argc==5 || argc==6,"Usage: outloud-error-logger-tests private-test.dll model.olp tokenizer.bin voice.olv [pre-logger.dll]");
        hr(CoInitializeEx(nullptr,COINIT_MULTITHREADED),"Logger test COM init failed");
        struct Uninit { ~Uninit() { CoUninitialize(); } } uninit;
        outloud::test::Scratch scratch; file_checks(scratch.path);
        adapter_checks(std::filesystem::absolute(argv[1]),std::filesystem::absolute(argv[2]),
            std::filesystem::absolute(argv[3]),std::filesystem::absolute(argv[4]),scratch.path,
            argc==6 ? std::filesystem::absolute(argv[5]) : std::filesystem::path{});
        return 0;
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
