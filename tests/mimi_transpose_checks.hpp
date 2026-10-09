#pragma once
// Minimal old dense-transpose primitive only; never linked into production.
namespace outloud::cpu {
struct MimiConvolutionTestAccess {
    static std::span<const float> rows(const MimiConvolutionKernels& kernels,std::size_t configuration) {
        const auto& s=mimi_convolutions.at(configuration);
        return std::span<const float>(kernels.phase_weights_).subspan(kernels.phase_offsets_[configuration],
            std::size_t(s.output)*s.stride*s.input*2);
    }
};
}
namespace outloud::test {
inline void old_dense_transpose(const PocketModelAsset& model,std::size_t configuration,cpu::KernelPath path,
        std::span<const float> input,std::span<float> carry,std::span<float> output) {
    const auto& s=cpu::mimi_convolutions.at(configuration);
    neural_require(s.transpose && s.groups==1,"Old primitive requires dense transpose");
    cpu::Kernels kernels(path);
    const auto steps=input.size()/s.input, width=std::size_t(s.input)*2, length=steps*s.stride;
    const auto weight=model.tensor(static_cast<PocketTensorId>(s.weight)).bf16_values;
    const auto bias=model.tensor(static_cast<PocketTensorId>(s.bias)).bf16_values;
    const auto add=[&](std::size_t c) { return bf16_to_float32(bias[c]); };
    std::vector<float> windows((steps+1)*width); std::array<float,4096> row;
    for (std::size_t t=0;t<=steps;++t) {
        for (std::size_t c=0;c<s.input;++c) {
            windows[t*width+c*2]=t<steps ? input[c*steps+t] : 0.0f;
            windows[t*width+c*2+1]=t>0 ? input[c*steps+t-1] : 0.0f;
        }
    }
    for (std::size_t c=0;c<s.output;++c) {
        for (std::size_t p=0;p<s.stride;++p) {
            for (std::size_t in=0;in<s.input;++in) {
                const auto w=(in*s.output+c)*s.kernel+p;
                row[in*2]=bf16_to_float32(weight[w]); row[in*2+1]=bf16_to_float32(weight[w+s.stride]);
            }
            kernels.dot_rows(std::span(row).first(width),{std::span(windows).first(steps*width),steps,width},
                output.subspan(c*length+p,length-p),s.stride,add(c));
            output[c*length+p]+=carry[c*s.stride+p];
            const float tail=kernels.dot(std::span(row).first(width),std::span(windows).subspan(steps*width,width))+add(c);
            carry[c*s.stride+p]=tail-add(c);
        }
    }
}
inline void transpose_cache_checks(const PocketModelAsset& model,const MimiFixture& fixture,cpu::KernelPath path) {
    cpu::MimiConvolutionKernels kernels(model,path);
    neural_require(kernels.prepared_weight_bytes()==7864320,"Unexpected Mimi cache allocation");
    std::size_t checked=0;
    for (const auto configuration:{2U,5U,8U}) {
        const auto& s=cpu::mimi_convolutions[configuration];
        const auto weight=model.tensor(static_cast<PocketTensorId>(s.weight)).bf16_values;
        const auto rows=cpu::MimiConvolutionTestAccess::rows(kernels,configuration);
        const auto width=std::size_t(s.input)*2;
        for (std::size_t c=0;c<s.output;++c) {
            for (std::size_t p=0;p<s.stride;++p) {
                for (std::size_t in=0;in<s.input;++in) {
                    const auto w=(in*s.output+c)*s.kernel+p, row=(c*s.stride+p)*width+in*2;
                    neural_require(std::bit_cast<std::uint32_t>(rows[row])==(std::uint32_t(weight[w])<<16) &&
                        std::bit_cast<std::uint32_t>(rows[row+1])==(std::uint32_t(weight[w+s.stride])<<16),"Prepared phase values differ from old BF16 conversion");
                    checked+=2;
                }
            }
        }
        const auto hash=detail::sha256({reinterpret_cast<const unsigned char*>(rows.data()),rows.size_bytes()});
        for (const auto frame:{0,1,2,23,45}) {
            const auto prefix=mimi_frame(frame)+std::string(s.name);
            const auto input=fixture.values(prefix+".input"), initial=fixture.values(prefix+".state_before");
            std::vector<float> carry(initial.begin(),initial.end()), old_carry=carry;
            std::vector<float> output(fixture.values(prefix+".output").size()), old_output(output.size());
            old_dense_transpose(model,configuration,path,input,old_carry,old_output);
            kernels.run(configuration,input,carry,output);
            neural_require(same_float_bits(output,old_output) && same_float_bits(carry,old_carry),"Cached transpose output/carry differs bitwise");
        }
        neural_require(detail::sha256({reinterpret_cast<const unsigned char*>(rows.data()),rows.size_bytes()})==hash,"Decoding mutated prepared rows");
    }
    neural_require(checked==1966080,"Incomplete prepared weight coverage");
    std::cout << cpu::path_name(path) << ": 1966080 phase values and 15 transpose output/carry cases bitwise PASS; cache 7864320 bytes\n";
}
} // namespace outloud::test
