#include "galaxy/coop_scheduler.h"

#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <xmmintrin.h>

#include <bit>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <string>

namespace galaxy::coop {
namespace {

// DefaultSwitchThreadCallback 0x804AAC08 is an empty guest function.
constexpr std::uint32_t kDefaultSwitchThreadCallback = 0x804AAC08u;

// A translated thread's native frames: deep scene traversals stay well below
// this; the reserve is address space only.
constexpr SIZE_T kFiberCommit = 256u * 1024u;
constexpr SIZE_T kFiberReserve = 8u * 1024u * 1024u;

std::uint32_t leading_zeros(std::uint32_t value) noexcept {
    return static_cast<std::uint32_t>(std::countl_zero(value));
}

std::string hex(std::uint32_t value) {
    char text[16];
    std::snprintf(text, sizeof(text), "0x%08X", value);
    return text;
}

struct FiberStart {
    Scheduler* scheduler;
    std::uint32_t thread;
};

}  // namespace

void save_thread_registers(const PpcContext& context, ThreadRegisters& out) noexcept {
    std::memcpy(out.gpr, context.gpr, sizeof(out.gpr));
    std::memcpy(out.fpr_bits, context.fpr_bits, sizeof(out.fpr_bits));
    std::memcpy(out.ps1_bits, context.ps1_bits, sizeof(out.ps1_bits));
    out.cr = context.cr;
    out.lr = context.lr;
    out.ctr = context.ctr;
    out.xer = context.xer;
    out.fpscr = context.fpscr;
    std::memcpy(out.gqr, context.gqr, sizeof(out.gqr));
    out.msr = context.msr;
    out.pc = context.pc;
    out.reserved_address = context.reserved_address;
    out.reserved_value = context.reserved_value;
}

void load_thread_registers(const ThreadRegisters& in, PpcContext& context) noexcept {
    std::memcpy(context.gpr, in.gpr, sizeof(in.gpr));
    std::memcpy(context.fpr_bits, in.fpr_bits, sizeof(in.fpr_bits));
    std::memcpy(context.ps1_bits, in.ps1_bits, sizeof(in.ps1_bits));
    context.cr = in.cr;
    context.lr = in.lr;
    context.ctr = in.ctr;
    context.xer = in.xer;
    context.fpscr = in.fpscr;
    std::memcpy(context.gqr, in.gqr, sizeof(in.gqr));
    context.msr = in.msr;
    context.pc = in.pc;
    context.reserved_address = in.reserved_address;
    context.reserved_value = in.reserved_value;
}

std::uint32_t GuestRam::load32(std::uint32_t address) const noexcept {
    std::uint32_t value;
    std::memcpy(&value, base + address, sizeof(value));
    return byte_swap_u32(value);
}

std::uint16_t GuestRam::load16(std::uint32_t address) const noexcept {
    std::uint16_t value;
    std::memcpy(&value, base + address, sizeof(value));
    return byte_swap_u16(value);
}

std::uint64_t GuestRam::load64(std::uint32_t address) const noexcept {
    std::uint64_t value;
    std::memcpy(&value, base + address, sizeof(value));
    return byte_swap_u64(value);
}

void GuestRam::store32(std::uint32_t address, std::uint32_t value) const noexcept {
    const std::uint32_t swapped = byte_swap_u32(value);
    std::memcpy(base + address, &swapped, sizeof(swapped));
}

void GuestRam::store16(std::uint32_t address, std::uint16_t value) const noexcept {
    const std::uint16_t swapped = byte_swap_u16(value);
    std::memcpy(base + address, &swapped, sizeof(swapped));
}

Scheduler::Scheduler(Host host, GuestRam ram, PpcContext* shared_context)
    : host_(host), ram_(ram), context_(shared_context) {
    if (host_.run_new_thread == nullptr || host_.idle == nullptr ||
        host_.call_guest == nullptr || host_.exchange_chain == nullptr ||
        ram_.base == nullptr || context_ == nullptr) {
        throw std::logic_error("cooperative scheduler needs every host hook");
    }
}

Scheduler::~Scheduler() {
    for (void* handle : retired_) {
        if (handle != nullptr && handle != GetCurrentFiber()) DeleteFiber(handle);
    }
    for (auto& [thread, fiber] : fibers_) {
        if (fiber.handle != nullptr && !fiber.boot && fiber.handle != GetCurrentFiber()) {
            DeleteFiber(fiber.handle);
        }
    }
}

void Scheduler::attach_boot_fiber() {
    if (boot_handle_ != nullptr) return;
    // Float switching keeps MXCSR (flush-to-zero, rounding) per fiber; new
    // fibers start from the simulation thread's mode.
    boot_handle_ = ConvertThreadToFiberEx(nullptr, FIBER_FLAG_FLOAT_SWITCH);
    if (boot_handle_ == nullptr) {
        throw std::runtime_error(
            "ConvertThreadToFiberEx failed: " + std::to_string(GetLastError()));
    }
    mxcsr_ = _mm_getcsr();
}

ChainState& Scheduler::running_chain() noexcept {
    if (running_thread_ != 0u) {
        const auto it = fibers_.find(running_thread_);
        if (it != fibers_.end() && it->second.handle == GetCurrentFiber()) {
            return it->second.chain;
        }
    }
    return unowned_chain_;
}

void Scheduler::set_current(std::uint32_t thread, std::uint32_t context_address) noexcept {
    ram_.store32(os::kCurrentThread, thread);
    ram_.store32(os::kCurrentContext, context_address);
    ram_.store32(os::kCurrentContextPhysical, context_address & 0x3FFFFFFFu);
    if (context_address != 0u && context_address != os::kIdleContext) {
        // Every fiber keeps its whole register file, so the FPU always
        // belongs to the running context: the SDK's lazy owner switch never
        // has anything to save or load.
        ram_.store32(os::kFpuContext, context_address);
        context_->msr |= os::kMsrFloatingPoint;
    }
}

void Scheduler::call_switch_callback(std::uint32_t old_thread, std::uint32_t new_thread) {
    const std::uint32_t callback = ram_.load32(os::kSwitchThreadCallback);
    if (callback == 0u || callback == kDefaultSwitchThreadCallback) return;
    // The SDK calls it from SelectThread on the outgoing thread's stack with
    // interrupts disabled; the caller's registers survive the call.
    ThreadRegisters saved;
    save_thread_registers(*context_, saved);
    context_->gpr[3] = old_thread;
    context_->gpr[4] = new_thread;
    context_->gpr[1] -= 0x40u;
    host_.call_guest(host_.user, callback, context_);
    load_thread_registers(saved, *context_);
}

void Scheduler::enqueue_run(std::uint32_t thread) noexcept {
    // SDK SetRun: tail-enqueue on RunQueue[priority], mark the priority bit.
    const std::uint32_t priority = ram_.load32(thread + os::kThreadPriority) & 31u;
    const std::uint32_t queue = os::kRunQueue + priority * 8u;
    ram_.store32(thread + os::kThreadQueue, queue);
    const std::uint32_t tail = ram_.load32(queue + 4u);
    if (tail == 0u) {
        ram_.store32(queue, thread);
    } else {
        ram_.store32(tail + os::kThreadLinkNext, thread);
    }
    ram_.store32(thread + os::kThreadLinkPrev, tail);
    ram_.store32(thread + os::kThreadLinkNext, 0u);
    ram_.store32(queue + 4u, thread);
    ram_.store32(os::kRunQueueBits,
                 ram_.load32(os::kRunQueueBits) | (1u << (31u - priority)));
    ram_.store32(os::kRunQueueHint, 1u);
}

void Scheduler::select_thread(bool yield) {
    context_->gpr[3] = 0u;
    if (static_cast<std::int32_t>(ram_.load32(os::kReschedule)) > 0) return;

    const std::uint32_t current_context = ram_.load32(os::kCurrentContext);
    const std::uint32_t current = ram_.load32(os::kCurrentThread);
    if (current_context != current) return;

    if (current != 0u) {
        // The fiber executing SelectThread belongs to the current thread.
        Fiber& self = fibers_[current];
        if (self.handle == nullptr) {
            self.handle = GetCurrentFiber();
            self.started = true;
            self.boot = self.handle == boot_handle_;
            if (self.boot) {
                boot_thread_ = current;
            }
        } else if (self.handle != GetCurrentFiber()) {
            throw std::runtime_error(
                "cooperative SelectThread: current thread " + hex(current) +
                " belongs to another fiber");
        }
        running_thread_ = current;
        if (ram_.load16(current + os::kThreadState) == os::kStateRunning) {
            if (!yield) {
                const std::uint32_t bits = ram_.load32(os::kRunQueueBits);
                const std::int32_t highest = static_cast<std::int32_t>(leading_zeros(bits));
                if (static_cast<std::int32_t>(ram_.load32(current + os::kThreadPriority)) <= highest) {
                    return;
                }
            }
            ram_.store16(current + os::kThreadState, os::kStateReady);
            enqueue_run(current);
        }
    }

    if (ram_.load32(os::kRunQueueBits) == 0u) {
        call_switch_callback(current, 0u);
        set_current(0u, os::kIdleContext);
        ++idle_entries_;
        do {
            ++idle_passes_;
            host_.idle(host_.user, context_);
        } while (ram_.load32(os::kRunQueueBits) == 0u);
    }

    ram_.store32(os::kRunQueueHint, 0u);
    const std::uint32_t bits = ram_.load32(os::kRunQueueBits);
    const std::uint32_t priority = leading_zeros(bits);
    const std::uint32_t queue = os::kRunQueue + priority * 8u;
    const std::uint32_t next = ram_.load32(queue);
    if (next == 0u) {
        throw std::runtime_error(
            "cooperative SelectThread: run queue " + std::to_string(priority) +
            " is marked ready but empty");
    }
    const std::uint32_t after = ram_.load32(next + os::kThreadLinkNext);
    if (after == 0u) {
        ram_.store32(queue + 4u, 0u);
    } else {
        ram_.store32(after + os::kThreadLinkPrev, 0u);
    }
    ram_.store32(queue, after);
    if (after == 0u) {
        ram_.store32(os::kRunQueueBits, bits & ~(1u << (31u - priority)));
    }
    ram_.store32(next + os::kThreadQueue, 0u);
    ram_.store16(next + os::kThreadState, os::kStateRunning);

    // __OSSwitchThread: OSSetCurrentThread (switch callback), then the
    // context; the "load" is the fiber switch below.
    call_switch_callback(ram_.load32(os::kCurrentThread), next);
    set_current(next, next);
    if (next != running_thread_ || fibers_[next].handle != GetCurrentFiber()) {
        switch_to(next);
    }
}

void Scheduler::switch_to(std::uint32_t next) {
    ChainState* const parked = &running_chain();
    if (running_thread_ != 0u) {
        Fiber& self = fibers_[running_thread_];
        if (self.handle == GetCurrentFiber()) {
            save_thread_registers(*context_, self.saved);
            self.started = true;
        }
    }
    // References into fibers_ stay valid across insertion.
    Fiber& target = fibers_[next];
    if (target.handle == nullptr) {
        auto* start = new FiberStart{this, next};
        target.handle = CreateFiberEx(kFiberCommit, kFiberReserve, FIBER_FLAG_FLOAT_SWITCH,
                                      &Scheduler::fiber_entry, start);
        if (target.handle == nullptr) {
            delete start;
            throw std::runtime_error(
                "CreateFiberEx failed for guest thread " + hex(next) + ": " +
                std::to_string(GetLastError()));
        }
        target.started = false;
        target.chain = {};
        ++fibers_created_;
    } else if (target.started) {
        load_thread_registers(target.saved, *context_);
    }
    host_.exchange_chain(host_.user, *parked, target.chain);
    running_thread_ = next;
    ++switches_;
    SwitchToFiber(target.handle);

    // Resumed: the switching fiber restored this thread's registers and chain.
    reap_exited();
    release_parked();
    if (fiber_error_ != nullptr && GetCurrentFiber() == boot_handle_) {
        std::exception_ptr error = fiber_error_;
        fiber_error_ = nullptr;
        std::rethrow_exception(error);
    }
}

void Scheduler::return_to_boot() noexcept {
    ChainState& parked = running_chain();
    const auto boot = fibers_.find(boot_thread_);
    const ChainState& resumed =
        boot != fibers_.end() && boot->second.handle == boot_handle_
            ? boot->second.chain
            : unowned_chain_;
    host_.exchange_chain(host_.user, parked, resumed);
    running_thread_ = boot_thread_;
    SwitchToFiber(boot_handle_);
    // The boot fiber never resumes a fiber that reported a failure.
    std::terminate();
}

void __stdcall Scheduler::fiber_entry(void* parameter) {
    auto* start = static_cast<FiberStart*>(parameter);
    Scheduler* const scheduler = start->scheduler;
    const std::uint32_t thread = start->thread;
    delete start;
    _mm_setcsr(scheduler->mxcsr_);
    try {
        scheduler->host_.run_new_thread(scheduler->host_.user, thread, scheduler->context_);
        throw std::runtime_error(
            "guest thread " + hex(thread) + " fiber resumed after its exit");
    } catch (...) {
        scheduler->fiber_error_ = std::current_exception();
    }
    // Report on the boot fiber, which owns the runtime's top-level handler.
    scheduler->return_to_boot();
}

void Scheduler::on_thread_created(std::uint32_t thread) {
    const auto it = fibers_.find(thread);
    if (it == fibers_.end()) return;
    if (it->second.handle != nullptr && it->second.handle != GetCurrentFiber() &&
        !it->second.boot) {
        retired_.push_back(it->second.handle);
    }
    fibers_.erase(it);
    release_parked();
}

// A thread that exited or was cancelled (state 0, or MORIBUND until joined)
// runs again only through OSCreateThread, which starts a new fiber: its parked
// fiber is released.
void Scheduler::reap_exited() {
    void* const current = GetCurrentFiber();
    for (auto it = fibers_.begin(); it != fibers_.end();) {
        const Fiber& fiber = it->second;
        const std::uint16_t state = ram_.load16(it->first + os::kThreadState);
        if (fiber.handle != nullptr && fiber.handle != current && !fiber.boot &&
            fiber.started && (state == 0u || state == os::kStateMoribund)) {
            retired_.push_back(fiber.handle);
            it = fibers_.erase(it);
        } else {
            ++it;
        }
    }
}

void Scheduler::release_parked() {
    if (retired_.empty()) return;
    void* const current = GetCurrentFiber();
    for (auto it = retired_.begin(); it != retired_.end();) {
        if (*it != current) {
            DeleteFiber(*it);
            it = retired_.erase(it);
        } else {
            ++it;
        }
    }
}

}  // namespace galaxy::coop
