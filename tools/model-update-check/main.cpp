#include "outloud/orchestration.hpp"
#include "outloud/voice_library.hpp"
#include "asset_io.hpp"
#include "../audio/wav.hpp"
#include <cmath>
#include <iostream>

namespace {
void require(bool value, const char* message) { if (!value) { throw std::runtime_error(message); } }
void synthesize(const outloud::PocketModelAsset& model, const outloud::Tokenizer& tokenizer,
                const outloud::OutLoudVoice& voice, const std::filesystem::path& output, bool expect_eos=false,
                std::string_view text="Hello. This is an Out Loud model update test.") {
    outloud::UtterancePipeline pipeline(model,voice,tokenizer,50,
        outloud::FlowLMGenerator::estimate_max_gen_len(50),outloud::cpu::KernelPath::automatic);
    std::vector<float> pcm;
    auto sink=[](void* context, std::uint64_t, std::size_t, std::uint32_t, std::span<const float> frame) {
        auto& data=*static_cast<std::vector<float>*>(context);
        require(frame.size()==1920 && data.size()<24000*60,"Invalid or excessive smoke-test audio");
        for (float value : frame) { require(std::isfinite(value),"Non-finite smoke PCM"); }
        data.insert(data.end(),frame.begin(),frame.end());
    };
    pipeline.prepare(text,73);
    const auto result=pipeline.run(sink,&pcm);
    require(result.status==outloud::SynthesisStatus::completed && result.samples==pcm.size() && !pcm.empty(),"Synthesis did not complete");
    const bool eos=std::all_of(result.segments.begin(),result.segments.end(),[](const auto& segment) {
        return segment.worker.termination==outloud::GenerationTermination::eos_tail;
    });
    require(!expect_eos || eos,"Fresh model smoke synthesis exhausted its frame budget");
    outloud::development::write_pcm16_wave(output,pcm);
    std::cout << "speech " << output.filename().string() << ": " << pcm.size() << " finite samples; " << (eos?"EOS reached":"frame limit reached") << '\n';
}
template<class Function> void rejects(Function function) {
    bool refused=false;
    try { function(); } catch (const std::exception&) { refused=true; }
    require(refused,"Unsafe asset was accepted");
}
}
int wmain(int argc, wchar_t** argv) {
    try {
        require(argc==9,"Usage: validator model tokenizer prepared.olv existing.olv source.safetensors invalid-voice invalid-model scratch");
        const outloud::PocketModelAsset model(argv[1]); const outloud::Tokenizer tokenizer(argv[2]);
        const outloud::OutLoudVoice prepared(argv[3]), existing(argv[4]);
        prepared.validate_against(model); existing.validate_against(model);
        rejects([&] { const outloud::OutLoudVoice invalid(argv[6]); });
        rejects([&] { const outloud::PocketModelAsset invalid(argv[7]); });
        const std::filesystem::path scratch(argv[8]); std::filesystem::create_directories(scratch);
        const auto imported_path=scratch/"external-import.olv";
        const auto imported_source=outloud::convert_pocket_voice_to_olv(argv[5],imported_path,{});
        const outloud::OutLoudVoice external(imported_path);
        require(external.version()==1 && external.source_sha256()==imported_source.source.source_sha256,"Import changed serialization/source identity");
        external.validate_against(model);
        outloud::VoiceLibrary library(scratch/"library");
        const std::array<std::filesystem::path,3> profiles{argv[3],argv[4],imported_path};
        const std::array<const char*,3> names{"prepared","existing","imported"};
        for (unsigned i=0;i<3;++i) {
            const auto imported=library.import_olv(profiles[i],names[i],outloud::DuplicateContentPolicy::allow_separate_entry);
            const auto inspected=library.inspect_voice(imported.voice.library_id);
            require(inspected.health==outloud::VoiceHealth::valid,"Structurally supported profile was rejected");
            const auto before=outloud::validate_olv(profiles[i]).file_sha256;
            (void)library.export_olv(inspected.library_id,scratch/(std::string(names[i])+".olv"));
            require(outloud::validate_olv(profiles[i]).file_sha256==before,"Original voice was changed");
            require(outloud::validate_olv(scratch/(std::string(names[i])+".olv")).file_sha256==before,"Export rewrote voice identity");
        }
        synthesize(model,tokenizer,prepared,scratch/"prepared.wav",true);
        synthesize(model,tokenizer,existing,scratch/"existing.wav");
        synthesize(model,tokenizer,external,scratch/"imported.wav");
        synthesize(model,tokenizer,prepared,scratch/"numbers.wav",true,
            "Bring 12 coins and 3.5 pounds of ore. The total is 42.");
        std::cout << "PASS: model load, OLV v1 and Safetensors playback, byte-preserving export and invalid asset rejection\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
