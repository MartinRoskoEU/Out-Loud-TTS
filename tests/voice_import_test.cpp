#include "outloud/voice_import.hpp"
#include "outloud/orchestration.hpp"
#include "scratch.hpp"

#include <bit>
#include <cstring>
#include <fstream>
#include <iostream>
#include <sstream>

namespace {
using namespace outloud;
using Code = VoiceImportErrorCode;
using detail::Bytes;
unsigned checks = 0;
void require(bool value, const char* message) { if (!value) { throw std::runtime_error(message); } }
template<class Function> void rejects(Code code, Function function, bool tensor = false) {
    try { function(); }
    catch (const VoiceImportError& error) {
        require(error.code() == code,"Unexpected structured error code");
        require(!error.path().empty(),"Error has no path context");
        if (tensor) { require(!error.tensor().empty(),"Error has no tensor context"); }
        ++checks; return;
    }
    throw std::runtime_error("Invalid input was accepted");
}
void put(std::vector<unsigned char>& bytes, std::size_t offset, std::uint64_t value, unsigned width) {
    for (unsigned i = 0; i < width; ++i) { bytes[offset+i] = static_cast<unsigned char>(value >> (8*i)); }
}
void write(const std::filesystem::path& path, Bytes bytes) {
    std::ofstream out(path,std::ios::binary|std::ios::trunc);
    out.write(reinterpret_cast<const char*>(bytes.data()),static_cast<std::streamsize>(bytes.size()));
    require(bool(out),"Cannot write test fixture");
}
std::vector<unsigned char> read(const std::filesystem::path& path) {
    const detail::MappedFile file(path,1024ULL*1024*1024+65536+256);
    return {file.bytes().begin(),file.bytes().end()};
}
using test::Scratch;
struct Fixture {
    std::string header;
    std::vector<unsigned char> payload;
    Fixture() : payload(6*8208,0) {
        std::ostringstream json; json << '{';
        for (unsigned layer = 0; layer < 6; ++layer) {
            const auto start = layer*8208;
            const auto base = "transformer.layers."+std::to_string(layer)+".self_attn/";
            if (layer) { json << ','; }
            json << '"' << base << "cache\":{\"dtype\":\"F32\",\"shape\":[2,1,1,16,64],\"data_offsets\":["
                 << start << ',' << start+8192 << "]},\"" << base
                 << "offset\":{\"dtype\":\"I64\",\"shape\":[1],\"data_offsets\":["
                 << start+8192 << ',' << start+8200 << "]},\"" << base
                 << "pad\":{\"dtype\":\"I64\",\"shape\":[1],\"data_offsets\":["
                 << start+8200 << ',' << start+8208 << "]}";
            put(payload,start,std::bit_cast<std::uint32_t>(float(layer)+0.25f),4);
            put(payload,start+4,0x80000000,4); // signed zero must survive verbatim
            put(payload,start+8,1,4); // smallest subnormal must survive verbatim
            put(payload,start+8192,1,8);
        }
        json << '}'; header = json.str();
    }
    void change(std::string_view before, std::string_view after) {
        const auto at = header.find(before); require(at != std::string::npos,"Fixture edit not found");
        header.replace(at,before.size(),after);
    }
    std::vector<unsigned char> bytes() const {
        auto padded = header;
        while (padded.size()%8) { padded += ' '; }
        std::vector<unsigned char> result(8); put(result,0,padded.size(),8);
        result.insert(result.end(),padded.begin(),padded.end());
        result.insert(result.end(),payload.begin(),payload.end()); return result;
    }
};
void synthesis(const std::filesystem::path& imported, const std::filesystem::path& model_path,
               const std::filesystem::path& tokenizer_path, const std::filesystem::path& canonical_path) {
    const PocketModelAsset model(model_path); const OutLoudVoice voice(imported);
    voice.validate_against(model); const Tokenizer tokenizer(tokenizer_path);
    UtterancePipeline pipeline(model,voice,tokenizer,51,FlowLMGenerator::estimate_max_gen_len(51),cpu::KernelPath::avx2_fma);
    const auto canonical = read(canonical_path);
    require(canonical.size()==326400*4,"Canonical PCM size differs");
    struct Sink {
        Bytes expected; std::uint64_t frames = 0;
        static void frame(void* context,std::uint64_t global,std::size_t,std::uint32_t,std::span<const float> pcm) {
            auto& sink = *static_cast<Sink*>(context);
            require(global==sink.frames && pcm.size()==1920,"Imported voice frame scheduling differs");
            const auto at = static_cast<std::size_t>(global)*1920*4;
            require(at <= sink.expected.size() && sink.expected.size()-at >= pcm.size()*4,"Extra imported voice PCM");
            require(std::memcmp(pcm.data(),sink.expected.data()+at,pcm.size()*4)==0,"Imported voice PCM differs");
            ++sink.frames;
        }
    } sink{canonical};
    constexpr std::string_view text = "The watch captain needs your help before the next caravan arrives. Follow the river to the abandoned tower, speak with the scout waiting at its gate, and bring his report back to the village. Keep your lantern burning, for the path is narrow.";
    pipeline.prepare(text,73); const auto result = pipeline.run(Sink::frame,&sink);
    require(result.status==SynthesisStatus::completed && result.draws==176 && result.candidates==173 &&
            result.frames==170 && result.samples==326400 && result.segments.size()==3 && sink.frames==170,
            "Imported voice utterance scheduling differs");
    constexpr std::array<unsigned,3> frames{46,84,40}, draws{48,86,42}, eos{43,81,37};
    for (std::size_t i=0;i<3;++i) {
        const auto& worker=result.segments[i].worker;
        require(worker.emitted==frames[i] && worker.draws==draws[i] && worker.first_eos==eos[i],"Imported voice segment differs");
    }
    std::cout << "PASS: imported voice, production loader, exact 170-frame / 326400-sample canonical FP32 synthesis\n"
              << "pcm_sha256=" << detail::hex(detail::sha256(canonical)) << '\n';
}
}
int wmain(int argc,wchar_t** argv) {
    try {
        require(argc==3 || argc==6,"Usage: outloud-voice-import-tests source.safetensors known-good.olv [model.olp tokenizer.bin canonical.f32]");
        Scratch scratch;
        const auto source=scratch.path/L"source.safetensors", output=scratch.path/L"imported.olv";
        const auto repeat=scratch.path/L"repeat.olv", bad=scratch.path/L"bad.olv";
        VoiceImportOptions options; options.declared_model_compatibility_id=std::string(pocket_model_compatibility_id);
        options.expected_source_sha256="2118ea60dfe87bd515b27e051e1d677a7fe15663875740599147bde37a9c4cea";
        const auto inspection=inspect_pocket_voice(argv[1]);
        require(inspection.source_sha256==options.expected_source_sha256 && inspection.prefix_length==112 &&
                inspection.metadata.empty() && !inspection.model_provenance_verified,"Gnome source audit differs");
        const auto imported=convert_pocket_voice_to_olv(argv[1],output,options);
        (void)convert_pocket_voice_to_olv(argv[1],repeat,options);
        const auto original=read(argv[2]), produced=read(output);
        require(produced==original && read(repeat)==produced,"Gnome serialization is not exact/deterministic");
        require(imported.voice.file_sha256=="ff138495b7d3834c2eabb926ff7226a2983e7af17cf5a0c6b4cad05ae6a5bb00","Gnome file identity differs");
        {
            const OutLoudVoice expected(argv[2]), actual(output);
            for (unsigned layer=0;layer<6;++layer) {
                require(actual.cache(layer).size()==expected.cache(layer).size() &&
                    std::memcmp(actual.cache(layer).data(),expected.cache(layer).data(),actual.cache(layer).size_bytes())==0,
                    "Gnome K/V bytes differ");
            }
        }
        std::cout << "PASS: Gnome entire .olv byte equality, all six K/V tensors and deterministic repeat\n";
        if (argc==6) { synthesis(output,argv[3],argv[4],argv[5]); }
        const auto unchanged=read(output);
        auto changed_options=options; changed_options.declared_model_compatibility_id.clear();
        rejects(Code::compatibility_required,[&] { (void)convert_pocket_voice_to_olv(argv[1],output,changed_options); });
        changed_options=options; changed_options.declared_model_compatibility_id=std::string(64,'0');
        rejects(Code::incompatible_model,[&] { (void)convert_pocket_voice_to_olv(argv[1],output,changed_options); });
        changed_options=options; changed_options.expected_source_sha256=std::string(64,'0');
        rejects(Code::source_identity_mismatch,[&] { (void)convert_pocket_voice_to_olv(argv[1],output,changed_options); });
        rejects(Code::invalid_destination,[&] { (void)convert_pocket_voice_to_olv(argv[1],output,options); });
        rejects(Code::invalid_destination,[&] { (void)convert_pocket_voice_to_olv(argv[1],scratch.path/"absent"/"voice.olv",options); });
        rejects(Code::invalid_destination,[&] { (void)convert_pocket_voice_to_olv(argv[1],scratch.path/"voice.txt",options); });
        rejects(Code::file_not_found,[&] { (void)inspect_pocket_voice(scratch.path/"missing"); });
        rejects(Code::source_io,[&] { (void)inspect_pocket_voice(scratch.path); });
        rejects(Code::source_io,[&] { (void)inspect_pocket_voice(L"\\\\server\\share\\voice.safetensors"); });
        options.expected_source_sha256.clear(); options.replace_existing=true;
        auto invalid=[&](Fixture fixture,Code code,bool tensor=false) {
            write(source,fixture.bytes());
            rejects(code,[&] { (void)inspect_pocket_voice(source); },tensor);
            rejects(code,[&] { (void)convert_pocket_voice_to_olv(source,output,options); },tensor);
            require(read(output)==unchanged,"Source failure changed existing destination");
        };
        Fixture fixture;
        fixture.change("[2,1,1,16,64]","[2,1,1,15,64]"); invalid(fixture,Code::invalid_shape,true);
        fixture=Fixture(); fixture.change("[2,1,1,16,64]","[2,1,0,16,64]"); invalid(fixture,Code::invalid_shape,true);
        fixture=Fixture(); fixture.change("[2,1,1,16,64]","[2,1,18446744073709551616,16,64]"); invalid(fixture,Code::invalid_shape,true);
        fixture=Fixture(); fixture.change("[2,1,1,16,64]","[2,1,-1,16,64]"); invalid(fixture,Code::invalid_shape,true);
        fixture=Fixture(); fixture.change("[2,1,1,16,64]","[2,1,1.0,16,64]"); invalid(fixture,Code::invalid_shape,true);
        fixture=Fixture(); fixture.change("[2,1,1,16,64]","[2,1,true,16,64]"); invalid(fixture,Code::invalid_shape,true);
        fixture=Fixture(); fixture.change("\"F32\"","\"BF16\""); invalid(fixture,Code::unsupported_dtype,true);
        fixture=Fixture(); fixture.change("\"I64\"","\"F32\""); invalid(fixture,Code::unsupported_dtype,true);
        fixture=Fixture(); fixture.change("self_attn/offset","self_attn/current_end"); invalid(fixture,Code::malformed_tensor,true);
        fixture=Fixture(); fixture.change("\"dtype\":\"F32\"","\"dtype\":\"F32\",\"dtype\":\"F32\""); invalid(fixture,Code::corrupt_source);
        fixture=Fixture(); fixture.change("\"dtype\":\"F32\"","\"dtype\":\"F32\",\"d\\u0074ype\":\"F32\""); invalid(fixture,Code::corrupt_source);
        fixture=Fixture(); fixture.change("[0,8192]","[18446744073709551615,8192]"); invalid(fixture,Code::malformed_tensor,true);
        fixture=Fixture(); fixture.change("[0,8192]","[0,8191]"); invalid(fixture,Code::malformed_tensor,true);
        fixture=Fixture(); fixture.change("[8208,16400]","[0,8192]"); invalid(fixture,Code::corrupt_source);
        fixture=Fixture(); fixture.payload.push_back(0); invalid(fixture,Code::corrupt_source);
        fixture=Fixture(); put(fixture.payload,8192,2,8); invalid(fixture,Code::malformed_tensor,true);
        fixture=Fixture(); put(fixture.payload,8200,1,8); invalid(fixture,Code::malformed_tensor,true);
        for (auto bits : {0x7f800000U,0xff800000U,0x7fc00000U}) {
            fixture=Fixture(); put(fixture.payload,0,bits,4); invalid(fixture,Code::malformed_tensor,true);
        }
        fixture=Fixture(); fixture.change("{","{\"__metadata__\":{\"version\":3},"); invalid(fixture,Code::corrupt_source);
        fixture=Fixture(); fixture.change("{","{\"__metadata__\":{\"note\":\"\\ud800\"},"); invalid(fixture,Code::corrupt_source);
        fixture=Fixture(); fixture.change("{","{\"__metadata__\":{\"note\":\"\xc0\xaf\"},"); invalid(fixture,Code::corrupt_source);
        fixture=Fixture(); fixture.change("{","{\"__metadata__\":{\"note\":[[[[[[[[[0]]]]]]]]]},"); invalid(fixture,Code::corrupt_source);
        auto truncated=Fixture().bytes(); truncated.resize(10); write(source,truncated);
        rejects(Code::corrupt_source,[&] { (void)validate_pocket_voice(source); });
        truncated=Fixture().bytes(); truncated.pop_back(); write(source,truncated);
        rejects(Code::malformed_tensor,[&] { (void)validate_pocket_voice(source); },true);
        write(source,std::array<unsigned char,4>{1,2,3,4});
        rejects(Code::corrupt_source,[&] { (void)inspect_pocket_voice(source); });
        write(source,std::array<unsigned char,8>{'P','K',3,4,0,0,0,0});
        rejects(Code::unsupported_format,[&] { (void)inspect_pocket_voice(source); });
        fixture=Fixture(); fixture.change("{","{\"__metadata__\":{\"note\":\"caf\\u00e9 \\ud83d\\ude00\",\"model_version\":\"unknown\"},");
        write(source,fixture.bytes());
        const auto metadata=validate_pocket_voice(source);
        require(metadata.metadata.at("note")=="caf\xc3\xa9 \xf0\x9f\x98\x80" &&
                !metadata.model_provenance_verified,"Metadata decoding falsely established compatibility");
        // Arbitrary source metadata has no pinned model/version semantics. Caller declaration remains required.
        auto no_declaration=options; no_declaration.declared_model_compatibility_id.clear();
        rejects(Code::compatibility_required,[&] { (void)convert_pocket_voice_to_olv(source,output,no_declaration); });
        {
            const auto locked=CreateFileW(output.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,0,nullptr);
            require(locked!=INVALID_HANDLE_VALUE,"Cannot lock destination for I/O failure test");
            try { rejects(Code::destination_io,[&] { (void)convert_pocket_voice_to_olv(source,output,options); }); }
            catch (...) { CloseHandle(locked); throw; }
            CloseHandle(locked);
        }
        require(read(output)==unchanged,"Finalization failure changed existing destination");
        require(std::distance(std::filesystem::directory_iterator(scratch.path),std::filesystem::directory_iterator())==3,
                "Failed import left a temporary file");
        // Source alias through a hard link must also be rejected with replacement requested.
        std::filesystem::create_hard_link(source,bad);
        rejects(Code::invalid_destination,[&] { (void)convert_pocket_voice_to_olv(source,bad,options); });
        std::filesystem::remove(bad);
        const auto unicode=scratch.path/L"hlas-\u017e\u00e1ba.olv";
        const auto small=convert_pocket_voice_to_olv(source,unicode,options);
        require(small.voice.prefix_length==1 && small.source.metadata==metadata.metadata,"Unicode destination or metadata handling failed");
        const auto small_bytes=read(unicode);
        for (std::size_t layer=0;layer<6;++layer) {
            require(std::memcmp(small_bytes.data()+256+layer*8192,fixture.payload.data()+layer*8208,8192)==0,
                    "Import transformed FP32/signed-zero/subnormal bytes");
        }
        (void)convert_pocket_voice_to_olv(source,output,options);
        require(read(output)==small_bytes,"Explicit replacement or deterministic serialization failed");
        auto corrupt=produced; corrupt.pop_back(); write(bad,corrupt);
        rejects(Code::olv_validation,[&] { (void)validate_olv(bad); });
        corrupt=produced; corrupt[256]^=1; write(bad,corrupt);
        rejects(Code::olv_validation,[&] { (void)validate_olv(bad); });
        corrupt=produced; put(corrupt,8,2,4); write(bad,corrupt);
        rejects(Code::olv_validation,[&] { (void)validate_olv(bad); });
        rejects(Code::incompatible_model,[&] { (void)validate_olv(repeat,std::string(64,'0')); });
        std::cout << "PASS: " << checks << " structured rejection checks; finite exact byte copy, metadata, Unicode, replacement, locked destination, cleanup\nRESULT: PASS\n";
        return 0;
    } catch (const VoiceImportError& error) {
        std::cerr << voice_import_error_name(error.code()) << ": " << error.what() << " tensor=" << error.tensor() << '\n'; return 1;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
