#include "outloud/worker_pipeline.hpp"
#include "worker_audit.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <exception>
#include <iomanip>
#include <mutex>
#include <stdexcept>
#include <thread>

namespace outloud {
namespace {
using Clock = std::chrono::steady_clock;
double elapsed(Clock::time_point a, Clock::time_point b) {
    return std::chrono::duration<double, std::milli>(b - a).count();
}
std::uint32_t suffix_capacity(std::uint32_t tokens, std::uint32_t frames) {
    if (!tokens || !frames || frames >= 4096 || tokens > 4096 - frames) {
        throw std::invalid_argument("Invalid worker state capacity");
    }
    return tokens + frames;
}
#ifdef OUTLOUD_WORKER_TRACE
#define TRACE(...) __VA_ARGS__
#else
#define TRACE(...) do {} while (false)
#endif
}
struct WorkerPipeline::Impl {
    FlowLMBackbone backbone;
    FlowSampler sampler;
    MimiDecoder mimi;
    FlowLMPrefillState flow;
    MimiDecoderState decoder;
    DeterministicCandidate candidate;
    const std::uint32_t maximum_tokens, maximum_frames;
    std::array<std::array<float, 32>, 2> queue{};
    std::size_t head = 0, size = 0;
    std::mutex mutex;
    std::condition_variable jobs, changed, finished;
    bool stopping = false, producer_done = false, prepared = false, running = false, valid_result = false, reset_job = false;
    std::uint64_t epoch = 0;
    unsigned ready = 0, completed = 0;
    std::exception_ptr failure;
    std::thread producer, consumer;
    std::span<const std::uint32_t> tokens;
    NoiseSource* noise = nullptr;
    std::span<float> destination;
    GenerationOptions options;
    PCMFrameSink sink = nullptr;
    void* sink_context = nullptr;
    const GenerationObserver* observer = nullptr;
    WorkerResult result;
    Clock::time_point start;
    std::uint64_t producer_allocations = 0, consumer_allocations = 0;
#ifdef OUTLOUD_WORKER_TRACE
    struct CandidateTrace { double begin = 0, end = 0, ready = 0, pushed = 0, wait = 0; };
    struct FrameTrace { double popped = 0, begin = 0, end = 0, pcm = 0, wait = 0; };
    struct QueueTrace { double time = 0; std::size_t occupancy = 0; };
    std::vector<CandidateTrace> candidates;
    std::vector<FrameTrace> frames;
    std::vector<QueueTrace> occupancy;
    std::size_t events = 0;
    double prefill_begin = 0, prefill_end = 0, producer_end = 0, consumer_end = 0;
    double now() const { return elapsed(start, Clock::now()); }
#endif
    Impl(PocketModelAsset model, OutLoudVoice voice, std::uint32_t max_tokens, std::uint32_t max_frames,
         cpu::KernelPath path)
        : backbone(model, voice, path), sampler(model, path), mimi(model, path),
          flow(backbone.make_state(suffix_capacity(max_tokens, max_frames))), decoder(max_frames),
          maximum_tokens(max_tokens), maximum_frames(max_frames) {
        candidate.backbone.normalized.resize(1024);
        TRACE(candidates.resize(max_frames); frames.resize(max_frames); occupancy.resize(2 * max_frames));
        try {
            producer = std::thread([this] { worker(true); });
            consumer = std::thread([this] { worker(false); });
            std::unique_lock lock(mutex);
            finished.wait(lock, [this] { return ready == 2; }); // both resident workers have started
        } catch (...) { stop(); throw; }
    }
    ~Impl() { stop(); }
    void stop() noexcept {
        { std::lock_guard lock(mutex); stopping = true; }
        jobs.notify_all(); changed.notify_all();
        if (producer.joinable()) { producer.join(); }
        if (consumer.joinable()) { consumer.join(); }
    }
    void worker(bool is_producer) noexcept {
        std::uint64_t seen = 0;
        std::unique_lock lock(mutex);
        ++ready; finished.notify_one();
        for (;;) {
            jobs.wait(lock, [&] { return stopping || epoch != seen; });
            if (stopping) { return; }
            seen = epoch;
            const bool resetting = reset_job;
            lock.unlock();
            try {
                if (resetting) { if (is_producer) { flow.reset(); } else { decoder.reset(); } }
                else { if (is_producer) { produce(); } else { consume(); } }
            }
            catch (...) {
                std::lock_guard failed_lock(mutex);
                if (!failure) { failure = std::current_exception(); }
                changed.notify_all();
            }
#ifdef OUTLOUD_WORKER_AUDIT
            development::audit = false;
            if (!resetting) { (is_producer ? producer_allocations : consumer_allocations) = development::allocations; }
#endif
            lock.lock();
            if (!resetting) {
                if (is_producer) { producer_done = true; TRACE(producer_end = now()); }
                else { TRACE(consumer_end = now()); }
            }
            ++completed;
            changed.notify_all(); finished.notify_one();
        }
    }
    void produce() {
        GenerationStopPolicy policy(options);
        TRACE(prefill_begin = now());
        const auto before = flow.offset();
        {
            const auto prefill = backbone.prefill(tokens, flow);
            const auto row = noise->next(); ++result.draws;
            const auto sample = sampler.sample(prefill.last_hidden(), row); // deliberately discarded
            if (observer && *observer) { (*observer)({GenerationInput::text, 0, before, flow.offset(), {},
                prefill.last_hidden(), row, sample, prefill.eos_logit,
                prefill.eos_logit > options.eos_threshold, false, {}}); }
        }
        TRACE(prefill_end = now());
#ifdef OUTLOUD_WORKER_AUDIT
        development::allocations = 0; development::audit = true; // existing audit boundary: after prefill
#endif
        std::array<float, 32> previous{};
        for (std::uint32_t i = 0; i < options.max_gen_len; ++i) {
            TRACE(candidates[i].begin = now());
            const auto offset = flow.offset();
            const auto row = noise->next(); ++result.draws;
            if (i == 0) { sampler.bos_candidate_into(backbone, flow, row, candidate); }
            else { sampler.frame_candidate_into(backbone, flow, previous, row, candidate); }
            TRACE(candidates[i].end = now());
            ++result.candidates;
            const bool emit = policy.emit(i, candidate.backbone.eos_logit);
            result.first_eos = policy.first_eos();
            if (observer && *observer) { (*observer)({i == 0 ? GenerationInput::bos : GenerationInput::previous_normalized_latent,
                i, offset, flow.offset(), i == 0 ? std::span<const float>{} : std::span<const float>(previous),
                candidate.backbone.last_hidden(), row, candidate.sample, candidate.backbone.eos_logit,
                candidate.backbone.eos_logit > options.eos_threshold, emit, result.first_eos}); }
            if (!emit) { result.termination = GenerationTermination::eos_tail; break; }
            previous = candidate.sample.latent; // feedback is independent of decoder completion
            TRACE(candidates[i].ready = now());
            std::unique_lock lock(mutex);
            TRACE(const double wait_begin = now(); const bool blocked = size == queue.size());
            changed.wait(lock, [this] { return size < queue.size() || stopping || failure; });
            TRACE(if (blocked) { candidates[i].wait = now() - wait_begin; });
            if (stopping || failure) { return; }
            queue[(head + size) % queue.size()] = previous;
            ++size; ++result.emitted;
            TRACE(candidates[i].pushed = now(); occupancy[events++] = {now(), size});
            lock.unlock(); changed.notify_one();
        }
        result.flow_offset = flow.offset();
    }
    void consume() {
#ifdef OUTLOUD_WORKER_AUDIT
        development::allocations = 0; development::audit = true;
#endif
        for (std::uint32_t i = 0; ; ++i) {
            std::array<float, 32> latent;
            std::unique_lock lock(mutex);
            TRACE(const double wait_begin = now(); const bool blocked = size == 0 && !producer_done);
            changed.wait(lock, [this] { return size || producer_done || stopping || failure; });
            // The terminal wait uses its own scalar, because i can equal maximum_frames.
            TRACE(const double waited = blocked ? now() - wait_begin : 0);
            if (stopping || failure || (!size && producer_done)) { TRACE(terminal_wait = waited); break; }
            latent = queue[head]; head = (head + 1) % queue.size(); --size;
            TRACE(occupancy[events++] = {now(), size}; frames[i].wait = waited; frames[i].popped = now());
            lock.unlock(); changed.notify_one();
            TRACE(frames[i].begin = now());
            const auto pcm = mimi.decode_frame(latent, decoder);
            TRACE(frames[i].end = now());
            auto output = destination.subspan(std::size_t(i) * 1920, 1920);
            std::copy(pcm.begin(), pcm.end(), output.begin());
            const auto available = Clock::now();
            if (i == 0) { result.first_pcm_ms = elapsed(start, available); }
            TRACE(frames[i].pcm = elapsed(start, available));
            result.samples += 1920;
            if (sink) { sink(sink_context, i, output); }
        }
        result.mimi_offset = decoder.offset();
    }
#ifdef OUTLOUD_WORKER_TRACE
    double terminal_wait = 0;
#endif
};
WorkerPipeline::WorkerPipeline(PocketModelAsset model, OutLoudVoice voice, std::uint32_t tokens,
        std::uint32_t frames, cpu::KernelPath path)
    : impl_(std::make_unique<Impl>(model, voice, tokens, frames, path)) {}
WorkerPipeline::~WorkerPipeline() = default;
void WorkerPipeline::prepare(std::span<const std::uint32_t> tokens, NoiseSource& noise, std::span<float> destination,
        const GenerationOptions& options, PCMFrameSink sink, void* context, const GenerationObserver* observer) {
    auto& p = *impl_;
    const auto maximum = options.max_gen_len ? options.max_gen_len : FlowLMGenerator::estimate_max_gen_len(tokens.size());
    if (p.running || tokens.empty() || tokens.size() > p.maximum_tokens || !maximum || maximum > p.maximum_frames ||
        destination.size() < std::size_t(maximum) * 1920 || !std::isfinite(options.eos_threshold) ||
        !std::all_of(tokens.begin(), tokens.end(), [](auto t) { return t < 4001; })) {
        throw std::invalid_argument("Invalid worker request/capacity");
    }
    // Publish reset work and wait for each owner to reset its own neural state.
    // No reset or request validation is included in measured synthesis.
    std::unique_lock lock(p.mutex);
    p.tokens = tokens; p.noise = &noise; p.destination = destination;
    p.options = options; p.options.max_gen_len = maximum;
    p.sink = sink; p.sink_context = context; p.observer = observer;
    p.head = p.size = 0; p.completed = 0; p.producer_done = false; p.failure = nullptr;
    p.result = {}; p.producer_allocations = p.consumer_allocations = 0;
    p.valid_result = false; p.prepared = true;
    TRACE(std::fill(p.candidates.begin(), p.candidates.end(), Impl::CandidateTrace{});
          std::fill(p.frames.begin(), p.frames.end(), Impl::FrameTrace{});
          p.events = 0; p.terminal_wait = 0);
    p.reset_job = true; ++p.epoch;
    p.jobs.notify_all();
    p.finished.wait(lock, [&] { return p.completed == 2; });
    if (p.failure) { p.prepared = false; std::rethrow_exception(p.failure); }
}
WorkerResult WorkerPipeline::run() {
    auto& p = *impl_;
    if (!p.prepared || p.running) { throw std::logic_error("Worker request not prepared/already running"); }
    std::unique_lock lock(p.mutex);
    p.running = true; p.prepared = false;
    p.reset_job = false; p.completed = 0;
    p.start = Clock::now(); ++p.epoch;
    p.jobs.notify_all();
    p.finished.wait(lock, [&] { return p.completed == 2; });
    const auto end = Clock::now();
    p.running = false;
    if (p.failure) { std::rethrow_exception(p.failure); }
    p.result.total_ms = elapsed(p.start, end);
    p.result.hot_allocations = p.producer_allocations + p.consumer_allocations;
    lock.unlock();
    p.noise->finish_segment(); // fixture EOF check outside request compute timing
    p.valid_result = true;
    return p.result;
}
const FlowLMPrefillState& WorkerPipeline::flow_state() const {
    if (!impl_->valid_result) { throw std::logic_error("No completed worker result"); }
    return impl_->flow;
}
const MimiDecoderState& WorkerPipeline::mimi_state() const {
    if (!impl_->valid_result) { throw std::logic_error("No completed worker result"); }
    return impl_->decoder;
}
std::size_t WorkerPipeline::backbone_workspace_bytes() const { return impl_->backbone.workspace_storage_bytes(); }
std::size_t WorkerPipeline::mimi_scratch_bytes() const { return impl_->mimi.scratch_bytes(); }
std::size_t WorkerPipeline::object_storage_bytes() const { return sizeof(Impl); }
#ifdef OUTLOUD_WORKER_TRACE
void WorkerPipeline::write_trace(std::ostream& out) const {
    const auto& p = *impl_;
    if (!p.valid_result) { throw std::logic_error("No completed trace"); }
    out << std::setprecision(17) << "{\"prefill_start\":" << p.prefill_begin << ",\"prefill_end\":" << p.prefill_end
        << ",\"producer_completion\":" << p.producer_end << ",\"consumer_completion\":" << p.consumer_end
        << ",\"terminal_consumer_wait\":" << p.terminal_wait << ",\"candidates\":[";
    for (std::size_t i = 0; i < p.result.candidates; ++i) {
        const auto& c = p.candidates[i]; if (i) { out << ','; }
        out << "{\"start\":" << c.begin << ",\"end\":" << c.end << ",\"latent_ready\":" << c.ready
            << ",\"queue_push\":" << c.pushed << ",\"capacity_wait\":" << c.wait << '}';
    }
    out << "],\"frames\":[";
    for (std::size_t i = 0; i < p.result.emitted; ++i) {
        const auto& f = p.frames[i]; if (i) { out << ','; }
        out << "{\"queue_pop\":" << f.popped << ",\"start\":" << f.begin << ",\"end\":" << f.end << ",\"pcm_ready\":" << f.pcm
            << ",\"latent_wait\":" << f.wait << '}';
    }
    out << "],\"queue_occupancy\":[";
    for (std::size_t i = 0; i < p.events; ++i) {
        if (i) { out << ','; } out << "[" << p.occupancy[i].time << ',' << p.occupancy[i].occupancy << ']';
    }
    out << "]}";
}
#endif
} // namespace outloud
