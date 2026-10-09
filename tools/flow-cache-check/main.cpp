#include "flow_sampler_fixture_checks.hpp"
#include "outloud/worker_pipeline.hpp"
#include "outloud/production_noise.hpp"
#include "outloud/pcm16.hpp"
#include <cstring>
#include <iostream>
#ifdef OUTLOUD_A09_ALLOCATION_AUDIT
#include <atomic>
#include <cstdlib>
#include <new>
namespace { thread_local bool audit=false; thread_local std::size_t allocations=0; thread_local std::int64_t fail_after=-1;
    std::atomic<std::size_t> live{0}; }
void* operator new(std::size_t size) {
    if (fail_after==0) { throw std::bad_alloc(); }
    if (fail_after>0) { --fail_after; }
    if (void* p=std::malloc(size ? size : 1)) { ++live; if (audit) { ++allocations; } return p; }
    throw std::bad_alloc();
}
void* operator new[](std::size_t size) { return ::operator new(size); }
void operator delete(void* p) noexcept { if (p) { --live; } std::free(p); }
void operator delete[](void* p) noexcept { ::operator delete(p); }
void operator delete(void* p,std::size_t) noexcept { ::operator delete(p); }
void operator delete[](void* p,std::size_t) noexcept { ::operator delete(p); }
#endif
namespace {
using namespace outloud;
using test::neural_require;
using Clock=std::chrono::steady_clock;
double ms(Clock::time_point a,Clock::time_point b) { return std::chrono::duration<double,std::milli>(b-a).count(); }
void prefill(FlowLMBackbone& owner,std::span<const std::uint32_t> tokens,FlowLMPrefillState& state,
        FlowLMResult& result,const FlowLMObserver& observer={}) { owner.prefill_into(tokens,state,result,observer); }
struct Record {
    std::vector<unsigned char> bytes;
    void add(std::string_view name,detail::Bytes data) {
        const std::uint64_t name_size=name.size(), size=data.size();
        const auto append=[&](const void* p,std::size_t n) {
            if (n) { const auto* b=static_cast<const unsigned char*>(p); bytes.insert(bytes.end(),b,b+n); }
        };
        append(&name_size,sizeof(name_size)); append(name.data(),name.size()); append(&size,sizeof(size)); append(data.data(),data.size());
    }
    template<class T> void values(std::string_view name,std::span<const T> data) {
        add(name,{reinterpret_cast<const unsigned char*>(data.data()),data.size_bytes()});
    }
    template<class T> void scalar(std::string_view name,const T& value) { values(name,std::span<const T>(&value,1)); }
    void floats(std::string_view name,std::span<const float> data) { values(name,data); }
};
void save(const std::filesystem::path& path,detail::Bytes bytes) {
    std::ofstream file(path,std::ios::binary); file.write(reinterpret_cast<const char*>(bytes.data()),static_cast<std::streamsize>(bytes.size()));
    neural_require(bool(file),"Cannot write A09 artifact");
}
void flow_state(Record& record,const FlowLMPrefillState& state) {
    record.scalar("offset",state.offset());
    for (unsigned i=0;i<6;++i) { const auto layer=state.layer(i);
        record.floats("keys"+std::to_string(i),layer.keys); record.floats("values"+std::to_string(i),layer.values); }
}
void generation_call(Record& record,const GenerationCall& call) {
    record.scalar("kind",call.input_kind); record.scalar("index",call.index);
    record.scalar("before",call.offset_before); record.scalar("after",call.offset_after);
    record.floats("input",call.input_latent); record.floats("hidden",call.hidden); record.floats("noise",call.noise);
    record.floats("velocity",call.sample.velocity); record.floats("latent",call.sample.latent);
    record.scalar("eos_logit",call.eos_logit); record.scalar("eos",call.eos); record.scalar("emitted",call.emitted);
    record.scalar("latched_eos",call.latched_eos.value_or(UINT32_MAX));
}
void capture(const PocketModelAsset& model,const OutLoudVoice& voice,const test::SamplerFixture& fixture,
        cpu::KernelPath path,Record& record) {
    FlowLMBackbone backbone(model,voice,path); auto state=backbone.make_state(32); FlowLMResult result;
    // Vary lengths in both directions on the SAME owner; copy borrowed views during callbacks only.
    for (const auto count:{1U,20U,7U,20U}) {
        state.reset();
        prefill(backbone,std::span(test::neural_tokens).first(count),state,result,
            [&](std::string_view name,std::span<const float> values,auto shape) {
                record.floats(name,values); record.values("shape",shape);
            });
        record.floats("prefill.normalized",result.normalized); record.scalar("prefill.eos",result.eos_logit); flow_state(record,state);
    }
    FlowSampler sampler(model,path);
    for (const auto* label:{"prefill","bos","prefill"}) {
        const std::string prefix=std::string(label)+'.';
        const auto sample=sampler.sample(fixture.values(prefix+"input.hidden"),fixture.values(prefix+"input.noise"),
            [&](std::string_view name,std::span<const float> values) { record.floats(name,values); });
        const auto plain=sampler.sample(fixture.values(prefix+"input.hidden"),fixture.values(prefix+"input.noise"));
        neural_require(test::same_float_bits(sample.latent,plain.latent) && test::same_float_bits(sample.velocity,plain.velocity),"Observed/plain sample differs");
        record.floats("plain.latent",plain.latent); record.floats("plain.velocity",plain.velocity);
    }
    ProductionNoiseSource noise(73); FlowLMGenerator generator(backbone,sampler); GenerationOptions options; options.max_gen_len=3;
    const auto generated=generator.generate_segment(test::neural_tokens,noise,options,[&](const GenerationCall& call) { generation_call(record,call); });
    neural_require(generated.emitted.size()==3 && generated.noise_draw_count==4,"Short generator schedule differs");
    flow_state(record,generator.state()); record.scalar("rng.draws",noise.consumed());
    const auto engine=noise.engine_state(); record.scalar("rng.seed",engine.seed); record.scalar("rng.left",engine.left);
    record.scalar("rng.position",engine.position); record.values("rng.words",engine.words);
    WorkerPipeline workers(model,voice,20,3,path); std::vector<float> pcm(5760);
    const GenerationObserver observer=[&](const GenerationCall& call) { generation_call(record,call); };
    workers.prepare(test::neural_tokens,73,pcm,options,nullptr,nullptr,&observer); const auto worked=workers.run();
    neural_require(worked.status==SynthesisStatus::completed && worked.samples==5760 && worked.draws==4,"Short worker schedule differs");
    record.floats("worker.pcm",pcm); flow_state(record,workers.flow_state());
    record.scalar("worker.mimi_offset",workers.mimi_state().offset());
    for (unsigned i=0;i<12;++i) { record.floats("worker.carry"+std::to_string(i),workers.mimi_state().convolution_state(i)); }
    std::vector<std::int16_t> pcm16(pcm.size()); std::transform(pcm.begin(),pcm.end(),pcm16.begin(),float_to_pcm16);
    record.values("worker.pcm16",std::span<const std::int16_t>(pcm16));
}
void stats(std::ostream& file,std::array<double,3> values) {
    file << "{\"runs\":[" << values[0] << ',' << values[1] << ',' << values[2] << ']';
    std::sort(values.begin(),values.end()); file << ",\"min\":" << values[0] << ",\"median\":" << values[1] << ",\"max\":" << values[2] << '}';
}
void lifecycle(const PocketModelAsset& model,const OutLoudVoice& voice,cpu::KernelPath path) {
    WorkerPipeline workers(model,voice,20,3,path); GenerationOptions options; options.max_gen_len=3;
    std::vector<float> pcm(5760),expected;
    const auto normal=[&](std::size_t count=20) {
        workers.prepare(std::span(test::neural_tokens).first(count),73,pcm,options);
        const auto result=workers.run(); neural_require(result.samples==5760 && result.draws==4 && result.status==SynthesisStatus::completed,"Resident schedule changed");
    };
    normal(); expected=pcm; normal(1); normal(7); normal();
    neural_require(test::same_float_bits(pcm,expected),"Varying text left stale prefill/condition data");
    std::stop_source early; early.request_stop(); workers.prepare(test::neural_tokens,73,pcm,options);
    const auto stopped=workers.run(early.get_token()); neural_require(stopped.status==SynthesisStatus::cancelled && stopped.samples==0 && stopped.draws==0,"Pre-stop did work");
    std::stop_source after_prefill;
    const GenerationObserver abort=[&](const GenerationCall& call) { if (call.input_kind==GenerationInput::text) { after_prefill.request_stop(); } };
    workers.prepare(test::neural_tokens,73,pcm,options,nullptr,nullptr,&abort);
    const auto prefilling=workers.run(after_prefill.get_token());
    neural_require(prefilling.status==SynthesisStatus::cancelled && prefilling.draws==1 && prefilling.samples==0,"Prefill stop changed discarded-draw scheduling");
    struct Sink {
        std::stop_source source; unsigned frames=0;
        static void frame(void* context,std::uint32_t index,std::span<const float> values) {
            auto& self=*static_cast<Sink*>(context); neural_require(index==self.frames && values.size()==1920,"Cancelled stream order differs");
            ++self.frames; self.source.request_stop();
        }
    } sink;
    workers.prepare(test::neural_tokens,73,pcm,options,Sink::frame,&sink);
    const auto interrupted=workers.run(sink.source.get_token());
    neural_require(interrupted.status==SynthesisStatus::cancelled && interrupted.samples==1920 && sink.frames==1,"Streaming stop failed");
    normal(); neural_require(test::same_float_bits(pcm,expected),"Cancellation changed recovery PCM");
    struct Failure final : NoiseSource { std::span<const float> next() override { throw std::runtime_error("Injected producer failure"); } } noise;
    workers.prepare(test::neural_tokens,noise,pcm,options); bool failed=false;
    try { (void)workers.run(); } catch (const std::runtime_error&) { failed=true; }
    neural_require(failed,"Producer error lost"); normal(); neural_require(test::same_float_bits(pcm,expected),"Failure changed recovery PCM");
    std::cout << cpu::path_name(path) << ": resident varying lengths / entry,prefill,stream stop / failed-request recovery exact PASS\n";
}
#ifndef OUTLOUD_A09_ALLOCATION_AUDIT
void benchmark(const PocketModelAsset& model,const OutLoudVoice& voice,const test::SamplerFixture& fixture,
        cpu::KernelPath path,const std::filesystem::path& output) {
    std::array<double,3> init{},prefill_ms{},sample_ms{},first_latent{},first_pcm{},total{};
    { FlowSampler warm(model,path); }
    for (unsigned i=0;i<3;++i) { const auto begin=Clock::now(); FlowSampler sampler(model,path); init[i]=ms(begin,Clock::now()); }
    FlowLMBackbone backbone(model,voice,path); FlowSampler sampler(model,path); auto state=backbone.make_state(23);
    FlowLMResult result; prefill(backbone,test::neural_tokens,state,result);
    for (unsigned i=0;i<3;++i) { state.reset(); const auto begin=Clock::now(); prefill(backbone,test::neural_tokens,state,result);
        const auto end=Clock::now(); prefill_ms[i]=ms(begin,end); neural_require(state.offset()==voice.prefix_length()+20,"Timed prefill offset differs"); }
    const auto hidden=fixture.values("bos.input.hidden"), supplied=fixture.values("bos.input.noise");
    (void)sampler.sample(hidden,supplied);
    for (unsigned i=0;i<3;++i) { const auto begin=Clock::now(); const auto sampled=sampler.sample(hidden,supplied);
        const auto end=Clock::now(); sample_ms[i]=ms(begin,end); neural_require(std::isfinite(sampled.latent[0]),"Timed sampler invalid"); }
    // Actual discarded prefill draw + BOS backbone/sample, no observer inside clocks.
    ProductionNoiseSource noise(73); DeterministicCandidate candidate;
    const auto first=[&] { prefill(backbone,test::neural_tokens,state,result); (void)sampler.sample(result.last_hidden(),noise.next());
        sampler.bos_candidate_into(backbone,state,noise.next(),candidate); };
    state.reset(); first();
    for (unsigned i=0;i<3;++i) { state.reset(); noise.reseed(73); const auto begin=Clock::now(); first();
        const auto end=Clock::now(); first_latent[i]=ms(begin,end); neural_require(noise.consumed()==2 && state.offset()==voice.prefix_length()+21,"First-latent schedule differs"); }
    WorkerPipeline workers(model,voice,20,3,path); GenerationOptions options; options.max_gen_len=3; std::vector<float> pcm(5760);
    const auto run=[&] { workers.prepare(test::neural_tokens,73,pcm,options); return workers.run(); };
    (void)run();
    for (unsigned i=0;i<3;++i) { const auto worked=run(); first_pcm[i]=worked.first_pcm_ms; total[i]=worked.total_ms;
        neural_require(worked.status==SynthesisStatus::completed && worked.samples==5760 && worked.draws==4,"Timed worker schedule differs"); }
    std::ofstream file(output); file << std::setprecision(17) << "{\"warmups\":1,\"repetitions\":3,\"frames\":3,\"samples\":5760,\"hooks\":false,\"sampler_inline_bytes\":"
        << sizeof(FlowSampler) << ",\"sampler_activation_bytes\":" << FlowSampler::activation_scratch_bytes()
        << ",\"backbone_workspace_bytes\":" << backbone.workspace_storage_bytes() << ",\"result_capacity_bytes\":" << result.normalized.capacity()*sizeof(float)
        << ",\"sampler_init_ms\":"; stats(file,init); file << ",\"prefill_ms\":"; stats(file,prefill_ms);
    file << ",\"sample_ms\":"; stats(file,sample_ms); file << ",\"first_latent_ms\":"; stats(file,first_latent);
    file << ",\"first_pcm_ms\":"; stats(file,first_pcm); file << ",\"worker_total_ms\":"; stats(file,total); file << "}\n";
    neural_require(bool(file),"Cannot write A09 benchmark"); std::cout << cpu::path_name(path) << ": short benchmark complete\n";
}
#else
void allocation_check(const PocketModelAsset& model,const OutLoudVoice& voice,cpu::KernelPath path,const std::filesystem::path& output) {
    FlowLMBackbone backbone(model,voice,path); auto state=backbone.make_state(23); FlowLMResult result;
    prefill(backbone,test::neural_tokens,state,result); state.reset(); allocations=0; audit=true;
    try { prefill(backbone,test::neural_tokens,state,result); } catch (...) { audit=false; throw; }
    audit=false; const auto count=allocations;
    neural_require(count==0,"Optimized warm prefill allocated");
    const auto construct=[&] {
        FlowLMBackbone cold(model,voice,path); FlowSampler sampler(model,path); auto fresh=cold.make_state(23); FlowLMResult normalized;
        prefill(cold,test::neural_tokens,fresh,normalized);
    };
    allocations=0; audit=true; construct(); audit=false; const auto initialization=allocations;
    for (std::size_t at=0;at<initialization;++at) {
        const auto before=live.load(); fail_after=static_cast<std::int64_t>(at); bool failed=false;
        try { construct(); } catch (const std::bad_alloc&) { failed=true; }
        fail_after=-1; neural_require(failed && live.load()==before,"Partial initialization leaked prefill/cache resources");
    }
    std::ofstream file(output); file << "{\"warm_prefill_allocations\":" << count << ",\"initialization_failure_cases\":" << initialization << "}\n";
    neural_require(bool(file),"Cannot write A09 allocation report");
    std::cout << cpu::path_name(path) << ": warm prefill C++ allocations " << count << "; " << initialization << " allocation failures cleanly released\n";
}
#endif
}
int wmain(int argc,wchar_t** argv) {
    try {
        neural_require(argc==5 || argc==6,"Usage: flow-cache-check capture|compare|bench|alloc|lifecycle ASSETS NEURAL OUTPUT [BASELINE]");
        const std::wstring mode=argv[1]; neural_require(mode==L"capture" || mode==L"compare" || mode==L"bench" || mode==L"alloc" || mode==L"lifecycle","Unknown A09 mode");
        neural_require((mode==L"compare")== (argc==6),"Compare requires baseline directory");
        const std::filesystem::path assets=argv[2],output=argv[4]; std::filesystem::create_directories(output);
        const PocketModelAsset model(assets/"pocket-english.olp"); const test::SamplerFixture fixture(argv[3]);
        for (const auto path:{cpu::KernelPath::portable,cpu::KernelPath::avx2_fma}) {
            if (path==cpu::KernelPath::avx2_fma && !cpu::optimized_available()) { continue; }
            const std::string name=path==cpu::KernelPath::portable ? "portable" : "avx2";
            if (mode==L"bench" || mode==L"alloc") {
                const OutLoudVoice voice(assets/"gnome_male.olv");
#ifndef OUTLOUD_A09_ALLOCATION_AUDIT
                neural_require(mode==L"bench","Allocation audit requires separate executable"); benchmark(model,voice,fixture,path,output/(name+"-benchmark.json"));
#else
                neural_require(mode==L"alloc","Allocation hooks forbidden in benchmark"); allocation_check(model,voice,path,output/(name+"-allocations.json"));
#endif
            } else {
                Record record;
                for (const auto* filename:{L"gnome_male.olv",L"undead_male.olv"}) {
                    const OutLoudVoice voice(assets/filename);
                    if (mode==L"lifecycle") { lifecycle(model,voice,path); }
                    else { capture(model,voice,fixture,path,record); }
                }
                if (mode==L"lifecycle") { continue; }
                if (argc==6) { const detail::MappedFile baseline(std::filesystem::path(argv[5])/(name+".bin"),64*1024*1024);
                    neural_require(baseline.bytes().size()==record.bytes.size() && std::memcmp(baseline.bytes().data(),record.bytes.data(),record.bytes.size())==0,"A09 exact baseline bytes differ"); }
                save(output/(name+".bin"),record.bytes);
                std::cout << name << ": two-profile prefill/sampler/checkpoints/latents/RNG/short PCM " << (argc==6 ? "exact PASS" : "baseline captured")
                    << "; SHA256 " << detail::hex(detail::sha256(record.bytes)) << '\n';
            }
        }
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
