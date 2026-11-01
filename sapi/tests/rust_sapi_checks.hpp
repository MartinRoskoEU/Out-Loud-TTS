#pragma once
#include <tlhelp32.h>
#include <atomic>
#include <thread>

namespace {
struct RustCheckHandle {
    HANDLE value = nullptr;
    ~RustCheckHandle() { if (value && value!=INVALID_HANDLE_VALUE) { CloseHandle(value); } }
};
DWORD rust_child(const std::filesystem::path& executable) {
    RustCheckHandle snapshot{CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS,0)};
    check(snapshot.value!=INVALID_HANDLE_VALUE,"Cannot inspect Rust SAPI child");
    PROCESSENTRY32W entry{}; entry.dwSize=sizeof(entry);
    DWORD found=0;
    for (BOOL more=Process32FirstW(snapshot.value,&entry); more; more=Process32NextW(snapshot.value,&entry)) {
        if (entry.th32ParentProcessID!=GetCurrentProcessId()) { continue; }
        RustCheckHandle process{OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION,FALSE,entry.th32ProcessID)};
        if (!process.value) { continue; }
        std::array<wchar_t,32768> name{}; DWORD size=static_cast<DWORD>(name.size());
        if (QueryFullProcessImageNameW(process.value,0,name.data(),&size) && _wcsicmp(name.data(),executable.c_str())==0) {
            check(!found,"Multiple Rust runtimes remained resident in SAPI check"); found=entry.th32ProcessID;
        }
    }
    check(found!=0,"Rust SAPI created no runtime process"); return found;
}
// Actual SAPI output sink, counting/scanning only: no utterance audio storage.
class RustCountingStream final : public IStream {
public:
    std::atomic<ULONG> references{1}, writes{0};
    std::atomic<ULONGLONG> bytes{0}, position{0};
    std::atomic<bool> nonzero{false};
    RustCheckHandle first{CreateEventW(nullptr,TRUE,FALSE,nullptr)}, release{CreateEventW(nullptr,TRUE,FALSE,nullptr)};
    RustCountingStream() { check(first.value && release.value,"Cannot create SAPI output gates"); }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid,void** out) override {
        if (!out) { return E_POINTER; } *out=nullptr;
        if (iid!=IID_IUnknown && iid!=IID_ISequentialStream && iid!=IID_IStream) { return E_NOINTERFACE; }
        *out=static_cast<IStream*>(this); AddRef(); return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++references; }
    ULONG STDMETHODCALLTYPE Release() override { const auto n=--references; if (!n) { delete this; } return n; }
    HRESULT STDMETHODCALLTYPE Read(void*,ULONG,ULONG*) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE Write(const void* data,ULONG count,ULONG* written) override {
        if (!written || (count && !data)) { return E_POINTER; } *written=0;
        if (count%2) { return E_INVALIDARG; }
        if (!count) { return S_OK; }
        const auto* input=static_cast<const std::uint8_t*>(data);
        if (std::any_of(input,input+count,[](auto byte) { return byte!=0; })) { nonzero=true; }
        bytes+=count; position+=count;
        if (writes.fetch_add(1)==0) {
            SetEvent(first.value);
            if (WaitForSingleObject(release.value,5000)!=WAIT_OBJECT_0) { return HRESULT_FROM_WIN32(ERROR_TIMEOUT); }
        }
        *written=count; return S_OK;
    }
    HRESULT STDMETHODCALLTYPE Seek(LARGE_INTEGER move,DWORD origin,ULARGE_INTEGER* out) override {
        if (origin>STREAM_SEEK_END) { return STG_E_INVALIDFUNCTION; }
        const auto base=origin==STREAM_SEEK_SET ? 0ULL : origin==STREAM_SEEK_CUR ? position.load() : bytes.load();
        const auto next=static_cast<LONGLONG>(base)+move.QuadPart;
        if (next<0) { return STG_E_INVALIDFUNCTION; }
        position=static_cast<ULONGLONG>(next); if (out) { out->QuadPart=position; } return S_OK;
    }
    HRESULT STDMETHODCALLTYPE SetSize(ULARGE_INTEGER size) override { bytes=size.QuadPart; return S_OK; }
    HRESULT STDMETHODCALLTYPE CopyTo(IStream*,ULARGE_INTEGER,ULARGE_INTEGER*,ULARGE_INTEGER*) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE Commit(DWORD) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE Revert() override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE LockRegion(ULARGE_INTEGER,ULARGE_INTEGER,DWORD) override { return STG_E_INVALIDFUNCTION; }
    HRESULT STDMETHODCALLTYPE UnlockRegion(ULARGE_INTEGER,ULARGE_INTEGER,DWORD) override { return STG_E_INVALIDFUNCTION; }
    HRESULT STDMETHODCALLTYPE Stat(STATSTG* out,DWORD) override {
        if (!out) { return E_POINTER; } *out={}; out->type=STGTY_STREAM; out->cbSize.QuadPart=bytes; return S_OK;
    }
    HRESULT STDMETHODCALLTYPE Clone(IStream**) override { return E_NOTIMPL; }
};

int rust_sapi_checks(wchar_t** args) {
    const auto dll=std::filesystem::absolute(args[2]);
    const auto runtime=std::filesystem::absolute(args[3]);
    const auto config=std::filesystem::absolute(args[4]);
    const auto voice_path=std::filesystem::absolute(args[5]);
    struct Module { HMODULE value; ~Module() { if (value) { FreeLibrary(value); } } } module{
        LoadLibraryExW(dll.c_str(),nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_SYSTEM32)};
    check(module.value!=nullptr,"Cannot load Rust-enabled SAPI DLL");
    const bool managed=std::wstring_view(args[1])==L"--managed-rust";
    if (managed) {
        using SetRoots=HRESULT (__stdcall*)(const wchar_t*,const wchar_t*);
        const auto roots=reinterpret_cast<SetRoots>(GetProcAddress(module.value,"OutLoudTestSelectionRoots"));
        check(roots!=nullptr,"Managed checks require the private test DLL");
        const auto library=voice_path.parent_path().parent_path().parent_path();
        hr(roots(config.c_str(),library.c_str()),"Cannot set isolated selection fixtures");
    }
    using GetClass=HRESULT (__stdcall*)(REFCLSID,REFIID,void**);
    const auto get=reinterpret_cast<GetClass>(GetProcAddress(module.value,"DllGetClassObject"));
    check(get!=nullptr,"Missing SAPI class factory export");
    Com<IClassFactory> factory;
    hr(get(outloud::sapi::engine_clsid,IID_IClassFactory,reinterpret_cast<void**>(factory.put())),"Cannot obtain SAPI factory");
    Com<TestToken> token; token.p=new TestToken;
    token->factory=factory.p;
    token->values={{L"Backend",L"Rust"},{L"RustRuntimePath",runtime.native()},{L"RustConfigPath",config.native()},
        {L"VoicePath",voice_path.native()},{L"TestId",L"OutLoud_Rust_InMemory"},{L"Language",L"409"}};
    if (managed) {
        token->values.erase(L"RustConfigPath");
        token->values[L"OutLoudManagedOwner"]=L"OutLoud.ManagedVoice.1";
        token->values[L"LibraryId"]=voice_path.parent_path().filename().native();
    }
    {
        Com<ISpTTSEngine> engine;
        hr(factory->CreateInstance(nullptr,__uuidof(ISpTTSEngine),reinterpret_cast<void**>(engine.put())),"Cannot create Rust SAPI engine");
        Com<ISpObjectWithToken> with;
        hr(engine->QueryInterface(__uuidof(ISpObjectWithToken),reinterpret_cast<void**>(with.put())),"Missing SAPI token interface");
        hr(with->SetObjectToken(token.p),"Rust development token rejected");
        GUID format{}; WAVEFORMATEX* raw=nullptr;
        hr(engine->GetOutputFormat(nullptr,nullptr,&format,&raw),"Cannot negotiate Rust SAPI format");
        struct Wave { WAVEFORMATEX* value; ~Wave() { CoTaskMemFree(value); } } wave{raw};
        check(format==SPDFID_WaveFormatEx && outloud::sapi::valid_format(raw),"Rust SAPI output format changed");
        auto speech=fragment(L"Hello from the Rust SAPI engine.");
        auto marker=fragment(L"RustStart"); marker.State.eAction=SPVA_Bookmark; marker.pNext=&speech;
        auto bad=*raw; bad.nSamplesPerSec=44100;
        Site rejected(Mode::normal,false); speak(engine.p,&bad,&speech,rejected,SPERR_UNSUPPORTED_FORMAT);
        Site cold_abort(Mode::normal,false); cold_abort.variable_frames=true; cold_abort.timed_abort=true;
        speak(engine.p,raw,&speech,cold_abort);
        check(cold_abort.abort_observations && !cold_abort.write_after_abort && cold_abort.abort_latency_ms<5000,
            "Cold Rust startup did not observe SAPI abort");
        Site partial(Mode::partial,false); partial.variable_frames=true; partial.controls=true; partial.rate=2;
        speak(engine.p,raw,&marker,partial);
        check(partial.bytes>0 && partial.nonzero_pcm && partial.frames>1 && partial.calls>partial.frames && !partial.remaining &&
            partial.bookmarks.size()==1 && partial.bookmarks[0].name==L"RustStart" && partial.rates && partial.volumes && partial.skip_completions,
            "Rust SAPI partial writes, PCM or controls failed");
        const auto pid=rust_child(runtime);
        RustCheckHandle child{OpenProcess(SYNCHRONIZE|PROCESS_QUERY_INFORMATION,FALSE,pid)};
        check(child.value!=nullptr,"Cannot retain Rust SAPI process handle");
        if (managed) {
            PROCESS_MITIGATION_IMAGE_LOAD_POLICY policy{};
            check(GetProcessMitigationPolicy(child.value,ProcessImageLoadPolicy,&policy,sizeof(policy)) && policy.PreferSystem32Images,
                "Managed Rust process did not prefer system libraries");
            // This mode is invoked by the isolated selection checker only.
            check(config.parent_path().filename().native().starts_with(L"outloud-selection-"),"Refusing to modify non-fixture Settings");
            const auto selection=config/L"model-selection.json";
            std::ifstream input(selection,std::ios::binary); const std::string original((std::istreambuf_iterator<char>(input)),{}); input.close();
            { std::ofstream saved(selection,std::ios::binary|std::ios::trunc); saved<<"{\"SchemaVersion\":1,\"SelectedModelAlias\":null}"; }
            Site unselected(Mode::normal,false); unselected.variable_frames=true;
            speak(engine.p,raw,&speech,unselected,E_FAIL);
            check(!unselected.bytes && WaitForSingleObject(child.value,1000)==WAIT_OBJECT_0,"Missing selection retained or used the previous model");
            { std::ofstream saved(selection,std::ios::binary|std::ios::trunc); saved<<original; }
            Site selected_again(Mode::normal,false); selected_again.variable_frames=true; speak(engine.p,raw,&speech,selected_again);
            check(selected_again.nonzero_pcm,"Explicitly restored selection did not restart cleanly");
            CloseHandle(child.value); child.value=OpenProcess(SYNCHRONIZE,FALSE,rust_child(runtime));
            check(child.value!=nullptr,"Cannot retain newly selected runtime process");
        }
        auto long_speech=fragment(text);
        Site aborted(Mode::normal,false); aborted.variable_frames=true; aborted.timed_abort=true;
        speak(engine.p,raw,&long_speech,aborted);
        check(aborted.abort_observations && !aborted.write_after_abort && aborted.abort_latency_ms<5000 &&
            WaitForSingleObject(child.value,0)==WAIT_OBJECT_0,"SAPI abort did not cancel and reap Rust");
        Site recovered(Mode::normal,false); recovered.variable_frames=true;
        speak(engine.p,raw,&speech,recovered);
        check(recovered.nonzero_pcm && rust_child(runtime)!=pid,"SAPI did not recover with a clean Rust process");
        Site failed(Mode::failed,false); failed.variable_frames=true;
        speak(engine.p,raw,&speech,failed,E_ACCESSDENIED);
        check(!failed.bytes,"Write failure delivered PCM");
        std::cout << "PASS: Rust engine/site, cold-start abort=" << cold_abort.abort_latency_ms << " ms, format rejection, partial writes ("
            << partial.calls << "), bookmarks/rate/volume/skip; warm abort="
            << aborted.abort_latency_ms << " ms, bytes before abort=" << aborted.bytes << "; clean recovery and Write error." << std::endl;
    }
    check(SUCCEEDED(CoInitializeEx(nullptr,COINIT_MULTITHREADED)),"Cannot initialize real SAPI COM");
    struct EndCOM { ~EndCOM() { CoUninitialize(); } } end_com;
    {
        Com<ISpVoice> voice;
        hr(CoCreateInstance(CLSID_SpVoice,nullptr,CLSCTX_INPROC_SERVER,__uuidof(ISpVoice),reinterpret_cast<void**>(voice.put())),"Cannot create real SpVoice");
        Com<RustCountingStream> output; output.p=new RustCountingStream;
        Com<ISpStream> stream;
        hr(CoCreateInstance(CLSID_SpStream,nullptr,CLSCTX_INPROC_SERVER,__uuidof(ISpStream),reinterpret_cast<void**>(stream.put())),"Cannot create real SpStream");
        auto format=outloud::sapi::native_format();
        hr(stream->SetBaseStream(output.p,SPDFID_WaveFormatEx,&format),"Cannot set counting SAPI output");
        hr(voice->SetOutput(stream.p,FALSE),"Cannot select SAPI output");
        hr(voice->SetVoice(token.p),"Cannot select in-memory Rust voice");
        hr(voice->SetVolume(75),"Cannot set real SAPI volume"); hr(voice->SetRate(2),"Cannot set real SAPI rate");
        hr(voice->Speak(L"Hello from the official Rust runtime through Windows SAPI.",SPF_ASYNC|SPF_IS_NOT_XML,nullptr),"Real Rust SAPI Speak failed");
        struct Release { HANDLE event; ~Release() { SetEvent(event); } } release{output->release.value};
        check(WaitForSingleObject(output->first.value,5000)==WAIT_OBJECT_0,"Real SAPI received no initial PCM");
        check(voice->WaitUntilDone(0)==S_FALSE && output->bytes>0,"SAPI audio was not received before completion");
        SetEvent(output->release.value);
        hr(voice->WaitUntilDone(10000),"Real SAPI synthesis did not finish");
        check(output->nonzero && output->writes>1,"Real SAPI output has no nonzero incremental audio");
        const auto complete_bytes=output->bytes.load(); const auto writes=output->writes.load();
        const auto pid=rust_child(runtime); RustCheckHandle child{OpenProcess(SYNCHRONIZE,FALSE,pid)};
        check(child.value!=nullptr,"Cannot retain real SAPI runtime handle");
        hr(voice->Speak(std::wstring(text).c_str(),SPF_ASYNC|SPF_IS_NOT_XML,nullptr),"Cannot begin real SAPI interruption check");
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        const auto stopping=Clock::now();
        hr(voice->Speak(L"",SPF_ASYNC|SPF_PURGEBEFORESPEAK|SPF_IS_NOT_XML,nullptr),"Real SAPI purge failed");
        hr(voice->WaitUntilDone(5000),"Real SAPI interruption did not finish");
        check(WaitForSingleObject(child.value,1000)==WAIT_OBJECT_0,"Real SAPI interruption left Rust running");
        std::cout << "PASS: real SpVoice/SpStream received " << complete_bytes << " PCM bytes in " << writes
            << " writes before completion; purge/owned process exit=" << std::chrono::duration<double,std::milli>(Clock::now()-stopping).count()
            << " ms. No registry changes, audio device, or audio file." << std::endl;
    }
    return 0;
}
} // namespace
