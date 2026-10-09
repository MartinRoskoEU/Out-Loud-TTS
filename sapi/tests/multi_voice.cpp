#include "scratch.hpp"
#include "identity.hpp"
#include "support.hpp"
#include "token.hpp"
#include "outloud/orchestration.hpp"
#include "outloud/pcm16.hpp"
#include <barrier>
#include <condition_variable>
#include <exception>
#include <iostream>
#include <thread>

namespace {
void check(bool value,const char* message) { if (!value) { throw std::runtime_error(message); } }
void hr(HRESULT value,const char* message) { if (value!=S_OK) { std::cerr << "HRESULT=" << std::hex << value << '\n'; } check(value==S_OK,message); }
template<class T> struct Com {
    T* p = nullptr;
    ~Com() { if (p) { p->Release(); } }
    T* operator->() const { return p; }
    T** put() { if (p) { p->Release(); } p = nullptr; return &p; }
};
constexpr std::wstring_view sentence = L"Hello traveler.";
constexpr std::string_view sentence_utf8 = "Hello traveler.";
struct Gate {
    std::mutex mutex;
    std::condition_variable changed;
    bool entered = false, release = false;
};
class Site final : public ISpTTSEngineSite {
public:
    std::vector<std::uint8_t> pcm;
    std::vector<std::wstring> bookmarks;
    std::size_t abort_at = SIZE_MAX;
    DWORD caller = GetCurrentThreadId();
    bool affinity = true, aborted = false, write_after_abort = false;
    Gate* gate = nullptr;
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid,void** out) override {
        if (!out) { return E_POINTER; } *out = nullptr;
        if (iid!=IID_IUnknown && iid!=__uuidof(ISpTTSEngineSite) && iid!=__uuidof(ISpEventSink)) { return E_NOINTERFACE; }
        *out = static_cast<ISpTTSEngineSite*>(this); AddRef(); return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return 2; }
    ULONG STDMETHODCALLTYPE Release() override { return 1; } // caller-owned synchronous fixture
    void thread() { affinity &= caller==GetCurrentThreadId(); }
    HRESULT STDMETHODCALLTYPE AddEvents(const SPEVENT* events,ULONG count) override {
        thread();
        for (ULONG i = 0; i<count; ++i) {
            if (!events || events[i].eEventId!=SPEI_TTS_BOOKMARK || events[i].ullAudioStreamOffset ||
                events[i].elParamType!=SPET_LPARAM_IS_STRING || !events[i].lParam || !pcm.empty()) { return E_UNEXPECTED; }
            bookmarks.emplace_back(reinterpret_cast<const wchar_t*>(events[i].lParam));
        } return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetEventInterest(ULONGLONG* out) override { thread(); if (!out) { return E_POINTER; } *out = SPFEI(SPEI_TTS_BOOKMARK); return S_OK; }
    DWORD STDMETHODCALLTYPE GetActions() override { thread(); aborted |= pcm.size()>=abort_at; return aborted ? SPVES_ABORT : 0; }
    HRESULT STDMETHODCALLTYPE Write(const void* data,ULONG count,ULONG* out) override {
        thread(); if (!out || !data || count%2) { return E_POINTER; }
        if (gate && pcm.empty()) {
            std::unique_lock lock(gate->mutex); gate->entered = true; gate->changed.notify_all();
            gate->changed.wait(lock,[&] { return gate->release; });
        }
        write_after_abort |= aborted; const auto* bytes = static_cast<const std::uint8_t*>(data);
        pcm.insert(pcm.end(),bytes,bytes+count); *out = count; return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetRate(long* out) override { thread(); *out = 0; return S_OK; }
    HRESULT STDMETHODCALLTYPE GetVolume(USHORT* out) override { thread(); *out = 100; return S_OK; }
    HRESULT STDMETHODCALLTYPE GetSkipInfo(SPVSKIPTYPE*,long*) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE CompleteSkip(long) override { return S_OK; }
};
SPVTEXTFRAG fragment(std::wstring_view value,SPVACTIONS action = SPVA_Speak) {
    SPVTEXTFRAG f{}; f.State.eAction = action; f.State.Volume = 100;
    f.pTextStart = value.data(); f.ulTextLen = static_cast<ULONG>(value.size()); return f;
}
std::vector<std::uint8_t> run(ISpTTSEngine* engine,bool bookmark = false,std::size_t abort = SIZE_MAX,Gate* gate = nullptr) {
    Site site; site.abort_at = abort; site.gate = gate; auto spoken = fragment(sentence); auto mark = fragment(L"Start",SPVA_Bookmark); mark.pNext = &spoken;
    const auto wave = outloud::sapi::native_format();
    hr(engine->Speak(0,SPDFID_WaveFormatEx,&wave,bookmark ? &mark : &spoken,&site),"Direct multi-token Speak failed.");
    check(site.affinity && !site.write_after_abort,"Site affinity or cancellation regressed.");
    check(site.bookmarks==(bookmark ? std::vector<std::wstring>{L"Start"} : std::vector<std::wstring>{}),"Bookmark differed.");
    return site.pcm;
}
std::vector<std::uint8_t> reference(const std::filesystem::path& model_path,const std::filesystem::path& tokenizer_path,const std::filesystem::path& voice_path) {
    outloud::PocketModelAsset model(model_path); outloud::OutLoudVoice voice(voice_path); outloud::Tokenizer tokenizer(tokenizer_path);
    outloud::UtterancePipeline pipeline(model,voice,tokenizer,128,outloud::FlowLMGenerator::estimate_max_gen_len(128),outloud::cpu::KernelPath::automatic);
    pipeline.prepare(sentence_utf8,73); std::vector<std::uint8_t> result;
    const auto sink = [](void* context,std::uint64_t,std::size_t,std::uint32_t,std::span<const float> frame) {
        std::array<std::int16_t,1920> pcm{}; outloud::convert_pcm16(frame,pcm);
        auto& output = *static_cast<std::vector<std::uint8_t>*>(context);
        const auto* bytes = reinterpret_cast<const std::uint8_t*>(pcm.data()); output.insert(output.end(),bytes,bytes+sizeof(pcm));
    };
    check(pipeline.run(sink,&result).status==outloud::SynthesisStatus::completed && !result.empty(),"Native voice reference failed."); return result;
}
class RealVoice {
public:
    Com<ISpVoice> voice; Com<IStream> memory; Com<ISpStream> stream;
    std::uint64_t offset = 0;
    RealVoice() {
        hr(CoCreateInstance(CLSID_SpVoice,nullptr,CLSCTX_INPROC_SERVER,__uuidof(ISpVoice),reinterpret_cast<void**>(voice.put())),"Cannot create Windows SAPI voice.");
        hr(CreateStreamOnHGlobal(nullptr,TRUE,memory.put()),"Cannot create memory stream.");
        hr(CoCreateInstance(CLSID_SpStream,nullptr,CLSCTX_INPROC_SERVER,__uuidof(ISpStream),reinterpret_cast<void**>(stream.put())),"Cannot create SAPI stream.");
        const auto wave = outloud::sapi::native_format(); hr(stream->SetBaseStream(memory.p,SPDFID_WaveFormatEx,&wave),"Cannot set SAPI format.");
        hr(voice->SetOutput(stream.p,FALSE),"Cannot direct SAPI output to memory.");
        hr(voice->SetInterest(SPFEI(SPEI_TTS_BOOKMARK),SPFEI(SPEI_TTS_BOOKMARK)),"Cannot set SAPI interest.");
    }
    std::vector<std::uint8_t> run(TestToken* token,bool bookmark) {
        hr(voice->SetVoice(token),"Cannot select in-memory voice token.");
        hr(voice->Speak(bookmark ? L"<bookmark mark=\"Start\"/>Hello traveler." : L"Hello traveler.",SPF_IS_XML,nullptr),"Windows SAPI multi-token Speak failed.");
        STATSTG stat{}; hr(memory->Stat(&stat,STATFLAG_NONAME),"Cannot inspect PCM stream.");
        check(stat.cbSize.QuadPart>offset && stat.cbSize.QuadPart-offset<1024*1024,"Windows SAPI PCM size invalid.");
        std::vector<std::uint8_t> result(static_cast<std::size_t>(stat.cbSize.QuadPart-offset));
        LARGE_INTEGER position{}; position.QuadPart = static_cast<LONGLONG>(offset);
        hr(memory->Seek(position,STREAM_SEEK_SET,nullptr),"Cannot seek PCM stream.");
        ULONG read = 0; hr(memory->Read(result.data(),static_cast<ULONG>(result.size()),&read),"Cannot read PCM stream."); check(read==result.size(),"PCM truncated.");
        unsigned events = 0;
        for (;;) {
            SPEVENT event{}; ULONG count = 0; const auto status = voice->GetEvents(1,&event,&count);
            struct Clear {
                SPEVENT& event;
                ~Clear() {
                    if (event.elParamType==SPET_LPARAM_IS_STRING || event.elParamType==SPET_LPARAM_IS_POINTER) { CoTaskMemFree(reinterpret_cast<void*>(event.lParam)); }
                    else if ((event.elParamType==SPET_LPARAM_IS_OBJECT || event.elParamType==SPET_LPARAM_IS_TOKEN) && event.lParam) { reinterpret_cast<IUnknown*>(event.lParam)->Release(); }
                }
            } clear{event};
            if (status==S_FALSE && !count) { break; }
            hr(status,"Cannot fetch bookmark.");
            check(count==1 && event.eEventId==SPEI_TTS_BOOKMARK && event.elParamType==SPET_LPARAM_IS_STRING && event.lParam &&
                std::wstring_view(reinterpret_cast<wchar_t*>(event.lParam))==L"Start" && event.ullAudioStreamOffset==offset,"Actual Windows bookmark differs."); ++events;
        }
        check(events==(bookmark ? 1U : 0U),"Unexpected Windows bookmark count."); offset = stat.cbSize.QuadPart; return result;
    }
};
}
int wmain(int argc,wchar_t** argv) {
    HMODULE module = nullptr;
    try {
        check(argc==6,"Usage: multi-voice-check DLL MODEL TOKENIZER GNOME.olv UNDEAD.olv");
        hr(CoInitializeEx(nullptr,COINIT_MULTITHREADED),"Cannot initialize COM."); struct EndCOM { ~EndCOM() { CoUninitialize(); } } end_com;
        outloud::test::Scratch scratch; outloud::VoiceLibrary library(scratch.path/L"hlasy-\u017e");
        const auto a = library.import_olv(argv[4],"Gnome test").voice;
        const auto b = library.import_olv(argv[5],"Undead test").voice;
        const auto expected_a = reference(argv[2],argv[3],a.managed_olv_path), expected_b = reference(argv[2],argv[3],b.managed_olv_path);
        check(expected_a!=expected_b,"Distinct voices produced identical references.");
        module = LoadLibraryExW(std::filesystem::absolute(argv[1]).c_str(),nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_SYSTEM32);
        check(module!=nullptr,"Cannot load shared DLL.");
        using GetClass = HRESULT (__stdcall*)(REFCLSID,REFIID,void**); using Unload = HRESULT (__stdcall*)();
        const auto get = reinterpret_cast<GetClass>(GetProcAddress(module,"DllGetClassObject"));
        const auto unload = reinterpret_cast<Unload>(GetProcAddress(module,"DllCanUnloadNow")); check(get && unload,"Missing COM exports.");
        {
            Com<IClassFactory> factory; hr(get(outloud::sapi::engine_clsid,IID_IClassFactory,reinterpret_cast<void**>(factory.put())),"Cannot get shared factory.");
            Com<TestToken> ta,tb; ta.p = new TestToken; tb.p = new TestToken;
            for (auto* token : {ta.p,tb.p}) {
                token->factory = factory.p;
                token->values = {{L"AssetRoot",scratch.path.wstring()},{L"ModelPath",std::filesystem::absolute(argv[2]).wstring()},
                    {L"TokenizerPath",std::filesystem::absolute(argv[3]).wstring()},{L"Language",L"409"},{L"CLSID",outloud::sapi::clsid_text}};
            }
            ta->values[L"VoicePath"] = a.managed_olv_path.wstring(); ta->values[L"TestId"] = L"OutLoud_Multi_A";
            tb->values[L"VoicePath"] = b.managed_olv_path.wstring(); tb->values[L"TestId"] = L"OutLoud_Multi_B";
            Com<ISpTTSEngine> ea,eb;
            hr(ta->CreateInstance(nullptr,CLSCTX_INPROC_SERVER,__uuidof(ISpTTSEngine),reinterpret_cast<void**>(ea.put())),"Cannot create A.");
            check(run(ea.p)==expected_a,"A selected wrong voice.");
            hr(tb->CreateInstance(nullptr,CLSCTX_INPROC_SERVER,__uuidof(ISpTTSEngine),reinterpret_cast<void**>(eb.put())),"Cannot create B.");
            check(run(eb.p,true)==expected_b && run(ea.p,true)==expected_a && run(eb.p)==expected_b,"Alternating A/B changed voice.");
            std::vector<std::uint8_t> parallel_a,parallel_b; std::exception_ptr error_a,error_b; std::barrier start(2);
            std::thread first([&] { start.arrive_and_wait(); try { parallel_a = run(ea.p); } catch (...) { error_a = std::current_exception(); } });
            std::thread second([&] { start.arrive_and_wait(); try { parallel_b = run(eb.p); } catch (...) { error_b = std::current_exception(); } });
            first.join(); second.join(); if (error_a) { std::rethrow_exception(error_a); } if (error_b) { std::rethrow_exception(error_b); }
            check(parallel_a==expected_a && parallel_b==expected_b,"Simultaneous engines leaked configuration.");
            for (auto* engine : {ea.p,eb.p}) {
                check(run(engine,false,3840).size()==3840,"Cancellation did not stop after first frame.");
                check(run(engine)==(engine==ea.p ? expected_a : expected_b),"Cancellation recovery changed voice.");
            }
            Com<ISpObjectWithToken> with;
            hr(ea->QueryInterface(__uuidof(ISpObjectWithToken),reinterpret_cast<void**>(with.put())),"Cannot query token interface.");
            Gate gate; HRESULT reassignment = E_UNEXPECTED; std::exception_ptr active_error;
            std::vector<std::uint8_t> active_pcm;
            std::thread active([&] { try { active_pcm = run(ea.p,false,SIZE_MAX,&gate); } catch (...) { active_error = std::current_exception(); } });
            { std::unique_lock lock(gate.mutex); gate.changed.wait(lock,[&] { return gate.entered; }); }
            std::mutex assignment_mutex; std::condition_variable assignment_changed; bool attempted = false, assigned = false;
            std::thread assignment([&] {
                { std::lock_guard lock(assignment_mutex); attempted = true; assignment_changed.notify_all(); }
                reassignment = with->SetObjectToken(tb.p);
                { std::lock_guard lock(assignment_mutex); assigned = true; assignment_changed.notify_all(); }
            });
            bool returned_while_active = false;
            {
                std::unique_lock lock(assignment_mutex); assignment_changed.wait(lock,[&] { return attempted; });
                returned_while_active = assignment_changed.wait_for(lock,std::chrono::milliseconds(20),[&] { return assigned; });
            }
            { std::lock_guard lock(gate.mutex); gate.release = true; gate.changed.notify_all(); }
            active.join(); assignment.join(); if (active_error) { std::rethrow_exception(active_error); }
            check(!returned_while_active && active_pcm==expected_a,"Active token reassignment changed running request.");
            hr(reassignment,"Active token reassignment failed after quiescence."); check(run(ea.p)==expected_b,"Waiting reassignment selected wrong voice.");
            hr(with->SetObjectToken(tb.p),"Quiescent token reassignment failed."); check(run(ea.p)==expected_b,"Reassignment retained old voice.");
            Com<TestToken> invalid; invalid.p = new TestToken; invalid->values = ta->values; invalid->values.erase(L"VoicePath");
            check(FAILED(with->SetObjectToken(invalid.p)) && run(ea.p)==expected_b,"Rejected token damaged configuration.");
            for (const auto& path : {L"",L"C:\\missing.olv",L"C:\\wrong.txt",L"\\\\.\\NUL",L"C:\\bad.olv:stream"}) {
                invalid->values[L"VoicePath"] = path; check(FAILED(with->SetObjectToken(invalid.p)),"Malformed voice path accepted.");
            }
            auto renamed = new TestToken; Com<TestToken> renamed_owner; renamed_owner.p = renamed; renamed->values = tb->values;
            renamed->values[L""] = L"New display label"; hr(with->SetObjectToken(renamed),"Display-only assignment failed.");
            check(run(ea.p)==expected_b,"Display rename changed PCM.");
            hr(with->SetObjectToken(ta.p),"Return to A failed."); check(run(ea.p)==expected_a,"Return to A leaked B.");
            // Actual Windows parser/site/event queues, with two independent
            // SpVoice objects sharing this one factory and in-memory tokens.
            RealVoice real_a,real_b;
            check(real_a.run(ta.p,false)==expected_a && real_b.run(tb.p,false)==expected_b,"Actual SAPI selected wrong voice.");
            check(real_a.run(tb.p,true)==expected_b && real_b.run(ta.p,true)==expected_a && real_a.run(ta.p,true)==expected_a,
                "Actual SAPI alternate tokens/bookmarks changed PCM.");
        }
        check(!TestToken::alive,"Token leaked."); hr(unload(),"Shared DLL retained engines/workers.");
        FreeLibrary(module); module = nullptr;
        std::cout << "PASS: shared DLL/CLSID, distinct Gnome/Undead Unicode managed paths, native-exact PCM, alternating/concurrent engines, reassignment/failure, cancellation recovery, real Windows SAPI tokens and bookmarks.\n";
        return 0;
    } catch (const std::exception& e) { if (module) { FreeLibrary(module); } std::cerr << e.what() << '\n'; return 1; }
}
