#include "mimi_fixture_checks.hpp"
#include "outloud/pcm16.hpp"
#include "outloud/worker_pipeline.hpp"
#include <cstring>
#include <iostream>

namespace {
using namespace outloud;
using Clock=std::chrono::steady_clock;
using test::neural_require;
double ms(Clock::time_point a,Clock::time_point b) { return std::chrono::duration<double,std::milli>(b-a).count(); }
std::string hash(std::span<const float> values) {
    return detail::hex(detail::sha256({reinterpret_cast<const unsigned char*>(values.data()),values.size_bytes()}));
}
void write(const std::filesystem::path& path,detail::Bytes bytes) {
    std::ofstream file(path,std::ios::binary); file.write(reinterpret_cast<const char*>(bytes.data()),static_cast<std::streamsize>(bytes.size()));
    neural_require(bool(file),"Cannot write A08 artifact");
}
void equal_file(const std::filesystem::path& path,detail::Bytes bytes) {
    const detail::MappedFile file(path,64*1024*1024);
    neural_require(file.bytes().size()==bytes.size() && std::memcmp(file.bytes().data(),bytes.data(),bytes.size())==0,"A08 baseline bytes differ");
}
void capture(const PocketModelAsset& model,const test::MimiFixture& fixture,cpu::KernelPath path,
        const std::filesystem::path& output,const std::filesystem::path& baseline,const std::string& name) {
    MimiDecoder decoder(model,path); MimiDecoderState state(109);
    std::map<std::string,std::string> hashes; std::map<std::string,test::SamplerError> errors;
    std::vector<float> pcm; pcm.reserve(88320); const auto latents=fixture.values("normalized.latents");
    for (std::size_t frame=0;frame<46;++frame) {
        MimiObserver observer;
        if (frame==0 || frame==1 || frame==2 || frame==23 || frame==45) {
            observer=[&](std::string_view field,std::span<const float> values) {
                const auto key=test::mimi_frame(frame)+std::string(field);
                hashes[key]=hash(values);
                if (!values.empty()) { errors[key]=test::sampler_error(values,fixture.values(key)); }
            };
        }
        const auto values=decoder.decode_frame(latents.subspan(frame*32,32),state,observer);
        neural_require(values.size()==1920 && state.offset()==(frame+1)*16 && state.valid(),"A08 scheduling/state differs");
        pcm.insert(pcm.end(),values.begin(),values.end());
    }
    errors["pcm.complete"]=test::sampler_error(pcm,fixture.values("pcm.complete")); test::enforce_mimi_errors(errors);
    for (std::size_t layer=0;layer<2;++layer) {
        hashes["final.keys"+std::to_string(layer)]=hash(state.keys(layer)); hashes["final.values"+std::to_string(layer)]=hash(state.values(layer));
    }
    for (std::size_t group=0;group<12;++group) { hashes["final.carry"+std::to_string(group)]=hash(state.convolution_state(group)); }
    std::ostringstream manifest;
    for (const auto& [key,value]:hashes) { manifest << key << ' ' << value << '\n'; }
    const auto text=manifest.str(); const detail::Bytes checkpoints{reinterpret_cast<const unsigned char*>(text.data()),text.size()};
    const detail::Bytes fp32{reinterpret_cast<const unsigned char*>(pcm.data()),pcm.size()*sizeof(float)};
    std::vector<std::int16_t> pcm16(pcm.size()); std::transform(pcm.begin(),pcm.end(),pcm16.begin(),float_to_pcm16);
    const detail::Bytes integer{reinterpret_cast<const unsigned char*>(pcm16.data()),pcm16.size()*sizeof(std::int16_t)};
    if (!baseline.empty()) {
        equal_file(baseline/(name+".f32"),fp32); equal_file(baseline/(name+".pcm16"),integer);
        equal_file(baseline/(name+"-checkpoints.txt"),checkpoints);
    }
    write(output/(name+".f32"),fp32); write(output/(name+".pcm16"),integer); write(output/(name+"-checkpoints.txt"),checkpoints);
    const auto saved=std::vector<float>(pcm.begin(),pcm.begin()+1920);
    state.reset(); neural_require(test::same_float_bits(decoder.decode_frame(latents.first(32),state),saved),"A08 resident reset differs");
    std::cout << name << ": 46 frames / 88320 samples; PCM SHA256 " << hash(pcm)
        << (baseline.empty() ? "; baseline captured\n" : "; exact FP32/PCM16 and checkpoint hashes PASS\n");
}
void stats(std::ostream& file,std::array<double,3> values) {
    file << "{\"runs\":[" << values[0] << ',' << values[1] << ',' << values[2] << ']';
    std::sort(values.begin(),values.end()); file << ",\"min\":" << values[0] << ",\"median\":" << values[1] << ",\"max\":" << values[2] << '}';
}
void benchmark(const PocketModelAsset& model,const test::MimiFixture& fixture,cpu::KernelPath path,
        const std::filesystem::path& output,const std::string& name) {
    std::array<double,3> preparation{}, initialization{}, first{}, total{};
    { cpu::MimiConvolutionKernels warm(model,path); }
    for (std::size_t run=0;run<3;++run) {
        const auto start=Clock::now(); cpu::MimiConvolutionKernels kernels(model,path); const auto end=Clock::now();
        preparation[run]=ms(start,end);
    }
    { MimiDecoder warm(model,path); }
    for (std::size_t run=0;run<3;++run) {
        const auto start=Clock::now(); MimiDecoder decoder(model,path); const auto end=Clock::now(); initialization[run]=ms(start,end);
    }
    MimiDecoder decoder(model,path); MimiDecoderState state(109); const auto latents=fixture.values("normalized.latents");
    const auto decode=[&] {
        for (std::size_t frame=0;frame<46;++frame) { (void)decoder.decode_frame(latents.subspan(frame*32,32),state); }
        neural_require(state.valid() && state.offset()==736,"Benchmark workload differs");
    };
    decode(); // One complete untimed warm-up. No observer/profiler/allocation hook.
    for (std::size_t run=0;run<3;++run) {
        state.reset(); const auto start=Clock::now();
        (void)decoder.decode_frame(latents.first(32),state); const auto ready=Clock::now();
        for (std::size_t frame=1;frame<46;++frame) { (void)decoder.decode_frame(latents.subspan(frame*32,32),state); }
        const auto end=Clock::now(); first[run]=ms(start,ready); total[run]=ms(start,end);
        neural_require(state.valid() && state.offset()==736,"Timed benchmark scheduling differs");
    }
    std::ofstream file(output/(name+"-benchmark.json")); file << std::setprecision(17)
        << "{\"warmups\":1,\"repetitions\":3,\"frames\":46,\"samples\":88320,\"profiler_or_hooks\":false,\"prepared_weight_bytes\":"
        << decoder.prepared_weight_bytes() << ",\"convolution_owner_bytes\":" << sizeof(cpu::MimiConvolutionKernels)
        << ",\"decoder_inline_bytes\":" << sizeof(MimiDecoder) << ",\"convolution_owner_init_ms\":";
    stats(file,preparation); file << ",\"decoder_init_ms\":"; stats(file,initialization);
    file << ",\"first_frame_ms\":"; stats(file,first); file << ",\"decode_ms\":"; stats(file,total); file << "}\n";
    neural_require(bool(file),"Cannot write benchmark report");
    std::sort(total.begin(),total.end()); std::cout << name << ": 46-frame median " << total[1] << " ms\n";
}
void worker_checks(const PocketModelAsset& model,const std::filesystem::path& assets,cpu::KernelPath path) {
    std::string first_voice;
    for (const auto* filename:{L"gnome_male.olv",L"undead_male.olv"}) {
        const OutLoudVoice voice(assets/filename); voice.validate_against(model);
        if (first_voice.empty()) { first_voice=voice.source_sha256(); }
        else { neural_require(first_voice!=voice.source_sha256(),"Worker profiles are not distinct"); }
        WorkerPipeline workers(model,voice,20,3,path); GenerationOptions options; options.max_gen_len=3;
        std::vector<float> pcm(5760), expected;
        const auto normal=[&] {
            workers.prepare(test::neural_tokens,73,pcm,options);
            const auto result=workers.run(); neural_require(result.status==SynthesisStatus::completed && result.samples==5760 &&
                result.emitted==3 && result.mimi_offset==48,"Short resident workload changed");
        };
        normal(); expected=pcm; normal(); neural_require(test::same_float_bits(pcm,expected),"Consecutive resident requests differ");
        std::stop_source early; early.request_stop(); workers.prepare(test::neural_tokens,73,pcm,options);
        const auto cancelled=workers.run(early.get_token());
        neural_require(cancelled.status==SynthesisStatus::cancelled && cancelled.samples==0 && cancelled.draws==0,"Pre-cancellation computed audio");
        struct Sink {
            std::stop_source source; unsigned frames=0;
            static void frame(void* context,std::uint32_t index,std::span<const float> values) {
                auto& self=*static_cast<Sink*>(context);
                neural_require(index==self.frames && values.size()==1920,"Cancelled stream order/size differs");
                ++self.frames; self.source.request_stop();
            }
        } sink;
        workers.prepare(test::neural_tokens,73,pcm,options,Sink::frame,&sink);
        const auto interrupted=workers.run(sink.source.get_token());
        neural_require(interrupted.status==SynthesisStatus::cancelled && interrupted.samples==1920 && sink.frames==1,"Streaming cancellation did not quiesce");
        normal(); neural_require(test::same_float_bits(pcm,expected),"Cancellation invalidated decoder cache/recovery");
        struct FailedNoise final : NoiseSource {
            std::span<const float> next() override { throw std::runtime_error("Injected short producer failure"); }
        } noise;
        workers.prepare(test::neural_tokens,noise,pcm,options); bool failed=false;
        try { (void)workers.run(); } catch (const std::runtime_error&) { failed=true; }
        neural_require(failed,"Producer failure lost"); normal();
        neural_require(test::same_float_bits(pcm,expected),"Worker failure invalidated decoder cache/recovery");
        std::cout << cpu::path_name(path) << ' ' << std::filesystem::path(filename).string()
            << ": 3-frame resident repeat / early & streaming cancellation / failure recovery exact PASS\n";
    }
}
}
int wmain(int argc,wchar_t** argv) {
    try {
        neural_require(argc==5 || argc==6,"Usage: mimi-cache-check capture|compare|bench|worker MODEL NEURAL OUTPUT [BASELINE]");
        const std::wstring mode=argv[1]; neural_require(mode==L"capture" || mode==L"compare" || mode==L"bench" || mode==L"worker","Unknown A08 mode");
        neural_require((mode==L"compare")== (argc==6),"Compare requires exact baseline directory");
        const PocketModelAsset model(argv[2]); const test::MimiFixture fixture(argv[3]);
        const std::filesystem::path output=argv[4], baseline=argc==6 ? std::filesystem::path(argv[5]) : std::filesystem::path{};
        std::filesystem::create_directories(output);
        for (const auto path:{cpu::KernelPath::portable,cpu::KernelPath::avx2_fma}) {
            if (path==cpu::KernelPath::avx2_fma && !cpu::optimized_available()) { std::cout << "AVX2 unavailable\n"; continue; }
            const std::string name=path==cpu::KernelPath::portable ? "portable" : "avx2";
            if (mode==L"worker") { worker_checks(model,std::filesystem::path(argv[2]).parent_path(),path); }
            else if (mode==L"bench") { benchmark(model,fixture,path,output,name); }
            else { capture(model,fixture,path,output,baseline,name); }
        }
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
