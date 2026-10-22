// Include the adapter so these checks exercise its actual RequestControl and
// write_frame, without adding a production test hook or running neural workers.
#include "../src/adapter.cpp"
#include <cmath>
#include <cstring>
#include <iostream>
#include <limits>

namespace {
using namespace outloud::sapi;
unsigned checks=0;
void check(bool condition,const char* message) {
    ++checks; if (!condition) { throw std::runtime_error(message); }
}
struct Change { std::size_t sample; USHORT volume; HRESULT result=S_OK; };
class Site final : public ISpTTSEngineSite {
public:
    USHORT volume=100;
    HRESULT volume_result=S_OK, write_result=S_OK;
    ULONG forced_write=ULONG_MAX;
    std::size_t maximum=SIZE_MAX, abort_at=SIZE_MAX, next=0;
    unsigned queries=0, writes=0;
    DWORD caller=GetCurrentThreadId();
    std::vector<Change> changes;
    std::vector<std::int16_t> pcm;
    void affinity() { check(caller==GetCurrentThreadId(),"Site called off the Speak caller thread"); }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID,void** out) override {
        if (!out) { return E_POINTER; } *out=static_cast<ISpTTSEngineSite*>(this); return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return 1; }
    ULONG STDMETHODCALLTYPE Release() override { return 1; }
    HRESULT STDMETHODCALLTYPE AddEvents(const SPEVENT*,ULONG) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE GetEventInterest(ULONGLONG* out) override { *out=0; return S_OK; }
    DWORD STDMETHODCALLTYPE GetActions() override {
        affinity(); DWORD actions=0;
        if (next<changes.size() && pcm.size()>=changes[next].sample) {
            const auto& change=changes[next++]; volume=change.volume; volume_result=change.result;
            actions|=SPVES_VOLUME;
        }
        if (pcm.size()>=abort_at) { actions|=SPVES_ABORT; }
        return actions;
    }
    HRESULT STDMETHODCALLTYPE GetRate(long* out) override { affinity(); *out=0; return S_OK; }
    HRESULT STDMETHODCALLTYPE GetVolume(USHORT* out) override { affinity(); ++queries; *out=volume; return volume_result; }
    HRESULT STDMETHODCALLTYPE GetSkipInfo(SPVSKIPTYPE*,long*) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE CompleteSkip(long) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE Write(const void* data,ULONG bytes,ULONG* written) override {
        affinity(); ++writes;
        if (write_result!=S_OK) { return write_result; }
        if (forced_write!=ULONG_MAX) { *written=forced_write; return S_OK; }
        const auto count=(std::min)(std::size_t{bytes/2},maximum);
        const auto old=pcm.size(); pcm.resize(old+count);
        std::memcpy(pcm.data()+old,data,count*2); *written=static_cast<ULONG>(count*2); return S_OK;
    }
};
std::vector<std::int16_t> signal(std::size_t count) {
    std::vector<std::int16_t> result(count);
    for (std::size_t i=0;i<count;++i) { result[i]=static_cast<std::int16_t>(static_cast<int>((i*7919)%65536)-32768); }
    return result;
}
std::int16_t reference(std::int16_t sample,unsigned baseline,unsigned fragment=100) {
    const auto value=std::nearbyint(double(sample)*(std::min)(baseline,100U)*(std::min)(fragment,100U)/10000.0);
    return static_cast<std::int16_t>(std::clamp(value,-32768.0,32767.0));
}
SPVTEXTFRAG fragment(std::wstring_view text,ULONG volume=100) {
    SPVTEXTFRAG f{}; f.State.eAction=SPVA_Speak; f.State.Volume=volume;
    f.pTextStart=text.data(); f.ulTextLen=static_cast<ULONG>(text.size()); return f;
}
void arithmetic() {
    for (const unsigned volume : {0U,50U,100U}) {
        for (int value=-32768;value<=32767;++value) {
            const auto sample=static_cast<std::int16_t>(value);
            check(scale_pcm16(sample,volume_gain(volume,100))==reference(sample,volume),"PCM16 exhaustive scaling differs");
        }
    }
    for (unsigned v=0;v<=100;++v) {
        for (const int value : {-32768,-32767,-101,-3,-1,0,1,3,101,32766,32767}) {
            check(scale_pcm16(static_cast<std::int16_t>(value),volume_gain(v,37))==reference(static_cast<std::int16_t>(value),v,37),"Combined gain/boundary rounding differs");
        }
    }
    check(volume_gain(UINT_MAX,UINT_MAX)==10000 && scale_pcm16(-32768,UINT_MAX)==-32768,"Out-of-range saturation differs");
}
void baseline_and_live() {
    const auto source=signal(1920), original=source;
    for (const USHORT volume : {USHORT{0},USHORT{50},USHORT{100}}) {
        for (const std::size_t partial : {std::size_t{1},std::size_t{31},SIZE_MAX}) {
            Site site; site.volume=volume; site.maximum=partial;
            RequestControl control{&site};
            check(write_frame(control,source)==S_OK,"Baseline write failed");
            check(site.queries==1 && control.volume==volume && site.pcm.size()==source.size(),"Initial volume without pending event not read/sample count changed");
            for (std::size_t i=0;i<source.size();++i) { check(site.pcm[i]==reference(source[i],volume),"Baseline/partial scaling differs"); }
            check(source==original,"Source PCM was modified");
        }
    }
    Site site; site.maximum=1;
    site.changes={{17,0},{43,50},{100,100},{151,7,E_FAIL},{201,150}};
    RequestControl control{&site};
    check(write_frame(control,source)==S_OK && site.queries==6,"Live volume queries differ");
    for (std::size_t i=0;i<source.size();++i) {
        const unsigned v=i<17 ? 100 : i<43 ? 0 : i<100 ? 50 : 100;
        check(site.pcm[i]==reference(source[i],v),"Live change/failed query/partial retry differs");
    }
    Site failed; failed.volume=0; failed.volume_result=E_FAIL;
    RequestControl fallback{&failed};
    check(write_frame(fallback,source)==S_OK && failed.pcm==source,"Failed initial query did not retain default 100");
    Site retained; retained.volume=50; retained.maximum=1; retained.changes={{20,100,E_FAIL}};
    RequestControl last{&retained}; check(write_frame(last,source)==S_OK,"Failed update stopped output");
    for (std::size_t i=0;i<source.size();++i) { check(retained.pcm[i]==reference(source[i],50),"Failed update lost last valid volume"); }
    Site second; RequestControl fresh{&second};
    check(write_frame(fresh,source)==S_OK && second.pcm==source,"Volume leaked into a subsequent request");
}
void fragments() {
    auto a=fragment(L"Hello "), b=fragment(L"traveler"), c=fragment(L"."), empty=fragment(L"",0);
    a.pNext=&b; b.pNext=&empty; empty.pNext=&c;
    auto request=collect_request(&a);
    check(request.text=="Hello traveler." && request.volume_runs.size()==1 && request.volume_runs[0].volume==100,"Equal/default volume text fragmented");
    b.State.Volume=50; c.State.Volume=0;
    request=collect_request(&a);
    check(request.text=="Hello traveler." && request.volume_runs.size()==3,"Mixed volume boundaries lost");
    Site site; site.volume=50; RequestControl control{&site};
    std::string rendered;
    check(render_volume_runs(request,control,[&](std::string_view text) {
        rendered+=text; const std::array<std::int16_t,2> pcm={20000,-20000}; return write_frame(control,pcm);
    })==S_OK,"Volume run rendering failed");
    check(rendered==request.text && site.pcm==std::vector<std::int16_t>({10000,-10000,5000,-5000,0,0}),"Baseline/fragment combination or boundaries differ");
    // Preserve existing support for surrogate pairs split across text fragments,
    // including a volume transition inside the pair and metadata between halves.
    auto high=fragment(std::wstring_view(L"\xd83d",1),25), low=fragment(std::wstring_view(L"\xde00",1),50), tail=fragment(L"x",100);
    high.pNext=&low; low.pNext=&tail;
    request=collect_request(&high);
    check(request.text=="\xf0\x9f\x98\x80x" && request.volume_runs.size()==2 && request.volume_runs[0].length==4 && request.volume_runs[1].offset==4,"Split Unicode character damaged at volume boundary");
    tail.State.Volume=1000; request=collect_request(&high);
    check(request.volume_runs.back().volume==100,"Fragment volume not clamped");
    auto word=fragment(L"hello"), space=fragment(L"\t\u2003 \n",0), next=fragment(L"world");
    word.pNext=&space; space.pNext=&next; request=collect_request(&word);
    check(request.text=="hello\t\xe2\x80\x83 \nworld" && request.volume_runs.size()==1 && request.volume_runs[0].length==request.text.size(),"Whitespace-only volume changes split or damaged text");
    request=collect_request(&space);
    check(request.volume_runs.size()==1 && request.volume_runs[0].offset==0 && request.volume_runs[0].volume==100 && request.volume_runs[0].length==request.text.size(),"Leading whitespace lost or assigned to a separate prompt");
    unsigned polls=0;
    check(collect_request(&a,{[](void* p) { return ++*static_cast<unsigned*>(p)==3; },&polls}).cancelled,"Collection cancellation differs");
    Site stopped; stopped.abort_at=2; RequestControl abort{&stopped};
    request=collect_request(&a); unsigned runs=0;
    check(render_volume_runs(request,abort,[&](std::string_view) { ++runs; const std::array<std::int16_t,2> pcm={1,2}; return write_frame(abort,pcm); })==S_OK && runs==1,"Abort dispatched a later volume run");
    Site errors; RequestControl error{&errors}; runs=0;
    check(render_volume_runs(request,error,[&](std::string_view) { ++runs; return SP_AUDIO_STOPPED; })==SP_AUDIO_STOPPED && runs==1,"Audio stop dispatched a later run");
}
std::vector<std::int16_t> rate_output(std::span<const std::int16_t> source,long rate,Site& site,ULONG fragment_volume=100) {
    RequestControl control{&site}; control.fragment_volume=fragment_volume;
    SpeechRate processor;
    const auto output=[&](std::span<const std::int16_t> pcm) { return write_frame(control,pcm); };
    for (std::size_t offset=0;offset<source.size();offset+=1920) {
        check(processor.write(source.subspan(offset,1920),rate,output)==S_OK,"Sonic output write failed");
    }
    const auto before=site.pcm.size();
    check(processor.finish(output)==S_OK,"Sonic flush failed");
    if (rate) { check(site.pcm.size()>before,"Synthetic fixture did not exercise flushed tail"); }
    const auto size=site.pcm.size(); check(processor.finish(output)==S_OK && site.pcm.size()==size,"Tail written twice");
    return site.pcm;
}
void sonic_and_errors() {
    const auto source=signal(1920*8);
    for (const long rate : {-5L,0L,5L}) {
        Site unity; const auto unscaled=rate_output(source,rate,unity);
        if (!rate) { check(unscaled==source,"Full-volume direct PCM changed"); }
        // Independent Sonic delivery without the adapter at unity gain.
        SpeechRate reference_rate; std::vector<std::int16_t> expected;
        const auto capture=[&](std::span<const std::int16_t> pcm) { expected.insert(expected.end(),pcm.begin(),pcm.end()); return S_OK; };
        for (std::size_t offset=0;offset<source.size();offset+=1920) {
            check(reference_rate.write(std::span<const std::int16_t>(source).subspan(offset,1920),rate,capture)==S_OK,"Reference Sonic failed");
        }
        check(reference_rate.finish(capture)==S_OK && unscaled==expected,"Full-volume Sonic PCM changed");
        for (const USHORT v : {USHORT{0},USHORT{50},USHORT{100}}) {
            Site scaled; scaled.volume=v; scaled.maximum=7;
            const auto actual=rate_output(source,rate,scaled,50);
            check(actual.size()==unscaled.size(),"Gain changed Sonic duration/sample count");
            for (std::size_t i=0;i<actual.size();++i) { check(actual[i]==reference(unscaled[i],v,50),"Sonic/tail/partial gain differs"); }
        }
        Site live; live.maximum=1; live.changes={{17,0},{1300,50},{3000,100}};
        const auto actual=rate_output(source,rate,live);
        check(actual.size()==unscaled.size(),"Live volume changed Sonic sample count");
        for (std::size_t i=0;i<actual.size();++i) {
            const unsigned v=i<17 ? 100 : i<1300 ? 0 : i<3000 ? 50 : 100;
            check(actual[i]==reference(unscaled[i],v),"Live Sonic gain differs");
        }
        Site tail; tail.maximum=1; tail.changes={{unscaled.size()-10,0}};
        const auto tail_changed=rate_output(source,rate,tail);
        for (std::size_t i=0;i<unscaled.size();++i) {
            check(tail_changed[i]==(i<unscaled.size()-10 ? unscaled[i] : 0),"Volume update during output tail ignored");
        }
    }
    auto first=fragment(L"first",50), second=fragment(L"second",0), third=fragment(L"third",100);
    first.pNext=&second; second.pNext=&third; const auto request=collect_request(&first);
    Site mixed; mixed.maximum=7; mixed.volume=50; RequestControl mixed_control{&mixed};
    check(render_volume_runs(request,mixed_control,[&](std::string_view) {
        SpeechRate processor;
        const auto output=[&](std::span<const std::int16_t> pcm) { return write_frame(mixed_control,pcm); };
        for (std::size_t offset=0;offset<source.size();offset+=1920) {
            const auto hr=processor.write(std::span<const std::int16_t>(source).subspan(offset,1920),5,output);
            if (hr!=S_OK) { return hr; }
        }
        return processor.finish(output);
    })==S_OK,"Mixed fragment Sonic run failed");
    Site raw; const auto unity=rate_output(source,5,raw);
    check(mixed.pcm.size()==3*unity.size(),"Mixed fragment duration/sample count differs");
    for (std::size_t run=0;run<3;++run) {
        const unsigned fragment_volume=run==0 ? 50 : run==1 ? 0 : 100;
        for (std::size_t i=0;i<unity.size();++i) {
            check(mixed.pcm[run*unity.size()+i]==reference(unity[i],50,fragment_volume),"Sonic tail crossed fragment volume boundary");
        }
    }
    const std::array<std::int16_t,4> pcm={-32768,-1,1,32767};
    for (const ULONG invalid : {0UL,1UL,10UL}) {
        Site site; site.forced_write=invalid; RequestControl control{&site};
        check(write_frame(control,pcm)==E_UNEXPECTED,"Invalid Write count accepted");
    }
    for (const HRESULT failure : {E_FAIL,SP_AUDIO_STOPPED}) {
        Site site; site.write_result=failure; RequestControl control{&site};
        check(write_frame(control,pcm)==failure,"Write error/status changed");
    }
    Site site; site.maximum=1; site.abort_at=2; RequestControl cancelled{&site};
    check(write_frame(cancelled,pcm)==S_OK && cancelled.aborted && site.pcm.size()==2,"Partial-write cancellation differs");
}
}
int main() {
    try {
        arithmetic(); baseline_and_live(); fragments(); sonic_and_errors();
        std::cout << "PASS: " << checks << " focused volume checks; synthetic PCM, actual adapter controls/writes, fragment/Unicode boundaries, Sonic/tails, live/partial changes, failures and cancellation. No inference or installed resources.\n";
        return 0;
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
