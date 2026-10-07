#include "galaxy/native_dsp.h"

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#endif

#include <algorithm>
#include <bit>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <limits>

namespace galaxy {
namespace {

thread_local DspAramMirrorBoundary* g_dsp_aram_worker_boundary = nullptr;

void copy_words_checked(
    std::span<std::uint16_t> destination,
    std::uint16_t first_word,
    std::span<const std::uint16_t> source) {
    const auto begin = static_cast<std::size_t>(first_word);
    if (begin > destination.size() || source.size() > destination.size() - begin) {
        std::abort();
    }
    std::copy(source.begin(), source.end(), destination.begin() + begin);
}

void copy_bytes_checked(
    DspContext& context,
    bool instruction_memory,
    std::uint32_t first_byte,
    std::span<const std::byte> source) {
    const auto limit =
        (instruction_memory ? kDspIramWords : kDspDramWords) * 2u;
    if (first_byte > limit || source.size() > limit - first_byte) {
        std::abort();
    }
    for (std::size_t i = 0; i < source.size(); ++i) {
        dsp_dma_write_byte(
            context,
            instruction_memory,
            first_byte + static_cast<std::uint32_t>(i),
            std::to_integer<std::uint8_t>(source[i]));
    }
}

bool env_flag(const char* name, bool default_value) {
#if defined(_WIN32)
    char* value = nullptr;
    std::size_t length = 0;
    if (_dupenv_s(&value, &length, name) != 0 || value == nullptr) {
        return default_value;
    }
    const bool enabled = length > 1u && value[0] != '\0' &&
                         value[0] != '0' && value[0] != 'f' &&
                         value[0] != 'F' && value[0] != 'n' &&
                         value[0] != 'N';
    std::free(value);
    return enabled;
#else
    const char* const value = std::getenv(name);
    if (value == nullptr || value[0] == '\0') {
        return default_value;
    }
    return value[0] != '0' && value[0] != 'f' && value[0] != 'F' &&
           value[0] != 'n' && value[0] != 'N';
#endif
}

bool trace_native_dsp_worker() {
    // This launch-time switch is queried on every mailbox notification. Match
    // the host-side trace gate without taking the CRT environment lock again.
    static const bool enabled = env_flag("GALAXY_TRACE_DSP_HOST", false);
    return enabled;
}

void configure_native_dsp_worker_scheduling() {
#if defined(_WIN32)
    if (!env_flag("GALAXY_DSP_WORKER_HIGH_PRIORITY", true)) {
        return;
    }
    if (!SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_ABOVE_NORMAL) &&
        trace_native_dsp_worker()) {
        const std::lock_guard<std::mutex> trace_lock(g_dsp_trace_mutex);
        std::cout << "[dsp-native-worker] SetThreadPriority failed error="
                  << GetLastError() << '\n';
    }
#endif
}

}  // namespace

DspMramTransactionBoundary::DspMramTransactionBoundary(
    std::chrono::milliseconds timeout)
    : service_thread_(std::this_thread::get_id()), timeout_(timeout) {
    if (timeout_.count() <= 0) {
        std::abort();
    }
}

DspMramTransactionBoundary::~DspMramTransactionBoundary() {
    shutdown();
    const std::lock_guard lock(mutex_);
    if (submitter_active_) {
        // Destroying the latch while its submitter can still relock it is a
        // use-after-free. Owners must cancel, join, then destroy.
        std::abort();
    }
}

void DspMramTransactionBoundary::increment(
    std::uint64_t& counter) noexcept {
    if (counter == std::numeric_limits<std::uint64_t>::max()) {
        std::abort();
    }
    ++counter;
}

bool DspMramTransactionBoundary::configure_wake(
    WakeCallback callback,
    void* user) noexcept {
    const std::lock_guard lock(mutex_);
    if (submitter_active_ || state_ == State::Pending ||
        state_ == State::Completed) {
        return false;
    }
    wake_callback_ = callback;
    wake_user_ = user;
    return true;
}

bool DspMramTransactionBoundary::reset(
    std::chrono::milliseconds timeout) noexcept {
    const std::lock_guard lock(mutex_);
    if (timeout.count() <= 0 || wake_callback_ == nullptr || submitter_active_ ||
        state_ == State::Pending || state_ == State::Completed) {
        return false;
    }
    service_thread_ = std::this_thread::get_id();
    timeout_ = timeout;
    state_ = State::Idle;
    address_ = 0u;
    size_ = 0u;
    telemetry_.in_flight = 0u;
    telemetry_.failed = false;
    telemetry_.shutdown = false;
    return true;
}

bool DspMramTransactionBoundary::submit_read(
    std::uint32_t address,
    std::uint8_t* destination,
    std::uint32_t size) noexcept {
    return submit(
        Direction::ReadFromMram,
        address,
        destination,
        nullptr,
        size);
}

bool DspMramTransactionBoundary::submit_write(
    std::uint32_t address,
    const std::uint8_t* source,
    std::uint32_t size) noexcept {
    return submit(
        Direction::WriteToMram,
        address,
        nullptr,
        source,
        size);
}

bool DspMramTransactionBoundary::submit(
    Direction direction,
    std::uint32_t address,
    std::uint8_t* read_destination,
    const std::uint8_t* write_source,
    std::uint32_t size) noexcept {
    const bool pointer_valid =
        direction == Direction::ReadFromMram
        ? read_destination != nullptr
        : write_source != nullptr;
    if (!pointer_valid || size == 0u || size > kMaximumSpanBytes ||
        static_cast<std::uint64_t>(address) + size >
            (UINT64_C(1) << 32u)) {
        const std::lock_guard lock(mutex_);
        increment(telemetry_.rejected_requests);
        return false;
    }

    WakeCallback wake = nullptr;
    void* wake_user = nullptr;
    std::chrono::steady_clock::time_point deadline{};
    {
        std::lock_guard lock(mutex_);
        if (submitter_active_ || state_ != State::Idle) {
            increment(telemetry_.rejected_requests);
            return false;
        }
        if (wake_callback_ == nullptr) {
            increment(telemetry_.rejected_requests);
            telemetry_.failed = true;
            state_ = State::Failed;
            return false;
        }

        direction_ = direction;
        address_ = address;
        size_ = size;
        if (direction == Direction::WriteToMram) {
            std::memcpy(bytes_.data(), write_source, size);
            increment(telemetry_.write_requests);
        } else {
            increment(telemetry_.read_requests);
        }
        submitter_active_ = true;
        telemetry_.in_flight = 1u;
        telemetry_.maximum_in_flight =
            std::max(telemetry_.maximum_in_flight, 1u);
        state_ = State::Pending;

        // mutex unlock is the request publication release. service_one()
        // acquires the same mutex before observing metadata or payload bytes.
        wake = wake_callback_;
        wake_user = wake_user_;
        deadline = std::chrono::steady_clock::now() + timeout_;
    }

    const bool wake_published = wake(wake_user);
    std::unique_lock lock(mutex_);
    if (wake_published) {
        increment(telemetry_.wake_publications);
    } else {
        increment(telemetry_.wake_failures);
        increment(telemetry_.failed_services);
        telemetry_.failed = true;
        if (state_ != State::Shutdown) {
            state_ = State::Failed;
        }
        completion_cv_.notify_all();
    }

    if (state_ == State::Pending &&
        !completion_cv_.wait_until(
            lock,
            deadline,
            [&] { return state_ != State::Pending; })) {
        state_ = State::Failed;
        increment(telemetry_.timeout_failures);
        increment(telemetry_.failed_services);
        telemetry_.failed = true;
        completion_cv_.notify_all();
    }

    const bool completed = state_ == State::Completed;
    if (completed && direction == Direction::ReadFromMram) {
        std::memcpy(read_destination, bytes_.data(), size);
    }
    if (completed) {
        state_ = State::Idle;
    }
    submitter_active_ = false;
    telemetry_.in_flight = 0u;
    address_ = 0u;
    size_ = 0u;
    completion_cv_.notify_all();
    return completed;
}

DspMramTransactionBoundary::ServiceResult
DspMramTransactionBoundary::service_one(
    void* user,
    ReadService read,
    WriteService write) noexcept {
    std::unique_lock lock(mutex_);
    if (state_ != State::Pending) {
        return ServiceResult::None;
    }
    if (service_thread_ != std::this_thread::get_id()) {
        state_ = State::Failed;
        increment(telemetry_.failed_services);
        telemetry_.failed = true;
        completion_cv_.notify_all();
        return ServiceResult::WrongThread;
    }

    increment(telemetry_.service_calls);
    const bool accepted = direction_ == Direction::ReadFromMram
        ? read != nullptr && read(user, address_, bytes_.data(), size_)
        : write != nullptr && write(user, address_, bytes_.data(), size_);
    if (!accepted) {
        state_ = State::Failed;
        increment(telemetry_.failed_services);
        telemetry_.failed = true;
        completion_cv_.notify_all();
        return ServiceResult::Rejected;
    }

    state_ = State::Completed;
    increment(telemetry_.completed_services);
    completion_cv_.notify_all();
    return ServiceResult::Completed;
}

void DspMramTransactionBoundary::shutdown() noexcept {
    const std::lock_guard lock(mutex_);
    if (state_ == State::Shutdown) {
        return;
    }
    if (submitter_active_ &&
        (state_ == State::Pending || state_ == State::Completed)) {
        increment(telemetry_.shutdown_cancellations);
        increment(telemetry_.failed_services);
        telemetry_.failed = true;
    }
    state_ = State::Shutdown;
    telemetry_.shutdown = true;
    completion_cv_.notify_all();
}

bool DspMramTransactionBoundary::pending() const noexcept {
    const std::lock_guard lock(mutex_);
    return state_ == State::Pending;
}

DspMramTransactionBoundary::Snapshot
DspMramTransactionBoundary::snapshot() const noexcept {
    const std::lock_guard lock(mutex_);
    return telemetry_;
}

struct DspAramMirrorBoundary::Storage {
    struct InboundSlot {
        InboundSlot()
            : bytes(std::make_unique<std::uint8_t[]>(kMirrorSizeBytes)),
              page_indices(
                  std::make_unique<std::uint32_t[]>(kDirtyPageCount)) {}

        std::unique_ptr<std::uint8_t[]> bytes{};
        std::unique_ptr<std::uint32_t[]> page_indices{};
        std::uint64_t generation{};
        std::uint32_t page_count{};
        InboundKind kind{InboundKind::Seed};
        bool occupied{};
        bool applied{};
    };

    Storage()
        : mirror(std::make_unique<std::uint8_t[]>(kMirrorSizeBytes)),
          dirty_page_flags(
              std::make_unique<std::uint8_t[]>(kDirtyPageCount)),
          dirty_byte_words(std::make_unique<std::uint64_t[]>(
              static_cast<std::size_t>(kDirtyPageCount) * 2u)),
          dirty_page_indices(
              std::make_unique<std::uint32_t[]>(kDirtyPageCount)),
          flush_page_indices(
              std::make_unique<std::uint32_t[]>(kDirtyPageCount)) {}

    std::unique_ptr<std::uint8_t[]> mirror{};
    InboundSlot inbound{};
    // Two 64-bit words describe the exact dirty bytes of each 128-byte page.
    // Only the bound worker touches these arrays between reset/flush bounds.
    std::unique_ptr<std::uint8_t[]> dirty_page_flags{};
    std::unique_ptr<std::uint64_t[]> dirty_byte_words{};
    std::unique_ptr<std::uint32_t[]> dirty_page_indices{};
    std::unique_ptr<std::uint32_t[]> flush_page_indices{};

    // Worker-only hot counters are batch-published while already crossing a
    // mail/flush/detach boundary. Snapshotting never reads these fields and
    // therefore cannot contend with or race the accelerator hot path.
    std::uint64_t hot_read_spans{};
    std::uint64_t hot_read_bytes{};
    std::uint64_t hot_write_spans{};
    std::uint64_t hot_write_bytes{};
    std::uint64_t hot_word_writes{};
};

DspAramMirrorBoundary::DspAramMirrorBoundary()
    : storage_(std::make_unique<Storage>()) {}

DspAramMirrorBoundary::~DspAramMirrorBoundary() {
    shutdown();
    const std::lock_guard lock(mutex_);
    if (worker_thread_bound_ || mail_callback_active_ ||
        outbound_callback_active_) {
        // The mirror/callback payload must outlive the worker that owns it.
        // Owners cancel, join/detach, and only then destroy the boundary.
        std::abort();
    }
}

void DspAramMirrorBoundary::increment(std::uint64_t& counter) noexcept {
    if (counter == std::numeric_limits<std::uint64_t>::max()) {
        std::abort();
    }
    ++counter;
}

void DspAramMirrorBoundary::add(
    std::uint64_t& counter,
    std::uint64_t value) noexcept {
    if (value > std::numeric_limits<std::uint64_t>::max() - counter) {
        std::abort();
    }
    counter += value;
}

bool DspAramMirrorBoundary::span_is_valid(
    std::uint32_t address,
    const void* pointer,
    std::uint32_t size) noexcept {
    return pointer != nullptr && size != 0u && address < kMirrorSizeBytes &&
           size <= kMirrorSizeBytes - address;
}

void DspAramMirrorBoundary::fail_locked(Failure failure) noexcept {
    const auto state = state_.load(std::memory_order_relaxed);
    if (state == State::Failed || state == State::Cancelled ||
        state == State::Shutdown) {
        return;
    }
    failure_ = failure;
    state_.store(State::Failed, std::memory_order_release);
    increment(telemetry_.hard_failures);
    switch (failure) {
    case Failure::GenerationMismatch:
        increment(telemetry_.generation_failures);
        break;
    case Failure::QueueFull:
        increment(telemetry_.queue_full_failures);
        break;
    case Failure::WrongCpuThread:
    case Failure::WrongWorkerThread:
    case Failure::WorkerNotBound:
        increment(telemetry_.wrong_thread_failures);
        break;
    case Failure::InvalidArgument:
        increment(telemetry_.invalid_argument_failures);
        break;
    default:
        break;
    }
}

bool DspAramMirrorBoundary::fail_worker_hot(Failure failure) noexcept {
    const std::lock_guard lock(mutex_);
    if (state_.load(std::memory_order_relaxed) == State::Active) {
        fail_locked(failure);
    }
    return false;
}

void DspAramMirrorBoundary::publish_worker_telemetry_locked() noexcept {
    const bool any = storage_->hot_read_spans != 0u ||
                     storage_->hot_read_bytes != 0u ||
                     storage_->hot_write_spans != 0u ||
                     storage_->hot_write_bytes != 0u ||
                     storage_->hot_word_writes != 0u;
    add(telemetry_.worker_read_spans, storage_->hot_read_spans);
    add(telemetry_.worker_read_bytes, storage_->hot_read_bytes);
    add(telemetry_.worker_write_spans, storage_->hot_write_spans);
    add(telemetry_.worker_write_bytes, storage_->hot_write_bytes);
    add(telemetry_.worker_word_writes, storage_->hot_word_writes);
    storage_->hot_read_spans = 0u;
    storage_->hot_read_bytes = 0u;
    storage_->hot_write_spans = 0u;
    storage_->hot_write_bytes = 0u;
    storage_->hot_word_writes = 0u;
    if (any) {
        increment(telemetry_.worker_hot_telemetry_publications);
    }
}

bool DspAramMirrorBoundary::cpu_thread_is_current_locked() noexcept {
    if (!cpu_thread_bound_ || cpu_thread_ != std::this_thread::get_id()) {
        fail_locked(Failure::WrongCpuThread);
        return false;
    }
    return true;
}

bool DspAramMirrorBoundary::worker_thread_is_current_locked() noexcept {
    if (!worker_thread_bound_) {
        fail_locked(Failure::WorkerNotBound);
        return false;
    }
    if (worker_thread_ != std::this_thread::get_id() ||
        g_dsp_aram_worker_boundary != this) {
        fail_locked(Failure::WrongWorkerThread);
        return false;
    }
    return true;
}

void DspAramMirrorBoundary::clear_inbound_locked() noexcept {
    storage_->inbound.generation = 0u;
    storage_->inbound.page_count = 0u;
    storage_->inbound.kind = InboundKind::Seed;
    storage_->inbound.occupied = false;
    storage_->inbound.applied = false;
    inbound_depth_ = 0u;
}

void DspAramMirrorBoundary::clear_outbound_full_locked() noexcept {
    std::fill_n(
        storage_->dirty_page_flags.get(), kDirtyPageCount, std::uint8_t{0});
    std::fill_n(
        storage_->dirty_byte_words.get(),
        static_cast<std::size_t>(kDirtyPageCount) * 2u,
        UINT64_C(0));
    outbound_dirty_page_count_ = 0u;
    published_outbound_dirty_page_count_.store(
        0u, std::memory_order_release);
}

void DspAramMirrorBoundary::clear_outbound_sparse_locked() noexcept {
    for (std::uint32_t index = 0u;
         index < outbound_dirty_page_count_;
         ++index) {
        const std::uint32_t page = storage_->dirty_page_indices[index];
        if (page >= kDirtyPageCount) {
            std::abort();
        }
        storage_->dirty_page_flags[page] = 0u;
        storage_->dirty_byte_words[
            static_cast<std::size_t>(page) * 2u] = 0u;
        storage_->dirty_byte_words[
            static_cast<std::size_t>(page) * 2u + 1u] = 0u;
    }
    outbound_dirty_page_count_ = 0u;
    published_outbound_dirty_page_count_.store(
        0u, std::memory_order_release);
}

bool DspAramMirrorBoundary::reset() noexcept {
    const std::lock_guard lock(mutex_);
    if (worker_thread_bound_ || mail_callback_active_ ||
        outbound_callback_active_) {
        fail_locked(Failure::ResetWhileWorkerBound);
        return false;
    }
    if (cpu_thread_bound_ && cpu_thread_ != std::this_thread::get_id()) {
        fail_locked(Failure::WrongCpuThread);
        return false;
    }
    if (!cpu_thread_bound_) {
        cpu_thread_ = std::this_thread::get_id();
        cpu_thread_bound_ = true;
    }

    std::memset(storage_->mirror.get(), 0, kMirrorSizeBytes);
    clear_inbound_locked();
    clear_outbound_full_locked();
    storage_->hot_read_spans = 0u;
    storage_->hot_read_bytes = 0u;
    storage_->hot_write_spans = 0u;
    storage_->hot_write_bytes = 0u;
    storage_->hot_word_writes = 0u;
    seed_staged_ = false;
    worker_seeded_ = false;
    last_cpu_staged_generation_ = 0u;
    last_cpu_consumed_generation_ = 0u;
    last_outbound_generation_ = 0u;
    failure_ = Failure::None;
    state_.store(State::Active, std::memory_order_release);
    increment(telemetry_.resets);
    return true;
}

void DspAramMirrorBoundary::cancel() noexcept {
    const std::lock_guard lock(mutex_);
    if (state_.load(std::memory_order_relaxed) == State::Shutdown ||
        state_.load(std::memory_order_relaxed) == State::Cancelled) {
        return;
    }
    if (cpu_thread_bound_ && cpu_thread_ != std::this_thread::get_id()) {
        fail_locked(Failure::WrongCpuThread);
        return;
    }
    add(telemetry_.cancelled_inbound_packets, inbound_depth_);
    add(
        telemetry_.cancelled_outbound_pages,
        published_outbound_dirty_page_count_.load(std::memory_order_acquire));
    clear_inbound_locked();
    if (!worker_thread_bound_) {
        clear_outbound_sparse_locked();
    }
    state_.store(State::Cancelled, std::memory_order_release);
    increment(telemetry_.cancellations);
}

void DspAramMirrorBoundary::shutdown() noexcept {
    const std::lock_guard lock(mutex_);
    const auto prior_state = state_.load(std::memory_order_relaxed);
    if (prior_state == State::Shutdown) {
        return;
    }
    if (prior_state != State::Cancelled) {
        add(telemetry_.cancelled_inbound_packets, inbound_depth_);
        add(
            telemetry_.cancelled_outbound_pages,
            published_outbound_dirty_page_count_.load(
                std::memory_order_acquire));
    }
    clear_inbound_locked();
    if (!worker_thread_bound_) {
        clear_outbound_sparse_locked();
    }
    state_.store(State::Shutdown, std::memory_order_release);
    increment(telemetry_.shutdowns);
}

bool DspAramMirrorBoundary::worker_bind() noexcept {
    const std::lock_guard lock(mutex_);
    if (state_.load(std::memory_order_relaxed) != State::Active) {
        return false;
    }
    const auto current = std::this_thread::get_id();
    if ((cpu_thread_bound_ && cpu_thread_ == current) ||
        (g_dsp_aram_worker_boundary != nullptr &&
         g_dsp_aram_worker_boundary != this)) {
        fail_locked(Failure::WrongWorkerThread);
        return false;
    }
    if (worker_thread_bound_) {
        if (worker_thread_ == current &&
            g_dsp_aram_worker_boundary == this) {
            return true;
        }
        fail_locked(Failure::WrongWorkerThread);
        return false;
    }
    worker_thread_ = current;
    worker_thread_bound_ = true;
    g_dsp_aram_worker_boundary = this;
    return true;
}

bool DspAramMirrorBoundary::worker_detach() noexcept {
    const std::lock_guard lock(mutex_);
    if (!worker_thread_bound_ || worker_thread_ != std::this_thread::get_id() ||
        g_dsp_aram_worker_boundary != this) {
        if (state_.load(std::memory_order_relaxed) == State::Active) {
            fail_locked(Failure::WrongWorkerThread);
        }
        return false;
    }
    if (mail_callback_active_ || outbound_callback_active_) {
        fail_locked(Failure::ReentrantOperation);
        return false;
    }

    bool clean = true;
    if (state_.load(std::memory_order_relaxed) == State::Active &&
        inbound_depth_ != 0u) {
        fail_locked(Failure::PendingInbound);
        clean = false;
    }
    if (state_.load(std::memory_order_relaxed) == State::Active &&
        outbound_dirty_page_count_ != 0u) {
        fail_locked(Failure::UnflushedOutbound);
        clean = false;
    }
    publish_worker_telemetry_locked();
    worker_thread_ = {};
    worker_thread_bound_ = false;
    g_dsp_aram_worker_boundary = nullptr;
    return clean;
}

bool DspAramMirrorBoundary::stage_packet_locked(
    InboundKind kind,
    std::uint64_t generation) noexcept {
    if (generation == 0u || generation <= last_cpu_staged_generation_) {
        fail_locked(Failure::GenerationMismatch);
        return false;
    }
    if (inbound_depth_ >= kInboundQueueCapacity ||
        storage_->inbound.occupied) {
        fail_locked(Failure::QueueFull);
        return false;
    }
    storage_->inbound.kind = kind;
    storage_->inbound.generation = generation;
    storage_->inbound.page_count = 0u;
    storage_->inbound.occupied = true;
    storage_->inbound.applied = false;
    inbound_depth_ = 1u;
    last_cpu_staged_generation_ = generation;
    telemetry_.maximum_inbound_depth =
        std::max(telemetry_.maximum_inbound_depth, inbound_depth_);
    return true;
}

bool DspAramMirrorBoundary::cpu_stage_seed(
    std::uint64_t generation,
    std::span<const std::uint8_t> bytes) noexcept {
    const std::lock_guard lock(mutex_);
    if (state_.load(std::memory_order_relaxed) != State::Active) {
        return false;
    }
    if (!cpu_thread_is_current_locked()) {
        return false;
    }
    if (bytes.size() != kMirrorSizeBytes || bytes.data() == nullptr) {
        fail_locked(Failure::InvalidArgument);
        return false;
    }
    if (seed_staged_) {
        fail_locked(Failure::DuplicateSeed);
        return false;
    }
    if (!stage_packet_locked(InboundKind::Seed, generation)) {
        return false;
    }
    std::memcpy(
        storage_->inbound.bytes.get(), bytes.data(), kMirrorSizeBytes);
    seed_staged_ = true;
    increment(telemetry_.seed_packets_staged);
    return true;
}

bool DspAramMirrorBoundary::cpu_stage_dirty_pages(
    std::uint64_t generation,
    std::span<const DirtyPageUpdate> pages) noexcept {
    const std::lock_guard lock(mutex_);
    if (state_.load(std::memory_order_relaxed) != State::Active) {
        return false;
    }
    if (!cpu_thread_is_current_locked()) {
        return false;
    }
    if (!seed_staged_) {
        fail_locked(Failure::SeedRequired);
        return false;
    }
    if (pages.size() > kDirtyPageCount) {
        fail_locked(Failure::InvalidArgument);
        return false;
    }
    std::uint32_t previous_page = 0u;
    bool have_previous = false;
    for (const auto& page : pages) {
        if (page.page_index >= kDirtyPageCount ||
            page.bytes.size() != kDirtyPageBytes ||
            page.bytes.data() == nullptr ||
            (have_previous && page.page_index <= previous_page)) {
            fail_locked(Failure::InvalidArgument);
            return false;
        }
        previous_page = page.page_index;
        have_previous = true;
    }
    if (!stage_packet_locked(InboundKind::DirtyPages, generation)) {
        return false;
    }
    for (std::size_t index = 0; index < pages.size(); ++index) {
        const auto& page = pages[index];
        storage_->inbound.page_indices[index] = page.page_index;
        std::memcpy(
            storage_->inbound.bytes.get() +
                static_cast<std::size_t>(page.page_index) * kDirtyPageBytes,
            page.bytes.data(),
            kDirtyPageBytes);
    }
    storage_->inbound.page_count = static_cast<std::uint32_t>(pages.size());
    increment(telemetry_.dirty_packets_staged);
    add(telemetry_.dirty_pages_staged, pages.size());
    return true;
}

bool DspAramMirrorBoundary::worker_apply_before_mail_consume(
    std::uint64_t generation,
    MailConsumeCallback consume,
    void* user) noexcept {
    std::unique_lock lock(mutex_);
    if (state_.load(std::memory_order_relaxed) != State::Active) {
        return false;
    }
    if (!worker_thread_is_current_locked()) {
        return false;
    }
    if (consume == nullptr) {
        fail_locked(Failure::InvalidArgument);
        return false;
    }
    if (mail_callback_active_ || outbound_callback_active_) {
        fail_locked(Failure::ReentrantOperation);
        return false;
    }
    if (inbound_depth_ != 1u || !storage_->inbound.occupied ||
        storage_->inbound.applied ||
        storage_->inbound.generation != generation ||
        generation <= last_cpu_consumed_generation_) {
        fail_locked(Failure::GenerationMismatch);
        return false;
    }
    if (outbound_dirty_page_count_ != 0u) {
        fail_locked(Failure::UnflushedOutbound);
        return false;
    }

    publish_worker_telemetry_locked();
    if (storage_->inbound.kind == InboundKind::Seed) {
        if (worker_seeded_) {
            fail_locked(Failure::DuplicateSeed);
            return false;
        }
        std::memcpy(
            storage_->mirror.get(),
            storage_->inbound.bytes.get(),
            kMirrorSizeBytes);
        worker_seeded_ = true;
        increment(telemetry_.seed_packets_applied);
    } else {
        if (!worker_seeded_) {
            fail_locked(Failure::SeedRequired);
            return false;
        }
        for (std::uint32_t index = 0u;
             index < storage_->inbound.page_count;
             ++index) {
            const auto page = storage_->inbound.page_indices[index];
            const auto offset =
                static_cast<std::size_t>(page) * kDirtyPageBytes;
            std::memcpy(
                storage_->mirror.get() + offset,
                storage_->inbound.bytes.get() + offset,
                kDirtyPageBytes);
        }
        increment(telemetry_.dirty_packets_applied);
        add(
            telemetry_.dirty_pages_applied,
            storage_->inbound.page_count);
    }
    storage_->inbound.applied = true;
    mail_callback_active_ = true;

    // The callback is the actual hardware CMBL consume. Keep the boundary
    // mutex held through that one atomic mailbox read and the immediately
    // following inbound-slot retirement. The busy bit becomes CPU-visible as
    // cleared inside the callback; unlocking before clear_inbound_locked()
    // allowed the CPU to observe an empty hardware mailbox and race the next
    // generation into a still-occupied mirror slot. Valid worker_read_* probes
    // are deliberately lock-free, so the callback can still verify the fully
    // installed mirror without re-entering this mutex.
    const bool consumed = consume(user, generation);
    mail_callback_active_ = false;
    increment(telemetry_.mail_consume_callbacks);

    if (!consumed) {
        increment(telemetry_.mail_consume_rejections);
        if (state_.load(std::memory_order_relaxed) == State::Active) {
            fail_locked(Failure::MailConsumeRejected);
        }
        return false;
    }
    if (state_.load(std::memory_order_relaxed) != State::Active) {
        return false;
    }
    if (!storage_->inbound.occupied || !storage_->inbound.applied ||
        storage_->inbound.generation != generation) {
        fail_locked(Failure::ReentrantOperation);
        return false;
    }
    last_cpu_consumed_generation_ = generation;
    clear_inbound_locked();
    return true;
}

bool DspAramMirrorBoundary::worker_read_span(
    std::uint32_t address,
    std::uint8_t* destination,
    std::uint32_t size) noexcept {
    if (g_dsp_aram_worker_boundary != this) {
        return fail_worker_hot(Failure::WorkerNotBound);
    }
    if (state_.load(std::memory_order_acquire) != State::Active) {
        return false;
    }
    if (!worker_seeded_) {
        return fail_worker_hot(Failure::SeedRequired);
    }
    if (!span_is_valid(address, destination, size)) {
        return fail_worker_hot(Failure::InvalidArgument);
    }
    std::memcpy(destination, storage_->mirror.get() + address, size);
    increment(storage_->hot_read_spans);
    add(storage_->hot_read_bytes, size);
    return state_.load(std::memory_order_acquire) == State::Active;
}

bool DspAramMirrorBoundary::worker_read_u16_be(
    std::uint32_t address,
    std::uint16_t* value) noexcept {
    if (g_dsp_aram_worker_boundary != this) {
        return fail_worker_hot(Failure::WorkerNotBound);
    }
    if (state_.load(std::memory_order_acquire) != State::Active) {
        return false;
    }
    if (!worker_seeded_) {
        return fail_worker_hot(Failure::SeedRequired);
    }
    if (!span_is_valid(address, value, 2u)) {
        return fail_worker_hot(Failure::InvalidArgument);
    }
    const auto* const source = storage_->mirror.get() + address;
    *value = static_cast<std::uint16_t>(
        (static_cast<std::uint16_t>(source[0]) << 8u) | source[1]);
    increment(storage_->hot_read_spans);
    add(storage_->hot_read_bytes, 2u);
    return state_.load(std::memory_order_acquire) == State::Active;
}

void DspAramMirrorBoundary::mark_outbound_dirty_page_worker(
    std::uint32_t page) noexcept {
    if (storage_->dirty_page_flags[page] == 0u) {
        if (outbound_dirty_page_count_ == kDirtyPageCount) {
            std::abort();
        }
        storage_->dirty_page_flags[page] = 1u;
        storage_->dirty_page_indices[outbound_dirty_page_count_++] = page;
    }
}

void DspAramMirrorBoundary::mark_outbound_dirty_worker(
    std::uint32_t address,
    std::uint32_t size) noexcept {
    // PCM word writes fit this path. If the entire checked span fits in
    // one bitmap word, it also fits in one 128-byte page: no splitting or
    // range clamping is needed. size==64 is valid only at bit zero, so the
    // mask shift always lies in [0,63] and never shifts by the word width.
    static_assert(kDirtyPageBytes == 128u);
    const std::uint32_t bit = address & 63u;
    if (size != 0u && size <= 64u - bit) {
        mark_outbound_dirty_page_worker(address / kDirtyPageBytes);
        storage_->dirty_byte_words[address / 64u] |=
            (~UINT64_C(0) >> (64u - size)) << bit;
        published_outbound_dirty_page_count_.store(
            outbound_dirty_page_count_, std::memory_order_release);
        return;
    }
    const std::uint32_t end = address + size;
    std::uint32_t cursor = address;
    while (cursor < end) {
        const std::uint32_t page = cursor / kDirtyPageBytes;
        const std::uint32_t page_base = page * kDirtyPageBytes;
        const std::uint32_t page_end = page_base + kDirtyPageBytes;
        const std::uint32_t chunk_end = std::min(end, page_end);
        mark_outbound_dirty_page_worker(page);

        const std::uint32_t first_bit = cursor - page_base;
        const std::uint32_t last_bit = chunk_end - page_base;
        const std::uint32_t first_word = first_bit / 64u;
        const std::uint32_t last_word = (last_bit - 1u) / 64u;
        for (std::uint32_t word = first_word; word <= last_word; ++word) {
            const std::uint32_t word_first = word * 64u;
            const std::uint32_t begin = std::max(first_bit, word_first) - word_first;
            const std::uint32_t finish = std::min(last_bit, word_first + 64u) - word_first;
            const std::uint64_t low_mask = ~UINT64_C(0) << begin;
            const std::uint64_t high_mask = finish == 64u
                ? ~UINT64_C(0)
                : (UINT64_C(1) << finish) - 1u;
            storage_->dirty_byte_words[
                static_cast<std::size_t>(page) * 2u + word] |=
                low_mask & high_mask;
        }
        cursor = chunk_end;
    }
    published_outbound_dirty_page_count_.store(
        outbound_dirty_page_count_, std::memory_order_release);
}

bool DspAramMirrorBoundary::worker_write_span(
    std::uint32_t address,
    const std::uint8_t* source,
    std::uint32_t size) noexcept {
    if (g_dsp_aram_worker_boundary != this) {
        return fail_worker_hot(Failure::WorkerNotBound);
    }
    if (state_.load(std::memory_order_acquire) != State::Active) {
        return false;
    }
    if (!worker_seeded_) {
        return fail_worker_hot(Failure::SeedRequired);
    }
    if (mail_callback_active_ || outbound_callback_active_) {
        return fail_worker_hot(Failure::ReentrantOperation);
    }
    if (!span_is_valid(address, source, size)) {
        return fail_worker_hot(Failure::InvalidArgument);
    }
    std::memcpy(storage_->mirror.get() + address, source, size);
    // Mirror bytes are worker-exclusive. Dirty publication follows the entire
    // memcpy, so no callback can observe a partly marked logical write.
    mark_outbound_dirty_worker(address, size);
    increment(storage_->hot_write_spans);
    add(storage_->hot_write_bytes, size);
    return state_.load(std::memory_order_acquire) == State::Active;
}

bool DspAramMirrorBoundary::worker_write_u16_be(
    std::uint32_t address,
    std::uint16_t value) noexcept {
    if (g_dsp_aram_worker_boundary != this) {
        return fail_worker_hot(Failure::WorkerNotBound);
    }
    if (state_.load(std::memory_order_acquire) != State::Active) {
        return false;
    }
    if (!worker_seeded_) {
        return fail_worker_hot(Failure::SeedRequired);
    }
    if (mail_callback_active_ || outbound_callback_active_) {
        return fail_worker_hot(Failure::ReentrantOperation);
    }
    if (!span_is_valid(address, storage_->mirror.get(), 2u)) {
        return fail_worker_hot(Failure::InvalidArgument);
    }
    auto* const destination = storage_->mirror.get() + address;
    destination[0] = static_cast<std::uint8_t>(value >> 8u);
    destination[1] = static_cast<std::uint8_t>(value);
    // Only this worker can access the installed mirror or dirty bits. Both
    // bytes become a single logical API operation before either exact dirty
    // bit is published, including when the word crosses a 128-byte page.
    mark_outbound_dirty_worker(address, 2u);
    increment(storage_->hot_write_spans);
    add(storage_->hot_write_bytes, 2u);
    increment(storage_->hot_word_writes);
    return state_.load(std::memory_order_acquire) == State::Active;
}

bool DspAramMirrorBoundary::worker_flush_outbound(
    std::uint64_t generation,
    OutboundSpanCallback commit,
    void* user) noexcept {
    std::unique_lock lock(mutex_);
    if (state_.load(std::memory_order_relaxed) != State::Active) {
        return false;
    }
    if (!worker_thread_is_current_locked()) {
        return false;
    }
    if (!worker_seeded_) {
        fail_locked(Failure::SeedRequired);
        return false;
    }
    if (commit == nullptr) {
        fail_locked(Failure::InvalidArgument);
        return false;
    }
    if (mail_callback_active_ || outbound_callback_active_) {
        fail_locked(Failure::ReentrantOperation);
        return false;
    }
    if (generation == 0u || generation <= last_outbound_generation_) {
        fail_locked(Failure::GenerationMismatch);
        return false;
    }

    publish_worker_telemetry_locked();
    if (outbound_dirty_page_count_ == 0u) {
        last_outbound_generation_ = generation;
        increment(telemetry_.outbound_flushes);
        return true;
    }

    std::copy_n(
        storage_->dirty_page_indices.get(),
        outbound_dirty_page_count_,
        storage_->flush_page_indices.get());
    std::sort(
        storage_->flush_page_indices.get(),
        storage_->flush_page_indices.get() + outbound_dirty_page_count_);
    outbound_callback_active_ = true;

    std::uint64_t committed_spans = 0u;
    std::uint64_t committed_bytes = 0u;
    std::uint64_t attempted_spans = 0u;
    std::uint64_t attempted_bytes = 0u;
    bool have_span = false;
    std::uint32_t span_begin = 0u;
    std::uint32_t span_end = 0u;
    bool accepted = true;

    const auto commit_span = [&]() noexcept {
        if (!have_span || !accepted) {
            return;
        }
        const std::uint32_t size = span_end - span_begin;
        ++attempted_spans;
        attempted_bytes += size;
        lock.unlock();
        const bool callback_result = commit(
            user,
            generation,
            span_begin,
            storage_->mirror.get() + span_begin,
            size);
        lock.lock();
        if (!callback_result ||
            state_.load(std::memory_order_relaxed) != State::Active) {
            accepted = false;
            return;
        }
        ++committed_spans;
        committed_bytes += size;
        have_span = false;
    };

    for (std::uint32_t list_index = 0u;
         list_index < outbound_dirty_page_count_ && accepted;
         ++list_index) {
        const std::uint32_t page = storage_->flush_page_indices[list_index];
        const std::uint32_t page_base = page * kDirtyPageBytes;
        // Enumerate exact contiguous runs in the worker-owned byte mask.
        // This preserves ascending callback spans, including runs crossing
        // word/page boundaries, without testing every clean byte in a page.
        static_assert(kDirtyPageBytes == 128u);
        for (unsigned word = 0u; word < 2u && accepted; ++word) {
            std::uint64_t bits = storage_->dirty_byte_words[
                static_cast<std::size_t>(page) * 2u + word];
            while (bits != 0u && accepted) {
                const unsigned first = std::countr_zero(bits);
                const unsigned length = std::countr_one(bits >> first);
                const std::uint32_t address = page_base + word * 64u + first;
                if (have_span && span_end != address) {
                    commit_span();
                }
                if (!accepted) break;
                if (!have_span) {
                    span_begin = address;
                    have_span = true;
                }
                span_end = address + length;
                const unsigned next = first + length;
                // Guard the full-word case: shifting by 64 is undefined.
                bits = next == 64u ? 0u : bits & (~UINT64_C(0) << next);
            }
        }
    }
    if (accepted && have_span) {
        commit_span();
    }

    outbound_callback_active_ = false;
    add(telemetry_.outbound_span_attempts, attempted_spans);
    add(telemetry_.outbound_span_callbacks, committed_spans);
    add(telemetry_.outbound_bytes, committed_bytes);
    telemetry_.maximum_outbound_spans_per_flush = std::max(
        telemetry_.maximum_outbound_spans_per_flush,
        attempted_spans);
    telemetry_.maximum_outbound_bytes_per_flush = std::max(
        telemetry_.maximum_outbound_bytes_per_flush,
        attempted_bytes);
    if (!accepted) {
        increment(telemetry_.outbound_span_rejections);
        if (state_.load(std::memory_order_relaxed) == State::Active) {
            fail_locked(Failure::OutboundSpanRejected);
        }
        return false;
    }
    clear_outbound_sparse_locked();
    last_outbound_generation_ = generation;
    increment(telemetry_.outbound_flushes);
    return true;
}

DspAramMirrorBoundary::Snapshot
DspAramMirrorBoundary::snapshot() const noexcept {
    const std::lock_guard lock(mutex_);
    Snapshot result = telemetry_;
    result.inbound_depth = inbound_depth_;
    result.outbound_dirty_pages =
        published_outbound_dirty_page_count_.load(std::memory_order_acquire);
    result.last_cpu_staged_generation = last_cpu_staged_generation_;
    result.last_cpu_consumed_generation = last_cpu_consumed_generation_;
    result.last_outbound_generation = last_outbound_generation_;
    result.failure = failure_;
    result.worker_bound = worker_thread_bound_;
    result.seeded = worker_seeded_;
    const auto state = state_.load(std::memory_order_acquire);
    result.failed = failure_ != Failure::None || state == State::Failed;
    result.cancelled = state == State::Cancelled;
    result.shutdown = state == State::Shutdown;
    return result;
}

NativeDspCoprocessor::NativeDspCoprocessor() {
    attach_bridge();
}

void NativeDspCoprocessor::require_no_active_worker() const noexcept {
    if (worker_owns_context_.load(std::memory_order_acquire)) {
        std::abort();
    }
}

DspContext& NativeDspCoprocessor::context() noexcept {
    require_no_active_worker();
    return context_;
}

const DspContext& NativeDspCoprocessor::context() const noexcept {
    require_no_active_worker();
    return context_;
}

void NativeDspCoprocessor::claim_context_for_worker() {
    bool expected = false;
    if (!worker_owns_context_.compare_exchange_strong(
            expected,
            true,
            std::memory_order_acq_rel,
            std::memory_order_acquire)) {
        std::abort();
    }
}

void NativeDspCoprocessor::release_context_from_worker() {
    if (!worker_owns_context_.exchange(false, std::memory_order_acq_rel)) {
        std::abort();
    }
}

void NativeDspCoprocessor::set_diagnostic_capture_enabled(bool enabled) {
    require_no_active_worker();
    {
        const std::lock_guard lock(diagnostic_state_.mutex);
        diagnostic_state_.latest = DspDiagnosticSnapshot{};
        diagnostic_state_.publication_generation = 0u;
        diagnostic_state_.dsp_mail_generation = 0u;
        diagnostic_state_.dirq_generation = 0u;
        diagnostic_state_.return_generation = 0u;
        diagnostic_state_.valid = false;
    }
    diagnostic_capture_enabled_ = enabled;
    attach_bridge();
}

std::optional<DspDiagnosticSnapshot>
NativeDspCoprocessor::latest_diagnostic_snapshot() const {
    const std::lock_guard lock(diagnostic_state_.mutex);
    if (!diagnostic_state_.valid) {
        return std::nullopt;
    }
    return diagnostic_state_.latest;
}

void NativeDspCoprocessor::set_audio_causal_capture_enabled(bool enabled) {
    require_no_active_worker();
    if (enabled) {
        audio_causal_recorder_ =
            std::make_unique<DspAudioCausalRecorder>();
    } else {
        audio_causal_recorder_.reset();
    }
    audio_causal_capture_enabled_ = enabled;
    attach_bridge();
}

DspAudioCausalSnapshot NativeDspCoprocessor::audio_causal_snapshot() const {
    require_no_active_worker();
    auto snapshot = audio_causal_recorder_ != nullptr
        ? galaxy::dsp_audio_causal_snapshot(*audio_causal_recorder_)
        : DspAudioCausalSnapshot{};
    snapshot.quiescent_mixer_level = context_.dram[0x038Eu];
    snapshot.quiescent_mixer_level_valid = true;
    return snapshot;
}

void NativeDspCoprocessor::set_channel_selection_dma_probe_enabled(
    bool enabled) {
    require_no_active_worker();
    if (enabled) {
        owned_channel_selection_dma_probe_ =
            std::make_unique<DspChannelSelectionDmaProbeRecorder>();
        dsp_channel_selection_dma_probe_begin_session(
            *owned_channel_selection_dma_probe_);
        channel_selection_dma_probe_ =
            owned_channel_selection_dma_probe_.get();
    } else {
        owned_channel_selection_dma_probe_.reset();
        channel_selection_dma_probe_ = nullptr;
    }
    channel_selection_dma_probe_enabled_ = enabled;
    attach_bridge();
}

void NativeDspCoprocessor::attach_channel_selection_dma_probe(
    DspChannelSelectionDmaProbeRecorder* recorder) {
    require_no_active_worker();
    owned_channel_selection_dma_probe_.reset();
    channel_selection_dma_probe_ = recorder;
    channel_selection_dma_probe_enabled_ = recorder != nullptr;
    attach_bridge();
}

void NativeDspCoprocessor::attach_audio_bus_controls(
    const audio::NativeAudioBusControls* controls) {
    require_no_active_worker();
    audio_bus_controls_ = controls;
    attach_bridge();
}

DspChannelSelectionDmaProbeSnapshot
NativeDspCoprocessor::channel_selection_dma_probe_snapshot() const {
    require_no_active_worker();
    return channel_selection_dma_probe_ != nullptr
        ? dsp_channel_selection_dma_probe_snapshot(
              *channel_selection_dma_probe_)
        : DspChannelSelectionDmaProbeSnapshot{};
}

void NativeDspCoprocessor::publish_worker_diagnostic(
    DspDiagnosticBoundary boundary) {
    dsp_publish_diagnostic_snapshot(context_, boundary);
}

DspDiagnosticSnapshot
NativeDspCoprocessor::capture_quiescent_worker_snapshot(
    std::uint64_t completed_runs) const {
    std::uint64_t publication_generation = 0u;
    std::uint64_t dsp_mail_generation = 0u;
    std::uint64_t dirq_generation = 0u;
    std::uint64_t return_generation = 0u;
    {
        const std::lock_guard lock(diagnostic_state_.mutex);
        publication_generation = diagnostic_state_.publication_generation;
        dsp_mail_generation = diagnostic_state_.dsp_mail_generation;
        dirq_generation = diagnostic_state_.dirq_generation;
        return_generation = diagnostic_state_.return_generation;
    }
    auto snapshot = dsp_capture_diagnostic_snapshot(
        context_,
        DspDiagnosticBoundary::QuiescentWorkerQuery,
        publication_generation,
        dsp_mail_generation,
        dirq_generation,
        return_generation);
    snapshot.worker_completed_runs = completed_runs;
    return snapshot;
}

void NativeDspCoprocessor::attach_bridge() {
    dsp_attach_guest_memory_bridge(context_, bridge_);
    // host_abort lives in this coprocessor, not the value-copied context, so it
    // must be re-pointed every time the context is rebuilt (reset()).
    context_.host_abort = &abort_flag_;
    context_.host_cpu_mail_generation = &cpu_mail_generation_;
    context_.host_external_interrupt = &external_interrupt_vector_;
    context_.host_wake = &host_wake_state_;
    context_.host_telemetry = &telemetry_state_;
    context_.audio_causal_recorder =
        audio_causal_capture_enabled_ ? audio_causal_recorder_.get() : nullptr;
    context_.channel_selection_dma_probe =
        channel_selection_dma_probe_enabled_
        ? channel_selection_dma_probe_
        : nullptr;
    context_.audio_bus_controls = audio_bus_controls_;
    context_.pending_idle_backedges = &pending_idle_backedges_;
    context_.host_diagnostics =
        diagnostic_capture_enabled_ ? &diagnostic_state_ : nullptr;
    context_.last_retired_pc.pending_entries =
        &pending_instruction_entries_;
    context_.last_retired_pc.idle_sequence_tracker =
        &idle_sequence_tracker_;
}

void NativeDspCoprocessor::reset() {
    require_no_active_worker();
    dsp_native_telemetry_publish_pending_worker_counts(context_);
    pending_instruction_entries_ = 0u;
    pending_idle_backedges_ = {};
    idle_sequence_tracker_ = {};
    if (audio_causal_recorder_ != nullptr) {
        *audio_causal_recorder_ = {};
    }
    if (owned_channel_selection_dma_probe_ != nullptr) {
        *owned_channel_selection_dma_probe_ = {};
        dsp_channel_selection_dma_probe_begin_session(
            *owned_channel_selection_dma_probe_);
    } else if (channel_selection_dma_probe_ != nullptr) {
        // An externally owned recorder carries process-lifetime totals, but a
        // coprocessor reset is still a hard causal boundary.  Close the old
        // publication/branch window and begin a distinct worker session so a
        // post-reset DMA can never inherit pre-reset identity.
        dsp_channel_selection_dma_probe_record_reset_boundary(
            *channel_selection_dma_probe_);
        dsp_channel_selection_dma_probe_begin_session(
            *channel_selection_dma_probe_);
    }
    cpu_mail_generation_.store(0u, std::memory_order_release);
    external_interrupt_vector_.store(
        kDspNoPendingExternalInterrupt,
        std::memory_order_release);
    clear_hard_trap();
    {
        const std::lock_guard lock(diagnostic_state_.mutex);
        diagnostic_state_.latest = DspDiagnosticSnapshot{};
        diagnostic_state_.publication_generation = 0u;
        diagnostic_state_.dsp_mail_generation = 0u;
        diagnostic_state_.dirq_generation = 0u;
        diagnostic_state_.return_generation = 0u;
        diagnostic_state_.valid = false;
    }
    context_ = DspContext{};
    control_ = 0;
    attach_bridge();
}

void NativeDspCoprocessor::connect_guest_memory(
    GuestMemoryV1* memory,
    const NativeServicesV1* services,
    std::uint32_t guest_pc) {
    require_no_active_worker();
    bridge_.memory = memory;
    bridge_.services = services;
    bridge_.guest_pc = guest_pc;
    attach_bridge();
}

void NativeDspCoprocessor::set_host_callbacks(
    void* user,
    InterruptCallback interrupt,
    AcceleratorExceptionCallback accelerator_exception) {
    require_no_active_worker();
    bridge_.user = user;
    bridge_.request_interrupt = interrupt;
    bridge_.accelerator_exception = accelerator_exception;
    attach_bridge();
}

void NativeDspCoprocessor::install_host_services(
    void* user,
    const DspHardwareServices& services) {
    require_no_active_worker();
    context_.hardware = services;
    context_.hardware_user = user;
    // host_abort is owned here, not by the value-copied services struct.
    context_.host_abort = &abort_flag_;
    context_.host_cpu_mail_generation = &cpu_mail_generation_;
    context_.host_external_interrupt = &external_interrupt_vector_;
    context_.host_wake = &host_wake_state_;
    context_.host_telemetry = &telemetry_state_;
    context_.audio_causal_recorder =
        audio_causal_capture_enabled_ ? audio_causal_recorder_.get() : nullptr;
    context_.channel_selection_dma_probe =
        channel_selection_dma_probe_enabled_
        ? channel_selection_dma_probe_
        : nullptr;
    context_.audio_bus_controls = audio_bus_controls_;
    context_.pending_idle_backedges = &pending_idle_backedges_;
    context_.host_diagnostics =
        diagnostic_capture_enabled_ ? &diagnostic_state_ : nullptr;
    context_.last_retired_pc.pending_entries =
        &pending_instruction_entries_;
    context_.last_retired_pc.idle_sequence_tracker =
        &idle_sequence_tracker_;
}

void NativeDspCoprocessor::load_iram_words(
    std::uint16_t destination,
    std::span<const std::uint16_t> words) {
    require_no_active_worker();
    copy_words_checked(context_.iram, destination, words);
}

void NativeDspCoprocessor::load_irom_words(std::span<const std::uint16_t> words) {
    require_no_active_worker();
    copy_words_checked(context_.irom, 0, words);
    context_.irom_loaded = true;
}

void NativeDspCoprocessor::load_dram_words(
    std::uint16_t destination,
    std::span<const std::uint16_t> words) {
    require_no_active_worker();
    copy_words_checked(context_.dram, destination, words);
}

void NativeDspCoprocessor::load_coefficient_words(
    std::span<const std::uint16_t> words) {
    require_no_active_worker();
    copy_words_checked(context_.coef, 0, words);
    context_.coef_loaded = true;
}

void NativeDspCoprocessor::load_iram_bytes(
    std::uint32_t destination_byte,
    std::span<const std::byte> bytes) {
    require_no_active_worker();
    copy_bytes_checked(context_, true, destination_byte, bytes);
}

void NativeDspCoprocessor::load_dram_bytes(
    std::uint32_t destination_byte,
    std::span<const std::byte> bytes) {
    require_no_active_worker();
    copy_bytes_checked(context_, false, destination_byte, bytes);
}

void NativeDspCoprocessor::cpu_write_to_dsp_mailbox_high(std::uint16_t value) {
    dsp_mailbox_write_high(dsp_cpu_mailbox(context_), value);
}

void NativeDspCoprocessor::cpu_write_to_dsp_mailbox_low(std::uint16_t value) {
    const auto generation = reserve_cpu_mail_generation();
    publish_cpu_mailbox_low(value, generation);
}

std::uint64_t NativeDspCoprocessor::reserve_cpu_mail_generation() {
    const auto previous =
        cpu_mail_generation_.fetch_add(1u, std::memory_order_acq_rel);
    if (previous == std::numeric_limits<std::uint64_t>::max()) {
        std::abort();
    }
    return previous + 1u;
}

void NativeDspCoprocessor::publish_cpu_mailbox_low(
    std::uint16_t value,
    std::uint64_t generation) {
    if (generation == 0u ||
        cpu_mail_generation_.load(std::memory_order_acquire) != generation) {
        std::abort();
    }
    (void)dsp_mailbox_write_low(dsp_cpu_mailbox(context_), value);
    dsp_native_telemetry_add(telemetry_state_.cpu_mail_published);
    notify_mailbox_poll();
}

std::uint16_t NativeDspCoprocessor::cpu_read_from_dsp_mailbox_high() const {
    return dsp_mailbox_read_high(dsp_dsp_mailbox(context_));
}

std::uint16_t NativeDspCoprocessor::cpu_read_from_dsp_mailbox_low() {
    bool consumed_mail = false;
    const auto value = dsp_mailbox_read_low(
        dsp_dsp_mailbox(context_), &consumed_mail);
    if (consumed_mail) {
        dsp_native_telemetry_add(telemetry_state_.dsp_mail_consumed);
        // RMGE01's generated DSP task waits at DMBH/PC 0x07B3 while this
        // hardware mail is busy. Only a low-half read that actually clears the
        // busy bit is a consume event; peeks and redundant reads must not wake
        // (and repeatedly re-park) the native DSP worker.
        notify_mailbox_poll();
    }
    return value;
}

std::uint16_t NativeDspCoprocessor::cpu_peek_from_dsp_mailbox_low() const {
    return dsp_mailbox_peek_low(dsp_dsp_mailbox(context_));
}

std::uint16_t NativeDspCoprocessor::cpu_peek_to_dsp_mailbox_high() const {
    return dsp_mailbox_read_high(dsp_cpu_mailbox(context_));
}

std::uint16_t NativeDspCoprocessor::cpu_peek_to_dsp_mailbox_low() const {
    return dsp_mailbox_peek_low(dsp_cpu_mailbox(context_));
}

std::uint16_t NativeDspCoprocessor::cpu_read_mmio16(std::uint32_t offset) {
    switch (offset) {
    case kNativeDspMmioCpuMailboxHigh:
        return cpu_peek_to_dsp_mailbox_high();
    case kNativeDspMmioCpuMailboxLow:
        return cpu_peek_to_dsp_mailbox_low();
    case kNativeDspMmioDspMailboxHigh:
        return cpu_read_from_dsp_mailbox_high();
    case kNativeDspMmioDspMailboxLow:
        return cpu_read_from_dsp_mailbox_low();
    case kNativeDspMmioControl:
        require_no_active_worker();
        return control_;
    default:
        std::abort();
    }
}

void NativeDspCoprocessor::cpu_write_mmio16(std::uint32_t offset, std::uint16_t value) {
    switch (offset) {
    case kNativeDspMmioCpuMailboxHigh:
        cpu_write_to_dsp_mailbox_high(value);
        return;
    case kNativeDspMmioCpuMailboxLow:
        cpu_write_to_dsp_mailbox_low(value);
        return;
    case kNativeDspMmioControl:
        require_no_active_worker();
        if ((value & kNativeDspControlReset) != 0u) {
            const auto completed =
                static_cast<std::uint16_t>(value & ~kNativeDspControlReset);
            reset();
            control_ = completed;
            context_.halted = (completed & kNativeDspControlHalt) != 0u;
        } else {
            control_ = value;
            context_.halted = (value & kNativeDspControlHalt) != 0u;
        }
        return;
    default:
        std::abort();
    }
}

void NativeDspCoprocessor::run_native_entry(EntryPoint entry) {
    if (entry == nullptr) {
        std::abort();
    }
    dsp_native_reset_idle_sequence_progress(context_);
    try {
        entry(context_);
    } catch (...) {
        dsp_native_telemetry_publish_pending_worker_counts(context_);
        throw;
    }
    dsp_native_telemetry_publish_pending_worker_counts(context_);
}

void NativeDspCoprocessor::clear_abort() {
    abort_flag_.store(false, std::memory_order_release);
}

void NativeDspCoprocessor::request_abort() {
    abort_flag_.store(true, std::memory_order_release);
    notify_mailbox_poll();
}

bool NativeDspCoprocessor::abort_requested() const {
    return abort_flag_.load(std::memory_order_acquire);
}

void NativeDspCoprocessor::record_hard_trap(std::uint16_t pc) {
    hard_trap_pc_.store(pc, std::memory_order_release);
    hard_trap_flag_.store(true, std::memory_order_release);
    request_abort();
}

void NativeDspCoprocessor::clear_hard_trap() {
    hard_trap_pc_.store(0u, std::memory_order_release);
    hard_trap_flag_.store(false, std::memory_order_release);
}

bool NativeDspCoprocessor::hard_trap_active() const {
    return hard_trap_flag_.load(std::memory_order_acquire);
}

std::uint16_t NativeDspCoprocessor::hard_trap_pc() const {
    return hard_trap_pc_.load(std::memory_order_acquire);
}

void NativeDspCoprocessor::request_external_interrupt(std::uint16_t vector) {
    external_interrupt_vector_.store(vector, std::memory_order_release);
    notify_mailbox_poll();
}

void NativeDspCoprocessor::notify_mailbox_poll() {
    const auto previous =
        host_wake_state_.generation.fetch_add(1u, std::memory_order_acq_rel);
    if (previous == std::numeric_limits<std::uint64_t>::max()) {
        std::abort();
    }
    host_wake_state_.generation.notify_one();
}

bool NativeDspCoprocessor::enter_pending_external_interrupt() {
    const auto vector = external_interrupt_vector_.exchange(
        kDspNoPendingExternalInterrupt,
        std::memory_order_acq_rel);
    if (vector == kDspNoPendingExternalInterrupt) {
        return false;
    }
    if (dsp_enter_external_interrupt(context_, vector)) {
        return true;
    }
    external_interrupt_vector_.store(vector, std::memory_order_release);
    return false;
}

bool NativeDspCoprocessor::run_free_running(EntryPoint entry) {
    if (entry == nullptr) {
        std::abort();
    }
    dsp_native_reset_idle_sequence_progress(context_);
    try {
        entry(context_);
        dsp_native_telemetry_publish_pending_worker_counts(context_);
        return false;
    } catch (const DspExecutionAborted&) {
        // Cooperative shutdown: the host set host_abort and the mailbox poll
        // unwound out of the ucode. Nothing to clean up — the goto blocks hold
        // no resources.
        dsp_native_telemetry_publish_pending_worker_counts(context_);
        return false;
    } catch (const DspExternalInterruptRequested&) {
        // PIINT was accepted at an IFX boundary. PC/SR are already stacked and
        // ctx.pc points at the native external interrupt vector.
        dsp_native_telemetry_publish_pending_worker_counts(context_);
        return true;
    } catch (...) {
        dsp_native_telemetry_publish_pending_worker_counts(context_);
        throw;
    }
}

NativeDspWorker::NativeDspWorker(
    NativeDspCoprocessor& coprocessor,
    NativeDspCoprocessor::EntryPoint entry,
    bool free_running,
    DspAramMirrorBoundary* aram_boundary)
    : coprocessor_(&coprocessor),
      entry_(entry),
      free_running_(free_running),
      aram_boundary_(aram_boundary) {
    if (entry_ == nullptr) {
        std::abort();
    }
}

NativeDspWorker::~NativeDspWorker() {
    stop();
}

void NativeDspWorker::start() {
    std::lock_guard lock(mutex_);
    if (running_ || state_ != WorkerState::Stopped || thread_.joinable() ||
        pending_resume_.has_value() ||
        pending_external_interrupt_pc_.has_value()) {
        std::abort();
    }
    stop_requested_ = false;
    running_ = true;
    state_ = WorkerState::Starting;
    if (free_running_) {
        coprocessor_->clear_hard_trap();
        coprocessor_->clear_abort();
    }
    coprocessor_->claim_context_for_worker();
    try {
        thread_ = std::thread(&NativeDspWorker::thread_main, this);
    } catch (...) {
        coprocessor_->release_context_from_worker();
        running_ = false;
        state_ = WorkerState::Stopped;
        throw;
    }
}

void NativeDspWorker::stop() {
    {
        std::lock_guard lock(mutex_);
        if (!running_) {
            return;
        }
        stop_requested_ = true;
        // Publish the cooperative abort in the same critical section as the
        // Stopping state. A worker that wins the mutex afterward can no longer
        // dispatch a retained HALT resume before it observes shutdown.
        if (free_running_) {
            coprocessor_->request_abort();
        }
        if (state_ != WorkerState::Failed) {
            state_ = WorkerState::Stopping;
        }
    }
    // A free-running entry never checks pending_runs_; it must be unwound out of
    // the ucode's mailbox-poll loop via the cooperative abort.
    work_cv_.notify_one();
    if (thread_.joinable()) {
        thread_.join();
    }
    coprocessor_->release_context_from_worker();
    std::lock_guard lock(mutex_);
    running_ = false;
    stop_requested_ = false;
    pending_runs_ = 0;
    if (pending_resume_.has_value()) {
        resolve_resume_locked();
    }
    pending_external_interrupt_pc_.reset();
    state_ = WorkerState::Stopped;
}

void NativeDspWorker::signal_work() {
    {
        std::lock_guard lock(mutex_);
        if (!running_) {
            std::abort();
        }
        if (free_running_) {
            coprocessor_->notify_mailbox_poll();
            return;
        }
        ++pending_runs_;
    }
    work_cv_.notify_one();
}

void NativeDspWorker::signal_resume(std::uint16_t resume_pc) {
    {
        std::lock_guard lock(mutex_);
        if (!running_) {
            std::abort();
        }
        if (free_running_) {
            if (stop_requested_ || state_ == WorkerState::Stopping ||
                state_ == WorkerState::Failed ||
                state_ == WorkerState::Stopped) {
                std::abort();
            }
            latch_resume_locked(
                resume_pc, ResumeRequirement::None, /*cpu_mail_generation=*/0u);
            if (trace_native_dsp_worker()) {
                static std::uint64_t s_resume_signal_count = 0;
                ++s_resume_signal_count;
                if (s_resume_signal_count <= 64u ||
                    s_resume_signal_count % 4096u == 0u) {
                    const std::lock_guard<std::mutex> trace_lock(
                        g_dsp_trace_mutex);
                    std::cout << "[dsp-native-worker] signal_resume #"
                              << s_resume_signal_count << " pc=0x" << std::hex
                              << resume_pc << std::dec << " generation="
                              << pending_resume_->request_generation << '\n';
                }
            }
            work_cv_.notify_one();
            return;
        }
        ++pending_runs_;
    }
    work_cv_.notify_one();
}

bool NativeDspWorker::signal_resume_if_waiting(std::uint16_t resume_pc) {
    {
        std::lock_guard lock(mutex_);
        if (!running_) {
            std::abort();
        }
        if (!free_running_) {
            return false;
        }
        if (stop_requested_ || state_ == WorkerState::Stopping ||
            state_ == WorkerState::Failed || state_ == WorkerState::Stopped) {
            std::abort();
        }

        const bool halted = state_ == WorkerState::Halted;
        const bool cpu_mail_pending = !coprocessor_->cpu_mail_consumed();
        const auto cpu_mail_generation = coprocessor_->cpu_mail_generation();

        // The executing state includes the narrow interval after lowered code
        // has set ctx.halted and returned, but before this worker can publish
        // Halted under mutex_. Retain a resume in that interval only when it is
        // tied to an actual still-pending CPU mailbox publication. If active
        // ucode consumes the mail, wait_for_halted_wake() discards the latch.
        if (!halted && !cpu_mail_pending) {
            return false;
        }

        if (cpu_mail_pending && cpu_mail_generation != 0u &&
            resolved_cpu_mail_generation_ == cpu_mail_generation) {
            // Duplicate wake for the same hardware mailbox publication. The
            // first request was either applied or proven already consumed.
            return true;
        }

        const auto requirement = cpu_mail_pending
            ? ResumeRequirement::CpuMailStillPending
            : ResumeRequirement::None;
        latch_resume_locked(resume_pc, requirement, cpu_mail_generation);
        if (trace_native_dsp_worker()) {
            static std::uint64_t s_waiting_resume_signal_count = 0;
            ++s_waiting_resume_signal_count;
            if (s_waiting_resume_signal_count <= 64u ||
                s_waiting_resume_signal_count % 4096u == 0u) {
                const std::lock_guard<std::mutex> trace_lock(
                    g_dsp_trace_mutex);
                std::cout << "[dsp-native-worker] signal_resume_waiting #"
                          << s_waiting_resume_signal_count << " pc=0x"
                          << std::hex << resume_pc << std::dec << " generation="
                          << pending_resume_->request_generation << " state="
                          << (halted ? "halted" : "executing-latched")
                          << " cpu-mail-generation=" << cpu_mail_generation
                          << '\n';
            }
        }
    }
    work_cv_.notify_one();
    return true;
}

void NativeDspWorker::signal_external_interrupt(std::uint16_t vector) {
    {
        std::lock_guard lock(mutex_);
        if (!running_) {
            std::abort();
        }
        if (free_running_) {
            if (stop_requested_ || state_ == WorkerState::Stopping ||
                state_ == WorkerState::Failed ||
                state_ == WorkerState::Stopped) {
                std::abort();
            }
            if (pending_external_interrupt_pc_.has_value() &&
                *pending_external_interrupt_pc_ != vector) {
                std::abort();
            }
            coprocessor_->request_external_interrupt(vector);
            pending_external_interrupt_pc_ = vector;
            if (trace_native_dsp_worker()) {
                static std::uint64_t s_external_signal_count = 0;
                ++s_external_signal_count;
                if (s_external_signal_count <= 64u ||
                    s_external_signal_count % 4096u == 0u) {
                    const std::lock_guard<std::mutex> trace_lock(
                        g_dsp_trace_mutex);
                    std::cout << "[dsp-native-worker] signal_external #"
                              << s_external_signal_count << " pc=0x" << std::hex
                              << vector << std::dec << '\n';
                }
            }
            work_cv_.notify_one();
            return;
        }
        ++pending_runs_;
    }
    work_cv_.notify_one();
}

void NativeDspWorker::latch_resume_locked(
    std::uint16_t resume_pc,
    ResumeRequirement requirement,
    std::uint64_t cpu_mail_generation) {
    if (pending_resume_.has_value()) {
        if (pending_resume_->pc != resume_pc) {
            // One hardware HALT cannot be resumed at two different PCs. Such a
            // request would otherwise make execution scheduler-order dependent.
            std::abort();
        }

        if (pending_resume_->requirement == ResumeRequirement::None) {
            return;
        }
        if (requirement == ResumeRequirement::None) {
            pending_resume_->requirement = ResumeRequirement::None;
            pending_resume_->cpu_mail_generation = 0u;
            return;
        }
        if (pending_resume_->cpu_mail_generation == cpu_mail_generation) {
            return;
        }

        // A newer mailbox publication supersedes a speculative latch whose
        // mail was consumed while the DSP was still executing.
        resolve_resume_locked();
    }

    if (next_resume_generation_ ==
        std::numeric_limits<std::uint64_t>::max()) {
        std::abort();
    }
    const auto request_generation = ++next_resume_generation_;
    pending_resume_ = ResumeLatch{
        resume_pc,
        request_generation,
        requirement,
        requirement == ResumeRequirement::CpuMailStillPending
            ? cpu_mail_generation
            : 0u};
}

void NativeDspWorker::resolve_resume_locked() {
    if (!pending_resume_.has_value() ||
        pending_resume_->request_generation <= resolved_resume_generation_) {
        std::abort();
    }
    resolved_resume_generation_ = pending_resume_->request_generation;
    if (pending_resume_->requirement ==
        ResumeRequirement::CpuMailStillPending) {
        if (pending_resume_->cpu_mail_generation == 0u) {
            std::abort();
        }
        resolved_cpu_mail_generation_ =
            pending_resume_->cpu_mail_generation;
    }
    pending_resume_.reset();
}

bool NativeDspWorker::is_halted() const {
    std::lock_guard lock(mutex_);
    return state_ == WorkerState::Halted;
}

std::optional<DspDiagnosticSnapshot> NativeDspWorker::halted_snapshot() const {
    std::lock_guard lock(mutex_);
    if (state_ != WorkerState::Halted) {
        return std::nullopt;
    }
    return coprocessor_->capture_quiescent_worker_snapshot(completed_runs_);
}

std::uint64_t NativeDspWorker::completed_runs() const {
    std::lock_guard lock(mutex_);
    return completed_runs_;
}

bool NativeDspWorker::wait_for_completed_runs(
    std::uint64_t target,
    std::chrono::milliseconds timeout) {
    std::unique_lock lock(mutex_);
    return completed_cv_.wait_for(
        lock,
        timeout,
        [&] { return completed_runs_ >= target; });
}

void NativeDspWorker::thread_main() {
#if defined(_WIN32)
    (void)SetThreadDescription(GetCurrentThread(), L"Nebula Native DSP");
#endif
    configure_native_dsp_worker_scheduling();
    {
        std::lock_guard lock(mutex_);
        if (stop_requested_ && state_ == WorkerState::Stopping) {
            return;
        }
        if (state_ != WorkerState::Starting) {
            std::abort();
        }
    }

    if (aram_boundary_ != nullptr && !aram_boundary_->worker_bind()) {
        std::lock_guard lock(mutex_);
        if (!stop_requested_ && state_ == WorkerState::Starting) {
            state_ = WorkerState::Failed;
        }
        completed_cv_.notify_all();
        return;
    }
    struct AramDetachGuard {
        DspAramMirrorBoundary* boundary{};
        ~AramDetachGuard() {
            if (boundary != nullptr) {
                (void)boundary->worker_detach();
            }
        }
    } aram_detach_guard{aram_boundary_};

    {
        std::lock_guard lock(mutex_);
        if (stop_requested_ && state_ == WorkerState::Stopping) {
            return;
        }
        if (state_ != WorkerState::Starting) {
            std::abort();
        }
        state_ = free_running_ && coprocessor_->worker_context().halted
            ? WorkerState::Halted
            : WorkerState::Executing;
    }
    if (free_running_) {
        // The real ucode owns the thread for the whole session. A lowered DSP
        // HALT returns to the worker with ctx.pc still on the HALT instruction;
        // a plain CPU mailbox write does not clear HALT. Only an explicit task
        // resume or DSP PIINT vector can re-enter code.
        auto wait_for_halted_wake = [&]() -> std::optional<std::uint16_t> {
            std::unique_lock lock(mutex_);
            if (stop_requested_ && state_ == WorkerState::Stopping) {
                return std::nullopt;
            }
            if (state_ != WorkerState::Halted) {
                std::abort();
            }

            auto finish_wait =
                [&](std::optional<std::uint16_t> resume_pc)
                    -> std::optional<std::uint16_t> {
                state_ = resume_pc.has_value() ? WorkerState::Executing
                                               : WorkerState::Stopping;
                return resume_pc;
            };

            for (;;) {
                // Shutdown has strict priority over queued work. Once stop()
                // begins, no retained resume may execute another ucode entry.
                if (stop_requested_ || coprocessor_->abort_requested()) {
                    return finish_wait(std::nullopt);
                }
                if (pending_external_interrupt_pc_.has_value()) {
                    pending_external_interrupt_pc_.reset();
                    if (coprocessor_->enter_pending_external_interrupt()) {
                        return finish_wait(
                            std::optional<std::uint16_t>{
                                coprocessor_->worker_context().pc});
                    }
                } else if (pending_resume_.has_value()) {
                    const auto resume = *pending_resume_;
                    bool requirement_satisfied = true;
                    if (resume.requirement ==
                        ResumeRequirement::CpuMailStillPending) {
                        requirement_satisfied =
                            resume.cpu_mail_generation ==
                                coprocessor_->cpu_mail_generation() &&
                            !coprocessor_->cpu_mail_consumed();
                    }
                    resolve_resume_locked();
                    if (requirement_satisfied) {
                        return finish_wait(
                            std::optional<std::uint16_t>{resume.pc});
                    }
                    // Active ucode consumed the mailbox before it halted. The
                    // speculative request is resolved without becoming a
                    // stale resume for a future task.
                }
                work_cv_.wait(lock, [&] {
                    return stop_requested_ || coprocessor_->abort_requested() ||
                           pending_resume_.has_value() ||
                           pending_external_interrupt_pc_.has_value();
                });
            }
        };

        auto apply_halted_wake = [&](std::uint16_t resume_pc) {
            if (trace_native_dsp_worker()) {
                static std::uint64_t s_resume_apply_count = 0;
                ++s_resume_apply_count;
                if (s_resume_apply_count <= 64u ||
                    s_resume_apply_count % 4096u == 0u) {
                    const std::lock_guard<std::mutex> trace_lock(
                        g_dsp_trace_mutex);
                    std::cout << "[dsp-native-worker] apply_resume #"
                              << s_resume_apply_count << " pc=0x" << std::hex
                              << resume_pc << " cpu-high=0x"
                              << coprocessor_->cpu_peek_to_dsp_mailbox_high()
                              << std::dec << '\n';
                }
            }
            coprocessor_->worker_context().pc = resume_pc;
        };

        bool halted_wake_ready = false;
        for (;;) {
            if (trace_native_dsp_worker()) {
                static std::uint64_t s_run_entry_count = 0;
                ++s_run_entry_count;
                if (s_run_entry_count <= 64u ||
                    s_run_entry_count % 4096u == 0u) {
                    const std::lock_guard<std::mutex> trace_lock(
                        g_dsp_trace_mutex);
                    const auto& ctx = coprocessor_->worker_context();
                    std::cout << "[dsp-native-worker] enter #"
                              << s_run_entry_count
                              << " halted=" << (ctx.halted ? 1 : 0)
                              << " pc=0x" << std::hex << ctx.pc
                              << " sr=0x" << ctx.sr
                              << " st0=0x" << ctx.st[0]
                              << " st1=0x" << ctx.st[1]
                              << " cpu-high=0x"
                              << coprocessor_->cpu_peek_to_dsp_mailbox_high()
                              << " cpu-low=0x"
                              << coprocessor_->cpu_peek_to_dsp_mailbox_low()
                              << " dsp-high=0x"
                              << coprocessor_->cpu_read_from_dsp_mailbox_high()
                              << " dsp-low=0x"
                              << coprocessor_->cpu_peek_from_dsp_mailbox_low()
                              << std::dec << '\n';
                }
            }
            if (coprocessor_->worker_context().halted && !halted_wake_ready) {
                const auto resume_pc = wait_for_halted_wake();
                if (!resume_pc.has_value()) {
                    return;
                }
                apply_halted_wake(*resume_pc);
                halted_wake_ready = true;
            }
            {
                std::lock_guard lock(mutex_);
                if (stop_requested_ || state_ == WorkerState::Stopping ||
                    coprocessor_->abort_requested()) {
                    return;
                }
                if (state_ != WorkerState::Executing) {
                    std::abort();
                }
            }
            if (coprocessor_->worker_context().halted) {
                coprocessor_->worker_context().halted = false;
            }
            halted_wake_ready = false;
            bool accepted_external_interrupt = false;
            try {
                accepted_external_interrupt =
                    coprocessor_->run_free_running(entry_);
                auto& context = coprocessor_->worker_context();
                if (context.halted && !accepted_external_interrupt &&
                    !coprocessor_->abort_requested() &&
                    context.hardware.before_clean_halt_return != nullptr &&
                    !context.hardware.before_clean_halt_return(
                        context.hardware_user)) {
                    dsp_hard_trap_at_current(
                        context,
                        "DSP clean HALT pre-return ordering was rejected");
                }
            } catch (const DspHardTrap& trap) {
                // During shutdown, a synchronous MRAM callback can be
                // released by the host cancellation path only by returning a
                // failed span to generated DSP code.  That deliberately
                // unwinds the worker through DspHardTrap, but it is not a
                // guest-visible DSP fault and must not poison the worker's
                // failure state or diagnostics.  A real span failure reaches
                // this point with neither shutdown signal set and remains a
                // hard failure below.
                bool shutdown_unwind = false;
                {
                    std::lock_guard lock(mutex_);
                    shutdown_unwind =
                        stop_requested_ || coprocessor_->abort_requested();
                    if (shutdown_unwind) {
                        ++completed_runs_;
                        state_ = WorkerState::Stopping;
                        if (pending_resume_.has_value()) {
                            resolve_resume_locked();
                        }
                    }
                }
                if (shutdown_unwind) {
                    coprocessor_->publish_worker_diagnostic(
                        DspDiagnosticBoundary::AbortReturn);
                    completed_cv_.notify_all();
                    return;
                }
                coprocessor_->record_hard_trap(trap.pc);
                coprocessor_->publish_worker_diagnostic(
                    DspDiagnosticBoundary::HardTrapReturn);
                {
                    std::lock_guard lock(mutex_);
                    ++completed_runs_;
                    state_ = WorkerState::Failed;
                    if (pending_resume_.has_value()) {
                        resolve_resume_locked();
                    }
                }
                completed_cv_.notify_all();
                {
                    const std::lock_guard<std::mutex> trace_lock(
                        g_dsp_trace_mutex);
                    const auto& ctx = coprocessor_->worker_context();
                    std::cerr << "[dsp-native-worker] hard-trap pc=0x"
                              << std::hex << trap.pc << " source-pc=0x"
                              << trap.last_retired_pc << " reason="
                              << (trap.reason != nullptr ? trap.reason
                                                          : "<unknown>")
                              << " abort="
                              << (coprocessor_->abort_requested() ? 1 : 0)
                              << " ar=(0x" << ctx.ar[0] << ",0x" << ctx.ar[1]
                              << ",0x" << ctx.ar[2] << ",0x" << ctx.ar[3]
                              << ") ix=(0x"
                              << static_cast<std::uint16_t>(ctx.ix[0])
                              << ",0x"
                              << static_cast<std::uint16_t>(ctx.ix[1])
                              << ",0x"
                              << static_cast<std::uint16_t>(ctx.ix[2])
                              << ",0x"
                              << static_cast<std::uint16_t>(ctx.ix[3])
                              << ") wr=(0x" << ctx.wr[0] << ",0x"
                              << ctx.wr[1] << ",0x" << ctx.wr[2]
                              << ",0x" << ctx.wr[3] << ") ac=("
                              << std::dec
                              << static_cast<long long>(ctx.ac[0]) << ','
                              << static_cast<long long>(ctx.ac[1])
                              << ") st=(0x" << std::hex << ctx.st[0]
                              << ",0x" << ctx.st[1] << ",0x" << ctx.st[2]
                              << ",0x" << ctx.st[3] << ") dram3a3=0x"
                              << ctx.dram[0x03A3u] << " dram3f9=0x"
                              << ctx.dram[0x03F9u] << " dram3fa=0x"
                              << ctx.dram[0x03FAu] << " dram3fd=0x"
                              << ctx.dram[0x03FDu] << " dram3fe=0x"
                              << ctx.dram[0x03FEu] << " dram3ff=0x"
                              << ctx.dram[0x03FFu]
                              // DIAGNOSTIC EXPERIMENT (gated by the trap
                              // itself firing, no separate flag needed): word
                              // 0x428 is what pc 0x0DEB/0x0DEC's clamped
                              // address-register read consumed to produce
                              // ar[0]=0x7fff. Dump 0x420-0x430 at the exact
                              // trap instant -- periodic host-side channel-
                              // table samples are close in time but not
                              // exact, and the observed 0x50 offset value
                              // (0x7F00) does not reproduce the overflow by
                              // itself, so the DSP-side value at the precise
                              // instant needs to be seen directly.
                              << " dram420=0x" << ctx.dram[0x0420u]
                              << " dram421=0x" << ctx.dram[0x0421u]
                              << " dram422=0x" << ctx.dram[0x0422u]
                              << " dram423=0x" << ctx.dram[0x0423u]
                              << " dram424=0x" << ctx.dram[0x0424u]
                              << " dram425=0x" << ctx.dram[0x0425u]
                              << " dram426=0x" << ctx.dram[0x0426u]
                              << " dram427=0x" << ctx.dram[0x0427u]
                              << " dram428=0x" << ctx.dram[0x0428u]
                              << " dram429=0x" << ctx.dram[0x0429u]
                              << " dram42a=0x" << ctx.dram[0x042Au]
                              << " dram42b=0x" << ctx.dram[0x042Bu]
                              << " dram42c=0x" << ctx.dram[0x042Cu]
                              << " dram42d=0x" << ctx.dram[0x042Du]
                              << " dram42e=0x" << ctx.dram[0x042Eu]
                              << " dram42f=0x" << ctx.dram[0x042Fu]
                              << " dram430=0x" << ctx.dram[0x0430u]
                              << " cpu=0x"
                              << dsp_mailbox_raw(dsp_cpu_mailbox(ctx))
                              << " dsp=0x"
                              << dsp_mailbox_raw(dsp_dsp_mailbox(ctx))
                              << std::dec << '\n';
                }
                return;
            }
            std::uint64_t completed_run = 0;
            const bool halted_after_run =
                coprocessor_->worker_context().halted;
            const bool should_wait_for_halted_wake =
                halted_after_run && !accepted_external_interrupt &&
                !coprocessor_->abort_requested();
            const DspDiagnosticBoundary return_boundary =
                accepted_external_interrupt
                    ? DspDiagnosticBoundary::ExternalInterruptReturn
                    : (coprocessor_->abort_requested()
                           ? DspDiagnosticBoundary::AbortReturn
                           : (halted_after_run
                                  ? DspDiagnosticBoundary::HaltReturn
                                  : DspDiagnosticBoundary::UnexpectedReturn));
            coprocessor_->publish_worker_diagnostic(return_boundary);
            bool stopped_after_run = false;
            bool impossible_return = false;
            {
                std::lock_guard lock(mutex_);
                completed_run = ++completed_runs_;
                stopped_after_run =
                    stop_requested_ || coprocessor_->abort_requested();
                if (stopped_after_run) {
                    state_ = WorkerState::Stopping;
                } else if (accepted_external_interrupt) {
                    if (!halted_after_run) {
                        impossible_return = true;
                        state_ = WorkerState::Failed;
                    } else {
                        // Exception entry is an immediate native continuation,
                        // not an architectural HALT wait.
                        state_ = WorkerState::Executing;
                    }
                } else if (should_wait_for_halted_wake) {
                    // This mutex publication closes the ordinary side of the
                    // resume race. Requests that arrived in the preceding
                    // post-return gap are already retained in pending_resume_.
                    state_ = WorkerState::Halted;
                } else {
                    impossible_return = true;
                    state_ = WorkerState::Failed;
                }
            }
            completed_cv_.notify_all();
            if (trace_native_dsp_worker() &&
                (completed_run <= 64u || completed_run % 4096u == 0u)) {
                const std::lock_guard<std::mutex> trace_lock(
                    g_dsp_trace_mutex);
                std::cout << "[dsp-native-worker] run #" << completed_run
                          << " returned halted="
                          << (coprocessor_->worker_context().halted ? 1 : 0)
                          << " pc=0x" << std::hex
                          << coprocessor_->worker_context().pc
                          << " cpu-high=0x"
                          << coprocessor_->cpu_peek_to_dsp_mailbox_high()
                          << " abort="
                          << (coprocessor_->abort_requested() ? 1 : 0)
                          << std::dec << '\n';
            }
            if (impossible_return) {
                // A free-running generated entry may leave only through HALT,
                // an accepted external interrupt, a hard trap, or shutdown.
                // Continuing after any other return would silently lose DSP
                // execution and strand audio on the last produced sample.
                std::abort();
            }
            if (stopped_after_run) {
                return;
            }
            if (accepted_external_interrupt &&
                coprocessor_->worker_context().halted) {
                {
                    std::lock_guard lock(mutex_);
                    if (pending_external_interrupt_pc_.has_value() &&
                        *pending_external_interrupt_pc_ ==
                            coprocessor_->worker_context().pc) {
                        pending_external_interrupt_pc_.reset();
                    }
                }
                halted_wake_ready = true;
                continue;
            }
            if (!coprocessor_->worker_context().halted) {
                std::abort();
            }
        }
        return;
    }
    for (;;) {
        {
            std::unique_lock lock(mutex_);
            work_cv_.wait(lock, [&] {
                return stop_requested_ || pending_runs_ != 0;
            });
            if (stop_requested_) {
                break;
            }
            --pending_runs_;
        }

        coprocessor_->run_native_entry(entry_);
        coprocessor_->publish_worker_diagnostic(
            DspDiagnosticBoundary::SingleShotReturn);

        {
            std::lock_guard lock(mutex_);
            ++completed_runs_;
        }
        completed_cv_.notify_all();
    }
}

}  // namespace galaxy
