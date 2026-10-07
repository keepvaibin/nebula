#include "galaxy/native_host.h"
#include "galaxy/ai_dma_timing.h"
#include "galaxy/interrupt_entry.h"
#include "galaxy/native_input_causality.h"
#include "galaxy/native_ios_anomaly.h"
#include "galaxy/owned_storage_worker.h"
#include "galaxy/runtime_settings.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <memory>
#include <span>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <vector>
#include <utility>

namespace galaxy::host {

using NativeHostOwnershipTestHook = bool (*)(
    unsigned, const std::filesystem::path&, unsigned);
void native_host_set_ownership_test_hook(NativeHostOwnershipTestHook hook);
bool native_host_test_handle_cleanup();
bool native_host_test_wav_dump(std::span<const std::byte> pcm,
    std::uint32_t rate, bool fail_finalization);
void native_host_test_attach_nand_metadata(const std::filesystem::path& path);

struct NativeNandTestAccess {
    static std::uint32_t persist_new_file(const std::filesystem::path& root,
        const std::filesystem::path& path) {
        GuestAddressSpace::NandBacking file;
        file.guest_path = "/tmp/collision.bin";
        file.data = {std::byte{0x25}};
        return GuestAddressSpace::persist_nand_file_at(file, root, path, false, false);
    }
    static void set_persist_hooks(GuestAddressSpace& memory,
        void (*before)(void*), void* before_user,
        void (*after)(void*) = nullptr, void* after_user = nullptr) {
        memory.native_nand_test_before_persist_ = before;
        memory.native_nand_test_before_persist_user_ = before_user;
        memory.native_nand_test_after_persist_ = after;
        memory.native_nand_test_after_persist_user_ = after_user;
    }
    static std::uint32_t file_position(const GuestAddressSpace& memory, std::uint32_t handle) {
        return memory.nand_files_.at(handle).position;
    }
    static std::vector<std::byte> file_bytes(const GuestAddressSpace& memory, std::uint32_t handle) {
        return memory.nand_files_.at(handle).backing->data;
    }
    static std::uint32_t ensure_rmge01_data_directory(
        GuestAddressSpace& address_space) {
        return address_space.ensure_rmge01_data_directory();
    }

    static bool provisional_nand_file_tracked(
        const GuestAddressSpace& address_space,
        std::string_view guest_path) {
        return address_space.provisional_nand_files_.contains(
            std::string(guest_path));
    }

    static std::filesystem::path nand_root(
        const GuestAddressSpace& address_space) {
        return address_space.nand_root_;
    }

    static std::filesystem::path nand_host_path(
        const GuestAddressSpace& address_space,
        std::string_view guest_path) {
        return address_space.nand_host_path(guest_path);
    }
};

struct NativeDspBoundaryTestAccess {
    struct AramDmaTransactionState {
        bool active{};
        bool aram_to_mram{};
        std::uint32_t mram_address{};
        std::uint32_t aram_address{};
        std::uint32_t total_blocks{};
        std::uint32_t completed_blocks{};
        std::uint64_t start_ticks{};
        std::uint64_t next_block_ticks{};

        bool operator==(const AramDmaTransactionState&) const = default;
    };

    static AramDmaTransactionState aram_dma_transaction_state(
        const GuestAddressSpace& address_space) {
        return {
            address_space.aram_dma_active_,
            address_space.aram_dma_aram_to_mram_,
            address_space.aram_dma_mram_address_,
            address_space.aram_dma_aram_address_,
            address_space.aram_dma_total_blocks_,
            address_space.aram_dma_completed_blocks_,
            address_space.aram_dma_start_ticks_,
            address_space.aram_dma_next_block_ticks_,
        };
    }

    static bool pin_aram_backing(
        GuestAddressSpace& address_space,
        std::uint32_t backing_base) {
        return address_space.dsp_native_pin_aram_backing(backing_base);
    }

    static bool reset_aram_boundary(GuestAddressSpace& address_space) {
        address_space.dsp_native_clear_aram_tracking();
        address_space.dsp_native_aram_backing_base_.store(
            0u, std::memory_order_release);
        address_space.dsp_cmd_words_remaining_ = 0u;
        address_space.dsp_cmd_first_word_ = 0u;
        address_space.dsp_cmd_word_count_ = 0u;
        address_space.dsp_cmd_have_first_ = false;
        address_space.dsp_native_command_declared_count_ = 0u;
        address_space.dsp_native_command_is_dset_ = false;
        address_space.dsp_native_dset_seen_ = false;
        const bool configured =
            address_space.dsp_native_aram_commit_transactions_.configure_wake(
                &NativeDspBoundaryTestAccess::publish_test_wake, nullptr);
        return configured &&
               address_space.dsp_native_aram_boundary_.reset() &&
               address_space.dsp_native_aram_commit_transactions_.reset();
    }

    static void observe_command_mail(
        GuestAddressSpace& address_space,
        std::uint32_t mail,
        std::uint64_t generation) {
        address_space.dsp_native_observe_command_mail(mail, generation);
    }

    static void service_transactions(GuestAddressSpace& address_space) {
        address_space.dsp_native_service_mram_transaction();
    }

    static galaxy::DspAramMirrorBoundary& aram_boundary(
        GuestAddressSpace& address_space) {
        return address_space.dsp_native_aram_boundary_;
    }

    static bool interrupt_pending(const GuestAddressSpace& address_space) {
        return address_space.dsp_native_int_pending_.load(
            std::memory_order_acquire);
    }

    static void clear_interrupt(GuestAddressSpace& address_space) {
        address_space.dsp_native_int_pending_.store(
            false, std::memory_order_release);
    }

    static void cancel_aram_boundary(GuestAddressSpace& address_space) {
        address_space.dsp_native_aram_boundary_.cancel();
        address_space.dsp_native_aram_commit_transactions_.shutdown();
        address_space.dsp_native_clear_aram_tracking();
    }

    static std::uint32_t command_words_remaining(
        const GuestAddressSpace& address_space) {
        return address_space.dsp_cmd_words_remaining_;
    }

    static void set_aram_dma_overlap_probe(
        GuestAddressSpace& address_space,
        bool active,
        std::uint32_t aram_address) {
        address_space.aram_dma_active_ = active;
        address_space.aram_dma_aram_address_ = aram_address;
        address_space.aram_dma_total_blocks_ = active ? 1u : 0u;
        address_space.aram_dma_completed_blocks_ = 0u;
    }

    static bool service_aram_commit(
        GuestAddressSpace& address_space,
        std::uint32_t address,
        const std::uint8_t* source,
        std::uint32_t size) {
        return GuestAddressSpace::dsp_native_service_aram_commit_write_cb(
            &address_space, address, source, size);
    }

    static bool start_synthetic_native_worker(
        GuestAddressSpace& address_space,
        NativeDspCoprocessor::EntryPoint entry,
        bool stage_seed,
        bool capture_audio_causality = false,
        bool capture_selection_dma_causality = false) {
        address_space.dsp_native_clear_aram_tracking();
        address_space.dsp_native_aram_backing_base_.store(
            0u, std::memory_order_release);
        address_space.dsp_cmd_words_remaining_ = 0u;
        address_space.dsp_cmd_word_count_ = 0u;
        address_space.dsp_cmd_have_first_ = false;
        address_space.dsp_native_command_declared_count_ = 0u;
        address_space.dsp_native_command_is_dset_ = false;
        address_space.dsp_native_dset_seen_ = false;
        const bool configured =
            address_space.dsp_native_mram_transactions_.configure_wake(
                &NativeDspBoundaryTestAccess::publish_test_wake, nullptr) &&
            address_space.dsp_native_aram_commit_transactions_.configure_wake(
                &NativeDspBoundaryTestAccess::publish_test_wake, nullptr);
        if (!configured || !address_space.dsp_native_aram_boundary_.reset() ||
            !address_space.dsp_native_mram_transactions_.reset() ||
            !address_space.dsp_native_aram_commit_transactions_.reset()) {
            return false;
        }
        if (stage_seed) {
            address_space.dsp_native_observe_command_mail(2u, 1u);
            address_space.dsp_native_observe_command_mail(
                0x8e000000u, 2u);
            address_space.dsp_native_observe_command_mail(
                0x90000800u, 3u);
        }
        address_space.dsp_native_ =
            std::make_unique<NativeDspCoprocessor>();
        address_space.dsp_native_->set_audio_causal_capture_enabled(
            capture_audio_causality);
        if (capture_selection_dma_causality) {
            if (address_space.dsp_channel_selection_dma_probe_ == nullptr) {
                address_space.dsp_channel_selection_dma_probe_ =
                    std::make_unique<
                        galaxy::DspChannelSelectionDmaProbeRecorder>();
                address_space.dsp_channel_selection_dma_probe_
                    ->generated_probe_contract_version =
                    galaxy::kDspGeneratedProbeContractVersion;
                address_space.dsp_channel_selection_dma_probe_
                    ->generated_probe_capabilities =
                    galaxy::kDspGeneratedProbeRequiredCapabilities;
            }
            galaxy::dsp_channel_selection_dma_probe_begin_session(
                *address_space.dsp_channel_selection_dma_probe_);
            address_space.dsp_native_->attach_channel_selection_dma_probe(
                address_space.dsp_channel_selection_dma_probe_.get());
        }
        address_space.dsp_native_->install_host_services(
            &address_space,
            GuestAddressSpace::dsp_native_hardware_services());
        address_space.dsp_native_worker_ =
            std::make_unique<NativeDspWorker>(
                *address_space.dsp_native_,
                entry,
                /*free_running=*/true,
                &address_space.dsp_native_aram_boundary_);
        address_space.dsp_native_worker_->start();
        address_space.dsp_native_running_ = true;
        address_space.dsp_task_booted_ = true;
        address_space.dsp_boot_iram_src_ = 0x80001000u;
        address_space.dsp_boot_iram_len_ = 0x20u;
        return true;
    }

    static bool submit_mram_read(
        GuestAddressSpace& address_space,
        std::uint8_t* destination,
        std::uint32_t size) {
        return address_space.dsp_native_mram_transactions_.submit_read(
            0x80001000u, destination, size);
    }

    static bool submit_aram_commit(
        GuestAddressSpace& address_space,
        const std::uint8_t* source,
        std::uint32_t size) {
        return address_space.dsp_native_aram_commit_transactions_.submit_write(
            0x90000800u, source, size);
    }

    static bool both_transactions_pending(
        const GuestAddressSpace& address_space) {
        return address_space.dsp_native_mram_transactions_.pending() &&
               address_space.dsp_native_aram_commit_transactions_.pending();
    }

    static bool synthetic_worker_stopped(
        const GuestAddressSpace& address_space) {
        return !address_space.dsp_native_running_ &&
               address_space.dsp_native_ == nullptr &&
               address_space.dsp_native_worker_ == nullptr &&
               address_space.memory_.cpu_dirty_page_words == nullptr;
    }

    static galaxy::DspChannelSelectionDmaProbeSnapshot
    selection_dma_probe_snapshot(const GuestAddressSpace& address_space) {
        return address_space.dsp_channel_selection_dma_probe_ != nullptr
            ? galaxy::dsp_channel_selection_dma_probe_snapshot(
                  *address_space.dsp_channel_selection_dma_probe_)
            : galaxy::DspChannelSelectionDmaProbeSnapshot{};
    }

    static bool start_synthetic_input_worker(
        GuestAddressSpace& address_space,
        std::unique_ptr<input::NativeHidIoBackend> backend) {
        if (address_space.bt_real_input_worker_ != nullptr) {
            return false;
        }
        address_space.bt_real_input_worker_ =
            std::make_unique<input::NativeHidIoWorker>(
                std::move(backend), 4u, 1u);
        address_space.bt_real_input_worker_->start();
        return true;
    }

    static bool synthetic_input_worker_stopped(
        const GuestAddressSpace& address_space) {
        return address_space.bt_real_input_worker_ == nullptr;
    }

    static void shutdown_synthetic_native_worker(
        GuestAddressSpace& address_space) {
        address_space.dsp_native_shutdown(
            /*emit_audio_causal_report=*/false);
    }

    static bool rom_reset_state(const GuestAddressSpace& address_space) {
        return !address_space.dsp_task_booted_ &&
               address_space.dsp_boot_iram_src_ == 0u &&
               address_space.dsp_boot_iram_len_ == 0u;
    }

    static DspMramTransactionBoundary::Snapshot mram_snapshot(
        const GuestAddressSpace& address_space) {
        return address_space.dsp_native_mram_transactions_.snapshot();
    }

    static DspMramTransactionBoundary::Snapshot aram_commit_snapshot(
        const GuestAddressSpace& address_space) {
        return address_space.dsp_native_aram_commit_transactions_.snapshot();
    }

    static DspHardwareServices services() {
        return GuestAddressSpace::dsp_native_hardware_services();
    }

private:
    static bool publish_test_wake(void*) noexcept {
        return true;
    }
};

struct NativeBluetoothTestAccess {
    static void arm_l2cap_post_auth_open(
        GuestAddressSpace& address_space,
        std::uint64_t authenticated_at_ticks) {
        address_space.bt_wiimote_connected_ = true;
        address_space.bt_wiimote_authenticated_ = true;
        address_space.bt_wiimote_encrypted_ = true;
        address_space.bt_wiimote_authenticated_at_ticks_ =
            authenticated_at_ticks;
        address_space.bt_l2cap_control_connect_pending_ = true;
        address_space.bt_l2cap_post_auth_wait_logged_ = false;
        address_space.bt_l2cap_retry_psm_ = 0u;
        address_space.bt_l2cap_retry_deadline_ticks_ = 0u;
        address_space.bt_l2cap_control_retry_count_ = 0u;
        address_space.bt_l2cap_interrupt_retry_count_ = 0u;
        address_space.bt_l2cap_control_local_cid_ = 0u;
        address_space.bt_l2cap_control_remote_cid_ = 0u;
        address_space.bt_l2cap_interrupt_local_cid_ = 0u;
        address_space.bt_l2cap_interrupt_remote_cid_ = 0u;
        address_space.bt_l2cap_control_config_request_seen_ = false;
        address_space.bt_l2cap_control_config_response_seen_ = false;
        address_space.bt_l2cap_interrupt_config_request_seen_ = false;
        address_space.bt_l2cap_interrupt_config_response_seen_ = false;
        address_space.bt_l2cap_next_local_cid_ = 0x0040u;
        address_space.bt_l2cap_next_signal_id_ = 0x02u;
        address_space.clear_wiimote_l2cap_signaling_transactions();
        address_space.bluetooth_acl_events_.clear();
    }

    static bool control_open_pending(const GuestAddressSpace& address_space) {
        return address_space.bt_l2cap_control_connect_pending_;
    }

    static void start_l2cap_control_request_for_protocol_fixture(
        GuestAddressSpace& address_space) {
        address_space.bt_l2cap_control_connect_pending_ = false;
        address_space.queue_wiimote_l2cap_connection_request(0x0011u);
    }

    static std::uint16_t control_local_cid(
        const GuestAddressSpace& address_space) {
        return address_space.bt_l2cap_control_local_cid_;
    }

    static std::uint16_t control_remote_cid(
        const GuestAddressSpace& address_space) {
        return address_space.bt_l2cap_control_remote_cid_;
    }

    static std::uint16_t interrupt_local_cid(
        const GuestAddressSpace& address_space) {
        return address_space.bt_l2cap_interrupt_local_cid_;
    }

    static std::uint16_t interrupt_remote_cid(
        const GuestAddressSpace& address_space) {
        return address_space.bt_l2cap_interrupt_remote_cid_;
    }

    static bool interrupt_open(const GuestAddressSpace& address_space) {
        return address_space.bt_wiimote_interrupt_open_;
    }

    static std::uint16_t l2cap_retry_psm(
        const GuestAddressSpace& address_space) {
        return address_space.bt_l2cap_retry_psm_;
    }

    static std::uint64_t l2cap_retry_deadline_ticks(
        const GuestAddressSpace& address_space) {
        return address_space.bt_l2cap_retry_deadline_ticks_;
    }

    static std::uint8_t l2cap_retry_count(
        const GuestAddressSpace& address_space,
        std::uint16_t psm) {
        if (psm == 0x0011u) {
            return address_space.bt_l2cap_control_retry_count_;
        }
        if (psm == 0x0013u) {
            return address_space.bt_l2cap_interrupt_retry_count_;
        }
        throw std::runtime_error("test requested unsupported L2CAP PSM");
    }

    static bool scan_enabled(const GuestAddressSpace& address_space) {
        return address_space.bt_wiimote_scan_enabled_;
    }

    static bool connection_requested(const GuestAddressSpace& address_space) {
        return address_space.bt_wiimote_connection_requested_;
    }

    static bool connection_request_delivered(
        const GuestAddressSpace& address_space) {
        return address_space.bt_wiimote_connection_request_delivered_;
    }

    static bool connected(const GuestAddressSpace& address_space) {
        return address_space.bt_wiimote_connected_;
    }

    static std::uint64_t connection_request_deadline_ticks(
        const GuestAddressSpace& address_space) {
        return address_space.bt_connection_request_deadline_ticks_;
    }

    static std::uint64_t next_connection_attempt_ticks(
        const GuestAddressSpace& address_space) {
        return address_space.bt_next_connection_attempt_ticks_;
    }

    static std::uint64_t last_connection_attempt_ticks(
        const GuestAddressSpace& address_space) {
        return address_space.bt_last_connection_attempt_ticks_;
    }

    static std::uint16_t l2cap_connection_psm(
        const GuestAddressSpace& address_space) {
        return address_space.bt_l2cap_connection_psm_;
    }

    static std::uint8_t l2cap_connection_signal_id(
        const GuestAddressSpace& address_space) {
        return address_space.bt_l2cap_connection_signal_id_;
    }

    static std::uint64_t l2cap_connection_response_deadline_ticks(
        const GuestAddressSpace& address_space) {
        return address_space.bt_l2cap_connection_response_deadline_ticks_;
    }

    static bool l2cap_connection_response_pending(
        const GuestAddressSpace& address_space) {
        return address_space.bt_l2cap_connection_response_pending_;
    }

    static std::uint64_t l2cap_connection_ertx_maximum_deadline_ticks(
        const GuestAddressSpace& address_space) {
        return address_space
            .bt_l2cap_connection_ertx_maximum_deadline_ticks_;
    }

    static std::uint16_t l2cap_configuration_psm(
        const GuestAddressSpace& address_space) {
        return address_space.bt_l2cap_configuration_psm_;
    }

    static std::uint8_t l2cap_config_signal_id(
        const GuestAddressSpace& address_space) {
        return address_space.bt_l2cap_config_signal_id_;
    }

    static std::uint64_t l2cap_config_response_deadline_ticks(
        const GuestAddressSpace& address_space) {
        return address_space.bt_l2cap_config_response_deadline_ticks_;
    }

    static bool l2cap_config_response_pending(
        const GuestAddressSpace& address_space) {
        return address_space.bt_l2cap_config_response_pending_;
    }

    static std::uint64_t l2cap_configuration_deadline_ticks(
        const GuestAddressSpace& address_space) {
        return address_space.bt_l2cap_configuration_deadline_ticks_;
    }

    static std::size_t queued_hci_events(
        const GuestAddressSpace& address_space) {
        return address_space.bluetooth_events_.size();
    }

    static std::vector<std::byte> take_hci_event(
        GuestAddressSpace& address_space) {
        if (address_space.bluetooth_events_.empty()) {
            return {};
        }
        address_space.note_bluetooth_event_delivered(
            address_space.bluetooth_events_.front());
        std::vector<std::byte> event =
            std::move(address_space.bluetooth_events_.front());
        address_space.bluetooth_events_.pop_front();
        return event;
    }

    static void clear_hci_events(GuestAddressSpace& address_space) {
        address_space.bluetooth_events_.clear();
    }

    static std::vector<std::byte> take_acl_event(
        GuestAddressSpace& address_space) {
        if (address_space.bluetooth_acl_events_.empty()) {
            return {};
        }
        address_space.note_bluetooth_acl_delivered(
            address_space.bluetooth_acl_events_.front());
        std::vector<std::byte> event =
            std::move(address_space.bluetooth_acl_events_.front());
        address_space.bluetooth_acl_events_.pop_front();
        return event;
    }

    static void queue_controller_command(
        GuestAddressSpace& address_space,
        std::span<const std::byte> command) {
        if (command.size() < 3u ||
            command.size() !=
                3u + static_cast<std::uint8_t>(command[2])) {
            throw std::runtime_error("malformed test HCI controller command");
        }
        const std::uint16_t opcode = static_cast<std::uint16_t>(
            static_cast<std::uint8_t>(command[0]) |
            static_cast<std::uint16_t>(
                static_cast<std::uint8_t>(command[1]))
                << 8);
        address_space.queue_bluetooth_command_complete(
            opcode,
            reinterpret_cast<const std::uint8_t*>(command.data()),
            static_cast<std::uint32_t>(command.size()));
    }

    static bool handle_connection_command(
        GuestAddressSpace& address_space,
        std::span<const std::byte> command) {
        if (command.size() < 3u ||
            command.size() !=
                3u + static_cast<std::uint8_t>(command[2])) {
            throw std::runtime_error("malformed test HCI connection command");
        }
        const std::uint16_t opcode = static_cast<std::uint16_t>(
            static_cast<std::uint8_t>(command[0]) |
            static_cast<std::uint16_t>(
                static_cast<std::uint8_t>(command[1]))
                << 8);
        return address_space.handle_bluetooth_connection_command(
            opcode,
            reinterpret_cast<const std::uint8_t*>(command.data()),
            static_cast<std::uint32_t>(command.size()));
    }

    static bool handle_acl_packet(
        GuestAddressSpace& address_space,
        std::span<const std::byte> packet) {
        constexpr std::uint32_t kPacketAddress = 0x13500000u;
        address_space.copy(kPacketAddress, packet);
        return address_space.handle_bluetooth_acl_out(
            kPacketAddress, static_cast<std::uint32_t>(packet.size()));
    }

    static std::size_t queued_acl_events(
        const GuestAddressSpace& address_space) {
        return address_space.bluetooth_acl_events_.size();
    }

    static bool virtual_input_queued(
        const GuestAddressSpace& address_space) {
        return address_space.bt_virtual_input_report_.has_value();
    }

    static std::uint8_t queued_virtual_report_mode(
        const GuestAddressSpace& address_space) {
        if (!address_space.bt_virtual_input_report_.has_value() ||
            address_space.bt_virtual_input_report_->packet.size() < 10u) {
            return 0u;
        }
        return static_cast<std::uint8_t>(
            address_space.bt_virtual_input_report_->packet[9]);
    }

    static std::uint64_t queued_virtual_deadline_ticks(
        const GuestAddressSpace& address_space) {
        return address_space.bt_virtual_input_report_.has_value()
            ? address_space.bt_virtual_input_report_->cadence.deadline_ticks
            : 0u;
    }

    static std::uint64_t queued_virtual_production_wii_ticks(
        const GuestAddressSpace& address_space) {
        return address_space.bt_virtual_input_report_.has_value()
            ? address_space.bt_virtual_input_report_->production_wii_ticks
            : 0u;
    }

    static std::uint64_t queued_virtual_input_mode_generation(
        const GuestAddressSpace& address_space) {
        return address_space.bt_virtual_input_report_.has_value()
            ? address_space.bt_virtual_input_report_
                  ->input_mode_generation
            : 0u;
    }

    static bool real_input_worker_started(
        const GuestAddressSpace& address_space) {
        return address_space.bt_real_input_worker_ != nullptr;
    }

    static std::uint64_t queued_virtual_host_pointer_sequence(
        const GuestAddressSpace& address_space) {
        return address_space.bt_virtual_input_report_.has_value()
            ? address_space.bt_virtual_input_report_->host_pointer_sequence
            : 0u;
    }

    static galaxy::host::HostPointerSequenceDomain
    queued_virtual_host_pointer_sequence_domain(
        const GuestAddressSpace& address_space) {
        return address_space.bt_virtual_input_report_.has_value()
            ? address_space.bt_virtual_input_report_
                  ->host_pointer_sequence_domain
            : galaxy::host::HostPointerSequenceDomain::None;
    }

    static std::uint64_t queued_virtual_host_pointer_acquired_ms(
        const GuestAddressSpace& address_space) {
        return address_space.bt_virtual_input_report_.has_value()
            ? address_space.bt_virtual_input_report_
                  ->host_pointer_acquired_ms
            : 0u;
    }

    static std::uint64_t queued_virtual_epoch(
        const GuestAddressSpace& address_space) {
        return address_space.bt_virtual_input_report_.has_value()
            ? address_space.bt_virtual_input_report_->cadence.epoch
            : 0u;
    }

    static std::uint64_t queued_virtual_sample_sequence(
        const GuestAddressSpace& address_space) {
        return address_space.bt_virtual_input_report_.has_value()
            ? address_space.bt_virtual_input_report_->cadence.sequence
            : 0u;
    }

    static bool queued_virtual_has_host_pointer_sample(
        const GuestAddressSpace& address_space) {
        return address_space.bt_virtual_input_report_.has_value() &&
            address_space.bt_virtual_input_report_->host_pointer_sampled;
    }

    static bool virtual_ir_gate_open(
        const GuestAddressSpace& address_space) {
        return address_space.bt_wiimote_ir_report13_enabled_ &&
            address_space.bt_wiimote_ir_report1a_enabled_ &&
            address_space.bt_wiimote_ir_sensitivity_block1_seen_ &&
            address_space.bt_wiimote_ir_sensitivity_block2_seen_ &&
            address_space.bt_wiimote_ir_latch_seen_ &&
            address_space.bt_wiimote_ir_mode_ != 0u;
    }

    static void reset_virtual_input_device(
        GuestAddressSpace& address_space,
        bool disconnected) {
        address_space.reset_virtual_wiimote_input_device(disconnected);
    }
};

}  // namespace galaxy::host

namespace {

bool capture_native_keyboard_for_test() noexcept {
    return true;
}

void write_be16(
    std::vector<std::byte>& bytes,
    std::size_t offset,
    std::uint16_t value) {
    bytes[offset] = static_cast<std::byte>(value >> 8);
    bytes[offset + 1] = static_cast<std::byte>(value);
}

void write_be32(std::vector<std::byte>& bytes, std::size_t offset, std::uint32_t value) {
    bytes[offset] = static_cast<std::byte>(value >> 24);
    bytes[offset + 1] = static_cast<std::byte>(value >> 16);
    bytes[offset + 2] = static_cast<std::byte>(value >> 8);
    bytes[offset + 3] = static_cast<std::byte>(value);
}

void write_be64(
    std::vector<std::byte>& bytes,
    std::size_t offset,
    std::uint64_t value) {
    for (unsigned index = 0u; index < 8u; ++index) {
        bytes[offset + index] = static_cast<std::byte>(
            value >> (56u - index * 8u));
    }
}

void write_le32(std::vector<std::byte>& bytes, std::size_t offset, std::uint32_t value) {
    bytes[offset] = static_cast<std::byte>(value);
    bytes[offset + 1] = static_cast<std::byte>(value >> 8);
    bytes[offset + 2] = static_cast<std::byte>(value >> 16);
    bytes[offset + 3] = static_cast<std::byte>(value >> 24);
}

void write_binary_file(
    const std::filesystem::path& path,
    std::span<const std::byte> bytes) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file) {
        throw std::runtime_error("failed to create " + path.string());
    }
    file.write(
        reinterpret_cast<const char*>(bytes.data()),
        static_cast<std::streamsize>(bytes.size()));
    if (!file) {
        throw std::runtime_error("failed to write " + path.string());
    }
}

std::filesystem::path extended_length_path_for_test(
    const std::filesystem::path& input) {
    std::error_code error;
    std::filesystem::path absolute =
        std::filesystem::absolute(input, error).lexically_normal();
    if (error || !absolute.is_absolute()) {
        throw std::runtime_error("failed to make absolute long test path");
    }
    absolute.make_preferred();
    const std::wstring native = absolute.native();
    if (native.starts_with(LR"(\\?\)")) return absolute;
    if (native.starts_with(LR"(\\)")) {
        return std::filesystem::path(
            std::wstring(LR"(\\?\UNC\)") + native.substr(2u));
    }
    if (native.size() < 3u || native[1] != L':' || native[2] != L'\\') {
        throw std::runtime_error("long test path is not drive-qualified");
    }
    return std::filesystem::path(
        std::wstring(LR"(\\?\)") + native);
}

std::vector<std::byte> gir1_rename_journal_for_test(
    std::string_view source_path,
    std::string_view target_path,
    std::uint32_t process_id,
    std::uint64_t sequence,
    const std::array<std::uint64_t, 2>& source_identity,
    const std::array<std::uint64_t, 2>& old_target_identity,
    bool old_target_was_provisional = false) {
    constexpr std::size_t kSerializedSize = 176u;
    const std::string_view source_name = source_path.substr(
        source_path.find_last_of('/') + 1u);
    const std::string_view target_name = target_path.substr(
        target_path.find_last_of('/') + 1u);
    if (source_path.empty() || source_path.size() >= 64u ||
        target_path.empty() || target_path.size() >= 64u ||
        source_path == target_path || source_name != target_name ||
        process_id == 0u || sequence == 0u ||
        source_identity[0] > std::numeric_limits<std::uint32_t>::max() ||
        old_target_identity[0] >
            std::numeric_limits<std::uint32_t>::max()) {
        throw std::runtime_error("invalid GIR1 test journal input");
    }

    std::vector<std::byte> bytes(kSerializedSize, std::byte{0});
    bytes[0] = std::byte{'G'};
    bytes[1] = std::byte{'I'};
    bytes[2] = std::byte{'R'};
    bytes[3] = std::byte{'1'};
    bytes[4] = old_target_was_provisional ? std::byte{1} : std::byte{0};
    write_be32(bytes, 8u, process_id);
    write_be64(bytes, 12u, sequence);
    write_be32(
        bytes, 20u, static_cast<std::uint32_t>(source_identity[0]));
    write_be64(bytes, 24u, source_identity[1]);
    write_be32(
        bytes, 32u, static_cast<std::uint32_t>(old_target_identity[0]));
    write_be64(bytes, 36u, old_target_identity[1]);
    for (std::size_t index = 0u; index < source_path.size(); ++index) {
        bytes[44u + index] = static_cast<std::byte>(
            static_cast<unsigned char>(source_path[index]));
    }
    for (std::size_t index = 0u; index < target_path.size(); ++index) {
        bytes[108u + index] = static_cast<std::byte>(
            static_cast<unsigned char>(target_path[index]));
    }
    std::uint32_t checksum = 2'166'136'261u;
    for (const std::byte value :
         std::span<const std::byte>(bytes.data(), 172u)) {
        checksum ^= std::to_integer<std::uint8_t>(value);
        checksum *= 16'777'619u;
    }
    write_be32(bytes, 172u, checksum);
    return bytes;
}

std::array<std::uint64_t, 2> host_file_identity_for_test(
    const std::filesystem::path& path) {
    HANDLE file = CreateFileW(
        path.c_str(),
        FILE_READ_ATTRIBUTES,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL | FILE_FLAG_BACKUP_SEMANTICS,
        nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        throw std::runtime_error(
            "failed to open test file identity for " + path.string());
    }
    BY_HANDLE_FILE_INFORMATION information{};
    const BOOL queried = GetFileInformationByHandle(file, &information);
    const DWORD query_error = queried == FALSE ? GetLastError() : ERROR_SUCCESS;
    const BOOL closed = CloseHandle(file);
    if (queried == FALSE || closed == FALSE) {
        throw std::runtime_error(
            "failed to query test file identity for " + path.string() +
            " (host error " +
            std::to_string(
                query_error != ERROR_SUCCESS ? query_error : GetLastError()) +
            ")");
    }
    return {
        static_cast<std::uint64_t>(information.dwVolumeSerialNumber),
        (static_cast<std::uint64_t>(information.nFileIndexHigh) << 32u) |
            information.nFileIndexLow,
    };
}

bool host_file_stream_exists_for_test(const std::filesystem::path& path) {
    HANDLE file = CreateFileW(
        path.c_str(),
        GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL | FILE_FLAG_BACKUP_SEMANTICS,
        nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        const DWORD error = GetLastError();
        if (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND) {
            return false;
        }
        throw std::runtime_error(
            "failed to probe test file stream " + path.string() +
            " (host error " + std::to_string(error) + ")");
    }
    if (CloseHandle(file) == FALSE) {
        throw std::runtime_error(
            "failed to close test file stream probe for " + path.string());
    }
    return true;
}

std::size_t host_named_stream_count_for_test(
    const std::filesystem::path& path) {
    WIN32_FIND_STREAM_DATA stream{};
    HANDLE search = FindFirstStreamW(
        path.c_str(), FindStreamInfoStandard, &stream, 0u);
    if (search == INVALID_HANDLE_VALUE) {
        const DWORD error = GetLastError();
        if (error == ERROR_HANDLE_EOF) return 0u;
        throw std::runtime_error(
            "failed to enumerate test file streams for " + path.string() +
            " (host error " + std::to_string(error) + ")");
    }
    std::size_t count = 0u;
    do {
        if (std::wstring_view(stream.cStreamName) != L"::$DATA") {
            ++count;
        }
    } while (FindNextStreamW(search, &stream) != FALSE);
    const DWORD enumeration_error = GetLastError();
    const BOOL closed = FindClose(search);
    if (enumeration_error != ERROR_HANDLE_EOF || closed == FALSE) {
        throw std::runtime_error(
            "failed to finish test file stream enumeration for " +
            path.string() + " (host error " +
            std::to_string(
                enumeration_error != ERROR_HANDLE_EOF
                    ? enumeration_error
                    : GetLastError()) +
            ")");
    }
    return count;
}

std::vector<std::byte> gis3_metadata_for_test(
    std::uint32_t owner,
    std::uint16_t group,
    std::uint8_t owner_mode,
    std::uint8_t group_mode,
    std::uint8_t other_mode,
    std::uint8_t attribute) {
    std::vector<std::byte> bytes(18u, std::byte{0});
    bytes[0] = std::byte{'G'};
    bytes[1] = std::byte{'I'};
    bytes[2] = std::byte{'S'};
    bytes[3] = std::byte{'3'};
    write_be32(bytes, 4u, owner);
    write_be16(bytes, 8u, group);
    bytes[10] = static_cast<std::byte>(owner_mode);
    bytes[11] = static_cast<std::byte>(group_mode);
    bytes[12] = static_cast<std::byte>(other_mode);
    bytes[13] = static_cast<std::byte>(attribute);
    std::uint32_t checksum = 2'166'136'261u;
    for (const std::byte value :
         std::span<const std::byte>(bytes.data(), 14u)) {
        checksum ^= std::to_integer<std::uint8_t>(value);
        checksum *= 16'777'619u;
    }
    write_be32(bytes, 14u, checksum);
    return bytes;
}

bool exact_gis3_metadata_for_test(
    const std::filesystem::path& object,
    std::uint32_t owner,
    std::uint16_t group,
    std::uint8_t owner_mode,
    std::uint8_t group_mode,
    std::uint8_t other_mode,
    std::uint8_t attribute) {
    const std::filesystem::path metadata(
        object.native() + L":galaxy.isfs.meta");
    return host_file_stream_exists_for_test(metadata) &&
        galaxy::host::read_binary_file(metadata) ==
            gis3_metadata_for_test(
                owner,
                group,
                owner_mode,
                group_mode,
                other_mode,
                attribute);
}

std::uint32_t host_file_link_count_for_test(
    const std::filesystem::path& path) {
    HANDLE file = CreateFileW(
        path.c_str(),
        FILE_READ_ATTRIBUTES,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL | FILE_FLAG_BACKUP_SEMANTICS,
        nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        throw std::runtime_error(
            "failed to open test link count for " + path.string());
    }
    BY_HANDLE_FILE_INFORMATION information{};
    const BOOL queried = GetFileInformationByHandle(file, &information);
    const DWORD query_error = queried == FALSE ? GetLastError() : ERROR_SUCCESS;
    const BOOL closed = CloseHandle(file);
    if (queried == FALSE || closed == FALSE) {
        throw std::runtime_error(
            "failed to query test link count for " + path.string() +
            " (host error " +
            std::to_string(
                query_error != ERROR_SUCCESS ? query_error : GetLastError()) +
            ")");
    }
    return information.nNumberOfLinks;
}

std::size_t nand_rename_transaction_artifact_count_for_test(
    const std::filesystem::path& root) {
    constexpr std::string_view kBackupMarker =
        ".__galaxy_isfs_rename_backup__.";
    constexpr std::string_view kJournalMarker =
        ".__galaxy_isfs_rename_journal__.";
    std::error_code error;
    std::size_t count = 0u;
    std::filesystem::recursive_directory_iterator iterator(root, error);
    const std::filesystem::recursive_directory_iterator end;
    while (!error && iterator != end) {
        const std::string name = iterator->path().filename().string();
        if (name.find(kBackupMarker) != std::string::npos ||
            name.find(kJournalMarker) != std::string::npos) {
            ++count;
        }
        iterator.increment(error);
    }
    if (error) {
        throw std::runtime_error(
            "failed to enumerate test Rename backups under " + root.string());
    }
    return count;
}

void write_text_file(const std::filesystem::path& path, const std::string& text) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file) {
        throw std::runtime_error("failed to create " + path.string());
    }
    file << text;
    if (!file) {
        throw std::runtime_error("failed to write " + path.string());
    }
}

struct EfbPeekProbe {
    std::uint16_t x = 0;
    std::uint16_t y = 0;
    bool depth = false;
    std::uint32_t value = 0;
    std::uint32_t calls = 0;
    galaxy::GuestMemoryV1* memory = nullptr;
};

constexpr std::uint32_t efb_peek_addr(
    std::uint16_t x,
    std::uint16_t y,
    bool depth) {
    return 0xC8000000u |
        (static_cast<std::uint32_t>(x) << 2u) |
        (static_cast<std::uint32_t>(y) << 12u) |
        (depth ? (1u << 22u) : 0u);
}

bool efb_peek_probe_callback(
    void* user,
    galaxy::GuestMemoryV1* memory,
    std::uint16_t x,
    std::uint16_t y,
    bool depth,
    std::uint32_t* value) {
    auto* probe = static_cast<EfbPeekProbe*>(user);
    if (probe == nullptr || value == nullptr) {
        return false;
    }
    ++probe->calls;
    probe->x = x;
    probe->y = y;
    probe->depth = depth;
    probe->memory = memory;
    *value = probe->value;
    return true;
}

bool efb_peek_failing_callback(
    void* user,
    galaxy::GuestMemoryV1* memory,
    std::uint16_t x,
    std::uint16_t y,
    bool depth,
    std::uint32_t* value) {
    auto* probe = static_cast<EfbPeekProbe*>(user);
    if (probe != nullptr) {
        ++probe->calls;
        probe->x = x;
        probe->y = y;
        probe->depth = depth;
        probe->memory = memory;
    }
    if (value != nullptr) {
        *value = 0xDEADBEEFu;
    }
    return false;
}

[[noreturn]] void fatal_throw(
    void*,
    std::uint32_t,
    const char* message) {
    throw std::runtime_error(message != nullptr ? message : "fatal");
}

class ScopedWideEnv {
public:
    explicit ScopedWideEnv(const wchar_t* name) : name_(name) {
        save_old();
        _wputenv_s(name_, L"");
    }

    ScopedWideEnv(const wchar_t* name, const std::filesystem::path& value)
        : name_(name) {
        save_old();
        _wputenv_s(name_, value.c_str());
    }

    ~ScopedWideEnv() {
        _wputenv_s(name_, had_value_ ? old_value_.c_str() : L"");
    }

    ScopedWideEnv(const ScopedWideEnv&) = delete;
    ScopedWideEnv& operator=(const ScopedWideEnv&) = delete;

private:
    void save_old() {
        wchar_t* old = nullptr;
        std::size_t old_length = 0;
        if (_wdupenv_s(&old, &old_length, name_) == 0 && old != nullptr) {
            had_value_ = true;
            old_value_ = old;
            std::free(old);
        }
    }
    const wchar_t* name_;
    bool had_value_ = false;
    std::wstring old_value_;
};

class ScopedEnv {
public:
    ScopedEnv(const char* name, const char* value) : name_(name) {
        save_old();
        _putenv_s(name_, value);
    }

    ~ScopedEnv() {
        _putenv_s(name_, had_value_ ? old_value_.c_str() : "");
    }

    ScopedEnv(const ScopedEnv&) = delete;
    ScopedEnv& operator=(const ScopedEnv&) = delete;

private:
    void save_old() {
        char* old = nullptr;
        std::size_t old_length = 0;
        if (_dupenv_s(&old, &old_length, name_) == 0 && old != nullptr) {
            had_value_ = true;
            old_value_ = old;
            std::free(old);
        }
    }

    const char* name_;
    bool had_value_ = false;
    std::string old_value_;
};

class ScopedStreamRedirect {
public:
    ScopedStreamRedirect(std::ostream& stream, std::streambuf* replacement)
        : stream_(stream), old_(stream.rdbuf(replacement)) {}

    ~ScopedStreamRedirect() {
        stream_.rdbuf(old_);
    }

    ScopedStreamRedirect(const ScopedStreamRedirect&) = delete;
    ScopedStreamRedirect& operator=(const ScopedStreamRedirect&) = delete;

private:
    std::ostream& stream_;
    std::streambuf* old_;
};

struct ProcessExitHidBackendState {
    std::atomic<bool> open_called{};
    std::atomic<bool> stop_requested{};
    std::atomic<bool> close_called{};
};

class ProcessExitHidBackend final
    : public galaxy::input::NativeHidIoBackend {
public:
    explicit ProcessExitHidBackend(ProcessExitHidBackendState& state)
        : state_(state) {}

    void open() override {
        state_.open_called.store(true, std::memory_order_release);
    }

    bool poll_input(std::vector<std::byte>&) override {
        return false;
    }

    bool write_output(std::span<const std::byte>) override {
        return !state_.stop_requested.load(std::memory_order_acquire);
    }

    void request_stop() noexcept override {
        state_.stop_requested.store(true, std::memory_order_release);
    }

    void close() noexcept override {
        state_.close_called.store(true, std::memory_order_release);
    }

private:
    ProcessExitHidBackendState& state_;
};

void publish_fake_renderer_content(const galaxy::host::HostPointerState& state) noexcept {
    (void)galaxy::g_presentation_content.publish(state.client_width,state.client_height,
        galaxy::fit_content_viewport(state.client_width,state.client_height));
}

galaxy::host::HostPointerState centered_host_pointer_provider() noexcept {
    galaxy::host::HostPointerState state{};
    state.window_focused = true;
    state.inside_client = true;
    state.absolute_valid = true;
    state.client_x = 320;
    state.client_y = 240;
    state.client_width = 641;
    state.client_height = 481;
    state.absolute_sequence = 1;
    state.absolute_acquired_ms = 90'000u;
    state.debug_flags = 0x40;
    state.debug_window = static_cast<std::uintptr_t>(0x1234u);
    publish_fake_renderer_content(state);
    return state;
}

galaxy::host::HostPointerState right_of_center_host_pointer_provider()
    noexcept {
    galaxy::host::HostPointerState state{};
    state.window_focused = true;
    state.inside_client = true;
    state.absolute_valid = true;
    state.client_x = 400;
    state.client_y = 240;
    state.client_width = 641;
    state.client_height = 481;
    state.absolute_sequence = 3;
    state.absolute_acquired_ms = 90'003u;
    state.debug_flags = 0x40;
    state.debug_window = static_cast<std::uintptr_t>(0x1234u);
    publish_fake_renderer_content(state);
    return state;
}

galaxy::host::HostPointerState right_edge_host_pointer_provider() noexcept {
    galaxy::host::HostPointerState state{};
    state.window_focused = true;
    state.inside_client = true;
    state.absolute_valid = true;
    state.client_x = 640;
    state.client_y = 240;
    state.client_width = 641;
    state.client_height = 481;
    state.absolute_sequence = 4;
    state.absolute_acquired_ms = 90'004u;
    state.debug_flags = 0x40;
    state.debug_window = static_cast<std::uintptr_t>(0x1234u);
    publish_fake_renderer_content(state);
    return state;
}

galaxy::host::HostPointerState g_timed_host_pointer{};

galaxy::host::HostPointerState timed_host_pointer_provider() noexcept {
    publish_fake_renderer_content(g_timed_host_pointer);
    return g_timed_host_pointer;
}

std::uint64_t g_test_host_pointer_now_ms = 0u;

std::uint64_t test_host_pointer_clock() noexcept {
    return g_test_host_pointer_now_ms;
}

struct NativeVirtualInputDeliveryCapture {
    static constexpr std::size_t kCapacity = 64u;

    galaxy::host::GuestAddressSpace* address_space{};
    galaxy::input::NativeInputCausalityTracker* causality_tracker{};
    std::array<galaxy::host::NativeVirtualInputAclDeliveryIdentity, kCapacity>
        identities{};
    std::array<bool, kCapacity> fifo_occupied_during_callback{};
    std::array<bool, kCapacity> ios_reply_visible_during_callback{};
    std::size_t count{};
    bool overflow{};
};

void capture_native_virtual_input_delivery(
    void* user,
    const galaxy::host::NativeVirtualInputAclDeliveryIdentity& identity)
    noexcept {
    auto* capture =
        static_cast<NativeVirtualInputDeliveryCapture*>(user);
    if (capture == nullptr || capture->address_space == nullptr ||
        capture->count >= capture->identities.size()) {
        if (capture != nullptr) {
            capture->overflow = true;
        }
        return;
    }

    const std::size_t index = capture->count++;
    capture->identities[index] = identity;
    capture->fifo_occupied_during_callback[index] =
        capture->address_space->native_hid_fifo_occupied();
    capture->ios_reply_visible_during_callback[index] =
        capture->address_space->read_u32(identity.ios_request) == 8u;
    if (capture->causality_tracker != nullptr) {
        capture->causality_tracker->observe_acl_delivery(identity);
    }
}

std::uint64_t expected_hid_payload_fingerprint(
    std::span<const std::uint8_t> framed_acl) {
    constexpr std::size_t kAclAndL2capHeaderBytes = 8u;
    constexpr std::uint64_t kFnv1a64OffsetBasis =
        14'695'981'039'346'656'037ull;
    constexpr std::uint64_t kFnv1a64Prime = 1'099'511'628'211ull;
    if (framed_acl.size() < kAclAndL2capHeaderBytes + 2u) {
        throw std::runtime_error(
            "test ACL packet is too short for a HID payload fingerprint");
    }
    std::uint64_t fingerprint = kFnv1a64OffsetBasis;
    for (const std::uint8_t value :
         framed_acl.subspan(kAclAndL2capHeaderBytes)) {
        fingerprint ^= static_cast<std::uint64_t>(value);
        fingerprint *= kFnv1a64Prime;
    }
    return fingerprint;
}

class ScopedHostPointerProvider {
public:
    explicit ScopedHostPointerProvider(
        galaxy::host::HostPointerProvider provider) {
        galaxy::host::install_host_pointer_provider(provider);
    }

    ~ScopedHostPointerProvider() {
        galaxy::host::install_host_pointer_provider(nullptr);
    }

    ScopedHostPointerProvider(const ScopedHostPointerProvider&) = delete;
    ScopedHostPointerProvider& operator=(const ScopedHostPointerProvider&) =
        delete;
};

class ScopedHostPointerClock {
public:
    explicit ScopedHostPointerClock(std::uint64_t now_ms) {
        g_test_host_pointer_now_ms = now_ms;
        galaxy::host::install_host_pointer_clock(&test_host_pointer_clock);
    }

    ~ScopedHostPointerClock() {
        galaxy::host::install_host_pointer_clock(nullptr);
    }

    ScopedHostPointerClock(const ScopedHostPointerClock&) = delete;
    ScopedHostPointerClock& operator=(const ScopedHostPointerClock&) = delete;

    void set(std::uint64_t now_ms) const noexcept {
        g_test_host_pointer_now_ms = now_ms;
    }
};

galaxy::host::HostPointerEventRing* g_test_pointer_transitions = nullptr;

galaxy::host::HostPointerTransitionPoll test_pointer_transition_provider(
    std::uint64_t after_sequence) noexcept {
    if (g_test_pointer_transitions == nullptr) {
        return {};
    }
    return g_test_pointer_transitions->read_after(after_sequence);
}

class ScopedHostPointerTransitionProvider {
public:
    explicit ScopedHostPointerTransitionProvider(
        galaxy::host::HostPointerEventRing& transitions) {
        g_test_pointer_transitions = &transitions;
        galaxy::host::install_host_pointer_transition_provider(
            &test_pointer_transition_provider);
    }

    ~ScopedHostPointerTransitionProvider() {
        galaxy::host::install_host_pointer_transition_provider(nullptr);
        g_test_pointer_transitions = nullptr;
    }

    ScopedHostPointerTransitionProvider(
        const ScopedHostPointerTransitionProvider&) = delete;
    ScopedHostPointerTransitionProvider& operator=(
        const ScopedHostPointerTransitionProvider&) = delete;
};

void submit_ios_request(
    galaxy::GuestMemoryV1* memory,
    std::uint32_t request) {
    galaxy::guest_store_u32(
        memory, 0xCD000000, request, nullptr, 0x80004000);
    galaxy::guest_store_u32(
        memory, 0xCD000004, 0x31, nullptr, 0x80004000);
}

void send_dsp_mail(
    galaxy::GuestMemoryV1* memory,
    std::uint32_t mail) {
    galaxy::guest_store_u16(
        memory,
        0xCC005000,
        static_cast<std::uint16_t>(mail >> 16),
        nullptr,
        0x80004000);
    galaxy::guest_store_u16(
        memory,
        0xCC005002,
        static_cast<std::uint16_t>(mail),
        nullptr,
        0x80004000);
}

void drain_dsp_mailbox(galaxy::GuestMemoryV1* memory) {
    for (int i = 0; i < 32; ++i) {
        const std::uint16_t high =
            galaxy::guest_load_u16(
                memory, 0xCC005004, nullptr, 0x80004000);
        if ((high & 0x8000u) == 0) {
            return;
        }
        static_cast<void>(
            galaxy::guest_load_u16(
                memory, 0xCC005006, nullptr, 0x80004000));
    }
}

void release_dsp_sync_subframe(galaxy::GuestMemoryV1* memory) {
    send_dsp_mail(memory, 0);
    send_dsp_mail(memory, 0x00030000);
}

void render_dsp_sync_2ch(
    galaxy::GuestMemoryV1* memory,
    std::uint32_t command,
    std::uint32_t left,
    std::uint32_t right) {
    send_dsp_mail(memory, 3);
    send_dsp_mail(memory, command);
    send_dsp_mail(memory, left);
    send_dsp_mail(memory, right);
    release_dsp_sync_subframe(memory);
}

void render_dsp_sync_4ch(
    galaxy::GuestMemoryV1* memory,
    std::uint32_t command,
    std::uint32_t left,
    std::uint32_t right,
    std::uint32_t aux_a,
    std::uint32_t aux_b) {
    send_dsp_mail(memory, 5);
    send_dsp_mail(memory, command);
    send_dsp_mail(memory, left);
    send_dsp_mail(memory, right);
    send_dsp_mail(memory, aux_a);
    send_dsp_mail(memory, aux_b);
    release_dsp_sync_subframe(memory);
}

std::int16_t load_guest_s16(galaxy::GuestMemoryV1* memory, std::uint32_t address) {
    return static_cast<std::int16_t>(
        galaxy::guest_load_u16(memory, address, nullptr, 0x80004000));
}

std::uint32_t ipc_control(galaxy::GuestMemoryV1* memory) {
    return galaxy::guest_load_u32(memory, 0xCD000004, nullptr, 0x80004000);
}

bool ios_reply_available(galaxy::GuestMemoryV1* memory) {
    return (ipc_control(memory) & 0x04u) != 0;
}

void acknowledge_ios_ack(galaxy::GuestMemoryV1* memory) {
    const std::uint32_t enables = ipc_control(memory) & 0x30u;
    galaxy::guest_store_u32(
        memory, 0xCD000004, enables | 0x02u, nullptr, 0x80004000);
}

void acknowledge_ios_reply(galaxy::GuestMemoryV1* memory) {
    const std::uint32_t enables = ipc_control(memory) & 0x30u;
    galaxy::guest_store_u32(
        memory, 0xCD000004, enables | 0x06u, nullptr, 0x80004000);
    galaxy::guest_store_u32(
        memory, 0xCD000030, 0x40000000u, nullptr, 0x80004000);
}

void acknowledge_ios_reply_register_only(galaxy::GuestMemoryV1* memory) {
    const std::uint32_t enables = ipc_control(memory) & 0x30u;
    galaxy::guest_store_u32(
        memory, 0xCD000004, enables | 0x06u, nullptr, 0x80004000);
}

bool expect(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
        // Many protocol checks intentionally capture std::cerr traces. Keep a
        // failure visible to CTest even when it occurs inside such a scope.
        std::cout << "FAILED: " << message << '\n';
    }
    return condition;
}

class ScopedHostOwnershipTestHook {
public:
    explicit ScopedHostOwnershipTestHook(galaxy::host::NativeHostOwnershipTestHook hook) {
        galaxy::host::native_host_set_ownership_test_hook(hook);
    }
    ~ScopedHostOwnershipTestHook() {
        galaxy::host::native_host_set_ownership_test_hook(nullptr);
    }
    ScopedHostOwnershipTestHook(const ScopedHostOwnershipTestHook&) = delete;
    ScopedHostOwnershipTestHook& operator=(const ScopedHostOwnershipTestHook&) = delete;
};

std::filesystem::path ownership_collision_path;
bool inject_host_ownership_failure(unsigned phase,
    const std::filesystem::path& path, unsigned attempt) {
    if (phase == 0u || (phase == 1u && attempt == 1u)) {
        throw std::bad_alloc();
    }
    if (phase == 2u && attempt == 0u) {
        ownership_collision_path = path;
        constexpr std::array<std::byte, 3> sentinel{
            std::byte{0x31}, std::byte{0x72}, std::byte{0xA9}};
        write_binary_file(path, sentinel);
    }
    return false;
}

bool inject_readdir_increment_failure(unsigned phase,
    const std::filesystem::path&, unsigned) {
    return phase == 3u;
}

bool native_host_failure_ownership_works(const std::filesystem::path& test_root) {
    using namespace galaxy::host;
    bool passed = expect(native_host_test_handle_cleanup(),
        "allocation unwinding releases HANDLE and HDEVINFO owners");
    const auto root = test_root / L"ownership_failures";
    std::filesystem::create_directories(root);
    const auto legacy = root / L"legacy.bin";
    constexpr std::array<std::byte, 3> sentinel{
        std::byte{0x31}, std::byte{0x72}, std::byte{0xA9}};
    write_binary_file(legacy, sentinel);
    bool threw_allocation = false;
    {
        ScopedHostOwnershipTestHook hook(&inject_host_ownership_failure);
        try { native_host_test_attach_nand_metadata(legacy); }
        catch (const std::bad_alloc&) { threw_allocation = true; }
    }
    const auto moved = root / L"legacy-moved.bin";
    passed &= expect(threw_allocation &&
        MoveFileExW(legacy.c_str(), moved.c_str(), 0u) != FALSE,
        "migration allocation failure releases the no-delete identity lease");
    passed &= expect(read_binary_file(moved) ==
        std::vector<std::byte>(sentinel.begin(), sentinel.end()),
        "migration unwinding preserves legacy bytes");
    ownership_collision_path.clear();
    {
        ScopedHostOwnershipTestHook hook(&inject_host_ownership_failure);
        passed &= expect(NativeNandTestAccess::persist_new_file(
            root, root / L"collision.bin") == static_cast<std::uint32_t>(-102),
            "candidate collision followed by allocation failure returns Access");
    }
    passed &= expect(!ownership_collision_path.empty() &&
        read_binary_file(ownership_collision_path) ==
            std::vector<std::byte>(sentinel.begin(), sentinel.end()) &&
        !std::filesystem::exists(root / L"collision.bin"),
        "unowned colliding candidate survives failure without destination publication");

    constexpr std::array<std::byte, 8> pcm{
        std::byte{1}, std::byte{2}, std::byte{3}, std::byte{4},
        std::byte{5}, std::byte{6}, std::byte{7}, std::byte{8}};
    ScopedEnv keep_silence("GALAXY_AUDIO_WAV_DUMP_SKIP_LEADING_SILENCE", "0");
    const auto good_wav = root / L"good.wav";
    {
        ScopedWideEnv dump_path(L"GALAXY_AUDIO_WAV_DUMP", good_wav);
        passed &= expect(native_host_test_wav_dump(pcm, 32000u, false),
            "WAV finalization reports successful header and payload publication");
    }
    const auto wav = read_binary_file(good_wav);
    passed &= expect(wav.size() == 52u && wav[4] == std::byte{44} &&
        wav[5] == std::byte{0} && wav[40] == std::byte{8} &&
        wav[41] == std::byte{0} &&
        std::equal(pcm.begin(), pcm.end(), wav.begin() + 44u),
        "finalized WAV independently contains RIFF/data sizes and exact PCM");
    {
        ScopedWideEnv dump_path(L"GALAXY_AUDIO_WAV_DUMP", root / L"failed.wav");
        std::ostringstream output;
        ScopedStreamRedirect capture(std::cout, output.rdbuf());
        passed &= expect(!native_host_test_wav_dump(pcm, 32000u, true) &&
            output.str().find("WAV dump wrote") == std::string::npos,
            "failed WAV finalization cannot publish a successful wrote message");
    }
    {
        // A file cannot serve as the parent directory for a diagnostic artifact.
        ScopedWideEnv dump_path(L"GALAXY_AUDIO_WAV_DUMP", moved / L"failed.wav");
        passed &= expect(!native_host_test_wav_dump(pcm, 32000u, false),
            "initial WAV artifact failure propagates as failure");
    }
    std::error_code error;
    std::filesystem::remove_all(root, error);
    return passed;
}

std::array<galaxy::host::NativeIosClockSample, 2> ios_trace_test_clock{};
std::size_t ios_trace_test_clock_calls{};

galaxy::host::NativeIosClockSample sample_ios_trace_test_clock() noexcept {
    const auto index = ios_trace_test_clock_calls++;
    return index < ios_trace_test_clock.size()
        ? ios_trace_test_clock[index] : galaxy::host::NativeIosClockSample{};
}

bool native_ios_anomaly_identity_and_bounds_work() {
    using namespace galaxy::host;
    bool passed = true;
    NativeIosAnomalyLedger ledger;
    NativeIosRequestIdentity identity{};
    identity.request = 0x133E0120u;
    identity.command = 4u;
    identity.handle = 17u;
    identity.device = NativeIosDevice::Nand;
    identity.arguments = {0x80004000u, 0xBE00u, 0u, 0u, 0u};
    identity.arguments_valid = true;
    ios_trace_test_clock_calls = 0u;
    ios_trace_test_clock = {{
        {100u, 200u, 900u, 400u, true, true},
        {5'000'000u, 5'000'100u, 900u, 500u, true, true}}};
    {
        NativeIosRequestTrace trace(ledger, identity, &sample_ios_trace_test_clock);
        // A real IOS reply overwrites command/handle words. Capture must own
        // the original identity and not observe the replacement on return.
        identity.command = 8u;
        identity.handle = 4u;
        identity.arguments[1] = 0u;
    }
    passed &= expect(ios_trace_test_clock_calls == 2u && ledger.started() == 1u &&
        ledger.finished() == 1u && ledger.records().size() == 1u,
        "IOS trace samples exactly two bracketed endpoints and includes threshold equality");
    if (!ledger.records().empty()) {
        const auto& first = ledger.records().front();
        passed &= expect(first.identity.command == 4u && first.identity.handle == 17u &&
            first.identity.arguments[1] == 0xBE00u && first.identity.arguments_valid &&
            first.minimum_elapsed_ns() == 4'999'800u &&
            first.maximum_elapsed_ns() == NativeIosAnomalyLedger::kThresholdNs &&
            first.entry.cpu_valid && first.exit.cpu_valid &&
            first.entry.cpu_100ns == first.exit.cpu_100ns && !first.unwound,
            "IOS immutable identity, endpoint uncertainty and valid quantized CPU delta survive capture");
    }
    // Hundreds of ordinary requests must not evict the first slow request.
    for (std::uint64_t i = 0; i < 300u; ++i) {
        NativeIosAnomalyRecord ordinary{};
        ordinary.sequence = ledger.begin();
        ordinary.entry = {10u, 20u};
        ordinary.exit = {4'999'999u, 5'000'009u};
        ledger.finish(ordinary);
    }
    passed &= expect(ledger.records().size() == 1u && ledger.slow() == 1u,
        "IOS requests strictly below the outer elapsed threshold do not evict retained anomalies");
    ios_trace_test_clock_calls = 0u;
    ios_trace_test_clock = {{{10u, 20u}, {6'000'010u, 6'000'020u}}};
    bool original_exception = false;
    try {
        NativeIosRequestTrace trace(ledger, identity, &sample_ios_trace_test_clock);
        throw 47;
    } catch (int error) {
        original_exception = error == 47;
    }
    passed &= expect(original_exception && ledger.unwound() == 1u &&
        ledger.records().back().unwound &&
        !ledger.records().back().entry.cpu_valid &&
        !ledger.records().back().exit.cycles_valid,
        "IOS tracing closes on the original exception and preserves unavailable counters");
    for (std::uint64_t i = 0; i < 40u; ++i) {
        NativeIosAnomalyRecord slow{};
        slow.sequence = ledger.begin();
        slow.entry = {10u, 20u};
        slow.exit = {6'000'010u, 6'000'020u};
        ledger.finish(slow);
    }
    NativeIosAnomalyRecord invalid{};
    invalid.sequence = ledger.begin();
    invalid.entry = {20u, 30u};
    invalid.exit = {10u, 15u};
    ledger.finish(invalid);
    passed &= expect(ledger.started() == 343u && ledger.finished() == 343u &&
        ledger.slow() == 42u && ledger.invalid_wall() == 1u &&
        ledger.records().size() == NativeIosAnomalyLedger::kCapacity &&
        ledger.overflow() == 11u && ledger.records().front().sequence == 1u &&
        ledger.records().back().sequence == 332u,
        "IOS overflow conserves slow and unknown observations while preserving first completion order");
    static_cast<void>(ledger.begin());
    passed &= expect(ledger.started() - ledger.finished() == 1u,
        "IOS started versus finished exposes an unclosed request instead of claiming complete capture");
    return passed;
}

template <typename Predicate>
bool wait_for_native_dsp_test(
    Predicate predicate,
    std::chrono::milliseconds timeout) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (!predicate()) {
        if (std::chrono::steady_clock::now() >= deadline) {
            return false;
        }
        std::this_thread::sleep_for(std::chrono::microseconds(100));
    }
    return true;
}

struct NativeDspMramServiceProbe {
    std::atomic<std::uint32_t> wake_count{};
    std::atomic<std::uint32_t> dirty_count{};
    std::atomic<std::uint32_t> dirty_address{};
    std::atomic<std::uint32_t> dirty_size{};
};

NativeDspMramServiceProbe* g_native_dsp_mram_service_probe = nullptr;

bool publish_native_dsp_mram_test_wake(void* user) noexcept {
    auto& probe = *static_cast<NativeDspMramServiceProbe*>(user);
    probe.wake_count.fetch_add(1u, std::memory_order_release);
    return true;
}

void capture_native_dsp_mram_dirty_range(
    void* /*user*/,
    std::uint32_t address,
    std::uint32_t size) {
    NativeDspMramServiceProbe* const probe =
        g_native_dsp_mram_service_probe;
    if (probe == nullptr) {
        return;
    }
    probe->dirty_address.store(address, std::memory_order_relaxed);
    probe->dirty_size.store(size, std::memory_order_relaxed);
    probe->dirty_count.fetch_add(1u, std::memory_order_release);
}

template <typename Fn>
bool expect_runtime_error(Fn&& fn, const char* message) {
    try {
        fn();
    } catch (const std::runtime_error&) {
        return true;
    }
    std::cerr << "FAILED: " << message << '\n';
    std::cout << "FAILED: " << message << '\n';
    return false;
}

template <typename Fn>
bool expect_runtime_error_message(
    Fn&& fn,
    std::string_view expected_error,
    const char* message) {
    try {
        fn();
    } catch (const std::runtime_error& error) {
        if (std::string_view(error.what()) == expected_error) {
            return true;
        }
        std::cerr << "FAILED: " << message << " (expected error '"
                  << expected_error << "', got '" << error.what() << "')\n";
        std::cout << "FAILED: " << message << " (wrong runtime_error)\n";
        return false;
    }
    std::cerr << "FAILED: " << message << " (no runtime_error)\n";
    std::cout << "FAILED: " << message << " (no runtime_error)\n";
    return false;
}

template <typename Fn>
bool expect_dsp_hard_trap(Fn&& fn, const char* message) {
    try {
        fn();
    } catch (const galaxy::DspHardTrap&) {
        return true;
    }
    return expect(false, message);
}

bool expect_ios_reply(
    const galaxy::host::GuestAddressSpace& memory,
    galaxy::GuestMemoryV1* guest_memory,
    std::uint32_t request,
    std::uint32_t expected_result,
    std::uint32_t original_command,
    const char* message) {
    const bool ok =
        memory.read_u32(request) == 8 &&
        memory.read_u32(request + 4) == expected_result &&
        memory.read_u32(request + 8) == original_command &&
        galaxy::guest_load_u32(
            guest_memory, 0xCD000008, nullptr, 0x80004000) == request &&
        ios_reply_available(guest_memory);
    if (!ok) {
        std::cerr << "FAILED: " << message << '\n';
        std::cout << "FAILED: " << message << '\n';
    }
    return ok;
}

std::uint32_t ios_open_path(
    galaxy::host::GuestAddressSpace& memory,
    galaxy::GuestMemoryV1* guest_memory,
    std::uint32_t request,
    std::uint32_t path_address,
    std::string_view path,
    std::uint32_t mode = 0u) {
    std::vector<std::byte> bytes;
    bytes.reserve(path.size() + 1u);
    for (const char ch : path) {
        bytes.push_back(static_cast<std::byte>(ch));
    }
    bytes.push_back(std::byte{0});
    memory.copy(path_address, bytes);
    memory.write_u32(request, 1);
    memory.write_u32(request + 0x08, 0);
    memory.write_u32(request + 0x0C, path_address);
    memory.write_u32(request + 0x10, mode);
    memory.write_u32(request + 0x20, 0);
    submit_ios_request(guest_memory, request);
    const std::uint32_t handle = memory.read_u32(request + 4);
    acknowledge_ios_reply(guest_memory);
    return handle;
}

void ios_read_request(
    galaxy::host::GuestAddressSpace& memory,
    galaxy::GuestMemoryV1* guest_memory,
    std::uint32_t request,
    std::uint32_t handle,
    std::uint32_t buffer,
    std::uint32_t length) {
    memory.write_u32(request, 3);
    memory.write_u32(request + 0x08, handle);
    memory.write_u32(request + 0x0C, buffer);
    memory.write_u32(request + 0x10, length);
    memory.write_u32(request + 0x20, 0);
    submit_ios_request(guest_memory, request);
}

void ios_write_request(
    galaxy::host::GuestAddressSpace& memory,
    galaxy::GuestMemoryV1* guest_memory,
    std::uint32_t request,
    std::uint32_t handle,
    std::uint32_t buffer,
    std::uint32_t length) {
    memory.write_u32(request, 4);
    memory.write_u32(request + 0x08, handle);
    memory.write_u32(request + 0x0C, buffer);
    memory.write_u32(request + 0x10, length);
    memory.write_u32(request + 0x20, 0);
    submit_ios_request(guest_memory, request);
}

void ios_seek_request(
    galaxy::host::GuestAddressSpace& memory,
    galaxy::GuestMemoryV1* guest_memory,
    std::uint32_t request,
    std::uint32_t handle,
    std::uint32_t offset,
    std::uint32_t whence) {
    memory.write_u32(request, 5);
    memory.write_u32(request + 0x08, handle);
    memory.write_u32(request + 0x0C, offset);
    memory.write_u32(request + 0x10, whence);
    memory.write_u32(request + 0x20, 0);
    submit_ios_request(guest_memory, request);
}

void ios_close_request(
    galaxy::host::GuestAddressSpace& memory,
    galaxy::GuestMemoryV1* guest_memory,
    std::uint32_t request,
    std::uint32_t handle) {
    memory.write_u32(request, 2);
    memory.write_u32(request + 0x08, handle);
    memory.write_u32(request + 0x20, 0);
    submit_ios_request(guest_memory, request);
}

void ios_ioctl_request(
    galaxy::host::GuestAddressSpace& memory,
    galaxy::GuestMemoryV1* guest_memory,
    std::uint32_t request,
    std::uint32_t handle,
    std::uint32_t ioctl,
    std::uint32_t input,
    std::uint32_t input_length,
    std::uint32_t output,
    std::uint32_t output_length) {
    memory.write_u32(request, 6u);
    memory.write_u32(request + 0x08u, handle);
    memory.write_u32(request + 0x0Cu, ioctl);
    memory.write_u32(request + 0x10u, input);
    memory.write_u32(request + 0x14u, input_length);
    memory.write_u32(request + 0x18u, output);
    memory.write_u32(request + 0x1Cu, output_length);
    memory.write_u32(request + 0x20u, 0u);
    submit_ios_request(guest_memory, request);
}

void ios_ioctlv_request(
    galaxy::host::GuestAddressSpace& memory,
    galaxy::GuestMemoryV1* guest_memory,
    std::uint32_t request,
    std::uint32_t handle,
    std::uint32_t ioctl,
    std::uint32_t input_count,
    std::uint32_t io_count,
    std::uint32_t vectors) {
    memory.write_u32(request, 7u);
    memory.write_u32(request + 0x08u, handle);
    memory.write_u32(request + 0x0Cu, ioctl);
    memory.write_u32(request + 0x10u, input_count);
    memory.write_u32(request + 0x14u, io_count);
    memory.write_u32(request + 0x18u, vectors);
    memory.write_u32(request + 0x20u, 0u);
    submit_ios_request(guest_memory, request);
}

void write_isfs_path_buffer(
    galaxy::host::GuestAddressSpace& memory,
    std::uint32_t address,
    std::string_view path) {
    constexpr std::uint32_t kPathBufferSize = 64u;
    if (path.size() >= kPathBufferSize) {
        throw std::invalid_argument("test ISFS path does not fit its path buffer");
    }
    memory.clear(address, kPathBufferSize);
    std::vector<std::byte> path_bytes;
    path_bytes.reserve(path.size() + 1u);
    for (const char value : path) {
        path_bytes.push_back(static_cast<std::byte>(value));
    }
    path_bytes.push_back(std::byte{0});
    memory.copy(address, path_bytes);
}

void write_isfs_attribute_block(
    galaxy::host::GuestAddressSpace& memory,
    std::uint32_t address,
    std::string_view path,
    std::uint8_t owner_mode,
    std::uint8_t group_mode,
    std::uint8_t other_mode,
    std::uint8_t attribute) {
    constexpr std::uint32_t kAttributeBlockSize = 0x4Cu;
    memory.clear(address, kAttributeBlockSize);
    write_isfs_path_buffer(memory, address + 6u, path);
    *memory.pointer(address + 70u, 1u) = static_cast<std::byte>(owner_mode);
    *memory.pointer(address + 71u, 1u) = static_cast<std::byte>(group_mode);
    *memory.pointer(address + 72u, 1u) = static_cast<std::byte>(other_mode);
    *memory.pointer(address + 73u, 1u) = static_cast<std::byte>(attribute);
}

std::vector<std::byte> valid_rmge_game_data_for_test() {
    constexpr std::size_t kSize = 0xBE00u;
    constexpr std::array<std::string_view, 19> kNames{
        "mario1", "luigi1", "config1", "mario2", "luigi2", "config2",
        "mario3", "luigi3", "config3", "mario4", "luigi4", "config4",
        "mario5", "luigi5", "config5", "mario6", "luigi6", "config6",
        "sysconf",
    };
    constexpr std::array<std::uint32_t, 19> kOffsets{
        0x140u, 0x10C0u, 0x2040u, 0x20A0u, 0x3020u, 0x3FA0u,
        0x4000u, 0x4F80u, 0x5F00u, 0x5F60u, 0x6EE0u, 0x7E60u,
        0x7EC0u, 0x8E40u, 0x9DC0u, 0x9E20u, 0xADA0u, 0xBD20u,
        0xBD80u,
    };

    std::vector<std::byte> bytes(kSize);
    for (std::size_t i = 0; i < bytes.size(); ++i) {
        bytes[i] = static_cast<std::byte>(
            (i * 37u + ((i >> 8u) * 13u) + 0x5Au) & 0xFFu);
    }
    write_be32(bytes, 4u, 2u);
    write_be32(bytes, 8u, static_cast<std::uint32_t>(kNames.size()));
    write_be32(bytes, 12u, static_cast<std::uint32_t>(bytes.size()));
    for (std::size_t i = 0; i < kNames.size(); ++i) {
        const std::size_t info = 0x10u + i * 0x10u;
        std::fill_n(bytes.begin() + info, 12u, std::byte{0});
        for (std::size_t j = 0; j < kNames[i].size(); ++j) {
            bytes[info + j] = static_cast<std::byte>(kNames[i][j]);
        }
        write_be32(bytes, info + 12u, kOffsets[i]);
    }
    std::uint16_t sum = 0u;
    std::uint16_t inverse_sum = 0u;
    for (std::size_t offset = 4u; offset < bytes.size(); offset += 2u) {
        const std::uint16_t word = static_cast<std::uint16_t>(
            (std::to_integer<std::uint16_t>(bytes[offset]) << 8u) |
            std::to_integer<std::uint16_t>(bytes[offset + 1u]));
        sum = static_cast<std::uint16_t>(sum + word);
        inverse_sum = static_cast<std::uint16_t>(
            inverse_sum + static_cast<std::uint16_t>(~word));
    }
    write_be32(
        bytes,
        0u,
        (static_cast<std::uint32_t>(sum) << 16u) | inverse_sum);
    return bytes;
}

std::vector<std::byte> valid_rmge_banner_for_test(
    std::uint16_t icon_speed = 2u) {
    if (icon_speed != 2u && icon_speed != 3u) {
        throw std::invalid_argument("test RMGE banner icon speed must be 2 or 3");
    }
    std::vector<std::byte> bytes(0x72A0u, std::byte{0});
    bytes[0] = std::byte{'W'};
    bytes[1] = std::byte{'I'};
    bytes[2] = std::byte{'B'};
    bytes[3] = std::byte{'N'};
    write_be16(bytes, 8u, icon_speed);
    return bytes;
}

std::uint16_t rfl_crc_for_test(std::span<const std::byte> bytes) {
    std::uint16_t crc = 0u;
    for (const std::byte byte : bytes) {
        std::uint8_t data = std::to_integer<std::uint8_t>(byte);
        for (int bit = 0; bit < 8; ++bit, data <<= 1u) {
            crc = (crc & 0x8000u) != 0u
                ? static_cast<std::uint16_t>((crc << 1u) ^ 0x1021u)
                : static_cast<std::uint16_t>(crc << 1u);
            if ((data & 0x80u) != 0u) {
                crc = static_cast<std::uint16_t>(crc ^ 0x0001u);
            }
        }
    }
    return crc;
}

std::vector<std::byte> valid_empty_rfl_database_for_test() {
    constexpr std::size_t kDatabaseSize = 0x1F1E0u;
    constexpr std::size_t kIsolationOffset = 0x1CECu;
    constexpr std::size_t kHiddenOffset = 0x1D00u;
    constexpr std::size_t kHiddenTableOffset = kHiddenOffset + 0x08u;
    constexpr std::size_t kHiddenEntrySize = 0x0Cu;
    constexpr std::size_t kHiddenEntryCount = 10'000u;

    std::vector<std::byte> database(kDatabaseSize, std::byte{0});
    write_be32(database, 0x0000u, 0x524E4F44u);  // RNOD
    write_be32(database, kIsolationOffset, 0x80000000u);
    write_be32(database, kHiddenOffset, 0x524E4844u);  // RNHD
    write_be16(database, kHiddenOffset + 0x04u, 0xFFFFu);
    write_be16(database, kHiddenOffset + 0x06u, 0xFFFFu);
    for (std::size_t i = 0u; i < kHiddenEntryCount; ++i) {
        const std::size_t offset =
            kHiddenTableOffset + i * kHiddenEntrySize;
        write_be16(database, offset + 0x08u, 0x7FFFu);
        write_be16(database, offset + 0x0Au, 0x7FFFu);
    }
    write_be16(database, kDatabaseSize - 2u, 0u);
    write_be16(
        database,
        kDatabaseSize - 2u,
        rfl_crc_for_test(database));
    if (rfl_crc_for_test(database) != 0u) {
        throw std::runtime_error("test empty RFL database CRC construction failed");
    }
    return database;
}

struct StorageTestLatch {
    HANDLE entered{CreateEventW(nullptr, TRUE, FALSE, nullptr)};
    HANDLE released{CreateEventW(nullptr, TRUE, FALSE, nullptr)};
    HANDLE completed{CreateEventW(nullptr, TRUE, FALSE, nullptr)};
    std::atomic_uint calls{};
    std::atomic_uint wakes{};

    StorageTestLatch() {
        if (!entered || !released || !completed)
            throw std::runtime_error("storage test event creation failed");
    }
    ~StorageTestLatch() {
        SetEvent(released);
        CloseHandle(entered);
        CloseHandle(released);
        CloseHandle(completed);
    }
    static void before(void* user) {
        auto& latch = *static_cast<StorageTestLatch*>(user);
        latch.calls.fetch_add(1u);
        SetEvent(latch.entered);
        if (WaitForSingleObject(latch.released, 10'000u) != WAIT_OBJECT_0)
            throw std::runtime_error("storage test release timed out");
    }
    static void wake(void* user) noexcept {
        auto& latch = *static_cast<StorageTestLatch*>(user);
        latch.wakes.fetch_add(1u);
        SetEvent(latch.completed);
    }
    static void fail_after_commit(void*) {
        throw std::runtime_error("injected postcommit fatal");
    }
    bool wait_entered() const { return WaitForSingleObject(entered, 3'000u) == WAIT_OBJECT_0; }
    bool wait_completed() const { return WaitForSingleObject(completed, 3'000u) == WAIT_OBJECT_0; }
    void release() const { SetEvent(released); }
    void reset() const { ResetEvent(entered); ResetEvent(released); ResetEvent(completed); }
};

bool owned_storage_worker_publication_works() {
    struct Work {
        StorageTestLatch* latch{};
        std::uint32_t value{};
    };
    StorageTestLatch latch;
    galaxy::host::OwnedStorageWorker<Work> worker(+[](Work& work) {
        StorageTestLatch::before(work.latch);
        work.value = 0xAABBCCDDu;
    });
    worker.set_wake(&StorageTestLatch::wake, &latch);
    auto first = std::make_unique<Work>(Work{&latch, 1u});
    auto second = std::make_unique<Work>(Work{&latch, 2u});
    bool passed = expect(worker.submit(first) && !first && latch.wait_entered(),
        "owned worker accepts one physical transaction and transfers its owner");
    passed &= expect(!worker.submit(second) && second && !worker.completion_pending() &&
        !worker.take_completed().work, "running transaction retains capacity without an early receipt");
    latch.release();
    passed &= expect(latch.wait_completed() && worker.completion_pending(),
        "payload completion publishes a durable wake");
    passed &= expect(!worker.submit(second) && second,
        "completed but unconsumed transaction still owns the only slot");
    auto receipt = worker.take_completed();
    passed &= expect(receipt.work && receipt.work->value == 0xAABBCCDDu && !receipt.fatal &&
        !worker.take_completed().work && !worker.completion_pending(),
        "acquired receipt sees all worker writes exactly once");
    worker.set_wake(nullptr, nullptr);
    const auto wakes_before_shutdown = latch.wakes.load();
    passed &= expect(worker.submit(second), "consuming receipt returns worker capacity");
    worker.shutdown();
    receipt = worker.take_completed();
    passed &= expect(receipt.work && receipt.work->value == 0xAABBCCDDu &&
        latch.calls.load() == 2u && latch.wakes.load() == wakes_before_shutdown,
        "shutdown drains accepted work once after callback quiescence");
    return passed;
}

bool native_async_nand_ownership_works(const std::filesystem::path& shared_test_root) {
    using Access = galaxy::host::NativeNandTestAccess;
    constexpr std::uint32_t kRequest = 0x133E4300u;
    constexpr std::uint32_t kWrite = kRequest + 0x100u;
    constexpr std::uint32_t kHeld = kRequest + 0x140u;
    constexpr std::uint32_t kPath = 0x133E4000u;
    constexpr std::uint32_t kAttr = 0x133E4200u;
    constexpr std::uint32_t kBuffer = 0x13400000u;
    constexpr std::uint32_t kRead = kBuffer + 0x100u;
    constexpr std::string_view kName = "/tmp/async.bin";
    const std::vector<std::byte> payload{std::byte{0x12}, std::byte{0x34}, std::byte{0x56}};
    ScopedEnv enable_async("GALAXY_ASYNC_NAND_WRITES", "1");
    bool passed = true;
    for (unsigned scenario = 0; scenario < 10u; ++scenario) {
        const auto isolated_root = shared_test_root / ("async_ownership_" + std::to_string(scenario));
        ScopedWideEnv set_root(L"GALAXY_NAND_ROOT", isolated_root);
        // The latch outlives the address space and any shutdown join.
        StorageTestLatch latch;
        auto owner = std::make_unique<galaxy::host::GuestAddressSpace>();
        auto& memory = *owner;
        auto* guest = memory.guest_memory();
        const auto fs = ios_open_path(memory, guest, kRequest, kPath, "/dev/fs");
        write_isfs_attribute_block(memory, kAttr, kName, 3u, 3u, 0u, 0u);
        ios_ioctl_request(memory, guest, kRequest, fs, 9u, kAttr, 0x4Cu, 0u, 0u);
        passed &= expect(memory.read_u32(kRequest + 4u) == 0u,
            "async fixture durably creates generic file");
        acknowledge_ios_reply(guest);
        const auto fd = ios_open_path(memory, guest, kRequest, kPath, kName, 3u);
        const auto sibling = ios_open_path(memory, guest, kRequest, kPath, kName, 3u);
        const auto host_path = Access::nand_host_path(memory, kName);
        Access::set_persist_hooks(memory, &StorageTestLatch::before, &latch,
            scenario == 2u || scenario == 3u ? &StorageTestLatch::fail_after_commit : nullptr);
        memory.set_native_ios_completion_wake_callback(&StorageTestLatch::wake, &latch);
        memory.copy(kBuffer, payload);
        ios_write_request(memory, guest, kWrite, fd, kBuffer, 3u);
        passed &= expect(latch.wait_entered() && memory.read_u32(kWrite) == 4u &&
            !ios_reply_available(guest) && (ipc_control(guest) & 2u) != 0u &&
            Access::file_position(memory, fd) == 0u && Access::file_bytes(memory, sibling).empty(),
            "accepted write returns only ACK while guest backing, position and reply remain pending");
        acknowledge_ios_ack(guest);
        memory.clear(kBuffer, 3u); // Candidate owns the already submitted bytes.
        memory.service_native_ios_completions();
        passed &= expect(!ios_reply_available(guest) && !memory.native_ios_completion_pending(),
            "completion service never waits for a blocked persistence worker");

        HANDLE deny_replace = INVALID_HANDLE_VALUE;
        if (scenario == 1u) {
            deny_replace = CreateFileW(host_path.c_str(), GENERIC_READ,
                FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0u, nullptr);
            passed &= expect(deny_replace != INVALID_HANDLE_VALUE,
                "precommit failure fixture denies destination replacement");
        }
        if (scenario == 0u) {
            const auto bt = ios_open_path(memory, guest, kRequest, kPath, "/dev/usb/oh1/57e/305");
            passed &= expect(bt > 0u && bt < 0x80000000u,
                "independent Bluetooth IOS open completes while persistence is blocked");
            ios_read_request(memory, guest, kHeld, sibling, kRead, 3u);
        } else if (scenario == 4u) {
            memory.copy(kBuffer, payload);
            ios_write_request(memory, guest, kHeld, sibling, kBuffer, 3u);
            memory.clear(kBuffer, 3u); // Violates the held unacknowledged request contract.
        } else if (scenario == 5u) {
            memory.copy(kBuffer, payload);
            ios_write_request(memory, guest, kHeld, fd, kBuffer, 3u);
        } else if (scenario == 6u) {
            ios_close_request(memory, guest, kHeld, fd);
        } else if (scenario == 7u) {
            write_isfs_path_buffer(memory, kPath, kName);
            memory.clear(kRead, 0x4Cu);
            ios_ioctl_request(memory, guest, kHeld, fs, 6u, kPath, 64u, kRead, 0x4Cu);
        } else if (scenario == 8u) {
            ios_seek_request(memory, guest, kHeld, fd, 2u, 0u);
        }
        if (scenario == 0u || (scenario >= 4u && scenario <= 8u)) {
            passed &= expect((ipc_control(guest) & 2u) == 0u && !ios_reply_available(guest) &&
                memory.read_u32(kHeld) != 8u,
                "dependent request occupies one immutable mailbox without returning send credit");
            passed &= expect_runtime_error([&] { submit_ios_request(guest, kRequest + 0x180u); },
                "a third X1 without credit fails explicitly instead of replacing held work");
        }
        latch.release();
        passed &= expect(latch.wait_completed() && memory.native_ios_completion_pending(),
            "native storage completion is observable before any guest reply");
        if (scenario == 3u || scenario == 9u) {
            std::ostringstream audit;
            {
                ScopedStreamRedirect capture(std::cout, audit.rdbuf());
                if (scenario == 3u) {
                    passed &= expect_runtime_error([&] { (void)memory.prepare_for_process_exit(); },
                        "fatal observed first during shutdown remains a failure");
                } else {
                    passed &= expect(memory.prepare_for_process_exit(),
                        "normal shutdown performs the first preparation");
                }
            }
            passed &= expect(audit.str().find(scenario == 3u ? "fatal-results=1" : "fatal-results=0") != std::string::npos &&
                audit.str().find("shutdown-unreplied=1") != std::string::npos &&
                memory.read_u32(kWrite) == 4u && latch.calls.load() == 1u &&
                galaxy::host::read_binary_file(host_path) == payload,
                "shutdown drains physical persistence once and audits its unserved reply honestly");
            continue;
        }
        if (scenario == 2u || scenario == 4u) {
            passed &= expect_runtime_error([&] { memory.service_native_ios_completions(); },
                "postcommit failure or held input mutation fails without unsafe replay");
            passed &= expect(galaxy::host::read_binary_file(host_path) == payload,
                "fatal after durable commit does not roll back or repeat the physical write");
            if (scenario == 2u) {
                passed &= expect(memory.read_u32(kWrite) == 4u &&
                    Access::file_position(memory, fd) == 0u && Access::file_bytes(memory, fd).empty(),
                    "postcommit fatal cannot be acknowledged as ordinary retryable failure");
            } else {
                passed &= expect(memory.read_u32(kHeld) == 4u && latch.calls.load() == 1u,
                    "mutated held payload is rejected before a second physical transaction");
            }
            std::ostringstream audit;
            {
                ScopedStreamRedirect capture(std::cout, audit.rdbuf());
                passed &= expect(memory.prepare_for_process_exit(),
                    "fatal completion leaves teardown available for remaining resources");
            }
            passed &= expect(audit.str().find("fatal-results=1") != std::string::npos &&
                audit.str().find(scenario == 2u ? " pending=1" : "held-unacknowledged=1") != std::string::npos,
                "terminal audit preserves fatal and outstanding request ownership after receipt consumption");
            continue;
        }
        if (scenario == 5u) latch.reset();
        memory.service_native_ios_completions();
        if (scenario == 1u) {
            if (deny_replace != INVALID_HANDLE_VALUE) CloseHandle(deny_replace);
            passed &= expect_ios_reply(memory, guest, kWrite,
                static_cast<std::uint32_t>(-111), 4u, "precommit replacement error produces exact IOS error");
            passed &= expect(Access::file_position(memory, fd) == 0u &&
                Access::file_bytes(memory, fd).empty() && galaxy::host::read_binary_file(host_path).empty(),
                "precommit failure preserves old disk bytes and live descriptor state");
        } else {
            passed &= expect_ios_reply(memory, guest, kWrite, 3u, 4u,
                "durable first write is the first visible reply");
            passed &= expect(Access::file_bytes(memory, sibling) == payload &&
                galaxy::host::read_binary_file(host_path) == payload,
                "durability commits the owned payload into shared backing before ordered followers");
        }
        acknowledge_ios_reply(guest);
        if (scenario == 0u) {
            passed &= expect_ios_reply(memory, guest, kHeld, 3u, 3u,
                "ordered sibling read replies after the write");
            passed &= expect(std::memcmp(memory.pointer(kRead, 3u), payload.data(), 3u) == 0 &&
                Access::file_position(memory, fd) == 3u && Access::file_position(memory, sibling) == 3u,
                "shared handles see new bytes with separate file positions");
            acknowledge_ios_reply(guest);
        } else if (scenario == 5u) {
            passed &= expect(latch.wait_entered() && memory.read_u32(kHeld) == 4u &&
                Access::file_position(memory, fd) == 3u && !ios_reply_available(guest),
                "held second write is admitted once against the committed first position");
            latch.release();
            passed &= expect(latch.wait_completed(), "second ordered persistence finishes");
            memory.service_native_ios_completions();
            auto twice = payload;
            twice.insert(twice.end(), payload.begin(), payload.end());
            passed &= expect_ios_reply(memory, guest, kHeld, 3u, 4u,
                "second durable write receives exactly one reply");
            passed &= expect(Access::file_position(memory, fd) == 6u &&
                Access::file_bytes(memory, sibling) == twice &&
                galaxy::host::read_binary_file(host_path) == twice && latch.calls.load() == 2u,
                "ordered writes persist each physical candidate once");
            acknowledge_ios_reply(guest);
        } else if (scenario == 6u) {
            passed &= expect_ios_reply(memory, guest, kHeld, 0u, 2u,
                "held close replies only after durable write commits");
            acknowledge_ios_reply(guest);
            const auto reopened = ios_open_path(memory, guest, kRequest, kPath, kName, 3u);
            passed &= expect(Access::file_position(memory, reopened) == 0u &&
                Access::file_bytes(memory, reopened) == payload,
                "reopening after an ordered close creates a fresh descriptor over committed bytes");
        } else if (scenario == 7u) {
            passed &= expect_ios_reply(memory, guest, kHeld, 0u, 6u,
                "FS-device metadata request follows durable NAND write ordering");
            passed &= expect(*memory.pointer(kRead + 70u, 1u) == std::byte{3} &&
                *memory.pointer(kRead + 71u, 1u) == std::byte{3} &&
                *memory.pointer(kRead + 72u, 1u) == std::byte{0},
                "held FS metadata operation returns the persisted file permissions");
            acknowledge_ios_reply(guest);
        } else if (scenario == 8u) {
            passed &= expect_ios_reply(memory, guest, kHeld, 2u, 5u,
                "held seek replies after the write's earlier reply");
            passed &= expect(Access::file_position(memory, fd) == 2u &&
                Access::file_position(memory, sibling) == 0u,
                "held seek executes after write position commit without changing sibling position");
            acknowledge_ios_reply(guest);
        }
        memory.service_native_ios_completions();
        passed &= expect(!ios_reply_available(guest) && !memory.native_ios_completion_pending(),
            "repeated completion service creates no duplicate reply");
    }
    // Later tests initialize shared_test_root itself as a NAND. Remove only
    // these owned child fixtures after every address space and worker is gone.
    const auto cleanup_root = std::filesystem::absolute(shared_test_root).lexically_normal();
    for (unsigned scenario = 0u; scenario < 10u; ++scenario) {
        const auto cleanup_target = std::filesystem::absolute(
            shared_test_root / ("async_ownership_" + std::to_string(scenario))).lexically_normal();
        if (cleanup_target.parent_path() != cleanup_root)
            throw std::runtime_error("async fixture cleanup target leaves its shared test root");
        std::error_code cleanup_error;
        std::filesystem::remove_all(cleanup_target, cleanup_error);
        passed &= expect(!cleanup_error,
            "async NAND fixture cleanup preserves later shared-root preflight isolation");
    }
    return passed;
}

bool native_nand_atomic_persistence_works(
    const std::filesystem::path& shared_test_root) {
    constexpr std::string_view kGuestPath =
        "/title/00010000/524d4745/data/GameData.bin";
    constexpr std::uint32_t kPath = 0x133E4000u;
    constexpr std::uint32_t kFsPath = 0x133E4100u;
    constexpr std::uint32_t kAttr = 0x133E4200u;
    constexpr std::uint32_t kRequest = 0x133E4300u;
    constexpr std::uint32_t kBuffer = 0x13400000u;
    constexpr std::uint32_t kIsfsErrInvalid =
        static_cast<std::uint32_t>(-101);
    constexpr std::uint32_t kIsfsErrInUse =
        static_cast<std::uint32_t>(-111);

    bool passed = true;
    const std::filesystem::path isolated_root =
        shared_test_root / L"atomic_game_data";
    std::error_code cleanup_error;
    std::filesystem::remove_all(isolated_root, cleanup_error);
    ScopedWideEnv set_isolated_nand_root(L"GALAXY_NAND_ROOT", isolated_root);
    const std::filesystem::path host_path =
        isolated_root / L"title" / L"00010000" / L"524d4745" / L"data" /
        L"GameData.bin";

    const std::vector<std::byte> expected = valid_rmge_game_data_for_test();

    {
        auto writer_owner =
            std::make_unique<galaxy::host::GuestAddressSpace>();
        galaxy::host::GuestAddressSpace& writer = *writer_owner;
        galaxy::GuestMemoryV1* const guest_memory = writer.guest_memory();
        passed &= expect(
            galaxy::host::NativeNandTestAccess::ensure_rmge01_data_directory(
                writer) == 0u &&
                std::filesystem::is_directory(host_path.parent_path()),
            "exact ES bootstrap provisions the fixed RMGE title/data parent chain");
        const std::uint32_t fs_handle = ios_open_path(
            writer, guest_memory, kRequest, kFsPath, "/dev/fs");
        passed &= expect(
            fs_handle > 0u && fs_handle < 0x80000000u,
            "isolated /dev/fs opens for GameData creation");

        write_isfs_attribute_block(
            writer, kAttr, kGuestPath, 3u, 3u, 0u, 0u);
        ios_ioctl_request(
            writer,
            guest_memory,
            kRequest + 0x40u,
            fs_handle,
            9u,
            kAttr,
            0x4Cu,
            0u,
            0u);
        passed &= expect(
            writer.read_u32(kRequest + 0x44u) == 0u,
            "ISFS CreateFile durably publishes an empty GameData file");
        acknowledge_ios_reply(guest_memory);

        const std::uint32_t game_data_handle = ios_open_path(
            writer,
            guest_memory,
            kRequest + 0x80u,
            kPath,
            kGuestPath,
            3u);
        passed &= expect(
            game_data_handle > 0u && game_data_handle < 0x80000000u,
            "new GameData opens as a host-backed NAND file");
        const std::array<std::uint64_t, 2> provisional_identity =
            host_file_identity_for_test(host_path);
        const std::filesystem::path provisional_metadata_path(
            host_path.native() + L":galaxy.isfs.meta");
        const std::filesystem::path provisional_marker_path(
            host_path.native() + L":galaxy.isfs.provisional");
        const std::vector<std::byte> provisional_metadata =
            galaxy::host::read_binary_file(provisional_metadata_path);
        const std::vector<std::byte> provisional_marker =
            galaxy::host::read_binary_file(provisional_marker_path);
        writer.copy(
            kBuffer,
            std::span<const std::byte>(expected.data(), expected.size() - 1u));
        ios_write_request(
            writer,
            guest_memory,
            kRequest + 0xC0u,
            game_data_handle,
            kBuffer,
            static_cast<std::uint32_t>(expected.size() - 1u));
        passed &= expect(
            writer.read_u32(kRequest + 0xC4u) == kIsfsErrInvalid &&
                (ipc_control(guest_memory) & 0x02u) != 0u,
            "partial invalid GameData write reports ISFS invalid and returns IOS acknowledgement credit");
        acknowledge_ios_reply(guest_memory);
        passed &= expect(
            host_file_identity_for_test(host_path) == provisional_identity &&
                std::filesystem::file_size(host_path) == 0u &&
                galaxy::host::read_binary_file(provisional_metadata_path) ==
                    provisional_metadata &&
                galaxy::host::read_binary_file(provisional_marker_path) ==
                    provisional_marker,
            "partial invalid GameData write preserves the exact empty object, GIS3, and GIP1 marker");
        // A replaced/tampered provisional marker must reject without losing
        // the request credit. Restore the temporary test object's exact marker
        // before exercising the already-existing valid retry below.
        auto tampered_marker = provisional_marker;
        tampered_marker.front() ^= std::byte{1u};
        {
            std::ofstream marker(provisional_marker_path, std::ios::binary | std::ios::trunc);
            marker.write(reinterpret_cast<const char*>(tampered_marker.data()),
                static_cast<std::streamsize>(tampered_marker.size()));
        }
        writer.copy(kBuffer, expected);
        ios_write_request(writer, guest_memory, kRequest + 0xE0u,
            game_data_handle, kBuffer, static_cast<std::uint32_t>(expected.size()));
        passed &= expect(writer.read_u32(kRequest + 0xE4u) == static_cast<std::uint32_t>(-102) &&
            (ipc_control(guest_memory) & 0x02u) != 0u,
            "provisional identity rejection returns access error and IOS acknowledgement credit");
        acknowledge_ios_reply(guest_memory);
        {
            std::ofstream marker(provisional_marker_path, std::ios::binary | std::ios::trunc);
            marker.write(reinterpret_cast<const char*>(provisional_marker.data()),
                static_cast<std::streamsize>(provisional_marker.size()));
        }
        writer.copy(kBuffer, expected);
        ios_write_request(
            writer,
            guest_memory,
            kRequest + 0x100u,
            game_data_handle,
            kBuffer,
            static_cast<std::uint32_t>(expected.size()));
        passed &= expect(
            writer.read_u32(kRequest + 0x104u) == expected.size(),
            "valid full GameData retry reports success only after persistence");
        acknowledge_ios_reply(guest_memory);
        ios_close_request(
            writer,
            guest_memory,
            kRequest + 0x140u,
            game_data_handle);
        passed &= expect(
            writer.read_u32(kRequest + 0x144u) == 0u,
            "GameData close releases an already-synchronously-persisted handle");
        acknowledge_ios_reply(guest_memory);
        ios_close_request(
            writer, guest_memory, kRequest + 0x180u, fs_handle);
        passed &= expect(
            writer.read_u32(kRequest + 0x184u) == 0u,
            "isolated /dev/fs handle closes cleanly");
        acknowledge_ios_reply(guest_memory);
    }

    const std::vector<std::byte> persisted =
        galaxy::host::read_binary_file(host_path);
    passed &= expect(
        persisted == expected,
        "disk GameData is exactly the complete 48,640-byte guest payload");
    const std::filesystem::path metadata_path(
        host_path.native() + L":galaxy.isfs.meta");
    const std::filesystem::path provisional_path(
        host_path.native() + L":galaxy.isfs.provisional");
    const std::vector<std::byte> metadata =
        galaxy::host::read_binary_file(metadata_path);
    std::uint32_t metadata_checksum = 2'166'136'261u;
    if (metadata.size() >= 14u) {
        for (const std::byte value :
             std::span<const std::byte>(metadata.data(), 14u)) {
            metadata_checksum ^= std::to_integer<std::uint8_t>(value);
            metadata_checksum *= 16'777'619u;
        }
    }
    const std::uint32_t stored_metadata_checksum = metadata.size() == 18u
        ? (std::to_integer<std::uint32_t>(metadata[14]) << 24u) |
              (std::to_integer<std::uint32_t>(metadata[15]) << 16u) |
              (std::to_integer<std::uint32_t>(metadata[16]) << 8u) |
              std::to_integer<std::uint32_t>(metadata[17])
        : 0u;
    passed &= expect(
        metadata.size() == 18u && metadata[0] == std::byte{'G'} &&
            metadata[1] == std::byte{'I'} &&
            metadata[2] == std::byte{'S'} &&
            metadata[3] == std::byte{'3'} &&
            metadata[4] == std::byte{0x00} &&
            metadata[5] == std::byte{0x00} &&
            metadata[6] == std::byte{0x10} &&
            metadata[7] == std::byte{0x01} &&
            metadata[8] == std::byte{0x30} &&
            metadata[9] == std::byte{0x31} &&
            metadata[10] == std::byte{3} &&
            metadata[11] == std::byte{3} &&
            metadata[12] == std::byte{0} &&
            metadata[13] == std::byte{0} &&
            stored_metadata_checksum == metadata_checksum,
        "canonical GameData carries exact checksummed GIS3 permission metadata");
    passed &= expect(
        !host_file_stream_exists_for_test(provisional_path),
        "a valid GameData replacement carries no stale GIP1 provisional marker");

    {
        auto cold_reader_owner =
            std::make_unique<galaxy::host::GuestAddressSpace>();
        galaxy::host::GuestAddressSpace& cold_reader = *cold_reader_owner;
        galaxy::GuestMemoryV1* const guest_memory = cold_reader.guest_memory();
        const std::uint32_t handle = ios_open_path(
            cold_reader, guest_memory, kRequest, kPath, kGuestPath, 3u);
        passed &= expect(
            handle > 0u && handle < 0x80000000u,
            "cold GuestAddressSpace reopens the persisted GameData");

        ios_read_request(
            cold_reader, guest_memory, kRequest + 0x40u, handle, 0u, 1u);
        passed &= expect(
            cold_reader.read_u32(kRequest + 0x44u) == kIsfsErrInvalid,
            "non-empty NAND read rejects a null guest buffer");
        acknowledge_ios_reply(guest_memory);
        ios_write_request(
            cold_reader, guest_memory, kRequest + 0x80u, handle, 0u, 1u);
        passed &= expect(
            cold_reader.read_u32(kRequest + 0x84u) == kIsfsErrInvalid,
            "non-empty NAND write rejects a null guest buffer");
        acknowledge_ios_reply(guest_memory);
        ios_seek_request(
            cold_reader, guest_memory, kRequest + 0xC0u, handle, 0u, 3u);
        passed &= expect(
            cold_reader.read_u32(kRequest + 0xC4u) == kIsfsErrInvalid,
            "NAND seek rejects an unsupported whence");
        acknowledge_ios_reply(guest_memory);

        cold_reader.clear(kBuffer, static_cast<std::uint32_t>(expected.size()));
        ios_read_request(
            cold_reader,
            guest_memory,
            kRequest + 0x100u,
            handle,
            kBuffer,
            static_cast<std::uint32_t>(expected.size()));
        passed &= expect(
            cold_reader.read_u32(kRequest + 0x104u) == expected.size(),
            "cold GameData read returns the exact persisted length");
        passed &= expect(
            std::equal(
                expected.begin(),
                expected.end(),
                cold_reader.pointer(kBuffer, static_cast<std::uint32_t>(expected.size()))),
            "cold GameData read returns the exact persisted bytes after rejected requests");
        acknowledge_ios_reply(guest_memory);
        ios_close_request(
            cold_reader, guest_memory, kRequest + 0x140u, handle);
        passed &= expect(
            cold_reader.read_u32(kRequest + 0x144u) == 0u,
            "cold GameData handle closes cleanly");
        acknowledge_ios_reply(guest_memory);
    }

    {
        auto failing_writer_owner =
            std::make_unique<galaxy::host::GuestAddressSpace>();
        galaxy::host::GuestAddressSpace& failing_writer =
            *failing_writer_owner;
        galaxy::GuestMemoryV1* const guest_memory = failing_writer.guest_memory();
        const std::uint32_t handle = ios_open_path(
            failing_writer, guest_memory, kRequest, kPath, kGuestPath, 3u);
        passed &= expect(
            handle > 0u && handle < 0x80000000u,
            "failure-injection writer opens the old GameData");
        std::vector<std::byte> replacement = expected;
        replacement.front() ^= std::byte{0xFF};
        failing_writer.copy(kBuffer, replacement);

        HANDLE locked_destination = CreateFileW(
            host_path.c_str(),
            GENERIC_READ,
            FILE_SHARE_READ | FILE_SHARE_WRITE,
            nullptr,
            OPEN_EXISTING,
            FILE_ATTRIBUTE_NORMAL,
            nullptr);
        passed &= expect(
            locked_destination != INVALID_HANDLE_VALUE,
            "failure injection locks the live destination against replacement");
        ios_write_request(
            failing_writer,
            guest_memory,
            kRequest + 0x40u,
            handle,
            kBuffer,
            static_cast<std::uint32_t>(replacement.size()));
        passed &= expect(
            failing_writer.read_u32(kRequest + 0x44u) == kIsfsErrInUse,
            "locked atomic replacement reports ISFS in-use from IOS_Write");
        acknowledge_ios_reply(guest_memory);
        ios_close_request(
            failing_writer, guest_memory, kRequest + 0x80u, handle);
        passed &= expect(
            failing_writer.read_u32(kRequest + 0x84u) == 0u,
            "IOS_Close only releases state after a rejected synchronous write");
        acknowledge_ios_reply(guest_memory);
        passed &= expect(
            galaxy::host::read_binary_file(host_path) == expected,
            "failed write and close preserve the complete old GameData");

        if (locked_destination != INVALID_HANDLE_VALUE) {
            passed &= expect(
                CloseHandle(locked_destination) != FALSE,
                "failure-injection destination unlock succeeds");
        }
        locked_destination = INVALID_HANDLE_VALUE;
        const std::uint32_t reopened = ios_open_path(
            failing_writer,
            guest_memory,
            kRequest + 0xC0u,
            kPath,
            kGuestPath,
            1u);
        passed &= expect(
            reopened > 0u && reopened < 0x80000000u,
            "GameData reopens after the injected host fault clears");
        failing_writer.clear(kBuffer, 1u);
        ios_read_request(
            failing_writer,
            guest_memory,
            kRequest + 0x100u,
            reopened,
            kBuffer,
            1u);
        passed &= expect(
            failing_writer.read_u32(kRequest + 0x104u) == 1u &&
                *failing_writer.pointer(kBuffer, 1u) == expected.front(),
            "failed synchronous persistence leaves the durable old bytes intact");
        acknowledge_ios_reply(guest_memory);
        ios_close_request(
            failing_writer, guest_memory, kRequest + 0x140u, reopened);
        passed &= expect(
            failing_writer.read_u32(kRequest + 0x144u) == 0u,
            "reopened NAND handle closes successfully after host-fault verification");
        acknowledge_ios_reply(guest_memory);
    }

    std::size_t transaction_marker_count = 0u;
    for (const auto& entry :
         std::filesystem::directory_iterator(host_path.parent_path())) {
        const std::wstring name = entry.path().filename().wstring();
        if (name.find(L".__galaxy_isfs_candidate__.") != std::wstring::npos ||
            name.find(L".__galaxy_isfs_deleted__.") != std::wstring::npos) {
            ++transaction_marker_count;
        }
    }
    passed &= expect(
        transaction_marker_count == 0u,
        "handled NAND persistence failures remove every GIS3 transaction marker");
    passed &= expect(
        galaxy::host::read_binary_file(host_path) == expected,
        "GameData remains exact after the injected synchronous persistence failure");

    std::filesystem::remove_all(isolated_root, cleanup_error);
    return passed;
}

bool native_nand_exact_current_surface_works(
    const std::filesystem::path& shared_test_root) {
    constexpr std::uint32_t kPath = 0x13500000u;
    constexpr std::uint32_t kFsPath = 0x13500100u;
    constexpr std::uint32_t kAttr = 0x13500200u;
    constexpr std::uint32_t kRequest = 0x13500400u;
    constexpr std::uint32_t kBuffer = 0x13501000u;
    constexpr std::uint32_t kStats = 0x13501100u;
    constexpr std::uint32_t kVectors = 0x13501200u;
    constexpr std::uint32_t kCount = 0x13501300u;
    constexpr std::uint32_t kNames = 0x13501400u;
    constexpr std::string_view kTestDirectory = "/tmp/surface";
    constexpr std::string_view kTestFile = "/tmp/surface/modes.bin";
    constexpr std::string_view kSettingFile =
        "/title/00000001/00000002/data/setting.txt";
    constexpr std::uint32_t kIsfsErrInvalid =
        static_cast<std::uint32_t>(-101);
    constexpr std::uint32_t kIsfsErrAccess =
        static_cast<std::uint32_t>(-102);
    constexpr std::uint32_t kIsfsErrNoFreeHandle =
        static_cast<std::uint32_t>(-109);

    bool passed = true;
    const std::filesystem::path isolated_root =
        shared_test_root / L"exact_current_surface";
    std::error_code cleanup_error;
    std::filesystem::remove_all(isolated_root, cleanup_error);
    ScopedWideEnv set_isolated_nand_root(L"GALAXY_NAND_ROOT", isolated_root);

    {
        galaxy::host::GuestAddressSpace memory;
        galaxy::GuestMemoryV1* const guest_memory = memory.guest_memory();
        const std::uint32_t fs_handle = ios_open_path(
            memory, guest_memory, kRequest, kFsPath, "/dev/fs");
        passed &= expect(
            fs_handle > 0u && fs_handle < 0x80000000u,
            "focused NAND test opens /dev/fs");

        const std::filesystem::path host_directory =
            isolated_root / L"tmp" / L"surface";
        write_isfs_attribute_block(
            memory, kAttr, kTestDirectory, 3u, 3u, 3u, 0u);
        for (const std::uint32_t bad_length : {0x4Au, 0x4Bu, 0x4Du}) {
            ios_ioctl_request(
                memory,
                guest_memory,
                kRequest + 0x40u,
                fs_handle,
                3u,
                kAttr,
                bad_length,
                0u,
                0u);
            passed &= expect(
                memory.read_u32(kRequest + 0x44u) == kIsfsErrInvalid &&
                    !std::filesystem::exists(host_directory),
                "CreateDir rejects every non-0x4c attribute shape without mutation");
            acknowledge_ios_reply(guest_memory);
        }
        ios_ioctl_request(
            memory,
            guest_memory,
            kRequest + 0x40u,
            fs_handle,
            3u,
            kAttr,
            0x4Cu,
            0u,
            0u);
        passed &= expect(
            memory.read_u32(kRequest + 0x44u) == 0u &&
                std::filesystem::is_directory(host_directory),
            "CreateDir accepts the exact 0x4c SDK attribute block");
        acknowledge_ios_reply(guest_memory);

        const std::filesystem::path host_file =
            host_directory / L"modes.bin";
        write_isfs_attribute_block(
            memory, kAttr, kTestFile, 3u, 3u, 3u, 0u);
        for (const std::uint32_t bad_length : {0x4Au, 0x4Bu, 0x4Du}) {
            ios_ioctl_request(
                memory,
                guest_memory,
                kRequest + 0x40u,
                fs_handle,
                9u,
                kAttr,
                bad_length,
                0u,
                0u);
            passed &= expect(
                memory.read_u32(kRequest + 0x44u) == kIsfsErrInvalid &&
                    !std::filesystem::exists(host_file),
                "CreateFile rejects every non-0x4c attribute shape without mutation");
            acknowledge_ios_reply(guest_memory);
        }
        ios_ioctl_request(
            memory,
            guest_memory,
            kRequest + 0x40u,
            fs_handle,
            9u,
            kAttr,
            0x4Cu,
            0u,
            0u);
        passed &= expect(
            memory.read_u32(kRequest + 0x44u) == 0u &&
                std::filesystem::is_regular_file(host_file) &&
                std::filesystem::file_size(host_file) == 0u,
            "CreateFile atomically publishes an exact empty file for the 0x4c shape");
        acknowledge_ios_reply(guest_memory);

        const std::uint32_t read_only = ios_open_path(
            memory,
            guest_memory,
            kRequest + 0x80u,
            kPath,
            kTestFile,
            1u);
        const std::uint32_t write_only = ios_open_path(
            memory,
            guest_memory,
            kRequest + 0x80u,
            kPath,
            kTestFile,
            2u);
        const std::uint32_t read_write = ios_open_path(
            memory,
            guest_memory,
            kRequest + 0x80u,
            kPath,
            kTestFile,
            3u);
        const std::uint32_t invalid_raw_mode = ios_open_path(
            memory,
            guest_memory,
            kRequest + 0x80u,
            kPath,
            kTestFile,
            0x101u);
        passed &= expect(
            read_only > 0u && read_only < 0x80000000u &&
                write_only > 0u && write_only < 0x80000000u &&
                read_write > 0u && read_write < 0x80000000u &&
                invalid_raw_mode == kIsfsErrInvalid,
            "NAND modes 1/2/3 open and raw mode bits outside 0..3 reject without masking");

        constexpr std::array<std::byte, 4> kPayload{
            std::byte{0x11}, std::byte{0x22}, std::byte{0x33}, std::byte{0x44}};
        memory.copy(kBuffer, kPayload);
        ios_write_request(
            memory,
            guest_memory,
            kRequest + 0xC0u,
            read_only,
            kBuffer,
            1u);
        passed &= expect(
            memory.read_u32(kRequest + 0xC4u) == kIsfsErrAccess,
            "mode 1 NAND handle rejects writes");
        acknowledge_ios_reply(guest_memory);
        ios_read_request(
            memory,
            guest_memory,
            kRequest + 0xC0u,
            write_only,
            kBuffer,
            1u);
        passed &= expect(
            memory.read_u32(kRequest + 0xC4u) == kIsfsErrAccess,
            "mode 2 NAND handle rejects reads");
        acknowledge_ios_reply(guest_memory);

        ios_write_request(
            memory,
            guest_memory,
            kRequest + 0xC0u,
            read_write,
            kBuffer,
            static_cast<std::uint32_t>(kPayload.size()));
        passed &= expect(
            memory.read_u32(kRequest + 0xC4u) == kPayload.size() &&
                galaxy::host::read_binary_file(host_file) ==
                    std::vector<std::byte>(kPayload.begin(), kPayload.end()),
            "mode 3 write publishes synchronously before IOS success");
        acknowledge_ios_reply(guest_memory);

        memory.write_u32(kStats, 0xAAAAAAAAu);
        memory.write_u32(kStats + 4u, 0xBBBBBBBBu);
        ios_ioctl_request(
            memory,
            guest_memory,
            kRequest + 0xC0u,
            read_write,
            11u,
            0u,
            0u,
            kStats,
            8u);
        passed &= expect(
            memory.read_u32(kRequest + 0xC4u) == 0u &&
                memory.read_u32(kStats) == kPayload.size() &&
                memory.read_u32(kStats + 4u) == kPayload.size(),
            "GetFileStats reports shared size and the calling handle position");
        acknowledge_ios_reply(guest_memory);

        memory.write_u32(kStats, 0xCCCCCCCCu);
        memory.write_u32(kStats + 4u, 0xDDDDDDDDu);
        ios_ioctl_request(
            memory,
            guest_memory,
            kRequest + 0xC0u,
            read_only,
            11u,
            0u,
            0u,
            kStats,
            8u);
        passed &= expect(
            memory.read_u32(kRequest + 0xC4u) == 0u &&
                memory.read_u32(kStats) == kPayload.size() &&
                memory.read_u32(kStats + 4u) == 0u,
            "duplicate NAND opens share bytes but retain independent positions");
        acknowledge_ios_reply(guest_memory);

        memory.clear(kBuffer, static_cast<std::uint32_t>(kPayload.size()));
        ios_read_request(
            memory,
            guest_memory,
            kRequest + 0xC0u,
            read_only,
            kBuffer,
            static_cast<std::uint32_t>(kPayload.size()));
        passed &= expect(
            memory.read_u32(kRequest + 0xC4u) == kPayload.size() &&
                std::equal(
                    kPayload.begin(),
                    kPayload.end(),
                    memory.pointer(kBuffer, static_cast<std::uint32_t>(kPayload.size()))),
            "a pre-existing duplicate handle immediately observes the committed shared backing");
        acknowledge_ios_reply(guest_memory);

        memory.copy(kBuffer, std::span<const std::byte>(kPayload.data(), 1u));
        ios_write_request(
            memory,
            guest_memory,
            kRequest + 0xC0u,
            write_only,
            kBuffer,
            1u);
        passed &= expect(
            memory.read_u32(kRequest + 0xC4u) == 1u,
            "mode 2 NAND handle performs a synchronous write");
        acknowledge_ios_reply(guest_memory);

        memory.write_u32(kStats, 0x11223344u);
        memory.write_u32(kStats + 4u, 0x55667788u);
        ios_ioctl_request(
            memory,
            guest_memory,
            kRequest + 0xC0u,
            read_write,
            11u,
            0u,
            0u,
            kStats,
            4u);
        passed &= expect(
            memory.read_u32(kRequest + 0xC4u) == kIsfsErrInvalid &&
                memory.read_u32(kStats) == 0x11223344u &&
                memory.read_u32(kStats + 4u) == 0x55667788u,
            "GetFileStats rejects a non-eight-byte output without partial writes");
        acknowledge_ios_reply(guest_memory);

        const auto close_handle = [&](std::uint32_t handle) {
            ios_close_request(
                memory, guest_memory, kRequest + 0x100u, handle);
            const bool closed = memory.read_u32(kRequest + 0x104u) == 0u;
            acknowledge_ios_reply(guest_memory);
            return closed;
        };
        bool mode_handles_closed = true;
        for (const std::uint32_t handle :
             {read_only, write_only, read_write}) {
            mode_handles_closed &= close_handle(handle);
        }
        passed &= expect(
            mode_handles_closed,
            "all mode and shared-backing handles close cleanly");

        std::vector<std::uint32_t> capped_handles;
        capped_handles.reserve(15u);
        bool first_fifteen_opened = true;
        for (std::uint32_t i = 0u; i < 15u; ++i) {
            const std::uint32_t handle = ios_open_path(
                memory,
                guest_memory,
                kRequest + 0x140u,
                kPath,
                kSettingFile,
                1u);
            first_fifteen_opened &=
                handle > 0u && handle < 0x80000000u;
            capped_handles.push_back(handle);
        }
        const std::uint32_t rejected_at_cap = ios_open_path(
            memory,
            guest_memory,
            kRequest + 0x140u,
            kPath,
            kSettingFile,
            1u);
        passed &= expect(
            first_fifteen_opened && rejected_at_cap == kIsfsErrNoFreeHandle,
            "the 16-descriptor ISFS cap counts the open /dev/fs device handle");

        passed &= expect(
            close_handle(capped_handles.front()),
            "closing one NAND descriptor frees exactly one ISFS slot");
        capped_handles.erase(capped_handles.begin());
        const std::uint32_t reopened_after_close = ios_open_path(
            memory,
            guest_memory,
            kRequest + 0x140u,
            kPath,
            kSettingFile,
            1u);
        passed &= expect(
            reopened_after_close > 0u && reopened_after_close < 0x80000000u,
            "a NAND descriptor can open immediately after one capped slot closes");
        for (const std::uint32_t handle : capped_handles) {
            passed &= close_handle(handle);
        }
        passed &= close_handle(reopened_after_close);
        passed &= expect(
            close_handle(fs_handle),
            "focused /dev/fs descriptor closes after cap verification");
    }

    {
        galaxy::host::GuestAddressSpace mode_zero_memory;
        galaxy::GuestMemoryV1* const guest_memory =
            mode_zero_memory.guest_memory();
        const std::uint32_t mode_zero_result = ios_open_path(
            mode_zero_memory,
            guest_memory,
            kRequest,
            kPath,
            kSettingFile,
            0u);
        passed &= expect(
            mode_zero_result == kIsfsErrInvalid,
            "mode 0 NAND file open rejects outside the exact RMGE01 surface");
    }

    {
        galaxy::host::GuestAddressSpace readdir_memory;
        galaxy::GuestMemoryV1* const guest_memory =
            readdir_memory.guest_memory();
        const std::uint32_t fs_handle = ios_open_path(
            readdir_memory,
            guest_memory,
            kRequest,
            kFsPath,
            "/dev/fs");
        constexpr std::array<std::byte, 5> kTmpPath{
            std::byte{'/'}, std::byte{'t'}, std::byte{'m'}, std::byte{'p'},
            std::byte{0}};
        readdir_memory.clear(kPath, 64u);
        readdir_memory.copy(kPath, kTmpPath);
        readdir_memory.write_u32(kVectors, kPath);
        readdir_memory.write_u32(kVectors + 4u, 64u);
        readdir_memory.write_u32(kVectors + 8u, kCount);
        readdir_memory.write_u32(kVectors + 12u, 4u);
        readdir_memory.write_u32(kCount, 0xA5A5A5A5u);
        ios_ioctlv_request(
            readdir_memory,
            guest_memory,
            kRequest + 0x40u,
            fs_handle,
            4u,
            1u,
            1u,
            kVectors);
        passed &= expect(
            readdir_memory.read_u32(kRequest + 0x44u) == 0u &&
                readdir_memory.read_u32(kCount) == 0u,
            "count-only ReadDir remains implemented for the exact RMGE01 route");
        acknowledge_ios_reply(guest_memory);

        // A valid reserved candidate is intentionally hidden, but still advances
        // the host iterator. Inject its late error before publishing the count.
        const auto hidden_candidate = isolated_root / L"tmp" /
            L"readdir.__galaxy_isfs_candidate__.1.1";
        write_binary_file(hidden_candidate, {});
        readdir_memory.write_u32(kCount, 0xA5A5A5A5u);
        {
            ScopedHostOwnershipTestHook hook(&inject_readdir_increment_failure);
            ios_ioctlv_request(readdir_memory, guest_memory,
                kRequest + 0x40u, fs_handle, 4u, 1u, 1u, kVectors);
        }
        passed &= expect(
            readdir_memory.read_u32(kRequest + 0x44u) ==
                static_cast<std::uint32_t>(-117) &&
            readdir_memory.read_u32(kCount) == 0xA5A5A5A5u,
            "late ReadDir iterator error replies Unknown and preserves the count");
        passed &= expect_ios_reply(readdir_memory, guest_memory,
            kRequest + 0x40u, static_cast<std::uint32_t>(-117), 7u,
            "late ReadDir error still publishes the original IOCTLV reply");
        acknowledge_ios_reply(guest_memory);
        std::filesystem::remove(hidden_candidate);

        readdir_memory.write_u32(kVectors + 8u, kCount);
        readdir_memory.write_u32(kVectors + 12u, 4u);
        readdir_memory.write_u32(kVectors + 16u, kNames);
        readdir_memory.write_u32(kVectors + 20u, 52u);
        readdir_memory.write_u32(kVectors + 24u, kCount + 4u);
        readdir_memory.write_u32(kVectors + 28u, 4u);
        readdir_memory.write_u32(kCount, 4u);
        readdir_memory.write_u32(kCount + 4u, 0x5A5A5A5Au);
        readdir_memory.write_u32(kNames, 0xC3C3C3C3u);
        passed &= expect_runtime_error(
            [&] {
                ios_ioctlv_request(
                    readdir_memory,
                    guest_memory,
                    kRequest + 0x80u,
                    fs_handle,
                    4u,
                    2u,
                    2u,
                    kVectors);
            },
            "full name-list ReadDir hard-fails outside the exact RMGE01 call graph");
        passed &= expect(
            readdir_memory.read_u32(kCount + 4u) == 0x5A5A5A5Au &&
                readdir_memory.read_u32(kNames) == 0xC3C3C3C3u,
            "hard-failed full ReadDir preserves every output vector");
    }

    std::filesystem::remove_all(isolated_root, cleanup_error);
    return passed;
}

bool native_nand_pre_ads_legacy_cohort_startup_works(
    const std::filesystem::path& shared_test_root) {
    constexpr std::string_view kSysconfPath = "/shared2/sys/SYSCONF";
    constexpr std::string_view kSettingPath =
        "/title/00000001/00000002/data/setting.txt";
    constexpr std::uint32_t kPath = 0x13500000u;
    constexpr std::uint32_t kRequest = 0x13500100u;
    constexpr std::uint32_t kFsPath = 0x13500200u;
    constexpr std::uint32_t kAttrOut = 0x13500300u;

    struct LegacyFixtureBytes {
        std::vector<std::byte> game_data;
        std::vector<std::byte> canonical_banner;
        std::vector<std::byte> legacy_banner;
        std::vector<std::byte> rfl;
        std::vector<std::byte> sysconf;
        std::vector<std::byte> setting;
        std::vector<std::byte> play_rec;
    };
    struct LegacySnapshot {
        std::vector<std::array<std::uint64_t, 2>> identities;
        std::vector<std::vector<std::byte>> file_bytes;
    };

    const auto directory_paths_for = [](const std::filesystem::path& root) {
        return std::vector<std::filesystem::path>{
            root,
            root / L"shared2",
            root / L"shared2" / L"menu",
            root / L"shared2" / L"menu" / L"FaceLib",
            root / L"shared2" / L"sys",
            root / L"title",
            root / L"title" / L"00000001",
            root / L"title" / L"00000001" / L"00000002",
            root / L"title" / L"00000001" / L"00000002" / L"data",
            root / L"title" / L"00010000",
            root / L"title" / L"00010000" / L"524d4745",
            root / L"title" / L"00010000" / L"524d4745" / L"data",
            root / L"tmp",
        };
    };
    const auto file_paths_for = [](const std::filesystem::path& root) {
        return std::vector<std::filesystem::path>{
            root / L"shared2" / L"menu" / L"FaceLib" / L"RFL_DB.dat",
            root / L"shared2" / L"sys" / L"SYSCONF",
            root / L"title" / L"00000001" / L"00000002" / L"data" /
                L"setting.txt",
            root / L"title" / L"00000001" / L"00000002" / L"data" /
                L"play_rec.dat",
            root / L"title" / L"00010000" / L"524d4745" / L"data" /
                L"GameData.bin",
            root / L"title" / L"00010000" / L"524d4745" / L"data" /
                L"banner.bin",
            root / L"GameData.bin",
            root / L"banner.bin",
            root / L"shared2" / L"sys" / L"SYSCONF.stale.bak",
        };
    };
    const auto all_paths_for = [&](const std::filesystem::path& root) {
        std::vector<std::filesystem::path> paths = directory_paths_for(root);
        std::vector<std::filesystem::path> files = file_paths_for(root);
        paths.insert(paths.end(), files.begin(), files.end());
        return paths;
    };
    const auto guest_visible_paths_for = [&](
                                              const std::filesystem::path& root) {
        std::vector<std::filesystem::path> paths = directory_paths_for(root);
        const std::vector<std::filesystem::path> files = file_paths_for(root);
        paths.insert(paths.end(), files.begin(), files.begin() + 6u);
        return paths;
    };
    const auto cold_open_path = [&](std::string_view guest_path) {
        auto cold = std::make_unique<galaxy::host::GuestAddressSpace>();
        galaxy::GuestMemoryV1* const guest_memory = cold->guest_memory();
        const std::uint32_t handle = ios_open_path(
            *cold,
            guest_memory,
            kRequest,
            kPath,
            guest_path,
            1u);
        if (handle == 0u || handle >= 0x80000000u) return false;
        ios_close_request(
            *cold, guest_memory, kRequest + 0x40u, handle);
        const bool closed = cold->read_u32(kRequest + 0x44u) == 0u;
        acknowledge_ios_reply(guest_memory);
        return closed;
    };
    const auto cold_open_sysconf = [&]() {
        return cold_open_path(kSysconfPath);
    };

    bool passed = true;
    std::error_code cleanup_error;
    const std::filesystem::path seed_root =
        shared_test_root / L"legacy_cohort_seed";
    std::filesystem::remove_all(seed_root, cleanup_error);
    LegacyFixtureBytes fixture{
        valid_rmge_game_data_for_test(),
        valid_rmge_banner_for_test(2u),
        valid_rmge_banner_for_test(3u),
        valid_empty_rfl_database_for_test(),
        {},
        {},
        {},
    };
    {
        ScopedWideEnv set_seed_nand_root(L"GALAXY_NAND_ROOT", seed_root);
        if (!cold_open_sysconf()) {
            throw std::runtime_error(
                "failed to construct fresh modeled system-file seed");
        }
        const std::vector<std::filesystem::path> seed_files =
            file_paths_for(seed_root);
        fixture.sysconf = galaxy::host::read_binary_file(seed_files[1]);
        fixture.setting = galaxy::host::read_binary_file(seed_files[2]);
        fixture.play_rec = galaxy::host::read_binary_file(seed_files[3]);
    }
    std::filesystem::remove_all(seed_root, cleanup_error);

    const auto create_fixture = [&](const std::filesystem::path& root) {
        std::filesystem::remove_all(root, cleanup_error);
        const std::vector<std::filesystem::path> directories =
            directory_paths_for(root);
        for (const std::filesystem::path& directory : directories) {
            std::filesystem::create_directories(directory);
        }
        const std::vector<std::filesystem::path> files = file_paths_for(root);
        write_binary_file(files[0], fixture.rfl);
        write_binary_file(files[1], fixture.sysconf);
        write_binary_file(files[2], fixture.setting);
        write_binary_file(files[3], fixture.play_rec);
        write_binary_file(files[4], fixture.game_data);
        write_binary_file(files[5], fixture.canonical_banner);
        write_binary_file(files[6], fixture.game_data);
        write_binary_file(files[7], fixture.legacy_banner);
        write_binary_file(files[8], fixture.sysconf);
    };
    const auto capture_snapshot = [&](const std::filesystem::path& root) {
        LegacySnapshot snapshot;
        const std::vector<std::filesystem::path> all_paths = all_paths_for(root);
        const std::vector<std::filesystem::path> files = file_paths_for(root);
        snapshot.identities.reserve(all_paths.size());
        snapshot.file_bytes.reserve(files.size());
        for (const std::filesystem::path& path : all_paths) {
            snapshot.identities.push_back(host_file_identity_for_test(path));
        }
        for (const std::filesystem::path& file : files) {
            snapshot.file_bytes.push_back(
                galaxy::host::read_binary_file(file));
        }
        return snapshot;
    };
    const auto snapshot_matches = [&](const std::filesystem::path& root,
                                      const LegacySnapshot& expected,
                                      bool include_tmp = true) {
        const std::vector<std::filesystem::path> all_paths = all_paths_for(root);
        const std::vector<std::filesystem::path> files = file_paths_for(root);
        if (expected.identities.size() != all_paths.size() ||
            expected.file_bytes.size() != files.size()) {
            return false;
        }
        for (std::size_t index = 0u; index < all_paths.size(); ++index) {
            if (!include_tmp && all_paths[index] == root / L"tmp") continue;
            if (host_file_identity_for_test(all_paths[index]) !=
                expected.identities[index]) {
                return false;
            }
        }
        for (std::size_t index = 0u; index < files.size(); ++index) {
            if (galaxy::host::read_binary_file(files[index]) !=
                expected.file_bytes[index]) {
                return false;
            }
        }
        return true;
    };
    const auto all_gis3_streams_absent = [&](
                                              const std::filesystem::path& root) {
        for (const std::filesystem::path& path : all_paths_for(root)) {
            const std::filesystem::path metadata(
                path.native() + L":galaxy.isfs.meta");
            if (host_file_stream_exists_for_test(metadata)) return false;
        }
        return true;
    };
    const auto all_named_streams_absent = [&](
                                               const std::filesystem::path& root) {
        for (const std::filesystem::path& path : all_paths_for(root)) {
            if (!std::filesystem::exists(path)) continue;
            if (host_named_stream_count_for_test(path) != 0u) return false;
        }
        return true;
    };
    const auto recursive_relative_paths = [](
                                              const std::filesystem::path& root) {
        std::vector<std::filesystem::path> paths;
        std::error_code enumeration_error;
        std::filesystem::recursive_directory_iterator iterator(
            root, enumeration_error);
        const std::filesystem::recursive_directory_iterator end;
        while (!enumeration_error && iterator != end) {
            paths.push_back(iterator->path().lexically_relative(root));
            iterator.increment(enumeration_error);
        }
        if (enumeration_error) {
            throw std::runtime_error(
                "failed to enumerate legacy-cohort fixture snapshot");
        }
        std::ranges::sort(paths);
        return paths;
    };
    struct LegacyMigrationStep {
        std::filesystem::path path;
        std::vector<std::byte> metadata;
    };
    const auto migration_steps_for = [&](const std::filesystem::path& root) {
        const std::vector<std::filesystem::path> directories =
            directory_paths_for(root);
        const std::vector<std::filesystem::path> files = file_paths_for(root);
        const std::vector<std::byte> root_metadata =
            gis3_metadata_for_test(0u, 0u, 0u, 1u, 1u, 0u);
        const std::vector<std::byte> public_kernel =
            gis3_metadata_for_test(0u, 0u, 3u, 3u, 3u, 0u);
        const std::vector<std::byte> title =
            gis3_metadata_for_test(0u, 0u, 3u, 3u, 1u, 0u);
        const std::vector<std::byte> system_menu_public =
            gis3_metadata_for_test(0x1000u, 1u, 3u, 3u, 3u, 0u);
        const std::vector<std::byte> system_menu_data =
            gis3_metadata_for_test(0x1000u, 1u, 3u, 0u, 0u, 0u);
        const std::vector<std::byte> rmge_public =
            gis3_metadata_for_test(0x1001u, 0x3031u, 3u, 3u, 3u, 0u);
        const std::vector<std::byte> rmge_data =
            gis3_metadata_for_test(0x1001u, 0x3031u, 3u, 0u, 0u, 0u);
        const std::vector<std::byte> rmge_file =
            gis3_metadata_for_test(0x1001u, 0x3031u, 3u, 3u, 0u, 0u);
        return std::vector<LegacyMigrationStep>{
            {files[1], system_menu_public},
            {directories[1], public_kernel},
            {directories[2], rmge_public},
            {directories[3], rmge_public},
            {directories[4], system_menu_public},
            {directories[5], title},
            {directories[6], title},
            {directories[7], title},
            {directories[8], system_menu_data},
            {directories[9], title},
            {directories[10], title},
            {directories[11], rmge_data},
            {directories[12], public_kernel},
            {files[0], rmge_public},
            {files[2], system_menu_public},
            {files[3], system_menu_public},
            {files[4], rmge_file},
            {files[5], rmge_file},
            {directories[0], root_metadata},
        };
    };
    const auto metadata_stream_path = [](
                                          const std::filesystem::path& path) {
        return std::filesystem::path(path.native() + L":galaxy.isfs.meta");
    };
    const auto pending_stream_path = [](
                                         const std::filesystem::path& path) {
        return std::filesystem::path(
            path.native() + L":galaxy.isfs.meta.pending");
    };
    const auto stamp_migration_prefix = [&](const std::filesystem::path& root,
                                            std::size_t count) {
        const std::vector<LegacyMigrationStep> steps =
            migration_steps_for(root);
        if (count > steps.size()) {
            throw std::runtime_error("invalid legacy migration prefix length");
        }
        for (std::size_t index = 0u; index < count; ++index) {
            write_binary_file(
                metadata_stream_path(steps[index].path),
                steps[index].metadata);
        }
    };
    const auto migration_is_complete = [&](const std::filesystem::path& root) {
        for (const LegacyMigrationStep& step : migration_steps_for(root)) {
            if (host_named_stream_count_for_test(step.path) != 1u ||
                !host_file_stream_exists_for_test(
                    metadata_stream_path(step.path)) ||
                galaxy::host::read_binary_file(
                    metadata_stream_path(step.path)) != step.metadata ||
                host_file_stream_exists_for_test(
                    pending_stream_path(step.path))) {
                return false;
            }
        }
        const std::vector<std::filesystem::path> files = file_paths_for(root);
        return host_named_stream_count_for_test(files[6]) == 0u &&
            host_named_stream_count_for_test(files[7]) == 0u &&
            host_named_stream_count_for_test(files[8]) == 0u;
    };

    const std::filesystem::path positive_root =
        shared_test_root / L"legacy_cohort_positive";
    create_fixture(positive_root);
    {
        ScopedWideEnv set_positive_nand_root(
            L"GALAXY_NAND_ROOT", positive_root);
        const LegacySnapshot before = capture_snapshot(positive_root);
        const std::array<std::uint64_t, 2> tmp_identity =
            host_file_identity_for_test(positive_root / L"tmp");
        passed &= expect(
            all_named_streams_absent(positive_root),
            "exact pre-ADS legacy cohort starts with no named streams");
        passed &= expect(
            cold_open_sysconf(),
            "cold startup accepts and migrates the exact observed pre-ADS legacy cohort");
        const std::vector<std::filesystem::path> positive_files =
            file_paths_for(positive_root);
        passed &= expect(
            snapshot_matches(positive_root, before),
            "legacy migration retains every preexisting object identity and unnamed byte");
        passed &= expect(
            exact_gis3_metadata_for_test(
                positive_files[1], 0x1000u, 1u, 3u, 3u, 3u, 0u),
            "legacy migration establishes System Menu 0x1000/1/333 SYSCONF provenance first");
        passed &= expect(
            host_file_identity_for_test(positive_root / L"tmp") ==
                tmp_identity,
            "legacy migration stamps the empty tmp directory in place without replacing its identity");
        passed &= expect(
            host_named_stream_count_for_test(positive_files[6]) == 0u &&
                host_named_stream_count_for_test(positive_files[7]) == 0u &&
                host_named_stream_count_for_test(positive_files[8]) == 0u,
            "legacy migration leaves required root save and stale-SYSCONF sentinels stream-free");
        const LegacySnapshot after_first = capture_snapshot(positive_root);
        const std::array<std::uint64_t, 2> first_boot_tmp_identity =
            host_file_identity_for_test(positive_root / L"tmp");
        const std::filesystem::path sysconf_metadata(
            positive_files[1].native() + L":galaxy.isfs.meta");
        const std::vector<std::byte> sysconf_gis3 =
            galaxy::host::read_binary_file(sysconf_metadata);
        passed &= expect(
            cold_open_sysconf(),
            "a second cold startup accepts the fully migrated legacy cohort");
        passed &= expect(
            snapshot_matches(positive_root, after_first, false) &&
                host_file_identity_for_test(positive_root / L"tmp") !=
                    first_boot_tmp_identity &&
                std::filesystem::is_empty(positive_root / L"tmp") &&
                galaxy::host::read_binary_file(sysconf_metadata) == sysconf_gis3 &&
                host_named_stream_count_for_test(positive_files[6]) == 0u &&
                host_named_stream_count_for_test(positive_files[7]) == 0u &&
                host_named_stream_count_for_test(positive_files[8]) == 0u,
            "second cold startup preserves persistent identity/bytes, resets tmp to a fresh empty object, and preserves sentinels");

        const std::filesystem::path stripped_metadata(
            positive_files[2].native() + L":galaxy.isfs.meta");
        passed &= expect(
            DeleteFileW(stripped_metadata.c_str()) != FALSE &&
                !host_file_stream_exists_for_test(stripped_metadata),
            "modern strict-loader fixture removes exactly one setting.txt GIS3 stream");
        passed &= expect_runtime_error(
            [&] { static_cast<void>(cold_open_path(kSettingPath)); },
            "modern Open hard-fails a post-initialization object whose GIS3 was stripped");
        passed &= expect(
            !host_file_stream_exists_for_test(stripped_metadata),
            "failed modern Open does not recreate stripped GIS3 authority");
        passed &= expect_runtime_error(
            [&] {
                auto cold =
                    std::make_unique<galaxy::host::GuestAddressSpace>();
                galaxy::GuestMemoryV1* const guest_memory =
                    cold->guest_memory();
                const std::uint32_t fs_handle = ios_open_path(
                    *cold,
                    guest_memory,
                    kRequest,
                    kFsPath,
                    "/dev/fs");
                if (fs_handle == 0u || fs_handle >= 0x80000000u) {
                    return;
                }
                write_isfs_path_buffer(*cold, kPath, kSettingPath);
                ios_ioctl_request(
                    *cold,
                    guest_memory,
                    kRequest + 0x80u,
                    fs_handle,
                    6u,
                    kPath,
                    64u,
                    kAttrOut,
                    0x4Cu);
            },
            "modern GetAttr hard-fails a post-initialization object whose GIS3 was stripped");
        passed &= expect(
            !host_file_stream_exists_for_test(stripped_metadata),
            "failed modern GetAttr does not recreate stripped GIS3 authority");
    }
    std::filesystem::remove_all(positive_root, cleanup_error);

    const std::filesystem::path resume_root =
        shared_test_root / L"legacy_cohort_sysconf_final_resume";
    create_fixture(resume_root);
    {
        ScopedWideEnv set_resume_nand_root(L"GALAXY_NAND_ROOT", resume_root);
        const std::vector<std::filesystem::path> resume_files =
            file_paths_for(resume_root);
        const std::filesystem::path sysconf_metadata(
            resume_files[1].native() + L":galaxy.isfs.meta");
        const std::vector<std::byte> system_menu_gis3 =
            gis3_metadata_for_test(0x1000u, 1u, 3u, 3u, 3u, 0u);
        write_binary_file(sysconf_metadata, system_menu_gis3);
        bool exact_partial_state = true;
        for (const std::filesystem::path& path : all_paths_for(resume_root)) {
            exact_partial_state &= host_named_stream_count_for_test(path) ==
                (path == resume_files[1] ? 1u : 0u);
        }
        const LegacySnapshot before_resume = capture_snapshot(resume_root);
        passed &= expect(
            exact_partial_state &&
                galaxy::host::read_binary_file(sysconf_metadata) ==
                    system_menu_gis3,
            "legacy crash-resume fixture has only durable System Menu SYSCONF authority");
        passed &= expect(
            cold_open_sysconf(),
            "cold startup resumes a SYS-first legacy migration after final SYSCONF GIS3");
        bool metadata_complete = true;
        for (const std::filesystem::path& path :
             guest_visible_paths_for(resume_root)) {
            const std::filesystem::path metadata(
                path.native() + L":galaxy.isfs.meta");
            metadata_complete &=
                host_file_stream_exists_for_test(metadata) &&
                host_named_stream_count_for_test(path) == 1u;
        }
        passed &= expect(
            snapshot_matches(resume_root, before_resume) && metadata_complete &&
                exact_gis3_metadata_for_test(
                    resume_files[1], 0x1000u, 1u, 3u, 3u, 3u, 0u) &&
                host_named_stream_count_for_test(resume_files[6]) == 0u &&
                host_named_stream_count_for_test(resume_files[7]) == 0u &&
                host_named_stream_count_for_test(resume_files[8]) == 0u,
            "SYS-first resume completes every planned GIS3 in place and leaves sentinels stream-free");
    }
    std::filesystem::remove_all(resume_root, cleanup_error);

    const std::filesystem::path resume_gap_root =
        shared_test_root / L"legacy_cohort_resume_gap";
    create_fixture(resume_gap_root);
    {
        ScopedWideEnv set_resume_gap_nand_root(
            L"GALAXY_NAND_ROOT", resume_gap_root);
        const std::vector<std::filesystem::path> gap_files =
            file_paths_for(resume_gap_root);
        const std::vector<std::byte> system_menu_gis3 =
            gis3_metadata_for_test(0x1000u, 1u, 3u, 3u, 3u, 0u);
        const std::filesystem::path sysconf_metadata(
            gap_files[1].native() + L":galaxy.isfs.meta");
        const std::filesystem::path play_rec_metadata(
            gap_files[3].native() + L":galaxy.isfs.meta");
        write_binary_file(sysconf_metadata, system_menu_gis3);
        write_binary_file(play_rec_metadata, system_menu_gis3);
        const LegacySnapshot before_gap = capture_snapshot(resume_gap_root);
        passed &= expect_runtime_error(
            [&] { static_cast<void>(cold_open_sysconf()); },
            "legacy crash-resume hard-fails a late play-rec GIS3 after an earlier migration gap");
        bool only_original_streams = true;
        for (const std::filesystem::path& path : all_paths_for(resume_gap_root)) {
            const std::size_t expected_count =
                path == gap_files[1] || path == gap_files[3] ? 1u : 0u;
            only_original_streams &=
                host_named_stream_count_for_test(path) == expected_count;
        }
        passed &= expect(
            snapshot_matches(resume_gap_root, before_gap) &&
                only_original_streams &&
                galaxy::host::read_binary_file(sysconf_metadata) ==
                    system_menu_gis3 &&
                galaxy::host::read_binary_file(play_rec_metadata) ==
                    system_menu_gis3,
            "legacy resume-gap rejection preserves every object, byte, and original GIS3 without mutation");
    }
    std::filesystem::remove_all(resume_gap_root, cleanup_error);

    const std::filesystem::path torn_sys_root =
        shared_test_root / L"legacy_cohort_torn_sys_pending";
    create_fixture(torn_sys_root);
    {
        ScopedWideEnv set_torn_sys_nand_root(
            L"GALAXY_NAND_ROOT", torn_sys_root);
        const std::vector<LegacyMigrationStep> steps =
            migration_steps_for(torn_sys_root);
        const LegacyMigrationStep& sys_step = steps[0];
        const std::filesystem::path pending =
            pending_stream_path(sys_step.path);
        write_binary_file(pending, std::span<const std::byte>{});
        const LegacySnapshot before = capture_snapshot(torn_sys_root);
        passed &= expect(
            host_file_stream_exists_for_test(pending) &&
                galaxy::host::read_binary_file(pending).empty(),
            "legacy SYS crash fixture carries an exact zero-byte pending GIS3 stream");
        passed &= expect(
            cold_open_sysconf(),
            "cold startup recovers a torn first-SYS pending GIS3 in the exact legacy cohort");
        passed &= expect(
            snapshot_matches(torn_sys_root, before) &&
                migration_is_complete(torn_sys_root),
            "first-SYS torn-pending recovery preserves every object/byte and completes SYS-first migration");
    }
    std::filesystem::remove_all(torn_sys_root, cleanup_error);

    const std::filesystem::path torn_mid_root =
        shared_test_root / L"legacy_cohort_torn_mid_pending";
    create_fixture(torn_mid_root);
    {
        ScopedWideEnv set_torn_mid_nand_root(
            L"GALAXY_NAND_ROOT", torn_mid_root);
        constexpr std::size_t kCurrentStep = 4u;
        stamp_migration_prefix(torn_mid_root, kCurrentStep);
        const std::vector<LegacyMigrationStep> steps =
            migration_steps_for(torn_mid_root);
        const LegacyMigrationStep& current = steps[kCurrentStep];
        const std::filesystem::path pending =
            pending_stream_path(current.path);
        write_binary_file(
            pending,
            std::span<const std::byte>(
                current.metadata.data(), 7u));
        const LegacySnapshot before = capture_snapshot(torn_mid_root);
        passed &= expect(
            cold_open_sysconf(),
            "cold startup recovers a truncated pending GIS3 at the deterministic legacy mid-prefix");
        passed &= expect(
            snapshot_matches(torn_mid_root, before) &&
                migration_is_complete(torn_mid_root),
            "mid-prefix torn-pending recovery retains every identity/byte and completes root-last");
    }
    std::filesystem::remove_all(torn_mid_root, cleanup_error);

    const std::filesystem::path torn_final_root =
        shared_test_root / L"legacy_cohort_valid_pending_torn_final";
    create_fixture(torn_final_root);
    {
        ScopedWideEnv set_torn_final_nand_root(
            L"GALAXY_NAND_ROOT", torn_final_root);
        constexpr std::size_t kCurrentStep = 4u;
        stamp_migration_prefix(torn_final_root, kCurrentStep);
        const std::vector<LegacyMigrationStep> steps =
            migration_steps_for(torn_final_root);
        const LegacyMigrationStep& current = steps[kCurrentStep];
        const std::filesystem::path pending =
            pending_stream_path(current.path);
        const std::filesystem::path final =
            metadata_stream_path(current.path);
        write_binary_file(pending, current.metadata);
        write_binary_file(
            final,
            std::span<const std::byte>(
                current.metadata.data(), 7u));
        const LegacySnapshot before = capture_snapshot(torn_final_root);
        passed &= expect(
            cold_open_sysconf(),
            "cold startup uses exact pending GIS3 authority to recover its truncated final stream");
        passed &= expect(
            snapshot_matches(torn_final_root, before) &&
                migration_is_complete(torn_final_root),
            "valid-pending/torn-final recovery preserves the legacy objects and completes migration");
    }
    std::filesystem::remove_all(torn_final_root, cleanup_error);

    const std::filesystem::path torn_root_commit =
        shared_test_root / L"legacy_cohort_torn_root_pending";
    create_fixture(torn_root_commit);
    {
        ScopedWideEnv set_torn_root_nand_root(
            L"GALAXY_NAND_ROOT", torn_root_commit);
        const std::vector<LegacyMigrationStep> steps =
            migration_steps_for(torn_root_commit);
        stamp_migration_prefix(torn_root_commit, steps.size() - 1u);
        const LegacyMigrationStep& root_step = steps.back();
        const std::filesystem::path pending =
            pending_stream_path(root_step.path);
        write_binary_file(
            pending,
            std::span<const std::byte>(
                root_step.metadata.data(), 7u));
        const LegacySnapshot before = capture_snapshot(torn_root_commit);
        passed &= expect(
            cold_open_sysconf(),
            "cold startup retries the root-last commit after a truncated root pending GIS3");
        passed &= expect(
            snapshot_matches(torn_root_commit, before) &&
                migration_is_complete(torn_root_commit),
            "root-last torn-pending recovery preserves the exact migrated cohort and commits final-only root authority");
    }
    std::filesystem::remove_all(torn_root_commit, cleanup_error);

    const std::filesystem::path ambiguous_root =
        shared_test_root / L"legacy_cohort_ambiguous_pending_final";
    create_fixture(ambiguous_root);
    {
        ScopedWideEnv set_ambiguous_nand_root(
            L"GALAXY_NAND_ROOT", ambiguous_root);
        const std::vector<LegacyMigrationStep> steps =
            migration_steps_for(ambiguous_root);
        const LegacyMigrationStep& sys_step = steps[0];
        const std::filesystem::path pending =
            pending_stream_path(sys_step.path);
        const std::filesystem::path final =
            metadata_stream_path(sys_step.path);
        write_binary_file(
            pending,
            std::span<const std::byte>(
                sys_step.metadata.data(), 7u));
        write_binary_file(final, sys_step.metadata);
        const LegacySnapshot before = capture_snapshot(ambiguous_root);
        const std::vector<std::byte> pending_before =
            galaxy::host::read_binary_file(pending);
        const std::vector<std::byte> final_before =
            galaxy::host::read_binary_file(final);
        passed &= expect_runtime_error(
            [&] { static_cast<void>(cold_open_sysconf()); },
            "legacy startup hard-fails ambiguous invalid-pending plus valid-final GIS3");
        passed &= expect(
            snapshot_matches(ambiguous_root, before) &&
                host_named_stream_count_for_test(sys_step.path) == 2u &&
                galaxy::host::read_binary_file(pending) == pending_before &&
                galaxy::host::read_binary_file(final) == final_before,
            "ambiguous pending/final rejection preserves every object, byte, and raw ADS");
    }
    std::filesystem::remove_all(ambiguous_root, cleanup_error);

    const std::filesystem::path orphan_final_root =
        shared_test_root / L"legacy_cohort_orphan_torn_final";
    create_fixture(orphan_final_root);
    {
        ScopedWideEnv set_orphan_final_nand_root(
            L"GALAXY_NAND_ROOT", orphan_final_root);
        const std::vector<LegacyMigrationStep> steps =
            migration_steps_for(orphan_final_root);
        const LegacyMigrationStep& sys_step = steps[0];
        const std::filesystem::path final =
            metadata_stream_path(sys_step.path);
        write_binary_file(
            final,
            std::span<const std::byte>(
                sys_step.metadata.data(), 7u));
        const LegacySnapshot before = capture_snapshot(orphan_final_root);
        const std::vector<std::byte> final_before =
            galaxy::host::read_binary_file(final);
        passed &= expect_runtime_error(
            [&] { static_cast<void>(cold_open_sysconf()); },
            "legacy startup hard-fails a truncated final GIS3 without pending authority");
        passed &= expect(
            snapshot_matches(orphan_final_root, before) &&
                host_named_stream_count_for_test(sys_step.path) == 1u &&
                !host_file_stream_exists_for_test(
                    pending_stream_path(sys_step.path)) &&
                galaxy::host::read_binary_file(final) == final_before,
            "orphan torn-final rejection preserves every object, byte, and raw final ADS");
    }
    std::filesystem::remove_all(orphan_final_root, cleanup_error);

    const std::filesystem::path missing_root =
        shared_test_root / L"legacy_cohort_missing_sentinel";
    create_fixture(missing_root);
    {
        ScopedWideEnv set_missing_nand_root(
            L"GALAXY_NAND_ROOT", missing_root);
        const std::filesystem::path missing_banner = missing_root / L"banner.bin";
        if (!std::filesystem::remove(missing_banner)) {
            throw std::runtime_error(
                "failed to remove required legacy banner sentinel fixture");
        }
        passed &= expect(
            all_named_streams_absent(missing_root),
            "missing-sentinel legacy fixture starts without metadata authority");
        passed &= expect_runtime_error(
            [&] { static_cast<void>(cold_open_sysconf()); },
            "legacy startup hard-fails when a required root sentinel is missing");
        passed &= expect(
            all_named_streams_absent(missing_root),
            "missing-sentinel rejection publishes no GIS3 pending/final stream");
    }
    std::filesystem::remove_all(missing_root, cleanup_error);

    const std::filesystem::path unknown_ads_root =
        shared_test_root / L"legacy_cohort_unknown_ads";
    create_fixture(unknown_ads_root);
    {
        ScopedWideEnv set_unknown_ads_nand_root(
            L"GALAXY_NAND_ROOT", unknown_ads_root);
        const std::vector<std::filesystem::path> unknown_ads_files =
            file_paths_for(unknown_ads_root);
        const std::filesystem::path unknown_ads(
            unknown_ads_files[4].native() + L":galaxy.unknown");
        constexpr std::array<std::byte, 4> kUnknownAdsBytes{
            std::byte{'N'}, std::byte{'O'}, std::byte{'P'}, std::byte{'E'}};
        write_binary_file(unknown_ads, kUnknownAdsBytes);
        passed &= expect(
            all_gis3_streams_absent(unknown_ads_root) &&
                host_named_stream_count_for_test(unknown_ads_files[4]) == 1u,
            "unknown-ADS legacy fixture contains only its deliberate unrecognized stream");
        passed &= expect_runtime_error(
            [&] { static_cast<void>(cold_open_sysconf()); },
            "legacy startup hard-fails an otherwise exact cohort with an unknown ADS");
        bool no_extra_streams = true;
        for (const std::filesystem::path& path : all_paths_for(unknown_ads_root)) {
            const std::size_t expected_count = path == unknown_ads_files[4]
                ? 1u
                : 0u;
            no_extra_streams &=
                host_named_stream_count_for_test(path) == expected_count;
        }
        passed &= expect(
            all_gis3_streams_absent(unknown_ads_root) && no_extra_streams &&
                galaxy::host::read_binary_file(unknown_ads) ==
                    std::vector<std::byte>(
                        kUnknownAdsBytes.begin(), kUnknownAdsBytes.end()),
            "unknown-ADS rejection preserves the poison stream and publishes no GIS3 authority");
    }
    std::filesystem::remove_all(unknown_ads_root, cleanup_error);

    const std::filesystem::path candidate_root =
        shared_test_root / L"legacy_cohort_internal_candidate";
    create_fixture(candidate_root);
    {
        ScopedWideEnv set_candidate_nand_root(
            L"GALAXY_NAND_ROOT", candidate_root);
        const std::filesystem::path candidate =
            candidate_root / L"title" / L"00010000" / L"524d4745" /
            L"data" / L"GameData.bin.__galaxy_isfs_candidate__.4242.1";
        constexpr std::array<std::byte, 8> kCandidateBytes{
            std::byte{'C'}, std::byte{'A'}, std::byte{'N'}, std::byte{'D'},
            std::byte{'I'}, std::byte{'D'}, std::byte{'A'}, std::byte{'T'}};
        write_binary_file(candidate, kCandidateBytes);
        const LegacySnapshot before_candidate = capture_snapshot(candidate_root);
        const std::vector<std::filesystem::path> paths_before =
            recursive_relative_paths(candidate_root);
        const std::array<std::uint64_t, 2> candidate_identity =
            host_file_identity_for_test(candidate);
        passed &= expect(
            all_gis3_streams_absent(candidate_root) &&
                host_named_stream_count_for_test(candidate) == 0u,
            "pre-ADS candidate fixture begins entirely stream-free");
        passed &= expect_runtime_error(
            [&] { static_cast<void>(cold_open_sysconf()); },
            "pre-ADS startup hard-fails an exact internal candidate before recovery authority exists");
        passed &= expect(
            snapshot_matches(candidate_root, before_candidate) &&
                recursive_relative_paths(candidate_root) == paths_before &&
                host_file_identity_for_test(candidate) == candidate_identity &&
                galaxy::host::read_binary_file(candidate) ==
                    std::vector<std::byte>(
                        kCandidateBytes.begin(), kCandidateBytes.end()) &&
                all_gis3_streams_absent(candidate_root) &&
                host_named_stream_count_for_test(candidate) == 0u,
            "pre-authority candidate rejection preserves the exact artifact, tree identities, bytes, and stream inventory");
    }
    std::filesystem::remove_all(candidate_root, cleanup_error);
    return passed;
}

bool native_nand_fresh_root_intent_recovery_works(
    const std::filesystem::path& shared_test_root) {
    constexpr std::uint32_t kPath = 0x13508000u;
    constexpr std::uint32_t kRequest = 0x13508100u;
    constexpr std::array<std::wstring_view, 13> kFreshPaths{
        L"title",
        L"title/00000001",
        L"title/00000001/00000002",
        L"title/00000001/00000002/data",
        L"shared2",
        L"shared2/sys",
        L"title/00010000",
        L"title/00010000/524d4745",
        L"title/00010000/524d4745/data",
        L"tmp",
        L"shared2/sys/SYSCONF",
        L"title/00000001/00000002/data/setting.txt",
        L"title/00000001/00000002/data/play_rec.dat",
    };
    constexpr std::array<std::array<std::uint32_t, 6>, 14> kFreshMetadata{{
        {0u, 0u, 0u, 1u, 1u, 0u},
        {0u, 0u, 3u, 3u, 1u, 0u},
        {0u, 0u, 3u, 3u, 1u, 0u},
        {0u, 0u, 3u, 3u, 1u, 0u},
        {0x1000u, 1u, 3u, 0u, 0u, 0u},
        {0u, 0u, 3u, 3u, 3u, 0u},
        {0x1000u, 1u, 3u, 3u, 3u, 0u},
        {0u, 0u, 3u, 3u, 1u, 0u},
        {0u, 0u, 3u, 3u, 1u, 0u},
        {0x1001u, 0x3031u, 3u, 0u, 0u, 0u},
        {0u, 0u, 3u, 3u, 3u, 0u},
        {0x1000u, 1u, 3u, 3u, 3u, 0u},
        {0x1000u, 1u, 3u, 3u, 3u, 0u},
        {0x1000u, 1u, 3u, 3u, 3u, 0u},
    }};
    struct FreshSnapshot {
        std::vector<std::filesystem::path> paths;
        std::vector<std::array<std::uint64_t, 2>> identities;
        std::vector<std::vector<std::byte>> bytes;
    };

    const auto metadata_bytes = [&](std::size_t index) {
        const auto& value = kFreshMetadata[index];
        return gis3_metadata_for_test(
            value[0],
            static_cast<std::uint16_t>(value[1]),
            static_cast<std::uint8_t>(value[2]),
            static_cast<std::uint8_t>(value[3]),
            static_cast<std::uint8_t>(value[4]),
            static_cast<std::uint8_t>(value[5]));
    };
    const auto root_pending_path = [](const std::filesystem::path& root) {
        return std::filesystem::path(
            root.native() + L":galaxy.isfs.meta.pending");
    };
    const auto root_final_path = [](const std::filesystem::path& root) {
        return std::filesystem::path(root.native() + L":galaxy.isfs.meta");
    };
    const auto all_fresh_paths = [&](const std::filesystem::path& root) {
        std::vector<std::filesystem::path> paths{root};
        paths.reserve(kFreshPaths.size() + 1u);
        for (const std::wstring_view relative : kFreshPaths) {
            paths.push_back(root / relative);
        }
        return paths;
    };
    const auto capture_snapshot = [&](const std::filesystem::path& root) {
        FreshSnapshot snapshot;
        snapshot.paths.push_back(root);
        std::error_code enumeration_error;
        std::filesystem::recursive_directory_iterator iterator(
            root, enumeration_error);
        const std::filesystem::recursive_directory_iterator end;
        while (!enumeration_error && iterator != end) {
            snapshot.paths.push_back(iterator->path());
            iterator.increment(enumeration_error);
        }
        if (enumeration_error) {
            throw std::runtime_error(
                "failed to enumerate fresh-intent fixture snapshot");
        }
        std::ranges::sort(snapshot.paths);
        snapshot.identities.reserve(snapshot.paths.size());
        snapshot.bytes.reserve(snapshot.paths.size());
        for (const std::filesystem::path& path : snapshot.paths) {
            snapshot.identities.push_back(host_file_identity_for_test(path));
            snapshot.bytes.push_back(std::filesystem::is_regular_file(path)
                ? galaxy::host::read_binary_file(path)
                : std::vector<std::byte>{});
        }
        return snapshot;
    };
    const auto snapshot_matches = [](const FreshSnapshot& snapshot) {
        for (std::size_t index = 0u; index < snapshot.paths.size(); ++index) {
            if (!std::filesystem::exists(snapshot.paths[index]) ||
                host_file_identity_for_test(snapshot.paths[index]) !=
                    snapshot.identities[index]) {
                return false;
            }
            if (!snapshot.bytes[index].empty() &&
                galaxy::host::read_binary_file(snapshot.paths[index]) !=
                    snapshot.bytes[index]) {
                return false;
            }
        }
        return true;
    };
    const auto snapshot_matches_except = [](
                                             const FreshSnapshot& snapshot,
                                             const std::filesystem::path& excluded) {
        for (std::size_t index = 0u; index < snapshot.paths.size(); ++index) {
            if (snapshot.paths[index] == excluded) continue;
            if (!std::filesystem::exists(snapshot.paths[index]) ||
                host_file_identity_for_test(snapshot.paths[index]) !=
                    snapshot.identities[index]) {
                return false;
            }
            if (!snapshot.bytes[index].empty() &&
                galaxy::host::read_binary_file(snapshot.paths[index]) !=
                    snapshot.bytes[index]) {
                return false;
            }
        }
        return true;
    };
    const auto cold_open_sysconf = [&]() {
        auto cold = std::make_unique<galaxy::host::GuestAddressSpace>();
        galaxy::GuestMemoryV1* const guest_memory = cold->guest_memory();
        const std::uint32_t handle = ios_open_path(
            *cold,
            guest_memory,
            kRequest,
            kPath,
            "/shared2/sys/SYSCONF",
            1u);
        if (handle == 0u || handle >= 0x80000000u) return false;
        ios_close_request(
            *cold, guest_memory, kRequest + 0x40u, handle);
        const bool closed = cold->read_u32(kRequest + 0x44u) == 0u;
        acknowledge_ios_reply(guest_memory);
        return closed;
    };

    bool passed = true;
    std::error_code cleanup_error;

    const std::filesystem::path torn_empty_root =
        shared_test_root / L"fresh_intent_torn_empty_root_pending";
    std::filesystem::remove_all(torn_empty_root, cleanup_error);
    std::filesystem::create_directories(torn_empty_root);
    {
        ScopedWideEnv set_torn_empty_root(
            L"GALAXY_NAND_ROOT", torn_empty_root);
        const std::array<std::uint64_t, 2> root_identity =
            host_file_identity_for_test(torn_empty_root);
        write_binary_file(
            root_pending_path(torn_empty_root),
            std::span<const std::byte>{});
        passed &= expect(
            std::filesystem::is_empty(torn_empty_root) &&
                host_file_stream_exists_for_test(
                    root_pending_path(torn_empty_root)) &&
                galaxy::host::read_binary_file(
                    root_pending_path(torn_empty_root)).empty(),
            "fresh-empty crash fixture contains only a zero-byte root pending GIS3");
        passed &= expect(
            cold_open_sysconf(),
            "fresh startup recovers a torn root pending GIS3 before creating the deterministic core");
        const std::vector<std::filesystem::path> paths =
            all_fresh_paths(torn_empty_root);
        bool complete = paths.size() == kFreshMetadata.size();
        for (std::size_t index = 0u; index < paths.size(); ++index) {
            complete &= std::filesystem::exists(paths[index]) &&
                galaxy::host::read_binary_file(
                    std::filesystem::path(
                        paths[index].native() + L":galaxy.isfs.meta")) ==
                    metadata_bytes(index);
        }
        passed &= expect(
            host_file_identity_for_test(torn_empty_root) == root_identity &&
                !host_file_stream_exists_for_test(
                    root_pending_path(torn_empty_root)) &&
                complete,
            "fresh torn-root recovery preserves root identity and publishes exact final-only core authority");
    }
    std::filesystem::remove_all(torn_empty_root, cleanup_error);

    const std::filesystem::path seed_root =
        shared_test_root / L"fresh_intent_seed";
    std::filesystem::remove_all(seed_root, cleanup_error);
    std::filesystem::create_directories(seed_root);
    std::vector<std::byte> sysconf;
    std::vector<std::byte> setting;
    std::vector<std::byte> play_rec;
    {
        ScopedWideEnv set_seed_root(L"GALAXY_NAND_ROOT", seed_root);
        if (!cold_open_sysconf()) {
            throw std::runtime_error("failed to create fresh-intent seed NAND");
        }
        const std::vector<std::filesystem::path> paths =
            all_fresh_paths(seed_root);
        sysconf = galaxy::host::read_binary_file(paths[11]);
        setting = galaxy::host::read_binary_file(paths[12]);
        play_rec = galaxy::host::read_binary_file(paths[13]);
    }
    std::filesystem::remove_all(seed_root, cleanup_error);

    const auto create_prefix_fixture = [&](const std::filesystem::path& root,
                                           std::size_t prefix_length,
                                           bool root_final) {
        std::filesystem::remove_all(root, cleanup_error);
        std::filesystem::create_directories(root);
        write_binary_file(root_pending_path(root), metadata_bytes(0u));
        if (root_final) {
            write_binary_file(root_final_path(root), metadata_bytes(0u));
        }
        for (std::size_t step = 0u; step < prefix_length; ++step) {
            const std::filesystem::path path = root / kFreshPaths[step];
            if (step < 10u) {
                std::filesystem::create_directories(path);
            } else {
                const std::vector<std::byte>& bytes = step == 10u
                    ? sysconf
                    : (step == 11u ? setting : play_rec);
                write_binary_file(path, bytes);
            }
            write_binary_file(
                std::filesystem::path(path.native() + L":galaxy.isfs.meta"),
                metadata_bytes(step + 1u));
        }
    };

    const std::filesystem::path resume_root =
        shared_test_root / L"fresh_intent_prefix_resume";
    create_prefix_fixture(resume_root, 7u, false);
    {
        ScopedWideEnv set_resume_root(L"GALAXY_NAND_ROOT", resume_root);
        const FreshSnapshot before = capture_snapshot(resume_root);
        passed &= expect(
            cold_open_sysconf(),
            "fresh root-pending startup resumes the exact deterministic seven-object prefix");
        passed &= expect(
            snapshot_matches(before) &&
                !host_file_stream_exists_for_test(root_pending_path(resume_root)) &&
                galaxy::host::read_binary_file(root_final_path(resume_root)) ==
                    metadata_bytes(0u),
            "fresh prefix resume preserves every preexisting identity/byte and commits root final-only");
        const std::vector<std::filesystem::path> paths =
            all_fresh_paths(resume_root);
        bool complete = paths.size() == kFreshMetadata.size();
        for (std::size_t index = 0u; index < paths.size(); ++index) {
            complete &= std::filesystem::exists(paths[index]) &&
                galaxy::host::read_binary_file(
                    std::filesystem::path(
                        paths[index].native() + L":galaxy.isfs.meta")) ==
                    metadata_bytes(index);
        }
        passed &= expect(
            complete,
            "fresh prefix resume publishes the exact complete generated core and metadata");
    }
    std::filesystem::remove_all(resume_root, cleanup_error);

    const std::filesystem::path torn_complete_root =
        shared_test_root / L"fresh_intent_valid_pending_torn_final";
    create_prefix_fixture(torn_complete_root, kFreshPaths.size(), false);
    {
        ScopedWideEnv set_torn_complete_root(
            L"GALAXY_NAND_ROOT", torn_complete_root);
        const std::filesystem::path final =
            root_final_path(torn_complete_root);
        const std::vector<std::byte> root_metadata = metadata_bytes(0u);
        write_binary_file(
            final,
            std::span<const std::byte>(root_metadata.data(), 7u));
        const FreshSnapshot before = capture_snapshot(torn_complete_root);
        const std::array<std::uint64_t, 2> tmp_identity =
            host_file_identity_for_test(torn_complete_root / L"tmp");
        passed &= expect(
            cold_open_sysconf(),
            "fresh complete startup uses valid root pending authority to recover a truncated root final GIS3");
        passed &= expect(
            snapshot_matches_except(
                before, torn_complete_root / L"tmp") &&
                host_file_identity_for_test(torn_complete_root / L"tmp") !=
                    tmp_identity &&
                std::filesystem::is_empty(torn_complete_root / L"tmp") &&
                !host_file_stream_exists_for_test(
                    root_pending_path(torn_complete_root)) &&
                galaxy::host::read_binary_file(final) == root_metadata,
            "fresh valid-pending/torn-final recovery preserves persistent identities/bytes, commits root, and resets tmp");
    }
    std::filesystem::remove_all(torn_complete_root, cleanup_error);

    const std::filesystem::path gap_root =
        shared_test_root / L"fresh_intent_gap";
    create_prefix_fixture(gap_root, 3u, false);
    {
        ScopedWideEnv set_gap_root(L"GALAXY_NAND_ROOT", gap_root);
        const std::filesystem::path later = gap_root / kFreshPaths[4];
        std::filesystem::create_directories(later);
        write_binary_file(
            std::filesystem::path(later.native() + L":galaxy.isfs.meta"),
            metadata_bytes(5u));
        const FreshSnapshot before = capture_snapshot(gap_root);
        const std::vector<std::byte> pending_before =
            galaxy::host::read_binary_file(root_pending_path(gap_root));
        passed &= expect_runtime_error(
            [&] { static_cast<void>(cold_open_sysconf()); },
            "fresh root-pending startup hard-fails a deterministic-prefix gap");
        passed &= expect(
            snapshot_matches(before) &&
                galaxy::host::read_binary_file(root_pending_path(gap_root)) ==
                    pending_before &&
                !host_file_stream_exists_for_test(root_final_path(gap_root)),
            "fresh prefix-gap rejection performs no object, byte, or root-stream mutation");
    }
    std::filesystem::remove_all(gap_root, cleanup_error);

    const std::filesystem::path final_pending_root =
        shared_test_root / L"fresh_intent_final_pending_incomplete";
    create_prefix_fixture(final_pending_root, 4u, true);
    {
        ScopedWideEnv set_final_pending_root(
            L"GALAXY_NAND_ROOT", final_pending_root);
        const FreshSnapshot before = capture_snapshot(final_pending_root);
        const std::vector<std::byte> pending_before =
            galaxy::host::read_binary_file(root_pending_path(final_pending_root));
        const std::vector<std::byte> final_before =
            galaxy::host::read_binary_file(root_final_path(final_pending_root));
        passed &= expect_runtime_error(
            [&] { static_cast<void>(cold_open_sysconf()); },
            "fresh startup hard-fails root pending+final with an incomplete core prefix");
        passed &= expect(
            snapshot_matches(before) &&
                galaxy::host::read_binary_file(
                    root_pending_path(final_pending_root)) == pending_before &&
                galaxy::host::read_binary_file(
                    root_final_path(final_pending_root)) == final_before,
            "incomplete root pending+final rejection preserves every object and intent stream");
    }
    std::filesystem::remove_all(final_pending_root, cleanup_error);

    const std::filesystem::path missing_modern_root =
        shared_test_root / L"fresh_modern_missing_core";
    std::filesystem::create_directories(missing_modern_root);
    {
        ScopedWideEnv set_modern_root(
            L"GALAXY_NAND_ROOT", missing_modern_root);
        passed &= expect(
            cold_open_sysconf(),
            "fresh strict-core fixture creates a complete modern NAND");
        const std::filesystem::path missing =
            missing_modern_root / L"title" / L"00000001" / L"00000002" /
            L"data" / L"play_rec.dat";
        const std::filesystem::path missing_metadata(
            missing.native() + L":galaxy.isfs.meta");
        passed &= expect(
            DeleteFileW(missing.c_str()) != FALSE &&
                !std::filesystem::exists(missing),
            "fresh strict-core fixture removes exactly one required modern child");
        const std::vector<std::byte> root_final =
            galaxy::host::read_binary_file(root_final_path(missing_modern_root));
        passed &= expect_runtime_error(
            [&] { static_cast<void>(cold_open_sysconf()); },
            "modern root-final startup hard-fails a missing required core child");
        passed &= expect(
            !std::filesystem::exists(missing) &&
                !host_file_stream_exists_for_test(missing_metadata) &&
                galaxy::host::read_binary_file(
                    root_final_path(missing_modern_root)) == root_final,
            "modern missing-core rejection does not recreate the child or alter root authority");
    }
    std::filesystem::remove_all(missing_modern_root, cleanup_error);
    return passed;
}

bool native_nand_invalid_sole_legacy_hard_fails(
    const std::filesystem::path& shared_test_root) {
    constexpr std::string_view kCanonicalGameData =
        "/title/00010000/524d4745/data/GameData.bin";
    constexpr std::uint32_t kPath = 0x13510000u;
    constexpr std::uint32_t kRequest = 0x13510100u;
    constexpr std::array<std::byte, 8> kInvalidLegacy{
        std::byte{'N'}, std::byte{'O'}, std::byte{'T'}, std::byte{'-'},
        std::byte{'R'}, std::byte{'M'}, std::byte{'G'}, std::byte{'E'}};

    bool passed = true;
    const std::filesystem::path isolated_root =
        shared_test_root / L"invalid_sole_legacy";
    std::error_code cleanup_error;
    std::filesystem::remove_all(isolated_root, cleanup_error);
    const std::filesystem::path legacy = isolated_root / L"GameData.bin";
    write_binary_file(legacy, kInvalidLegacy);
    ScopedWideEnv set_isolated_nand_root(L"GALAXY_NAND_ROOT", isolated_root);

    {
        galaxy::host::GuestAddressSpace memory;
        galaxy::GuestMemoryV1* const guest_memory = memory.guest_memory();
        passed &= expect_runtime_error(
            [&] {
                static_cast<void>(ios_open_path(
                    memory,
                    guest_memory,
                    kRequest,
                    kPath,
                    kCanonicalGameData,
                    1u));
            },
            "an invalid sole root-level legacy GameData hard-fails migration");
    }
    const std::filesystem::path canonical =
        isolated_root / L"title" / L"00010000" / L"524d4745" / L"data" /
        L"GameData.bin";
    passed &= expect(
        galaxy::host::read_binary_file(legacy) ==
                std::vector<std::byte>(
                    kInvalidLegacy.begin(), kInvalidLegacy.end()) &&
            !std::filesystem::exists(canonical),
        "rejected sole legacy migration preserves the source and publishes no canonical save");

    std::filesystem::remove_all(isolated_root, cleanup_error);
    return passed;
}

bool native_nand_provisional_known_file_contract_works(
    const std::filesystem::path& shared_test_root) {
    constexpr std::string_view kGameDataPath =
        "/title/00010000/524d4745/data/GameData.bin";
    constexpr std::uint32_t kPath = 0x13520000u;
    constexpr std::uint32_t kFsPath = 0x13520100u;
    constexpr std::uint32_t kAttr = 0x13520200u;
    constexpr std::uint32_t kRequest = 0x13520400u;
    constexpr std::uint32_t kBuffer = 0x13530000u;
    constexpr std::uint32_t kIsfsErrInvalid =
        static_cast<std::uint32_t>(-101);
    constexpr std::uint32_t kIsfsErrAccess =
        static_cast<std::uint32_t>(-102);
    constexpr std::uint32_t kIsfsErrNoExist =
        static_cast<std::uint32_t>(-106);

    const auto canonical_path_for = [](const std::filesystem::path& root) {
        return root / L"title" / L"00010000" / L"524d4745" / L"data" /
            L"GameData.bin";
    };
    const auto close_handle = [](
                                  galaxy::host::GuestAddressSpace& memory,
                                  galaxy::GuestMemoryV1* guest_memory,
                                  std::uint32_t request,
                                  std::uint32_t handle) {
        ios_close_request(memory, guest_memory, request, handle);
        const bool closed = memory.read_u32(request + 4u) == 0u;
        acknowledge_ios_reply(guest_memory);
        return closed;
    };

    bool passed = true;
    std::error_code cleanup_error;

    // Known save files have a fixed persisted ACL. Rejecting a malformed
    // create before publication prevents an empty file with plausible ADS
    // from becoming a path-only provisional exception.
    const std::filesystem::path delete_root =
        shared_test_root / L"provisional_create_delete";
    std::filesystem::remove_all(delete_root, cleanup_error);
    {
        ScopedWideEnv set_isolated_nand_root(L"GALAXY_NAND_ROOT", delete_root);
        {
            auto memory_owner =
                std::make_unique<galaxy::host::GuestAddressSpace>();
            galaxy::host::GuestAddressSpace& memory = *memory_owner;
            galaxy::GuestMemoryV1* const guest_memory = memory.guest_memory();
            const std::filesystem::path canonical =
                canonical_path_for(delete_root);
            passed &= expect(
                galaxy::host::NativeNandTestAccess::
                        ensure_rmge01_data_directory(memory) == 0u,
                "provisional regression provisions the canonical save parent chain");
            const std::uint32_t fs_handle = ios_open_path(
                memory, guest_memory, kRequest, kFsPath, "/dev/fs");
            passed &= expect(
                fs_handle > 0u && fs_handle < 0x80000000u,
                "provisional regression opens /dev/fs");

            constexpr std::array<std::array<std::uint8_t, 4>, 4>
                kInvalidMetadata{{
                    {2u, 3u, 0u, 0u},
                    {3u, 2u, 0u, 0u},
                    {3u, 3u, 1u, 0u},
                    {3u, 3u, 0u, 1u},
                }};
            bool every_invalid_create_rejected = true;
            for (const auto& metadata : kInvalidMetadata) {
                write_isfs_attribute_block(
                    memory,
                    kAttr,
                    kGameDataPath,
                    metadata[0],
                    metadata[1],
                    metadata[2],
                    metadata[3]);
                ios_ioctl_request(
                    memory,
                    guest_memory,
                    kRequest + 0x40u,
                    fs_handle,
                    9u,
                    kAttr,
                    0x4Cu,
                    0u,
                    0u);
                every_invalid_create_rejected &=
                    memory.read_u32(kRequest + 0x44u) == kIsfsErrInvalid &&
                    !std::filesystem::exists(canonical);
                acknowledge_ios_reply(guest_memory);
            }
            passed &= expect(
                every_invalid_create_rejected,
                "GameData CreateFile rejects every wrong fixed mode or attribute without publication");

            write_isfs_attribute_block(
                memory, kAttr, kGameDataPath, 3u, 3u, 0u, 0u);
            ios_ioctl_request(
                memory,
                guest_memory,
                kRequest + 0x40u,
                fs_handle,
                9u,
                kAttr,
                0x4Cu,
                0u,
                0u);
            passed &= expect(
                memory.read_u32(kRequest + 0x44u) == 0u &&
                    std::filesystem::is_regular_file(canonical) &&
                    std::filesystem::file_size(canonical) == 0u,
                "exact GameData metadata publishes a same-instance provisional empty file");
            acknowledge_ios_reply(guest_memory);

            ios_ioctl_request(
                memory,
                guest_memory,
                kRequest + 0x80u,
                fs_handle,
                7u,
                kAttr + 6u,
                64u,
                0u,
                0u);
            passed &= expect(
                memory.read_u32(kRequest + 0x84u) == 0u &&
                    !std::filesystem::exists(canonical),
                "the creating GuestAddressSpace can transactionally delete an incomplete provisional GameData");
            acknowledge_ios_reply(guest_memory);
            passed &= expect(
                close_handle(
                    memory,
                    guest_memory,
                    kRequest + 0xC0u,
                    fs_handle),
                "provisional create/delete /dev/fs handle closes cleanly");
        }

        auto cold_owner =
            std::make_unique<galaxy::host::GuestAddressSpace>();
        galaxy::host::GuestAddressSpace& cold = *cold_owner;
        galaxy::GuestMemoryV1* const cold_guest_memory = cold.guest_memory();
        const std::uint32_t missing = ios_open_path(
            cold,
            cold_guest_memory,
            kRequest,
            kPath,
            kGameDataPath,
            1u);
        passed &= expect(
            missing == kIsfsErrNoExist,
            "cold validation accepts the tree after same-instance provisional deletion");
    }
    std::filesystem::remove_all(delete_root, cleanup_error);

    // CreateFile publishes durable recovery intent before exposing a known
    // file with incomplete contents.  A cold instance must consume that
    // exact marker/object pair as an interrupted creation, while leaving
    // unrelated, fully committed NAND objects untouched.
    const std::filesystem::path cold_root =
        shared_test_root / L"provisional_cold_empty";
    std::filesystem::remove_all(cold_root, cleanup_error);
    const std::filesystem::path cold_canonical =
        canonical_path_for(cold_root);
    const std::filesystem::path cold_provisional_marker(
        cold_canonical.native() + L":galaxy.isfs.provisional");
    constexpr std::string_view kUnrelatedPath =
        "/title/00010000/524d4745/data/keep.bin";
    const std::filesystem::path cold_unrelated =
        cold_root / L"title" / L"00010000" / L"524d4745" / L"data" /
        L"keep.bin";
    constexpr std::array<std::byte, 4> kUnrelatedBytes{
        std::byte{0x4B},
        std::byte{0x45},
        std::byte{0x45},
        std::byte{0x50},
    };
    {
        ScopedWideEnv set_isolated_nand_root(L"GALAXY_NAND_ROOT", cold_root);
        {
            auto creator_owner =
                std::make_unique<galaxy::host::GuestAddressSpace>();
            galaxy::host::GuestAddressSpace& creator = *creator_owner;
            galaxy::GuestMemoryV1* const guest_memory = creator.guest_memory();
            passed &= expect(
                galaxy::host::NativeNandTestAccess::
                        ensure_rmge01_data_directory(creator) == 0u,
                "cold-empty regression provisions the canonical save parent chain");
            const std::uint32_t fs_handle = ios_open_path(
                creator, guest_memory, kRequest, kFsPath, "/dev/fs");
            write_isfs_attribute_block(
                creator, kAttr, kGameDataPath, 3u, 3u, 0u, 0u);
            ios_ioctl_request(
                creator,
                guest_memory,
                kRequest + 0x40u,
                fs_handle,
                9u,
                kAttr,
                0x4Cu,
                0u,
                0u);
            passed &= expect(
                creator.read_u32(kRequest + 0x44u) == 0u &&
                    std::filesystem::is_regular_file(cold_canonical) &&
                    std::filesystem::file_size(cold_canonical) == 0u,
                "cold-empty fixture publishes an exact provisional GameData");
            acknowledge_ios_reply(guest_memory);
            const std::vector<std::byte> provisional_marker =
                galaxy::host::read_binary_file(cold_provisional_marker);
            passed &= expect(
                provisional_marker.size() == 24u &&
                    provisional_marker[0] == std::byte{'G'} &&
                    provisional_marker[1] == std::byte{'I'} &&
                    provisional_marker[2] == std::byte{'P'} &&
                    provisional_marker[3] == std::byte{'1'},
                "provisional GameData publication carries the durable GIP1 marker");

            write_isfs_attribute_block(
                creator, kAttr, kUnrelatedPath, 3u, 3u, 3u, 0u);
            ios_ioctl_request(
                creator,
                guest_memory,
                kRequest + 0x80u,
                fs_handle,
                9u,
                kAttr,
                0x4Cu,
                0u,
                0u);
            passed &= expect(
                creator.read_u32(kRequest + 0x84u) == 0u,
                "cold-empty fixture publishes an unrelated persistent NAND file");
            acknowledge_ios_reply(guest_memory);
            const std::uint32_t unrelated_handle = ios_open_path(
                creator,
                guest_memory,
                kRequest + 0xC0u,
                kPath,
                kUnrelatedPath,
                3u);
            passed &= expect(
                unrelated_handle > 0u && unrelated_handle < 0x80000000u,
                "cold-empty fixture opens its unrelated persistent NAND file");
            creator.copy(kBuffer, kUnrelatedBytes);
            ios_write_request(
                creator,
                guest_memory,
                kRequest + 0x100u,
                unrelated_handle,
                kBuffer,
                static_cast<std::uint32_t>(kUnrelatedBytes.size()));
            passed &= expect(
                creator.read_u32(kRequest + 0x104u) ==
                    kUnrelatedBytes.size(),
                "cold-empty fixture durably writes the unrelated persistent NAND file");
            acknowledge_ios_reply(guest_memory);
            passed &= expect(
                close_handle(
                    creator,
                    guest_memory,
                    kRequest + 0x140u,
                    unrelated_handle),
                "cold-empty fixture closes its unrelated persistent NAND file");
            passed &= expect(
                close_handle(
                    creator,
                    guest_memory,
                    kRequest + 0x180u,
                    fs_handle),
                "cold-empty fixture closes its /dev/fs handle without promotion");
        }

        auto cold_owner =
            std::make_unique<galaxy::host::GuestAddressSpace>();
        galaxy::host::GuestAddressSpace& cold = *cold_owner;
        galaxy::GuestMemoryV1* const cold_guest_memory = cold.guest_memory();
        const std::uint32_t recovered_missing = ios_open_path(
            cold,
            cold_guest_memory,
            kRequest,
            kPath,
            kGameDataPath,
            1u);
        passed &= expect(
            recovered_missing == kIsfsErrNoExist &&
                !std::filesystem::exists(cold_canonical) &&
                !host_file_stream_exists_for_test(cold_provisional_marker),
            "cold recovery consumes the exact unfinished zero-byte GameData and returns ENOENT");
        passed &= expect(
            std::filesystem::is_regular_file(cold_unrelated) &&
                galaxy::host::read_binary_file(cold_unrelated) ==
                    std::vector<std::byte>(
                        kUnrelatedBytes.begin(), kUnrelatedBytes.end()),
            "cold provisional recovery preserves unrelated persistent NAND bytes");
        const std::uint32_t unrelated_handle = ios_open_path(
            cold,
            cold_guest_memory,
            kRequest + 0x40u,
            kPath,
            kUnrelatedPath,
            1u);
        passed &= expect(
            unrelated_handle > 0u && unrelated_handle < 0x80000000u,
            "cold provisional recovery leaves the unrelated persistent NAND file openable");
        if (unrelated_handle > 0u && unrelated_handle < 0x80000000u) {
            passed &= expect(
                close_handle(
                    cold,
                    cold_guest_memory,
                    kRequest + 0x80u,
                    unrelated_handle),
                "cold provisional recovery leaves the unrelated persistent NAND handle usable");
        }
    }
    std::filesystem::remove_all(cold_root, cleanup_error);

    // Match the data and GIS3 stream exactly but swap the NTFS file object.
    // A provisional record must be bound to the object it published, not just
    // to the guest pathname.
    const std::filesystem::path identity_root =
        shared_test_root / L"provisional_identity_swap";
    std::filesystem::remove_all(identity_root, cleanup_error);
    {
        ScopedWideEnv set_isolated_nand_root(
            L"GALAXY_NAND_ROOT", identity_root);
        auto memory_owner =
            std::make_unique<galaxy::host::GuestAddressSpace>();
        galaxy::host::GuestAddressSpace& memory = *memory_owner;
        galaxy::GuestMemoryV1* const guest_memory = memory.guest_memory();
        const std::filesystem::path canonical =
            canonical_path_for(identity_root);
        passed &= expect(
            galaxy::host::NativeNandTestAccess::
                    ensure_rmge01_data_directory(memory) == 0u,
            "identity-swap regression provisions the canonical save parent chain");
        const std::uint32_t fs_handle = ios_open_path(
            memory, guest_memory, kRequest, kFsPath, "/dev/fs");
        write_isfs_attribute_block(
            memory, kAttr, kGameDataPath, 3u, 3u, 0u, 0u);
        ios_ioctl_request(
            memory,
            guest_memory,
            kRequest + 0x40u,
            fs_handle,
            9u,
            kAttr,
            0x4Cu,
            0u,
            0u);
        passed &= expect(
            memory.read_u32(kRequest + 0x44u) == 0u,
            "identity-swap fixture publishes a provisional GameData");
        acknowledge_ios_reply(guest_memory);

        const std::array<std::uint64_t, 2> original_identity =
            host_file_identity_for_test(canonical);
        const std::filesystem::path original_metadata(
            canonical.native() + L":galaxy.isfs.meta");
        const std::filesystem::path replacement =
            canonical.parent_path() / L"GameData.bin.identity-replacement";
        const std::filesystem::path replacement_metadata(
            replacement.native() + L":galaxy.isfs.meta");
        write_binary_file(replacement, std::span<const std::byte>{});
        write_binary_file(
            replacement_metadata,
            galaxy::host::read_binary_file(original_metadata));
        if (MoveFileExW(
                replacement.c_str(),
                canonical.c_str(),
                MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) == FALSE) {
            throw std::runtime_error(
                "failed to replace provisional GameData identity fixture");
        }
        const std::array<std::uint64_t, 2> replacement_identity =
            host_file_identity_for_test(canonical);
        passed &= expect(
            replacement_identity != original_identity,
            "host fixture replaces GameData with a distinct file object carrying identical bytes and GIS3 metadata");

        const std::uint32_t rejected = ios_open_path(
            memory,
            guest_memory,
            kRequest + 0x80u,
            kPath,
            kGameDataPath,
            3u);
        passed &= expect(
            rejected == kIsfsErrAccess,
            "a path-matching replacement object is not accepted under provisional authority");
        passed &= expect(
            close_handle(
                memory,
                guest_memory,
                kRequest + 0xC0u,
                fs_handle),
            "identity-swap /dev/fs handle closes after rejection");
    }
    std::filesystem::remove_all(identity_root, cleanup_error);

    return passed;
}

bool native_nand_modern_startup_recovery_edges_work(
    const std::filesystem::path& shared_test_root) {
    constexpr std::uint32_t kPath = 0x13560000u;
    constexpr std::uint32_t kRequest = 0x13560100u;
    constexpr std::array<std::wstring_view, 3> kStreamSuffixes{
        L":galaxy.isfs.meta",
        L":galaxy.isfs.meta.pending",
        L":galaxy.isfs.provisional",
    };
    struct ModernTreeSnapshot {
        std::vector<std::filesystem::path> paths;
        std::vector<std::array<std::uint64_t, 2>> identities;
        std::vector<bool> directories;
        std::vector<std::vector<std::byte>> bytes;
        std::vector<
            std::array<std::optional<std::vector<std::byte>>, 3>> streams;
    };

    const auto stream_path = [](
                                 const std::filesystem::path& path,
                                 std::wstring_view suffix) {
        std::wstring native = path.native();
        native.append(suffix);
        return std::filesystem::path(std::move(native));
    };
    const auto read_optional_stream = [&](
                                          const std::filesystem::path& path,
                                          std::wstring_view suffix) {
        const std::filesystem::path stream = stream_path(path, suffix);
        return host_file_stream_exists_for_test(stream)
            ? std::optional<std::vector<std::byte>>(
                  galaxy::host::read_binary_file(stream))
            : std::optional<std::vector<std::byte>>{};
    };
    const auto tree_paths = [](const std::filesystem::path& root) {
        std::vector<std::filesystem::path> paths{root};
        std::error_code enumeration_error;
        std::filesystem::recursive_directory_iterator iterator(
            root, enumeration_error);
        const std::filesystem::recursive_directory_iterator end;
        while (!enumeration_error && iterator != end) {
            paths.push_back(iterator->path());
            iterator.increment(enumeration_error);
        }
        if (enumeration_error) {
            throw std::runtime_error(
                "failed to enumerate modern NAND recovery fixture");
        }
        std::ranges::sort(paths);
        return paths;
    };
    const auto capture_tree = [&](const std::filesystem::path& root) {
        ModernTreeSnapshot snapshot;
        snapshot.paths = tree_paths(root);
        snapshot.identities.reserve(snapshot.paths.size());
        snapshot.directories.reserve(snapshot.paths.size());
        snapshot.bytes.reserve(snapshot.paths.size());
        snapshot.streams.reserve(snapshot.paths.size());
        for (const std::filesystem::path& path : snapshot.paths) {
            snapshot.identities.push_back(host_file_identity_for_test(path));
            const bool directory = std::filesystem::is_directory(path);
            snapshot.directories.push_back(directory);
            snapshot.bytes.push_back(directory
                ? std::vector<std::byte>{}
                : galaxy::host::read_binary_file(path));
            std::array<std::optional<std::vector<std::byte>>, 3> streams;
            for (std::size_t index = 0u;
                 index < kStreamSuffixes.size();
                 ++index) {
                streams[index] =
                    read_optional_stream(path, kStreamSuffixes[index]);
            }
            snapshot.streams.push_back(std::move(streams));
        }
        return snapshot;
    };
    const auto tree_matches = [&](const ModernTreeSnapshot& snapshot,
                                  const std::optional<std::filesystem::path>&
                                      excluded = std::nullopt) {
        std::vector<std::filesystem::path> expected_paths;
        expected_paths.reserve(snapshot.paths.size());
        for (const std::filesystem::path& path : snapshot.paths) {
            if (!excluded.has_value() || path != *excluded) {
                expected_paths.push_back(path);
            }
        }
        std::vector<std::filesystem::path> current_paths =
            tree_paths(snapshot.paths.front());
        if (excluded.has_value()) {
            std::erase(current_paths, *excluded);
        }
        if (current_paths != expected_paths) return false;
        for (std::size_t index = 0u; index < snapshot.paths.size(); ++index) {
            const std::filesystem::path& path = snapshot.paths[index];
            if (excluded.has_value() && path == *excluded) continue;
            if (!std::filesystem::exists(path) ||
                host_file_identity_for_test(path) !=
                    snapshot.identities[index] ||
                std::filesystem::is_directory(path) !=
                    snapshot.directories[index] ||
                (!snapshot.directories[index] &&
                 galaxy::host::read_binary_file(path) !=
                     snapshot.bytes[index])) {
                return false;
            }
            for (std::size_t stream_index = 0u;
                 stream_index < kStreamSuffixes.size();
                 ++stream_index) {
                if (read_optional_stream(
                        path, kStreamSuffixes[stream_index]) !=
                    snapshot.streams[index][stream_index]) {
                    return false;
                }
            }
        }
        return true;
    };
    const auto cold_open_sysconf = [&]() {
        auto cold = std::make_unique<galaxy::host::GuestAddressSpace>();
        galaxy::GuestMemoryV1* const guest_memory = cold->guest_memory();
        const std::uint32_t handle = ios_open_path(
            *cold,
            guest_memory,
            kRequest,
            kPath,
            "/shared2/sys/SYSCONF",
            1u);
        if (handle == 0u || handle >= 0x80000000u) return false;
        ios_close_request(
            *cold, guest_memory, kRequest + 0x40u, handle);
        const bool closed = cold->read_u32(kRequest + 0x44u) == 0u;
        acknowledge_ios_reply(guest_memory);
        return closed;
    };

    bool passed = true;
    std::error_code cleanup_error;
    const std::vector<std::byte> rmge_file_metadata =
        gis3_metadata_for_test(0x1001u, 0x3031u, 3u, 3u, 0u, 0u);
    const std::vector<std::byte> rmge_public_metadata =
        gis3_metadata_for_test(0x1001u, 0x3031u, 3u, 3u, 3u, 0u);
    const std::vector<std::byte> kernel_public_metadata =
        gis3_metadata_for_test(0u, 0u, 3u, 3u, 3u, 0u);

    const std::filesystem::path gip_root =
        shared_test_root / L"modern_torn_gip_candidates";
    std::filesystem::remove_all(gip_root, cleanup_error);
    {
        ScopedWideEnv set_gip_root(L"GALAXY_NAND_ROOT", gip_root);
        passed &= expect(
            cold_open_sysconf(),
            "torn-GIP recovery fixture creates an exact root-authorized modern NAND");
        const std::filesystem::path canonical =
            gip_root / L"title" / L"00010000" / L"524d4745" / L"data" /
            L"GameData.bin";
        const std::vector<std::byte> game_data =
            valid_rmge_game_data_for_test();
        write_binary_file(canonical, game_data);
        write_binary_file(
            stream_path(canonical, kStreamSuffixes[0]),
            rmge_file_metadata);
        const std::array<std::uint64_t, 2> root_identity =
            host_file_identity_for_test(gip_root);
        const std::array<std::uint64_t, 2> canonical_identity =
            host_file_identity_for_test(canonical);
        const std::filesystem::path zero_candidate =
            canonical.parent_path() /
            L"GameData.bin.__galaxy_isfs_candidate__.4242.1";
        const std::filesystem::path truncated_candidate =
            canonical.parent_path() /
            L"GameData.bin.__galaxy_isfs_candidate__.4242.2";
        constexpr std::array<std::byte, 7> kTruncatedGip{
            std::byte{'G'}, std::byte{'I'}, std::byte{'P'}, std::byte{'1'},
            std::byte{1}, std::byte{0}, std::byte{0}};
        for (const std::filesystem::path& candidate :
             {zero_candidate, truncated_candidate}) {
            write_binary_file(candidate, std::span<const std::byte>{});
            write_binary_file(
                stream_path(candidate, kStreamSuffixes[0]),
                rmge_file_metadata);
        }
        write_binary_file(
            stream_path(zero_candidate, kStreamSuffixes[2]),
            std::span<const std::byte>{});
        write_binary_file(
            stream_path(truncated_candidate, kStreamSuffixes[2]),
            kTruncatedGip);
        const std::array<std::uint64_t, 2> tmp_identity =
            host_file_identity_for_test(gip_root / L"tmp");
        passed &= expect(
            exact_gis3_metadata_for_test(
                zero_candidate, 0x1001u, 0x3031u, 3u, 3u, 0u, 0u) &&
                exact_gis3_metadata_for_test(
                    truncated_candidate,
                    0x1001u,
                    0x3031u,
                    3u,
                    3u,
                    0u,
                    0u),
            "torn-GIP candidates carry exact valid GIS3 before cold recovery");
        passed &= expect(
            cold_open_sysconf(),
            "root-authorized modern startup purges exact candidate residues with zero/truncated GIP");
        passed &= expect(
            !std::filesystem::exists(zero_candidate) &&
                !std::filesystem::exists(truncated_candidate) &&
                host_file_identity_for_test(gip_root) == root_identity &&
                host_file_identity_for_test(canonical) ==
                    canonical_identity &&
                galaxy::host::read_binary_file(canonical) == game_data &&
                galaxy::host::read_binary_file(
                    stream_path(canonical, kStreamSuffixes[0])) ==
                    rmge_file_metadata &&
                host_file_identity_for_test(gip_root / L"tmp") !=
                    tmp_identity &&
                std::filesystem::is_empty(gip_root / L"tmp"),
            "torn-GIP recovery removes only internal candidates, preserves canonical/root identity, and resets tmp");
    }
    std::filesystem::remove_all(gip_root, cleanup_error);

    const std::filesystem::path gir_candidate_root =
        shared_test_root / L"modern_torn_gir_atomic_candidates";
    std::filesystem::remove_all(gir_candidate_root, cleanup_error);
    {
        ScopedWideEnv set_gir_candidate_root(
            L"GALAXY_NAND_ROOT", gir_candidate_root);
        passed &= expect(
            cold_open_sysconf(),
            "torn-GIR recovery fixture creates an exact root-authorized modern NAND");
        const ModernTreeSnapshot before = capture_tree(gir_candidate_root);
        const std::filesystem::path zero_candidate =
            gir_candidate_root /
            L"galaxy-rename-journal.__galaxy_isfs_candidate__.5252.1";
        const std::filesystem::path truncated_candidate =
            gir_candidate_root /
            L"galaxy-rename-journal.__galaxy_isfs_candidate__.5252.2";
        constexpr std::array<std::byte, 7> kTruncatedGir{
            std::byte{'G'}, std::byte{'I'}, std::byte{'R'}, std::byte{'1'},
            std::byte{0}, std::byte{0}, std::byte{0}};
        write_binary_file(zero_candidate, std::span<const std::byte>{});
        write_binary_file(truncated_candidate, kTruncatedGir);
        passed &= expect(
            cold_open_sysconf(),
            "root-authorized startup purges zero/truncated atomic GIR journal candidates");
        passed &= expect(
            !std::filesystem::exists(zero_candidate) &&
                !std::filesystem::exists(truncated_candidate) &&
                tree_matches(before, gir_candidate_root / L"tmp") &&
                std::filesystem::is_empty(gir_candidate_root / L"tmp"),
            "torn atomic GIR candidate recovery preserves every persistent guest object/byte/ADS");
    }
    std::filesystem::remove_all(gir_candidate_root, cleanup_error);

    const std::filesystem::path rfl_parent_root =
        shared_test_root / L"modern_optional_rfl_parent_creators";
    std::filesystem::remove_all(rfl_parent_root, cleanup_error);
    {
        ScopedWideEnv set_rfl_parent_root(
            L"GALAXY_NAND_ROOT", rfl_parent_root);
        passed &= expect(
            cold_open_sysconf(),
            "optional-RFL-parent fixture creates an exact modern core without RFL");
        const std::filesystem::path menu =
            rfl_parent_root / L"shared2" / L"menu";
        const std::filesystem::path face_lib = menu / L"FaceLib";
        const std::filesystem::path rfl = face_lib / L"RFL_DB.dat";
        std::filesystem::create_directories(face_lib);
        write_binary_file(
            stream_path(menu, kStreamSuffixes[0]),
            rmge_public_metadata);
        write_binary_file(
            stream_path(face_lib, kStreamSuffixes[0]),
            rmge_public_metadata);
        const std::array<std::uint64_t, 2> menu_identity =
            host_file_identity_for_test(menu);
        const std::array<std::uint64_t, 2> face_lib_identity =
            host_file_identity_for_test(face_lib);
        passed &= expect(
            cold_open_sysconf(),
            "modern startup accepts matching optional menu/FaceLib creators while RFL is absent");
        passed &= expect(
            !std::filesystem::exists(rfl) &&
                host_file_identity_for_test(menu) == menu_identity &&
                host_file_identity_for_test(face_lib) == face_lib_identity,
            "matching optional RFL parents remain in place and do not synthesize RFL");
        const std::filesystem::path face_lib_metadata =
            stream_path(face_lib, kStreamSuffixes[0]);
        if (DeleteFileW(face_lib_metadata.c_str()) == FALSE) {
            throw std::runtime_error(
                "failed to replace FaceLib creator metadata fixture");
        }
        write_binary_file(face_lib_metadata, kernel_public_metadata);
        const ModernTreeSnapshot before_mismatch =
            capture_tree(rfl_parent_root);
        passed &= expect_runtime_error(
            [&] { static_cast<void>(cold_open_sysconf()); },
            "modern startup hard-fails mismatched optional menu/FaceLib creators without RFL");
        passed &= expect(
            tree_matches(before_mismatch) &&
                !std::filesystem::exists(rfl),
            "optional-parent mismatch rejection preserves the complete object/byte/ADS inventory and keeps RFL absent");
    }
    std::filesystem::remove_all(rfl_parent_root, cleanup_error);
    return passed;
}

bool native_nand_rename_failure_atomicity_works(
    const std::filesystem::path& shared_test_root) {
    constexpr std::string_view kCanonicalPath =
        "/title/00010000/524d4745/data/GameData.bin";
    constexpr std::string_view kGoodDirectory = "/tmp/rengood";
    constexpr std::string_view kGoodPath = "/tmp/rengood/GameData.bin";
    constexpr std::string_view kBadDirectory = "/tmp/renbad";
    constexpr std::string_view kBadPath = "/tmp/renbad/GameData.bin";
    constexpr std::uint32_t kPath = 0x13580000u;
    constexpr std::uint32_t kFsPath = 0x13580100u;
    constexpr std::uint32_t kAttr = 0x13580200u;
    constexpr std::uint32_t kRename = 0x13580300u;
    constexpr std::uint32_t kRequest = 0x13580400u;
    constexpr std::uint32_t kBuffer = 0x13600000u;
    constexpr std::uint32_t kIsfsErrInUse =
        static_cast<std::uint32_t>(-111);

    bool passed = true;
    std::error_code cleanup_error;
    const std::filesystem::path isolated_root =
        shared_test_root / L"rename_failure_atomicity";
    std::filesystem::remove_all(isolated_root, cleanup_error);
    ScopedWideEnv set_isolated_nand_root(L"GALAXY_NAND_ROOT", isolated_root);

    const std::filesystem::path canonical =
        isolated_root / L"title" / L"00010000" / L"524d4745" / L"data" /
        L"GameData.bin";
    const std::filesystem::path good_source =
        isolated_root / L"tmp" / L"rengood" / L"GameData.bin";
    const std::filesystem::path bad_source =
        isolated_root / L"tmp" / L"renbad" / L"GameData.bin";
    const std::vector<std::byte> valid_game_data =
        valid_rmge_game_data_for_test();
    constexpr std::array<std::byte, 4> kMalformedGameData{
        std::byte{0x42},
        std::byte{0x41},
        std::byte{0x44},
        std::byte{0x21},
    };

    galaxy::host::GuestAddressSpace memory;
    galaxy::GuestMemoryV1* const guest_memory = memory.guest_memory();
    passed &= expect(
        galaxy::host::NativeNandTestAccess::ensure_rmge01_data_directory(
            memory) == 0u,
        "Rename atomicity regression provisions the canonical save parent chain");
    const std::uint32_t fs_handle = ios_open_path(
        memory, guest_memory, kRequest, kFsPath, "/dev/fs");
    passed &= expect(
        fs_handle > 0u && fs_handle < 0x80000000u,
        "Rename atomicity regression opens /dev/fs");

    write_isfs_attribute_block(
        memory, kAttr, kCanonicalPath, 3u, 3u, 0u, 0u);
    ios_ioctl_request(
        memory,
        guest_memory,
        kRequest + 0x40u,
        fs_handle,
        9u,
        kAttr,
        0x4Cu,
        0u,
        0u);
    passed &= expect(
        memory.read_u32(kRequest + 0x44u) == 0u,
        "Rename atomicity fixture creates the exact canonical GameData");
    acknowledge_ios_reply(guest_memory);
    const std::uint32_t canonical_handle = ios_open_path(
        memory,
        guest_memory,
        kRequest + 0x80u,
        kPath,
        kCanonicalPath,
        3u);
    passed &= expect(
        canonical_handle > 0u && canonical_handle < 0x80000000u,
        "Rename atomicity fixture opens canonical GameData read/write");
    memory.copy(kBuffer, valid_game_data);
    ios_write_request(
        memory,
        guest_memory,
        kRequest + 0xC0u,
        canonical_handle,
        kBuffer,
        static_cast<std::uint32_t>(valid_game_data.size()));
    passed &= expect(
        memory.read_u32(kRequest + 0xC4u) == valid_game_data.size(),
        "Rename atomicity fixture durably promotes canonical GameData");
    acknowledge_ios_reply(guest_memory);
    ios_close_request(
        memory,
        guest_memory,
        kRequest + 0x100u,
        canonical_handle);
    passed &= expect(
        memory.read_u32(kRequest + 0x104u) == 0u,
        "Rename atomicity fixture closes canonical GameData");
    acknowledge_ios_reply(guest_memory);

    for (const std::string_view directory : {kGoodDirectory, kBadDirectory}) {
        write_isfs_attribute_block(
            memory, kAttr, directory, 3u, 3u, 3u, 0u);
        ios_ioctl_request(
            memory,
            guest_memory,
            kRequest + 0x140u,
            fs_handle,
            3u,
            kAttr,
            0x4Cu,
            0u,
            0u);
        passed &= expect(
            memory.read_u32(kRequest + 0x144u) == 0u,
            "Rename atomicity fixture creates an exact generic source directory");
        acknowledge_ios_reply(guest_memory);
    }

    write_isfs_attribute_block(
        memory, kAttr, kGoodPath, 3u, 3u, 0u, 0u);
    ios_ioctl_request(
        memory,
        guest_memory,
        kRequest + 0x180u,
        fs_handle,
        9u,
        kAttr,
        0x4Cu,
        0u,
        0u);
    passed &= expect(
        memory.read_u32(kRequest + 0x184u) == 0u,
        "Rename atomicity fixture creates a same-name valid source");
    acknowledge_ios_reply(guest_memory);
    const std::uint32_t good_handle = ios_open_path(
        memory,
        guest_memory,
        kRequest + 0x1C0u,
        kPath,
        kGoodPath,
        3u);
    passed &= expect(
        good_handle > 0u && good_handle < 0x80000000u,
        "Rename atomicity fixture opens its valid source read/write");
    memory.copy(kBuffer, valid_game_data);
    ios_write_request(
        memory,
        guest_memory,
        kRequest + 0x200u,
        good_handle,
        kBuffer,
        static_cast<std::uint32_t>(valid_game_data.size()));
    passed &= expect(
        memory.read_u32(kRequest + 0x204u) == valid_game_data.size(),
        "Rename atomicity fixture persists the complete valid source");
    acknowledge_ios_reply(guest_memory);
    ios_close_request(
        memory, guest_memory, kRequest + 0x240u, good_handle);
    passed &= expect(
        memory.read_u32(kRequest + 0x244u) == 0u,
        "Rename atomicity fixture closes the complete valid source");
    acknowledge_ios_reply(guest_memory);

    const std::array<std::uint64_t, 2> canonical_identity_before_lock =
        host_file_identity_for_test(canonical);
    const std::array<std::uint64_t, 2> good_identity_before_lock =
        host_file_identity_for_test(good_source);
    HANDLE locked_destination = CreateFileW(
        canonical.c_str(),
        GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        nullptr,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        nullptr);
    passed &= expect(
        locked_destination != INVALID_HANDLE_VALUE,
        "Rename atomicity fixture locks the canonical target against replacement");
    if (locked_destination != INVALID_HANDLE_VALUE) {
        write_isfs_path_buffer(memory, kRename, kGoodPath);
        write_isfs_path_buffer(memory, kRename + 64u, kCanonicalPath);
        ios_ioctl_request(
            memory,
            guest_memory,
            kRequest + 0x280u,
            fs_handle,
            8u,
            kRename,
            128u,
            0u,
            0u);
        passed &= expect(
            memory.read_u32(kRequest + 0x284u) == kIsfsErrInUse,
            "Rename to a host-locked canonical target reports ISFS in-use");
        acknowledge_ios_reply(guest_memory);
        passed &= expect(
            CloseHandle(locked_destination) != FALSE,
            "Rename atomicity fixture releases the canonical target lock");
        locked_destination = INVALID_HANDLE_VALUE;
    }
    passed &= expect(
        std::filesystem::is_regular_file(canonical) &&
            std::filesystem::is_regular_file(good_source) &&
            host_file_identity_for_test(canonical) ==
                canonical_identity_before_lock &&
            host_file_identity_for_test(good_source) ==
                good_identity_before_lock &&
            galaxy::host::read_binary_file(canonical) == valid_game_data &&
            galaxy::host::read_binary_file(good_source) == valid_game_data,
        "failed locked-target Rename preserves both exact file objects and bytes");
    passed &= expect(
        nand_rename_transaction_artifact_count_for_test(isolated_root) == 0u,
        "failed locked-target Rename leaves no hardlink journal residue");

    const std::array<std::uint64_t, 2> good_identity_before_success =
        host_file_identity_for_test(good_source);
    write_isfs_path_buffer(memory, kRename, kGoodPath);
    write_isfs_path_buffer(memory, kRename + 64u, kCanonicalPath);
    ios_ioctl_request(
        memory,
        guest_memory,
        kRequest + 0x2C0u,
        fs_handle,
        8u,
        kRename,
        128u,
        0u,
        0u);
    passed &= expect(
        memory.read_u32(kRequest + 0x2C4u) == 0u,
        "valid Rename atomically replaces the existing canonical target");
    acknowledge_ios_reply(guest_memory);
    const std::filesystem::path canonical_provisional(
        canonical.native() + L":galaxy.isfs.provisional");
    passed &= expect(
        !std::filesystem::exists(good_source) &&
            host_file_identity_for_test(canonical) ==
                good_identity_before_success &&
            galaxy::host::read_binary_file(canonical) == valid_game_data &&
            !host_file_stream_exists_for_test(canonical_provisional) &&
            host_file_link_count_for_test(canonical) == 1u &&
            nand_rename_transaction_artifact_count_for_test(isolated_root) ==
                0u,
        "successful Rename publishes the exact source identity without GIP1 or backup residue");

    write_isfs_attribute_block(
        memory, kAttr, kBadPath, 3u, 3u, 0u, 0u);
    ios_ioctl_request(
        memory,
        guest_memory,
        kRequest + 0x300u,
        fs_handle,
        9u,
        kAttr,
        0x4Cu,
        0u,
        0u);
    passed &= expect(
        memory.read_u32(kRequest + 0x304u) == 0u,
        "Rename target-policy fixture creates a same-name malformed source");
    acknowledge_ios_reply(guest_memory);
    const std::uint32_t bad_handle = ios_open_path(
        memory,
        guest_memory,
        kRequest + 0x340u,
        kPath,
        kBadPath,
        3u);
    passed &= expect(
        bad_handle > 0u && bad_handle < 0x80000000u,
        "Rename target-policy fixture opens its malformed generic source");
    memory.copy(kBuffer, kMalformedGameData);
    ios_write_request(
        memory,
        guest_memory,
        kRequest + 0x380u,
        bad_handle,
        kBuffer,
        static_cast<std::uint32_t>(kMalformedGameData.size()));
    passed &= expect(
        memory.read_u32(kRequest + 0x384u) == kMalformedGameData.size(),
        "Rename target-policy fixture persists malformed generic bytes");
    acknowledge_ios_reply(guest_memory);
    ios_close_request(
        memory, guest_memory, kRequest + 0x3C0u, bad_handle);
    passed &= expect(
        memory.read_u32(kRequest + 0x3C4u) == 0u,
        "Rename target-policy fixture closes its malformed generic source");
    acknowledge_ios_reply(guest_memory);

    const std::array<std::uint64_t, 2> canonical_identity_before_policy =
        host_file_identity_for_test(canonical);
    const std::array<std::uint64_t, 2> bad_identity_before_policy =
        host_file_identity_for_test(bad_source);
    write_isfs_path_buffer(memory, kRename, kBadPath);
    write_isfs_path_buffer(memory, kRename + 64u, kCanonicalPath);
    passed &= expect_runtime_error(
        [&] {
            ios_ioctl_request(
                memory,
                guest_memory,
                kRequest + 0x400u,
                fs_handle,
                8u,
                kRename,
                128u,
                0u,
                0u);
        },
        "Rename hard-fails malformed content before replacing a known target");
    passed &= expect(
        std::filesystem::is_regular_file(canonical) &&
            std::filesystem::is_regular_file(bad_source) &&
            host_file_identity_for_test(canonical) ==
                canonical_identity_before_policy &&
            host_file_identity_for_test(bad_source) ==
                bad_identity_before_policy &&
            galaxy::host::read_binary_file(canonical) == valid_game_data &&
            galaxy::host::read_binary_file(bad_source) ==
                std::vector<std::byte>(
                    kMalformedGameData.begin(), kMalformedGameData.end()),
        "target-policy rejection preserves both source and canonical target before mutation");

    if (locked_destination != INVALID_HANDLE_VALUE) {
        static_cast<void>(CloseHandle(locked_destination));
    }
    std::filesystem::remove_all(isolated_root, cleanup_error);
    return passed;
}

bool native_nand_rename_journal_cold_recovery_works(
    const std::filesystem::path& shared_test_root) {
    constexpr std::string_view kTargetPath =
        "/title/00010000/524d4745/data/journal.bin";
    constexpr std::string_view kSourceDirectory =
        "/title/00010000/524d4745/data/source";
    constexpr std::string_view kSourcePath =
        "/title/00010000/524d4745/data/source/journal.bin";
    constexpr std::uint32_t kJournalProcessId = 123u;
    constexpr std::uint64_t kJournalSequence = 1u;
    constexpr std::wstring_view kBackupSuffix =
        L".__galaxy_isfs_rename_backup__.123.1";
    constexpr std::wstring_view kJournalSuffix =
        L".__galaxy_isfs_rename_journal__.123.1";
    constexpr std::uint32_t kPath = 0x13640000u;
    constexpr std::uint32_t kFsPath = 0x13640100u;
    constexpr std::uint32_t kAttr = 0x13640200u;
    constexpr std::uint32_t kRequest = 0x13640400u;
    constexpr std::uint32_t kBuffer = 0x13700000u;
    constexpr std::array<std::byte, 4> kOldBytes{
        std::byte{0x4F},
        std::byte{0x4C},
        std::byte{0x44},
        std::byte{0x21},
    };
    constexpr std::array<std::byte, 4> kNewBytes{
        std::byte{0x4E},
        std::byte{0x45},
        std::byte{0x57},
        std::byte{0x21},
    };

    const auto target_path_for = [](const std::filesystem::path& root) {
        return root / L"title" / L"00010000" / L"524d4745" / L"data" /
            L"journal.bin";
    };
    const auto source_path_for = [](const std::filesystem::path& root) {
        return root / L"title" / L"00010000" / L"524d4745" / L"data" /
            L"source" / L"journal.bin";
    };
    const auto backup_path_for = [&](const std::filesystem::path& root) {
        const std::filesystem::path target = target_path_for(root);
        std::wstring backup_name = target.native();
        backup_name.append(kBackupSuffix.data(), kBackupSuffix.size());
        return std::filesystem::path(std::move(backup_name));
    };
    const auto journal_path_for = [&](const std::filesystem::path& root) {
        const std::filesystem::path target = target_path_for(root);
        std::wstring journal_name = target.native();
        journal_name.append(kJournalSuffix.data(), kJournalSuffix.size());
        return std::filesystem::path(std::move(journal_name));
    };
    const auto publish_file = [&](galaxy::host::GuestAddressSpace& memory,
                                  galaxy::GuestMemoryV1* guest_memory,
                                  std::uint32_t fs_handle,
                                  std::string_view path,
                                  std::span<const std::byte> bytes,
                                  std::uint8_t other_mode = 3u) {
        write_isfs_attribute_block(
            memory, kAttr, path, 3u, 3u, other_mode, 0u);
        ios_ioctl_request(
            memory,
            guest_memory,
            kRequest + 0x40u,
            fs_handle,
            9u,
            kAttr,
            0x4Cu,
            0u,
            0u);
        const bool created = memory.read_u32(kRequest + 0x44u) == 0u;
        acknowledge_ios_reply(guest_memory);
        if (!created) return false;
        const std::uint32_t handle = ios_open_path(
            memory,
            guest_memory,
            kRequest + 0x80u,
            kPath,
            path,
            3u);
        if (handle == 0u || handle >= 0x80000000u) return false;
        memory.copy(kBuffer, bytes);
        ios_write_request(
            memory,
            guest_memory,
            kRequest + 0xC0u,
            handle,
            kBuffer,
            static_cast<std::uint32_t>(bytes.size()));
        const bool written =
            memory.read_u32(kRequest + 0xC4u) ==
            static_cast<std::uint32_t>(bytes.size());
        acknowledge_ios_reply(guest_memory);
        ios_close_request(
            memory, guest_memory, kRequest + 0x100u, handle);
        const bool closed = memory.read_u32(kRequest + 0x104u) == 0u;
        acknowledge_ios_reply(guest_memory);
        return written && closed;
    };
    const auto create_fixture = [&](const std::filesystem::path& root,
                                     bool create_source) {
        auto creator_owner =
            std::make_unique<galaxy::host::GuestAddressSpace>();
        galaxy::host::GuestAddressSpace& creator = *creator_owner;
        galaxy::GuestMemoryV1* const guest_memory = creator.guest_memory();
        if (galaxy::host::NativeNandTestAccess::
                ensure_rmge01_data_directory(creator) != 0u) {
            return false;
        }
        const std::uint32_t fs_handle = ios_open_path(
            creator, guest_memory, kRequest, kFsPath, "/dev/fs");
        if (fs_handle == 0u || fs_handle >= 0x80000000u) return false;
        write_isfs_attribute_block(
            creator, kAttr, kSourceDirectory, 3u, 3u, 3u, 0u);
        ios_ioctl_request(
            creator,
            guest_memory,
            kRequest + 0x180u,
            fs_handle,
            3u,
            kAttr,
            0x4Cu,
            0u,
            0u);
        const bool source_directory_created =
            creator.read_u32(kRequest + 0x184u) == 0u;
        acknowledge_ios_reply(guest_memory);
        if (!source_directory_created ||
            !publish_file(
                creator, guest_memory, fs_handle, kTargetPath, kOldBytes) ||
            (create_source &&
             !publish_file(
                 creator,
                 guest_memory,
                 fs_handle,
                 kSourcePath,
                 kNewBytes))) {
            return false;
        }
        ios_close_request(
            creator, guest_memory, kRequest + 0x140u, fs_handle);
        const bool closed = creator.read_u32(kRequest + 0x144u) == 0u;
        acknowledge_ios_reply(guest_memory);
        return closed && std::filesystem::is_regular_file(target_path_for(root));
    };
    const auto cold_open_guest_path = [&](std::string_view guest_path) {
        auto cold_owner = std::make_unique<galaxy::host::GuestAddressSpace>();
        galaxy::host::GuestAddressSpace& cold = *cold_owner;
        galaxy::GuestMemoryV1* const guest_memory = cold.guest_memory();
        const std::uint32_t handle = ios_open_path(
            cold,
            guest_memory,
            kRequest,
            kPath,
            guest_path,
            1u);
        const bool opened = handle > 0u && handle < 0x80000000u;
        if (opened) {
            ios_close_request(
                cold, guest_memory, kRequest + 0x40u, handle);
            const bool closed = cold.read_u32(kRequest + 0x44u) == 0u;
            acknowledge_ios_reply(guest_memory);
            return closed;
        }
        return false;
    };
    const auto cold_open_target = [&](const std::filesystem::path&) {
        return cold_open_guest_path(kTargetPath);
    };

    bool passed = true;
    std::error_code cleanup_error;

    const std::filesystem::path precommit_root =
        shared_test_root / L"rename_recovery_precommit";
    std::filesystem::remove_all(precommit_root, cleanup_error);
    {
        ScopedWideEnv set_isolated_nand_root(
            L"GALAXY_NAND_ROOT", precommit_root);
        passed &= expect(
            create_fixture(precommit_root, true),
            "precommit Rename recovery fixture creates committed source and target objects");
        const std::filesystem::path target = target_path_for(precommit_root);
        const std::filesystem::path source = source_path_for(precommit_root);
        const std::filesystem::path backup = backup_path_for(precommit_root);
        const std::filesystem::path journal = journal_path_for(precommit_root);
        const std::array<std::uint64_t, 2> old_identity =
            host_file_identity_for_test(target);
        const std::array<std::uint64_t, 2> source_identity =
            host_file_identity_for_test(source);
        const std::vector<std::byte> journal_bytes =
            gir1_rename_journal_for_test(
                kSourcePath,
                kTargetPath,
                kJournalProcessId,
                kJournalSequence,
                source_identity,
                old_identity);
        if (CreateHardLinkW(backup.c_str(), target.c_str(), nullptr) == FALSE) {
            throw std::runtime_error(
                "failed to construct precommit Rename backup fixture");
        }
        write_binary_file(journal, journal_bytes);
        passed &= expect(
            host_file_identity_for_test(source) == source_identity &&
                host_file_link_count_for_test(source) == 1u &&
                host_file_identity_for_test(backup) == old_identity &&
                host_file_link_count_for_test(target) == 2u &&
                host_file_link_count_for_test(backup) == 2u &&
                host_file_link_count_for_test(journal) == 1u &&
                galaxy::host::read_binary_file(journal) == journal_bytes,
            "precommit GIR1 fixture binds the source and two-link old target to paired journal names");
        passed &= expect(
            cold_open_target(precommit_root),
            "cold startup accepts and recovers a precommit Rename journal");
        passed &= expect(
            !std::filesystem::exists(backup) &&
                !std::filesystem::exists(journal) &&
                host_file_identity_for_test(target) == old_identity &&
                host_file_link_count_for_test(target) == 1u &&
                galaxy::host::read_binary_file(target) ==
                    std::vector<std::byte>(kOldBytes.begin(), kOldBytes.end()) &&
                nand_rename_transaction_artifact_count_for_test(
                    precommit_root) == 0u,
            "precommit recovery removes only the second name and preserves the old target identity");
    }
    std::filesystem::remove_all(precommit_root, cleanup_error);

    const std::filesystem::path postcommit_root =
        shared_test_root / L"rename_recovery_postcommit";
    std::filesystem::remove_all(postcommit_root, cleanup_error);
    {
        ScopedWideEnv set_isolated_nand_root(
            L"GALAXY_NAND_ROOT", postcommit_root);
        passed &= expect(
            create_fixture(postcommit_root, true),
            "postcommit Rename recovery fixture creates old target and new source");
        const std::filesystem::path target = target_path_for(postcommit_root);
        const std::filesystem::path source = source_path_for(postcommit_root);
        const std::filesystem::path backup = backup_path_for(postcommit_root);
        const std::filesystem::path journal = journal_path_for(postcommit_root);
        const std::array<std::uint64_t, 2> old_identity =
            host_file_identity_for_test(target);
        const std::array<std::uint64_t, 2> new_identity =
            host_file_identity_for_test(source);
        const std::vector<std::byte> journal_bytes =
            gir1_rename_journal_for_test(
                kSourcePath,
                kTargetPath,
                kJournalProcessId,
                kJournalSequence,
                new_identity,
                old_identity);
        if (CreateHardLinkW(backup.c_str(), target.c_str(), nullptr) == FALSE ||
            MoveFileExW(
                source.c_str(),
                target.c_str(),
                MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) == FALSE) {
            throw std::runtime_error(
                "failed to construct postcommit Rename recovery fixture");
        }
        write_binary_file(journal, journal_bytes);
        passed &= expect(
            !std::filesystem::exists(source) &&
            host_file_identity_for_test(backup) == old_identity &&
                host_file_identity_for_test(target) == new_identity &&
                old_identity != new_identity &&
                host_file_link_count_for_test(backup) == 1u &&
                host_file_link_count_for_test(target) == 1u &&
                host_file_link_count_for_test(journal) == 1u &&
                galaxy::host::read_binary_file(journal) == journal_bytes,
            "postcommit GIR1 fixture has distinct one-link backup and committed destination objects");
        passed &= expect(
            cold_open_target(postcommit_root),
            "cold startup accepts and recovers a postcommit Rename journal");
        passed &= expect(
            !std::filesystem::exists(backup) &&
                !std::filesystem::exists(journal) &&
                host_file_identity_for_test(target) == new_identity &&
                host_file_link_count_for_test(target) == 1u &&
                galaxy::host::read_binary_file(target) ==
                    std::vector<std::byte>(kNewBytes.begin(), kNewBytes.end()) &&
                nand_rename_transaction_artifact_count_for_test(
                    postcommit_root) == 0u,
            "postcommit recovery purges only the old backup and preserves the committed destination");
    }
    std::filesystem::remove_all(postcommit_root, cleanup_error);

    const std::filesystem::path journal_only_precommit_root =
        shared_test_root / L"rename_recovery_journal_only_precommit";
    std::filesystem::remove_all(journal_only_precommit_root, cleanup_error);
    {
        ScopedWideEnv set_isolated_nand_root(
            L"GALAXY_NAND_ROOT", journal_only_precommit_root);
        passed &= expect(
            create_fixture(journal_only_precommit_root, true),
            "journal-only precommit fixture creates distinct source and old target objects");
        const std::filesystem::path target =
            target_path_for(journal_only_precommit_root);
        const std::filesystem::path source =
            source_path_for(journal_only_precommit_root);
        const std::filesystem::path journal =
            journal_path_for(journal_only_precommit_root);
        const std::array<std::uint64_t, 2> old_identity =
            host_file_identity_for_test(target);
        const std::array<std::uint64_t, 2> source_identity =
            host_file_identity_for_test(source);
        const std::vector<std::byte> journal_bytes =
            gir1_rename_journal_for_test(
                kSourcePath,
                kTargetPath,
                kJournalProcessId,
                kJournalSequence,
                source_identity,
                old_identity);
        write_binary_file(journal, journal_bytes);
        passed &= expect(
            host_file_link_count_for_test(source) == 1u &&
                host_file_link_count_for_test(target) == 1u &&
                host_file_link_count_for_test(journal) == 1u &&
                nand_rename_transaction_artifact_count_for_test(
                    journal_only_precommit_root) == 1u,
            "journal-only precommit fixture has two one-link live objects and no backup");
        passed &= expect(
            cold_open_target(journal_only_precommit_root),
            "cold startup accepts the journal-only precommit cleanup window");
        passed &= expect(
            !std::filesystem::exists(journal) &&
                host_file_identity_for_test(source) == source_identity &&
                host_file_identity_for_test(target) == old_identity &&
                host_file_link_count_for_test(source) == 1u &&
                host_file_link_count_for_test(target) == 1u &&
                galaxy::host::read_binary_file(source) ==
                    std::vector<std::byte>(kNewBytes.begin(), kNewBytes.end()) &&
                galaxy::host::read_binary_file(target) ==
                    std::vector<std::byte>(kOldBytes.begin(), kOldBytes.end()) &&
                nand_rename_transaction_artifact_count_for_test(
                    journal_only_precommit_root) == 0u,
            "journal-only precommit recovery removes only GIR1 and preserves both exact objects");
    }
    std::filesystem::remove_all(journal_only_precommit_root, cleanup_error);

    const std::filesystem::path journal_only_postcommit_root =
        shared_test_root / L"rename_recovery_journal_only_postcommit";
    std::filesystem::remove_all(journal_only_postcommit_root, cleanup_error);
    {
        ScopedWideEnv set_isolated_nand_root(
            L"GALAXY_NAND_ROOT", journal_only_postcommit_root);
        passed &= expect(
            create_fixture(journal_only_postcommit_root, true),
            "journal-only postcommit fixture creates distinct source and old target objects");
        const std::filesystem::path target =
            target_path_for(journal_only_postcommit_root);
        const std::filesystem::path source =
            source_path_for(journal_only_postcommit_root);
        const std::filesystem::path journal =
            journal_path_for(journal_only_postcommit_root);
        const std::array<std::uint64_t, 2> old_identity =
            host_file_identity_for_test(target);
        const std::array<std::uint64_t, 2> new_identity =
            host_file_identity_for_test(source);
        const std::vector<std::byte> journal_bytes =
            gir1_rename_journal_for_test(
                kSourcePath,
                kTargetPath,
                kJournalProcessId,
                kJournalSequence,
                new_identity,
                old_identity);
        if (MoveFileExW(
                source.c_str(),
                target.c_str(),
                MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) == FALSE) {
            throw std::runtime_error(
                "failed to construct journal-only postcommit fixture");
        }
        write_binary_file(journal, journal_bytes);
        passed &= expect(
            !std::filesystem::exists(source) &&
                host_file_identity_for_test(target) == new_identity &&
                host_file_link_count_for_test(target) == 1u &&
                host_file_link_count_for_test(journal) == 1u &&
                nand_rename_transaction_artifact_count_for_test(
                    journal_only_postcommit_root) == 1u,
            "journal-only postcommit fixture binds GIR1 to the committed source identity");
        passed &= expect(
            cold_open_target(journal_only_postcommit_root),
            "cold startup accepts the journal-only postcommit cleanup window");
        passed &= expect(
            !std::filesystem::exists(source) &&
                !std::filesystem::exists(journal) &&
                host_file_identity_for_test(target) == new_identity &&
                host_file_link_count_for_test(target) == 1u &&
                galaxy::host::read_binary_file(target) ==
                    std::vector<std::byte>(kNewBytes.begin(), kNewBytes.end()) &&
                nand_rename_transaction_artifact_count_for_test(
                    journal_only_postcommit_root) == 0u,
            "journal-only postcommit recovery removes only GIR1 and retains the committed object");
    }
    std::filesystem::remove_all(journal_only_postcommit_root, cleanup_error);

    const std::filesystem::path wrong_identity_root =
        shared_test_root / L"rename_recovery_wrong_source_identity";
    std::filesystem::remove_all(wrong_identity_root, cleanup_error);
    {
        ScopedWideEnv set_isolated_nand_root(
            L"GALAXY_NAND_ROOT", wrong_identity_root);
        passed &= expect(
            create_fixture(wrong_identity_root, true),
            "wrong-identity GIR1 fixture creates valid source and target objects");
        const std::filesystem::path target = target_path_for(wrong_identity_root);
        const std::filesystem::path source = source_path_for(wrong_identity_root);
        const std::filesystem::path journal =
            journal_path_for(wrong_identity_root);
        const std::array<std::uint64_t, 2> old_identity =
            host_file_identity_for_test(target);
        const std::array<std::uint64_t, 2> source_identity =
            host_file_identity_for_test(source);
        std::array<std::uint64_t, 2> wrong_source_identity = source_identity;
        wrong_source_identity[1] ^= 1u;
        if (wrong_source_identity == old_identity) {
            wrong_source_identity[1] ^= 2u;
        }
        const std::vector<std::byte> journal_bytes =
            gir1_rename_journal_for_test(
                kSourcePath,
                kTargetPath,
                kJournalProcessId,
                kJournalSequence,
                wrong_source_identity,
                old_identity);
        write_binary_file(journal, journal_bytes);
        passed &= expect_runtime_error(
            [&] {
                static_cast<void>(cold_open_target(wrong_identity_root));
            },
            "cold startup hard-fails a checksummed GIR1 bound to the wrong source identity");
        passed &= expect(
            host_file_identity_for_test(source) == source_identity &&
                host_file_identity_for_test(target) == old_identity &&
                host_file_link_count_for_test(source) == 1u &&
                host_file_link_count_for_test(target) == 1u &&
                host_file_link_count_for_test(journal) == 1u &&
                galaxy::host::read_binary_file(source) ==
                    std::vector<std::byte>(kNewBytes.begin(), kNewBytes.end()) &&
                galaxy::host::read_binary_file(target) ==
                    std::vector<std::byte>(kOldBytes.begin(), kOldBytes.end()) &&
                galaxy::host::read_binary_file(journal) == journal_bytes &&
                nand_rename_transaction_artifact_count_for_test(
                    wrong_identity_root) == 1u,
            "wrong-source GIR1 rejection performs no mutation and preserves forensic journal bytes");
    }
    std::filesystem::remove_all(wrong_identity_root, cleanup_error);

    const std::filesystem::path backup_only_root =
        shared_test_root / L"rename_recovery_backup_only";
    std::filesystem::remove_all(backup_only_root, cleanup_error);
    {
        ScopedWideEnv set_isolated_nand_root(
            L"GALAXY_NAND_ROOT", backup_only_root);
        passed &= expect(
            create_fixture(backup_only_root, true),
            "rollback Rename recovery fixture creates committed source and target objects");
        const std::filesystem::path target = target_path_for(backup_only_root);
        const std::filesystem::path source = source_path_for(backup_only_root);
        const std::filesystem::path backup = backup_path_for(backup_only_root);
        const std::filesystem::path journal = journal_path_for(backup_only_root);
        const std::array<std::uint64_t, 2> old_identity =
            host_file_identity_for_test(target);
        const std::array<std::uint64_t, 2> source_identity =
            host_file_identity_for_test(source);
        const std::vector<std::byte> journal_bytes =
            gir1_rename_journal_for_test(
                kSourcePath,
                kTargetPath,
                kJournalProcessId,
                kJournalSequence,
                source_identity,
                old_identity);
        if (CreateHardLinkW(backup.c_str(), target.c_str(), nullptr) == FALSE ||
            DeleteFileW(target.c_str()) == FALSE) {
            throw std::runtime_error(
                "failed to construct interrupted rollback Rename recovery fixture");
        }
        write_binary_file(journal, journal_bytes);
        passed &= expect(
            !std::filesystem::exists(target) &&
                host_file_identity_for_test(source) == source_identity &&
                host_file_link_count_for_test(source) == 1u &&
                host_file_identity_for_test(backup) == old_identity &&
                host_file_link_count_for_test(backup) == 1u &&
                host_file_link_count_for_test(journal) == 1u &&
                galaxy::host::read_binary_file(journal) == journal_bytes,
            "rollback GIR1 fixture binds the live source and backup-only old target");
        passed &= expect(
            cold_open_target(backup_only_root),
            "cold startup restores an interrupted GIR1 rollback");
        passed &= expect(
            !std::filesystem::exists(backup) &&
                !std::filesystem::exists(journal) &&
                host_file_identity_for_test(target) == old_identity &&
                host_file_link_count_for_test(target) == 1u &&
                galaxy::host::read_binary_file(target) ==
                    std::vector<std::byte>(kOldBytes.begin(), kOldBytes.end()) &&
                nand_rename_transaction_artifact_count_for_test(
                    backup_only_root) == 0u,
            "rollback recovery restores the exact old object to its guest name without GIR1 residue");
    }
    std::filesystem::remove_all(backup_only_root, cleanup_error);

    constexpr std::string_view kProvisionalTargetPath =
        "/title/00010000/524d4745/data/GameData.bin";
    constexpr std::string_view kProvisionalSourceDirectory =
        "/title/00010000/524d4745/data/prov";
    constexpr std::string_view kProvisionalSourcePath =
        "/title/00010000/524d4745/data/prov/GameData.bin";
    const std::filesystem::path provisional_root =
        shared_test_root / L"rename_recovery_provisional_postcommit";
    std::filesystem::remove_all(provisional_root, cleanup_error);
    {
        ScopedWideEnv set_isolated_nand_root(
            L"GALAXY_NAND_ROOT", provisional_root);
        auto creator_owner =
            std::make_unique<galaxy::host::GuestAddressSpace>();
        galaxy::host::GuestAddressSpace& creator = *creator_owner;
        galaxy::GuestMemoryV1* const guest_memory = creator.guest_memory();
        passed &= expect(
            galaxy::host::NativeNandTestAccess::
                    ensure_rmge01_data_directory(creator) == 0u,
            "provisional GIR1 fixture provisions the canonical save parent");
        const std::uint32_t fs_handle = ios_open_path(
            creator, guest_memory, kRequest, kFsPath, "/dev/fs");
        passed &= expect(
            fs_handle > 0u && fs_handle < 0x80000000u,
            "provisional GIR1 fixture opens /dev/fs");
        write_isfs_attribute_block(
            creator,
            kAttr,
            kProvisionalTargetPath,
            3u,
            3u,
            0u,
            0u);
        ios_ioctl_request(
            creator,
            guest_memory,
            kRequest + 0x200u,
            fs_handle,
            9u,
            kAttr,
            0x4Cu,
            0u,
            0u);
        passed &= expect(
            creator.read_u32(kRequest + 0x204u) == 0u,
            "provisional GIR1 fixture durably publishes an exact empty GameData target");
        acknowledge_ios_reply(guest_memory);
        write_isfs_attribute_block(
            creator,
            kAttr,
            kProvisionalSourceDirectory,
            3u,
            3u,
            3u,
            0u);
        ios_ioctl_request(
            creator,
            guest_memory,
            kRequest + 0x240u,
            fs_handle,
            3u,
            kAttr,
            0x4Cu,
            0u,
            0u);
        passed &= expect(
            creator.read_u32(kRequest + 0x244u) == 0u,
            "provisional GIR1 fixture creates a persistent source directory");
        acknowledge_ios_reply(guest_memory);
        const std::vector<std::byte> valid_game_data =
            valid_rmge_game_data_for_test();
        passed &= expect(
            publish_file(
                creator,
                guest_memory,
                fs_handle,
                kProvisionalSourcePath,
                valid_game_data,
                0u),
            "provisional GIR1 fixture publishes a complete target-compatible source");
        ios_close_request(
            creator, guest_memory, kRequest + 0x280u, fs_handle);
        passed &= expect(
            creator.read_u32(kRequest + 0x284u) == 0u,
            "provisional GIR1 fixture closes /dev/fs before crash-state construction");
        acknowledge_ios_reply(guest_memory);

        const std::filesystem::path target =
            provisional_root / L"title" / L"00010000" / L"524d4745" /
            L"data" / L"GameData.bin";
        const std::filesystem::path source =
            provisional_root / L"title" / L"00010000" / L"524d4745" /
            L"data" / L"prov" / L"GameData.bin";
        std::wstring backup_name = target.native();
        backup_name.append(kBackupSuffix.data(), kBackupSuffix.size());
        const std::filesystem::path backup(std::move(backup_name));
        std::wstring journal_name = target.native();
        journal_name.append(kJournalSuffix.data(), kJournalSuffix.size());
        const std::filesystem::path journal(std::move(journal_name));
        const std::filesystem::path target_marker(
            target.native() + L":galaxy.isfs.provisional");
        const std::array<std::uint64_t, 2> old_identity =
            host_file_identity_for_test(target);
        const std::array<std::uint64_t, 2> new_identity =
            host_file_identity_for_test(source);
        const std::vector<std::byte> provisional_marker =
            galaxy::host::read_binary_file(target_marker);
        const std::vector<std::byte> journal_bytes =
            gir1_rename_journal_for_test(
                kProvisionalSourcePath,
                kProvisionalTargetPath,
                kJournalProcessId,
                kJournalSequence,
                new_identity,
                old_identity,
                true);
        if (CreateHardLinkW(backup.c_str(), target.c_str(), nullptr) == FALSE ||
            MoveFileExW(
                source.c_str(),
                target.c_str(),
                MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) == FALSE) {
            throw std::runtime_error(
                "failed to construct provisional-old-target postcommit fixture");
        }
        write_binary_file(journal, journal_bytes);
        const std::filesystem::path backup_marker(
            backup.native() + L":galaxy.isfs.provisional");
        passed &= expect(
            !std::filesystem::exists(source) &&
                host_file_identity_for_test(target) == new_identity &&
                host_file_identity_for_test(backup) == old_identity &&
                host_file_link_count_for_test(target) == 1u &&
                host_file_link_count_for_test(backup) == 1u &&
                galaxy::host::read_binary_file(backup).empty() &&
                galaxy::host::read_binary_file(backup_marker) ==
                    provisional_marker &&
                !host_file_stream_exists_for_test(target_marker) &&
                galaxy::host::read_binary_file(journal) == journal_bytes,
            "GIR1 provisional flag binds the postcommit backup to the exact empty GIP1 object");
        creator_owner.reset();
        passed &= expect(
            cold_open_guest_path(kProvisionalTargetPath),
            "cold startup accepts a postcommit GIR1 whose old target is exact provisional GameData");
        passed &= expect(
            !std::filesystem::exists(source) &&
                !std::filesystem::exists(backup) &&
                !std::filesystem::exists(journal) &&
                host_file_identity_for_test(target) == new_identity &&
                host_file_link_count_for_test(target) == 1u &&
                galaxy::host::read_binary_file(target) == valid_game_data &&
                !host_file_stream_exists_for_test(target_marker) &&
                nand_rename_transaction_artifact_count_for_test(
                    provisional_root) == 0u,
            "provisional-old-target recovery purges GIP1 with the backup and preserves the committed save");
    }
    std::filesystem::remove_all(provisional_root, cleanup_error);
    return passed;
}

bool native_nand_extended_length_rename_recovery_works(
    const std::filesystem::path& shared_test_root) {
    constexpr std::string_view kDeepestGuestPath =
        "/aaaaaaaaaaaa/bbbbbbbbbbbb/cccccccccccc/ddddddddddd/eeeeeeeeeee";
    constexpr std::string_view kTargetPath =
        "/title/00010000/524d4745/data/journal.bin";
    constexpr std::string_view kSourceDirectory =
        "/title/00010000/524d4745/data/source";
    constexpr std::string_view kSourcePath =
        "/title/00010000/524d4745/data/source/journal.bin";
    static_assert(kDeepestGuestPath.size() == 63u);
    constexpr std::uint32_t kJournalProcessId = 123u;
    constexpr std::uint64_t kJournalSequence = 1u;
    constexpr std::wstring_view kJournalSuffix =
        L".__galaxy_isfs_rename_journal__.123.1";
    constexpr std::uint32_t kPath = 0x13640000u;
    constexpr std::uint32_t kFsPath = 0x13640100u;
    constexpr std::uint32_t kAttr = 0x13640200u;
    constexpr std::uint32_t kRename = 0x13640300u;
    constexpr std::uint32_t kRequest = 0x13640400u;
    constexpr std::uint32_t kBuffer = 0x13700000u;
    constexpr std::array<std::byte, 4> kOldBytes{
        std::byte{0x4F},
        std::byte{0x4C},
        std::byte{0x44},
        std::byte{0x21},
    };
    constexpr std::array<std::byte, 4> kNewBytes{
        std::byte{0x4E},
        std::byte{0x45},
        std::byte{0x57},
        std::byte{0x21},
    };

    std::error_code path_error;
    const std::filesystem::path anchor = std::filesystem::absolute(
        shared_test_root / L"extended_length_rename_recovery", path_error);
    if (path_error) {
        return expect(false, "long-path regression resolves its test anchor");
    }
    const std::filesystem::path extended_anchor =
        extended_length_path_for_test(anchor);
    std::error_code cleanup_error;
    std::filesystem::remove_all(extended_anchor, cleanup_error);
    cleanup_error.clear();

    constexpr std::size_t kDesiredRootLength = 204u;
    const std::size_t padding_length = anchor.native().size() + 1u <
            kDesiredRootLength
        ? kDesiredRootLength - anchor.native().size() - 1u
        : 8u;
    if (padding_length == 0u || padding_length > 240u) {
        return expect(false, "long-path regression has a valid padding component");
    }
    std::wstring padding(padding_length, L'p');
    const std::filesystem::path isolated_root = anchor / padding;

    const auto publish_file = [&](galaxy::host::GuestAddressSpace& memory,
                                  galaxy::GuestMemoryV1* guest_memory,
                                  std::uint32_t fs_handle,
                                  std::string_view path,
                                  std::span<const std::byte> bytes) {
        write_isfs_attribute_block(
            memory, kAttr, path, 3u, 3u, 3u, 0u);
        ios_ioctl_request(
            memory,
            guest_memory,
            kRequest + 0x40u,
            fs_handle,
            9u,
            kAttr,
            0x4Cu,
            0u,
            0u);
        const bool created = memory.read_u32(kRequest + 0x44u) == 0u;
        acknowledge_ios_reply(guest_memory);
        if (!created) return false;
        const std::uint32_t handle = ios_open_path(
            memory,
            guest_memory,
            kRequest + 0x80u,
            kPath,
            path,
            3u);
        if (handle == 0u || handle >= 0x80000000u) return false;
        memory.copy(kBuffer, bytes);
        ios_write_request(
            memory,
            guest_memory,
            kRequest + 0xC0u,
            handle,
            kBuffer,
            static_cast<std::uint32_t>(bytes.size()));
        const bool written = memory.read_u32(kRequest + 0xC4u) == bytes.size();
        acknowledge_ios_reply(guest_memory);
        ios_close_request(
            memory, guest_memory, kRequest + 0x100u, handle);
        const bool closed = memory.read_u32(kRequest + 0x104u) == 0u;
        acknowledge_ios_reply(guest_memory);
        return written && closed;
    };

    bool passed = true;
    std::filesystem::path runtime_root;
    std::filesystem::path target;
    std::filesystem::path source;
    std::filesystem::path journal;
    std::array<std::uint64_t, 2> source_identity{};
    std::array<std::uint64_t, 2> target_identity{};
    {
        ScopedWideEnv set_isolated_nand_root(
            L"GALAXY_NAND_ROOT", isolated_root);
        auto creator_owner =
            std::make_unique<galaxy::host::GuestAddressSpace>();
        galaxy::host::GuestAddressSpace& creator = *creator_owner;
        galaxy::GuestMemoryV1* const guest_memory = creator.guest_memory();
        passed &= expect(
            galaxy::host::NativeNandTestAccess::
                    ensure_rmge01_data_directory(creator) == 0u,
            "long-path regression provisions the canonical NAND layout");
        runtime_root = galaxy::host::NativeNandTestAccess::nand_root(creator);
        target = galaxy::host::NativeNandTestAccess::nand_host_path(
            creator, kTargetPath);
        source = galaxy::host::NativeNandTestAccess::nand_host_path(
            creator, kSourcePath);
        const std::filesystem::path deepest_host =
            galaxy::host::NativeNandTestAccess::nand_host_path(
                creator, kDeepestGuestPath);
        journal = std::filesystem::path(
            target.native() +
            std::wstring(kJournalSuffix.data(), kJournalSuffix.size()));
        const std::filesystem::path target_metadata(
            target.native() + L":galaxy.isfs.meta");
        const std::filesystem::path target_pending(
            target.native() + L":galaxy.isfs.meta.pending");
        const std::filesystem::path candidate_pending(
            target.native() +
            L".__galaxy_isfs_candidate__.1.1:galaxy.isfs.meta.pending");
        passed &= expect(
            runtime_root == extended_length_path_for_test(isolated_root) &&
                deepest_host.native().size() > MAX_PATH &&
                target.native().size() < MAX_PATH &&
                target_metadata.native().size() > MAX_PATH &&
                target_pending.native().size() > MAX_PATH &&
                candidate_pending.native().size() > MAX_PATH &&
                journal.native().size() > MAX_PATH,
            "long-path regression maps the 63-byte guest bound and crosses MAX_PATH only in ADS/candidate/journal suffixes");

        const std::uint32_t fs_handle = ios_open_path(
            creator, guest_memory, kRequest, kFsPath, "/dev/fs");
        passed &= expect(
            fs_handle > 0u && fs_handle < 0x80000000u,
            "long-path regression opens /dev/fs");
        write_isfs_attribute_block(
            creator, kAttr, kSourceDirectory, 3u, 3u, 3u, 0u);
        ios_ioctl_request(
            creator,
            guest_memory,
            kRequest + 0x20u,
            fs_handle,
            3u,
            kAttr,
            0x4Cu,
            0u,
            0u);
        passed &= expect(
            creator.read_u32(kRequest + 0x24u) == 0u,
            "long-path regression creates its persistent source directory");
        acknowledge_ios_reply(guest_memory);
        passed &= expect(
            publish_file(
                creator,
                guest_memory,
                fs_handle,
                kTargetPath,
                kOldBytes) &&
                publish_file(
                    creator,
                    guest_memory,
                    fs_handle,
                    kSourcePath,
                    kNewBytes) &&
                host_file_stream_exists_for_test(target_metadata) &&
                !host_file_stream_exists_for_test(target_pending),
            "long-path regression commits data and ADS beyond MAX_PATH");

        write_isfs_path_buffer(creator, kRename, kSourcePath);
        write_isfs_path_buffer(creator, kRename + 64u, kTargetPath);
        ios_ioctl_request(
            creator,
            guest_memory,
            kRequest + 0x140u,
            fs_handle,
            8u,
            kRename,
            128u,
            0u,
            0u);
        passed &= expect(
            creator.read_u32(kRequest + 0x144u) == 0u,
            "long-path regression renames through journal and backup paths beyond MAX_PATH");
        acknowledge_ios_reply(guest_memory);
        passed &= expect(
            !std::filesystem::exists(source) &&
                galaxy::host::read_binary_file(target) ==
                    std::vector<std::byte>(
                        kNewBytes.begin(), kNewBytes.end()) &&
                nand_rename_transaction_artifact_count_for_test(runtime_root) ==
                    0u,
            "long-path Rename publishes exact bytes and removes internal artifacts");

        passed &= expect(
            publish_file(
                creator,
                guest_memory,
                fs_handle,
                kSourcePath,
                kOldBytes),
            "long-path recovery fixture recreates a distinct source");
        source_identity = host_file_identity_for_test(source);
        target_identity = host_file_identity_for_test(target);
        const std::vector<std::byte> journal_bytes =
            gir1_rename_journal_for_test(
                kSourcePath,
                kTargetPath,
                kJournalProcessId,
                kJournalSequence,
                source_identity,
                target_identity);
        write_binary_file(journal, journal_bytes);
        passed &= expect(
            nand_rename_transaction_artifact_count_for_test(runtime_root) ==
                    1u &&
                galaxy::host::read_binary_file(journal) == journal_bytes,
            "long-path recovery fixture persists a GIR1 journal beyond MAX_PATH");
        ios_close_request(
            creator, guest_memory, kRequest + 0x180u, fs_handle);
        passed &= expect(
            creator.read_u32(kRequest + 0x184u) == 0u,
            "long-path recovery fixture closes /dev/fs");
        acknowledge_ios_reply(guest_memory);
        creator_owner.reset();

        auto cold_owner =
            std::make_unique<galaxy::host::GuestAddressSpace>();
        galaxy::host::GuestAddressSpace& cold = *cold_owner;
        galaxy::GuestMemoryV1* const cold_memory = cold.guest_memory();
        const std::uint32_t cold_handle = ios_open_path(
            cold,
            cold_memory,
            kRequest + 0x1C0u,
            kPath,
            kTargetPath,
            1u);
        passed &= expect(
            cold_handle > 0u && cold_handle < 0x80000000u,
            "cold startup recovers a long-path journal and opens its target");
        if (cold_handle > 0u && cold_handle < 0x80000000u) {
            ios_close_request(
                cold,
                cold_memory,
                kRequest + 0x200u,
                cold_handle);
            passed &= expect(
                cold.read_u32(kRequest + 0x204u) == 0u,
                "cold startup closes the recovered long-path target");
            acknowledge_ios_reply(cold_memory);
        }
        cold_owner.reset();
        passed &= expect(
            !std::filesystem::exists(journal) &&
                host_file_identity_for_test(source) == source_identity &&
                host_file_identity_for_test(target) == target_identity &&
                galaxy::host::read_binary_file(source) ==
                    std::vector<std::byte>(
                        kOldBytes.begin(), kOldBytes.end()) &&
                galaxy::host::read_binary_file(target) ==
                    std::vector<std::byte>(
                        kNewBytes.begin(), kNewBytes.end()) &&
                nand_rename_transaction_artifact_count_for_test(runtime_root) ==
                    0u,
            "cold recovery removes only the long GIR1 artifact and preserves both objects");
    }
    std::filesystem::remove_all(extended_anchor, cleanup_error);
    passed &= expect(
        !cleanup_error && !std::filesystem::exists(extended_anchor),
        "long-path regression removes its isolated extended-length test tree");

    constexpr std::array<std::wstring_view, 4> kRejectedNamespaces{
        LR"(\\?\GLOBALROOT\Device\HarddiskVolumeShadowCopy1\galaxy-nand)",
        LR"(\\?\Volume{00000000-0000-0000-0000-000000000000}\galaxy-nand)",
        LR"(\\.\C:\galaxy-nand)",
        LR"(\\?\UNC\server)",
    };
    for (const std::wstring_view rejected : kRejectedNamespaces) {
        ScopedWideEnv set_rejected_nand_root(
            L"GALAXY_NAND_ROOT", std::filesystem::path(rejected));
        passed &= expect_runtime_error(
            [] {
                galaxy::host::GuestAddressSpace rejected_owner;
                static_cast<void>(
                    galaxy::host::NativeNandTestAccess::
                        ensure_rmge01_data_directory(rejected_owner));
            },
            "native NAND rejects non-file extended device namespaces before mutation");
    }
    return passed;
}

bool native_nand_fresh_rfl_safe_replace_works(
    const std::filesystem::path& shared_test_root) {
    constexpr std::string_view kMenuPath = "/shared2/menu";
    constexpr std::string_view kFaceLibPath = "/shared2/menu/FaceLib";
    constexpr std::string_view kRflPath =
        "/shared2/menu/FaceLib/RFL_DB.dat";
    constexpr std::string_view kTempPath = "/tmp/rflsafe";
    constexpr std::string_view kTempRflPath = "/tmp/rflsafe/RFL_DB.dat";
    constexpr std::uint32_t kPath = 0x13540000u;
    constexpr std::uint32_t kFsPath = 0x13540100u;
    constexpr std::uint32_t kAttr = 0x13540200u;
    constexpr std::uint32_t kAttrOut = 0x13540300u;
    constexpr std::uint32_t kRename = 0x13540400u;
    constexpr std::uint32_t kRequest = 0x13540600u;
    constexpr std::uint32_t kBuffer = 0x13600000u;
    constexpr std::uint32_t kIsfsErrInvalid =
        static_cast<std::uint32_t>(-101);

    const auto close_handle = [](
                                  galaxy::host::GuestAddressSpace& memory,
                                  galaxy::GuestMemoryV1* guest_memory,
                                  std::uint32_t request,
                                  std::uint32_t handle) {
        ios_close_request(memory, guest_memory, request, handle);
        const bool closed = memory.read_u32(request + 4u) == 0u;
        acknowledge_ios_reply(guest_memory);
        return closed;
    };
    const auto has_exact_rfl_attributes = [](
                                              galaxy::host::GuestAddressSpace& memory,
                                              std::uint32_t address) {
        const std::byte* const group = memory.pointer(address + 4u, 2u);
        const std::byte* const modes = memory.pointer(address + 70u, 4u);
        return memory.read_u32(address) == 0x1001u &&
            std::to_integer<std::uint8_t>(group[0]) == 0x30u &&
            std::to_integer<std::uint8_t>(group[1]) == 0x31u &&
            std::to_integer<std::uint8_t>(modes[0]) == 3u &&
            std::to_integer<std::uint8_t>(modes[1]) == 3u &&
            std::to_integer<std::uint8_t>(modes[2]) == 3u &&
            std::to_integer<std::uint8_t>(modes[3]) == 0u;
    };

    bool passed = true;
    std::error_code cleanup_error;
    const std::filesystem::path isolated_root =
        shared_test_root / L"fresh_rfl_safe_replace";
    std::filesystem::remove_all(isolated_root, cleanup_error);
    const std::filesystem::path host_rfl =
        isolated_root / L"shared2" / L"menu" / L"FaceLib" /
        L"RFL_DB.dat";
    const std::filesystem::path host_temp_rfl =
        isolated_root / L"tmp" / L"rflsafe" / L"RFL_DB.dat";
    const std::vector<std::byte> valid_rfl =
        valid_empty_rfl_database_for_test();

    {
        ScopedWideEnv set_isolated_nand_root(
            L"GALAXY_NAND_ROOT", isolated_root);
        {
            galaxy::host::GuestAddressSpace memory;
            galaxy::GuestMemoryV1* const guest_memory = memory.guest_memory();
            const std::uint32_t fs_handle = ios_open_path(
                memory, guest_memory, kRequest, kFsPath, "/dev/fs");
            passed &= expect(
                fs_handle > 0u && fs_handle < 0x80000000u,
                "fresh-RFL regression opens /dev/fs");

            for (const std::string_view directory :
                 {kMenuPath, kFaceLibPath}) {
                write_isfs_attribute_block(
                    memory, kAttr, directory, 3u, 3u, 3u, 0u);
                ios_ioctl_request(
                    memory,
                    guest_memory,
                    kRequest + 0x40u,
                    fs_handle,
                    3u,
                    kAttr,
                    0x4Cu,
                    0u,
                    0u);
                passed &= expect(
                    memory.read_u32(kRequest + 0x44u) == 0u,
                    "fresh-RFL parent directory accepts exact 333/attr0 metadata");
                acknowledge_ios_reply(guest_memory);
            }
            passed &= expect(
                std::filesystem::is_directory(host_rfl.parent_path()),
                "fresh-RFL exact parent chain is present before direct create");

            constexpr std::array<std::array<std::uint8_t, 4>, 4>
                kInvalidRflMetadata{{
                    {2u, 3u, 3u, 0u},
                    {3u, 2u, 3u, 0u},
                    {3u, 3u, 2u, 0u},
                    {3u, 3u, 3u, 1u},
                }};
            bool every_invalid_create_rejected = true;
            for (const auto& metadata : kInvalidRflMetadata) {
                write_isfs_attribute_block(
                    memory,
                    kAttr,
                    kRflPath,
                    metadata[0],
                    metadata[1],
                    metadata[2],
                    metadata[3]);
                ios_ioctl_request(
                    memory,
                    guest_memory,
                    kRequest + 0x40u,
                    fs_handle,
                    9u,
                    kAttr,
                    0x4Cu,
                    0u,
                    0u);
                every_invalid_create_rejected &=
                    memory.read_u32(kRequest + 0x44u) == kIsfsErrInvalid &&
                    !std::filesystem::exists(host_rfl);
                acknowledge_ios_reply(guest_memory);
            }
            passed &= expect(
                every_invalid_create_rejected,
                "RFL_DB.dat direct CreateFile rejects every non-333/attr0 shape without publication");

            write_isfs_attribute_block(
                memory, kAttr, kRflPath, 3u, 3u, 3u, 0u);
            ios_ioctl_request(
                memory,
                guest_memory,
                kRequest + 0x40u,
                fs_handle,
                9u,
                kAttr,
                0x4Cu,
                0u,
                0u);
            passed &= expect(
                memory.read_u32(kRequest + 0x44u) == 0u &&
                    std::filesystem::is_regular_file(host_rfl) &&
                    std::filesystem::file_size(host_rfl) == 0u &&
                    galaxy::host::NativeNandTestAccess::
                        provisional_nand_file_tracked(memory, kRflPath),
                "exact RFL_DB.dat direct CreateFile publishes and tracks the bound empty original");
            acknowledge_ios_reply(guest_memory);

            write_isfs_path_buffer(memory, kPath, kRflPath);
            memory.clear(kAttrOut, 0x4Cu);
            ios_ioctl_request(
                memory,
                guest_memory,
                kRequest + 0x80u,
                fs_handle,
                6u,
                kPath,
                64u,
                kAttrOut,
                0x4Cu);
            passed &= expect(
                memory.read_u32(kRequest + 0x84u) == 0u &&
                    has_exact_rfl_attributes(memory, kAttrOut),
                "same-instance GetAttr exposes RMGE owner/group and exact 333/attr0 for provisional RFL");
            acknowledge_ios_reply(guest_memory);

            const std::uint32_t provisional_handle = ios_open_path(
                memory,
                guest_memory,
                kRequest + 0xC0u,
                kPath,
                kRflPath,
                1u);
            passed &= expect(
                provisional_handle > 0u &&
                    provisional_handle < 0x80000000u,
                "same-instance Open accepts the exact bound provisional empty RFL");
            passed &= expect(
                close_handle(
                    memory,
                    guest_memory,
                    kRequest + 0x100u,
                    provisional_handle),
                "provisional RFL handle closes before safe replacement");

            write_isfs_attribute_block(
                memory, kAttr, kTempPath, 3u, 3u, 3u, 0u);
            ios_ioctl_request(
                memory,
                guest_memory,
                kRequest + 0x140u,
                fs_handle,
                3u,
                kAttr,
                0x4Cu,
                0u,
                0u);
            passed &= expect(
                memory.read_u32(kRequest + 0x144u) == 0u,
                "safe-open temp directory accepts exact 333/attr0 metadata");
            acknowledge_ios_reply(guest_memory);

            write_isfs_attribute_block(
                memory, kAttr, kTempRflPath, 3u, 3u, 3u, 0u);
            ios_ioctl_request(
                memory,
                guest_memory,
                kRequest + 0x140u,
                fs_handle,
                9u,
                kAttr,
                0x4Cu,
                0u,
                0u);
            passed &= expect(
                memory.read_u32(kRequest + 0x144u) == 0u &&
                    std::filesystem::is_regular_file(host_temp_rfl),
                "generic same-basename safe-open temp file publishes atomically");
            acknowledge_ios_reply(guest_memory);

            const std::uint32_t temp_handle = ios_open_path(
                memory,
                guest_memory,
                kRequest + 0x180u,
                kPath,
                kTempRflPath,
                3u);
            passed &= expect(
                temp_handle > 0u && temp_handle < 0x80000000u,
                "safe-open temp RFL opens read/write");
            memory.copy(kBuffer, valid_rfl);
            ios_write_request(
                memory,
                guest_memory,
                kRequest + 0x1C0u,
                temp_handle,
                kBuffer,
                static_cast<std::uint32_t>(valid_rfl.size()));
            passed &= expect(
                memory.read_u32(kRequest + 0x1C4u) ==
                        static_cast<std::uint32_t>(valid_rfl.size()) &&
                    galaxy::host::read_binary_file(host_temp_rfl) == valid_rfl,
                "safe-open temp receives the complete CRC-valid generated empty RFL before success");
            acknowledge_ios_reply(guest_memory);
            passed &= expect(
                close_handle(
                    memory,
                    guest_memory,
                    kRequest + 0x200u,
                    temp_handle),
                "complete safe-open temp RFL closes before Rename");

            write_isfs_path_buffer(memory, kRename, kTempRflPath);
            write_isfs_path_buffer(memory, kRename + 64u, kRflPath);
            ios_ioctl_request(
                memory,
                guest_memory,
                kRequest + 0x240u,
                fs_handle,
                8u,
                kRename,
                128u,
                0u,
                0u);
            passed &= expect(
                memory.read_u32(kRequest + 0x244u) == 0u &&
                    !std::filesystem::exists(host_temp_rfl) &&
                    galaxy::host::read_binary_file(host_rfl) == valid_rfl &&
                    !galaxy::host::NativeNandTestAccess::
                        provisional_nand_file_tracked(memory, kRflPath),
                "safe-close Rename replaces the empty target with the validated same-name object and consumes provisional authority");
            acknowledge_ios_reply(guest_memory);

            write_isfs_path_buffer(memory, kPath, kRflPath);
            memory.clear(kAttrOut, 0x4Cu);
            ios_ioctl_request(
                memory,
                guest_memory,
                kRequest + 0x280u,
                fs_handle,
                6u,
                kPath,
                64u,
                kAttrOut,
                0x4Cu);
            passed &= expect(
                memory.read_u32(kRequest + 0x284u) == 0u &&
                    has_exact_rfl_attributes(memory, kAttrOut),
                "post-Rename GetAttr succeeds through ordinary persistent validation");
            acknowledge_ios_reply(guest_memory);
            passed &= expect(
                close_handle(
                    memory,
                    guest_memory,
                    kRequest + 0x2C0u,
                    fs_handle),
                "fresh-RFL /dev/fs handle closes after successful safe replacement");
        }

        galaxy::host::GuestAddressSpace cold;
        galaxy::GuestMemoryV1* const cold_guest_memory = cold.guest_memory();
        const std::uint32_t cold_rfl_handle = ios_open_path(
            cold,
            cold_guest_memory,
            kRequest,
            kPath,
            kRflPath,
            1u);
        passed &= expect(
            cold_rfl_handle > 0u && cold_rfl_handle < 0x80000000u,
            "cold GuestAddressSpace reopens the fully validated replaced RFL");
        cold.clear(kBuffer, 4u);
        ios_read_request(
            cold,
            cold_guest_memory,
            kRequest + 0x40u,
            cold_rfl_handle,
            kBuffer,
            4u);
        passed &= expect(
            cold.read_u32(kRequest + 0x44u) == 4u &&
                cold.read_u32(kBuffer) == 0x524E4F44u,
            "cold RFL read returns the RNOD database header");
        acknowledge_ios_reply(cold_guest_memory);
        passed &= expect(
            close_handle(
                cold,
                cold_guest_memory,
                kRequest + 0x80u,
                cold_rfl_handle),
            "cold RFL handle closes cleanly");

        const std::uint32_t cold_fs_handle = ios_open_path(
            cold,
            cold_guest_memory,
            kRequest + 0xC0u,
            kFsPath,
            "/dev/fs");
        write_isfs_path_buffer(cold, kPath, kRflPath);
        cold.clear(kAttrOut, 0x4Cu);
        ios_ioctl_request(
            cold,
            cold_guest_memory,
            kRequest + 0x100u,
            cold_fs_handle,
            6u,
            kPath,
            64u,
            kAttrOut,
            0x4Cu);
        passed &= expect(
            cold.read_u32(kRequest + 0x104u) == 0u &&
                has_exact_rfl_attributes(cold, kAttrOut),
            "cold GetAttr revalidates persistent RMGE 333/attr0 RFL metadata");
        acknowledge_ios_reply(cold_guest_memory);
        passed &= expect(
            close_handle(
                cold,
                cold_guest_memory,
                kRequest + 0x140u,
                cold_fs_handle),
            "cold fresh-RFL /dev/fs handle closes cleanly");
    }

    std::filesystem::remove_all(isolated_root, cleanup_error);
    return passed;
}

bool expect_bytes(
    std::span<const std::uint8_t> actual,
    std::span<const std::uint8_t> expected,
    const char* message) {
    if (actual.size() != expected.size()) {
        std::cerr << "FAILED: " << message << " size actual=" << actual.size()
                  << " expected=" << expected.size() << '\n';
        return false;
    }
    for (std::size_t i = 0; i < actual.size(); ++i) {
        if (actual[i] != expected[i]) {
            std::cerr << "FAILED: " << message << " byte[" << i
                      << "] actual=0x" << std::hex
                      << static_cast<unsigned>(actual[i]) << " expected=0x"
                      << static_cast<unsigned>(expected[i]) << std::dec << '\n';
            return false;
        }
    }
    return true;
}

std::vector<std::byte> acl_packet(
    std::uint16_t cid,
    std::span<const std::byte> payload) {
    const auto append_le16 =
        [](std::vector<std::byte>& bytes, std::uint16_t value) {
            bytes.push_back(static_cast<std::byte>(value));
            bytes.push_back(static_cast<std::byte>(value >> 8));
        };
    std::vector<std::byte> packet;
    append_le16(packet, 0x2100);
    append_le16(packet, static_cast<std::uint16_t>(payload.size() + 4u));
    append_le16(packet, static_cast<std::uint16_t>(payload.size()));
    append_le16(packet, cid);
    packet.insert(packet.end(), payload.begin(), payload.end());
    return packet;
}

std::array<std::byte, 23> wiimote_memory_write_report(
    std::uint8_t common,
    std::uint32_t address,
    std::span<const std::byte> data) {
    if (data.size() > 16u) {
        throw std::runtime_error("test Wiimote memory write is too large");
    }
    std::array<std::byte, 23> report{};
    report[0] = std::byte{0xA2};
    report[1] = std::byte{0x16};
    report[2] = std::byte{common};
    report[3] = static_cast<std::byte>(address >> 16u);
    report[4] = static_cast<std::byte>(address >> 8u);
    report[5] = static_cast<std::byte>(address);
    report[6] = static_cast<std::byte>(data.size());
    std::copy(data.begin(), data.end(), report.begin() + 7);
    return report;
}

std::array<std::byte, 23> wiimote_speaker_data_report(
    std::uint8_t common,
    std::span<const std::byte> data) {
    if (data.size() > 20u) {
        throw std::runtime_error("test Wiimote speaker data is too large");
    }
    std::array<std::byte, 23> report{};
    report[0] = std::byte{0xA2};
    report[1] = std::byte{0x18};
    report[2] = std::byte{common};
    std::copy(data.begin(), data.end(), report.begin() + 3);
    return report;
}

std::uint64_t fake_tick_source(void* user);

void invalid_native_dsp_mram_worker_entry(galaxy::DspContext& context) {
    context.pc = 0x1234u;
    static_cast<void>(
        galaxy::dsp_external_read_byte(context, 0x81800000u));
}

void synthetic_native_dsp_reset_wait_entry(galaxy::DspContext& context) {
    context.pc = galaxy::kRmge01DspCmbhCommandPollPc;
    for (;;) {
        static_cast<void>(
            galaxy::dsp_ifx_read(context, galaxy::kDspIfxCmbh));
    }
}

std::atomic<bool> synthetic_native_dsp_selection_probe_ready{};

void synthetic_native_dsp_selection_probe_wait_entry(
    galaxy::DspContext& context) {
    constexpr std::uint32_t kChannelRecordHostAddress = 0x807B04E0u;
    galaxy::dsp_channel_selection_dma_probe_record_selection_write(
        context.channel_selection_dma_probe, 0x04FCu, 0x8000u);
    galaxy::dsp_channel_selection_dma_probe_record_selection_write(
        context.channel_selection_dma_probe, 0x04FDu, 0u);
    galaxy::dsp_channel_selection_dma_probe_record_selection_write(
        context.channel_selection_dma_probe, 0x04FEu, 0u);
    galaxy::dsp_channel_selection_dma_probe_record_selection_write(
        context.channel_selection_dma_probe, 0x04FFu, 0u);
    galaxy::dsp_channel_selection_dma_probe_record_selected_channel_branch(
        context.channel_selection_dma_probe,
        0u,
        0x8000u,
        kChannelRecordHostAddress);
    galaxy::dsp_channel_selection_dma_probe_record_dma(
        context.channel_selection_dma_probe,
        0x05EFu,
        /*instruction_memory=*/false,
        /*to_host=*/false,
        kChannelRecordHostAddress,
        0x0800u,
        0x0180u);
    synthetic_native_dsp_selection_probe_ready.store(
        true, std::memory_order_release);
    synthetic_native_dsp_reset_wait_entry(context);
}

bool native_bt_hci_connection_lifecycle_retries_deterministically() {
    ScopedEnv enable_native_bt("GALAXY_NATIVE_BT_WIIMOTE", "1");
    auto memory_owner =
        std::make_unique<galaxy::host::GuestAddressSpace>();
    galaxy::host::GuestAddressSpace& memory = *memory_owner;
    using TestAccess = galaxy::host::NativeBluetoothTestAccess;
    std::uint64_t fake_ticks = 0x1000u;
    memory.set_tick_source(&fake_tick_source, &fake_ticks);

    const std::array<std::byte, 4> scan_enable{
        std::byte{0x1A}, std::byte{0x0C}, std::byte{0x01}, std::byte{0x02}};
    const std::array<std::byte, 4> scan_disable{
        std::byte{0x1A}, std::byte{0x0C}, std::byte{0x01}, std::byte{0x00}};
    const std::array<std::byte, 3> hci_reset{
        std::byte{0x03}, std::byte{0x0C}, std::byte{0x00}};
    const std::array<std::byte, 10> accept_connection{
        std::byte{0x09}, std::byte{0x04}, std::byte{0x07},
        std::byte{0x11}, std::byte{0x02}, std::byte{0x19},
        std::byte{0x79}, std::byte{0x00}, std::byte{0x00},
        std::byte{0x00}};
    const std::array<std::byte, 10> reject_connection{
        std::byte{0x0A}, std::byte{0x04}, std::byte{0x07},
        std::byte{0x11}, std::byte{0x02}, std::byte{0x19},
        std::byte{0x79}, std::byte{0x00}, std::byte{0x00},
        std::byte{0x0E}};
    constexpr std::array<std::uint8_t, 12> kExpectedConnectionRequest{
        0x04, 0x0A, 0x11, 0x02, 0x19, 0x79,
        0x00, 0x00, 0x04, 0x25, 0x00, 0x01};

    const std::array<std::uint8_t, 6> expected_scan_complete{
        0x0E, 0x04, 0x01, 0x1A, 0x0C, 0x00};
    const std::array<std::uint8_t, 6> expected_reject_status{
        0x0F, 0x04, 0x00, 0x01, 0x0A, 0x04};
    const std::array<std::uint8_t, 13> expected_timeout_complete{
        0x03, 0x0B, 0x10, 0x00, 0x00, 0x11, 0x02,
        0x19, 0x79, 0x00, 0x00, 0x01, 0x00};
    const std::array<std::uint8_t, 13> expected_reject_complete{
        0x03, 0x0B, 0x0E, 0x00, 0x00, 0x11, 0x02,
        0x19, 0x79, 0x00, 0x00, 0x01, 0x00};

    const auto expect_event_bytes = [](
                                        const std::vector<std::byte>& event,
                                        auto expected,
                                        const char* message) {
        return expect_bytes(
            std::span<const std::uint8_t>(
                reinterpret_cast<const std::uint8_t*>(event.data()),
                event.size()),
            expected,
            message);
    };

    bool passed = true;
    TestAccess::queue_controller_command(memory, scan_enable);
    passed &= expect(
        TestAccess::scan_enabled(memory) &&
            !TestAccess::connection_requested(memory),
        "native BT Write_Scan_Enable(2) enables page scan before connection injection");
    passed &= expect_event_bytes(
        TestAccess::take_hci_event(memory),
        expected_scan_complete,
        "native BT scan-enable command-complete wire identity");

    memory.poll_native_bt_reconnect();
    passed &= expect(
        TestAccess::connection_requested(memory) &&
            !TestAccess::connection_request_delivered(memory) &&
            TestAccess::connection_request_deadline_ticks(memory) == 0u &&
            TestAccess::last_connection_attempt_ticks(memory) == fake_ticks &&
            TestAccess::queued_hci_events(memory) == 1u,
        "native BT queued Connection_Request does not start CAT before HCI delivery");

    fake_ticks +=
        galaxy::host::kNativeBluetoothConnectionAcceptTimeoutTicks * 2u;
    memory.poll_native_bt_reconnect();
    passed &= expect(
        TestAccess::connection_requested(memory) &&
            !TestAccess::connection_request_delivered(memory) &&
            TestAccess::connection_request_deadline_ticks(memory) == 0u &&
            TestAccess::queued_hci_events(memory) == 1u,
        "native BT undelivered Connection_Request cannot expire in its queue");

    const std::vector<std::byte> first_request =
        TestAccess::take_hci_event(memory);
    const std::uint64_t first_request_deadline =
        fake_ticks + galaxy::host::kNativeBluetoothConnectionAcceptTimeoutTicks;
    passed &= expect_event_bytes(
        first_request,
        kExpectedConnectionRequest,
        "native BT first inbound request wire identity");
    passed &= expect(
        TestAccess::connection_request_delivered(memory) &&
            TestAccess::connection_request_deadline_ticks(memory) ==
                first_request_deadline,
        "native BT CAT starts at the exact HCI delivery boundary");

    fake_ticks = first_request_deadline - 1u;
    memory.poll_native_bt_reconnect();
    passed &= expect(
        TestAccess::connection_requested(memory) &&
            TestAccess::queued_hci_events(memory) == 0u,
        "native BT delivered request remains live one tick before CAT");
    fake_ticks = first_request_deadline;
    memory.poll_native_bt_reconnect();
    const std::uint64_t ignored_retry_deadline =
        fake_ticks + galaxy::host::kNativeBluetoothConnectionRetryDelayTicks;
    passed &= expect(
        !TestAccess::connection_requested(memory) &&
            !TestAccess::connection_request_delivered(memory) &&
            TestAccess::connection_request_deadline_ticks(memory) == 0u &&
            TestAccess::next_connection_attempt_ticks(memory) ==
                ignored_retry_deadline &&
            TestAccess::queued_hci_events(memory) == 1u,
        "native BT CAT expiry closes the request and arms one exact retry");
    passed &= expect_event_bytes(
        TestAccess::take_hci_event(memory),
        expected_timeout_complete,
        "native BT CAT expiry emits Connection_Complete status 0x10");

    fake_ticks = ignored_retry_deadline - 1u;
    memory.poll_native_bt_reconnect();
    passed &= expect(
        !TestAccess::connection_requested(memory) &&
            TestAccess::queued_hci_events(memory) == 0u,
        "native BT ignored-request retry cannot fire one tick early");
    fake_ticks = ignored_retry_deadline;
    memory.poll_native_bt_reconnect();
    passed &= expect(
        TestAccess::connection_requested(memory) &&
            !TestAccess::connection_request_delivered(memory) &&
            TestAccess::queued_hci_events(memory) == 1u,
        "native BT ignored-request retry fires exactly on its deterministic deadline");
    static_cast<void>(TestAccess::take_hci_event(memory));

    auto malformed_reject = reject_connection;
    malformed_reject[9] = std::byte{0x01};
    passed &= expect_runtime_error(
        [&] {
            static_cast<void>(TestAccess::handle_connection_command(
                memory, malformed_reject));
        },
        "native BT Reject_Connection_Request rejects a reserved reason");
    passed &= expect(
        TestAccess::connection_requested(memory),
        "native BT malformed reject leaves the valid pending request intact");

    const std::uint64_t delivered_deadline =
        TestAccess::connection_request_deadline_ticks(memory);
    TestAccess::queue_controller_command(memory, scan_disable);
    passed &= expect(
        !TestAccess::scan_enabled(memory) &&
            TestAccess::connection_requested(memory) &&
            TestAccess::connection_request_delivered(memory) &&
            TestAccess::connection_request_deadline_ticks(memory) ==
                delivered_deadline &&
            TestAccess::next_connection_attempt_ticks(memory) == 0u,
        "native BT page-scan disable preserves an already delivered request until Host disposition");
    static_cast<void>(TestAccess::take_hci_event(memory));

    passed &= expect(
        TestAccess::handle_connection_command(memory, reject_connection),
        "native BT delivered request remains rejectable after page-scan disable");
    passed &= expect(
        !TestAccess::connection_requested(memory) &&
            !TestAccess::connected(memory) &&
            TestAccess::next_connection_attempt_ticks(memory) == 0u &&
            TestAccess::queued_hci_events(memory) == 2u,
        "native BT explicit reject while scan-disabled closes the request without retry");
    passed &= expect_event_bytes(
        TestAccess::take_hci_event(memory),
        expected_reject_status,
        "native BT reject command-status wire identity");
    passed &= expect_event_bytes(
        TestAccess::take_hci_event(memory),
        expected_reject_complete,
        "native BT rejected connection-complete wire identity");

    TestAccess::queue_controller_command(memory, scan_enable);
    static_cast<void>(TestAccess::take_hci_event(memory));
    memory.poll_native_bt_reconnect();
    static_cast<void>(TestAccess::take_hci_event(memory));
    TestAccess::queue_controller_command(memory, scan_disable);
    static_cast<void>(TestAccess::take_hci_event(memory));
    passed &= expect(
        TestAccess::handle_connection_command(memory, accept_connection) &&
            TestAccess::connected(memory) &&
            !TestAccess::connection_requested(memory),
        "native BT delivered request remains acceptable after page-scan disable");

    TestAccess::queue_controller_command(memory, hci_reset);
    const std::vector<std::byte> reset_complete =
        TestAccess::take_hci_event(memory);
    const std::array<std::uint8_t, 6> expected_reset_complete{
        0x0E, 0x04, 0x01, 0x03, 0x0C, 0x00};
    passed &= expect_event_bytes(
        reset_complete,
        expected_reset_complete,
        "native BT HCI Reset command-complete is the sole surviving event");
    passed &= expect(
        !TestAccess::scan_enabled(memory) &&
            !TestAccess::connection_requested(memory) &&
            !TestAccess::connection_request_delivered(memory) &&
            !TestAccess::connected(memory) &&
            TestAccess::connection_request_deadline_ticks(memory) == 0u &&
            TestAccess::next_connection_attempt_ticks(memory) == 0u &&
            TestAccess::control_local_cid(memory) == 0u &&
            TestAccess::interrupt_local_cid(memory) == 0u &&
            TestAccess::l2cap_retry_psm(memory) == 0u &&
            TestAccess::l2cap_connection_signal_id(memory) == 0u &&
            TestAccess::l2cap_config_signal_id(memory) == 0u &&
            TestAccess::l2cap_configuration_psm(memory) == 0u &&
            TestAccess::l2cap_retry_count(memory, 0x0011u) == 0u &&
            TestAccess::l2cap_retry_count(memory, 0x0013u) == 0u &&
            TestAccess::queued_hci_events(memory) == 0u,
        "native BT HCI Reset clears all HCI and L2CAP transaction identity");

    {
        auto queued_memory_owner =
            std::make_unique<galaxy::host::GuestAddressSpace>();
        galaxy::host::GuestAddressSpace& queued_memory =
            *queued_memory_owner;
        std::uint64_t queued_ticks = 0x2000u;
        queued_memory.set_tick_source(&fake_tick_source, &queued_ticks);
        TestAccess::queue_controller_command(queued_memory, scan_enable);
        static_cast<void>(TestAccess::take_hci_event(queued_memory));
        queued_memory.poll_native_bt_reconnect();
        TestAccess::queue_controller_command(queued_memory, scan_disable);
        passed &= expect(
            !TestAccess::connection_requested(queued_memory) &&
                !TestAccess::connection_request_delivered(queued_memory) &&
                TestAccess::connection_request_deadline_ticks(queued_memory) ==
                    0u &&
                TestAccess::queued_hci_events(queued_memory) == 1u,
            "native BT page-scan disable cancels only an undelivered queued request");
    }

    {
        auto overflow_memory_owner =
            std::make_unique<galaxy::host::GuestAddressSpace>();
        galaxy::host::GuestAddressSpace& overflow_memory =
            *overflow_memory_owner;
        std::uint64_t overflow_ticks =
            std::numeric_limits<std::uint64_t>::max() - 8u;
        overflow_memory.set_tick_source(
            &fake_tick_source, &overflow_ticks);
        TestAccess::queue_controller_command(overflow_memory, scan_enable);
        TestAccess::clear_hci_events(overflow_memory);
        overflow_memory.poll_native_bt_reconnect();
        passed &= expect(
            TestAccess::connection_request_deadline_ticks(overflow_memory) ==
                0u,
            "native BT queued request has no deadline near tick overflow");
        static_cast<void>(TestAccess::take_hci_event(overflow_memory));
        passed &= expect(
            TestAccess::connection_request_deadline_ticks(overflow_memory) ==
                std::numeric_limits<std::uint64_t>::max(),
            "native BT delivered CAT deadline saturates instead of wrapping");
    }
    return passed;
}

bool native_bt_l2cap_rejection_recovery_is_bounded() {
    ScopedEnv enable_native_bt("GALAXY_NATIVE_BT_WIIMOTE", "1");
    ScopedEnv control_first("GALAXY_NATIVE_BT_INTERRUPT_FIRST", "0");
    using TestAccess = galaxy::host::NativeBluetoothTestAccess;

    const auto byte_at = [](const std::vector<std::byte>& bytes,
                            std::size_t index) {
        return static_cast<std::uint8_t>(bytes.at(index));
    };
    const auto le16_at = [&](const std::vector<std::byte>& bytes,
                             std::size_t index) {
        return static_cast<std::uint16_t>(
            byte_at(bytes, index) |
            static_cast<std::uint16_t>(byte_at(bytes, index + 1u)) << 8);
    };
    const auto connection_response = [](
                                         std::uint8_t id,
                                         std::uint16_t dcid,
                                         std::uint16_t scid,
                                         std::uint16_t result,
                                         std::uint16_t status) {
        return std::array<std::byte, 12>{
            std::byte{0x03}, static_cast<std::byte>(id),
            std::byte{0x08}, std::byte{0x00},
            static_cast<std::byte>(dcid),
            static_cast<std::byte>(dcid >> 8),
            static_cast<std::byte>(scid),
            static_cast<std::byte>(scid >> 8),
            static_cast<std::byte>(result),
            static_cast<std::byte>(result >> 8),
            static_cast<std::byte>(status),
            static_cast<std::byte>(status >> 8)};
    };
    const auto config_request = [](
                                    std::uint8_t id,
                                    std::uint16_t local_cid) {
        return std::array<std::byte, 8>{
            std::byte{0x04}, static_cast<std::byte>(id),
            std::byte{0x04}, std::byte{0x00},
            static_cast<std::byte>(local_cid),
            static_cast<std::byte>(local_cid >> 8),
            std::byte{0x00}, std::byte{0x00}};
    };
    const auto config_response = [](
                                     std::uint8_t id,
                                     std::uint16_t local_cid) {
        return std::array<std::byte, 10>{
            std::byte{0x05}, static_cast<std::byte>(id),
            std::byte{0x06}, std::byte{0x00},
            static_cast<std::byte>(local_cid),
            static_cast<std::byte>(local_cid >> 8),
            std::byte{0x00}, std::byte{0x00},
            std::byte{0x00}, std::byte{0x00}};
    };
    const auto submit_signal = [](
                                   galaxy::host::GuestAddressSpace& memory,
                                   std::span<const std::byte> signal) {
        const std::vector<std::byte> packet = acl_packet(0x0001u, signal);
        return TestAccess::handle_acl_packet(memory, packet);
    };
    const auto expect_connect_request = [&byte_at, &le16_at](
                                            const std::vector<std::byte>& packet,
                                            std::uint16_t psm,
                                            const char* message,
                                            bool& passed) {
        const bool valid =
            packet.size() == 16u && byte_at(packet, 8u) == 0x02u &&
            le16_at(packet, 10u) == 4u && le16_at(packet, 12u) == psm &&
            le16_at(packet, 14u) >= 0x0040u;
        passed &= expect(valid, message);
        return valid ? le16_at(packet, 14u) : std::uint16_t{0};
    };
    const auto accept_and_configure =
        [&](galaxy::host::GuestAddressSpace& memory,
            std::uint16_t local_cid,
            std::uint16_t remote_cid,
            std::uint8_t peer_config_id,
            const char* channel_name,
            bool& passed) {
            const auto accepted = connection_response(
                TestAccess::l2cap_connection_signal_id(memory),
                remote_cid,
                local_cid,
                0x0000u,
                0x0000u);
            passed &= expect(
                submit_signal(memory, accepted),
                channel_name);
            const std::vector<std::byte> outbound_config =
                TestAccess::take_acl_event(memory);
            passed &= expect(
                outbound_config.size() == 20u &&
                    byte_at(outbound_config, 8u) == 0x04u &&
                    le16_at(outbound_config, 12u) == remote_cid,
                "native BT accepted retry emits configuration request for the exact peer CID");

            const auto peer_config =
                config_request(peer_config_id, local_cid);
            passed &= expect(
                submit_signal(memory, peer_config),
                "native BT peer configuration request is handled after retry acceptance");
            const std::vector<std::byte> outbound_config_response =
                TestAccess::take_acl_event(memory);
            passed &= expect(
                outbound_config_response.size() == 18u &&
                    byte_at(outbound_config_response, 8u) == 0x05u &&
                    le16_at(outbound_config_response, 12u) == remote_cid,
                "native BT retry configuration response targets the exact peer CID");

            const auto peer_config_response = config_response(
                TestAccess::l2cap_config_signal_id(memory), local_cid);
            passed &= expect(
                submit_signal(memory, peer_config_response),
                "native BT peer configuration response completes the retried channel");
        };

    bool passed = true;
    {
        auto memory_owner =
            std::make_unique<galaxy::host::GuestAddressSpace>();
        galaxy::host::GuestAddressSpace& memory = *memory_owner;
        std::uint64_t fake_ticks =
            galaxy::host::kNativeBluetoothL2capPostAuthDelayTicks * 4u;
        memory.set_tick_source(&fake_tick_source, &fake_ticks);
        TestAccess::arm_l2cap_post_auth_open(
            memory,
            fake_ticks -
                galaxy::host::kNativeBluetoothL2capPostAuthDelayTicks);
        memory.poll_native_bt_reconnect();
        const std::uint16_t first_control_cid = expect_connect_request(
            TestAccess::take_acl_event(memory),
            0x0011u,
            "native BT initial control PSM request precedes rejection recovery",
            passed);
        passed &= expect(
            first_control_cid == 0x0040u,
            "native BT control recovery starts from the first dynamic CID");

        // A rejected response may legally carry invalid CIDs. Recovery must be
        // keyed by the one outstanding channel, not by those invalid fields.
        const auto reject_control =
            connection_response(
                TestAccess::l2cap_connection_signal_id(memory),
                0u,
                0u,
                0x0003u,
                0u);
        passed &= expect(
            submit_signal(memory, reject_control),
            "native BT control security rejection is recoverable");
        const std::uint64_t control_retry_deadline =
            fake_ticks + galaxy::host::kNativeBluetoothL2capRetryDelayTicks;
        passed &= expect(
            TestAccess::control_local_cid(memory) == 0u &&
                TestAccess::l2cap_retry_psm(memory) == 0x0011u &&
                TestAccess::l2cap_retry_count(memory, 0x0011u) == 1u &&
                TestAccess::l2cap_retry_deadline_ticks(memory) ==
                    control_retry_deadline,
            "native BT control rejection clears half-open state and arms exact retry one");
        fake_ticks = control_retry_deadline - 1u;
        memory.poll_native_bt_reconnect();
        passed &= expect(
            TestAccess::queued_acl_events(memory) == 0u,
            "native BT control retry cannot fire one tick early");
        fake_ticks = control_retry_deadline;
        memory.poll_native_bt_reconnect();
        const std::uint16_t retried_control_cid = expect_connect_request(
            TestAccess::take_acl_event(memory),
            0x0011u,
            "native BT control retry fires exactly on its tick deadline",
            passed);
        passed &= expect(
            retried_control_cid == 0x0041u &&
                TestAccess::l2cap_retry_psm(memory) == 0u &&
                TestAccess::l2cap_retry_count(memory, 0x0011u) == 1u,
            "native BT control retry allocates a fresh CID and retains count until configuration completes");

        accept_and_configure(
            memory,
            retried_control_cid,
            0x0050u,
            0x61u,
            "native BT retried control connection response is accepted",
            passed);
        passed &= expect(
            TestAccess::l2cap_retry_count(memory, 0x0011u) == 0u &&
                TestAccess::control_local_cid(memory) == retried_control_cid &&
                TestAccess::control_remote_cid(memory) == 0x0050u,
            "native BT control retry count resets only after bidirectional configuration");

        const std::uint16_t first_interrupt_cid = expect_connect_request(
            TestAccess::take_acl_event(memory),
            0x0013u,
            "native BT configured control channel advances to interrupt PSM",
            passed);
        const auto reject_interrupt =
            connection_response(
                TestAccess::l2cap_connection_signal_id(memory),
                0u,
                0u,
                0x0003u,
                0u);
        passed &= expect(
            submit_signal(memory, reject_interrupt),
            "native BT interrupt security rejection is recoverable");
        const std::uint64_t interrupt_retry_deadline =
            fake_ticks + galaxy::host::kNativeBluetoothL2capRetryDelayTicks;
        passed &= expect(
            TestAccess::interrupt_local_cid(memory) == 0u &&
                TestAccess::l2cap_retry_psm(memory) == 0x0013u &&
                TestAccess::l2cap_retry_count(memory, 0x0013u) == 1u &&
                TestAccess::l2cap_retry_deadline_ticks(memory) ==
                    interrupt_retry_deadline &&
                TestAccess::control_local_cid(memory) == retried_control_cid &&
                TestAccess::control_remote_cid(memory) == 0x0050u,
            "native BT interrupt rejection preserves the configured control channel and arms exact retry one");
        passed &= expect(
            first_interrupt_cid == 0x0042u,
            "native BT first interrupt attempt uses the next dynamic CID");
        fake_ticks = interrupt_retry_deadline;
        memory.poll_native_bt_reconnect();
        const std::uint16_t retried_interrupt_cid = expect_connect_request(
            TestAccess::take_acl_event(memory),
            0x0013u,
            "native BT interrupt retry fires exactly on its tick deadline",
            passed);
        passed &= expect(
            retried_interrupt_cid == 0x0043u,
            "native BT interrupt retry allocates a fresh dynamic CID");
        accept_and_configure(
            memory,
            retried_interrupt_cid,
            0x0051u,
            0x71u,
            "native BT retried interrupt connection response is accepted",
            passed);
        passed &= expect(
            TestAccess::interrupt_open(memory) &&
                TestAccess::interrupt_remote_cid(memory) == 0x0051u &&
                TestAccess::l2cap_retry_count(memory, 0x0013u) == 0u &&
                TestAccess::control_local_cid(memory) == retried_control_cid &&
                TestAccess::queued_acl_events(memory) == 0u,
            "native BT interrupt retry completes without disturbing the control channel");
    }

    const auto exercise_retry_limit =
        [&](bool interrupt_channel) {
            auto memory_owner =
                std::make_unique<galaxy::host::GuestAddressSpace>();
            galaxy::host::GuestAddressSpace& memory = *memory_owner;
            std::uint64_t fake_ticks =
                galaxy::host::kNativeBluetoothL2capPostAuthDelayTicks * 8u;
            memory.set_tick_source(&fake_tick_source, &fake_ticks);
            TestAccess::arm_l2cap_post_auth_open(
                memory,
                fake_ticks -
                    galaxy::host::kNativeBluetoothL2capPostAuthDelayTicks);
            const std::array<std::byte, 4> scan_enable{
                std::byte{0x1A}, std::byte{0x0C},
                std::byte{0x01}, std::byte{0x02}};
            TestAccess::queue_controller_command(memory, scan_enable);
            TestAccess::clear_hci_events(memory);
            memory.poll_native_bt_reconnect();

            std::vector<std::byte> current_request =
                TestAccess::take_acl_event(memory);
            std::uint16_t control_cid = 0u;
            std::uint16_t control_remote_cid = 0u;
            if (interrupt_channel) {
                control_cid = expect_connect_request(
                    current_request,
                    0x0011u,
                    "native BT retry-limit setup begins with control PSM",
                    passed);
                control_remote_cid = 0x0060u;
                accept_and_configure(
                    memory,
                    control_cid,
                    control_remote_cid,
                    0x80u,
                    "native BT retry-limit setup accepts control channel",
                    passed);
                current_request = TestAccess::take_acl_event(memory);
            }

            const std::uint16_t psm =
                interrupt_channel ? 0x0013u : 0x0011u;
            std::uint16_t current_local_cid = expect_connect_request(
                current_request,
                psm,
                interrupt_channel
                    ? "native BT interrupt retry-limit test has one initial request"
                    : "native BT control retry-limit test has one initial request",
                passed);

            for (std::uint8_t rejection = 0u;
                 rejection <= galaxy::host::kNativeBluetoothL2capMaxRetries;
                 ++rejection) {
                const auto refused = connection_response(
                    TestAccess::l2cap_connection_signal_id(memory),
                    0u,
                    0u,
                    0x0003u,
                    0u);
                passed &= expect(
                    submit_signal(memory, refused),
                    "native BT retry-limit refusal remains a handled protocol outcome");
                if (rejection <
                    galaxy::host::kNativeBluetoothL2capMaxRetries) {
                    const std::uint8_t expected_retry_count =
                        static_cast<std::uint8_t>(rejection + 1u);
                    const std::uint64_t expected_deadline =
                        fake_ticks +
                        galaxy::host::kNativeBluetoothL2capRetryDelayTicks;
                    passed &= expect(
                        TestAccess::connected(memory) &&
                            TestAccess::l2cap_retry_psm(memory) == psm &&
                            TestAccess::l2cap_retry_count(memory, psm) ==
                                expected_retry_count &&
                            TestAccess::l2cap_retry_deadline_ticks(memory) ==
                                expected_deadline &&
                            (!interrupt_channel ||
                             (TestAccess::control_local_cid(memory) ==
                                  control_cid &&
                              TestAccess::control_remote_cid(memory) ==
                                  control_remote_cid)),
                        "native BT finite L2CAP retry preserves exact count, deadline, and prerequisite channel");
                    fake_ticks = expected_deadline - 1u;
                    memory.poll_native_bt_reconnect();
                    passed &= expect(
                        TestAccess::queued_acl_events(memory) == 0u,
                        "native BT finite L2CAP retry does not fire early");
                    fake_ticks = expected_deadline;
                    memory.poll_native_bt_reconnect();
                    current_local_cid = expect_connect_request(
                        TestAccess::take_acl_event(memory),
                        psm,
                        "native BT finite L2CAP retry fires once at exact deadline",
                        passed);
                    passed &= expect(
                        current_local_cid >= 0x0040u &&
                            TestAccess::l2cap_retry_count(memory, psm) ==
                                expected_retry_count,
                        "native BT retry count is not reset by merely transmitting another request");
                } else {
                    passed &= expect(
                        !TestAccess::connected(memory) &&
                            TestAccess::l2cap_retry_psm(memory) == 0u &&
                            TestAccess::l2cap_retry_count(memory, psm) == 0u &&
                            TestAccess::control_local_cid(memory) == 0u &&
                            TestAccess::interrupt_local_cid(memory) == 0u &&
                            TestAccess::queued_acl_events(memory) == 0u &&
                            TestAccess::queued_hci_events(memory) == 1u,
                        "native BT retry limit tears down the ACL and clears all channel state");
                    const std::vector<std::byte> disconnect_event =
                        TestAccess::take_hci_event(memory);
                    passed &= expect(
                        disconnect_event.size() == 6u &&
                            byte_at(disconnect_event, 0u) == 0x05u &&
                            byte_at(disconnect_event, 2u) == 0x00u &&
                            byte_at(disconnect_event, 5u) == 0x13u,
                        "native BT retry-limit teardown emits exact successful remote-user disconnection");
                    const std::uint64_t reconnect_deadline =
                        fake_ticks +
                        galaxy::host::kNativeBluetoothConnectionRetryDelayTicks;
                    passed &= expect(
                        TestAccess::next_connection_attempt_ticks(memory) ==
                            reconnect_deadline,
                        "native BT retry-limit teardown arms exact HCI reconnect deadline");
                    fake_ticks = reconnect_deadline - 1u;
                    memory.poll_native_bt_reconnect();
                    passed &= expect(
                        !TestAccess::connection_requested(memory),
                        "native BT post-teardown HCI reconnect cannot fire early");
                    fake_ticks = reconnect_deadline;
                    memory.poll_native_bt_reconnect();
                    passed &= expect(
                        TestAccess::connection_requested(memory) &&
                            TestAccess::queued_hci_events(memory) == 1u,
                        "native BT post-teardown HCI reinjection fires at exact deadline");
                }
            }
        };

    exercise_retry_limit(false);
    exercise_retry_limit(true);
    return passed;
}

bool native_bt_l2cap_signaling_is_correlated_and_timed() {
    ScopedEnv enable_native_bt("GALAXY_NATIVE_BT_WIIMOTE", "1");
    ScopedEnv control_first("GALAXY_NATIVE_BT_INTERRUPT_FIRST", "0");
    using TestAccess = galaxy::host::NativeBluetoothTestAccess;

    const auto connection_response = [](
                                         std::uint8_t id,
                                         std::uint16_t remote_cid,
                                         std::uint16_t local_cid,
                                         std::uint16_t result,
                                         std::uint16_t status) {
        return std::array<std::byte, 12>{
            std::byte{0x03}, static_cast<std::byte>(id),
            std::byte{0x08}, std::byte{0x00},
            static_cast<std::byte>(remote_cid),
            static_cast<std::byte>(remote_cid >> 8),
            static_cast<std::byte>(local_cid),
            static_cast<std::byte>(local_cid >> 8),
            static_cast<std::byte>(result),
            static_cast<std::byte>(result >> 8),
            static_cast<std::byte>(status),
            static_cast<std::byte>(status >> 8)};
    };
    const auto config_response = [](
                                     std::uint8_t id,
                                     std::uint16_t local_cid,
                                     std::uint16_t result) {
        return std::array<std::byte, 10>{
            std::byte{0x05}, static_cast<std::byte>(id),
            std::byte{0x06}, std::byte{0x00},
            static_cast<std::byte>(local_cid),
            static_cast<std::byte>(local_cid >> 8),
            std::byte{0x00}, std::byte{0x00},
            static_cast<std::byte>(result),
            static_cast<std::byte>(result >> 8)};
    };
    const auto submit_signal = [](
                                   galaxy::host::GuestAddressSpace& memory,
                                   std::span<const std::byte> signal) {
        const std::vector<std::byte> packet = acl_packet(0x0001u, signal);
        return TestAccess::handle_acl_packet(memory, packet);
    };
    const auto start_control_request = [](
                                           galaxy::host::GuestAddressSpace& memory,
                                           std::uint64_t& ticks) {
        TestAccess::arm_l2cap_post_auth_open(
            memory,
            ticks - galaxy::host::kNativeBluetoothL2capPostAuthDelayTicks);
        memory.poll_native_bt_reconnect();
        return TestAccess::take_acl_event(memory);
    };
    const auto stale_id = [](std::uint8_t id) {
        return id == 0xFFu ? std::uint8_t{1u}
                           : static_cast<std::uint8_t>(id + 1u);
    };

    bool passed = true;
    {
        auto memory_owner =
            std::make_unique<galaxy::host::GuestAddressSpace>();
        galaxy::host::GuestAddressSpace& memory = *memory_owner;
        std::uint64_t ticks =
            galaxy::host::kNativeBluetoothL2capPostAuthDelayTicks * 4u;
        memory.set_tick_source(&fake_tick_source, &ticks);
        const std::vector<std::byte> request =
            start_control_request(memory, ticks);
        const std::uint64_t rtx_deadline =
            ticks + galaxy::host::kNativeBluetoothL2capRtxTicks;
        passed &= expect(
            request.size() == 16u &&
                TestAccess::l2cap_connection_psm(memory) == 0x0011u &&
                TestAccess::l2cap_connection_signal_id(memory) != 0u &&
                TestAccess::l2cap_connection_response_deadline_ticks(memory) ==
                    rtx_deadline &&
                !TestAccess::l2cap_connection_response_pending(memory),
            "native BT delivered L2CAP Connection Request owns one exact RTX timer");
        ticks = rtx_deadline - 1u;
        memory.poll_native_bt_reconnect();
        passed &= expect(
            TestAccess::l2cap_connection_signal_id(memory) != 0u &&
                TestAccess::l2cap_retry_psm(memory) == 0u,
            "native BT L2CAP connection RTX cannot expire one tick early");
        ticks = rtx_deadline;
        memory.poll_native_bt_reconnect();
        passed &= expect(
            TestAccess::l2cap_connection_signal_id(memory) == 0u &&
                TestAccess::control_local_cid(memory) == 0u &&
                TestAccess::l2cap_retry_psm(memory) == 0x0011u &&
                TestAccess::l2cap_retry_count(memory, 0x0011u) == 1u &&
                TestAccess::l2cap_retry_deadline_ticks(memory) ==
                    ticks + galaxy::host::kNativeBluetoothL2capRetryDelayTicks,
            "native BT unanswered L2CAP Connection Request recovers exactly at RTX");
    }

    {
        auto memory_owner =
            std::make_unique<galaxy::host::GuestAddressSpace>();
        galaxy::host::GuestAddressSpace& memory = *memory_owner;
        std::uint64_t ticks =
            galaxy::host::kNativeBluetoothL2capPostAuthDelayTicks * 5u;
        memory.set_tick_source(&fake_tick_source, &ticks);
        static_cast<void>(start_control_request(memory, ticks));
        const std::uint8_t id =
            TestAccess::l2cap_connection_signal_id(memory);
        const auto pending =
            connection_response(id, 0u, 0u, 0x0001u, 0x0001u);
        passed &= expect(
            submit_signal(memory, pending),
            "native BT L2CAP Connection Pending response is handled");
        const std::uint64_t ertx_deadline =
            ticks + galaxy::host::kNativeBluetoothL2capErtxTicks;
        const std::uint64_t ertx_maximum_deadline =
            ticks +
            galaxy::host::kNativeBluetoothL2capErtxMaximumTotalTicks;
        passed &= expect(
            TestAccess::l2cap_connection_response_pending(memory) &&
                TestAccess::l2cap_connection_response_deadline_ticks(memory) ==
                    ertx_deadline &&
                TestAccess::l2cap_connection_ertx_maximum_deadline_ticks(
                    memory) == ertx_maximum_deadline,
            "native BT first Connection Pending starts exact ERTX and one non-renewable ceiling");
        ticks = ertx_deadline - 1u;
        passed &= expect(
            submit_signal(memory, pending),
            "native BT repeated Connection Pending before ERTX is handled");
        const std::uint64_t renewed_ertx_deadline =
            ticks + galaxy::host::kNativeBluetoothL2capErtxTicks;
        passed &= expect(
            TestAccess::l2cap_connection_response_deadline_ticks(memory) ==
                    renewed_ertx_deadline &&
                TestAccess::l2cap_connection_ertx_maximum_deadline_ticks(
                    memory) == ertx_maximum_deadline,
            "native BT repeated Pending renews only ERTX and preserves its total ceiling");

        for (std::uint8_t renewal = 0u;
             renewal < 8u &&
             TestAccess::l2cap_connection_response_deadline_ticks(memory) <
                 ertx_maximum_deadline;
             ++renewal) {
            ticks =
                TestAccess::l2cap_connection_response_deadline_ticks(memory) -
                1u;
            passed &= expect(
                submit_signal(memory, pending),
                "native BT repeated Connection Pending before the live ERTX is handled");
        }
        ticks = ertx_maximum_deadline - 1u;
        passed &= expect(
            TestAccess::l2cap_connection_response_deadline_ticks(memory) ==
                    ertx_maximum_deadline &&
                TestAccess::l2cap_connection_ertx_maximum_deadline_ticks(
                    memory) == ertx_maximum_deadline,
            "native BT repeated Pending is clamped to the non-renewable ERTX ceiling");
        memory.poll_native_bt_reconnect();
        passed &= expect(
            TestAccess::l2cap_connection_signal_id(memory) == id &&
                TestAccess::l2cap_retry_psm(memory) == 0u,
            "native BT total ERTX ceiling cannot expire one tick early");

        ticks = ertx_maximum_deadline;
        passed &= expect(
            submit_signal(memory, pending),
            "native BT repeated Connection Pending at its total ceiling is handled without renewal");
        passed &= expect(
            TestAccess::l2cap_connection_response_deadline_ticks(memory) ==
                    ertx_maximum_deadline &&
                TestAccess::l2cap_connection_ertx_maximum_deadline_ticks(
                    memory) == ertx_maximum_deadline,
            "native BT Pending at the total ERTX ceiling cannot move either deadline");
        memory.poll_native_bt_reconnect();
        passed &= expect(
            TestAccess::l2cap_connection_signal_id(memory) == 0u &&
                TestAccess::l2cap_connection_ertx_maximum_deadline_ticks(
                    memory) == 0u &&
                TestAccess::l2cap_retry_psm(memory) == 0x0011u &&
                TestAccess::l2cap_retry_count(memory, 0x0011u) == 1u,
            "native BT repeated Connection Pending recovers exactly at the total ERTX ceiling");
    }

    {
        auto memory_owner =
            std::make_unique<galaxy::host::GuestAddressSpace>();
        galaxy::host::GuestAddressSpace& memory = *memory_owner;
        std::uint64_t ticks =
            galaxy::host::kNativeBluetoothL2capPostAuthDelayTicks * 6u;
        memory.set_tick_source(&fake_tick_source, &ticks);
        static_cast<void>(start_control_request(memory, ticks));
        const std::uint8_t connection_id =
            TestAccess::l2cap_connection_signal_id(memory);
        const auto pending_connection = connection_response(
            connection_id, 0u, 0u, 0x0001u, 0u);
        passed &= expect(
            submit_signal(memory, pending_connection),
            "native BT correlation fixture enters Connection Pending");
        const std::uint64_t connection_deadline =
            TestAccess::l2cap_connection_response_deadline_ticks(memory);
        const std::uint64_t connection_ertx_maximum_deadline =
            TestAccess::l2cap_connection_ertx_maximum_deadline_ticks(memory);
        const auto stale_connection = connection_response(
            stale_id(connection_id), 0x0050u, 0x0040u, 0u, 0u);
        passed &= expect_runtime_error(
            [&] {
                static_cast<void>(submit_signal(memory, stale_connection));
            },
            "native BT rejects a stale L2CAP Connection Response identifier");
        passed &= expect(
            TestAccess::l2cap_connection_signal_id(memory) == connection_id &&
                TestAccess::l2cap_connection_response_deadline_ticks(memory) ==
                    connection_deadline &&
                TestAccess::l2cap_connection_ertx_maximum_deadline_ticks(
                    memory) == connection_ertx_maximum_deadline &&
                TestAccess::control_remote_cid(memory) == 0u,
            "native BT stale Connection Response leaves the owned transaction intact");

        const auto accepted = connection_response(
            connection_id, 0x0050u, 0x0040u, 0u, 0u);
        passed &= expect(
            submit_signal(memory, accepted),
            "native BT accepts the exactly correlated Connection Response");
        const std::uint64_t configuration_deadline =
            ticks +
            galaxy::host::kNativeBluetoothL2capConfigurationTimeoutTicks;
        passed &= expect(
            TestAccess::l2cap_configuration_psm(memory) == 0x0011u &&
                TestAccess::l2cap_configuration_deadline_ticks(memory) ==
                    configuration_deadline &&
                TestAccess::l2cap_connection_ertx_maximum_deadline_ticks(
                    memory) == 0u &&
                TestAccess::l2cap_config_signal_id(memory) != 0u &&
                TestAccess::l2cap_config_response_deadline_ticks(memory) == 0u,
            "native BT accepted channel starts one bounded Standard Configuration process");

        const std::vector<std::byte> config_request =
            TestAccess::take_acl_event(memory);
        const std::uint8_t config_id =
            TestAccess::l2cap_config_signal_id(memory);
        const std::uint64_t config_rtx_deadline =
            ticks + galaxy::host::kNativeBluetoothL2capRtxTicks;
        passed &= expect(
            config_request.size() == 20u &&
                TestAccess::l2cap_config_response_deadline_ticks(memory) ==
                    config_rtx_deadline,
            "native BT delivered Config Request starts its exact RTX timer");
        const auto stale_config =
            config_response(stale_id(config_id), 0x0040u, 0u);
        passed &= expect_runtime_error(
            [&] { static_cast<void>(submit_signal(memory, stale_config)); },
            "native BT rejects a stale L2CAP Config Response identifier");
        passed &= expect(
            TestAccess::l2cap_config_signal_id(memory) == config_id &&
                TestAccess::l2cap_config_response_deadline_ticks(memory) ==
                    config_rtx_deadline,
            "native BT stale Config Response leaves the owned transaction intact");
        ticks = config_rtx_deadline;
        memory.poll_native_bt_reconnect();
        passed &= expect(
            TestAccess::l2cap_config_signal_id(memory) == 0u &&
                TestAccess::l2cap_configuration_psm(memory) == 0u &&
                TestAccess::control_local_cid(memory) == 0u &&
                TestAccess::l2cap_retry_psm(memory) == 0x0011u,
            "native BT unanswered Config Request recovers exactly at RTX");
    }

    {
        auto memory_owner =
            std::make_unique<galaxy::host::GuestAddressSpace>();
        galaxy::host::GuestAddressSpace& memory = *memory_owner;
        std::uint64_t ticks =
            galaxy::host::kNativeBluetoothL2capPostAuthDelayTicks * 7u;
        memory.set_tick_source(&fake_tick_source, &ticks);
        static_cast<void>(start_control_request(memory, ticks));
        const auto accepted = connection_response(
            TestAccess::l2cap_connection_signal_id(memory),
            0x0051u,
            0x0040u,
            0u,
            0u);
        static_cast<void>(submit_signal(memory, accepted));
        static_cast<void>(TestAccess::take_acl_event(memory));
        const std::uint8_t config_id =
            TestAccess::l2cap_config_signal_id(memory);
        const auto pending_config =
            config_response(config_id, 0x0040u, 0x0004u);
        passed &= expect(
            submit_signal(memory, pending_config),
            "native BT L2CAP Config Pending response is handled");
        const std::uint64_t ertx_deadline =
            ticks + galaxy::host::kNativeBluetoothL2capErtxTicks;
        passed &= expect(
            TestAccess::l2cap_config_response_pending(memory) &&
                TestAccess::l2cap_config_response_deadline_ticks(memory) ==
                    ertx_deadline,
            "native BT Config Pending replaces RTX with exact ERTX");
        ticks = ertx_deadline - 1u;
        memory.poll_native_bt_reconnect();
        passed &= expect(
            TestAccess::l2cap_config_signal_id(memory) == config_id,
            "native BT configuration ERTX cannot expire one tick early");
        ticks = ertx_deadline;
        memory.poll_native_bt_reconnect();
        passed &= expect(
            TestAccess::l2cap_config_signal_id(memory) == 0u &&
                TestAccess::l2cap_retry_psm(memory) == 0x0011u,
            "native BT Config Pending recovers exactly at ERTX");
    }

    {
        auto memory_owner =
            std::make_unique<galaxy::host::GuestAddressSpace>();
        galaxy::host::GuestAddressSpace& memory = *memory_owner;
        std::uint64_t ticks =
            galaxy::host::kNativeBluetoothL2capPostAuthDelayTicks * 8u;
        memory.set_tick_source(&fake_tick_source, &ticks);
        static_cast<void>(start_control_request(memory, ticks));
        const auto accepted = connection_response(
            TestAccess::l2cap_connection_signal_id(memory),
            0x0052u,
            0x0040u,
            0u,
            0u);
        static_cast<void>(submit_signal(memory, accepted));
        const std::uint64_t overall_deadline =
            TestAccess::l2cap_configuration_deadline_ticks(memory);
        static_cast<void>(TestAccess::take_acl_event(memory));
        const auto successful_config = config_response(
            TestAccess::l2cap_config_signal_id(memory), 0x0040u, 0u);
        static_cast<void>(submit_signal(memory, successful_config));
        passed &= expect(
            TestAccess::l2cap_config_signal_id(memory) == 0u &&
                TestAccess::l2cap_configuration_psm(memory) == 0x0011u &&
                TestAccess::l2cap_configuration_deadline_ticks(memory) ==
                    overall_deadline,
            "native BT one-sided configuration remains owned until its peer half completes");
        ticks = overall_deadline - 1u;
        memory.poll_native_bt_reconnect();
        passed &= expect(
            TestAccess::l2cap_configuration_psm(memory) == 0x0011u,
            "native BT Standard Configuration deadline cannot expire one tick early");
        ticks = overall_deadline;
        memory.poll_native_bt_reconnect();
        passed &= expect(
            TestAccess::l2cap_configuration_psm(memory) == 0u &&
                TestAccess::control_local_cid(memory) == 0u &&
                TestAccess::l2cap_retry_psm(memory) == 0x0011u,
            "native BT one-sided configuration recovers at the 120-second deadline");
    }

    {
        auto memory_owner =
            std::make_unique<galaxy::host::GuestAddressSpace>();
        galaxy::host::GuestAddressSpace& memory = *memory_owner;
        std::uint64_t ticks =
            galaxy::host::kNativeBluetoothL2capPostAuthDelayTicks * 9u;
        memory.set_tick_source(&fake_tick_source, &ticks);
        static_cast<void>(start_control_request(memory, ticks));
        const auto pending = connection_response(
            TestAccess::l2cap_connection_signal_id(memory),
            0u,
            0u,
            0x0001u,
            0u);
        static_cast<void>(submit_signal(memory, pending));
        passed &= expect(
            TestAccess::l2cap_connection_ertx_maximum_deadline_ticks(memory) !=
                0u,
            "native BT reset fixture enters a live connection ERTX transaction");
        const std::array<std::byte, 3> hci_reset{
            std::byte{0x03}, std::byte{0x0C}, std::byte{0x00}};
        TestAccess::queue_controller_command(memory, hci_reset);
        passed &= expect(
            !TestAccess::connected(memory) &&
                TestAccess::l2cap_connection_psm(memory) == 0u &&
                TestAccess::l2cap_connection_signal_id(memory) == 0u &&
                TestAccess::l2cap_connection_response_deadline_ticks(memory) ==
                    0u &&
                TestAccess::l2cap_connection_ertx_maximum_deadline_ticks(
                    memory) == 0u &&
                TestAccess::l2cap_configuration_psm(memory) == 0u &&
                TestAccess::l2cap_config_signal_id(memory) == 0u &&
                TestAccess::l2cap_configuration_deadline_ticks(memory) == 0u,
            "native BT HCI Reset retires every active L2CAP timer and identifier");
    }

    {
        auto memory_owner =
            std::make_unique<galaxy::host::GuestAddressSpace>();
        galaxy::host::GuestAddressSpace& memory = *memory_owner;
        std::uint64_t ticks =
            std::numeric_limits<std::uint64_t>::max() - 8u;
        memory.set_tick_source(&fake_tick_source, &ticks);
        TestAccess::arm_l2cap_post_auth_open(memory, 0u);
        memory.poll_native_bt_reconnect();
        passed &= expect(
            TestAccess::l2cap_connection_response_deadline_ticks(memory) == 0u,
            "native BT queued L2CAP request has no RTX deadline near overflow");
        static_cast<void>(TestAccess::take_acl_event(memory));
        passed &= expect(
            TestAccess::l2cap_connection_response_deadline_ticks(memory) ==
                std::numeric_limits<std::uint64_t>::max(),
            "native BT delivered L2CAP RTX deadline saturates instead of wrapping");
        const auto pending = connection_response(
            TestAccess::l2cap_connection_signal_id(memory),
            0u,
            0u,
            0x0001u,
            0u);
        passed &= expect(
            submit_signal(memory, pending),
            "native BT Connection Pending near tick overflow is handled");
        passed &= expect(
            TestAccess::l2cap_connection_response_deadline_ticks(memory) ==
                    std::numeric_limits<std::uint64_t>::max() &&
                TestAccess::l2cap_connection_ertx_maximum_deadline_ticks(
                    memory) == std::numeric_limits<std::uint64_t>::max(),
            "native BT ERTX and its total ceiling saturate instead of wrapping");
    }
    return passed;
}

bool native_bt_l2cap_post_auth_open_is_nonblocking() {
    ScopedEnv enable_native_bt("GALAXY_NATIVE_BT_WIIMOTE", "1");
    galaxy::host::GuestAddressSpace memory;
    using TestAccess = galaxy::host::NativeBluetoothTestAccess;
    std::uint64_t fake_ticks =
        galaxy::host::kNativeBluetoothL2capPostAuthDelayTicks * 2u;
    memory.set_tick_source(&fake_tick_source, &fake_ticks);

    TestAccess::arm_l2cap_post_auth_open(memory, fake_ticks);
    const auto before_poll = std::chrono::steady_clock::now();
    memory.poll_native_bt_reconnect();
    const auto early_poll_elapsed =
        std::chrono::steady_clock::now() - before_poll;

    bool passed = expect(
        early_poll_elapsed < std::chrono::milliseconds{100},
        "native BT post-auth L2CAP deadline never sleeps the IOS/IPC thread");
    passed &= expect(
        TestAccess::control_open_pending(memory) &&
            TestAccess::control_local_cid(memory) == 0u &&
            TestAccess::queued_acl_events(memory) == 0u,
        "native BT post-auth L2CAP deadline remains pending before it is due");

    TestAccess::arm_l2cap_post_auth_open(
        memory,
        fake_ticks -
            galaxy::host::kNativeBluetoothL2capPostAuthDelayTicks);
    memory.poll_native_bt_reconnect();
    passed &= expect(
        !TestAccess::control_open_pending(memory) &&
            TestAccess::control_local_cid(memory) == 0x0040u &&
            TestAccess::queued_acl_events(memory) == 1u,
        "native BT post-auth L2CAP deadline emits exactly one control request when due");

    memory.poll_native_bt_reconnect();
    passed &= expect(
        TestAccess::control_local_cid(memory) == 0x0040u &&
            TestAccess::queued_acl_events(memory) == 1u,
        "native BT post-auth L2CAP poll cannot duplicate the due control request");
    return passed;
}

bool native_bt_wiimote_protocol_path_works(
    bool strict_command_sequence_only = false) {
    ScopedEnv enable_native_bt("GALAXY_NATIVE_BT_WIIMOTE", "1");
    ScopedEnv disable_real_hid("GALAXY_REAL_WIIMOTE_HID", "0");
    ScopedEnv enable_input_trace("GALAXY_TRACE_BOOT_INPUT", "1");
    ScopedEnv calibrated_mouse_x("GALAXY_MOUSE_IR_ABSOLUTE_X_SCALE", "0.38");
    ScopedEnv calibrated_mouse_y("GALAXY_MOUSE_IR_ABSOLUTE_Y_SCALE", "1.0");

    galaxy::host::GuestAddressSpace memory(
        strict_command_sequence_only
            ? galaxy::input::kQualifiedTestHidTimingProfile
            : galaxy::input::kProvisionalVirtualHidTimingProfile);
    // A production report reads the active WPAD channel's dynamic extension
    // key tables. Install captured non-default tables in this host-only
    // protocol fixture so it exercises that exact integration instead of a
    // retired process-global cipher assumption.
    constexpr std::uint32_t kRmge01WpadControlBlockTable = 0x80660170u;
    constexpr std::uint32_t kRmge01WpadControlBlock = 0x806611A0u;
    constexpr std::array<std::byte, 16> rmge01_nunchuk_key_tables{
        std::byte{0x78}, std::byte{0xDD}, std::byte{0x81}, std::byte{0xBB},
        std::byte{0xEA}, std::byte{0x77}, std::byte{0x5C}, std::byte{0x06},
        std::byte{0x50}, std::byte{0x3E}, std::byte{0xD2}, std::byte{0x78},
        std::byte{0x6D}, std::byte{0x40}, std::byte{0xFD}, std::byte{0xAF}};
    constexpr std::array<std::uint8_t, 6> expected_encrypted_neutral_nunchuk{
        0x53, 0x9A, 0x2D, 0xBD, 0xFB, 0xCC};
    memory.write_u32(kRmge01WpadControlBlockTable, kRmge01WpadControlBlock);
    memory.copy(
        kRmge01WpadControlBlock + 0x924u, rmge01_nunchuk_key_tables);
    using TestAccess = galaxy::host::NativeBluetoothTestAccess;
    std::uint64_t fake_ticks = 0;
    memory.set_tick_source(&fake_tick_source, &fake_ticks);
    struct CadenceArmObservation {
        std::uint64_t notifications{};
        std::uint64_t next_unconsumed_sequence{};
        std::uint64_t next_unconsumed_deadline_ticks{};
        std::uint64_t period_ticks{};
    } cadence_arm_observation;
    memory.set_native_input_cadence_arm_callback(
        [](void* user,
           std::uint64_t next_unconsumed_sequence,
           std::uint64_t next_unconsumed_deadline_ticks,
           std::uint64_t period_ticks) {
            auto& observation =
                *static_cast<CadenceArmObservation*>(user);
            ++observation.notifications;
            observation.next_unconsumed_sequence =
                next_unconsumed_sequence;
            observation.next_unconsumed_deadline_ticks =
                next_unconsumed_deadline_ticks;
            observation.period_ticks = period_ticks;
        },
        &cadence_arm_observation);
    galaxy::GuestMemoryV1* guest_memory = memory.guest_memory();
    const std::uint32_t handle = ios_open_path(
        memory,
        guest_memory,
        0x13400000,
        0x13400100,
        "/dev/usb/oh1/57e/305");

    bool passed = expect(handle != 0, "native BT test opens Bluetooth device");

    constexpr std::uint32_t kVectors = 0x13401000;
    constexpr std::uint32_t kEndpoint = 0x13401100;
    constexpr std::uint32_t kEndpointLength = 0x13401110;
    constexpr std::uint32_t kBuffer = 0x13402000;
    constexpr std::uint32_t kRequest = 0x13403000;
    constexpr std::uint32_t kCommand = 0x13404000;
    constexpr std::uint32_t kAclCompletedEventBuffer = 0x13412000;
    constexpr std::uint64_t kTestHidReportPeriodTicks =
        galaxy::input::kNativeHidReportPeriodTicks;
    std::uint32_t next_acl_completed_event_request = 0x13410000;
    *reinterpret_cast<std::uint16_t*>(
        memory.pointer(kEndpointLength, sizeof(std::uint16_t))) = 0x2000;

    const auto set_endpoint = [&](std::uint8_t endpoint) {
        *reinterpret_cast<std::uint8_t*>(memory.pointer(kEndpoint, 1)) =
            endpoint;
    };
    const auto write_read_vectors =
        [&](std::uint8_t endpoint,
            std::uint32_t buffer,
            std::uint32_t capacity) {
            set_endpoint(endpoint);
            memory.write_u32(kVectors, kEndpoint);
            memory.write_u32(kVectors + 4, 1);
            memory.write_u32(kVectors + 8, kEndpointLength);
            memory.write_u32(kVectors + 12, 2);
            memory.write_u32(kVectors + 16, buffer);
            memory.write_u32(kVectors + 20, capacity);
        };
    const auto submit_bt_read =
        [&](std::uint32_t request,
            std::uint32_t ioctl,
            std::uint8_t endpoint,
            std::uint32_t buffer,
            std::uint32_t capacity) {
            write_read_vectors(endpoint, buffer, capacity);
            memory.write_u32(request, 7);
            memory.write_u32(request + 8, handle);
            memory.write_u32(request + 0x0C, ioctl);
            memory.write_u32(request + 0x10, 2);
            memory.write_u32(request + 0x14, 1);
            memory.write_u32(request + 0x18, kVectors);
            memory.write_u32(request + 0x20, 0x80401000);
            memory.write_u32(request + 0x24, 0x81234000);
            submit_ios_request(guest_memory, request);
        };
    const auto submit_hci = [&](std::span<const std::byte> command) {
        memory.copy(kCommand, command);
        for (std::uint32_t i = 0; i < 6; ++i) {
            memory.write_u32(kVectors + i * 8, kEndpoint);
            memory.write_u32(kVectors + i * 8 + 4, 1);
        }
        memory.write_u32(kVectors + 48, kCommand);
        memory.write_u32(
            kVectors + 52, static_cast<std::uint32_t>(command.size()));
        memory.write_u32(kRequest, 7);
        memory.write_u32(kRequest + 8, handle);
        memory.write_u32(kRequest + 0x0C, 0);
        memory.write_u32(kRequest + 0x10, 6);
        memory.write_u32(kRequest + 0x14, 1);
        memory.write_u32(kRequest + 0x18, kVectors);
        submit_ios_request(guest_memory, kRequest);
    };
    const auto submit_acl_out = [&](std::span<const std::byte> packet) {
        memory.copy(kBuffer, packet);
        write_read_vectors(
            0x02, kBuffer, static_cast<std::uint32_t>(packet.size()));
        memory.write_u32(kRequest, 7);
        memory.write_u32(kRequest + 8, handle);
        memory.write_u32(kRequest + 0x0C, 1);
        memory.write_u32(kRequest + 0x10, 2);
        memory.write_u32(kRequest + 0x14, 1);
        memory.write_u32(kRequest + 0x18, kVectors);
        submit_ios_request(guest_memory, kRequest);
    };
    const auto arm_acl_completed_packet_read = [&]() {
        const std::uint32_t request = next_acl_completed_event_request;
        next_acl_completed_event_request += 0x40u;
        submit_bt_read(request, 2, 0x81, kAclCompletedEventBuffer, 32);
        acknowledge_ios_ack(guest_memory);
        return request;
    };
    const auto expect_acl_completed_packet =
        [&](std::uint32_t request, const char* message) {
            passed &= expect(
                expect_ios_reply(
                    memory, guest_memory, request, 7, 7, message),
                message);
            const auto* event =
                reinterpret_cast<const std::uint8_t*>(
                    memory.pointer(kAclCompletedEventBuffer, 7));
            passed &= expect(
                event[0] == 0x13 && event[1] == 0x05 &&
                    event[2] == 0x01 && event[3] == 0x00 &&
                    event[4] == 0x01 && event[5] == 0x01 &&
                    event[6] == 0x00,
                message);
            acknowledge_ios_reply(guest_memory);
        };
    const auto submit_acl_out_with_completion =
        [&](std::span<const std::byte> packet, const char* message) {
            const std::uint32_t event_request =
                arm_acl_completed_packet_read();
            submit_acl_out(packet);
            passed &= expect(
                expect_ios_reply(
                    memory,
                    guest_memory,
                    kRequest,
                    static_cast<std::uint32_t>(packet.size()),
                    7,
                    message),
                message);
            acknowledge_ios_reply(guest_memory);
            expect_acl_completed_packet(
                event_request,
                "native BT HCI Number Of Completed Packets event arrives");
        };
    const auto submit_hid_output =
        [&](std::span<const std::byte> payload, const char* message) {
            const std::vector<std::byte> packet = acl_packet(0x0040, payload);
            submit_acl_out_with_completion(packet, message);
        };
    const auto read_bulk_acl =
        [&](std::uint32_t request,
            std::uint32_t expected_size,
            const char* message) {
            submit_bt_read(request, 1, 0x82, kBuffer, 64);
            // A bulk read is only the transport consumer. Drive the virtual
            // device separately so an unarmed post-init/reconnect epoch can
            // start at this first genuinely deliverable read without making
            // IOS submission itself a sampling trigger.
            memory.poll_native_bt_reconnect();
            passed &= expect(
                expect_ios_reply(
                    memory, guest_memory, request, expected_size, 7, message),
                message);
            const auto* packet =
                reinterpret_cast<const std::uint8_t*>(
                    memory.pointer(kBuffer, expected_size));
            acknowledge_ios_reply(guest_memory);
            return packet;
        };
    const auto set_input_report_mode =
        [&](std::uint8_t report_mode,
            const char* message,
            bool service_device = true) {
            const std::array<std::byte, 4> hid_set_report_mode{
                std::byte{0xA2},
                std::byte{0x12},
                std::byte{0x00},
                static_cast<std::byte>(report_mode)};
            submit_hid_output(hid_set_report_mode, message);
            // Service an already-running/deliverable device explicitly. A
            // pre-init mode write remains configuration only and cannot arm
            // the initial stream until a bulk read can accept it.
            if (service_device) {
                memory.poll_native_bt_reconnect();
            }
        };

    submit_bt_read(kRequest + 0x100, 2, 0x81, kBuffer, 32);
    acknowledge_ios_ack(guest_memory);
    const std::array<std::byte, 4> scan_enable{
        std::byte{0x1A}, std::byte{0x0C}, std::byte{1}, std::byte{0x02}};
    submit_hci(scan_enable);
    passed &= expect(
        expect_ios_reply(
            memory, guest_memory, kRequest, 0, 7,
            "native BT Write_Scan_Enable control completes"),
        "native BT Write_Scan_Enable control completes");
    acknowledge_ios_reply(guest_memory);
    passed &= expect(
        expect_ios_reply(
            memory, guest_memory, kRequest + 0x100, 6, 7,
            "native BT scan-enable command-complete arrives"),
        "native BT scan-enable command-complete arrives");
    acknowledge_ios_reply(guest_memory);

    submit_bt_read(kRequest + 0x200, 2, 0x81, kBuffer, 32);
    passed &= expect(
        expect_ios_reply(
            memory, guest_memory, kRequest + 0x200, 12, 7,
            "native BT connection request arrives"),
        "native BT connection request arrives");
    const auto* connection_request =
        reinterpret_cast<const std::uint8_t*>(memory.pointer(kBuffer, 12));
    constexpr std::array<std::uint8_t, 12> expected_connection_request{
        0x04, 0x0A, 0x11, 0x02, 0x19, 0x79,
        0x00, 0x00, 0x04, 0x25, 0x00, 0x01};
    passed &= expect_bytes(
        std::span<const std::uint8_t>(connection_request, 12),
        expected_connection_request,
        "native BT connection request packet");
    acknowledge_ios_reply(guest_memory);

    submit_bt_read(kRequest + 0x300, 2, 0x81, kBuffer, 32);
    acknowledge_ios_ack(guest_memory);
    const std::array<std::byte, 10> accept_connection{
        std::byte{0x09}, std::byte{0x04}, std::byte{7},
        std::byte{0x11}, std::byte{0x02}, std::byte{0x19},
        std::byte{0x79}, std::byte{0x00}, std::byte{0x00},
        std::byte{0x00}};
    submit_hci(accept_connection);
    passed &= expect(
        expect_ios_reply(
            memory, guest_memory, kRequest + 0x300, 6, 7,
            "native BT accept command-status arrives"),
        "native BT accept command-status arrives");
    acknowledge_ios_reply(guest_memory);
    passed &= expect(
        expect_ios_reply(
            memory, guest_memory, kRequest, 0, 7,
            "native BT accept control transfer completes"),
        "native BT accept control transfer completes");
    acknowledge_ios_reply(guest_memory);

    submit_bt_read(kRequest + 0x380, 2, 0x81, kBuffer, 32);
    passed &= expect(
        expect_ios_reply(
            memory, guest_memory, kRequest + 0x380, 10, 7,
            "native BT role-change arrives"),
        "native BT role-change arrives");
    const auto* role_change =
        reinterpret_cast<const std::uint8_t*>(memory.pointer(kBuffer, 10));
    passed &= expect(
        role_change[0] == 0x12 && role_change[1] == 0x08 &&
            role_change[2] == 0x00 &&
            std::equal(
                expected_connection_request.begin() + 2,
                expected_connection_request.begin() + 8,
                role_change + 3) &&
            role_change[9] == 0x00,
        "native BT role-change marks Wii as master for accepted connection");
    acknowledge_ios_reply(guest_memory);

    submit_bt_read(kRequest + 0x400, 2, 0x81, kBuffer, 32);
    passed &= expect(
        expect_ios_reply(
            memory, guest_memory, kRequest + 0x400, 13, 7,
            "native BT connection-complete arrives"),
        "native BT connection-complete arrives");
    const auto* connection_complete =
        reinterpret_cast<const std::uint8_t*>(memory.pointer(kBuffer, 13));
    passed &= expect(
        connection_complete[0] == 0x03 && connection_complete[1] == 0x0B &&
            connection_complete[2] == 0 &&
            connection_complete[3] == 0x00 &&
            connection_complete[4] == 0x01,
        "native BT connection-complete has expected handle");
    acknowledge_ios_reply(guest_memory);

    submit_bt_read(kRequest + 0x408, 2, 0x81, kBuffer, 32);
    passed &= expect(
        expect_ios_reply(
            memory, guest_memory, kRequest + 0x408, 8, 7,
            "native BT link-key request arrives"),
        "native BT link-key request arrives");
    const auto* link_key_request =
        reinterpret_cast<const std::uint8_t*>(memory.pointer(kBuffer, 8));
    passed &= expect(
        link_key_request[0] == 0x17 && link_key_request[1] == 0x06 &&
            std::equal(
                expected_connection_request.begin() + 2,
                expected_connection_request.begin() + 8,
                link_key_request + 2),
        "native BT link-key request targets the connected remote");
    acknowledge_ios_reply(guest_memory);

    const std::array<std::byte, 3> hid_status_request{
        std::byte{0xA2}, std::byte{0x15}, std::byte{0x00}};

    const std::array<std::byte, 8> premature_l2cap_config{
        std::byte{0x04}, std::byte{0x31}, std::byte{0x04}, std::byte{0x00},
        std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00}};
    passed &= expect_runtime_error(
        [&] {
            const std::vector<std::byte> packet =
                acl_packet(0x0001, premature_l2cap_config);
            submit_acl_out(packet);
        },
        "native BT L2CAP config before channel assignment must hard-fail");

    passed &= expect_runtime_error(
        [&] {
            const std::vector<std::byte> packet =
                acl_packet(0x0000, hid_status_request);
            submit_acl_out(packet);
        },
        "native BT HID output before control channel assignment must hard-fail");

    submit_bt_read(kRequest + 0x4D0, 2, 0x81, kBuffer, 32);
    acknowledge_ios_ack(guest_memory);
    std::array<std::byte, 25> link_key_reply{
        std::byte{0x0B}, std::byte{0x04}, std::byte{0x16},
        std::byte{0x11}, std::byte{0x02}, std::byte{0x19},
        std::byte{0x79}, std::byte{0x00}, std::byte{0x00}};
    for (std::size_t i = 9; i < link_key_reply.size(); ++i) {
        link_key_reply[i] = std::byte{0xA0};
    }
    submit_hci(link_key_reply);
    passed &= expect(
        expect_ios_reply(
            memory,
            guest_memory,
            kRequest + 0x4D0,
            12,
            7,
            "native BT link-key reply command-complete arrives"),
        "native BT link-key reply command-complete arrives");
    const auto* link_key_complete =
        reinterpret_cast<const std::uint8_t*>(memory.pointer(kBuffer, 12));
    passed &= expect(
        link_key_complete[0] == 0x0E && link_key_complete[1] == 0x0A &&
            link_key_complete[2] == 0x01 &&
            link_key_complete[3] == 0x0B &&
            link_key_complete[4] == 0x04 &&
            link_key_complete[5] == 0x00 &&
            std::equal(
                expected_connection_request.begin() + 2,
                expected_connection_request.begin() + 8,
                link_key_complete + 6),
        "native BT link-key reply completion returns the remote address");
    acknowledge_ios_reply(guest_memory);
    passed &= expect(
        expect_ios_reply(
            memory,
            guest_memory,
            kRequest,
            0,
            7,
            "native BT link-key reply control transfer completes"),
        "native BT link-key reply control transfer completes");
    acknowledge_ios_reply(guest_memory);
    submit_bt_read(kRequest + 0x4E0, 2, 0x81, kBuffer, 32);
    passed &= expect(
        expect_ios_reply(
            memory,
            guest_memory,
            kRequest + 0x4E0,
            5,
            7,
            "native BT authentication-complete event arrives"),
        "native BT authentication-complete event arrives");
    const auto* auth_complete =
        reinterpret_cast<const std::uint8_t*>(memory.pointer(kBuffer, 5));
    passed &= expect(
        auth_complete[0] == 0x06 && auth_complete[1] == 0x03 &&
            auth_complete[2] == 0x00 &&
            auth_complete[3] == 0x00 &&
            auth_complete[4] == 0x01,
        "native BT authentication-complete reports the active handle");
    acknowledge_ios_reply(guest_memory);

    submit_bt_read(kRequest + 0x4F0, 2, 0x81, kBuffer, 32);
    passed &= expect(
        expect_ios_reply(
            memory,
            guest_memory,
            kRequest + 0x4F0,
            6,
            7,
            "native BT encryption-change event arrives"),
        "native BT encryption-change event arrives");
    const auto* encryption_change =
        reinterpret_cast<const std::uint8_t*>(memory.pointer(kBuffer, 6));
    passed &= expect(
        encryption_change[0] == 0x08 && encryption_change[1] == 0x04 &&
            encryption_change[2] == 0x00 &&
            encryption_change[3] == 0x00 &&
            encryption_change[4] == 0x01 &&
            encryption_change[5] == 0x01,
        "native BT encryption-change enables the active ACL link");
    acknowledge_ios_reply(guest_memory);

    const auto expect_hci_status_control_and_event =
        [&](std::span<const std::byte> command,
            std::uint32_t status_request,
            std::uint32_t event_request,
            std::uint32_t event_capacity,
            std::uint32_t event_size,
            const char* status_message,
            const char* control_message,
            const char* event_message) {
            submit_bt_read(status_request, 2, 0x81, kBuffer, 32);
            acknowledge_ios_ack(guest_memory);
            submit_hci(command);
            passed &= expect(
                expect_ios_reply(
                    memory,
                    guest_memory,
                    status_request,
                    6,
                    7,
                    status_message),
                status_message);
            const auto* status_event =
                reinterpret_cast<const std::uint8_t*>(
                    memory.pointer(kBuffer, 6));
            passed &= expect(
                status_event[0] == 0x0F && status_event[1] == 0x04 &&
                    status_event[2] == 0x00 &&
                    status_event[4] ==
                        static_cast<std::uint8_t>(command[0]) &&
                    status_event[5] ==
                        static_cast<std::uint8_t>(command[1]),
                status_message);
            acknowledge_ios_reply(guest_memory);
            passed &= expect(
                expect_ios_reply(
                    memory,
                    guest_memory,
                    kRequest,
                    0,
                    7,
                    control_message),
                control_message);
            acknowledge_ios_reply(guest_memory);
            submit_bt_read(event_request, 2, 0x81, kBuffer, event_capacity);
            passed &= expect(
                expect_ios_reply(
                    memory,
                    guest_memory,
                    event_request,
                    event_size,
                    7,
                    event_message),
                event_message);
            const auto* event =
                reinterpret_cast<const std::uint8_t*>(
                    memory.pointer(kBuffer, event_size));
            acknowledge_ios_reply(guest_memory);
            return event;
        };

    const std::array<std::byte, 13> remote_name_request{
        std::byte{0x19}, std::byte{0x04}, std::byte{0x0A},
        std::byte{0x11}, std::byte{0x02}, std::byte{0x19},
        std::byte{0x79}, std::byte{0x00}, std::byte{0x00},
        std::byte{0x01}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00}};
    const auto* name_event =
        expect_hci_status_control_and_event(
            remote_name_request,
            kRequest + 0x410,
            kRequest + 0x420,
            300,
            257,
            "native BT remote-name command-status arrives",
            "native BT remote-name control transfer completes",
            "native BT remote-name completion arrives");
    constexpr char kExpectedRemoteName[] = "Nintendo RVL-CNT-01";
    passed &= expect(
        name_event[0] == 0x07 && name_event[1] == 0xFF &&
            name_event[2] == 0x00 &&
            std::equal(
                kExpectedRemoteName,
                kExpectedRemoteName + sizeof(kExpectedRemoteName) - 1u,
                reinterpret_cast<const char*>(name_event + 9)),
        "native BT remote-name completion identifies RVL-CNT-01");

    const std::array<std::byte, 5> read_remote_features{
        std::byte{0x1B}, std::byte{0x04}, std::byte{0x02},
        std::byte{0x00}, std::byte{0x01}};
    const auto* features_event =
        expect_hci_status_control_and_event(
            read_remote_features,
            kRequest + 0x430,
            kRequest + 0x440,
            32,
            13,
            "native BT remote-features command-status arrives",
            "native BT remote-features control transfer completes",
            "native BT remote-features completion arrives");
    passed &= expect(
        features_event[0] == 0x0B && features_event[1] == 11 &&
            features_event[2] == 0x00 && features_event[3] == 0x00 &&
            features_event[4] == 0x01 && features_event[5] == 0xBC &&
            features_event[6] == 0x02 && features_event[7] == 0x04 &&
            features_event[8] == 0x38 && features_event[9] == 0x08 &&
            features_event[10] == 0x00 && features_event[11] == 0x00 &&
            features_event[12] == 0x00,
        "native BT remote-features completion has RVL-CNT-01 features");

    const std::array<std::byte, 5> read_remote_version{
        std::byte{0x1D}, std::byte{0x04}, std::byte{0x02},
        std::byte{0x00}, std::byte{0x01}};
    const auto* version_event =
        expect_hci_status_control_and_event(
            read_remote_version,
            kRequest + 0x450,
            kRequest + 0x460,
            32,
            10,
            "native BT remote-version command-status arrives",
            "native BT remote-version control transfer completes",
            "native BT remote-version completion arrives");
    passed &= expect(
        version_event[0] == 0x0C && version_event[1] == 8 &&
            version_event[2] == 0x00 && version_event[3] == 0x00 &&
            version_event[4] == 0x01 && version_event[5] == 0x02 &&
            version_event[6] == 0x0F && version_event[7] == 0x00 &&
            version_event[8] == 0x29 && version_event[9] == 0x02,
        "native BT remote-version completion has expected LMP fields");

    const std::array<std::byte, 5> read_clock_offset{
        std::byte{0x1F}, std::byte{0x04}, std::byte{0x02},
        std::byte{0x00}, std::byte{0x01}};
    const auto* clock_offset_event =
        expect_hci_status_control_and_event(
            read_clock_offset,
            kRequest + 0x470,
            kRequest + 0x480,
            32,
            7,
            "native BT read-clock-offset command-status arrives",
            "native BT read-clock-offset control transfer completes",
            "native BT read-clock-offset completion arrives");
    passed &= expect(
        clock_offset_event[0] == 0x1C && clock_offset_event[1] == 5 &&
            clock_offset_event[2] == 0x00 &&
            clock_offset_event[3] == 0x00 &&
            clock_offset_event[4] == 0x01 &&
            clock_offset_event[5] == 0xA5 &&
            clock_offset_event[6] == 0x8A,
        "native BT read-clock-offset completion has expected handle and offset");

    const std::array<std::byte, 7> change_packet_type{
        std::byte{0x0F}, std::byte{0x04}, std::byte{0x04},
        std::byte{0x00}, std::byte{0x01}, std::byte{0x18},
        std::byte{0xCC}};
    const auto* packet_type_event =
        expect_hci_status_control_and_event(
            change_packet_type,
            kRequest + 0x490,
            kRequest + 0x4A0,
            32,
            7,
            "native BT packet-type command-status arrives",
            "native BT packet-type control transfer completes",
            "native BT packet-type changed event arrives");
    passed &= expect(
        packet_type_event[0] == 0x1D && packet_type_event[1] == 5 &&
            packet_type_event[2] == 0x00 &&
            packet_type_event[3] == 0x00 &&
            packet_type_event[4] == 0x01 &&
            packet_type_event[5] == 0x18 &&
            packet_type_event[6] == 0xCC,
        "native BT packet-type changed event echoes requested mask");

    const std::array<std::byte, 13> sniff_mode{
        std::byte{0x03}, std::byte{0x08}, std::byte{0x0A},
        std::byte{0x00}, std::byte{0x01}, std::byte{0x08},
        std::byte{0x00}, std::byte{0x08}, std::byte{0x00},
        std::byte{0x01}, std::byte{0x00}, std::byte{0x00},
        std::byte{0x00}};
    const auto* sniff_mode_event =
        expect_hci_status_control_and_event(
            sniff_mode,
            kRequest + 0x700,
            kRequest + 0x740,
            32,
            8,
            "native BT sniff-mode command-status arrives",
            "native BT sniff-mode control transfer completes",
            "native BT sniff-mode change event arrives");
    passed &= expect(
        sniff_mode_event[0] == 0x14 && sniff_mode_event[1] == 6 &&
            sniff_mode_event[2] == 0x00 &&
            sniff_mode_event[3] == 0x00 &&
            sniff_mode_event[4] == 0x01 &&
            sniff_mode_event[5] == 0x02 &&
            sniff_mode_event[6] == 0x08 &&
            sniff_mode_event[7] == 0x00,
        "native BT sniff-mode change event enters sniff at requested interval");

    const std::array<std::byte, 5> exit_sniff_mode{
        std::byte{0x04}, std::byte{0x08}, std::byte{0x02},
        std::byte{0x00}, std::byte{0x01}};
    const auto* exit_sniff_event =
        expect_hci_status_control_and_event(
            exit_sniff_mode,
            kRequest + 0x780,
            kRequest + 0x7C0,
            32,
            8,
            "native BT exit-sniff command-status arrives",
            "native BT exit-sniff control transfer completes",
            "native BT exit-sniff mode-change event arrives");
    passed &= expect(
        exit_sniff_event[0] == 0x14 && exit_sniff_event[1] == 6 &&
            exit_sniff_event[2] == 0x00 &&
            exit_sniff_event[3] == 0x00 &&
            exit_sniff_event[4] == 0x01 &&
            exit_sniff_event[5] == 0x00 &&
            exit_sniff_event[6] == 0x00 &&
            exit_sniff_event[7] == 0x00,
        "native BT exit-sniff mode-change event returns to active mode");

    submit_bt_read(kRequest + 0x4B0, 2, 0x81, kBuffer, 32);
    acknowledge_ios_ack(guest_memory);
    const std::array<std::byte, 7> write_link_policy{
        std::byte{0x0D}, std::byte{0x08}, std::byte{0x04},
        std::byte{0x00}, std::byte{0x01}, std::byte{0x05},
        std::byte{0x00}};
    submit_hci(write_link_policy);
    passed &= expect(
        expect_ios_reply(
            memory,
            guest_memory,
            kRequest + 0x4B0,
            8,
            7,
            "native BT write-link-policy command-complete arrives"),
        "native BT write-link-policy command-complete arrives");
    const auto* link_policy_event =
        reinterpret_cast<const std::uint8_t*>(memory.pointer(kBuffer, 8));
    passed &= expect(
        link_policy_event[0] == 0x0E && link_policy_event[1] == 6 &&
            link_policy_event[2] == 0x01 &&
            link_policy_event[3] == 0x0D &&
            link_policy_event[4] == 0x08 &&
            link_policy_event[5] == 0x00 &&
            link_policy_event[6] == 0x00 &&
            link_policy_event[7] == 0x01,
        "native BT write-link-policy command-complete returns active handle");
    acknowledge_ios_reply(guest_memory);
    passed &= expect(
        expect_ios_reply(
            memory,
            guest_memory,
            kRequest,
            0,
            7,
            "native BT write-link-policy control transfer completes"),
        "native BT write-link-policy control transfer completes");
    acknowledge_ios_reply(guest_memory);

    submit_bt_read(kRequest + 0x4C0, 2, 0x81, kBuffer, 32);
    acknowledge_ios_ack(guest_memory);
    const std::array<std::byte, 7> write_supervision_timeout{
        std::byte{0x37}, std::byte{0x0C}, std::byte{0x04},
        std::byte{0x00}, std::byte{0x01}, std::byte{0x80},
        std::byte{0x0C}};
    submit_hci(write_supervision_timeout);
    passed &= expect(
        expect_ios_reply(
            memory,
            guest_memory,
            kRequest + 0x4C0,
            8,
            7,
            "native BT supervision-timeout command-complete arrives"),
        "native BT supervision-timeout command-complete arrives");
    const auto* supervision_event =
        reinterpret_cast<const std::uint8_t*>(memory.pointer(kBuffer, 8));
    passed &= expect(
        supervision_event[0] == 0x0E && supervision_event[1] == 6 &&
            supervision_event[2] == 0x01 &&
            supervision_event[3] == 0x37 &&
            supervision_event[4] == 0x0C &&
            supervision_event[5] == 0x00 &&
            supervision_event[6] == 0x00 &&
            supervision_event[7] == 0x01,
        "native BT supervision-timeout command-complete returns active handle");
    acknowledge_ios_reply(guest_memory);
    passed &= expect(
        expect_ios_reply(
            memory,
            guest_memory,
            kRequest,
            0,
            7,
            "native BT supervision-timeout control transfer completes"),
        "native BT supervision-timeout control transfer completes");
    acknowledge_ios_reply(guest_memory);

    const auto submit_l2cap_signal =
        [&](std::span<const std::byte> payload, const char* message) {
            const std::vector<std::byte> packet = acl_packet(0x0001, payload);
            submit_acl_out_with_completion(packet, message);
        };

    // Deadline behavior is covered independently above. Keep this broad wire
    // protocol fixture on its historical zero-based HID script epoch.
    galaxy::host::NativeBluetoothTestAccess::
        start_l2cap_control_request_for_protocol_fixture(memory);
    const auto* control_conn_req =
        read_bulk_acl(
            kRequest + 0x500,
            16,
            "native BT remote L2CAP control connection request arrives");
    constexpr std::array<std::uint8_t, 16> expected_control_conn_req{
        0x00, 0x21, 0x0C, 0x00, 0x08, 0x00, 0x01, 0x00,
        0x02, 0x02, 0x04, 0x00, 0x11, 0x00, 0x40, 0x00};
    passed &= expect_bytes(
        std::span<const std::uint8_t>(control_conn_req, 16),
        expected_control_conn_req,
        "native BT remote opens HID control PSM");

    const std::array<std::byte, 12> control_conn_rsp{
        std::byte{0x03}, std::byte{0x02}, std::byte{0x08}, std::byte{0x00},
        std::byte{0x41}, std::byte{0x00}, std::byte{0x40}, std::byte{0x00},
        std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00}};
    submit_l2cap_signal(
        control_conn_rsp,
        "native BT guest accepts remote control L2CAP connection");

    const auto* control_config_req =
        read_bulk_acl(
            kRequest + 0x540,
            20,
            "native BT remote L2CAP control config request arrives");
    constexpr std::array<std::uint8_t, 20> expected_control_config_req{
        0x00, 0x21, 0x10, 0x00, 0x0C, 0x00, 0x01, 0x00,
        0x04, 0x03, 0x08, 0x00, 0x41, 0x00, 0x00, 0x00,
        0x01, 0x02, 0xB9, 0x00};
    passed &= expect_bytes(
        std::span<const std::uint8_t>(control_config_req, 20),
        expected_control_config_req,
        "native BT remote requests HID control MTU");

    const std::array<std::byte, 8> control_config_req_from_guest{
        std::byte{0x04}, std::byte{0x41}, std::byte{0x04}, std::byte{0x00},
        std::byte{0x40}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00}};
    submit_l2cap_signal(
        control_config_req_from_guest,
        "native BT guest configures remote control L2CAP channel");
    const auto* control_config_rsp =
        read_bulk_acl(
            kRequest + 0x580,
            18,
            "native BT remote L2CAP control config response arrives");
    passed &= expect(
        control_config_rsp[8] == 0x05 && control_config_rsp[9] == 0x41 &&
            control_config_rsp[12] == 0x41,
        "native BT remote control config response targets guest CID");

    const std::array<std::byte, 10> control_config_rsp_from_guest{
        std::byte{0x05}, std::byte{0x03}, std::byte{0x06}, std::byte{0x00},
        std::byte{0x40}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00},
        std::byte{0x00}, std::byte{0x00}};
    submit_l2cap_signal(
        control_config_rsp_from_guest,
        "native BT guest accepts remote control L2CAP config");

    passed &= expect_runtime_error(
        [&] {
            const std::vector<std::byte> packet =
                acl_packet(0x0040, hid_status_request);
            submit_acl_out(packet);
        },
        "native BT HID output before interrupt channel open must hard-fail");

    const auto* interrupt_conn_req =
        read_bulk_acl(
            kRequest + 0x600,
            16,
            "native BT remote L2CAP interrupt connection request arrives");
    constexpr std::array<std::uint8_t, 16> expected_interrupt_conn_req{
        0x00, 0x21, 0x0C, 0x00, 0x08, 0x00, 0x01, 0x00,
        0x02, 0x04, 0x04, 0x00, 0x13, 0x00, 0x41, 0x00};
    passed &= expect_bytes(
        std::span<const std::uint8_t>(interrupt_conn_req, 16),
        expected_interrupt_conn_req,
        "native BT remote opens HID interrupt PSM");

    const std::array<std::byte, 12> interrupt_conn_rsp{
        std::byte{0x03}, std::byte{0x04}, std::byte{0x08}, std::byte{0x00},
        std::byte{0x43}, std::byte{0x00}, std::byte{0x41}, std::byte{0x00},
        std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00}};
    submit_l2cap_signal(
        interrupt_conn_rsp,
        "native BT guest accepts remote interrupt L2CAP connection");

    const auto* interrupt_config_req =
        read_bulk_acl(
            kRequest + 0x640,
            20,
            "native BT remote L2CAP interrupt config request arrives");
    passed &= expect(
        interrupt_config_req[8] == 0x04 && interrupt_config_req[9] == 0x05 &&
            interrupt_config_req[12] == 0x43 &&
            interrupt_config_req[16] == 0x01 &&
            interrupt_config_req[18] == 0xB9,
        "native BT remote requests HID interrupt MTU");

    const std::array<std::byte, 8> interrupt_config_req_from_guest{
        std::byte{0x04}, std::byte{0x42}, std::byte{0x04}, std::byte{0x00},
        std::byte{0x41}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00}};
    submit_l2cap_signal(
        interrupt_config_req_from_guest,
        "native BT guest configures remote interrupt L2CAP channel");
    const auto* interrupt_config_rsp =
        read_bulk_acl(
            kRequest + 0x680,
            18,
            "native BT remote L2CAP interrupt config response arrives");
    passed &= expect(
        interrupt_config_rsp[8] == 0x05 && interrupt_config_rsp[9] == 0x42 &&
            interrupt_config_rsp[12] == 0x43,
        "native BT remote interrupt config response targets guest CID");

    const std::array<std::byte, 10> interrupt_config_rsp_from_guest{
        std::byte{0x05}, std::byte{0x05}, std::byte{0x06}, std::byte{0x00},
        std::byte{0x41}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00},
        std::byte{0x00}, std::byte{0x00}};
    submit_l2cap_signal(
        interrupt_config_rsp_from_guest,
        "native BT guest accepts remote interrupt L2CAP config");

    set_input_report_mode(
        0x37,
        "native BT HID set-report-mode output is consumed");

    submit_hid_output(
        hid_status_request,
        "native BT HID status request output is consumed");
    const auto* status_acl =
        read_bulk_acl(
            kRequest + 0x800,
            16,
            "native BT HID status report arrives on bulk ACL read");
    constexpr std::array<std::uint8_t, 16> expected_status_acl{
        0x00, 0x21, 0x0C, 0x00, 0x08, 0x00, 0x43, 0x00,
        0xA1, 0x20, 0x00, 0x00, 0x12, 0x00, 0x00, 0xFF};
    passed &= expect_bytes(
        std::span<const std::uint8_t>(status_acl, 16),
        expected_status_acl,
        "native BT status report advertises extension and full battery");

    const std::array<std::byte, 4> unsupported_report_mode{
        std::byte{0xA2}, std::byte{0x12}, std::byte{0x00}, std::byte{0x3E}};
    passed &= expect_runtime_error(
        [&] {
            const std::vector<std::byte> packet =
                acl_packet(0x0040, unsupported_report_mode);
            submit_acl_out(packet);
        },
        "native BT unsupported input report mode must hard-fail");

    const std::array<std::byte, 8> eeprom_calibration_read{
        std::byte{0xA2}, std::byte{0x17}, std::byte{0x00}, std::byte{0x00},
        std::byte{0x00}, std::byte{0x0B}, std::byte{0x00}, std::byte{0x0B}};
    submit_hid_output(
        eeprom_calibration_read,
        "native BT HID EEPROM calibration read is consumed");
    const auto* eeprom_acl =
        read_bulk_acl(
            kRequest + 0x840,
            31,
            "native BT HID EEPROM calibration report arrives");
    constexpr std::array<std::uint8_t, 11> expected_ir_calibration{
        0x7F, 0xA2, 0x8B, 0x80, 0xA2, 0x80,
        0x5D, 0x30, 0x7F, 0x5D, 0x0C};
    passed &= expect(
        eeprom_acl[8] == 0xA1 && eeprom_acl[9] == 0x21 &&
            eeprom_acl[12] == 0xA0 &&
            eeprom_acl[13] == 0x00 && eeprom_acl[14] == 0x0B,
        "native BT EEPROM calibration read-memory header is Wii-shaped");
    passed &= expect_bytes(
        std::span<const std::uint8_t>(eeprom_acl + 15, 11),
        expected_ir_calibration,
        "native BT EEPROM calibration bytes are stable");

    // The real RVL-CNT-01 only exposes EEPROM addresses below 0x1700; a read at
    // or past the end of the readable region answers with a read error
    // (size/error byte 0xF8), not zero-filled data. RMGE01's WPAD probes
    // EEPROM 0x1770 right
    // after the HID interrupt channel opens and disconnects (reason 0x13) unless
    // it sees that silicon error. (Captured ground truth:
    // docs/WIIMOTE_HARDWARE_CAPTURE.md.)
    const std::array<std::byte, 8> eeprom_firmware_probe_read{
        std::byte{0xA2}, std::byte{0x17}, std::byte{0x00}, std::byte{0x00},
        std::byte{0x17}, std::byte{0x70}, std::byte{0x00}, std::byte{0x01}};
    submit_hid_output(
        eeprom_firmware_probe_read,
        "native BT HID EEPROM firmware-area probe read is consumed");
    const auto* eeprom_probe_acl =
        read_bulk_acl(
            kRequest + 0x820,
            31,
            "native BT HID EEPROM firmware-area probe report arrives");
    passed &= expect(
        eeprom_probe_acl[8] == 0xA1 && eeprom_probe_acl[9] == 0x21 &&
            eeprom_probe_acl[12] == 0xF8 &&
            eeprom_probe_acl[13] == 0x17 && eeprom_probe_acl[14] == 0x70,
        "native BT EEPROM 0x1770 probe returns silicon read error (0xF8)");

    const std::array<std::byte, 4> pre_init_set_report_mode{
        std::byte{0xA2}, std::byte{0x12}, std::byte{0x06}, std::byte{0x30}};
    submit_hid_output(
        pre_init_set_report_mode,
        "native BT HID pre-init set-report-mode output is consumed");
    const auto* pre_init_report_mode_ack =
        read_bulk_acl(
            kRequest + 0x8230,
            14,
            "native BT pre-init set-report-mode ACK arrives");
    passed &= expect(
        pre_init_report_mode_ack[8] == 0xA1 &&
            pre_init_report_mode_ack[9] == 0x22 &&
            pre_init_report_mode_ack[12] == 0x12 &&
            pre_init_report_mode_ack[13] == 0x00,
        "native BT pre-init set-report-mode ACK identifies report 0x12");
    submit_bt_read(kRequest + 0x8240, 1, 0x82, kBuffer, 64);
    passed &= expect(
        !ios_reply_available(guest_memory),
        "native BT virtual input waits until RMGE01 init reads complete");
    const galaxy::input::NativeHidCadenceStats pre_init_cadence =
        memory.native_hid_cadence_stats();
    fake_ticks += kTestHidReportPeriodTicks * 4u;
    memory.poll_native_bt_reconnect();
    memory.poll_native_bt_reconnect();
    const galaxy::input::NativeHidCadenceStats after_pre_init_wait =
        memory.native_hid_cadence_stats();
    passed &= expect(
        !after_pre_init_wait.armed &&
            after_pre_init_wait.scheduled_samples ==
                pre_init_cadence.scheduled_samples &&
            after_pre_init_wait.produced_samples ==
                pre_init_cadence.produced_samples &&
            after_pre_init_wait.queue_replacements ==
                pre_init_cadence.queue_replacements &&
            after_pre_init_wait.delivery_gaps ==
                pre_init_cadence.delivery_gaps &&
            !TestAccess::virtual_input_queued(memory) &&
            !ios_reply_available(guest_memory) &&
            cadence_arm_observation.notifications == 0u,
        "native BT startup wait cannot arm, sample, replace, or gap the virtual HID stream before RMGE01 init completes");

    // The current RMGE01 startup path finishes the Wii Remote EEPROM burst with
    // a 0x0000/0x002A read. Virtual input must stay gated until all of those
    // command-response chunks have drained, otherwise spontaneous input can
    // consume the guest BTE event pool before WPAD finishes setup.
    const std::array<std::byte, 8> rmge01_final_init_read{
        std::byte{0xA2}, std::byte{0x17}, std::byte{0x00}, std::byte{0x00},
        std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0x2A}};
    const std::vector<std::byte> final_init_packet =
        acl_packet(0x0040, rmge01_final_init_read);
    const std::uint32_t final_init_completed_event =
        arm_acl_completed_packet_read();
    submit_acl_out(final_init_packet);
    passed &= expect(
        expect_ios_reply(
            memory,
            guest_memory,
            kRequest + 0x8240,
            31,
            7,
            "native BT RMGE01 final init read reply completes pending read"),
        "native BT RMGE01 final init read reply completes pending read");
    const auto* final_init_chunk0 =
        reinterpret_cast<const std::uint8_t*>(memory.pointer(kBuffer, 31));
    passed &= expect(
        final_init_chunk0[8] == 0xA1 && final_init_chunk0[9] == 0x21 &&
            final_init_chunk0[12] == 0xF0 &&
            final_init_chunk0[13] == 0x00 &&
            final_init_chunk0[14] == 0x00,
        "native BT RMGE01 final init read returns EEPROM chunk 0");
    acknowledge_ios_reply(guest_memory);
    passed &= expect(
        expect_ios_reply(
            memory,
            guest_memory,
            kRequest,
            static_cast<std::uint32_t>(final_init_packet.size()),
            7,
            "native BT HID RMGE01 final init EEPROM read is consumed"),
        "native BT HID RMGE01 final init EEPROM read is consumed");
    acknowledge_ios_reply(guest_memory);
    expect_acl_completed_packet(
        final_init_completed_event,
        "native BT RMGE01 final init read frees a controller ACL buffer");
    const auto* final_init_chunk1 =
        read_bulk_acl(
            kRequest + 0x8248,
            31,
            "native BT RMGE01 final init EEPROM chunk 1 arrives");
    passed &= expect(
        final_init_chunk1[8] == 0xA1 && final_init_chunk1[9] == 0x21 &&
            final_init_chunk1[12] == 0xF0 &&
            final_init_chunk1[13] == 0x00 &&
            final_init_chunk1[14] == 0x10,
        "native BT RMGE01 final init read returns EEPROM chunk 1");
    const auto* final_init_chunk2 =
        read_bulk_acl(
            kRequest + 0x8250,
            31,
            "native BT RMGE01 final init EEPROM chunk 2 arrives");
    passed &= expect(
        final_init_chunk2[8] == 0xA1 && final_init_chunk2[9] == 0x21 &&
            final_init_chunk2[12] == 0x90 &&
            final_init_chunk2[13] == 0x00 &&
            final_init_chunk2[14] == 0x20,
        "native BT RMGE01 final init read returns EEPROM chunk 2");
    // Draining the final command response only opens the device-side input
    // gate. It must not start the hardware clock until a guest bulk read can
    // accept the first sample.
    memory.poll_native_bt_reconnect();
    passed &= expect(
        !memory.native_hid_cadence_stats().armed &&
            !TestAccess::virtual_input_queued(memory),
        "native BT init completion without a bulk read cannot pre-arm or queue input");
    submit_bt_read(kRequest + 0x8260, 1, 0x82, kBuffer, 64);
    passed &= expect(
        !ios_reply_available(guest_memory),
        "native BT first post-init bulk read waits for the device deadline service");
    memory.poll_native_bt_reconnect();
    passed &= expect(
        expect_ios_reply(
            memory,
            guest_memory,
            kRequest + 0x8260,
            12,
            7,
            "native BT virtual input starts at the first deliverable post-init read"),
        "native BT virtual input starts at the first deliverable post-init read");
    const auto* post_init_input =
        reinterpret_cast<const std::uint8_t*>(memory.pointer(kBuffer, 12));
    passed &= expect(
        post_init_input[8] == 0xA1 && post_init_input[9] == 0x30,
        "native BT virtual input unblocks only after RMGE01 init reads drain");
    const galaxy::input::NativeHidCadenceStats first_deliverable_cadence =
        memory.native_hid_cadence_stats();
    passed &= expect(
        first_deliverable_cadence.armed &&
            first_deliverable_cadence.scheduled_samples ==
                after_pre_init_wait.scheduled_samples + 1u &&
            first_deliverable_cadence.produced_samples ==
                after_pre_init_wait.produced_samples + 1u &&
            first_deliverable_cadence.delivered_samples ==
                after_pre_init_wait.delivered_samples + 1u &&
            first_deliverable_cadence.queue_replacements ==
                after_pre_init_wait.queue_replacements &&
            first_deliverable_cadence.skipped_due_slots ==
                after_pre_init_wait.skipped_due_slots &&
            first_deliverable_cadence.delivery_gaps ==
                after_pre_init_wait.delivery_gaps &&
            first_deliverable_cadence.delivery_bursts ==
                after_pre_init_wait.delivery_bursts &&
            first_deliverable_cadence.exact_cadence_valid &&
            cadence_arm_observation.notifications == 1u &&
            cadence_arm_observation.next_unconsumed_sequence ==
                first_deliverable_cadence.last_produced_sequence + 1u &&
            cadence_arm_observation.next_unconsumed_deadline_ticks ==
                fake_ticks + kTestHidReportPeriodTicks &&
            cadence_arm_observation.period_ticks ==
                kTestHidReportPeriodTicks &&
            !TestAccess::virtual_input_queued(memory),
        "native BT first deliverable epoch produces and delivers one exact sample with zero startup replacement or gap");
    acknowledge_ios_reply(guest_memory);

    // 0x176C is also past the readable region: a single error reply, not the
    // multi-chunk success read the old (pre-hardware) model assumed.
    const std::array<std::byte, 8> eeprom_176c_read{
        std::byte{0xA2}, std::byte{0x17}, std::byte{0x00}, std::byte{0x00},
        std::byte{0x17}, std::byte{0x6C}, std::byte{0x00}, std::byte{0x2A}};
    submit_hid_output(
        eeprom_176c_read,
        "native BT HID EEPROM 0x176C read is consumed");
    const auto* eeprom_176c_acl =
        read_bulk_acl(
            kRequest + 0x8270,
            31,
            "native BT HID EEPROM 0x176C error report arrives");
    passed &= expect(
        eeprom_176c_acl[8] == 0xA1 && eeprom_176c_acl[9] == 0x21 &&
            eeprom_176c_acl[12] == 0xF8 &&
            eeprom_176c_acl[13] == 0x17 &&
            eeprom_176c_acl[14] == 0x6C,
        "native BT RMGE01 0x176C read returns silicon read error (0xF8)");

    // A read well past the EEPROM (0x4000) also returns the silicon read error
    // rather than crashing the host -- matching how hardware answers any read
    // beyond the readable region.
    const std::array<std::byte, 8> eeprom_out_of_range_read{
        std::byte{0xA2}, std::byte{0x17}, std::byte{0x00}, std::byte{0x00},
        std::byte{0x40}, std::byte{0x00}, std::byte{0x00}, std::byte{0x01}};
    submit_hid_output(
        eeprom_out_of_range_read,
        "native BT HID EEPROM out-of-range read is consumed");
    const auto* eeprom_oor_acl =
        read_bulk_acl(
            kRequest + 0x828,
            31,
            "native BT HID EEPROM out-of-range read reply arrives");
    passed &= expect(
        eeprom_oor_acl[8] == 0xA1 && eeprom_oor_acl[9] == 0x21 &&
            eeprom_oor_acl[12] == 0xF8,
        "native BT EEPROM read past 0x1700 returns silicon read error (0xF8)");

    const std::array<std::byte, 8> zero_byte_memory_read{
        std::byte{0xA2}, std::byte{0x17}, std::byte{0x04}, std::byte{0xA4},
        std::byte{0x00}, std::byte{0x20}, std::byte{0x00}, std::byte{0x00}};
    passed &= expect_runtime_error(
        [&] {
            const std::vector<std::byte> packet =
                acl_packet(0x0040, zero_byte_memory_read);
            submit_acl_out(packet);
        },
        "native BT zero-byte memory read must hard-fail");

    const std::array<std::byte, 8> unsupported_ir_register_read{
        std::byte{0xA2}, std::byte{0x17}, std::byte{0x04}, std::byte{0xB0},
        std::byte{0x00}, std::byte{0x34}, std::byte{0x00}, std::byte{0x01}};
    passed &= expect_runtime_error(
        [&] {
            const std::vector<std::byte> packet =
                acl_packet(0x0040, unsupported_ir_register_read);
            submit_acl_out(packet);
        },
        "native BT IR register read out of range must hard-fail");

    const std::array<std::byte, 8> extension_id_read_before_init{
        std::byte{0xA2}, std::byte{0x17}, std::byte{0x04}, std::byte{0xA4},
        std::byte{0x00}, std::byte{0xFA}, std::byte{0x00}, std::byte{0x06}};
    passed &= expect_runtime_error(
        [&] {
            const std::vector<std::byte> packet =
                acl_packet(0x0040, extension_id_read_before_init);
            submit_acl_out(packet);
        },
        "native BT extension ID read before init must hard-fail");

    constexpr std::array<std::uint8_t, 6> expected_nunchuk_id{
        0x00, 0x00, 0xA4, 0x20, 0x00, 0x00};

    const auto expect_ack = [&](std::uint32_t request, std::uint8_t report_id) {
        const auto* ack_acl =
            read_bulk_acl(request, 14, "native BT HID ack report arrives");
        passed &= expect(
            ack_acl[4] == 0x06 && ack_acl[5] == 0x00 &&
                ack_acl[6] == 0x43 && ack_acl[7] == 0x00 &&
                ack_acl[8] == 0xA1 && ack_acl[9] == 0x22 &&
                ack_acl[12] == report_id && ack_acl[13] == 0x00,
            "native BT HID ack report references the output report");
    };

    const auto expect_status_flags =
        [&](std::uint32_t request, std::uint8_t expected_flags,
            const char* message) {
            submit_hid_output(hid_status_request, message);
            const auto* status =
                read_bulk_acl(request, 16, message);
            passed &= expect(
                status[8] == 0xA1 && status[9] == 0x20 &&
                    status[12] == expected_flags && status[15] == 0xFF,
                message);
        };
    const auto expect_basic_ir_bytes_hidden =
        [&](const std::uint8_t* acl, const char* message) {
            bool hidden = true;
            for (std::size_t i = 15u; i < 25u; ++i) {
                hidden = hidden && acl[i] == 0xFFu;
            }
            passed &= expect(hidden, message);
        };
    const auto expect_ir_bytes_published =
        [&](const std::uint8_t* acl,
            std::size_t ir_begin,
            std::size_t ir_end,
            const char* message) {
            bool any_visible_byte = false;
            for (std::size_t i = ir_begin; i < ir_end; ++i) {
                any_visible_byte = any_visible_byte || acl[i] != 0xFFu;
            }
            passed &= expect(
                any_visible_byte,
                message);
        };

    const std::array<std::byte, 2> short_led_report{
        std::byte{0xA2}, std::byte{0x11}};
    passed &= expect_runtime_error(
        [&] {
            const std::vector<std::byte> packet =
                acl_packet(0x0040, short_led_report);
            submit_acl_out(packet);
        },
        "native BT short fixed-length HID output report must hard-fail");
    const std::array<std::byte, 4> long_led_report{
        std::byte{0xA2}, std::byte{0x11}, std::byte{0x10}, std::byte{0x00}};
    passed &= expect_runtime_error(
        [&] {
            const std::vector<std::byte> packet =
                acl_packet(0x0040, long_led_report);
            submit_acl_out(packet);
        },
        "native BT long fixed-length HID output report must hard-fail");

    {
        ScopedEnv enable_real_hid("GALAXY_REAL_WIIMOTE_HID", "1");
        passed &= expect_runtime_error(
            [&] {
                const std::vector<std::byte> packet =
                    acl_packet(0x0040, short_led_report);
                submit_acl_out(packet);
            },
            "native BT real HID short fixed-length output report must hard-fail");
        passed &= expect_runtime_error(
            [&] {
                const std::vector<std::byte> packet =
                    acl_packet(0x0040, long_led_report);
                submit_acl_out(packet);
            },
            "native BT real HID long fixed-length output report must hard-fail");
        const std::array<std::byte, 2> unsupported_real_hid_output{
            std::byte{0xA2}, std::byte{0x99}};
        passed &= expect_runtime_error(
            [&] {
                const std::vector<std::byte> packet =
                    acl_packet(0x0040, unsupported_real_hid_output);
                submit_acl_out(packet);
            },
            "native BT real HID unsupported output report must hard-fail");
    }

    const std::array<std::byte, 3> rumble_ack_report{
        std::byte{0xA2}, std::byte{0x10}, std::byte{0x02}};
    submit_hid_output(
        rumble_ack_report,
        "native BT HID rumble-only report is consumed");
    expect_ack(kRequest + 0x1200, 0x10);

    const std::array<std::byte, 3> led_report{
        std::byte{0xA2}, std::byte{0x11}, std::byte{0x32}};
    submit_hid_output(
        led_report,
        "native BT HID LED report is consumed");
    expect_ack(kRequest + 0x1240, 0x11);
    expect_status_flags(
        kRequest + 0xE40,
        0x32,
        "native BT status report reflects LED state");

    std::ostringstream pre_init_ir_trace;
    {
        ScopedEnv scripted_pointer_x("GALAXY_INPUT_POINTER_X", "0.000");
        ScopedEnv scripted_pointer_y("GALAXY_INPUT_POINTER_Y", "0.000");
        ScopedStreamRedirect capture_pre_init_ir_trace(
            std::cerr, pre_init_ir_trace.rdbuf());
        set_input_report_mode(
            0x37,
            "native BT HID set-report-mode before IR init is consumed");
        const auto* disabled_ir_acl = read_bulk_acl(
            kRequest + 0x14C0,
            31,
            "native BT report mode 0x37 hides IR before camera init");
        passed &= expect(
            disabled_ir_acl[8] == 0xA1 && disabled_ir_acl[9] == 0x37,
            "native BT pre-init hidden IR report is mode 0x37");
        expect_basic_ir_bytes_hidden(
            disabled_ir_acl,
            "native BT pre-init IR bytes stay hidden despite host pointer input");
    }
    const std::string pre_init_ir_trace_text = pre_init_ir_trace.str();
    if (pre_init_ir_trace_text.find("FAILED:") != std::string::npos) {
        std::cerr << pre_init_ir_trace_text;
    }
    passed &= expect(
        pre_init_ir_trace_text.find("[input-ir-out] report=0x12") !=
                std::string::npos &&
            pre_init_ir_trace_text.find("mode=0x37") != std::string::npos,
        "native BT IR trace records report-mode output before IR init");
    passed &= expect(
        pre_init_ir_trace_text.find("[input-ir-gate]") != std::string::npos &&
            pre_init_ir_trace_text.find("irgate=0") != std::string::npos &&
            pre_init_ir_trace_text.find("ir13=0") != std::string::npos &&
            pre_init_ir_trace_text.find("ir1a=0") != std::string::npos,
        "native BT IR trace identifies the closed pre-init IR gate");

    const std::array<std::byte, 3> ir_enable_report{
        std::byte{0xA2}, std::byte{0x13}, std::byte{0x06}};
    submit_hid_output(
        ir_enable_report,
        "native BT HID IR enable report is consumed");
    expect_ack(kRequest + 0x1280, 0x13);
    expect_status_flags(
        kRequest + 0xEC0,
        0x3A,
        "native BT status report reflects IR state");

    const std::array<std::byte, 3> ir_enable_part2_report{
        std::byte{0xA2}, std::byte{0x1A}, std::byte{0x06}};
    submit_hid_output(
        ir_enable_part2_report,
        "native BT HID IR enable part 2 report is consumed");
    expect_ack(kRequest + 0x12C0, 0x1A);
    expect_status_flags(
        kRequest + 0xF00,
        0x3A,
        "native BT status report reflects IR part 2 state");

    {
        ScopedEnv scripted_pointer_x("GALAXY_INPUT_POINTER_X", "0.000");
        ScopedEnv scripted_pointer_y("GALAXY_INPUT_POINTER_Y", "0.000");
        set_input_report_mode(
            0x37,
            "native BT HID set-report-mode before IR register init is consumed");
        const auto* pre_register_ir_acl = read_bulk_acl(
            kRequest + 0x1500,
            31,
            "native BT report mode 0x37 hides IR before sensitivity init");
        passed &= expect(
            pre_register_ir_acl[8] == 0xA1 &&
                pre_register_ir_acl[9] == 0x37,
            "native BT pre-register hidden IR report is mode 0x37");
        expect_basic_ir_bytes_hidden(
            pre_register_ir_acl,
            "native BT IR enable reports alone do not publish host pointer dots");
    }

    const std::array<std::byte, 1> ir_clock_start{std::byte{0x01}};
    const auto ir_clock_start_write =
        wiimote_memory_write_report(0x04, 0xB00030, ir_clock_start);
    submit_hid_output(
        ir_clock_start_write,
        "native BT HID IR clock-start register write is consumed");
    expect_ack(kRequest + 0xF40, 0x16);

    const std::array<std::byte, 9> ir_sensitivity_block1{
        std::byte{0x02}, std::byte{0x00}, std::byte{0x00},
        std::byte{0x71}, std::byte{0x01}, std::byte{0x00},
        std::byte{0xAA}, std::byte{0x00}, std::byte{0x64}};
    const auto ir_block1_write =
        wiimote_memory_write_report(0x04, 0xB00000, ir_sensitivity_block1);
    submit_hid_output(
        ir_block1_write,
        "native BT HID IR sensitivity block 1 write is consumed");
    expect_ack(kRequest + 0xF80, 0x16);

    const std::array<std::byte, 2> ir_sensitivity_block2{
        std::byte{0x63}, std::byte{0x03}};
    const auto ir_block2_write =
        wiimote_memory_write_report(0x04, 0xB0001A, ir_sensitivity_block2);
    submit_hid_output(
        ir_block2_write,
        "native BT HID IR sensitivity block 2 write is consumed");
    expect_ack(kRequest + 0xFC0, 0x16);

    set_input_report_mode(
        0x30,
        "native BT HID non-IR report-mode during IR startup is consumed");
    const auto* mid_ir_startup_input = read_bulk_acl(
        kRequest + 0xFD0,
        12,
        "native BT mid-IR startup preserves the requested non-IR input report");
    passed &= expect(
        mid_ir_startup_input[8] == 0xA1 &&
            mid_ir_startup_input[9] == 0x30,
        "native BT mid-IR startup does not suppress or promote report mode 0x30");

    const std::array<std::byte, 1> ir_extended_mode{std::byte{0x03}};
    const auto ir_mode_write =
        wiimote_memory_write_report(0x04, 0xB00033, ir_extended_mode);
    submit_hid_output(
        ir_mode_write,
        "native BT HID IR mode register write is consumed");
    expect_ack(kRequest + 0x1000, 0x16);

    const std::array<std::byte, 1> ir_clock_finish{std::byte{0x08}};
    const auto ir_clock_finish_write =
        wiimote_memory_write_report(0x04, 0xB00030, ir_clock_finish);
    submit_hid_output(
        ir_clock_finish_write,
        "native BT HID IR clock-finish register write is consumed");
    expect_ack(kRequest + 0x1040, 0x16);

    {
        ScopedEnv scripted_pointer_x("GALAXY_INPUT_POINTER_X", "0.000");
        ScopedEnv scripted_pointer_y("GALAXY_INPUT_POINTER_Y", "0.000");
        NativeVirtualInputDeliveryCapture exact_payload_capture{};
        exact_payload_capture.address_space = &memory;
        memory.set_native_virtual_input_acl_delivery_callback(
            &capture_native_virtual_input_delivery,
            &exact_payload_capture);

        const auto expect_exact_input_report =
            [&](std::uint8_t requested_mode,
                std::uint32_t request_offset,
                std::uint32_t acl_size,
                std::size_t ir_begin,
                std::size_t ir_end,
                const char* message) {
            set_input_report_mode(
                requested_mode,
                "native BT HID set-report-mode after full IR init is consumed");
            const std::size_t identity_index = exact_payload_capture.count;
            const auto* ir_acl = read_bulk_acl(
                kRequest + request_offset,
                acl_size,
                message);
            passed &= expect(
                ir_acl[8] == 0xA1 && ir_acl[9] == requested_mode,
                message);
            const auto& identity =
                exact_payload_capture.identities[identity_index];
            const std::uint32_t expected_payload_size =
                acl_size - 8u;
            passed &= expect(
                exact_payload_capture.count == identity_index + 1u &&
                    identity.payload_size == expected_payload_size &&
                    std::equal(
                        ir_acl + 8u,
                        ir_acl + acl_size,
                        identity.payload.begin()) &&
                    std::all_of(
                        identity.payload.begin() + identity.payload_size,
                        identity.payload.end(),
                        [](std::uint8_t value) { return value == 0u; }),
                "native BT delivery identity retains exact variable-length HID payload bytes with a zeroed tail");
            if (ir_begin != ir_end) {
                expect_ir_bytes_published(
                    ir_acl,
                    ir_begin,
                    ir_end,
                    message);
            }
        };
        const auto set_ir_camera_mode =
            [&](std::uint8_t camera_mode,
                std::uint32_t ack_request_offset,
                const char* message) {
                const std::array<std::byte, 1> mode_data{
                    static_cast<std::byte>(camera_mode)};
                const auto mode_write = wiimote_memory_write_report(
                    0x04, 0xB00033, mode_data);
                submit_hid_output(mode_write, message);
                expect_ack(kRequest + ack_request_offset, 0x16);
            };

        // RMGE01 requests 0x30 while the camera is in extended mode. Hardware
        // sends exactly 0x30; it does not silently substitute an IR report.
        expect_exact_input_report(
            0x30,
            0x1500,
            12,
            0,
            0,
            "native BT camera mode 3 preserves requested report mode 0x30");
        expect_exact_input_report(
            0x33,
            0x1540,
            27,
            15,
            27,
            "native BT camera mode 3 publishes extended IR only through report mode 0x33");
        if (!strict_command_sequence_only) {
            passed &= expect_runtime_error(
                [&] {
                    set_input_report_mode(
                        0x37,
                        "native BT camera mode 3 rejects a basic-IR input layout");
                },
                "native BT camera mode 3 and report mode 0x37 must hard-fail as an incompatible IR layout");
        }

        set_ir_camera_mode(
            0x01,
            0x1580,
            "native BT switches the IR camera to basic layout");
        expect_exact_input_report(
            0x30,
            0x15C0,
            12,
            0,
            0,
            "native BT camera mode 1 preserves requested report mode 0x30");
        expect_exact_input_report(
            0x36,
            0x1600,
            31,
            12,
            22,
            "native BT camera mode 1 publishes basic IR through report mode 0x36");
        expect_exact_input_report(
            0x37,
            0x1640,
            31,
            15,
            25,
            "native BT camera mode 1 publishes basic IR through report mode 0x37");
        if (!strict_command_sequence_only) {
            passed &= expect_runtime_error(
                [&] {
                    set_input_report_mode(
                        0x33,
                        "native BT camera mode 1 rejects an extended-IR input layout");
                },
                "native BT camera mode 1 and report mode 0x33 must hard-fail as an incompatible IR layout");

            set_ir_camera_mode(
                0x05,
                0x1680,
                "native BT records an unknown nonzero IR camera layout exactly");
            passed &= expect_runtime_error(
                [&] {
                    set_input_report_mode(
                        0x37,
                        "native BT unknown IR camera layout rejects basic-IR input");
                },
                "native BT unknown nonzero IR camera mode must hard-fail instead of guessing a report layout");
            set_ir_camera_mode(
                0x01,
                0x16C0,
                "native BT restores the basic IR camera layout");
        }
        set_input_report_mode(
            0x37,
            "native BT restores the compatible basic-IR report mode");
        const auto* restored_basic_ir = read_bulk_acl(
            kRequest + 0x1700,
            31,
            "native BT resumes exact basic-IR input after restoring a compatible layout");
        passed &= expect(
            restored_basic_ir[8] == 0xA1 && restored_basic_ir[9] == 0x37,
            "native BT compatible IR layout recovery preserves the requested report mode");
        memory.set_native_virtual_input_acl_delivery_callback(nullptr, nullptr);
    }

    if (strict_command_sequence_only) {
        const galaxy::input::NativeHidCadenceStats& stats =
            memory.native_hid_cadence_stats();
        const std::uint64_t fifo_occupied =
            TestAccess::virtual_input_queued(memory) ? 1u : 0u;
        const std::uint64_t accounted_production =
            stats.delivered_samples + stats.mode_invalidations +
            stats.reset_queue_discards + stats.queue_replacements +
            fifo_occupied;
        const bool strict_cadence_valid =
            stats.strict_proof && stats.profile_strict_proof_qualified &&
                stats.exact_cadence_valid &&
                stats.scheduled_samples > 0u &&
                stats.scheduled_samples == stats.produced_samples &&
                stats.epochs_started > 1u &&
                stats.reset_queue_discards == 0u &&
                stats.queue_replacements == 0u &&
                stats.skipped_due_slots == 0u &&
                stats.delivery_gaps == 0u &&
                stats.delivery_bursts == 0u &&
                stats.out_of_order_deliveries == 0u &&
                stats.produced_samples == accounted_production;
        if (!strict_cadence_valid) {
            galaxy::input::dump_native_hid_cadence_audit(
                std::cerr,
                "strict-rmge01-ir-command-sequence-failure",
                stats,
                fifo_occupied != 0u);
        }
        passed &= expect(
            strict_cadence_valid,
            "strict native HID cadence survives the actual RMGE01 IR command/ACK sequence with conserved logical mode epochs");
        return passed;
    }

    const std::array<std::byte, 3> ir_enable_part1_disable_report{
        std::byte{0xA2}, std::byte{0x13}, std::byte{0x02}};
    std::ostringstream ir_part1_disable_trace;
    {
        ScopedStreamRedirect capture_ir_part1_disable_trace(
            std::cerr, ir_part1_disable_trace.rdbuf());
        submit_hid_output(
            ir_enable_part1_disable_report,
            "native BT HID IR enable part 1 disable report is consumed");
        expect_ack(kRequest + 0x1600, 0x13);
        ScopedEnv scripted_pointer_x("GALAXY_INPUT_POINTER_X", "0.000");
        ScopedEnv scripted_pointer_y("GALAXY_INPUT_POINTER_Y", "0.000");
        set_input_report_mode(
            0x37,
            "native BT HID set-report-mode after IR part 1 disable is consumed");
        const auto* post_disable_ir_acl = read_bulk_acl(
            kRequest + 0x1640,
            31,
            "native BT report mode 0x37 hides IR after IR part 1 disable");
        passed &= expect(
            post_disable_ir_acl[8] == 0xA1 &&
                post_disable_ir_acl[9] == 0x37,
            "native BT post-disable hidden IR report is mode 0x37");
        expect_basic_ir_bytes_hidden(
            post_disable_ir_acl,
            "native BT disabling one IR enable report hides host pointer dots");
    }
    const std::string ir_part1_disable_trace_text =
        ir_part1_disable_trace.str();
    if (ir_part1_disable_trace_text.find("FAILED:") != std::string::npos) {
        std::cerr << ir_part1_disable_trace_text;
    }
    passed &= expect(
        ir_part1_disable_trace_text.find("[input-ir-out] report=0x13") !=
                std::string::npos &&
            ir_part1_disable_trace_text.find("enable=0") != std::string::npos &&
            ir_part1_disable_trace_text.find("irgate=0") != std::string::npos &&
            ir_part1_disable_trace_text.find("ir13=0") != std::string::npos,
        "native BT IR trace identifies part 1 disable as a closed IR gate");
    submit_hid_output(
        ir_enable_report,
        "native BT HID IR enable part 1 re-enable report is consumed");
    expect_ack(kRequest + 0x1680, 0x13);

    const std::array<std::byte, 3> ir_enable_part2_disable_report{
        std::byte{0xA2}, std::byte{0x1A}, std::byte{0x02}};
    std::ostringstream ir_part2_disable_trace;
    {
        ScopedStreamRedirect capture_ir_part2_disable_trace(
            std::cerr, ir_part2_disable_trace.rdbuf());
        submit_hid_output(
            ir_enable_part2_disable_report,
            "native BT HID IR enable part 2 disable report is consumed");
        expect_ack(kRequest + 0x16C0, 0x1A);
        ScopedEnv scripted_pointer_x("GALAXY_INPUT_POINTER_X", "0.000");
        ScopedEnv scripted_pointer_y("GALAXY_INPUT_POINTER_Y", "0.000");
        set_input_report_mode(
            0x37,
            "native BT HID set-report-mode after IR part 2 disable is consumed");
        const auto* post_disable_ir_acl = read_bulk_acl(
            kRequest + 0x1700,
            31,
            "native BT report mode 0x37 hides IR after IR part 2 disable");
        passed &= expect(
            post_disable_ir_acl[8] == 0xA1 &&
                post_disable_ir_acl[9] == 0x37,
            "native BT post-disable hidden IR report is mode 0x37");
        expect_basic_ir_bytes_hidden(
            post_disable_ir_acl,
            "native BT disabling IR enable part 2 hides host pointer dots");
    }
    const std::string ir_part2_disable_trace_text =
        ir_part2_disable_trace.str();
    if (ir_part2_disable_trace_text.find("FAILED:") != std::string::npos) {
        std::cerr << ir_part2_disable_trace_text;
    }
    passed &= expect(
        ir_part2_disable_trace_text.find("[input-ir-out] report=0x1a") !=
                std::string::npos &&
            ir_part2_disable_trace_text.find("enable=0") != std::string::npos &&
            ir_part2_disable_trace_text.find("irgate=0") != std::string::npos &&
            ir_part2_disable_trace_text.find("ir1a=0") != std::string::npos,
        "native BT IR trace identifies part 2 disable as a closed IR gate");
    submit_hid_output(
        ir_enable_part2_report,
        "native BT HID IR enable part 2 re-enable report is consumed");
    expect_ack(kRequest + 0x1740, 0x1A);

    const std::array<std::byte, 1> invalid_ir_mode{std::byte{0x09}};
    const auto invalid_ir_mode_write =
        wiimote_memory_write_report(0x04, 0xB00033, invalid_ir_mode);
    passed &= expect_runtime_error(
        [&] {
            const std::vector<std::byte> packet =
                acl_packet(0x0040, invalid_ir_mode_write);
            submit_acl_out(packet);
        },
        "native BT invalid IR mode register write must hard-fail");

    const std::array<std::byte, 1> invalid_ir_register{std::byte{0x00}};
    const auto invalid_ir_register_write =
        wiimote_memory_write_report(0x04, 0xB00034, invalid_ir_register);
    passed &= expect_runtime_error(
        [&] {
            const std::vector<std::byte> packet =
                acl_packet(0x0040, invalid_ir_register_write);
            submit_acl_out(packet);
        },
        "native BT unsupported IR register write must hard-fail");

    const std::array<std::byte, 20> speaker_data{};
    const auto speaker_data_before_enable =
        wiimote_speaker_data_report(0xA0, speaker_data);
    passed &= expect_runtime_error(
        [&] {
            const std::vector<std::byte> packet =
                acl_packet(0x0040, speaker_data_before_enable);
            submit_acl_out(packet);
        },
        "native BT speaker data before speaker enable must hard-fail");

    const std::array<std::byte, 3> speaker_enable_report{
        std::byte{0xA2}, std::byte{0x14}, std::byte{0x06}};
    submit_hid_output(
        speaker_enable_report,
        "native BT HID speaker enable report is consumed");
    expect_ack(kRequest + 0x1300, 0x14);
    expect_status_flags(
        kRequest + 0x10C0,
        0x3E,
        "native BT status report reflects speaker state");

    const std::array<std::byte, 1> speaker_config_gate{std::byte{0x01}};
    const auto speaker_config_gate_write =
        wiimote_memory_write_report(0x04, 0xA20009, speaker_config_gate);
    submit_hid_output(
        speaker_config_gate_write,
        "native BT HID speaker config gate write is consumed");
    expect_ack(kRequest + 0x1100, 0x16);

    const std::array<std::byte, 1> speaker_config_gate_close{std::byte{0x00}};
    const auto speaker_config_gate_close_write =
        wiimote_memory_write_report(0x04, 0xA20009, speaker_config_gate_close);
    submit_hid_output(
        speaker_config_gate_close_write,
        "native BT HID speaker config gate-close write is consumed");
    expect_ack(kRequest + 0x1120, 0x16);

    const std::array<std::byte, 1> speaker_config_enable{std::byte{0x08}};
    const auto speaker_config_enable_write =
        wiimote_memory_write_report(0x04, 0xA20001, speaker_config_enable);
    submit_hid_output(
        speaker_config_enable_write,
        "native BT HID speaker config enable write is consumed");
    expect_ack(kRequest + 0x1140, 0x16);

    const std::array<std::byte, 1> speaker_config_alt_enable{std::byte{0x80}};
    const auto speaker_config_alt_enable_write =
        wiimote_memory_write_report(0x04, 0xA20001, speaker_config_alt_enable);
    submit_hid_output(
        speaker_config_alt_enable_write,
        "native BT HID alternate speaker config write is consumed");
    expect_ack(kRequest + 0x1160, 0x16);

    const std::array<std::byte, 1> speaker_config_commit_value{
        std::byte{0x01}};
    const auto speaker_config_commit_value_write =
        wiimote_memory_write_report(
            0x04, 0xA20001, speaker_config_commit_value);
    submit_hid_output(
        speaker_config_commit_value_write,
        "native BT HID speaker config value write is consumed");
    expect_ack(kRequest + 0x11A0, 0x16);

    const std::array<std::byte, 7> speaker_config_block{
        std::byte{0x00}, std::byte{0x40}, std::byte{0x70},
        std::byte{0x17}, std::byte{0x60}, std::byte{0x00},
        std::byte{0x00}};
    const auto speaker_config_block_write =
        wiimote_memory_write_report(0x04, 0xA20001, speaker_config_block);
    submit_hid_output(
        speaker_config_block_write,
        "native BT HID speaker config block write is consumed");
    expect_ack(kRequest + 0x1180, 0x16);

    const std::array<std::byte, 1> speaker_config_commit{std::byte{0x01}};
    const auto speaker_config_commit_write =
        wiimote_memory_write_report(0x04, 0xA20008, speaker_config_commit);
    submit_hid_output(
        speaker_config_commit_write,
        "native BT HID speaker config commit write is consumed");
    expect_ack(kRequest + 0x11C0, 0x16);

    const std::array<std::byte, 1> invalid_speaker_register{std::byte{0x01}};
    const auto invalid_speaker_register_write =
        wiimote_memory_write_report(0x04, 0xA2000A, invalid_speaker_register);
    passed &= expect_runtime_error(
        [&] {
            const std::vector<std::byte> packet =
                acl_packet(0x0040, invalid_speaker_register_write);
            submit_acl_out(packet);
        },
        "native BT unsupported speaker register write must hard-fail");

    const std::array<std::byte, 2> invalid_speaker_config{
        std::byte{0x08}, std::byte{0x00}};
    const auto invalid_speaker_config_write =
        wiimote_memory_write_report(0x04, 0xA20001, invalid_speaker_config);
    passed &= expect_runtime_error(
        [&] {
            const std::vector<std::byte> packet =
                acl_packet(0x0040, invalid_speaker_config_write);
            submit_acl_out(packet);
        },
        "native BT invalid speaker config write size must hard-fail");

    const std::array<std::byte, 3> speaker_mute_report{
        std::byte{0xA2}, std::byte{0x19}, std::byte{0x06}};
    submit_hid_output(
        speaker_mute_report,
        "native BT HID speaker mute report is consumed");
    expect_ack(kRequest + 0x1340, 0x19);
    submit_hid_output(
        speaker_data_before_enable,
        "native BT HID speaker data is consumed after enable");

    const auto speaker_data_bad_length =
        wiimote_speaker_data_report(0xF8, speaker_data);
    passed &= expect_runtime_error(
        [&] {
            const std::vector<std::byte> packet =
                acl_packet(0x0040, speaker_data_bad_length);
            submit_acl_out(packet);
        },
        "native BT invalid speaker data length flag must hard-fail");

    const std::array<std::byte, 8> short_memory_write{
        std::byte{0xA2}, std::byte{0x16}, std::byte{0x04}, std::byte{0xA4},
        std::byte{0x00}, std::byte{0xF0}, std::byte{0x01}, std::byte{0x55}};
    passed &= expect_runtime_error(
        [&] {
            const std::vector<std::byte> packet =
                acl_packet(0x0040, short_memory_write);
            submit_acl_out(packet);
        },
        "native BT short write-memory report must hard-fail");

    const auto zero_byte_register_write =
        wiimote_memory_write_report(0x04, 0xA40040, {});
    passed &= expect_runtime_error(
        [&] {
            const std::vector<std::byte> packet =
                acl_packet(0x0040, zero_byte_register_write);
            submit_acl_out(packet);
        },
        "native BT zero-byte register write must hard-fail");

    const std::array<std::byte, 1> extension_init_40_data{std::byte{0x00}};
    const auto extension_init_40 =
        wiimote_memory_write_report(0x04, 0xA40040, extension_init_40_data);
    submit_hid_output(
        extension_init_40,
        "native BT HID extension init write 40 is consumed");
    expect_ack(kRequest + 0x890, 0x16);
    submit_hid_output(
        extension_id_read_before_init,
        "native BT HID extension ID read works after 40 init");
    const auto* extension_id_40_acl =
        read_bulk_acl(
            kRequest + 0x8C0,
            31,
            "native BT HID extension ID read-memory report arrives after 40 init");
    passed &= expect(
        extension_id_40_acl[8] == 0xA1 && extension_id_40_acl[9] == 0x21 &&
            extension_id_40_acl[12] == 0x50 &&
            extension_id_40_acl[13] == 0x00 &&
            extension_id_40_acl[14] == 0xFA,
        "native BT extension ID after 40 init has a Wii-shaped header");
    passed &= expect_bytes(
        std::span<const std::uint8_t>(extension_id_40_acl + 15, 6),
        expected_nunchuk_id,
        "native BT extension ID after 40 init identifies a Nunchuk");

    const std::array<std::byte, 1> bad_extension_init_40_data{
        std::byte{0x01}};
    const auto bad_extension_init_40 =
        wiimote_memory_write_report(
            0x04, 0xA40040, bad_extension_init_40_data);
    passed &= expect_runtime_error(
        [&] {
            const std::vector<std::byte> packet =
                acl_packet(0x0040, bad_extension_init_40);
            submit_acl_out(packet);
        },
        "native BT invalid extension init write 40 must hard-fail");

    const std::array<std::byte, 1> extension_init_f0_data{std::byte{0x55}};
    const auto extension_init_f0 =
        wiimote_memory_write_report(0x04, 0xA400F0, extension_init_f0_data);
    submit_hid_output(
        extension_init_f0,
        "native BT HID extension init write F0 is consumed");
    expect_ack(kRequest + 0x900, 0x16);
    passed &= expect_runtime_error(
        [&] {
            const std::vector<std::byte> packet =
                acl_packet(0x0040, extension_id_read_before_init);
            submit_acl_out(packet);
        },
        "native BT extension ID read after F0 before FB must hard-fail");

    const std::array<std::byte, 1> extension_init_fb_data{std::byte{0x00}};
    const auto extension_init_fb =
        wiimote_memory_write_report(0x04, 0xA400FB, extension_init_fb_data);
    submit_hid_output(
        extension_init_fb,
        "native BT HID extension init write FB is consumed");
    expect_ack(kRequest + 0xA00, 0x16);

    submit_hid_output(
        extension_id_read_before_init,
        "native BT HID extension ID read is consumed after init");
    const auto* extension_id_acl =
        read_bulk_acl(
            kRequest + 0xB00,
            31,
            "native BT HID extension ID read-memory report arrives");
    passed &= expect(
        extension_id_acl[4] == 0x17 && extension_id_acl[5] == 0x00 &&
            extension_id_acl[6] == 0x43 && extension_id_acl[7] == 0x00 &&
            extension_id_acl[8] == 0xA1 && extension_id_acl[9] == 0x21 &&
            extension_id_acl[12] == 0x50 && extension_id_acl[13] == 0x00 &&
            extension_id_acl[14] == 0xFA,
        "native BT extension ID read-memory header is Wii-shaped");
    passed &= expect_bytes(
        std::span<const std::uint8_t>(extension_id_acl + 15, 6),
        expected_nunchuk_id,
        "native BT extension ID identifies a Nunchuk");

    const std::array<std::byte, 1> extension_post_init_f0_data{
        std::byte{0xAA}};
    const auto extension_post_init_f0 =
        wiimote_memory_write_report(
            0x04, 0xA400F0, extension_post_init_f0_data);
    submit_hid_output(
        extension_post_init_f0,
        "native BT HID post-init extension F0 control write is consumed");
    expect_ack(kRequest + 0xA40, 0x16);

    const std::array<std::byte, 6> extension_control_block{
        std::byte{0x81}, std::byte{0x80}, std::byte{0x2D},
        std::byte{0xE5}, std::byte{0x50}, std::byte{0x95}};
    const auto extension_control_block_write =
        wiimote_memory_write_report(
            0x04, 0xA40040, extension_control_block);
    submit_hid_output(
        extension_control_block_write,
        "native BT HID extension control block write is consumed");
    expect_ack(kRequest + 0xA80, 0x16);

    const std::array<std::byte, 6> extension_control_block_46{
        std::byte{0xCE}, std::byte{0xA5}, std::byte{0xD3},
        std::byte{0xEE}, std::byte{0xE1}, std::byte{0xA7}};
    const auto extension_control_block_46_write =
        wiimote_memory_write_report(
            0x04, 0xA40046, extension_control_block_46);
    submit_hid_output(
        extension_control_block_46_write,
        "native BT HID extension control block 46 write is consumed");
    expect_ack(kRequest + 0xAC0, 0x16);

    const std::array<std::byte, 4> extension_control_block_4c{
        std::byte{0xCC}, std::byte{0xE7}, std::byte{0x11},
        std::byte{0x69}};
    const auto extension_control_block_4c_write =
        wiimote_memory_write_report(
            0x04, 0xA4004C, extension_control_block_4c);
    submit_hid_output(
        extension_control_block_4c_write,
        "native BT HID extension control block 4C write is consumed");
    expect_ack(kRequest + 0xB40, 0x16);

    const std::array<std::byte, 8> extension_calibration_read{
        std::byte{0xA2}, std::byte{0x17}, std::byte{0x04}, std::byte{0xA4},
        std::byte{0x00}, std::byte{0x20}, std::byte{0x00}, std::byte{0x10}};
    submit_hid_output(
        extension_calibration_read,
        "native BT HID extension calibration read is consumed");
    const auto* calibration_acl =
        read_bulk_acl(
            kRequest + 0xC00,
            31,
            "native BT HID extension calibration report arrives");
    constexpr std::array<std::uint8_t, 16> expected_nunchuk_calibration{
        0x7D, 0x7E, 0x7A, 0x3C, 0xB1, 0xAF, 0xAE, 0x1B,
        0xE0, 0x15, 0x7B, 0xE6, 0x1D, 0x81, 0x23, 0x78};
    passed &= expect(
        calibration_acl[8] == 0xA1 && calibration_acl[9] == 0x21 &&
            calibration_acl[12] == 0xF0 &&
            calibration_acl[13] == 0x00 && calibration_acl[14] == 0x20,
        "native BT extension calibration read-memory header is Wii-shaped");
    passed &= expect_bytes(
        std::span<const std::uint8_t>(calibration_acl + 15, 16),
        expected_nunchuk_calibration,
        "native BT extension calibration bytes are stable");

    const std::array<std::byte, 8> extension_multi_chunk_read{
        std::byte{0xA2}, std::byte{0x17}, std::byte{0x04}, std::byte{0xA4},
        std::byte{0x00}, std::byte{0x20}, std::byte{0x00}, std::byte{0x20}};
    submit_hid_output(
        extension_multi_chunk_read,
        "native BT HID multi-chunk extension read is consumed");
    const auto* chunk0_acl =
        read_bulk_acl(
            kRequest + 0xC40,
            31,
            "native BT first extension read chunk arrives");
    passed &= expect(
        chunk0_acl[8] == 0xA1 && chunk0_acl[9] == 0x21 &&
            chunk0_acl[12] == 0xF0 &&
            chunk0_acl[13] == 0x00 && chunk0_acl[14] == 0x20,
        "native BT first extension read chunk header is Wii-shaped");
    passed &= expect_bytes(
        std::span<const std::uint8_t>(chunk0_acl + 15, 16),
        expected_nunchuk_calibration,
        "native BT first extension read chunk carries calibration bytes");
    const auto* chunk1_acl =
        read_bulk_acl(
            kRequest + 0xC80,
            31,
            "native BT second extension read chunk arrives");
    constexpr std::array<std::uint8_t, 16> expected_zero_extension_chunk{};
    passed &= expect(
        chunk1_acl[8] == 0xA1 && chunk1_acl[9] == 0x21 &&
            chunk1_acl[12] == 0xF0 &&
            chunk1_acl[13] == 0x00 && chunk1_acl[14] == 0x30,
        "native BT second extension read chunk header advances address");
    passed &= expect_bytes(
        std::span<const std::uint8_t>(chunk1_acl + 15, 16),
        expected_zero_extension_chunk,
        "native BT second extension read chunk carries following register bytes");

    const std::array<std::byte, 4> acked_set_report_mode{
        std::byte{0xA2}, std::byte{0x12}, std::byte{0x06}, std::byte{0x30}};
    submit_hid_output(
        acked_set_report_mode,
        "native BT HID ack-requested set-report-mode output is consumed");
    expect_ack(kRequest + 0x1380, 0x12);
    memory.poll_native_bt_reconnect();
    const auto* acked_mode_input =
        read_bulk_acl(
            kRequest + 0x13C0,
            12,
            "native BT ack-requested report mode starts exact requested input");
    passed &= expect(
        acked_mode_input[8] == 0xA1 && acked_mode_input[9] == 0x30,
        "native BT ack-requested set-report-mode preserves mode 0x30 after camera init");

    const std::array<std::byte, 4> no_ack_set_report_mode{
        std::byte{0xA2}, std::byte{0x12}, std::byte{0x04}, std::byte{0x37}};
    submit_hid_output(
        no_ack_set_report_mode,
        "native BT HID non-ack set-report-mode output is consumed");
    memory.poll_native_bt_reconnect();
    const auto* no_ack_mode_input =
        read_bulk_acl(
            kRequest + 0x1400,
            31,
            "native BT non-ack report mode streams input directly");
    passed &= expect(
        no_ack_mode_input[8] == 0xA1 && no_ack_mode_input[9] == 0x37,
        "native BT non-ack set-report-mode is not preceded by a HID ack");

    struct InputReportModeCase {
        std::uint8_t requested_mode;
        std::uint8_t emitted_mode;
        std::uint32_t report_size;
        const char* message;
    };
    constexpr std::array<InputReportModeCase, 8> input_report_modes{{
        {0x30, 0x30, 4, "native BT HID input report preserves requested mode 0x30"},
        {0x31, 0x31, 7, "native BT HID input report preserves requested mode 0x31"},
        {0x32, 0x32, 12, "native BT HID input report preserves requested mode 0x32"},
        {0x33, 0x33, 19, "native BT HID input report is ACL-wrapped report mode 0x33"},
        {0x34, 0x34, 23, "native BT HID input report preserves requested mode 0x34"},
        {0x35, 0x35, 23, "native BT HID input report preserves requested mode 0x35"},
        {0x36, 0x36, 23, "native BT HID input report is ACL-wrapped report mode 0x36"},
        {0x37, 0x37, 23, "native BT HID input report is ACL-wrapped report mode 0x37"},
    }};
    std::uint32_t input_report_request = kRequest + 0xD00;
    std::ostringstream input_trace;
    {
        ScopedEnv scripted_buttons(
            "GALAXY_INPUT_AUTOPRESS_SCRIPT", "0:16:A+B");
        ScopedEnv scripted_stick(
            "GALAXY_INPUT_STICK_SCRIPT", "0:16:0.500:-1.000");
        ScopedEnv scripted_shake("GALAXY_INPUT_SHAKE_SCRIPT", "0:16");
        ScopedEnv scripted_pointer_x("GALAXY_INPUT_POINTER_X", "0.000");
        ScopedEnv scripted_pointer_y("GALAXY_INPUT_POINTER_Y", "0.000");
        ScopedEnv disable_script_stop(
            "GALAXY_INPUT_AUTOPRESS_SCRIPT_STOP_AT_MARIO_CONTROL", "0");
        ScopedEnv disable_relative_stick(
            "GALAXY_INPUT_STICK_SCRIPT_RELATIVE_MARIO_CONTROL", "0");
        ScopedStreamRedirect capture_input_trace(std::cerr, input_trace.rdbuf());
        for (const InputReportModeCase& input_report_mode : input_report_modes) {
            if (input_report_mode.requested_mode == 0x33u) {
                const std::array<std::byte, 1> extended_camera_mode{
                    std::byte{0x03}};
                const auto extended_camera_write = wiimote_memory_write_report(
                    0x04, 0xB00033, extended_camera_mode);
                submit_hid_output(
                    extended_camera_write,
                    "native BT selects extended camera bytes before report mode 0x33");
                expect_ack(kRequest + 0x13D0, 0x16);
            } else if (input_report_mode.requested_mode == 0x36u) {
                const std::array<std::byte, 1> basic_camera_mode{
                    std::byte{0x01}};
                const auto basic_camera_write = wiimote_memory_write_report(
                    0x04, 0xB00033, basic_camera_mode);
                submit_hid_output(
                    basic_camera_write,
                    "native BT selects basic camera bytes before report modes 0x36 and 0x37");
                expect_ack(kRequest + 0x13E0, 0x16);
            }
            set_input_report_mode(
                input_report_mode.requested_mode,
                "native BT HID set-report-mode output is consumed");
            const auto* input_acl = read_bulk_acl(
                input_report_request,
                input_report_mode.report_size + 8u,
                input_report_mode.message);
            passed &= expect(
                input_acl[4] == input_report_mode.report_size &&
                    input_acl[5] == 0x00 &&
                    input_acl[6] == 0x43 && input_acl[7] == 0x00 &&
                    input_acl[8] == 0xA1 &&
                    input_acl[9] == input_report_mode.emitted_mode,
                input_report_mode.message);
            if (input_report_mode.requested_mode == 0x30u) {
                passed &= expect(
                    input_acl[10] == 0x00 && input_acl[11] == 0x0C,
                    "native BT scripted A+B is encoded as exact mode 0x30 core button bytes");
            }
            if (input_report_mode.requested_mode == 0x32u) {
                passed &= expect(
                    input_acl[12] == 0x66u && input_acl[13] == 0x7Eu,
                    "native BT scripted stick is encrypted as exact mode 0x32 Nunchuk wire bytes");
            }
            if (input_report_mode.emitted_mode == 0x36u) {
                passed &= expect(
                    input_acl[12] == 0xAEu && input_acl[13] == 0xE7u &&
                        input_acl[14] == 0x56u && input_acl[15] == 0x4Eu &&
                        input_acl[16] == 0xE7u,
                    "native BT scripted pointer is encoded as IR bytes");
            }
            input_report_request += 0x40u;
        }
    }
    {
        const std::uint64_t vi_period_ticks =
            galaxy::input::kNativeHidTimelineTicksPerSecond / 60u;
        const std::uint64_t fallback_vi = fake_ticks / vi_period_ticks;
        const std::uint64_t first_mario_control_vi = fallback_vi + 8u;
        const std::string fallback_vi_text = std::to_string(fallback_vi);
        const std::string button_stop_fallback_vi_text =
            std::to_string(first_mario_control_vi + 100u);
        ScopedEnv scripted_buttons(
            "GALAXY_INPUT_AUTOPRESS_SCRIPT", "0:1000000:A+B");
        ScopedEnv marker_stopped_buttons(
            "GALAXY_INPUT_AUTOPRESS_SCRIPT_STOP_AT_MARIO_CONTROL", "1");
        ScopedEnv button_stop_fallback(
            "GALAXY_INPUT_AUTOPRESS_SCRIPT_STOP_FALLBACK_VI",
            button_stop_fallback_vi_text.c_str());
        ScopedEnv scripted_stick(
            "GALAXY_INPUT_STICK_SCRIPT", "0:4:0.500:-1.000");
        ScopedEnv relative_stick(
            "GALAXY_INPUT_STICK_SCRIPT_RELATIVE_MARIO_CONTROL", "1");
        ScopedEnv relative_fallback(
            "GALAXY_INPUT_STICK_SCRIPT_RELATIVE_FALLBACK_VI",
            fallback_vi_text.c_str());

        passed &= expect(
            !memory.native_input_first_mario_control_vi().has_value(),
            "native input relative stick starts without a published Mario-control VI");
        set_input_report_mode(
            0x32,
            "native BT selects Nunchuk report mode before relative-stick anchor checks");
        const auto* fallback_stick_acl = read_bulk_acl(
            kRequest + 0x1D00,
            20,
            "native BT relative stick uses the configured fallback before Mario control");
        passed &= expect(
            fallback_stick_acl[8] == 0xA1 &&
                fallback_stick_acl[9] == 0x32 &&
                fallback_stick_acl[10] == 0x00u &&
                fallback_stick_acl[11] == 0x0Cu &&
                fallback_stick_acl[12] == 0x66u &&
                fallback_stick_acl[13] == 0x7Eu,
            "native BT marker-timed scripts use their configured fallbacks while no marker exists");

        {
            const std::string reached_fallback_vi_text = std::to_string(
                fake_ticks / vi_period_ticks);
            ScopedEnv reached_button_fallback(
                "GALAXY_INPUT_AUTOPRESS_SCRIPT_STOP_FALLBACK_VI",
                reached_fallback_vi_text.c_str());
            fake_ticks += kTestHidReportPeriodTicks;
            const auto* fallback_stopped_acl = read_bulk_acl(
                kRequest + 0x1CA0,
                20,
                "native BT autopress script stops at its configured pre-marker fallback");
            passed &= expect(
                fallback_stopped_acl[10] == 0x00u &&
                    fallback_stopped_acl[11] == 0x00u,
                "native BT autopress fallback suppresses A+B before a live marker exists");
        }

        bool malformed_button_fallback_failed = false;
        {
            ScopedEnv malformed_button_fallback(
                "GALAXY_INPUT_AUTOPRESS_SCRIPT_STOP_FALLBACK_VI",
                "not-a-vi");
            fake_ticks += kTestHidReportPeriodTicks;
            try {
                memory.poll_native_bt_reconnect();
            } catch (const std::runtime_error& error) {
                malformed_button_fallback_failed =
                    std::string(error.what()).find(
                        "GALAXY_INPUT_AUTOPRESS_SCRIPT_STOP_FALLBACK_VI") !=
                    std::string::npos;
            }
        }
        passed &= expect(
            malformed_button_fallback_failed,
            "native BT autopress stop hard-fails a malformed fallback before Mario control");

        memory.publish_native_input_first_mario_control_vi(
            first_mario_control_vi);
        memory.publish_native_input_first_mario_control_vi(fallback_vi);
        passed &= expect(
            memory.native_input_first_mario_control_vi() ==
                std::optional<std::uint64_t>{first_mario_control_vi},
            "native input Mario-control VI publishes exactly once");
        // A malformed fallback would be a hard failure before publication.
        // Once the real marker exists it is no longer timing input at all and
        // therefore must not even be parsed.
        ScopedEnv ignored_invalid_fallback(
            "GALAXY_INPUT_STICK_SCRIPT_RELATIVE_FALLBACK_VI",
            "not-a-vi");
        ScopedEnv ignored_invalid_button_fallback(
            "GALAXY_INPUT_AUTOPRESS_SCRIPT_STOP_FALLBACK_VI",
            "not-a-vi");
        std::ostringstream marker_stop_trace;
        ScopedStreamRedirect capture_marker_stop_trace(
            std::cerr, marker_stop_trace.rdbuf());

        fake_ticks += kTestHidReportPeriodTicks;
        memory.poll_native_bt_reconnect();
        const auto* pre_marker_stick_acl = read_bulk_acl(
            kRequest + 0x1D40,
            20,
            "native BT relative stick waits for the published Mario-control VI");
        passed &= expect(
            pre_marker_stick_acl[8] == 0xA1 &&
                pre_marker_stick_acl[9] == 0x32 &&
                pre_marker_stick_acl[10] == 0x00u &&
                pre_marker_stick_acl[11] == 0x0Cu &&
                pre_marker_stick_acl[12] ==
                    expected_encrypted_neutral_nunchuk[0] &&
                pre_marker_stick_acl[13] ==
                    expected_encrypted_neutral_nunchuk[1],
            "published Mario-control VI overrides an otherwise-active fallback window");

        fake_ticks =
            first_mario_control_vi * vi_period_ticks +
            kTestHidReportPeriodTicks;
        {
            ScopedEnv disable_relative_stick_for_stop_alone(
                "GALAXY_INPUT_STICK_SCRIPT_RELATIVE_MARIO_CONTROL", "0");
            ScopedEnv disable_stick_script_for_stop_alone(
                "GALAXY_INPUT_STICK_SCRIPT", "");
            set_input_report_mode(
                0x30,
                "native BT selects core report mode for stop-alone marker observation");
            const auto* stop_alone_acl = read_bulk_acl(
                kRequest + 0x1D80,
                12,
                "native BT autopress stop observes Mario control without relative stick");
            passed &= expect(
                stop_alone_acl[8] == 0xA1 &&
                    stop_alone_acl[9] == 0x30 &&
                    stop_alone_acl[10] == 0x00u &&
                    stop_alone_acl[11] == 0x00u,
                "native BT stop-alone feature loads the marker and suppresses A+B");
        }
        fake_ticks += kTestHidReportPeriodTicks * 2u;
        set_input_report_mode(
            0x32,
            "native BT restores Nunchuk report mode after stop-alone observation");
        const auto* marker_stick_acl = read_bulk_acl(
            kRequest + 0x1DC0,
            20,
            "native BT relative stick starts from the published Mario-control VI");
        passed &= expect(
            marker_stick_acl[8] == 0xA1 &&
                marker_stick_acl[9] == 0x32 &&
                marker_stick_acl[10] == 0x00u &&
                marker_stick_acl[11] == 0x00u &&
                marker_stick_acl[12] == 0x66u &&
                marker_stick_acl[13] == 0x7Eu,
            "native BT relative stick consumes the marker while the button script stops at it");

        fake_ticks += kTestHidReportPeriodTicks * 2u;
        const auto* post_marker_acl = read_bulk_acl(
            kRequest + 0x1E00,
            20,
            "native BT autopress script stays stopped after Mario control");
        passed &= expect(
            post_marker_acl[10] == 0x00u && post_marker_acl[11] == 0x00u,
            "native BT autopress script emits no A/B after the live marker");
        const std::string stop_trace_text = marker_stop_trace.str();
        const std::string expected_stop_prefix =
            "[input-script-stop] source=mario-control marker-vi=" +
            std::to_string(first_mario_control_vi) +
            " first-suppressed-vi=";
        const std::size_t first_stop_record =
            stop_trace_text.find(expected_stop_prefix);
        passed &= expect(
            first_stop_record != std::string::npos &&
                stop_trace_text.find(
                    expected_stop_prefix,
                    first_stop_record + expected_stop_prefix.size()) ==
                    std::string::npos &&
                stop_trace_text.find(
                    " script-ab=0 guard=terminal", first_stop_record) !=
                    std::string::npos,
            "native BT autopress live stop emits exactly one epoch-consistent proof record");

        {
            ScopedEnv disable_marker_stop(
                "GALAXY_INPUT_AUTOPRESS_SCRIPT_STOP_AT_MARIO_CONTROL", "0");
            fake_ticks += kTestHidReportPeriodTicks * 2u;
            const auto* stop_disabled_acl = read_bulk_acl(
                kRequest + 0x1E40,
                20,
                "native BT disabled autopress marker stop ignores the published marker");
            passed &= expect(
                stop_disabled_acl[10] == 0x00u &&
                    stop_disabled_acl[11] == 0x0Cu,
                "native BT stop=0 leaves the long-form button script active after Mario control");
        }
    }
    {
        std::ostringstream waypoint_duration_trace;
        ScopedEnv waypoint_x("GALAXY_INPUT_POINTER2_X", "1.000");
        ScopedEnv waypoint_y("GALAXY_INPUT_POINTER2_Y", "0.000");
        const std::string waypoint_vi_value = std::to_string(
            fake_ticks /
            (galaxy::input::kNativeHidTimelineTicksPerSecond / 60u));
        ScopedEnv waypoint_vi(
            "GALAXY_INPUT_POINTER2_VI", waypoint_vi_value.c_str());
        ScopedEnv waypoint_duration("GALAXY_INPUT_POINTER2_DURATION", "1");
        ScopedStreamRedirect capture_waypoint_duration_trace(
            std::cerr, waypoint_duration_trace.rdbuf());
        set_input_report_mode(
            0x37,
            "native BT HID set-report-mode output is consumed before pointer waypoint duration check");
        const auto* active_waypoint_acl = read_bulk_acl(
            kRequest + 0x1780,
            31,
            "native BT pointer waypoint emits IR while its duration is active");
        expect_ir_bytes_published(
            active_waypoint_acl,
            15u,
            25u,
            "native BT pointer waypoint emits visible IR bytes during its duration");
        ScopedHostPointerProvider fallback_pointer(
            &centered_host_pointer_provider);
        // 100 Hz HID slots do not align one-for-one with 60 Hz VI script
        // indices. Advance two report slots so the produced deadline is in
        // the following VI, where the one-VI waypoint has expired.
        fake_ticks += kTestHidReportPeriodTicks * 2u;
        memory.poll_native_bt_reconnect();
        const auto* expired_waypoint_acl = read_bulk_acl(
            kRequest + 0x17C0,
            31,
            "native BT pointer waypoint stops overriding input after its duration");
        passed &= expect(
            expired_waypoint_acl[8] == 0xA1 &&
                expired_waypoint_acl[9] == 0x37,
            "native BT pointer waypoint expiration still emits the selected report mode");
        expect_ir_bytes_published(
            expired_waypoint_acl,
            15u,
            25u,
            "native BT pointer waypoint expiration falls back to keyboard/mouse IR dots");
        const std::string waypoint_duration_trace_text =
            waypoint_duration_trace.str();
        passed &= expect(
            waypoint_duration_trace_text.find("script=0x8000") !=
                    std::string::npos &&
                waypoint_duration_trace_text.find("ptrsrc=script") !=
                    std::string::npos,
            "native BT pointer waypoint is traced as scripted while active");
        passed &= expect(
                waypoint_duration_trace_text.find("script=0x0") !=
                    std::string::npos &&
                waypoint_duration_trace_text.find("source=keyboard_mouse") !=
                    std::string::npos &&
                waypoint_duration_trace_text.find("ptrsrc=mouse") !=
                    std::string::npos &&
                waypoint_duration_trace_text.find("ir=1") !=
                    std::string::npos,
            "native BT pointer waypoint duration expires back to active keyboard/mouse IR input");
        if (waypoint_duration_trace_text.find("FAILED:") != std::string::npos) {
            std::cout << waypoint_duration_trace_text;
        }
    }
    {
        std::ostringstream host_pointer_trace;
        ScopedHostPointerProvider host_pointer(
            &centered_host_pointer_provider);
        ScopedStreamRedirect capture_host_pointer_trace(
            std::cerr, host_pointer_trace.rdbuf());
        set_input_report_mode(
            0x37,
            "native BT HID set-report-mode output is consumed before host pointer check");
        const auto* host_pointer_acl = read_bulk_acl(
            kRequest + 0x1800,
            31,
            "native BT host pointer emits IR from renderer-owned coordinates");
        passed &= expect(
            host_pointer_acl[8] == 0xA1 && host_pointer_acl[9] == 0x37,
            "native BT host pointer keeps IR-bearing report mode");
        expect_ir_bytes_published(
            host_pointer_acl,
            15u,
            25u,
            "native BT host pointer publishes visible IR bytes");
        const std::string host_pointer_trace_text =
            host_pointer_trace.str();
        passed &= expect(
            host_pointer_trace_text.find("ptrsrc=mouse") !=
                    std::string::npos &&
                host_pointer_trace_text.find("pointer=(0,0)") !=
                    std::string::npos &&
                host_pointer_trace_text.find(
                    "hostptr=(1,1,1,320,240,641,481)") !=
                    std::string::npos &&
                host_pointer_trace_text.find(
                    " host-domain=absolute-position host-seq=") !=
                    std::string::npos,
            "native BT host pointer trace records mouse-backed IR with its explicit absolute-position sequence domain");
        if (host_pointer_trace_text.find("FAILED:") != std::string::npos) {
            std::cerr << host_pointer_trace_text;
        }
    }
    {
        passed &= expect(
            galaxy::host::host_pointer_sequence_domain_name(
                galaxy::host::HostPointerSequenceDomain::None) == "none" &&
                galaxy::host::host_pointer_sequence_domain_name(
                    galaxy::host::HostPointerSequenceDomain::AbsolutePosition) ==
                    "absolute-position" &&
                galaxy::host::host_pointer_sequence_domain_name(
                    galaxy::host::HostPointerSequenceDomain::ButtonTransition) ==
                    "button-transition",
            "host pointer producer domains have stable diagnostic spellings");
        galaxy::host::HostPointerEventRing transitions;
        ScopedHostPointerProvider host_pointer(
            &timed_host_pointer_provider);
        ScopedHostPointerTransitionProvider transition_provider(transitions);
        g_timed_host_pointer = {};
        g_timed_host_pointer.window_focused = true;
        g_timed_host_pointer.inside_client = true;
        g_timed_host_pointer.absolute_valid = true;
        g_timed_host_pointer.client_x = 520;
        g_timed_host_pointer.client_y = 240;
        g_timed_host_pointer.client_width = 641;
        g_timed_host_pointer.client_height = 481;
        // This synthetic provider owns no native game window. Valid IR and
        // queued transition coordinates must keep their identities, while
        // the production button-ownership check must withhold Wii A/B.
        g_timed_host_pointer.debug_window = 0u;
        // Deliberately collide with the first transition's numeric sequence.
        // The two independent producer namespaces must remain distinguishable.
        g_timed_host_pointer.absolute_sequence = 1u;
        g_timed_host_pointer.absolute_acquired_ms = 90'000u;
        const galaxy::host::HostPointerCoordinates down_coordinates{
            true, true, 120, 240, 641, 481};
        const galaxy::host::HostPointerCoordinates up_coordinates{
            true, true, 160, 240, 641, 481};
        constexpr std::uint64_t kQueuedDownAcquiredMs = 70'001u;
        constexpr std::uint64_t kQueuedUpAcquiredMs = 70'002u;
        NativeVirtualInputDeliveryCapture transition_delivery_capture{};
        transition_delivery_capture.address_space = &memory;
        memory.set_native_virtual_input_acl_delivery_callback(
            &capture_native_virtual_input_delivery,
            &transition_delivery_capture);
        passed &= expect(
            transitions.accept_button_transition(
                galaxy::host::HostPointerButton::Left,
                true,
                down_coordinates,
                kQueuedDownAcquiredMs) &&
                transitions.accept_button_transition(
                    galaxy::host::HostPointerButton::Left,
                    false,
                    up_coordinates,
                    kQueuedUpAcquiredMs),
            "native BT pointer journal records a complete click between HID samples");
        fake_ticks += kTestHidReportPeriodTicks;
        memory.poll_native_bt_reconnect();
        passed &= expect(
            TestAccess::queued_virtual_host_pointer_sequence(memory) == 1u &&
                TestAccess::queued_virtual_host_pointer_sequence_domain(
                    memory) ==
                    galaxy::host::HostPointerSequenceDomain::ButtonTransition &&
                TestAccess::queued_virtual_host_pointer_acquired_ms(memory) ==
                    kQueuedDownAcquiredMs &&
                TestAccess::queued_virtual_host_pointer_acquired_ms(memory) !=
                    g_timed_host_pointer.absolute_acquired_ms,
            "native BT queued click binds the first transition domain/sequence/timestamp instead of the colliding newer absolute sample");
        const auto* click_down_acl = read_bulk_acl(
            kRequest + 0x18C0,
            31,
            "native BT pointer journal preserves a sub-sample left-down transition");
        const auto basic_ir_left_x = [](const std::uint8_t* acl) {
            return static_cast<std::uint16_t>(
                acl[15] |
                (static_cast<std::uint16_t>((acl[17] >> 4u) & 0x03u)
                 << 8u));
        };
        const std::uint16_t click_down_left_x =
            basic_ir_left_x(click_down_acl);
        passed &= expect(
            (click_down_acl[11] & 0x0Cu) == 0u,
            "native BT queued click without an owned game window cannot inject Wii A/B");
        passed &= expect(
            transition_delivery_capture.count == 1u &&
                transition_delivery_capture.identities[0]
                        .host_pointer_acquired_ms ==
                    kQueuedDownAcquiredMs &&
                transition_delivery_capture.identities[0]
                        .host_pointer_sequence == 1u &&
                transition_delivery_capture.identities[0]
                        .host_pointer_sequence_domain ==
                    galaxy::host::HostPointerSequenceDomain::ButtonTransition,
            "native BT pointer journal emits the queued left-down edge with its exact transition identity");
        fake_ticks += kTestHidReportPeriodTicks;
        memory.poll_native_bt_reconnect();
        passed &= expect(
            TestAccess::queued_virtual_host_pointer_sequence(memory) == 2u &&
                TestAccess::queued_virtual_host_pointer_sequence_domain(
                    memory) ==
                    galaxy::host::HostPointerSequenceDomain::ButtonTransition &&
                TestAccess::queued_virtual_host_pointer_acquired_ms(memory) ==
                    kQueuedUpAcquiredMs,
            "native BT queued release keeps its independent transition identity");
        const auto* click_up_acl = read_bulk_acl(
            kRequest + 0x1900,
            31,
            "native BT pointer journal preserves the matching click release");
        const std::uint16_t click_up_left_x = basic_ir_left_x(click_up_acl);
        passed &= expect(
            (click_up_acl[11] & 0x08u) == 0u &&
                transition_delivery_capture.count == 2u &&
                transition_delivery_capture.identities[1]
                        .host_pointer_acquired_ms ==
                    kQueuedUpAcquiredMs &&
                transition_delivery_capture.identities[1]
                        .host_pointer_sequence == 2u &&
                transition_delivery_capture.identities[1]
                        .host_pointer_sequence_domain ==
                    galaxy::host::HostPointerSequenceDomain::ButtonTransition,
            "native BT pointer journal emits the queued left-up edge with its exact transition identity");

        fake_ticks += kTestHidReportPeriodTicks;
        memory.poll_native_bt_reconnect();
        passed &= expect(
            TestAccess::queued_virtual_host_pointer_sequence(memory) == 1u &&
                TestAccess::queued_virtual_host_pointer_sequence_domain(
                    memory) ==
                    galaxy::host::HostPointerSequenceDomain::AbsolutePosition &&
                TestAccess::queued_virtual_host_pointer_acquired_ms(memory) ==
                    g_timed_host_pointer.absolute_acquired_ms,
            "native BT restores the pending absolute publication after draining transition identities even when its numeric sequence collides");
        const auto* absolute_acl = read_bulk_acl(
            kRequest + 0x1940,
            31,
            "native BT pointer journal returns to the renderer absolute sample");
        const std::uint16_t absolute_left_x = basic_ir_left_x(absolute_acl);
        passed &= expect(
            transition_delivery_capture.count == 3u &&
                transition_delivery_capture.identities[2]
                        .host_pointer_sequence == 1u &&
                transition_delivery_capture.identities[2]
                        .host_pointer_sequence_domain ==
                    galaxy::host::HostPointerSequenceDomain::AbsolutePosition &&
                transition_delivery_capture.identities[2]
                        .host_pointer_acquired_ms ==
                    g_timed_host_pointer.absolute_acquired_ms &&
                click_down_left_x > click_up_left_x &&
                click_up_left_x > absolute_left_x,
            "native BT keeps edge A, edge B, and absolute sample C coordinates bound to their own identities in order");

        passed &= expect(transitions.accept_button_transition(
            galaxy::host::HostPointerButton::Left, true, down_coordinates, 80'001u),
            "focus fixture queues an old down before cancellation");
        transitions.invalidate_focus(up_coordinates, 80'002u);
        passed &= expect(transitions.accept_button_transition(
            galaxy::host::HostPointerButton::Left, true, down_coordinates, 80'003u),
            "focus fixture queues a new-epoch down after cancellation");
        fake_ticks += kTestHidReportPeriodTicks;
        memory.poll_native_bt_reconnect();
        passed &= expect(
            TestAccess::queued_virtual_host_pointer_sequence(memory) == 4u &&
            TestAccess::queued_virtual_host_pointer_acquired_ms(memory) == 80'002u,
            "native BT retires old-focus down before emitting current cancellation");
        (void)read_bulk_acl(kRequest + 0x1980u, 31u,
            "native BT current-focus cancellation delivers without replaying a stale click");
        fake_ticks += kTestHidReportPeriodTicks;
        memory.poll_native_bt_reconnect();
        passed &= expect(
            TestAccess::queued_virtual_host_pointer_sequence(memory) == 5u &&
            TestAccess::queued_virtual_host_pointer_acquired_ms(memory) == 80'003u,
            "native BT preserves the next valid new-focus edge after stale retirement");
        (void)read_bulk_acl(kRequest + 0x19C0u, 31u,
            "native BT valid new-focus down reaches IOS");
        memory.set_native_virtual_input_acl_delivery_callback(nullptr, nullptr);
    }
    {
        std::ostringstream scaled_host_pointer_trace;
        ScopedHostPointerProvider host_pointer(
            &right_of_center_host_pointer_provider);
        ScopedStreamRedirect capture_scaled_host_pointer_trace(
            std::cerr, scaled_host_pointer_trace.rdbuf());
        set_input_report_mode(
            0x37,
            "native BT HID set-report-mode output is consumed before scaled host pointer check");
        const auto* scaled_host_pointer_acl = read_bulk_acl(
            kRequest + 0x1820,
            31,
            "native BT host pointer applies calibrated absolute mouse scale");
        passed &= expect(
            scaled_host_pointer_acl[8] == 0xA1 &&
                scaled_host_pointer_acl[9] == 0x37,
            "native BT scaled host pointer keeps IR-bearing report mode");
        expect_ir_bytes_published(
            scaled_host_pointer_acl,
            15u,
            25u,
            "native BT scaled host pointer publishes visible IR bytes");
        const std::string scaled_host_pointer_trace_text =
            scaled_host_pointer_trace.str();
        passed &= expect(
            scaled_host_pointer_trace_text.find("ptrsrc=mouse") !=
                    std::string::npos &&
                scaled_host_pointer_trace_text.find("pointer=(0.095,0)") !=
                    std::string::npos &&
                scaled_host_pointer_trace_text.find(
                    "hostptr=(1,1,1,400,240,641,481)") !=
                    std::string::npos,
            "native BT host pointer trace records calibrated center mouse-backed IR");
        if (scaled_host_pointer_trace_text.find("FAILED:") !=
            std::string::npos) {
            std::cerr << scaled_host_pointer_trace_text;
        }
    }
    {
        std::ostringstream edge_host_pointer_trace;
        ScopedHostPointerProvider host_pointer(
            &right_edge_host_pointer_provider);
        ScopedStreamRedirect capture_edge_host_pointer_trace(
            std::cerr, edge_host_pointer_trace.rdbuf());
        set_input_report_mode(
            0x37,
            "native BT HID set-report-mode output is consumed before edge-expanded host pointer check");
        const auto* edge_host_pointer_acl = read_bulk_acl(
            kRequest + 0x1830,
            31,
            "native BT host pointer expands calibrated mouse scale at screen edge");
        passed &= expect(
            edge_host_pointer_acl[8] == 0xA1 &&
                edge_host_pointer_acl[9] == 0x37,
            "native BT edge-expanded host pointer keeps IR-bearing report mode");
        expect_ir_bytes_published(
            edge_host_pointer_acl,
            15u,
            25u,
            "native BT edge-expanded host pointer publishes visible IR bytes");
        const std::string edge_host_pointer_trace_text =
            edge_host_pointer_trace.str();
        passed &= expect(
            edge_host_pointer_trace_text.find("ptrsrc=mouse") !=
                    std::string::npos &&
                edge_host_pointer_trace_text.find("pointer=(1,0)") !=
                    std::string::npos &&
                edge_host_pointer_trace_text.find(
                    "hostptr=(1,1,1,640,240,641,481)") !=
                    std::string::npos,
            "native BT host pointer trace records full edge reach after center calibration");
        if (edge_host_pointer_trace_text.find("FAILED:") !=
            std::string::npos) {
            std::cerr << edge_host_pointer_trace_text;
        }
    }
    {
        std::string long_autopress_script = "0:999999:A+B";
        for (std::uint32_t vi = 1000; long_autopress_script.size() < 5000u;
             vi += 30u) {
            long_autopress_script +=
                ";" + std::to_string(vi) + ":1:A";
        }
        ScopedEnv scripted_buttons(
            "GALAXY_INPUT_AUTOPRESS_SCRIPT",
            long_autopress_script.c_str());
        ScopedEnv disable_script_stop(
            "GALAXY_INPUT_AUTOPRESS_SCRIPT_STOP_AT_MARIO_CONTROL", "0");
        set_input_report_mode(
            0x30,
            "native BT HID set-report-mode output is consumed before long autopress script check");
        const auto* long_script_acl = read_bulk_acl(
            kRequest + 0x1840,
            12,
            "native BT long autopress script still emits core button input");
        passed &= expect(
            long_script_acl[8] == 0xA1 && long_script_acl[9] == 0x30,
            "native BT long autopress script preserves requested report mode 0x30");
        passed &= expect(
            long_script_acl[10] == 0x00 && long_script_acl[11] == 0x0C,
            "native BT long autopress script is accepted and encoded as A+B");
    }
    ScopedEnv short_mouse_ir_hold("GALAXY_MOUSE_IR_HOLD_MS", "40");
    {
        constexpr std::uint64_t kFreshPointerAcquiredMs = 100'000u;
        constexpr std::uint64_t kFreshPointerSequence = 100u;
        ScopedHostPointerClock pointer_clock(1'000u);
        ScopedHostPointerProvider host_pointer(
            &timed_host_pointer_provider);
        std::ostringstream renderer_grace_trace;
        ScopedStreamRedirect capture_renderer_grace_trace(
            std::cerr, renderer_grace_trace.rdbuf());

        g_timed_host_pointer = {};
        g_timed_host_pointer.window_focused = true;
        g_timed_host_pointer.inside_client = true;
        g_timed_host_pointer.absolute_valid = true;
        g_timed_host_pointer.client_x = 400;
        g_timed_host_pointer.client_y = 240;
        g_timed_host_pointer.client_width = 641;
        g_timed_host_pointer.client_height = 481;
        g_timed_host_pointer.absolute_sequence = kFreshPointerSequence;
        g_timed_host_pointer.absolute_acquired_ms =
            kFreshPointerAcquiredMs;
        set_input_report_mode(
            0x37,
            "native BT HID set-report-mode output is consumed before renderer visibility ownership check");
        passed &= expect(
            TestAccess::queued_virtual_host_pointer_sequence(memory) ==
                    kFreshPointerSequence &&
                TestAccess::queued_virtual_host_pointer_acquired_ms(memory) ==
                    kFreshPointerAcquiredMs,
            "native BT fresh renderer sample keeps its absolute sequence/timestamp identity");
        const auto* fresh_inside_acl = read_bulk_acl(
            kRequest + 0x1880,
            31,
            "native BT fresh renderer sample publishes IR");
        expect_ir_bytes_published(
            fresh_inside_acl,
            15u,
            25u,
            "native BT fresh renderer sample publishes visible IR bytes");

        // Repeated focused inside samples keep the exact same identity. HID
        // polling may consume that point, but must not restart the host's
        // invalid-sample uncertainty clock from an unchanged publication.
        pointer_clock.set(1'030u);
        fake_ticks += kTestHidReportPeriodTicks;
        memory.poll_native_bt_reconnect();
        passed &= expect(
            TestAccess::queued_virtual_host_pointer_sequence(memory) ==
                    kFreshPointerSequence &&
                TestAccess::queued_virtual_host_pointer_acquired_ms(memory) ==
                    kFreshPointerAcquiredMs,
            "native BT renderer-held sample preserves the original absolute identity");
        const auto* renderer_held_acl = read_bulk_acl(
            kRequest + 0x18A0,
            31,
            "native BT consumes an unchanged focused inside point without changing identity");
        expect_ir_bytes_published(
            renderer_held_acl,
            15u,
            25u,
            "native BT keeps a focused known-inside point visible");

        // Focus is not an IR-coordinate ownership gate. The global cursor is
        // still geometrically over the game client, so an unfocused window
        // must preserve the same lightweight pointing coordinate without
        // synthesizing any keyboard or mouse-button ownership.
        pointer_clock.set(1'035u);
        g_timed_host_pointer.window_focused = false;
        g_timed_host_pointer.inside_client = true;
        g_timed_host_pointer.left_button = true;
        g_timed_host_pointer.right_button = true;
        fake_ticks += kTestHidReportPeriodTicks;
        memory.poll_native_bt_reconnect();
        const auto* unfocused_inside_acl = read_bulk_acl(
            kRequest + 0x18C0,
            31,
            "native BT tracks an unfocused cursor inside the game client");
        expect_ir_bytes_published(
            unfocused_inside_acl,
            15u,
            25u,
            "native BT keeps unfocused in-client IR geometry visible");
        passed &= expect(
            unfocused_inside_acl[10] == 0x00u &&
                unfocused_inside_acl[11] == 0x00u,
            "native BT keeps unfocused mouse buttons out of the Wii report");

        // Leaving the client is authoritative even when the last inside point
        // is only 41 ms old. Known-outside geometry may not use uncertainty
        // hold merely because the window is also unfocused.
        pointer_clock.set(1'041u);
        g_timed_host_pointer.window_focused = false;
        g_timed_host_pointer.inside_client = false;
        g_timed_host_pointer.left_button = false;
        g_timed_host_pointer.right_button = false;
        fake_ticks += kTestHidReportPeriodTicks;
        memory.poll_native_bt_reconnect();
        const auto* renderer_drop_acl = read_bulk_acl(
            kRequest + 0x18E0,
            31,
            "native BT drops IR immediately after leaving the client");
        passed &= expect(
            renderer_drop_acl[8] == 0xA1 && renderer_drop_acl[9] == 0x37,
            "native BT renderer dropout keeps the requested IR-bearing report shape");
        expect_basic_ir_bytes_hidden(
            renderer_drop_acl,
            "native BT does not apply invalid-sample hold to known-outside geometry");

        // A real focused reentry has a new renderer sequence and must
        // immediately publish its new coordinate and acquisition identity.
        pointer_clock.set(1'050u);
        g_timed_host_pointer.window_focused = true;
        g_timed_host_pointer.inside_client = true;
        g_timed_host_pointer.client_x = 460;
        g_timed_host_pointer.absolute_sequence = 101u;
        g_timed_host_pointer.absolute_acquired_ms = 100'050u;
        fake_ticks += kTestHidReportPeriodTicks;
        memory.poll_native_bt_reconnect();
        passed &= expect(
            TestAccess::queued_virtual_host_pointer_sequence(memory) == 101u &&
                TestAccess::queued_virtual_host_pointer_acquired_ms(memory) ==
                    100'050u,
            "native BT fresh reentry publishes one coherent new renderer identity");
        const auto* reentry_acl = read_bulk_acl(
            kRequest + 0x1920,
            31,
            "native BT fresh renderer reentry restores IR");
        expect_ir_bytes_published(
            reentry_acl,
            15u,
            25u,
            "native BT fresh renderer reentry publishes its new coordinate");

        // Resize is also a fresh renderer publication. A later rejected stale
        // center poll keeps this identity and therefore cannot prolong it.
        pointer_clock.set(1'060u);
        g_timed_host_pointer.client_x = 560;
        g_timed_host_pointer.client_y = 300;
        g_timed_host_pointer.client_width = 801;
        g_timed_host_pointer.client_height = 601;
        g_timed_host_pointer.absolute_sequence = 102u;
        g_timed_host_pointer.absolute_acquired_ms = 100'060u;
        g_timed_host_pointer.debug_flags = 0u;
        fake_ticks += kTestHidReportPeriodTicks;
        memory.poll_native_bt_reconnect();
        const auto* resized_acl = read_bulk_acl(
            kRequest + 0x1960,
            31,
            "native BT renderer resize publication remains visible");
        expect_ir_bytes_published(
            resized_acl,
            15u,
            25u,
            "native BT renderer resize publishes visible IR bytes");

        pointer_clock.set(1'085u);
        g_timed_host_pointer.debug_flags = 1u << 12u;
        fake_ticks += kTestHidReportPeriodTicks;
        memory.poll_native_bt_reconnect();
        passed &= expect(
            TestAccess::queued_virtual_host_pointer_sequence(memory) == 102u &&
                TestAccess::queued_virtual_host_pointer_acquired_ms(memory) ==
                    100'060u,
            "native BT rejected stale-center poll preserves resize identity");
        const auto* stale_center_acl = read_bulk_acl(
            kRequest + 0x19A0,
            31,
            "native BT keeps renderer-selected coordinate after stale-center rejection");
        expect_ir_bytes_published(
            stale_center_acl,
            15u,
            25u,
            "native BT keeps renderer-selected IR visible after stale-center rejection");

        pointer_clock.set(1'101u);
        g_timed_host_pointer.window_focused = false;
        g_timed_host_pointer.inside_client = false;
        fake_ticks += kTestHidReportPeriodTicks;
        memory.poll_native_bt_reconnect();
        const auto* post_resize_drop_acl = read_bulk_acl(
            kRequest + 0x19E0,
            31,
            "native BT stale-center rejection cannot override known-outside geometry");
        expect_basic_ir_bytes_hidden(
            post_resize_drop_acl,
            "native BT known-outside geometry after stale-center rejection drops immediately");

        const std::string renderer_grace_trace_text =
            renderer_grace_trace.str();
        passed &= expect(
            renderer_grace_trace_text.find("ptrsrc=mouse") !=
                    std::string::npos &&
                renderer_grace_trace_text.find("ptrsrc=none") !=
                    std::string::npos &&
                renderer_grace_trace_text.find("hostptr=(1,1,0,") !=
                    std::string::npos,
            "native BT trace distinguishes in-client availability from geometric dropout");
        if (renderer_grace_trace_text.find("FAILED:") !=
            std::string::npos) {
            std::cerr << renderer_grace_trace_text;
        }
    }
    {
        const galaxy::RuntimeSettings saved_host_pointer_settings =
            galaxy::get_runtime_settings();
        galaxy::RuntimeSettings keyboard_host_pointer_settings =
            saved_host_pointer_settings;
        keyboard_host_pointer_settings.input_mode =
            galaxy::RuntimeInputMode::KeyboardMouse;
        galaxy::set_runtime_settings(keyboard_host_pointer_settings);

        ScopedHostPointerProvider host_pointer(
            &centered_host_pointer_provider);
        set_input_report_mode(
            0x37,
            "native BT queues a keyboard/mouse report before the runtime source-transition check");
        const galaxy::RuntimeInputModeState keyboard_input_state =
            galaxy::get_runtime_input_mode_state();
        passed &= expect(
            TestAccess::virtual_input_queued(memory) &&
                TestAccess::queued_virtual_input_mode_generation(memory) ==
                    keyboard_input_state.generation,
            "native BT queued report owns its keyboard/mouse source generation");

        galaxy::RuntimeSettings controller_host_pointer_settings =
            keyboard_host_pointer_settings;
        controller_host_pointer_settings.input_mode =
            galaxy::RuntimeInputMode::Controller;
        galaxy::set_runtime_settings(controller_host_pointer_settings);
        const galaxy::RuntimeInputModeState controller_input_state =
            galaxy::get_runtime_input_mode_state();
        const galaxy::input::NativeHidCadenceStats before_source_change =
            memory.native_hid_cadence_stats();
        passed &= expect(
            controller_input_state.generation !=
                keyboard_input_state.generation,
            "runtime input source transition advances its coherent generation");

        std::ostringstream controller_host_pointer_trace;
        // The production trace is intentionally cadence-throttled. Make this
        // exact unit-test sample interesting so the assertion never depends on
        // how many valid samples earlier test cases happened to produce.
        ScopedEnv force_controller_trace_sample(
            "GALAXY_INPUT_AUTOPRESS_SCRIPT", "0:1000000:A");
        ScopedStreamRedirect capture_controller_host_pointer_trace(
            std::cerr, controller_host_pointer_trace.rdbuf());
        // Exercise the most demanding ordering directly: the guest posts its
        // bulk-in read after the settings change but before the next periodic
        // device poll. The read must remain pending and the old-source FIFO
        // entry must be invalidated synchronously.
        submit_bt_read(kRequest + 0x1840, 1, 0x82, kBuffer, 64);
        passed &= expect(
            !ios_reply_available(guest_memory) &&
                !TestAccess::virtual_input_queued(memory) &&
                memory.native_hid_cadence_stats().mode_invalidations ==
                    before_source_change.mode_invalidations + 1u,
            "native BT direct bulk-in rejects the queued old-source report");

        fake_ticks += kTestHidReportPeriodTicks;
        memory.poll_native_bt_reconnect();
        passed &= expect(
            expect_ios_reply(
                memory,
                guest_memory,
                kRequest + 0x1840,
                31,
                7,
                "native BT pending bulk-in receives the new controller-source report"),
            "native BT pending bulk-in receives the new controller-source report");
        const auto* controller_host_pointer_acl =
            reinterpret_cast<const std::uint8_t*>(
                memory.pointer(kBuffer, 31));
        acknowledge_ios_reply(guest_memory);
        passed &= expect(
            controller_host_pointer_acl[8] == 0xA1 &&
                controller_host_pointer_acl[9] == 0x37,
            "native BT controller host pointer keeps IR-bearing report mode");
        expect_ir_bytes_published(
            controller_host_pointer_acl,
            15u,
            25u,
            "native BT controller host pointer publishes visible IR bytes");
        galaxy::set_runtime_settings(saved_host_pointer_settings);

        const std::string controller_host_pointer_trace_text =
            controller_host_pointer_trace.str();
        passed &= expect(
            controller_host_pointer_trace_text.find("source=keyboard_mouse") ==
                    std::string::npos &&
                (controller_host_pointer_trace_text.find("source=controller") !=
                     std::string::npos ||
                 controller_host_pointer_trace_text.find(
                     "source=controller_disconnected") != std::string::npos) &&
                controller_host_pointer_trace_text.find("ptrsrc=mouse") !=
                    std::string::npos &&
                controller_host_pointer_trace_text.find(
                    "hostptr=(1,1,1,320,240,641,481)") !=
                    std::string::npos &&
                controller_host_pointer_trace_text.find("ir=1") !=
                    std::string::npos,
            "native BT controller mode traces live mouse-backed IR without switching to keyboard/mouse source");
        if (controller_host_pointer_trace_text.find("FAILED:") !=
            std::string::npos) {
            std::cerr << controller_host_pointer_trace_text;
        }
    }
    {
        const galaxy::RuntimeSettings saved_real_transition_settings =
            galaxy::get_runtime_settings();
        galaxy::RuntimeSettings virtual_source_settings =
            saved_real_transition_settings;
        virtual_source_settings.input_mode =
            galaxy::RuntimeInputMode::KeyboardMouse;
        galaxy::set_runtime_settings(virtual_source_settings);

        ScopedHostPointerProvider host_pointer(
            &centered_host_pointer_provider);
        set_input_report_mode(
            0x37,
            "native BT queues a virtual report before real-Wii-Remote selection");
        const galaxy::RuntimeInputModeState virtual_source_state =
            galaxy::get_runtime_input_mode_state();
        passed &= expect(
            TestAccess::virtual_input_queued(memory) &&
                TestAccess::queued_virtual_input_mode_generation(memory) ==
                    virtual_source_state.generation,
            "native BT pre-real-source report owns its virtual source generation");

        galaxy::RuntimeSettings real_source_settings =
            virtual_source_settings;
        real_source_settings.input_mode =
            galaxy::RuntimeInputMode::RealWiimote;
        galaxy::set_runtime_settings(real_source_settings);
        const galaxy::input::NativeHidCadenceStats before_real_source =
            memory.native_hid_cadence_stats();

        // A bulk-in arriving before the host event pump must not leak one last
        // virtual report into a session that now belongs to physical HID. It
        // must also not open the physical device on the IOS request thread.
        submit_bt_read(kRequest + 0x1860, 1, 0x82, kBuffer, 64);
        passed &= expect(
            !ios_reply_available(guest_memory) &&
                !TestAccess::virtual_input_queued(memory) &&
                memory.native_hid_cadence_stats().mode_invalidations ==
                    before_real_source.mode_invalidations + 1u &&
                !TestAccess::real_input_worker_started(memory),
            "native BT real-source selection rejects queued virtual bytes without starting physical HID on bulk-in");

        // Return to a virtual source without polling while RealWiimote owns
        // the setting. The original guest read remains pending and receives
        // only a newly acquired report from the restored source generation.
        galaxy::set_runtime_settings(virtual_source_settings);
        fake_ticks += kTestHidReportPeriodTicks;
        memory.poll_native_bt_reconnect();
        passed &= expect(
            expect_ios_reply(
                memory,
                guest_memory,
                kRequest + 0x1860,
                31,
                7,
                "native BT pending read resumes with a fresh restored-virtual-source report"),
            "native BT pending read resumes with a fresh restored-virtual-source report");
        const auto* restored_virtual_acl =
            reinterpret_cast<const std::uint8_t*>(
                memory.pointer(kBuffer, 31));
        passed &= expect(
            restored_virtual_acl[8] == 0xA1 &&
                restored_virtual_acl[9] == 0x37 &&
                !TestAccess::real_input_worker_started(memory),
            "native BT restored virtual source delivers no physical-HID bytes");
        acknowledge_ios_reply(guest_memory);
        galaxy::set_runtime_settings(saved_real_transition_settings);
    }
    const std::array<std::byte, 1> cadence_extended_camera_mode{
        std::byte{0x03}};
    const auto cadence_extended_camera_write = wiimote_memory_write_report(
        0x04, 0xB00033, cadence_extended_camera_mode);
    submit_hid_output(
        cadence_extended_camera_write,
        "native BT selects extended camera layout before exact mode-transition cadence checks");
    expect_ack(kRequest + 0xE40, 0x16);

    const galaxy::input::NativeHidCadenceStats pacing_before =
        memory.native_hid_cadence_stats();
    const std::uint64_t pacing_arm_notifications_before =
        cadence_arm_observation.notifications;
    set_input_report_mode(
        0x30,
        "native BT HID set-report-mode output is consumed before pacing check");
    passed &= expect(
        cadence_arm_observation.notifications ==
                pacing_arm_notifications_before + 1u &&
            cadence_arm_observation.next_unconsumed_sequence ==
                pacing_before.last_produced_sequence + 1u &&
            cadence_arm_observation.next_unconsumed_deadline_ticks ==
                fake_ticks &&
            cadence_arm_observation.period_ticks ==
                kTestHidReportPeriodTicks,
        "native BT report-mode rearm publishes its still-unconsumed origin deadline");
    memory.poll_native_bt_reconnect();
    const galaxy::input::NativeHidCadenceStats pacing_queued =
        memory.native_hid_cadence_stats();
    passed &= expect(
        TestAccess::virtual_input_queued(memory) &&
            pacing_queued.produced_samples ==
                pacing_before.produced_samples + 1u &&
            pacing_queued.delivered_samples ==
                pacing_before.delivered_samples,
        "native BT device produces its first sample without a pending USB read");
    const auto* paced_first_input = read_bulk_acl(
        kRequest + 0xE80,
        12,
        "native BT first paced HID input report arrives immediately");
    passed &= expect(
        paced_first_input[8] == 0xA1 && paced_first_input[9] == 0x30,
        "native BT first paced HID input report preserves requested mode 0x30");
    const std::uint64_t produced_before_consumer_only_read =
        memory.native_hid_cadence_stats().produced_samples;
    submit_bt_read(kRequest + 0xE90, 1, 0x82, kBuffer, 64);
    passed &= expect(
        !ios_reply_available(guest_memory) &&
            memory.native_hid_cadence_stats().produced_samples ==
                produced_before_consumer_only_read,
        "native BT bulk read is consumer-only and waits for a device deadline");
    const std::array<std::byte, 1> paced_ack_write_data{std::byte{0x08}};
    const auto paced_ack_request =
        wiimote_memory_write_report(0x04, 0xB00030, paced_ack_write_data);
    const std::vector<std::byte> paced_ack_packet =
        acl_packet(0x0040, paced_ack_request);
    const std::uint32_t paced_ack_completed_event =
        arm_acl_completed_packet_read();
    submit_acl_out(paced_ack_packet);
    passed &= expect(
        expect_ios_reply(
            memory,
            guest_memory,
            kRequest + 0xE90,
            14,
            7,
            "native BT pending paced read receives memory-write ACK first"),
        "native BT pending paced read receives memory-write ACK first");
    const auto* paced_ack =
        reinterpret_cast<const std::uint8_t*>(memory.pointer(kBuffer, 14));
    passed &= expect(
        paced_ack[8] == 0xA1 && paced_ack[9] == 0x22 &&
            paced_ack[12] == 0x16 && paced_ack[13] == 0x00,
        "native BT memory-write ACK bypasses the native input device FIFO");
    acknowledge_ios_reply(guest_memory);
    passed &= expect(
        expect_ios_reply(
            memory,
            guest_memory,
            kRequest,
            static_cast<std::uint32_t>(paced_ack_packet.size()),
            7,
            "native BT paced memory-write output transfer completes"),
        "native BT paced memory-write output transfer completes");
    acknowledge_ios_reply(guest_memory);
    expect_acl_completed_packet(
        paced_ack_completed_event,
        "native BT paced memory-write frees a controller ACL buffer");

    const galaxy::input::NativeHidCadenceStats queued_priority_before =
        memory.native_hid_cadence_stats();
    fake_ticks += kTestHidReportPeriodTicks;
    memory.poll_native_bt_reconnect();
    submit_bt_read(kRequest + 0xEA0, 1, 0x82, kBuffer, 1);
    passed &= expect(
        expect_ios_reply(
            memory,
            guest_memory,
            kRequest + 0xEA0,
            static_cast<std::uint32_t>(-23),
            7,
            "native BT undersized bulk read leaves virtual input queued"),
        "native BT undersized bulk read leaves virtual input queued");
    acknowledge_ios_reply(guest_memory);
    const galaxy::input::NativeHidCadenceStats queued_priority_sampled =
        memory.native_hid_cadence_stats();
    passed &= expect(
        TestAccess::virtual_input_queued(memory) &&
            queued_priority_sampled.produced_samples ==
                queued_priority_before.produced_samples + 1u,
        "native BT device samples on schedule even when the USB read is undersized");
    const std::array<std::byte, 1> queued_ack_write_data{std::byte{0x08}};
    const auto queued_ack_request =
        wiimote_memory_write_report(0x04, 0xB00030, queued_ack_write_data);
    const std::vector<std::byte> queued_ack_packet =
        acl_packet(0x0040, queued_ack_request);
    const std::uint32_t queued_ack_completed_event =
        arm_acl_completed_packet_read();
    submit_acl_out(queued_ack_packet);
    passed &= expect(
        TestAccess::virtual_input_queued(memory) &&
            memory.native_hid_cadence_stats().produced_samples ==
                queued_priority_sampled.produced_samples,
        "native BT command ACK priority does not consume or resample queued device input");
    passed &= expect(
        expect_ios_reply(
            memory,
            guest_memory,
            kRequest,
            static_cast<std::uint32_t>(queued_ack_packet.size()),
            7,
            "native BT queued memory-write output transfer completes"),
        "native BT queued memory-write output transfer completes");
    acknowledge_ios_reply(guest_memory);
    expect_acl_completed_packet(
        queued_ack_completed_event,
        "native BT queued memory-write frees a controller ACL buffer");
    const auto* queued_ack =
        read_bulk_acl(
            kRequest + 0xEB0,
            14,
            "native BT memory-write ACK outranks queued virtual input");
    passed &= expect(
        queued_ack[8] == 0xA1 && queued_ack[9] == 0x22 &&
            queued_ack[12] == 0x16 && queued_ack[13] == 0x00,
        "native BT memory-write ACK is delivered before queued virtual input");
    const auto* queued_input =
        read_bulk_acl(
            kRequest + 0xEC0,
            12,
            "native BT queued virtual input follows prioritized ACK");
    passed &= expect(
        queued_input[8] == 0xA1 && queued_input[9] == 0x30,
        "native BT queued virtual input preserves requested mode 0x30 behind ACK");

    const std::uint64_t cadence_origin_ticks = fake_ticks;
    set_input_report_mode(
        0x30,
        "native BT HID set-report-mode output is consumed before immutable cadence check");
    const auto* cadence_origin_input = read_bulk_acl(
        kRequest + 0x1A00,
        12,
        "native BT cadence emits the first report immediately at its reporting origin");
    passed &= expect(
        cadence_origin_input[8] == 0xA1 && cadence_origin_input[9] == 0x30,
        "native BT cadence origin report preserves requested mode 0x30");

    const galaxy::input::NativeHidCadenceStats cadence_origin_stats =
        memory.native_hid_cadence_stats();
    fake_ticks = cadence_origin_ticks + kTestHidReportPeriodTicks;
    memory.poll_native_bt_reconnect();
    const galaxy::input::NativeHidCadenceStats cadence_one_slot =
        memory.native_hid_cadence_stats();
    passed &= expect(
        TestAccess::virtual_input_queued(memory) &&
            cadence_one_slot.produced_samples ==
                cadence_origin_stats.produced_samples + 1u &&
            cadence_one_slot.queue_replacements ==
                cadence_origin_stats.queue_replacements,
        "native BT device produces one sample at the next slot without a USB read");
    memory.poll_native_bt_reconnect();
    passed &= expect(
        memory.native_hid_cadence_stats().produced_samples ==
            cadence_one_slot.produced_samples,
        "native BT repeated service in one deadline slot cannot resample input");

    fake_ticks = cadence_origin_ticks + kTestHidReportPeriodTicks * 2u;
    memory.poll_native_bt_reconnect();
    const galaxy::input::NativeHidCadenceStats cadence_replaced =
        memory.native_hid_cadence_stats();
    passed &= expect(
        TestAccess::virtual_input_queued(memory) &&
            cadence_replaced.produced_samples ==
                cadence_one_slot.produced_samples + 1u &&
            cadence_replaced.queue_replacements ==
                cadence_one_slot.queue_replacements + 1u,
        "native BT bounded device FIFO replaces one unconsumed old sample with the newest slot");
    const auto* cadence_replaced_input = read_bulk_acl(
        kRequest + 0x1A40,
        12,
        "native BT device delivers only the newest bounded-FIFO sample");
    passed &= expect(
        cadence_replaced_input[8] == 0xA1 &&
            cadence_replaced_input[9] == 0x30,
        "native BT replaced cadence report preserves requested mode 0x30");

    const galaxy::input::NativeHidCadenceStats before_late_service =
        memory.native_hid_cadence_stats();
    fake_ticks = cadence_origin_ticks +
                 kTestHidReportPeriodTicks * 4u +
                 kTestHidReportPeriodTicks / 2u;
    memory.poll_native_bt_reconnect();
    const auto* cadence_late_input = read_bulk_acl(
        kRequest + 0x1A60,
        12,
        "native BT late service emits one newest report instead of a catch-up burst");
    passed &= expect(
        cadence_late_input[8] == 0xA1 && cadence_late_input[9] == 0x30,
        "native BT late newest report preserves requested mode 0x30");
    const galaxy::input::NativeHidCadenceStats after_late_service =
        memory.native_hid_cadence_stats();
    passed &= expect(
        after_late_service.scheduled_samples ==
                before_late_service.scheduled_samples + 2u &&
            after_late_service.produced_samples ==
                before_late_service.produced_samples + 1u &&
            after_late_service.skipped_due_slots ==
                before_late_service.skipped_due_slots + 1u,
        "native BT late service audits skipped slots and samples host input only once");

    submit_bt_read(kRequest + 0x1A80, 1, 0x82, kBuffer, 64);
    passed &= expect(
        !ios_reply_available(guest_memory),
        "native BT pending read waits after the late newest report");
    fake_ticks = cadence_origin_ticks + kTestHidReportPeriodTicks * 5u - 1u;
    memory.poll_native_bt_reconnect();
    passed &= expect(
        !ios_reply_available(guest_memory),
        "native BT pending read cannot complete before its immutable deadline");
    fake_ticks = cadence_origin_ticks + kTestHidReportPeriodTicks * 5u;
    memory.poll_native_bt_reconnect();
    passed &= expect(
        expect_ios_reply(
            memory,
            guest_memory,
            kRequest + 0x1A80,
            12,
            7,
            "native BT pending read completes exactly at the next immutable deadline"),
        "native BT pending read completes exactly at the next immutable deadline");
    acknowledge_ios_reply(guest_memory);

    submit_bt_read(kRequest + 0x1AC0, 1, 0x82, kBuffer, 64);
    passed &= expect(
        !ios_reply_available(guest_memory),
        "native BT immutable boundary emits at most one report per guest read");
    fake_ticks = cadence_origin_ticks + kTestHidReportPeriodTicks * 6u;
    memory.poll_native_bt_reconnect();
    passed &= expect(
        expect_ios_reply(
            memory,
            guest_memory,
            kRequest + 0x1AC0,
            12,
            7,
            "native BT cadence advances to the following origin boundary"),
        "native BT cadence advances to the following origin boundary");
    acknowledge_ios_reply(guest_memory);

    const galaxy::input::NativeHidCadenceStats audited_cadence =
        memory.native_hid_cadence_stats();
    passed &= expect(
        audited_cadence.queue_replacements >=
                cadence_origin_stats.queue_replacements + 1u &&
            audited_cadence.skipped_due_slots >=
                cadence_origin_stats.skipped_due_slots + 1u &&
            audited_cadence.delivery_gaps >=
                cadence_origin_stats.delivery_gaps + 2u &&
            audited_cadence.delivery_bursts ==
                cadence_origin_stats.delivery_bursts &&
            audited_cadence.out_of_order_deliveries ==
                cadence_origin_stats.out_of_order_deliveries,
        "native BT cadence audit exposes bounded coalescing without bursts or reordering");

    fake_ticks = cadence_origin_ticks + kTestHidReportPeriodTicks * 7u;
    memory.poll_native_bt_reconnect();
    passed &= expect(
        TestAccess::virtual_input_queued(memory) &&
            TestAccess::queued_virtual_report_mode(memory) == 0x30u,
        "native BT queues an old-mode report before mode-transition stale-byte proof");
    const galaxy::input::NativeHidCadenceStats before_mode_change =
        memory.native_hid_cadence_stats();
    set_input_report_mode(
        0x33,
        "native BT mode transition invalidates a queued old-mode sample",
        false);
    passed &= expect(
        !TestAccess::virtual_input_queued(memory) &&
            memory.native_hid_cadence_stats().mode_invalidations ==
                before_mode_change.mode_invalidations + 1u,
        "native BT report-mode transition discards stale queued bytes");
    memory.poll_native_bt_reconnect();
    passed &= expect(
        TestAccess::virtual_input_queued(memory) &&
            TestAccess::queued_virtual_report_mode(memory) == 0x33u,
        "native BT report-mode transition queues only new-mode bytes");
    const auto* new_mode_input = read_bulk_acl(
        kRequest + 0x1B00,
        27,
        "native BT report-mode transition delivers only new-mode bytes");
    passed &= expect(
        new_mode_input[8] == 0xA1 && new_mode_input[9] == 0x33,
        "native BT report-mode transition cannot deliver stale old-mode bytes");

    const std::array<std::byte, 1> cadence_basic_camera_mode{
        std::byte{0x01}};
    const auto cadence_basic_camera_write = wiimote_memory_write_report(
        0x04, 0xB00033, cadence_basic_camera_mode);
    submit_hid_output(
        cadence_basic_camera_write,
        "native BT restores basic camera layout before report mode 0x37 cadence checks");
    expect_ack(kRequest + 0x1B40, 0x16);
    set_input_report_mode(
        0x37,
        "native BT IR-gate stale-byte proof starts active IR report mode");
    memory.poll_native_bt_reconnect();
    passed &= expect(
        TestAccess::virtual_input_queued(memory) &&
            TestAccess::queued_virtual_report_mode(memory) == 0x37u,
        "native BT queues an active-IR sample before gate transition");
    const galaxy::input::NativeHidCadenceStats before_ir_gate_change =
        memory.native_hid_cadence_stats();
    submit_hid_output(
        ir_enable_part1_disable_report,
        "native BT IR-gate transition invalidates queued active-IR bytes");
    expect_ack(kRequest + 0x1B80, 0x13);
    passed &= expect(
        !TestAccess::virtual_input_queued(memory) &&
            memory.native_hid_cadence_stats().mode_invalidations ==
                before_ir_gate_change.mode_invalidations + 1u,
        "native BT IR-gate transition discards queued pre-transition bytes");
    fake_ticks += kTestHidReportPeriodTicks;
    memory.poll_native_bt_reconnect();
    const auto* disabled_ir_input = read_bulk_acl(
        kRequest + 0x1BC0,
        31,
        "native BT IR-gate transition delivers a freshly sampled hidden-IR report");
    passed &= expect(
        disabled_ir_input[8] == 0xA1 && disabled_ir_input[9] == 0x37,
        "native BT post-gate sample keeps the selected report mode");
    expect_basic_ir_bytes_hidden(
        disabled_ir_input,
        "native BT IR-gate transition cannot deliver stale visible IR bytes");

    // Leave a second hidden-IR sample queued when the last conjunct of the
    // full IR gate reopens. Reopening must discard those stale bytes without
    // moving the independently running 100 Hz phase. The pending USB read is
    // then a pure consumer: it cannot sample early, and the first visible IR
    // report must arrive at the next immutable device deadline.
    fake_ticks += kTestHidReportPeriodTicks;
    {
        ScopedHostPointerProvider host_pointer(
            &centered_host_pointer_provider);
        memory.poll_native_bt_reconnect();
    }
    passed &= expect(
        TestAccess::virtual_input_queued(memory) &&
            TestAccess::queued_virtual_report_mode(memory) == 0x37u &&
            TestAccess::queued_virtual_deadline_ticks(memory) == fake_ticks,
        "native BT queues one timestamped hidden-IR sample before full-gate reopen");
    const galaxy::input::NativeHidCadenceStats before_full_ir_reopen =
        memory.native_hid_cadence_stats();
    const std::uint64_t full_ir_reopen_ticks = fake_ticks;
    submit_hid_output(
        ir_enable_report,
        "native BT IR-gate stale-byte proof restores IR part 1");
    passed &= expect(
        TestAccess::virtual_ir_gate_open(memory) &&
            !TestAccess::virtual_input_queued(memory) &&
            memory.native_hid_cadence_stats().mode_invalidations ==
                before_full_ir_reopen.mode_invalidations + 1u,
        "native BT full IR gate reopen discards the hidden pre-gate report immediately");
    expect_ack(kRequest + 0x1C00, 0x13);

    const std::uint64_t produced_at_full_ir_reopen =
        memory.native_hid_cadence_stats().produced_samples;
    submit_bt_read(kRequest + 0x1C40, 1, 0x82, kBuffer, 64);
    passed &= expect(
        !ios_reply_available(guest_memory) &&
            memory.native_hid_cadence_stats().produced_samples ==
                produced_at_full_ir_reopen,
        "native BT first full-gate USB read cannot sample or resurrect pre-gate bytes");
    fake_ticks =
        full_ir_reopen_ticks + kTestHidReportPeriodTicks - 1u;
    memory.poll_native_bt_reconnect();
    passed &= expect(
        !ios_reply_available(guest_memory),
        "native BT first visible IR report does not precede its profiled deadline");
    fake_ticks = full_ir_reopen_ticks + kTestHidReportPeriodTicks;
    {
        ScopedHostPointerProvider host_pointer(
            &centered_host_pointer_provider);
        memory.poll_native_bt_reconnect();
    }
    passed &= expect(
        expect_ios_reply(
            memory,
            guest_memory,
            kRequest + 0x1C40,
            31,
            7,
            "native BT first visible IR report arrives at the first post-gate deadline"),
        "native BT first visible IR report arrives at the first post-gate deadline");
    const auto* first_post_gate_ir =
        reinterpret_cast<const std::uint8_t*>(memory.pointer(kBuffer, 31));
    passed &= expect(
        first_post_gate_ir[8] == 0xA1u &&
            first_post_gate_ir[9] == 0x37u,
        "native BT first post-gate report retains the selected IR-bearing mode");
    expect_ir_bytes_published(
        first_post_gate_ir,
        15u,
        25u,
        "native BT first post-gate report contains current host-pointer IR dots");
    const galaxy::input::NativeHidCadenceStats first_post_gate_stats =
        memory.native_hid_cadence_stats();
    passed &= expect(
        first_post_gate_stats.last_delivered_deadline_ticks == fake_ticks &&
            first_post_gate_stats.last_delivery_ticks == fake_ticks &&
            first_post_gate_stats.last_delivery_ticks -
                    full_ir_reopen_ticks <=
                kTestHidReportPeriodTicks,
        "native BT first visible IR delivery latency is bounded to one profiled report period");
    acknowledge_ios_reply(guest_memory);

    const galaxy::input::NativeHidCadenceStats before_device_reset =
        memory.native_hid_cadence_stats();
    const std::uint64_t reconnect_arm_notifications_before =
        cadence_arm_observation.notifications;
    TestAccess::reset_virtual_input_device(memory, true);
    passed &= expect(
        !TestAccess::virtual_input_queued(memory) &&
            !memory.native_hid_cadence_stats().armed &&
            memory.native_hid_cadence_stats().disconnect_resets ==
                before_device_reset.disconnect_resets + 1u,
        "native BT disconnect reset clears the device epoch and FIFO");
    set_input_report_mode(
        0x37,
        "native BT reconnect proof rearms a fresh input epoch");
    memory.poll_native_bt_reconnect();
    passed &= expect(
        !memory.native_hid_cadence_stats().armed &&
            !TestAccess::virtual_input_queued(memory),
        "native BT reconnect waits for its first deliverable bulk read before rearming");
    submit_bt_read(kRequest + 0x1B40, 1, 0x82, kBuffer, 64);
    passed &= expect(
        !ios_reply_available(guest_memory),
        "native BT reconnect bulk read waits for fresh device service");
    memory.poll_native_bt_reconnect();
    passed &= expect(
        expect_ios_reply(
            memory,
            guest_memory,
            kRequest + 0x1B40,
            31,
            7,
            "native BT reconnect proof delivers from the fresh input epoch"),
        "native BT reconnect proof delivers from the fresh input epoch");
    const auto* reconnected_input =
        reinterpret_cast<const std::uint8_t*>(memory.pointer(kBuffer, 31));
    const galaxy::input::NativeHidCadenceStats reconnected_stats =
        memory.native_hid_cadence_stats();
    passed &= expect(
        reconnected_input[8] == 0xA1 && reconnected_input[9] == 0x37 &&
            reconnected_stats.epochs_started ==
                before_device_reset.epochs_started + 1u &&
            reconnected_stats.last_delivered_sequence >
                before_device_reset.last_delivered_sequence &&
            cadence_arm_observation.notifications ==
                reconnect_arm_notifications_before + 1u &&
            cadence_arm_observation.next_unconsumed_sequence ==
                reconnected_stats.last_produced_sequence + 1u &&
            cadence_arm_observation.next_unconsumed_deadline_ticks ==
                fake_ticks + kTestHidReportPeriodTicks &&
            cadence_arm_observation.period_ticks ==
                kTestHidReportPeriodTicks,
        "native BT reconnect epoch preserves globally increasing delivery IDs");
    acknowledge_ios_reply(guest_memory);

    // Regression oracle for the old direct KPAD pointer's update semantics,
    // without restoring that HLE path: move the renderer-owned mouse every
    // millisecond, sample only on the profiled 100 Hz HID clock, and consume at
    // 60 Hz. Each delivered ACL report must identify the newest host sequence
    // sampled at its immutable deadline, never the newer state visible when
    // the guest happens to post its read.
    galaxy::input::NativeInputCausalityTracker acl_decoder_admission;
    NativeVirtualInputDeliveryCapture delivery_capture{};
    delivery_capture.address_space = &memory;
    delivery_capture.causality_tracker = &acl_decoder_admission;
    memory.set_native_virtual_input_acl_delivery_callback(
        &capture_native_virtual_input_delivery,
        &delivery_capture);
    set_input_report_mode(
        0x37,
        "native BT moving-pointer timeline rearms without guest-read sampling",
        false);
    constexpr std::uint64_t kTestOneMillisecondTicks =
        galaxy::input::kNativeHidTimelineTicksPerSecond / 1'000u;
    constexpr std::uint64_t kTestGuestSixtyHzPeriodTicks =
        galaxy::input::kNativeHidTimelineTicksPerSecond / 60u;
    constexpr std::uint32_t kMovingPointerDurationMs = 200u;
    constexpr std::uint64_t kMovingPointerHostOriginMs = 1'000'000u;
    const std::uint64_t moving_pointer_origin_ticks = fake_ticks;
    std::uint64_t next_guest_consume_ticks =
        moving_pointer_origin_ticks + kTestGuestSixtyHzPeriodTicks;
    const galaxy::input::NativeHidCadenceStats moving_pointer_before =
        memory.native_hid_cadence_stats();
    std::uint64_t maximum_moving_pointer_age_ticks = 0u;
    std::uint16_t previous_delivered_left_dot_x = 0u;
    bool have_previous_delivered_dot = false;
    bool actual_acl_decoder_admission_proven = false;
    std::uint32_t moving_pointer_deliveries = 0u;
    {
        ScopedHostPointerProvider host_pointer(
            &timed_host_pointer_provider);
        for (std::uint32_t elapsed_ms = 0u;
             elapsed_ms <= kMovingPointerDurationMs;
             ++elapsed_ms) {
            g_timed_host_pointer = {};
            g_timed_host_pointer.window_focused = true;
            g_timed_host_pointer.inside_client = true;
            g_timed_host_pointer.absolute_valid = true;
            g_timed_host_pointer.client_x =
                220 + static_cast<int>(elapsed_ms);
            g_timed_host_pointer.client_y = 240;
            g_timed_host_pointer.client_width = 641;
            g_timed_host_pointer.client_height = 481;
            g_timed_host_pointer.absolute_sequence =
                static_cast<std::uint64_t>(elapsed_ms) + 1u;
            g_timed_host_pointer.absolute_acquired_ms =
                kMovingPointerHostOriginMs + elapsed_ms;
            fake_ticks = moving_pointer_origin_ticks +
                static_cast<std::uint64_t>(elapsed_ms) *
                    kTestOneMillisecondTicks;
            memory.poll_native_bt_reconnect();

            if (fake_ticks < next_guest_consume_ticks) {
                continue;
            }
            passed &= expect(
                TestAccess::virtual_input_queued(memory) &&
                    TestAccess::queued_virtual_has_host_pointer_sample(memory),
                "native BT 60 Hz consumer finds an independently sampled host-pointer report");
            const std::uint64_t queued_deadline =
                TestAccess::queued_virtual_deadline_ticks(memory);
            const std::uint64_t queued_production =
                TestAccess::queued_virtual_production_wii_ticks(memory);
            const std::uint64_t queued_pointer_sequence =
                TestAccess::queued_virtual_host_pointer_sequence(memory);
            const std::uint64_t queued_pointer_acquired_ms =
                TestAccess::queued_virtual_host_pointer_acquired_ms(memory);
            const std::uint64_t queued_epoch =
                TestAccess::queued_virtual_epoch(memory);
            const std::uint64_t queued_sample_sequence =
                TestAccess::queued_virtual_sample_sequence(memory);
            const std::uint64_t expected_pointer_sequence =
                (queued_deadline - moving_pointer_origin_ticks) /
                    kTestOneMillisecondTicks +
                1u;
            const std::uint64_t expected_pointer_acquired_ms =
                kMovingPointerHostOriginMs + expected_pointer_sequence - 1u;
            passed &= expect(
                queued_production == queued_deadline &&
                    queued_pointer_sequence == expected_pointer_sequence &&
                    queued_pointer_acquired_ms ==
                        expected_pointer_acquired_ms &&
                    queued_pointer_sequence <=
                        g_timed_host_pointer.absolute_sequence,
                "native BT queued ACL metadata preserves one coherent host sequence/timestamp publication at its profiled deadline");

            const std::size_t callback_count_before_read =
                delivery_capture.count;
            if (moving_pointer_deliveries == 0u) {
                constexpr std::uint32_t kUndersizedIdentityRequestOffset =
                    0x7400u;
                submit_bt_read(
                    kRequest + kUndersizedIdentityRequestOffset,
                    1,
                    0x82,
                    kBuffer,
                    1);
                passed &= expect(
                    expect_ios_reply(
                        memory,
                        guest_memory,
                        kRequest + kUndersizedIdentityRequestOffset,
                        static_cast<std::uint32_t>(-23),
                        7,
                        "native BT undersized identity read is rejected"),
                    "native BT undersized identity read is rejected");
                acknowledge_ios_reply(guest_memory);
                passed &= expect(
                    delivery_capture.count == callback_count_before_read &&
                        TestAccess::virtual_input_queued(memory) &&
                        TestAccess::queued_virtual_epoch(memory) ==
                            queued_epoch &&
                        TestAccess::queued_virtual_sample_sequence(memory) ==
                            queued_sample_sequence &&
                        TestAccess::queued_virtual_host_pointer_sequence(
                            memory) == queued_pointer_sequence &&
                        TestAccess::queued_virtual_host_pointer_acquired_ms(
                            memory) == queued_pointer_acquired_ms,
                    "native BT rejected IOS bulk read neither emits nor strips queued delivery identity");
            }

            const std::uint64_t produced_before_guest_read =
                memory.native_hid_cadence_stats().produced_samples;
            const std::uint32_t request =
                kRequest + 0x5000u + moving_pointer_deliveries * 0x40u;
            const auto* moving_pointer_acl = read_bulk_acl(
                request,
                31,
                "native BT moving pointer reaches the guest at 60 Hz");
            passed &= expect(
                delivery_capture.count == callback_count_before_read + 1u,
                "native BT successful IOS bulk reply emits exactly one delivery identity");
            const std::size_t identity_index = callback_count_before_read;
            const auto& delivered_identity =
                delivery_capture.identities[identity_index];
            passed &= expect(
                delivered_identity.logical_epoch == queued_epoch &&
                    delivered_identity.sample_sequence ==
                        queued_sample_sequence &&
                    delivered_identity.deadline_wii_ticks ==
                        queued_deadline &&
                    delivered_identity.production_wii_ticks ==
                        queued_production &&
                    delivered_identity.delivery_wii_ticks == fake_ticks &&
                    delivered_identity.host_pointer_sequence ==
                        queued_pointer_sequence &&
                    delivered_identity.host_pointer_acquired_ms ==
                        queued_pointer_acquired_ms &&
                    delivered_identity.host_pointer_sampled &&
                    delivered_identity.report_id == 0x37u &&
                    delivered_identity.payload_size == 23u &&
                    std::equal(
                        moving_pointer_acl + 8u,
                        moving_pointer_acl + 31u,
                        delivered_identity.payload.begin()) &&
                    std::all_of(
                        delivered_identity.payload.begin() +
                            delivered_identity.payload_size,
                        delivered_identity.payload.end(),
                        [](std::uint8_t value) { return value == 0u; }) &&
                    delivered_identity.ios_request == request &&
                    delivered_identity.payload_fingerprint ==
                        expected_hid_payload_fingerprint(
                            std::span<const std::uint8_t>(
                                moving_pointer_acl, 31u)) &&
                    delivery_capture
                        .fifo_occupied_during_callback[identity_index] &&
                    delivery_capture
                        .ios_reply_visible_during_callback[identity_index] &&
                    !TestAccess::virtual_input_queued(memory),
                "native BT delivery identity survives acquisition, production, ACL copy, and IOS reply before FIFO retirement");

            if (!actual_acl_decoder_admission_proven &&
                delivered_identity.host_pointer_sampled &&
                delivered_identity.report_id == 0x37u &&
                delivered_identity.payload_size == 23u) {
                // This is the last deterministic boundary available without
                // booting RMGE01's translated IOS callback, L2CAP dispatcher,
                // and guest scheduler. The next normal step enters report
                // decoder 0x804E0ED0 with boot-created WPAD control state at
                // 0x80660170 and a live OS context/thread. Directly invoking
                // that generated function with seeded globals would bypass
                // the contract under test, so prove exact decoder admission
                // from the real guest-visible ACL bytes and explicitly verify
                // that no downstream WPAD identity can be fabricated without
                // an observed translated commit.
                galaxy::input::NativeInputDecoderObservation observation{};
                observation.wii_ticks =
                    delivered_identity.delivery_wii_ticks;
                const auto decoder_token =
                    acl_decoder_admission.begin_report_decoder(
                        0x804E0ED0u,
                        0u,
                        std::span<const std::uint8_t>(
                            moving_pointer_acl + 9u,
                            delivered_identity.payload_size - 1u),
                        observation);
                passed &= expect(
                    decoder_token != 0u &&
                        acl_decoder_admission.protocol_failure() ==
                            galaxy::input::NativeInputCausalityReason::None,
                    "real host-pointer ACL delivery and guest bytes satisfy the translated WPAD 0x37 decoder admission contract");
                acl_decoder_admission.abort_report_decoder(decoder_token);
                passed &= expect(
                    acl_decoder_admission.protocol_failure() ==
                        galaxy::input::NativeInputCausalityReason::
                            DecoderDidNotCommit,
                    "ACL delivery proof cannot fabricate downstream WPAD/KPAD state without a measured translated decoder commit");
                actual_acl_decoder_admission_proven = decoder_token != 0u;
            }
            const galaxy::input::NativeHidCadenceStats delivered_stats =
                memory.native_hid_cadence_stats();
            passed &= expect(
                delivered_stats.produced_samples ==
                        produced_before_guest_read &&
                    delivered_stats.last_delivered_deadline_ticks ==
                        queued_deadline &&
                    delivered_stats.last_delivery_ticks == fake_ticks &&
                    delivered_stats.last_delivery_age_ticks ==
                        fake_ticks - queued_deadline &&
                    delivered_stats.delivery_timing_conservation_valid(),
                "native BT guest read records exact ACL delivery age on the native timeline without resampling host input");
            const std::uint64_t sample_age =
                delivered_stats.last_delivery_ticks - queued_deadline;
            maximum_moving_pointer_age_ticks = std::max(
                maximum_moving_pointer_age_ticks, sample_age);
            expect_ir_bytes_published(
                moving_pointer_acl,
                15u,
                25u,
                "native BT moving-pointer ACL report retains valid IR dots");
            const std::uint8_t packed_high = moving_pointer_acl[17];
            const std::uint16_t delivered_left_dot_x =
                static_cast<std::uint16_t>(
                    moving_pointer_acl[15] |
                    (static_cast<std::uint16_t>(
                         (packed_high >> 4u) & 0x03u)
                     << 8u));
            if (have_previous_delivered_dot) {
                passed &= expect(
                    delivered_left_dot_x < previous_delivered_left_dot_x,
                    "native BT 1 ms mouse sweep remains monotonic after 100 Hz sampling and 60 Hz ACL delivery");
            }
            previous_delivered_left_dot_x = delivered_left_dot_x;
            have_previous_delivered_dot = true;
            ++moving_pointer_deliveries;
            next_guest_consume_ticks += kTestGuestSixtyHzPeriodTicks;
        }
    }
    const galaxy::input::NativeHidCadenceStats moving_pointer_after =
        memory.native_hid_cadence_stats();
    const std::uint64_t moving_pointer_produced =
        moving_pointer_after.produced_samples -
        moving_pointer_before.produced_samples;
    const std::uint64_t moving_pointer_replaced =
        moving_pointer_after.queue_replacements -
        moving_pointer_before.queue_replacements;
    const std::uint64_t moving_pointer_delivered =
        moving_pointer_after.delivered_samples -
        moving_pointer_before.delivered_samples;
    const std::uint64_t moving_pointer_age_observations =
        moving_pointer_after.delivery_age_samples -
        moving_pointer_before.delivery_age_samples;
    const bool moving_pointer_cadence_valid =
        actual_acl_decoder_admission_proven &&
            moving_pointer_deliveries == 12u &&
            delivery_capture.count == moving_pointer_deliveries &&
            !delivery_capture.overflow &&
            maximum_moving_pointer_age_ticks <=
                kTestHidReportPeriodTicks &&
            moving_pointer_replaced > 0u &&
            moving_pointer_after.skipped_due_slots ==
                moving_pointer_before.skipped_due_slots &&
            moving_pointer_produced ==
                moving_pointer_replaced + moving_pointer_delivered &&
            moving_pointer_age_observations == moving_pointer_delivered &&
            moving_pointer_after.max_delivery_age_ticks <=
                moving_pointer_after.delivery_age_limit_ticks &&
            moving_pointer_after.delivery_timing_conservation_valid() &&
            !TestAccess::virtual_input_queued(memory);
    if (!moving_pointer_cadence_valid) {
        std::cerr
            << "[native-bt-moving-pointer-failure] decoder="
            << actual_acl_decoder_admission_proven
            << " deliveries=" << moving_pointer_deliveries
            << " callbacks=" << delivery_capture.count
            << " callback-overflow=" << delivery_capture.overflow
            << " max-window-age=" << maximum_moving_pointer_age_ticks
            << " period=" << kTestHidReportPeriodTicks
            << " replacements=" << moving_pointer_replaced
            << " skipped-before=" << moving_pointer_before.skipped_due_slots
            << " skipped-after=" << moving_pointer_after.skipped_due_slots
            << " produced=" << moving_pointer_produced
            << " delivered=" << moving_pointer_delivered
            << " age-observations=" << moving_pointer_age_observations
            << " max-global-age="
            << moving_pointer_after.max_delivery_age_ticks
            << " age-limit=" << moving_pointer_after.delivery_age_limit_ticks
            << " timing-conserved="
            << moving_pointer_after.delivery_timing_conservation_valid()
            << " fifo-occupied="
            << TestAccess::virtual_input_queued(memory) << '\n';
    }
    passed &= expect(
        moving_pointer_cadence_valid,
        "native BT 1 ms producer/60 Hz consumer is fresh, bounded, and conserves every delivery-age observation");

    bool delivery_identities_ordered = true;
    for (std::size_t i = 0; i < delivery_capture.count; ++i) {
        const auto& current = delivery_capture.identities[i];
        delivery_identities_ordered =
            current.logical_epoch != 0u &&
            current.sample_sequence != 0u &&
            current.deadline_wii_ticks <=
                current.production_wii_ticks &&
            current.production_wii_ticks <= current.delivery_wii_ticks &&
            current.host_pointer_sequence != 0u &&
            current.host_pointer_acquired_ms != 0u &&
            current.payload_fingerprint != 0u &&
            (i == 0u ||
             (current.logical_epoch >=
                  delivery_capture.identities[i - 1u].logical_epoch &&
              current.sample_sequence >
                  delivery_capture.identities[i - 1u].sample_sequence &&
              current.deadline_wii_ticks >
                  delivery_capture.identities[i - 1u]
                      .deadline_wii_ticks &&
              current.host_pointer_sequence >
                  delivery_capture.identities[i - 1u]
                      .host_pointer_sequence &&
              current.host_pointer_acquired_ms >
                  delivery_capture.identities[i - 1u]
                      .host_pointer_acquired_ms)) &&
            delivery_identities_ordered;
    }
    passed &= expect(
        delivery_identities_ordered,
        "native BT ACL identities remain monotonic within their separate Wii-tick and host-millisecond clock domains");

    // A mode invalidation and a disconnect reset are accounting outcomes, not
    // deliveries. Neither may invoke the ACL-delivery callback or erase the
    // queued identity before its corresponding cadence counter advances.
    const std::size_t callbacks_before_accounted_discards =
        delivery_capture.count;
    fake_ticks += kTestHidReportPeriodTicks;
    g_timed_host_pointer.absolute_sequence += 1u;
    g_timed_host_pointer.absolute_acquired_ms += 10u;
    {
        ScopedHostPointerProvider host_pointer(
            &timed_host_pointer_provider);
        memory.poll_native_bt_reconnect();
    }
    const galaxy::input::NativeHidCadenceStats before_identity_invalidation =
        memory.native_hid_cadence_stats();
    passed &= expect(
        TestAccess::virtual_input_queued(memory) &&
            TestAccess::queued_virtual_has_host_pointer_sample(memory) &&
            TestAccess::queued_virtual_host_pointer_acquired_ms(memory) ==
                g_timed_host_pointer.absolute_acquired_ms,
        "native BT queues complete host metadata before mode invalidation accounting");
    set_input_report_mode(
        0x36,
        "native BT identity mode invalidation consumes report-mode output",
        false);
    passed &= expect(
        !TestAccess::virtual_input_queued(memory) &&
            delivery_capture.count == callbacks_before_accounted_discards &&
            memory.native_hid_cadence_stats().mode_invalidations ==
                before_identity_invalidation.mode_invalidations + 1u,
        "native BT mode invalidation is accounted without a false ACL delivery identity");

    fake_ticks += kTestHidReportPeriodTicks;
    g_timed_host_pointer.absolute_sequence += 1u;
    g_timed_host_pointer.absolute_acquired_ms += 10u;
    {
        ScopedHostPointerProvider host_pointer(
            &timed_host_pointer_provider);
        memory.poll_native_bt_reconnect();
    }
    const galaxy::input::NativeHidCadenceStats before_identity_reset =
        memory.native_hid_cadence_stats();
    passed &= expect(
        TestAccess::virtual_input_queued(memory) &&
            TestAccess::queued_virtual_host_pointer_acquired_ms(memory) ==
                g_timed_host_pointer.absolute_acquired_ms,
        "native BT queues complete host metadata before reset accounting");
    TestAccess::reset_virtual_input_device(memory, true);
    passed &= expect(
        !TestAccess::virtual_input_queued(memory) &&
            delivery_capture.count == callbacks_before_accounted_discards &&
            memory.native_hid_cadence_stats().reset_queue_discards ==
                before_identity_reset.reset_queue_discards + 1u,
        "native BT reset discard is accounted without a false ACL delivery identity");
    memory.set_native_virtual_input_acl_delivery_callback(nullptr, nullptr);

    const std::string input_trace_text = input_trace.str();
    if (input_trace_text.find("FAILED:") != std::string::npos) {
        std::cerr << input_trace_text;
    }
    passed &= expect(
        input_trace_text.find("[input-trace] boundary=bt-hid") !=
            std::string::npos,
        "native BT input trace is emitted at the Bluetooth/HID boundary");
    passed &= expect(
        input_trace_text.find(
            "[input-hid-wire] boundary=bt-hid-wire") != std::string::npos &&
            input_trace_text.find(" irraw=") != std::string::npos,
        "native BT input trace exposes the exact serialized HID IR bytes");
    passed &= expect(
        input_trace_text.find("[input-acl-delivery] epoch=") !=
                std::string::npos &&
            input_trace_text.find(" deadline-wii-ticks=") !=
                std::string::npos &&
            input_trace_text.find(" production-wii-ticks=") !=
                std::string::npos &&
            input_trace_text.find(" delivery-wii-ticks=") !=
                std::string::npos &&
            input_trace_text.find(" host-acquired-ms=") !=
                std::string::npos &&
            input_trace_text.find(" payload-size=") !=
                std::string::npos &&
            input_trace_text.find(" payload-bytes=a1:") !=
                std::string::npos &&
            input_trace_text.find(" payload-fnv1a64=0x") !=
                std::string::npos &&
            input_trace_text.find(" ios-request=0x") !=
                std::string::npos,
        "native BT ACL trace keeps host milliseconds distinct from Wii cadence ticks and identifies exact payload/reply");
    passed &= expect(
        input_trace_text.find("source=keyboard_mouse") != std::string::npos,
        "native BT input trace records the virtual host source");
    passed &= expect(
        input_trace_text.find("requested=0x30") != std::string::npos &&
            input_trace_text.find("report=0x30") != std::string::npos &&
            input_trace_text.find("promoted=") == std::string::npos,
        "native BT input trace records the exact requested HID report mode without promotion");
    passed &= expect(
        input_trace_text.find("script=0xe003") != std::string::npos,
        "native BT input trace records scripted report-level input");
    passed &= expect(
        input_trace_text.find("stick=(0.5,-1)") != std::string::npos &&
            input_trace_text.find("nunchuk=(174,29)") != std::string::npos,
        "native BT input trace records scripted stick report values");
    passed &= expect(
        input_trace_text.find("shake=1") != std::string::npos,
        "native BT input trace records scripted shake report values");
    passed &= expect(
        input_trace_text.find("pointer=(0,0)") != std::string::npos &&
            input_trace_text.find("ptrsrc=script") != std::string::npos,
        "native BT input trace records scripted pointer report values");
    passed &= expect(
        input_trace_text.find("[input-ir-out] report=0x12") !=
                std::string::npos &&
            input_trace_text.find("[input-ir-gate]") != std::string::npos &&
            input_trace_text.find("irgate=1") != std::string::npos &&
            input_trace_text.find("ir13=1") != std::string::npos &&
            input_trace_text.find("ir1a=1") != std::string::npos &&
            input_trace_text.find("sens1=1") != std::string::npos &&
            input_trace_text.find("sens2=1") != std::string::npos &&
            input_trace_text.find("latch=1") != std::string::npos &&
            input_trace_text.find("irmode=0x3") != std::string::npos,
        "native BT IR trace identifies the open fully initialized IR gate");

    const std::filesystem::path replay_path =
        std::filesystem::temp_directory_path() /
        "galaxy_native_host_input_replay.tsv";
    {
        std::ofstream replay(replay_path, std::ios::binary | std::ios::trunc);
        replay << "meta\tkey_order\t"
               << "LButton,RButton,Space,Enter,Shift,LShift,RShift,"
               << "Left,Up,Right,Down,1,2,A,B,C,D,E,I,J,K,L,Q,R,S,W,Z,"
               << "Ctrl,LCtrl,RCtrl\n";
        replay << "elapsed_ms\tgame_pid\tgame_focused\tfg_pid\t"
               << "mouse_screen_x\tmouse_screen_y\tmouse_client_x\t"
               << "mouse_client_y\tclient_w\tclient_h\tkeys_hold_hex\t"
               << "keys_trig_hex\tkeys_release_hex\txinput_connected\t"
               << "xinput_buttons_hex\tleft_trigger\tright_trigger\t"
               << "thumb_lx\tthumb_ly\tthumb_rx\tthumb_ry\n";
        replay << "0\t0\t1\t0\t0\t0\t320\t240\t641\t481\t"
               << "0x0000000002410006\t0x0000000000000000\t"
               << "0x0000000000000000\t0\t0x0000\t0\t0\t0\t0\t0\t0\n";
    }
    std::ostringstream replay_trace;
    {
        const std::string replay_string = replay_path.string();
        ScopedEnv replay_log("GALAXY_INPUT_REPLAY_LOG", replay_string.c_str());
        ScopedStreamRedirect capture_replay_trace(
            std::cerr, replay_trace.rdbuf());
        memory.preload_native_input_replay_log();
        std::error_code replay_remove_error;
        const bool replay_removed =
            std::filesystem::remove(replay_path, replay_remove_error);
        passed &= expect(
            replay_removed && !replay_remove_error,
            "native input replay is loaded before the periodic HID service");
        set_input_report_mode(
            0x37, "native BT HID set-report-mode for replay is consumed");
        const auto* replay_acl = read_bulk_acl(
            kRequest + 0xF80,
            31,
            "native BT replay log emits ACL-wrapped HID report");
        passed &= expect(
            replay_acl[8] == 0xA1 && replay_acl[9] == 0x37 &&
                replay_acl[10] == 0x00 && replay_acl[11] == 0x0C,
            "native BT replay log encodes A+B as core button bytes");
        passed &= expect(
            replay_acl[15] != 0xFFu && replay_acl[16] != 0xFFu &&
                replay_acl[17] != 0xFFu && replay_acl[18] != 0xFFu &&
                replay_acl[19] != 0xFFu,
            "native BT replay log encodes mouse position as IR bytes");
        passed &= expect(
            replay_acl[25] == 0x38u && replay_acl[26] == 0x37u,
            "native BT replay log encrypts keyboard movement as Nunchuk wire bytes");
    }
    const std::string replay_trace_text = replay_trace.str();
    if (replay_trace_text.find("FAILED:") != std::string::npos) {
        std::cerr << replay_trace_text;
    }
    passed &= expect(
        replay_trace_text.find("[input-trace] boundary=bt-hid") !=
                std::string::npos &&
            replay_trace_text.find("replay=1") != std::string::npos,
        "native BT replay log is traced at the Bluetooth/HID boundary");
    passed &= expect(
        replay_trace_text.find("source=keyboard_mouse") != std::string::npos &&
            replay_trace_text.find("ptrsrc=replay") != std::string::npos &&
            replay_trace_text.find("shake=1") != std::string::npos,
        "native BT replay log trace records native replay source values");

    const galaxy::RuntimeSettings saved_replay_settings =
        galaxy::get_runtime_settings();
    galaxy::RuntimeSettings keyboard_mouse_replay_settings =
        saved_replay_settings;
    keyboard_mouse_replay_settings.input_mode =
        galaxy::RuntimeInputMode::KeyboardMouse;
    galaxy::set_runtime_settings(keyboard_mouse_replay_settings);
    const std::filesystem::path replay_xinput_path =
        std::filesystem::temp_directory_path() /
        "galaxy_native_host_input_replay_xinput_pointer.tsv";
    {
        std::ofstream replay(replay_xinput_path, std::ios::binary | std::ios::trunc);
        replay << "meta\tkey_order\t"
               << "LButton,RButton,Space,Enter,Shift,LShift,RShift,"
               << "Left,Up,Right,Down,1,2,A,B,C,D,E,I,J,K,L,Q,R,S,W,Z,"
               << "Ctrl,LCtrl,RCtrl\n";
        replay << "elapsed_ms\tgame_pid\tgame_focused\tfg_pid\t"
               << "mouse_screen_x\tmouse_screen_y\tmouse_client_x\t"
               << "mouse_client_y\tclient_w\tclient_h\tkeys_hold_hex\t"
               << "keys_trig_hex\tkeys_release_hex\txinput_connected\t"
               << "xinput_buttons_hex\tleft_trigger\tright_trigger\t"
               << "thumb_lx\tthumb_ly\tthumb_rx\tthumb_ry\n";
        replay << "0\t0\t1\t0\t0\t0\t320\t240\t641\t481\t"
               << "0x0000000000000000\t0x0000000000000000\t"
               << "0x0000000000000000\t1\t0x1000\t0\t0\t0\t0\t32767\t0\n";
    }
    std::ostringstream replay_xinput_trace;
    {
        const std::string replay_xinput_string = replay_xinput_path.string();
        ScopedEnv replay_log(
            "GALAXY_INPUT_REPLAY_LOG", replay_xinput_string.c_str());
        ScopedStreamRedirect capture_replay_xinput_trace(
            std::cerr, replay_xinput_trace.rdbuf());
        set_input_report_mode(
            0x37,
            "native BT HID set-report-mode for isolated keyboard/mouse replay is consumed");
        const auto* replay_xinput_acl = read_bulk_acl(
            kRequest + 0x1400,
            31,
            "native BT keyboard/mouse replay ignores XInput right stick");
        passed &= expect(
            replay_xinput_acl[8] == 0xA1 && replay_xinput_acl[9] == 0x37,
            "native BT isolated keyboard/mouse replay emits IR report mode");
        passed &= expect(
            replay_xinput_acl[10] == 0x00u && replay_xinput_acl[11] == 0x00u,
            "native BT keyboard/mouse replay ignores XInput A as a Wiimote button");
        passed &= expect_bytes(
            std::span<const std::uint8_t>(
                replay_xinput_acl + 25, expected_encrypted_neutral_nunchuk.size()),
            expected_encrypted_neutral_nunchuk,
            "native BT keyboard/mouse replay leaves XInput left stick off the encrypted Nunchuk wire payload");
        passed &= expect(
            replay_xinput_acl[15] == 0xAEu &&
                replay_xinput_acl[16] == 0xE7u &&
                replay_xinput_acl[17] == 0x56u &&
                replay_xinput_acl[18] == 0x4Eu &&
                replay_xinput_acl[19] == 0xE7u,
            "native BT keyboard/mouse replay keeps mouse-centered IR despite XInput right-stick input");
    }

    const std::filesystem::path replay_deadzone_path =
        std::filesystem::temp_directory_path() /
        "galaxy_native_host_input_replay_xinput_pointer_deadzone.tsv";
    {
        std::ofstream replay(replay_deadzone_path, std::ios::binary | std::ios::trunc);
        replay << "meta\tkey_order\t"
               << "LButton,RButton,Space,Enter,Shift,LShift,RShift,"
               << "Left,Up,Right,Down,1,2,A,B,C,D,E,I,J,K,L,Q,R,S,W,Z,"
               << "Ctrl,LCtrl,RCtrl\n";
        replay << "elapsed_ms\tgame_pid\tgame_focused\tfg_pid\t"
               << "mouse_screen_x\tmouse_screen_y\tmouse_client_x\t"
               << "mouse_client_y\tclient_w\tclient_h\tkeys_hold_hex\t"
               << "keys_trig_hex\tkeys_release_hex\txinput_connected\t"
               << "xinput_buttons_hex\tleft_trigger\tright_trigger\t"
               << "thumb_lx\tthumb_ly\tthumb_rx\tthumb_ry\n";
        replay << "0\t0\t1\t0\t0\t0\t320\t240\t641\t481\t"
               << "0x0000000000000000\t0x0000000000000000\t"
               << "0x0000000000000000\t1\t0x0000\t0\t0\t0\t0\t10000\t0\n";
    }
    {
        const std::string replay_deadzone_string = replay_deadzone_path.string();
        ScopedEnv replay_log(
            "GALAXY_INPUT_REPLAY_LOG", replay_deadzone_string.c_str());
        ScopedEnv pointer_deadzone("GALAXY_XINPUT_POINTER_DEADZONE", "20000");
        set_input_report_mode(
            0x37,
            "native BT HID set-report-mode for keyboard/mouse replay with XInput pointer deadzone is consumed");
        const auto* replay_deadzone_acl = read_bulk_acl(
            kRequest + 0x1440,
            31,
            "native BT keyboard/mouse replay applies XInput pointer deadzone");
        passed &= expect(
            replay_deadzone_acl[8] == 0xA1 &&
                replay_deadzone_acl[9] == 0x37,
            "native BT keyboard/mouse XInput deadzone replay emits IR report mode");
        passed &= expect(
            replay_deadzone_acl[15] != 0xFFu &&
                replay_deadzone_acl[17] != 0xFFu,
            "native BT keyboard/mouse replay suppresses right-stick motion inside pointer deadzone");
    }
    galaxy::set_runtime_settings(saved_replay_settings);
    const std::string replay_xinput_trace_text = replay_xinput_trace.str();
    if (replay_xinput_trace_text.find("FAILED:") != std::string::npos) {
        std::cerr << replay_xinput_trace_text;
    }
    passed &= expect(
        replay_xinput_trace_text.find("source=keyboard_mouse") !=
                std::string::npos &&
            replay_xinput_trace_text.find("xinput=1") != std::string::npos &&
            replay_xinput_trace_text.find("rstick=(0,0)") !=
                std::string::npos &&
            replay_xinput_trace_text.find("ptrsrc=replay") !=
                std::string::npos &&
            replay_xinput_trace_text.find("pointer=(0,0)") !=
                std::string::npos &&
            replay_xinput_trace_text.find("ptrsrc=controller") ==
                std::string::npos,
        "native BT keyboard/mouse replay records XInput data without allowing it to drive IR");

    galaxy::RuntimeSettings controller_mouse_replay_settings =
        saved_replay_settings;
    controller_mouse_replay_settings.input_mode =
        galaxy::RuntimeInputMode::Controller;
    galaxy::set_runtime_settings(controller_mouse_replay_settings);
    const std::filesystem::path replay_controller_mouse_path =
        std::filesystem::temp_directory_path() /
        "galaxy_native_host_input_replay_controller_mouse_pointer.tsv";
    {
        std::ofstream replay(
            replay_controller_mouse_path, std::ios::binary | std::ios::trunc);
        replay << "meta\tkey_order\t"
               << "LButton,RButton,Space,Enter,Shift,LShift,RShift,"
               << "Left,Up,Right,Down,1,2,A,B,C,D,E,I,J,K,L,Q,R,S,W,Z,"
               << "Ctrl,LCtrl,RCtrl\n";
        replay << "elapsed_ms\tgame_pid\tgame_focused\tfg_pid\t"
               << "mouse_screen_x\tmouse_screen_y\tmouse_client_x\t"
               << "mouse_client_y\tclient_w\tclient_h\tkeys_hold_hex\t"
               << "keys_trig_hex\tkeys_release_hex\txinput_connected\t"
               << "xinput_buttons_hex\tleft_trigger\tright_trigger\t"
               << "thumb_lx\tthumb_ly\tthumb_rx\tthumb_ry\n";
        replay << "0\t0\t1\t0\t0\t0\t320\t240\t641\t481\t"
               << "0x0000000000000007\t0x0000000000000000\t"
               << "0x0000000000000000\t0\t0x0000\t0\t0\t0\t0\t0\t0\n";
    }
    std::ostringstream replay_controller_mouse_trace;
    {
        const std::string replay_controller_mouse_string =
            replay_controller_mouse_path.string();
        ScopedEnv replay_log(
            "GALAXY_INPUT_REPLAY_LOG",
            replay_controller_mouse_string.c_str());
        ScopedStreamRedirect capture_replay_controller_mouse_trace(
            std::cerr, replay_controller_mouse_trace.rdbuf());
        set_input_report_mode(
            0x37,
            "native BT HID set-report-mode for controller replay with mouse pointer is consumed");
        const auto* replay_controller_mouse_acl = read_bulk_acl(
            kRequest + 0x1480,
            31,
            "native BT controller replay keeps mouse position on IR only");
        passed &= expect(
            replay_controller_mouse_acl[8] == 0xA1 &&
                replay_controller_mouse_acl[9] == 0x37,
            "native BT controller mouse replay emits IR report mode");
        passed &= expect(
            replay_controller_mouse_acl[10] == 0x00u &&
                replay_controller_mouse_acl[11] == 0x00u,
            "native BT controller replay ignores mouse buttons as Wiimote buttons");
        passed &= expect_bytes(
            std::span<const std::uint8_t>(
                replay_controller_mouse_acl + 25,
                expected_encrypted_neutral_nunchuk.size()),
            expected_encrypted_neutral_nunchuk,
            "native BT controller replay ignores keyboard movement in the encrypted Nunchuk wire payload");
        expect_ir_bytes_published(
            replay_controller_mouse_acl,
            15u,
            25u,
            "native BT controller replay publishes mouse-position IR bytes");
    }
    galaxy::set_runtime_settings(saved_replay_settings);
    const std::string replay_controller_mouse_trace_text =
        replay_controller_mouse_trace.str();
    if (replay_controller_mouse_trace_text.find("FAILED:") !=
        std::string::npos) {
        std::cerr << replay_controller_mouse_trace_text;
    }
    passed &= expect(
        replay_controller_mouse_trace_text.find("source=controller") !=
                std::string::npos &&
            replay_controller_mouse_trace_text.find("ptrsrc=replay") !=
                std::string::npos &&
            replay_controller_mouse_trace_text.find("ir=1") !=
                std::string::npos,
        "native BT controller replay drives IR from mouse position without switching to keyboard/mouse gameplay");

    galaxy::RuntimeSettings controller_rstick_replay_settings =
        saved_replay_settings;
    controller_rstick_replay_settings.input_mode =
        galaxy::RuntimeInputMode::Controller;
    galaxy::set_runtime_settings(controller_rstick_replay_settings);
    const std::filesystem::path replay_controller_rstick_path =
        std::filesystem::temp_directory_path() /
        "galaxy_native_host_input_replay_controller_rstick_pointer.tsv";
    {
        std::ofstream replay(
            replay_controller_rstick_path, std::ios::binary | std::ios::trunc);
        replay << "meta\tkey_order\t"
               << "LButton,RButton,Space,Enter,Shift,LShift,RShift,"
               << "Left,Up,Right,Down,1,2,A,B,C,D,E,I,J,K,L,Q,R,S,W,Z,"
               << "Ctrl,LCtrl,RCtrl\n";
        replay << "elapsed_ms\tgame_pid\tgame_focused\tfg_pid\t"
               << "mouse_screen_x\tmouse_screen_y\tmouse_client_x\t"
               << "mouse_client_y\tclient_w\tclient_h\tkeys_hold_hex\t"
               << "keys_trig_hex\tkeys_release_hex\txinput_connected\t"
               << "xinput_buttons_hex\tleft_trigger\tright_trigger\t"
               << "thumb_lx\tthumb_ly\tthumb_rx\tthumb_ry\n";
        replay << "0\t0\t1\t0\t0\t0\t320\t240\t641\t481\t"
               << "0x0000000000000000\t0x0000000000000000\t"
               << "0x0000000000000000\t1\t0x0000\t0\t0\t0\t0\t32767\t0\n";
    }
    std::ostringstream replay_controller_rstick_trace;
    {
        const std::string replay_controller_rstick_string =
            replay_controller_rstick_path.string();
        ScopedEnv replay_log(
            "GALAXY_INPUT_REPLAY_LOG",
            replay_controller_rstick_string.c_str());
        ScopedStreamRedirect capture_replay_controller_rstick_trace(
            std::cerr, replay_controller_rstick_trace.rdbuf());
        set_input_report_mode(
            0x37,
            "native BT HID set-report-mode for controller replay with right-stick pointer is consumed");
        const auto* replay_controller_rstick_acl = read_bulk_acl(
            kRequest + 0x14C0,
            31,
            "native BT controller replay keeps XInput right stick on IR");
        passed &= expect(
            replay_controller_rstick_acl[8] == 0xA1 &&
                replay_controller_rstick_acl[9] == 0x37,
            "native BT controller right-stick replay emits IR report mode");
        passed &= expect(
            replay_controller_rstick_acl[10] == 0x00u &&
                replay_controller_rstick_acl[11] == 0x00u,
            "native BT controller right-stick replay has no unintended Wiimote buttons");
        passed &= expect_bytes(
            std::span<const std::uint8_t>(
                replay_controller_rstick_acl + 25,
                expected_encrypted_neutral_nunchuk.size()),
            expected_encrypted_neutral_nunchuk,
            "native BT controller right-stick replay leaves the encrypted Nunchuk wire payload neutral");
        expect_ir_bytes_published(
            replay_controller_rstick_acl,
            15u,
            25u,
            "native BT controller right-stick replay publishes visible IR bytes");
    }
    galaxy::set_runtime_settings(saved_replay_settings);
    const std::string replay_controller_rstick_trace_text =
        replay_controller_rstick_trace.str();
    if (replay_controller_rstick_trace_text.find("FAILED:") !=
        std::string::npos) {
        std::cerr << replay_controller_rstick_trace_text;
    }
    passed &= expect(
        replay_controller_rstick_trace_text.find("source=controller") !=
                std::string::npos &&
            replay_controller_rstick_trace_text.find("xinput=1") !=
                std::string::npos &&
            replay_controller_rstick_trace_text.find("ptrsrc=controller") !=
                std::string::npos &&
            replay_controller_rstick_trace_text.find("ir=1") !=
                std::string::npos,
        "native BT controller replay drives IR from XInput right stick in controller mode");

    struct ControllerPointerStressCase {
        const char* name;
        bool focused;
        int mouse_x;
        int mouse_y;
        int client_w;
        int client_h;
        const char* keys_hold_hex;
        bool xinput_connected;
        int thumb_rx;
        int thumb_ry;
        bool expect_ir;
        const char* expect_ptrsrc;
        bool expect_exact_ir;
        std::uint16_t expected_left_x;
        std::uint16_t expected_right_x;
        std::uint16_t expected_y;
    };
    // A 641x481 client fits 16:9 content to y=[60,421). The three samples
    // outside that half-open rectangle must be clipped instead of being
    // projected through letterbox pixels as sensor-bar coordinates.
    constexpr std::array<ControllerPointerStressCase, 7>
        controller_pointer_stress_cases{{
            {"center_mouse_buttons_held",
             true,
             320,
             240,
             641,
             481,
             "0x0000000000000007",
             false,
             0,
             0,
             true,
             "ptrsrc=replay",
             true,
             430u,
             590u,
             487u},
            {"top_left_mouse",
             true,
             0,
             0,
             641,
             481,
             "0x0000000000000000",
             false,
             0,
             0,
             false,
             "ptrsrc=none",
             false,
             0u,
             0u,
             0u},
            {"bottom_right_mouse",
             true,
             640,
             480,
             641,
             481,
             "0x0000000000000000",
             false,
             0,
             0,
             false,
             "ptrsrc=none",
             false,
             0u,
             0u,
             0u},
            {"clamped_mouse",
             true,
             -500,
             9999,
             641,
             481,
             "0x0000000000000000",
             false,
             0,
             0,
             false,
             "ptrsrc=none",
             false,
             0u,
             0u,
             0u},
            {"unfocused_disconnected_mouse_pointer",
             false,
             320,
             240,
             641,
             481,
             "0x0000000000000000",
             false,
             0,
             0,
             true,
             "ptrsrc=replay",
             true,
             430u,
             590u,
             487u},
            {"rstick_connected_unfocused",
             false,
             320,
             240,
             641,
             481,
             "0x0000000000000000",
             true,
             32767,
             0,
             true,
             "ptrsrc=controller",
             false,
             0u,
             0u,
             0u},
            {"rstick_disconnected_unfocused",
             false,
             320,
             240,
             641,
             481,
             "0x0000000000000000",
             false,
             32767,
             0,
             true,
             "ptrsrc=replay",
             true,
             430u,
             590u,
             487u},
        }};

    const auto write_controller_pointer_replay =
        [](const std::filesystem::path& path,
           const ControllerPointerStressCase& stress_case) {
            std::ostringstream replay;
            replay << "meta\tkey_order\t"
                   << "LButton,RButton,Space,Enter,Shift,LShift,RShift,"
                   << "Left,Up,Right,Down,1,2,A,B,C,D,E,I,J,K,L,Q,R,S,W,Z,"
                   << "Ctrl,LCtrl,RCtrl\n";
            replay << "elapsed_ms\tgame_pid\tgame_focused\tfg_pid\t"
                   << "mouse_screen_x\tmouse_screen_y\tmouse_client_x\t"
                   << "mouse_client_y\tclient_w\tclient_h\tkeys_hold_hex\t"
                   << "keys_trig_hex\tkeys_release_hex\txinput_connected\t"
                   << "xinput_buttons_hex\tleft_trigger\tright_trigger\t"
                   << "thumb_lx\tthumb_ly\tthumb_rx\tthumb_ry\n";
            replay << "0\t0\t" << (stress_case.focused ? 1 : 0)
                   << "\t0\t0\t0\t" << stress_case.mouse_x << '\t'
                   << stress_case.mouse_y << '\t' << stress_case.client_w
                   << '\t' << stress_case.client_h << '\t'
                   << stress_case.keys_hold_hex
                   << "\t0x0000000000000000\t0x0000000000000000\t"
                   << (stress_case.xinput_connected ? 1 : 0)
                   << "\t0x0000\t0\t0\t0\t0\t" << stress_case.thumb_rx
                   << '\t' << stress_case.thumb_ry << '\n';
            write_text_file(path, replay.str());
        };
    const auto decode_acl_basic_ir_pair =
        [](const std::uint8_t* acl, std::size_t offset) {
            struct DecodedBasicIrPair {
                std::uint16_t left_x;
                std::uint16_t left_y;
                std::uint16_t right_x;
                std::uint16_t right_y;
            };
            const std::uint8_t high = acl[offset + 2u];
            return DecodedBasicIrPair{
                static_cast<std::uint16_t>(
                    acl[offset] |
                    (static_cast<std::uint16_t>((high >> 4u) & 0x03u)
                     << 8u)),
                static_cast<std::uint16_t>(
                    acl[offset + 1u] |
                    (static_cast<std::uint16_t>((high >> 6u) & 0x03u)
                     << 8u)),
                static_cast<std::uint16_t>(
                    acl[offset + 3u] |
                    (static_cast<std::uint16_t>(high & 0x03u) << 8u)),
                static_cast<std::uint16_t>(
                    acl[offset + 4u] |
                    (static_cast<std::uint16_t>((high >> 2u) & 0x03u)
                     << 8u))};
        };

    galaxy::RuntimeSettings controller_pointer_stress_settings =
        saved_replay_settings;
    controller_pointer_stress_settings.input_mode =
        galaxy::RuntimeInputMode::Controller;
    galaxy::set_runtime_settings(controller_pointer_stress_settings);
    for (std::size_t i = 0; i < controller_pointer_stress_cases.size(); ++i) {
        const ControllerPointerStressCase& stress_case =
            controller_pointer_stress_cases[i];
        const std::filesystem::path replay_stress_path =
            std::filesystem::temp_directory_path() /
            ("galaxy_native_host_input_replay_controller_pointer_stress_" +
             std::string(stress_case.name) + ".tsv");
        write_controller_pointer_replay(replay_stress_path, stress_case);
        std::ostringstream replay_stress_trace;
        {
            const std::string replay_stress_string =
                replay_stress_path.string();
            ScopedEnv replay_log(
                "GALAXY_INPUT_REPLAY_LOG",
                replay_stress_string.c_str());
            ScopedStreamRedirect capture_replay_stress_trace(
                std::cerr, replay_stress_trace.rdbuf());
            set_input_report_mode(
                0x37,
                "native BT HID set-report-mode for controller pointer stress is consumed");
            const auto* stress_acl = read_bulk_acl(
                kRequest + 0x1900u +
                    static_cast<std::uint32_t>(i * 0x40u),
                31,
                stress_case.name);
            passed &= expect(
                stress_acl[8] == 0xA1 && stress_acl[9] == 0x37,
                "native BT controller pointer stress emits IR report mode");
            passed &= expect(
                stress_acl[10] == 0x00u && stress_acl[11] == 0x00u,
                "native BT controller pointer stress does not leak mouse/keyboard buttons");
            passed &= expect_bytes(
                std::span<const std::uint8_t>(
                    stress_acl + 25,
                    expected_encrypted_neutral_nunchuk.size()),
                expected_encrypted_neutral_nunchuk,
                "native BT controller pointer stress keeps the encrypted Nunchuk wire payload neutral");
            if (stress_case.expect_ir) {
                expect_ir_bytes_published(
                    stress_acl,
                    15u,
                    25u,
                    "native BT controller pointer stress publishes IR");
                if (stress_case.expect_exact_ir) {
                    const auto decoded = decode_acl_basic_ir_pair(
                        stress_acl, 15u);
                    passed &= expect(
                        decoded.left_x == stress_case.expected_left_x,
                        "native BT controller pointer stress left IR x");
                    passed &= expect(
                        decoded.right_x == stress_case.expected_right_x,
                        "native BT controller pointer stress right IR x");
                    passed &= expect(
                        decoded.left_y == stress_case.expected_y,
                        "native BT controller pointer stress left IR y");
                    passed &= expect(
                        decoded.right_y == stress_case.expected_y,
                        "native BT controller pointer stress right IR y");
                passed &= expect(
                        decoded.right_x - decoded.left_x == 160u,
                        "native BT controller pointer stress IR separation");
                }
            } else {
                expect_basic_ir_bytes_hidden(
                    stress_acl,
                    "native BT controller pointer stress hides IR");
            }
        }
        const std::string replay_stress_trace_text =
            replay_stress_trace.str();
        if (replay_stress_trace_text.find("FAILED:") != std::string::npos) {
            std::cerr << replay_stress_trace_text;
        }
        passed &= expect(
            replay_stress_trace_text.find("source=controller") !=
                std::string::npos,
            "native BT controller pointer stress keeps controller input source");
        passed &= expect(
            replay_stress_trace_text.find(stress_case.expect_ptrsrc) !=
                std::string::npos,
            "native BT controller pointer stress traces expected pointer source");
        passed &= expect(
            (replay_stress_trace_text.find("ir=1") != std::string::npos) ==
                stress_case.expect_ir,
            "native BT controller pointer stress traces expected IR active state");
    }
    galaxy::set_runtime_settings(saved_replay_settings);

    const std::array<std::byte, 2> unsupported_hid_output{
        std::byte{0xA2}, std::byte{0x99}};
    const std::vector<std::byte> unsupported_hid_acl =
        acl_packet(0x0040, unsupported_hid_output);
    passed &= expect_runtime_error(
        [&] { submit_acl_out(unsupported_hid_acl); },
        "native BT unsupported HID output report must hard-fail");

    const std::array<std::byte, 1> unsupported_register_write_data{
        std::byte{0x00}};
    const auto unsupported_register_write =
        wiimote_memory_write_report(
            0x04, 0xA40001, unsupported_register_write_data);
    passed &= expect_runtime_error(
        [&] {
            const std::vector<std::byte> packet =
                acl_packet(0x0040, unsupported_register_write);
            submit_acl_out(packet);
        },
        "native BT unsupported extension register write must hard-fail");

    const std::array<std::byte, 3> unsupported_link_control{
        std::byte{0x01}, std::byte{0x04}, std::byte{0x00}};
    passed &= expect_runtime_error(
        [&] { submit_hci(unsupported_link_control); },
        "native BT unsupported link-control HCI command must hard-fail");

    auto remote_name_wrong_address = remote_name_request;
    remote_name_wrong_address[3] = std::byte{0x99};
    passed &= expect_runtime_error(
        [&] { submit_hci(remote_name_wrong_address); },
        "native BT link-control command for unknown BD_ADDR must hard-fail");

    auto read_remote_features_wrong_handle = read_remote_features;
    read_remote_features_wrong_handle[3] = std::byte{0x01};
    passed &= expect_runtime_error(
        [&] { submit_hci(read_remote_features_wrong_handle); },
        "native BT link-control command for unknown handle must hard-fail");

    const std::array<std::byte, 8> unsupported_l2cap_psm{
        std::byte{0x02}, std::byte{0x58}, std::byte{0x04}, std::byte{0x00},
        std::byte{0x99}, std::byte{0x99}, std::byte{0x58}, std::byte{0x00}};
    passed &= expect_runtime_error(
        [&] {
            const std::vector<std::byte> packet =
                acl_packet(0x0001, unsupported_l2cap_psm);
            submit_acl_out(packet);
        },
        "native BT unsupported L2CAP PSM must hard-fail");

    passed &= expect_runtime_error(
        [&] {
            const std::array<std::byte, 8> duplicate_l2cap_control_conn{
                std::byte{0x02}, std::byte{0x41},
                std::byte{0x04}, std::byte{0x00},
                std::byte{0x11}, std::byte{0x00},
                std::byte{0x41}, std::byte{0x00}};
            const std::vector<std::byte> packet =
                acl_packet(0x0001, duplicate_l2cap_control_conn);
            submit_acl_out(packet);
        },
        "native BT duplicate L2CAP control channel must hard-fail");

    const std::array<std::byte, 8> malformed_l2cap_scid{
        std::byte{0x02}, std::byte{0x58}, std::byte{0x04}, std::byte{0x00},
        std::byte{0x11}, std::byte{0x00}, std::byte{0x02}, std::byte{0x00}};
    passed &= expect_runtime_error(
        [&] {
            const std::vector<std::byte> packet =
                acl_packet(0x0001, malformed_l2cap_scid);
            submit_acl_out(packet);
        },
        "native BT malformed L2CAP source CID must hard-fail");

    const std::array<std::byte, 8> unsupported_l2cap_signal{
        std::byte{0xFE}, std::byte{0x59}, std::byte{0x04}, std::byte{0x00},
        std::byte{0x40}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00}};
    passed &= expect_runtime_error(
        [&] {
            const std::vector<std::byte> packet =
                acl_packet(0x0001, unsupported_l2cap_signal);
            submit_acl_out(packet);
        },
        "native BT unsupported L2CAP signaling code must hard-fail");

    const std::array<std::byte, 8> unsupported_l2cap_config_flags{
        std::byte{0x04}, std::byte{0x5B}, std::byte{0x04}, std::byte{0x00},
        std::byte{0x40}, std::byte{0x00}, std::byte{0x01}, std::byte{0x00}};
    passed &= expect_runtime_error(
        [&] {
            const std::vector<std::byte> packet =
                acl_packet(0x0001, unsupported_l2cap_config_flags);
            submit_acl_out(packet);
        },
        "native BT unsupported L2CAP config flags must hard-fail");

    const std::array<std::byte, 8> unknown_l2cap_config_cid{
        std::byte{0x04}, std::byte{0x5A}, std::byte{0x04}, std::byte{0x00},
        std::byte{0x99}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00}};
    passed &= expect_runtime_error(
        [&] {
            const std::vector<std::byte> packet =
                acl_packet(0x0001, unknown_l2cap_config_cid);
            submit_acl_out(packet);
        },
        "native BT L2CAP config for an unknown local CID must hard-fail");

    const std::array<std::byte, 4> unknown_cid_payload{
        std::byte{0xA2}, std::byte{0x10}, std::byte{0x00}, std::byte{0x00}};
    passed &= expect_runtime_error(
        [&] {
            const std::vector<std::byte> packet =
                acl_packet(0x9999, unknown_cid_payload);
            submit_acl_out(packet);
        },
        "native BT outbound ACL on an unknown CID must hard-fail");

    const std::array<std::byte, 4> valid_set_report_mode{
        std::byte{0xA2}, std::byte{0x12}, std::byte{0x00}, std::byte{0x37}};
    std::vector<std::byte> malformed_acl =
        acl_packet(0x0040, valid_set_report_mode);
    malformed_acl[2] = std::byte{0xFF};
    malformed_acl[3] = std::byte{0x7F};
    passed &= expect_runtime_error(
        [&] { submit_acl_out(malformed_acl); },
        "native BT malformed ACL/L2CAP lengths must hard-fail");

    std::vector<std::byte> wrong_acl_handle =
        acl_packet(0x0040, valid_set_report_mode);
    wrong_acl_handle[0] = std::byte{0x01};
    wrong_acl_handle[1] = std::byte{0x21};
    passed &= expect_runtime_error(
        [&] { submit_acl_out(wrong_acl_handle); },
        "native BT ACL packet for wrong handle must hard-fail");

    std::vector<std::byte> wrong_acl_boundary =
        acl_packet(0x0040, valid_set_report_mode);
    wrong_acl_boundary[0] = std::byte{0x00};
    wrong_acl_boundary[1] = std::byte{0x11};
    passed &= expect_runtime_error(
        [&] { submit_acl_out(wrong_acl_boundary); },
        "native BT ACL packet with unsupported boundary flag must hard-fail");

    {
        const std::filesystem::path malformed_replay_path =
            std::filesystem::temp_directory_path() /
            "galaxy_native_host_bad_input_replay.tsv";
        {
            std::ofstream malformed(
                malformed_replay_path, std::ios::binary | std::ios::trunc);
            malformed << "elapsed_ms\tkeys_hold_hex\n0\t0x0\n";
        }
        const std::string malformed_replay_string =
            malformed_replay_path.string();
        ScopedEnv malformed_replay_log(
            "GALAXY_INPUT_REPLAY_LOG", malformed_replay_string.c_str());
        fake_ticks += kTestHidReportPeriodTicks;
        passed &= expect_runtime_error(
            [&] {
                memory.poll_native_bt_reconnect();
            },
            "native BT malformed input replay log must hard-fail");
    }

    const std::array<std::byte, 6> disconnect{
        std::byte{0x06}, std::byte{0x04}, std::byte{0x03},
        std::byte{0x00}, std::byte{0x01}, std::byte{0x13}};
    submit_bt_read(kRequest + 0x4D0, 2, 0x81, kBuffer, 32);
    acknowledge_ios_ack(guest_memory);
    submit_hci(disconnect);
    passed &= expect(
        expect_ios_reply(
            memory,
            guest_memory,
            kRequest + 0x4D0,
            6,
            7,
            "native BT disconnect command-status arrives"),
        "native BT disconnect command-status arrives");
    const auto* disconnect_status =
        reinterpret_cast<const std::uint8_t*>(memory.pointer(kBuffer, 6));
    passed &= expect(
        disconnect_status[0] == 0x0F && disconnect_status[1] == 0x04 &&
            disconnect_status[2] == 0x00 &&
            disconnect_status[3] == 0x01 &&
            disconnect_status[4] == 0x06 &&
            disconnect_status[5] == 0x04,
        "native BT disconnect status identifies the command");
    acknowledge_ios_reply(guest_memory);
    passed &= expect(
        expect_ios_reply(
            memory,
            guest_memory,
            kRequest,
            0,
            7,
            "native BT disconnect control transfer completes"),
        "native BT disconnect control transfer completes");
    acknowledge_ios_reply(guest_memory);
    submit_bt_read(kRequest + 0xFE0, 2, 0x81, kBuffer, 32);
    passed &= expect(
        expect_ios_reply(
            memory,
            guest_memory,
            kRequest + 0xFE0,
            6,
            7,
            "native BT disconnection-complete event arrives"),
        "native BT disconnection-complete event arrives");
    const auto* disconnection_complete =
        reinterpret_cast<const std::uint8_t*>(memory.pointer(kBuffer, 6));
    passed &= expect(
        disconnection_complete[0] == 0x05 &&
            disconnection_complete[1] == 0x04 &&
            disconnection_complete[2] == 0x00 &&
            disconnection_complete[3] == 0x00 &&
            disconnection_complete[4] == 0x01 &&
            disconnection_complete[5] == 0x13,
        "native BT disconnection-complete event returns handle and reason");
    acknowledge_ios_reply(guest_memory);

    return passed;
}

std::uint64_t fake_tick_source(void* user) {
    return *static_cast<std::uint64_t*>(user);
}

bool interrupt_register_foundation_works() {
    constexpr std::uint32_t kPiCausePhysical = 0x0C003000u;
    constexpr std::uint32_t kPiCauseCached = 0xCC003000u;
    constexpr std::array<std::uint32_t, 2> kPiCauseAliases{
        kPiCausePhysical,
        kPiCauseCached,
    };
    constexpr std::uint32_t kPiMaskPhysical = 0x0C003004u;
    constexpr std::uint32_t kPiMaskCached = 0xCC003004u;
    constexpr std::array<std::uint32_t, 2> kPiMaskAliases{
        kPiMaskPhysical,
        kPiMaskCached,
    };
    constexpr std::uint32_t kPiDspInterface = 0x00000040u;
    constexpr std::uint32_t kPiVi = 0x00000100u;
    constexpr std::uint32_t kPiPeToken = 0x00000200u;
    constexpr std::uint32_t kPiPeFinish = 0x00000400u;
    constexpr std::uint32_t kPiIpc = 0x00004000u;
    constexpr std::uint32_t kViDi0 = 0xCC002030u;
    constexpr std::uint32_t kPeControlPhysical = 0x0C00100Au;
    constexpr std::uint32_t kPeControl = 0xCC00100Au;
    constexpr std::array<std::uint32_t, 2> kPeControlAliases{
        kPeControlPhysical,
        kPeControl,
    };
    constexpr std::uint32_t kPeTokenPhysical = 0x0C00100Eu;
    constexpr std::uint32_t kPeToken = 0xCC00100Eu;
    constexpr std::array<std::uint32_t, 2> kPeTokenAliases{
        kPeTokenPhysical,
        kPeToken,
    };
    constexpr std::uint32_t kDspControl = 0xCC00500Au;

    ScopedEnv allow_retired_dsp(
        "GALAXY_ALLOW_RETIRED_DSP_MIXER_FOR_TESTS", "1");
    ScopedEnv disable_native_dsp("GALAXY_DSP_NATIVE", "0");
    galaxy::host::GuestAddressSpace memory;
    galaxy::GuestMemoryV1* guest_memory = memory.guest_memory();
    bool passed = true;

    const auto pi_cause = [&] {
        return galaxy::guest_load_u32(
            guest_memory, kPiCauseCached, nullptr, 0x80004000u);
    };
    passed &= expect(
        pi_cause() == 0u &&
            galaxy::guest_load_u32(
                guest_memory, kPiCausePhysical, nullptr, 0x80004000u) == 0u,
        "PI cause begins clear and is identical through both Broadway aliases");
    galaxy::guest_store_u32(
        guest_memory, kPiMaskPhysical, 0x00004542u, nullptr, 0x80004000u);
    passed &= expect(
        galaxy::guest_load_u32(
            guest_memory, kPiMaskCached, nullptr, 0x80004000u) ==
                0x00004542u &&
            pi_cause() == 0u,
        "guest PI mask remains ordinary shared register state and does not synthesize cause");
    for (const std::uint32_t alias : kPiCauseAliases) {
        galaxy::guest_store_u32(
            guest_memory, alias, 0x00001000u, nullptr, 0x80004000u);
    }
    passed &= expect(
        pi_cause() == 0u &&
            galaxy::guest_load_u32(
                guest_memory, kPiMaskCached, nullptr, 0x80004000u) ==
                0x00004542u,
        "exact PI cause W1C through both aliases is legal and a missing PI-only latch is a no-op");
    for (const std::uint32_t alias : kPiCauseAliases) {
        passed &= expect_runtime_error(
            [&] {
                static_cast<void>(galaxy::guest_load_u16(
                    guest_memory, alias, nullptr, 0x80004000u));
            },
            "PI cause rejects exact-base reads with the wrong width through both aliases");
        passed &= expect_runtime_error(
            [&] {
                galaxy::guest_store_u16(
                    guest_memory, alias, 0u, nullptr, 0x80004000u);
            },
            "PI cause rejects exact-base writes with the wrong width through both aliases");
        passed &= expect_runtime_error(
            [&] {
                static_cast<void>(galaxy::guest_load_u8(
                    guest_memory, alias + 1u, nullptr, 0x80004000u));
            },
            "PI cause rejects a byte read from inside the register through both aliases");
        passed &= expect_runtime_error(
            [&] {
                galaxy::guest_store_u8(
                    guest_memory,
                    alias + 3u,
                    0xFFu,
                    nullptr,
                    0x80004000u);
            },
            "PI cause rejects a byte write inside the register through both aliases");
        passed &= expect_runtime_error(
            [&] {
                static_cast<void>(galaxy::guest_load_u32(
                    guest_memory, alias + 1u, nullptr, 0x80004000u));
            },
            "PI cause rejects a misaligned 32-bit read through both aliases");
        passed &= expect_runtime_error(
            [&] {
                galaxy::guest_store_u32(
                    guest_memory, alias + 1u, 0u, nullptr, 0x80004000u);
            },
            "PI cause rejects a misaligned 32-bit write through both aliases");
        passed &= expect_runtime_error(
            [&] {
                static_cast<void>(galaxy::guest_load_u16(
                    guest_memory, alias - 1u, nullptr, 0x80004000u));
            },
            "PI cause rejects a read whose range crosses into the register");
        passed &= expect_runtime_error(
            [&] {
                galaxy::guest_store_u16(
                    guest_memory, alias - 1u, 0u, nullptr, 0x80004000u);
            },
            "PI cause rejects a write whose range crosses into the register");
        passed &= expect_runtime_error(
            [&] {
                static_cast<void>(galaxy::guest_load_u16(
                    guest_memory, alias + 3u, nullptr, 0x80004000u));
            },
            "PI cause rejects a read crossing the cause-mask register boundary");
        passed &= expect_runtime_error(
            [&] {
                galaxy::guest_store_u16(
                    guest_memory, alias + 3u, 0u, nullptr, 0x80004000u);
            },
            "PI cause rejects a write crossing the cause-mask register boundary");
    }
    galaxy::guest_store_u32(
        guest_memory, kPiMaskCached, 0xA5A55A5Au, nullptr, 0x80004000u);
    passed &= expect(
        galaxy::guest_load_u32(
            guest_memory, kPiMaskPhysical, nullptr, 0x80004000u) ==
            0xA5A55A5Au,
        "PI mask exact 32-bit writes and reads work through either Broadway alias");
    galaxy::guest_store_u32(
        guest_memory, kPiMaskPhysical, 0x00004542u, nullptr, 0x80004000u);
    for (const std::uint32_t alias : kPiMaskAliases) {
        passed &= expect_runtime_error(
            [&] {
                static_cast<void>(galaxy::guest_load_u16(
                    guest_memory, alias, nullptr, 0x80004000u));
            },
            "PI mask rejects exact-base reads with the wrong width through both aliases");
        passed &= expect_runtime_error(
            [&] {
                galaxy::guest_store_u16(
                    guest_memory, alias, 0u, nullptr, 0x80004000u);
            },
            "PI mask rejects exact-base writes with the wrong width through both aliases");
        passed &= expect_runtime_error(
            [&] {
                static_cast<void>(galaxy::guest_load_u8(
                    guest_memory, alias + 1u, nullptr, 0x80004000u));
            },
            "PI mask rejects a byte read from inside the register through both aliases");
        passed &= expect_runtime_error(
            [&] {
                galaxy::guest_store_u8(
                    guest_memory,
                    alias + 3u,
                    0xFFu,
                    nullptr,
                    0x80004000u);
            },
            "PI mask rejects a byte write inside the register through both aliases");
        passed &= expect_runtime_error(
            [&] {
                static_cast<void>(galaxy::guest_load_u32(
                    guest_memory, alias + 1u, nullptr, 0x80004000u));
            },
            "PI mask rejects a misaligned 32-bit read through both aliases");
        passed &= expect_runtime_error(
            [&] {
                galaxy::guest_store_u32(
                    guest_memory, alias + 1u, 0u, nullptr, 0x80004000u);
            },
            "PI mask rejects a misaligned 32-bit write through both aliases");
        passed &= expect_runtime_error(
            [&] {
                static_cast<void>(galaxy::guest_load_u16(
                    guest_memory, alias + 3u, nullptr, 0x80004000u));
            },
            "PI mask rejects a read crossing out of the register");
        passed &= expect_runtime_error(
            [&] {
                galaxy::guest_store_u16(
                    guest_memory, alias + 3u, 0u, nullptr, 0x80004000u);
            },
            "PI mask rejects a write crossing out of the register");
    }
    passed &= expect(
        pi_cause() == 0u &&
            galaxy::guest_load_u32(
                guest_memory, kPiMaskCached, nullptr, 0x80004000u) ==
                0x00004542u,
        "rejected PI cause/mask access shapes cannot mutate either register");

    galaxy::guest_store_u16(
        guest_memory, kViDi0, 0x8000u, nullptr, 0x80004000u);
    passed &= expect(
        (pi_cause() & kPiVi) == 0u,
        "VI status without its device enable does not assert PI");
    galaxy::guest_store_u16(
        guest_memory, kViDi0, 0x9000u, nullptr, 0x80004000u);
    const std::uint16_t vi_before_cause_reads = galaxy::guest_load_u16(
        guest_memory, kViDi0, nullptr, 0x80004000u);
    const std::uint32_t first_vi_cause = pi_cause();
    const std::uint32_t second_vi_cause = galaxy::guest_load_u32(
        guest_memory, kPiCausePhysical, nullptr, 0x80004000u);
    passed &= expect(
        first_vi_cause == kPiVi && second_vi_cause == first_vi_cause &&
            galaxy::guest_load_u16(
                guest_memory, kViDi0, nullptr, 0x80004000u) ==
                vi_before_cause_reads,
        "VI latch asserts PI and repeated cause reads are observational");
    galaxy::guest_store_u32(
        guest_memory, kPiCauseCached, kPiVi, nullptr, 0x80004000u);
    passed &= expect(
        (pi_cause() & kPiVi) != 0u &&
            galaxy::guest_load_u16(
                guest_memory, kViDi0, nullptr, 0x80004000u) ==
                vi_before_cause_reads,
        "PI cause W1C cannot acknowledge a live device-owned VI level");
    galaxy::guest_store_u16(
        guest_memory, kViDi0, 0x1000u, nullptr, 0x80004000u);
    passed &= expect(
        (pi_cause() & kPiVi) == 0u,
        "translated VI status clear deasserts its synthesized PI line");

    for (const std::uint32_t alias : kPeControlAliases) {
        passed &= expect_runtime_error(
            [&] {
                static_cast<void>(galaxy::guest_load_u32(
                    guest_memory, alias, nullptr, 0x80004000u));
            },
            "PE control rejects exact-base reads with the wrong width through both aliases");
        passed &= expect_runtime_error(
            [&] {
                galaxy::guest_store_u32(
                    guest_memory, alias, 0u, nullptr, 0x80004000u);
            },
            "PE control rejects exact-base writes with the wrong width through both aliases");
        passed &= expect_runtime_error(
            [&] {
                static_cast<void>(galaxy::guest_load_u8(
                    guest_memory, alias + 1u, nullptr, 0x80004000u));
            },
            "PE control rejects a byte read from inside the register through both aliases");
        passed &= expect_runtime_error(
            [&] {
                galaxy::guest_store_u8(
                    guest_memory,
                    alias + 1u,
                    0xFFu,
                    nullptr,
                    0x80004000u);
            },
            "PE control rejects a byte write inside the register through both aliases");
        passed &= expect_runtime_error(
            [&] {
                static_cast<void>(galaxy::guest_load_u16(
                    guest_memory, alias - 1u, nullptr, 0x80004000u));
            },
            "PE control rejects a read whose range crosses into the register");
        passed &= expect_runtime_error(
            [&] {
                galaxy::guest_store_u16(
                    guest_memory, alias - 1u, 0u, nullptr, 0x80004000u);
            },
            "PE control rejects a write whose range crosses into the register");
    }
    passed &= expect(
        galaxy::guest_load_u16(
            guest_memory, kPeControlPhysical, nullptr, 0x80004000u) == 0u &&
            galaxy::guest_load_u16(
                guest_memory, kPeControl, nullptr, 0x80004000u) == 0u &&
            pi_cause() == 0u,
        "rejected PE access shapes cannot mutate enables or private pending latches");

    for (const std::uint32_t alias : kPeTokenAliases) {
        passed &= expect_runtime_error(
            [&] {
                static_cast<void>(galaxy::guest_load_u32(
                    guest_memory, alias, nullptr, 0x80004000u));
            },
            "PE token rejects exact-base reads with the wrong width through both aliases");
        passed &= expect_runtime_error(
            [&] {
                galaxy::guest_store_u32(
                    guest_memory, alias, 0u, nullptr, 0x80004000u);
            },
            "PE token rejects exact-base writes with the wrong width through both aliases");
        passed &= expect_runtime_error(
            [&] {
                static_cast<void>(galaxy::guest_load_u8(
                    guest_memory, alias + 1u, nullptr, 0x80004000u));
            },
            "PE token rejects a byte read from inside the register through both aliases");
        passed &= expect_runtime_error(
            [&] {
                galaxy::guest_store_u8(
                    guest_memory,
                    alias + 1u,
                    0xFFu,
                    nullptr,
                    0x80004000u);
            },
            "PE token rejects a byte write inside the register through both aliases");
        passed &= expect_runtime_error(
            [&] {
                static_cast<void>(galaxy::guest_load_u16(
                    guest_memory, alias - 1u, nullptr, 0x80004000u));
            },
            "PE token rejects a read whose range crosses into the register");
        passed &= expect_runtime_error(
            [&] {
                galaxy::guest_store_u16(
                    guest_memory, alias - 1u, 0u, nullptr, 0x80004000u);
            },
            "PE token rejects a write whose range crosses into the register");
    }
    galaxy::guest_store_u16(
        guest_memory, kPeTokenPhysical, 0xCAFEu, nullptr, 0x80004000u);
    galaxy::guest_store_u16(
        guest_memory, kPeToken, 0x1234u, nullptr, 0x80004000u);
    passed &= expect(
        galaxy::guest_load_u16(
            guest_memory, kPeTokenPhysical, nullptr, 0x80004000u) == 0u &&
            galaxy::guest_load_u16(
                guest_memory, kPeToken, nullptr, 0x80004000u) == 0u,
        "PE token overlap guard preserves the existing exact-width guest-write policy without inventing token state");

    memory.raise_pe_token(0xBEEFu, true);
    passed &= expect(
        galaxy::guest_load_u16(
            guest_memory, kPeControl, nullptr, 0x80004000u) == 0x0000u &&
            (pi_cause() & kPiPeToken) == 0u,
        "PE token pending level remains private while its interrupt enable is clear");
    galaxy::guest_store_u16(
        guest_memory, kPeControl, 0x0001u, nullptr, 0x80004000u);
    passed &= expect(
        galaxy::guest_load_u16(
            guest_memory, kPeControl, nullptr, 0x80004000u) == 0x0001u &&
            pi_cause() == kPiPeToken && memory.take_pe_token() &&
            pi_cause() == kPiPeToken,
        "PE token enable asserts PI without exposing the write-only clear strobe");
    galaxy::guest_store_u16(
        guest_memory, kPeControl, 0x0005u, nullptr, 0x80004000u);
    passed &= expect(
        galaxy::guest_load_u16(
            guest_memory, kPeControl, nullptr, 0x80004000u) == 0x0001u &&
            pi_cause() == 0u,
        "PE token W1C acknowledges status while preserving its enable");

    memory.raise_pe_finish();
    passed &= expect(
        galaxy::guest_load_u16(
            guest_memory, kPeControl, nullptr, 0x80004000u) == 0x0001u &&
            (pi_cause() & kPiPeFinish) == 0u,
        "PE finish pending level remains private while its interrupt enable is clear");
    galaxy::guest_store_u16(
        guest_memory, kPeControl, 0x0003u, nullptr, 0x80004000u);
    passed &= expect(
        galaxy::guest_load_u16(
            guest_memory, kPeControl, nullptr, 0x80004000u) == 0x0003u &&
            pi_cause() == kPiPeFinish && memory.take_pe_finish() &&
            pi_cause() == kPiPeFinish,
        "PE finish enable asserts PI without exposing the write-only clear strobe");
    galaxy::guest_store_u16(
        guest_memory, kPeControl, 0x000Bu, nullptr, 0x80004000u);
    galaxy::guest_store_u16(
        guest_memory, kPeControl, 0x000Fu, nullptr, 0x80004000u);
    passed &= expect(
        galaxy::guest_load_u16(
            guest_memory, kPeControl, nullptr, 0x80004000u) == 0x0003u &&
            pi_cause() == 0u,
        "PE finish W1C deasserts PI and software cannot manufacture status bits");

    // Pixel Engine may latch a token and finish from the same completed GPU
    // work interval.  RMGE01's IRQ18/IRQ19 handlers each read PE_SR, OR only
    // their own write-only clear strobe, and write it back.  A read must never
    // expose the other pending source or the first handler would erase it.
    memory.raise_pe_token(0xA000u, true);
    memory.raise_pe_finish();
    galaxy::guest_store_u16(
        guest_memory, kPeControl, 0x0003u, nullptr, 0x80004000u);
    passed &= expect(
        galaxy::guest_load_u16(
            guest_memory, kPeControl, nullptr, 0x80004000u) == 0x0003u &&
            pi_cause() == (kPiPeToken | kPiPeFinish) &&
            memory.pe_token_pending() && memory.pe_finish_pending(),
        "co-latched PE token and finish expose only enables while asserting both PI causes");
    const std::uint16_t token_handler_control = galaxy::guest_load_u16(
        guest_memory, kPeControl, nullptr, 0x80004000u);
    galaxy::guest_store_u16(
        guest_memory,
        kPeControl,
        static_cast<std::uint16_t>(token_handler_control | 0x0004u),
        nullptr,
        0x80004000u);
    passed &= expect(
        galaxy::guest_load_u16(
            guest_memory, kPeControl, nullptr, 0x80004000u) == 0x0003u &&
            pi_cause() == kPiPeFinish &&
            !memory.pe_token_pending() && memory.pe_finish_pending(),
        "IRQ18-style PE read/modify/write clears only token and preserves co-latched finish");
    const std::uint16_t finish_handler_control = galaxy::guest_load_u16(
        guest_memory, kPeControl, nullptr, 0x80004000u);
    galaxy::guest_store_u16(
        guest_memory,
        kPeControl,
        static_cast<std::uint16_t>(finish_handler_control | 0x0008u),
        nullptr,
        0x80004000u);
    passed &= expect(
        galaxy::guest_load_u16(
            guest_memory, kPeControl, nullptr, 0x80004000u) == 0x0003u &&
            pi_cause() == 0u &&
            !memory.pe_token_pending() && !memory.pe_finish_pending(),
        "IRQ19-style PE read/modify/write independently clears the remaining finish level");
    passed &= expect_runtime_error(
        [&] {
            galaxy::guest_store_u16(
                guest_memory, kPeControl, 0x0010u, nullptr, 0x80004000u);
        },
        "unknown PE control bits hard-fail instead of becoming hidden state");

    send_dsp_mail(guest_memory, 0x80F3D001u);
    send_dsp_mail(guest_memory, 0u);
    const std::uint16_t dsp_status_masked_out = galaxy::guest_load_u16(
        guest_memory, kDspControl, nullptr, 0x80004000u);
    passed &= expect(
        (dsp_status_masked_out & 0x0080u) != 0u &&
            !memory.dsp_interrupt_pending() &&
            (pi_cause() & kPiDspInterface) == 0u,
        "DCD1 DIRQ latches DSPCR status without asserting PI while DSP mask is clear");
    galaxy::guest_store_u16(
        guest_memory,
        kDspControl,
        static_cast<std::uint16_t>(
            (dsp_status_masked_out & ~0x0080u) | 0x0100u),
        nullptr,
        0x80004000u);
    const std::uint16_t dsp_status_masked_in = galaxy::guest_load_u16(
        guest_memory, kDspControl, nullptr, 0x80004000u);
    passed &= expect(
        (dsp_status_masked_in & 0x0180u) == 0x0180u &&
            memory.dsp_interrupt_pending() &&
            pi_cause() == kPiDspInterface && memory.take_dsp_interrupt() &&
            pi_cause() == kPiDspInterface,
        "DSP status plus mask asserts shared PI and observation cannot consume it");
    galaxy::guest_store_u16(
        guest_memory,
        kDspControl,
        static_cast<std::uint16_t>(dsp_status_masked_in | 0x0080u),
        nullptr,
        0x80004000u);
    passed &= expect(
        (galaxy::guest_load_u16(
             guest_memory, kDspControl, nullptr, 0x80004000u) &
         0x0180u) == 0x0100u &&
            !memory.dsp_interrupt_pending() &&
            pi_cause() == 0u,
        "translated DSPCR W1C clears DIRQ status, preserves mask, and deasserts PI");

    constexpr std::uint32_t kIpcPathAddress = 0x80010000u;
    constexpr std::uint32_t kIpcRequest = 0x80010100u;
    constexpr std::string_view kIpcPath = "/dev/fs";
    std::vector<std::byte> ipc_path;
    ipc_path.reserve(kIpcPath.size() + 1u);
    for (const char ch : kIpcPath) {
        ipc_path.push_back(static_cast<std::byte>(ch));
    }
    ipc_path.push_back(std::byte{0});
    memory.copy(kIpcPathAddress, ipc_path);
    memory.write_u32(kIpcRequest, 1u);
    memory.write_u32(kIpcRequest + 0x08u, 0u);
    memory.write_u32(kIpcRequest + 0x0Cu, kIpcPathAddress);
    memory.write_u32(kIpcRequest + 0x20u, 0u);
    submit_ios_request(guest_memory, kIpcRequest);
    const std::uint32_t ipc_control_before_cause = ipc_control(guest_memory);
    passed &= expect(
        (ipc_control_before_cause & 0x36u) == 0x36u &&
            pi_cause() == kPiIpc &&
            galaxy::guest_load_u32(
                guest_memory, kPiCausePhysical, nullptr, 0x80004000u) ==
                kPiIpc &&
            ipc_control(guest_memory) == ipc_control_before_cause,
        "enabled IPC Y1/Y2 state asserts PI and cause reads do not acknowledge it");
    acknowledge_ios_reply(guest_memory);
    passed &= expect(
        pi_cause() == 0u,
        "translated IPC W1C acknowledgement deasserts synthesized PI");
    passed &= expect(
        galaxy::guest_load_u32(
            guest_memory, kPiMaskCached, nullptr, 0x80004000u) ==
            0x00004542u,
        "device activity and cause reads leave the guest PI mask untouched");

    return passed;
}

struct AiDeadlineProbe {
    galaxy::host::GuestAddressSpace* address_space{};
    std::vector<std::uint64_t> deadlines;
    std::vector<std::uint64_t> submit_counts_at_callback;
};

void capture_ai_deadline(void* user, std::uint64_t deadline_ticks) {
    auto& probe = *static_cast<AiDeadlineProbe*>(user);
    probe.deadlines.push_back(deadline_ticks);
    probe.submit_counts_at_callback.push_back(
        probe.address_space != nullptr
            ? probe.address_space->ai_audio_submit_count()
            : std::numeric_limits<std::uint64_t>::max());
}

struct AiMmioServiceDecisionProbe {
    galaxy::host::AiDmaMmioServiceDecision decision{
        galaxy::host::AiDmaMmioServiceDecision::ExternalInterruptsMasked};
};

galaxy::host::AiDmaMmioServiceDecision provide_ai_mmio_service_decision(
    void* user) noexcept {
    return static_cast<AiMmioServiceDecisionProbe*>(user)->decision;
}

bool ai_dma_interrupt_entry_masks_and_restores_ee() {
    galaxy::PpcContext context{};
    context.msr = 0x0000D031u;
    context.pc = 0x80001234u;
    context.gpr[1] = 0x817FFFE0u;
    context.gpr[3] = 0xAABBCCDDu;
    context.time_base_offset = 11u;
    context.decrementer_start_ticks = 22u;
    context.decrementer_start_value = 33u;
    context.hid2 = 44u;
    const galaxy::PpcContext outer = context;

    bool handler_called = false;
    bool handler_saw_masked_ee = false;
    const auto translated_ai_handler = [&](galaxy::PpcContext& handler_context) {
        handler_called = true;
        handler_saw_masked_ee =
            (handler_context.msr &
             galaxy::interrupt::kMsrExternalInterruptEnable) == 0u &&
            handler_context.msr ==
                (outer.msr &
                 ~galaxy::interrupt::kMsrExternalInterruptEnable);
        handler_context.msr = 0u;
        handler_context.pc = 0x804C8140u;
        handler_context.gpr[1] = 0x817FF000u;
        handler_context.gpr[3] = 5u;
        handler_context.time_base_offset = 111u;
        handler_context.decrementer_start_ticks = 222u;
        handler_context.decrementer_start_value = 333u;
        handler_context.hid2 = 444u;
    };

    galaxy::interrupt::enter_guest_handler(
        context, galaxy::interrupt::ai_dma_msr_clear_bits());
    translated_ai_handler(context);
    galaxy::interrupt::restore_outer_context(context, outer);

    bool passed = expect(
        galaxy::interrupt::ai_dma_msr_clear_bits() ==
            galaxy::interrupt::kMsrExternalInterruptEnable,
        "AI DMA is classified as a Broadway external interrupt");
    passed &= expect(
        handler_called && handler_saw_masked_ee,
        "AI DMA guest handler executes with MSR[EE] clear");
    passed &= expect(
        context.msr == outer.msr && context.pc == outer.pc &&
            context.gpr[1] == outer.gpr[1] &&
            context.gpr[3] == outer.gpr[3],
        "AI DMA interrupt return restores the outer register snapshot including MSR[EE]");
    passed &= expect(
        context.time_base_offset == 111u &&
            context.decrementer_start_ticks == 222u &&
            context.decrementer_start_value == 333u && context.hid2 == 444u,
        "AI DMA interrupt return preserves timer progress made inside the handler");
    return passed;
}

bool translated_exception_entry_contract_works() {
    using galaxy::interrupt::kDecrementerExceptionEntry;
    using galaxy::interrupt::kExceptionHandlerTable;
    using galaxy::interrupt::kFloatingPointUnavailableExceptionEntry;

    galaxy::PpcContext context{};
    context.msr = 0x0000D033u;
    context.pc = 0x80001234u;
    context.gpr[3] = 0x33333333u;
    context.gpr[4] = 0x44444444u;
    constexpr std::uint32_t kGuestContext = 0x809C3060u;

    galaxy::interrupt::enter_translated_exception_wrapper(
        context, kDecrementerExceptionEntry, kGuestContext);

    bool passed = expect(
        galaxy::interrupt::kWiiDiscDolEntryMsr == 0x00002032u &&
            (galaxy::interrupt::kWiiDiscDolEntryMsr &
             (galaxy::interrupt::kMsrFloatingPointAvailable |
              galaxy::interrupt::kMsrInstructionRelocation |
              galaxy::interrupt::kMsrDataRelocation |
              galaxy::interrupt::kMsrRecoverableException)) ==
                0x00002032u &&
            (galaxy::interrupt::kWiiDiscDolEntryMsr &
             (galaxy::interrupt::kMsrExternalInterruptEnable |
              galaxy::interrupt::kMsrProblemState)) == 0u,
        "Wii disc DOL entry begins in translated supervisor mode with FP/IR/DR/RI and EE masked");
    passed &= expect(
        kExceptionHandlerTable +
                kDecrementerExceptionEntry.exception * sizeof(std::uint32_t) ==
            0x80003020u,
        "decrementer exception resolves through the exact OS exception-table slot");
    passed &= expect(
        kDecrementerExceptionEntry.exception == 8u &&
            kDecrementerExceptionEntry.wrapper == 0x804A259Cu,
        "decrementer exception enters RMGE01's translated wrapper");
    passed &= expect(
        context.pc == kDecrementerExceptionEntry.wrapper &&
            context.gpr[3] == kDecrementerExceptionEntry.exception &&
            context.gpr[4] == kGuestContext &&
            context.gpr[5] == kDecrementerExceptionEntry.wrapper &&
            context.spr[272] == 0x44444444u &&
            context.spr[26] == kDecrementerExceptionEntry.wrapper &&
            context.spr[27] == context.msr &&
            (context.cr & 0xF0000000u) == 0x40000000u,
        "translated exception entry reproduces the exact recoverable low-vector register state");
    passed &= expect(
        context.msr ==
            galaxy::interrupt::os_exception_handler_msr(0x0000D033u) &&
            context.msr == 0x00001030u,
        "translated exception entry applies the exact hardware and RMGE01 low-vector MSR transition");
    passed &= expect(
        galaxy::interrupt::hardware_exception_srr1(0xFFFFFFFFu) ==
            0x87C0FFFFu,
        "recoverable exception SRR1 retains only the MPC750/Gekko-defined MSR subset");
    passed &= expect(
        galaxy::interrupt::hardware_exception_msr(0x0005FF77u) ==
                0x00011041u &&
            galaxy::interrupt::os_exception_handler_msr(0x0005FF77u) ==
                0x00011071u,
        "exception entry clears POW/EE/PR/FP/FE/trace/translation/PM/RI and assigns LE from ILE before the OS vector restores IR/DR");
    passed &= expect(
        galaxy::interrupt::exception_is_recoverable(0x00000002u) &&
            !galaxy::interrupt::exception_is_recoverable(0x00000000u),
        "exception entry distinguishes recoverable and default-handler SRR1 paths");
    passed &= expect(
        galaxy::interrupt::physical_context_pointer(kGuestContext) ==
            0x009C3060u,
        "exception entry validates the physical low-memory context alias exactly");
    passed &= expect(
        kExceptionHandlerTable +
                kFloatingPointUnavailableExceptionEntry.exception *
                    sizeof(std::uint32_t) ==
            0x8000301Cu &&
            kFloatingPointUnavailableExceptionEntry.exception == 7u &&
            kFloatingPointUnavailableExceptionEntry.wrapper == 0x804A3C9Cu,
        "FPU-unavailable exception resolves to RMGE01's exact translated handler");

    galaxy::PpcContext fpu_context{};
    fpu_context.msr = 0x0000D033u;
    galaxy::interrupt::enter_translated_exception_wrapper(
        fpu_context,
        kFloatingPointUnavailableExceptionEntry,
        kGuestContext);
    passed &= expect(
        fpu_context.pc == kFloatingPointUnavailableExceptionEntry.wrapper &&
            fpu_context.gpr[3] ==
                kFloatingPointUnavailableExceptionEntry.exception &&
            fpu_context.gpr[4] == kGuestContext &&
            (fpu_context.msr &
             galaxy::interrupt::kMsrExternalInterruptEnable) == 0u &&
            (fpu_context.msr & galaxy::kMsrFloatingPointAvailable) == 0u,
        "FPU-unavailable entry applies the exact post-vector MSR and leaves FP disabled for the translated handler to enable");
    return passed;
}

bool fpu_retry_provenance_lifecycle_works() {
    constexpr std::uint32_t kFirstPc = 0x803A02ACu;
    constexpr std::uint32_t kFirstContext = 0x80650878u;
    constexpr std::uint32_t kSecondPc = 0x80394118u;
    constexpr std::uint32_t kSecondContext = 0x806BDBC8u;

    galaxy::interrupt::FpuRetryProvenance provenance{};
    bool passed = expect(
        !provenance.active() &&
            !provenance.begin(0u, kFirstContext) &&
            !provenance.begin(kFirstPc, 0u),
        "FPU retry provenance starts empty and rejects incomplete identities");
    passed &= expect(
        provenance.begin(kFirstPc, kFirstContext) &&
            provenance.active() &&
            provenance.matches(kFirstPc, kFirstContext),
        "FPU retry provenance acquires one exact PC/context identity");
    passed &= expect(
        !provenance.begin(kSecondPc, kSecondContext) &&
            !provenance.matches(kSecondPc, kSecondContext) &&
            provenance.matches(kFirstPc, kFirstContext),
        "an overlapping exception-7 retry is rejected without replacing its owner");

    // The exception-7 RFI is the lifetime boundary, not the surrounding host
    // callback frame. A pending asynchronous exception may switch contexts and
    // begin a second lazy-FPU retry after this clear but before that frame has
    // physically returned.
    provenance.clear();
    passed &= expect(
        !provenance.active() &&
            provenance.begin(kSecondPc, kSecondContext) &&
            provenance.matches(kSecondPc, kSecondContext),
        "completed exception-7 provenance retires before asynchronous arbitration");
    provenance.clear();
    passed &= expect(
        !provenance.active() && provenance.retry_pc() == 0u &&
            provenance.retry_context() == 0u,
        "FPU retry provenance clears both identity fields on every unwind");
    return passed;
}

bool interrupt_scheduler_handoff_linearity_policy_works() {
    using galaxy::interrupt::SchedulerHandoffSource;
    using galaxy::interrupt::should_defer_scheduler_handoff;

    bool passed = expect(
        should_defer_scheduler_handoff(
            SchedulerHandoffSource::Ai, true, true, false),
        "active AI interrupt defers a cross-context scheduler handoff");
    passed &= expect(
        !should_defer_scheduler_handoff(
            SchedulerHandoffSource::Ai, true, false, false) &&
            !should_defer_scheduler_handoff(
                SchedulerHandoffSource::Ai, false, true, false),
        "AI scheduler handoff stays unchanged outside an enabled active interrupt");
    passed &= expect(
        !should_defer_scheduler_handoff(
            SchedulerHandoffSource::Ai, true, true, true),
        "AI nested self-resume stays on the established self-resume path");

    passed &= expect(
        should_defer_scheduler_handoff(
            SchedulerHandoffSource::Ipc, true, true, false) &&
            should_defer_scheduler_handoff(
                SchedulerHandoffSource::Ipc, true, true, true),
        "IPC keeps its existing whole-handler deferral policy");
    passed &= expect(
        should_defer_scheduler_handoff(
            SchedulerHandoffSource::Vi, true, true, false) &&
            !should_defer_scheduler_handoff(
                SchedulerHandoffSource::Vi, true, true, true),
        "VI keeps its existing cross-context-only deferral policy");
    passed &= expect(
        should_defer_scheduler_handoff(
            SchedulerHandoffSource::Di, true, true, false) &&
            !should_defer_scheduler_handoff(
                SchedulerHandoffSource::Di, true, true, true),
        "DI keeps its existing cross-context-only deferral policy");
    return passed;
}

}  // namespace

bool native_dsp_mram_transaction_path_works(
    galaxy::host::GuestAddressSpace& memory) {
    constexpr std::uint32_t kMramAddress = 0x80006000u;
    constexpr std::uint32_t kDspByteAddress = 0x0080u;
    constexpr std::uint32_t kSpanSize = 32u;

    NativeDspMramServiceProbe probe{};
    galaxy::GuestMemoryV1* const guest_memory = memory.guest_memory();
    const auto saved_notify = guest_memory->notify_write;
    std::array<std::byte, kSpanSize> saved_mram{};
    std::memcpy(
        saved_mram.data(), memory.pointer(kMramAddress, kSpanSize), kSpanSize);

    g_native_dsp_mram_service_probe = &probe;
    guest_memory->notify_write = &capture_native_dsp_mram_dirty_range;
    bool passed = expect(
        memory.set_native_dsp_service_wake_callback(
            &publish_native_dsp_mram_test_wake, &probe),
        "native DSP MRAM service binds the product safepoint wake");

    galaxy::DspHardwareServices services =
        galaxy::host::NativeDspBoundaryTestAccess::services();
    passed &= expect(
        services.external_read_span != nullptr &&
            services.external_write_span != nullptr &&
            services.external_read_byte == nullptr &&
            services.external_write_byte == nullptr,
        "product native DSP hardware services expose only whole-span MRAM callbacks");
    // A DMA must remain functional with both byte callbacks absent. This
    // proves dsp_run_dma selected the production span contract and that the
    // product service table cannot silently fall back to worker-side bytes.

    galaxy::DspContext context{};
    context.pc = 0x0600u;
    context.hardware = services;
    context.hardware_user = &memory;
    for (std::uint32_t offset = 0u; offset < kSpanSize; ++offset) {
        galaxy::dsp_dma_write_byte(
            context,
            false,
            kDspByteAddress + offset,
            static_cast<std::uint8_t>(0x40u + offset));
    }
    std::memset(memory.pointer(kMramAddress, kSpanSize), 0x11, kSpanSize);
    context.ifx[galaxy::kDspIfxDsmah] =
        static_cast<std::uint16_t>(kMramAddress >> 16u);
    context.ifx[galaxy::kDspIfxDsmal] =
        static_cast<std::uint16_t>(kMramAddress);
    context.ifx[galaxy::kDspIfxDspa] =
        static_cast<std::uint16_t>(kDspByteAddress / 2u);
    context.ifx[galaxy::kDspIfxDscr] = 0x0001u;
    context.ifx[galaxy::kDspIfxDsbl] =
        static_cast<std::uint16_t>(kSpanSize);

    std::atomic<bool> write_succeeded{};
    std::thread write_worker([&] {
        try {
            galaxy::dsp_run_dma(context);
            write_succeeded.store(true, std::memory_order_release);
        } catch (const galaxy::DspHardTrap&) {
            write_succeeded.store(false, std::memory_order_release);
        }
    });
    const bool write_wake = wait_for_native_dsp_test(
        [&] {
            return probe.wake_count.load(std::memory_order_acquire) >= 1u;
        },
        std::chrono::seconds(1));
    std::array<std::byte, kSpanSize> untouched{};
    untouched.fill(std::byte{0x11});
    const bool write_invisible_before_service =
        std::memcmp(
            memory.pointer(kMramAddress, kSpanSize),
            untouched.data(),
            untouched.size()) == 0;
    memory.poll_native_dsp();
    write_worker.join();
    bool write_exact = true;
    const std::byte* const written =
        memory.pointer(kMramAddress, kSpanSize);
    for (std::uint32_t offset = 0u; offset < kSpanSize; ++offset) {
        write_exact = write_exact &&
            written[offset] == static_cast<std::byte>(0x40u + offset);
    }
    passed &= expect(
        write_wake && write_invisible_before_service && write_exact &&
            write_succeeded.load(std::memory_order_acquire) &&
            probe.dirty_count.load(std::memory_order_acquire) == 1u &&
            probe.dirty_address.load(std::memory_order_acquire) ==
                kMramAddress &&
            probe.dirty_size.load(std::memory_order_acquire) == kSpanSize,
        "product DSP-to-MRAM DMA commits one coherent span and emits one exact dirty range");

    std::array<std::byte, kSpanSize> read_source{};
    for (std::uint32_t offset = 0u; offset < kSpanSize; ++offset) {
        read_source[offset] = static_cast<std::byte>(0xE0u - offset);
        galaxy::dsp_dma_write_byte(
            context, false, kDspByteAddress + offset, 0x77u);
    }
    std::memcpy(
        memory.pointer(kMramAddress, kSpanSize),
        read_source.data(),
        read_source.size());
    context.ifx[galaxy::kDspIfxDscr] = 0x0000u;
    context.ifx[galaxy::kDspIfxDsbl] =
        static_cast<std::uint16_t>(kSpanSize);
    std::atomic<bool> read_succeeded{};
    std::thread read_worker([&] {
        try {
            galaxy::dsp_run_dma(context);
            read_succeeded.store(true, std::memory_order_release);
        } catch (const galaxy::DspHardTrap&) {
            read_succeeded.store(false, std::memory_order_release);
        }
    });
    const bool read_wake = wait_for_native_dsp_test(
        [&] {
            return probe.wake_count.load(std::memory_order_acquire) >= 2u;
        },
        std::chrono::seconds(1));
    bool dram_invisible_before_service = true;
    for (std::uint32_t offset = 0u; offset < kSpanSize; ++offset) {
        dram_invisible_before_service = dram_invisible_before_service &&
            galaxy::dsp_dma_read_byte(
                context, false, kDspByteAddress + offset) == 0x77u;
    }
    memory.poll_native_dsp();
    read_worker.join();
    bool read_exact = true;
    for (std::uint32_t offset = 0u; offset < kSpanSize; ++offset) {
        read_exact = read_exact &&
            galaxy::dsp_dma_read_byte(
                context, false, kDspByteAddress + offset) ==
                std::to_integer<std::uint8_t>(read_source[offset]);
    }
    const auto stats = memory.dsp_native_mram_transaction_stats();
    passed &= expect(
        read_wake && dram_invisible_before_service && read_exact &&
            read_succeeded.load(std::memory_order_acquire) &&
            stats.read_requests == 1u && stats.write_requests == 1u &&
            stats.completed_services == 2u && stats.service_calls == 2u &&
            stats.maximum_in_flight == 1u && stats.timeout_failures == 0u &&
            stats.failed_services == 0u,
        "product MRAM telemetry proves two whole-span services and no worker-side fallback");

    // Mapping ownership is CPU-side too: the worker may publish an
    // arithmetically valid address, but it cannot inspect GuestMemory regions.
    // The service rejects the unmapped span before a byte or dirty range is
    // committed, then the generated DSP path turns that rejection into a hard
    // trap rather than fabricating success.
    constexpr std::uint32_t kUnmappedMramAddress = 0x81800000u;
    context.pc = 0x0601u;
    context.ifx[galaxy::kDspIfxDsmah] =
        static_cast<std::uint16_t>(kUnmappedMramAddress >> 16u);
    context.ifx[galaxy::kDspIfxDsmal] =
        static_cast<std::uint16_t>(kUnmappedMramAddress);
    context.ifx[galaxy::kDspIfxDscr] = 0x0001u;
    context.ifx[galaxy::kDspIfxDsbl] =
        static_cast<std::uint16_t>(kSpanSize);
    std::atomic<bool> invalid_hard_trapped{};
    std::thread invalid_worker([&] {
        try {
            galaxy::dsp_run_dma(context);
        } catch (const galaxy::DspHardTrap&) {
            invalid_hard_trapped.store(true, std::memory_order_release);
        }
    });
    const bool invalid_wake = wait_for_native_dsp_test(
        [&] {
            return probe.wake_count.load(std::memory_order_acquire) >= 3u;
        },
        std::chrono::seconds(1));
    passed &= expect_runtime_error(
        [&] { memory.poll_native_dsp(); },
        "CPU MRAM service hard-fails an arithmetically valid but unmapped DSP span");
    invalid_worker.join();
    const auto rejected_stats = memory.dsp_native_mram_transaction_stats();
    passed &= expect(
        invalid_wake &&
            invalid_hard_trapped.load(std::memory_order_acquire) &&
            probe.dirty_count.load(std::memory_order_acquire) == 1u &&
            rejected_stats.service_calls == 3u &&
            rejected_stats.completed_services == 2u &&
            rejected_stats.failed_services == 1u,
        "unmapped product MRAM span rejects coherently with no write or dirty publication");

    std::memcpy(
        memory.pointer(kMramAddress, kSpanSize),
        saved_mram.data(),
        saved_mram.size());
    guest_memory->notify_write = saved_notify;
    g_native_dsp_mram_service_probe = nullptr;
    passed &= expect(
        memory.set_native_dsp_service_wake_callback(nullptr, nullptr),
        "native DSP MRAM test wake detaches only after the transaction slot is idle");
    return passed;
}

bool flat_guest_owner_abi_boundary_works() {
    bool passed = true;
    {
        ScopedEnv use_flat_guest_ram("GALAXY_FLAT_GUEST_MEMORY", "0");
        galaxy::host::GuestAddressSpace ordinary;
        passed &= expect(
            galaxy::guest_flat_read_base(ordinary.guest_memory()) == nullptr,
            "default guest memory does not publish the flat-read capability");
    }
    {
        ScopedEnv use_flat_guest_ram("GALAXY_FLAT_GUEST_MEMORY", "1");
        auto first = std::make_unique<galaxy::host::GuestAddressSpace>();
        const auto* first_base =
            galaxy::guest_flat_read_base(first->guest_memory());
        passed &= expect(
            first_base == reinterpret_cast<const std::byte*>(
                              galaxy::host::flat_guest_memory::kGuestBase),
            "flat owner publishes the exact guest-view capability");
        first->write_u32(0x80002040u, 0x12345678u);
        passed &= expect(
            galaxy::guest_load_flat_or_checked_u32(
                first_base, first->guest_memory(), 0xC0002040u,
                nullptr, 0u) == 0x12345678u,
            "host write reaches translated alias read through shared RAM");

        {
            galaxy::host::GuestAddressSpace concurrent;
            passed &= expect(
                galaxy::guest_flat_read_base(concurrent.guest_memory()) == nullptr,
                "second live owner falls back to independent checked memory");
            concurrent.write_u32(0x80002040u, 0x87654321u);
            passed &= expect(
                galaxy::guest_load_flat_or_checked_u32(
                    galaxy::guest_flat_read_base(concurrent.guest_memory()),
                    concurrent.guest_memory(), 0xC0002040u,
                    nullptr, 0u) == 0x87654321u &&
                    first->read_u32(0xC0002040u) == 0x12345678u,
                "concurrent vector-backed guest retains its own RAM image");
        }
        first.reset();
        galaxy::host::GuestAddressSpace sequential;
        passed &= expect(
            galaxy::guest_flat_read_base(sequential.guest_memory()) != nullptr &&
                sequential.read_u32(0x80002040u) == 0u,
            "next flat owner receives reset RAM and a fresh capability");
    }
    return passed;
}


std::uint64_t g_synthetic_pointer_provider_reads = 0u;

galaxy::host::HostPointerState counted_synthetic_pointer_provider() noexcept {
    ++g_synthetic_pointer_provider_reads;
    return g_timed_host_pointer;
}

bool synthetic_kpad_pointer_read_owns_press_coordinates_and_fresh_geometry() {
    using namespace galaxy::host;
    bool passed = true;
    GuestAddressSpace memory;
    ScopedHostPointerProvider provider(&counted_synthetic_pointer_provider);
    g_synthetic_pointer_provider_reads = 0u;
    g_timed_host_pointer = {};
    g_timed_host_pointer.absolute_valid = true;
    g_timed_host_pointer.inside_client = true;
    g_timed_host_pointer.window_focused = false;
    g_timed_host_pointer.client_width = 641;
    g_timed_host_pointer.client_height = 481;
    g_timed_host_pointer.client_x = 600;
    g_timed_host_pointer.client_y = 240;
    g_timed_host_pointer.absolute_sequence = 9u;
    g_timed_host_pointer.absolute_acquired_ms = 2'020u;
    // Fresh geometry must ignore raw held mouse levels. Only the already
    // owned, once-acquired HID button path may deliver A/B.
    g_timed_host_pointer.left_button = true;
    g_timed_host_pointer.debug_window = 0u;

    SyntheticKpadPointerSample point_x{
        -0.75f, 0.0f, true, true, SyntheticKpadPointerOrigin::PhysicalMouse,
        HostPointerSequenceDomain::ButtonTransition, 1u, 2'000u, 7u, 100u};
    SyntheticKpadPointerSample point_y{
        0.625f, 0.0f, true, true, SyntheticKpadPointerOrigin::PhysicalMouse,
        HostPointerSequenceDomain::AbsolutePosition, 1u, 2'010u, 7u, 200u};
    const auto publish = [&](const SyntheticKpadPointerSample& sample,
                             std::uint32_t mouse, std::uint32_t other) {
        galaxy::input::WiimoteInputSnapshot hid{};
        hid.buttons.a = ((mouse | other) & 0x0800u) != 0u;
        hid.buttons.b = ((mouse | other) & 0x0400u) != 0u;
        memory.set_latest_native_hid_snapshot(hid, sample.x, sample.y, sample.active);
        memory.capture_synthetic_kpad_pointer(sample, mouse, other);
    };
    const auto read = [&](bool retain = true, std::uint64_t generation = 7u) {
        const auto buttons = memory.consume_synthetic_kpad_buttons(retain);
        return std::pair{buttons,
            memory.consume_synthetic_kpad_pointer(buttons, retain, generation)};
    };
    publish(point_x, 0x0800u, 0u);
    publish(point_y, 0u, 0u);
    auto frame = read();
    passed &= expect(frame.first.hold == 0x0800u && frame.first.trigger == 0x0800u &&
        frame.first.retained_press == 0x0800u &&
        frame.second.selection == SyntheticKpadPointerSelection::RetainedMousePress &&
        frame.second.sample.x == point_x.x && frame.second.sample.domain == point_x.domain &&
        frame.second.sample.sequence == 1u && frame.second.sample.acquired_ms == 2'000u &&
        frame.second.sample.production_wii_ticks == 100u &&
        frame.second.stored_domain == point_y.domain && frame.second.stored_sequence == 1u &&
        frame.second.stored_acquired_ms == 2'010u && frame.second.stored_x == point_y.x &&
        g_synthetic_pointer_provider_reads == 0u,
        "mouse press at X then newer neutral Y reaches retained KPAD trigger at X with its exact original identity");
    frame = read();
    passed &= expect(frame.first.release == 0x0800u && frame.first.trigger == 0u &&
        frame.second.selection == SyntheticKpadPointerSelection::ButtonEdge &&
        g_synthetic_pointer_provider_reads == 0u,
        "retained mouse press releases once on next read without consuming another physical edge");
    frame = read();
    passed &= expect(frame.first.hold == 0u &&
        frame.second.selection == SyntheticKpadPointerSelection::FreshAbsolute &&
        frame.second.sample.active && frame.second.sample.x > 0.70f &&
        frame.second.sample.domain == HostPointerSequenceDomain::AbsolutePosition &&
        frame.second.sample.sequence == 9u && frame.second.sample.acquired_ms == 2'020u &&
        frame.second.sample.production_wii_ticks == 0u &&
        memory.latest_native_pointer_x() == point_y.x &&
        memory.latest_native_hid_snapshot()->buttons.a == false &&
        g_synthetic_pointer_provider_reads == 1u,
        "button-free unfocused game read gets newest absolute geometry without rewriting immutable HID snapshot or claiming its production tag");

    g_timed_host_pointer.inside_client = false;
    g_timed_host_pointer.client_x = 800;
    g_timed_host_pointer.absolute_sequence = 10u;
    g_timed_host_pointer.absolute_acquired_ms = 2'030u;
    frame = read();
    passed &= expect(!frame.second.sample.active &&
        frame.second.selection == SyntheticKpadPointerSelection::KnownOutside,
        "fresh known leave disables IR without requiring foreground focus");
    g_timed_host_pointer.absolute_valid = false;
    frame = read();
    passed &= expect(!frame.second.sample.active && frame.second.sample.sequence == 10u &&
        frame.second.selection == SyntheticKpadPointerSelection::InvalidAcquisition,
        "uncertain read cannot resurrect stored inside geometry after a proven leave");
    g_timed_host_pointer.absolute_valid = true;
    g_timed_host_pointer.inside_client = true;
    g_timed_host_pointer.client_x = 100;
    g_timed_host_pointer.absolute_sequence = 11u;
    g_timed_host_pointer.absolute_acquired_ms = 2'040u;
    frame = read();
    passed &= expect(frame.second.sample.active && frame.second.sample.x < -0.40f &&
        frame.second.selection == SyntheticKpadPointerSelection::FreshAbsolute,
        "fresh viewport reentry restores IR while unfocused");

    for (const auto origin : {SyntheticKpadPointerOrigin::Replay,
         SyntheticKpadPointerOrigin::Script, SyntheticKpadPointerOrigin::Controller,
         SyntheticKpadPointerOrigin::KeyboardPointer}) {
        auto protected_point = point_y;
        protected_point.origin = origin;
        // Even an incorrectly permissive caller hint cannot steal authority.
        protected_point.refresh_mouse_allowed = true;
        publish(protected_point, 0u, 0u);
        const auto before = g_synthetic_pointer_provider_reads;
        frame = read();
        passed &= expect(frame.second.selection == SyntheticKpadPointerSelection::Stored &&
            frame.second.sample.x == protected_point.x &&
            frame.second.sample.origin == origin && g_synthetic_pointer_provider_reads == before,
            "replay script controller and keyboard-pointer origins keep their existing point and never repoll");
    }
    publish(point_y, 0u, 0x0800u);
    auto neutral_y = point_y;
    publish(neutral_y, 0u, 0u);
    frame = read();
    passed &= expect(frame.first.retained_press == 0x0800u &&
        frame.second.selection == SyntheticKpadPointerSelection::ButtonEdge &&
        frame.second.sample.x == point_y.x,
        "short keyboard A keeps its old point and original retained button behavior");
    (void)read(); // corresponding release

    publish(point_x, 0x0800u, 0u);
    publish(point_y, 0u, 0x0800u);
    publish(point_y, 0u, 0u);
    frame = read();
    passed &= expect(frame.first.trigger == 0x0800u &&
        frame.second.selection == SyntheticKpadPointerSelection::AmbiguousPressIdentity &&
        frame.second.sample.x == point_y.x,
        "competing keyboard and mouse A origins retain old policy and explicitly reject an exact target claim");
    (void)read();

    publish(point_x, 0x0800u, 0u);
    auto point_b = point_x;
    point_b.x = 0.5f;
    point_b.sequence = 3u;
    point_b.acquired_ms = 2'003u;
    publish(point_b, 0x0400u, 0u);
    publish(point_y, 0u, 0u);
    frame = read();
    passed &= expect(frame.first.retained_press == 0x0C00u &&
        frame.second.selection == SyntheticKpadPointerSelection::AmbiguousPressIdentity &&
        frame.second.sample.x == point_y.x,
        "different coalesced A and B targets cannot be misrepresented as one exact pointer identity");
    (void)read();

    publish(point_y, 0u, 0u);
    const auto before_mismatch = g_synthetic_pointer_provider_reads;
    frame = read(true, 8u);
    passed &= expect(frame.second.selection == SyntheticKpadPointerSelection::ModeMismatch &&
        g_synthetic_pointer_provider_reads == before_mismatch,
        "old input mode generation cannot acquire new-source geometry");
    publish(point_x, 0x0800u, 0u);
    publish(point_y, 0u, 0u);
    frame = read(false);
    passed &= expect(frame.first.trigger == 0u && frame.first.retained_press == 0u &&
        frame.second.selection == SyntheticKpadPointerSelection::FreshAbsolute,
        "disabled edge-retention experiment does not introduce a retained mouse press");
    return passed;
}

bool ios_open_respects_guest_mapping_boundaries() {
    galaxy::host::GuestAddressSpace memory;
    auto* guest = memory.guest_memory();
    constexpr std::uint32_t request = 0x00100000u;
    constexpr std::uint32_t end = galaxy::host::GuestAddressSpace::kMem1Size;
    const std::string_view dvd = "/dev/di";
    const auto handle = ios_open_path(memory, guest, request,
        end - static_cast<std::uint32_t>(dvd.size() + 1u), dvd);
    bool passed = expect(handle != 0u && handle < 0x80000000u,
        "IOSOpen accepts a valid string whose terminator is the final mapped byte");
    ios_close_request(memory, guest, request, handle);
    acknowledge_ios_reply(guest);
    const auto reject = [&](std::uint32_t address) {
        memory.write_u32(request, 1u);
        memory.write_u32(request + 0x08u, 0u);
        memory.write_u32(request + 0x0Cu, address);
        memory.write_u32(request + 0x10u, 0u);
        memory.write_u32(request + 0x20u, 0u);
        submit_ios_request(guest, request);
        const bool rejected = memory.read_u32(request + 4u) ==
            static_cast<std::uint32_t>(-4) && ios_reply_available(guest) &&
            (ipc_control(guest) & 0x02u) != 0u;
        acknowledge_ios_reply(guest);
        return rejected;
    };
    *memory.pointer(end - 1u, 1u) = std::byte{'X'};
    passed &= expect(reject(end - 1u),
        "IOSOpen rejects an unterminated mapped tail without reading host memory past it");
    passed &= expect(reject(std::numeric_limits<std::uint32_t>::max()),
        "IOSOpen rejects an unmapped maximum guest address without wrapping");
    return passed;
}

bool checked_host_span_lookup_matches_regions(galaxy::host::GuestAddressSpace& memory) {
    bool passed = true;
    const auto* guest = memory.guest_memory();
    const auto& const_memory = memory;
    for (std::uint32_t region_index = 0u; region_index < guest->region_count; ++region_index) {
        const auto& region = guest->regions[region_index];
        for (auto offset : {0u, 1u, region.size - 1u, region.size, region.size + 1u}) {
            for (auto size : {0u, 1u, 2u, 4u, 12u, 0xffffffffu}) {
                const auto address = region.guest_base + offset;
                std::byte* expected = nullptr;
                // Preserve the complete list's priority, including empty
                // one-past spans, locked cache and nonaligned device mappings.
                for (std::uint32_t candidate_index = 0u; candidate_index < guest->region_count; ++candidate_index) {
                    const auto& candidate = guest->regions[candidate_index];
                    if (address >= candidate.guest_base &&
                        static_cast<std::uint64_t>(address) + size <=
                            static_cast<std::uint64_t>(candidate.guest_base) + candidate.size) {
                        expected = candidate.host_base + (address - candidate.guest_base); break;
                    }
                }
                passed &= expect(memory.pointer_or_null(address, size) == expected &&
                    const_memory.pointer_or_null(address, size) == expected,
                    "host span lookup preserves the complete region oracle at every boundary");
                if (expected != nullptr) {
                    passed &= expect(memory.pointer(address, size) == expected &&
                        const_memory.pointer(address, size) == expected,
                        "throwing and const host span lookups return the identical admitted storage");
                } else {
                    passed &= expect_runtime_error([&] { (void)memory.pointer(address, size); },
                        "host span lookup rejects unmapped and overflowing spans");
                }
            }
        }
    }
    return passed;
}

int main() {
    try {
    _putenv_s("GALAXY_AUDIO_DISABLE", "1");
    _putenv_s("GALAXY_DIRECT_WGPIPE_PE_EVENTS", "1");
    // Most NAND fixtures below assert the synchronous IOS mailbox contract.
    // Keep that mode explicit now that the product default is asynchronous;
    // native_async_nand_ownership_works scopes the flag to 1 and restores it.
    _putenv_s("GALAXY_ASYNC_NAND_WRITES", "0");
    _putenv_s("GALAXY_DSP_DETAILED_STATS", "1");
    const std::filesystem::path nand_test_root =
        std::filesystem::temp_directory_path() /
        L"galaxy_native_host_tests_nand";
    std::error_code nand_test_ec;
    std::filesystem::remove_all(nand_test_root, nand_test_ec);
    ScopedWideEnv set_nand_root(L"GALAXY_NAND_ROOT", nand_test_root);
    bool passed = true;
    passed &= flat_guest_owner_abi_boundary_works();
    passed &= native_host_failure_ownership_works(nand_test_root);
    std::cerr << "[native-host-test] native_nand_atomic_persistence_works\n";
    passed &= native_ios_anomaly_identity_and_bounds_work();
    passed &= owned_storage_worker_publication_works();
    passed &= native_async_nand_ownership_works(nand_test_root);
    passed &= native_nand_atomic_persistence_works(nand_test_root);
    std::cerr << "[native-host-test] native_nand_exact_current_surface_works\n";
    passed &= native_nand_exact_current_surface_works(nand_test_root);
    std::cerr << "[native-host-test] native_nand_pre_ads_legacy_cohort_startup_works\n";
    passed &= native_nand_pre_ads_legacy_cohort_startup_works(nand_test_root);
    std::cerr << "[native-host-test] native_nand_fresh_root_intent_recovery_works\n";
    passed &= native_nand_fresh_root_intent_recovery_works(nand_test_root);
    std::cerr << "[native-host-test] native_nand_invalid_sole_legacy_hard_fails\n";
    passed &= native_nand_invalid_sole_legacy_hard_fails(nand_test_root);
    std::cerr << "[native-host-test] native_nand_provisional_known_file_contract_works\n";
    passed &= native_nand_provisional_known_file_contract_works(nand_test_root);
    std::cerr << "[native-host-test] native_nand_modern_startup_recovery_edges_work\n";
    passed &= native_nand_modern_startup_recovery_edges_work(nand_test_root);
    std::cerr << "[native-host-test] native_nand_rename_failure_atomicity_works\n";
    passed &= native_nand_rename_failure_atomicity_works(nand_test_root);
    std::cerr << "[native-host-test] native_nand_rename_journal_cold_recovery_works\n";
    passed &= native_nand_rename_journal_cold_recovery_works(nand_test_root);
    std::cerr << "[native-host-test] native_nand_extended_length_rename_recovery_works\n";
    passed &= native_nand_extended_length_rename_recovery_works(nand_test_root);
    std::cerr << "[native-host-test] native_nand_fresh_rfl_safe_replace_works\n";
    passed &= native_nand_fresh_rfl_safe_replace_works(nand_test_root);

    const std::array<std::byte, 8> code{
        std::byte{0x12},
        std::byte{0x34},
        std::byte{0x56},
        std::byte{0x78},
        std::byte{0x9A},
        std::byte{0xBC},
        std::byte{0xDE},
        std::byte{0xF0},
    };
    galaxy::host::install_native_keyboard_capture_provider(nullptr);
    passed &= expect(
        !galaxy::host::native_keyboard_capture_active(),
        "native keyboard capture defaults to disabled");
    galaxy::host::install_native_keyboard_capture_provider(
        &capture_native_keyboard_for_test);
    passed &= expect(
        galaxy::host::native_keyboard_capture_active(),
        "native keyboard capture follows the installed window-owner callback");
    galaxy::host::install_native_keyboard_capture_provider(nullptr);
    passed &= expect(
        !galaxy::host::native_keyboard_capture_active(),
        "native keyboard capture detaches cleanly");
    passed &= ai_dma_interrupt_entry_masks_and_restores_ee();
    passed &= translated_exception_entry_contract_works();
    passed &= fpu_retry_provenance_lifecycle_works();
    passed &= interrupt_scheduler_handoff_linearity_policy_works();
    passed &= expect(
        galaxy::host::ai_dma_timing::kInitialInterruptDelayTimelineTicks ==
                200u &&
            galaxy::timing::kTimelineTicksPerSecond == 60'750'000ull &&
            !galaxy::host::ai_dma_timing::
                kInitialInterruptDelayHardwareValidated,
        "AI DMA provisional start delay records timeline-tick units and cannot be labeled hardware-validated");
    passed &= native_bt_hci_connection_lifecycle_retries_deterministically();
    passed &= native_bt_l2cap_rejection_recovery_is_bounded();
    passed &= native_bt_l2cap_signaling_is_correlated_and_timed();
    passed &= native_bt_l2cap_post_auth_open_is_nonblocking();
    {
        ScopedEnv strict_native_hid_cadence(
            "GALAXY_STRICT_NATIVE_HID_CADENCE", "1");
        const bool strict_native_bt_passed =
            native_bt_wiimote_protocol_path_works(true);
        if (!strict_native_bt_passed) {
            std::cerr << "FAILED: strict native Bluetooth RMGE01 IR command sequence aggregate\n";
        }
        passed &= strict_native_bt_passed;
    }
    const bool native_bt_passed = native_bt_wiimote_protocol_path_works();
    if (!native_bt_passed) {
        std::cerr << "FAILED: native Bluetooth Wiimote protocol path aggregate\n";
    }
    passed &= native_bt_passed;
    // The ordinary BT fixture performs the first calibrated projection and
    // initializes the process-static mouse gains. Preserve its first-use
    // environment before exercising the fresh synthetic KPAD read fixture.
    passed &= synthetic_kpad_pointer_read_owns_press_coordinates_and_fresh_geometry();

    {
        ScopedEnv enable_native_dsp("GALAXY_DSP_NATIVE", "1");
        {
            galaxy::host::GuestAddressSpace native_dsp_memory;
            galaxy::GuestMemoryV1* native_dsp_guest_memory =
                native_dsp_memory.guest_memory();
            passed &= expect_runtime_error(
                [&] {
                    send_dsp_mail(native_dsp_guest_memory, 0x80F3D001u);
                    send_dsp_mail(native_dsp_guest_memory, 1u);
                },
                "requested native DSP must hard-fail instead of faking a nonzero boot vector");
        }
        {
            galaxy::host::GuestAddressSpace native_dsp_memory;
            galaxy::GuestMemoryV1* native_dsp_guest_memory =
                native_dsp_memory.guest_memory();
            passed &= expect_runtime_error(
                [&] {
                    send_dsp_mail(native_dsp_guest_memory, 0x80F3D001u);
                    send_dsp_mail(native_dsp_guest_memory, 0u);
                },
                "requested native DSP must hard-fail instead of faking boot without an IRAM descriptor");
        }
    }
    ScopedEnv disable_native_bt_for_legacy_tests(
        "GALAXY_NATIVE_BT_WIIMOTE", "0");
    ScopedEnv allow_retired_dsp_mixer_for_legacy_tests(
        "GALAXY_ALLOW_RETIRED_DSP_MIXER_FOR_TESTS", "1");
    ScopedEnv disable_native_dsp_for_legacy_tests("GALAXY_DSP_NATIVE", "0");
    passed &= interrupt_register_foundation_works();

    const std::filesystem::path malformed_boot_image_path =
        std::filesystem::temp_directory_path() /
        "galaxy_native_host_malformed_boot_image.bin";
    std::error_code malformed_boot_remove_error;
    std::filesystem::remove(
        malformed_boot_image_path, malformed_boot_remove_error);
    const std::vector<std::byte> truncated_boot_image(32u);
    write_binary_file(malformed_boot_image_path, truncated_boot_image);
    passed &= expect_runtime_error(
        [&] {
            static_cast<void>(galaxy::host::read_boot_image(
                malformed_boot_image_path));
        },
        "truncated install-time boot image must hard-fail");
    constexpr std::size_t kCanonicalBootImageSize = 6'283'408u;
    std::vector<std::byte> wrong_version_boot_image(kCanonicalBootImageSize);
    constexpr std::array<char, 8> kBootMagic{
        'G', 'R', 'B', 'T', 'I', 'M', 'G', '\0'};
    std::memcpy(
        wrong_version_boot_image.data(),
        kBootMagic.data(),
        kBootMagic.size());
    write_le32(wrong_version_boot_image, 8u, 2u);
    write_binary_file(malformed_boot_image_path, wrong_version_boot_image);
    passed &= expect_runtime_error(
        [&] {
            static_cast<void>(galaxy::host::read_boot_image(
                malformed_boot_image_path));
        },
        "unknown install-time boot image version must hard-fail");
    std::filesystem::remove(
        malformed_boot_image_path, malformed_boot_remove_error);

    passed &= ios_open_respects_guest_mapping_boundaries();
    galaxy::host::GuestAddressSpace memory;
    passed &= checked_host_span_lookup_matches_regions(memory);
    passed &= native_dsp_mram_transaction_path_works(memory);
    {
        using DspAccess = galaxy::host::NativeDspBoundaryTestAccess;
        const bool synthetic_started =
            DspAccess::start_synthetic_native_worker(
                memory,
                &synthetic_native_dsp_reset_wait_entry,
                /*stage_seed=*/true);
        std::array<std::uint8_t, 16> pending_read{};
        std::array<std::uint8_t, 16> pending_write{};
        pending_write.fill(0x5au);
        std::atomic<bool> mram_submit_result{true};
        std::atomic<bool> aram_submit_result{true};
        std::thread pending_mram([&] {
            mram_submit_result.store(
                DspAccess::submit_mram_read(
                    memory,
                    pending_read.data(),
                    static_cast<std::uint32_t>(pending_read.size())),
                std::memory_order_release);
        });
        std::thread pending_aram([&] {
            aram_submit_result.store(
                DspAccess::submit_aram_commit(
                    memory,
                    pending_write.data(),
                    static_cast<std::uint32_t>(pending_write.size())),
                std::memory_order_release);
        });
        const bool both_pending = wait_for_native_dsp_test(
            [&] { return DspAccess::both_transactions_pending(memory); },
            std::chrono::seconds(1));
        DspAccess::shutdown_synthetic_native_worker(memory);
        pending_mram.join();
        pending_aram.join();
        const auto cancelled_mram = DspAccess::mram_snapshot(memory);
        const auto cancelled_aram = DspAccess::aram_commit_snapshot(memory);
        const auto cancelled_mirror = DspAccess::aram_boundary(memory).snapshot();
        passed &= expect(
            synthetic_started && both_pending &&
                !mram_submit_result.load(std::memory_order_acquire) &&
                !aram_submit_result.load(std::memory_order_acquire) &&
                DspAccess::synthetic_worker_stopped(memory) &&
                cancelled_mram.shutdown && cancelled_aram.shutdown &&
                cancelled_mram.shutdown_cancellations >= 1u &&
                cancelled_aram.shutdown_cancellations >= 1u &&
                cancelled_mirror.cancelled &&
                cancelled_mirror.inbound_depth == 0u &&
                !cancelled_mirror.worker_bound,
            "native DSP shutdown cancels pending MRAM, ARAM commit, and inbound mirror slots before joining and detaching the worker");

        const bool reset_worker_started =
            DspAccess::start_synthetic_native_worker(
                memory,
                &synthetic_native_dsp_reset_wait_entry,
                /*stage_seed=*/false);
        galaxy::guest_store_u16(
            memory.guest_memory(),
            0xCC00500Au,
            0x0001u,
            nullptr,
            0x80004000u);
        const std::uint16_t reset_control = galaxy::guest_load_u16(
            memory.guest_memory(),
            0xCC00500Au,
            nullptr,
            0x80004000u);
        passed &= expect(
            reset_worker_started &&
                DspAccess::synthetic_worker_stopped(memory) &&
                DspAccess::rom_reset_state(memory) &&
                (reset_control & 0x0001u) == 0u,
            "DSPCR reset synchronously cancels and joins the native worker, clears the strobe, and returns ownership to the ROM upload state");
    }
    {
        using DspAccess = galaxy::host::NativeDspBoundaryTestAccess;
        std::ostringstream reset_history_trace;
        galaxy::DspChannelSelectionDmaProbeSnapshot after_reset{};
        galaxy::DspChannelSelectionDmaProbeSnapshot terminal_snapshot{};
        bool first_worker_started = false;
        bool first_worker_published = false;
        bool reset_stopped_worker = false;
        bool second_worker_started = false;
        bool prepared_for_exit = false;
        {
            ScopedStreamRedirect capture_reset_history_trace(
                std::cerr, reset_history_trace.rdbuf());
            auto history_memory =
                std::make_unique<galaxy::host::GuestAddressSpace>();
            synthetic_native_dsp_selection_probe_ready.store(
                false, std::memory_order_release);
            first_worker_started = DspAccess::start_synthetic_native_worker(
                *history_memory,
                &synthetic_native_dsp_selection_probe_wait_entry,
                /*stage_seed=*/false,
                /*capture_audio_causality=*/false,
                /*capture_selection_dma_causality=*/true);
            first_worker_published =
                first_worker_started &&
                wait_for_native_dsp_test(
                    [] {
                        return synthetic_native_dsp_selection_probe_ready.load(
                            std::memory_order_acquire);
                    },
                    std::chrono::seconds(1));
            if (first_worker_published) {
                galaxy::guest_store_u16(
                    history_memory->guest_memory(),
                    0xCC00500Au,
                    0x0001u,
                    nullptr,
                    0x80004000u);
                reset_stopped_worker =
                    DspAccess::synthetic_worker_stopped(*history_memory);
                after_reset =
                    DspAccess::selection_dma_probe_snapshot(*history_memory);
            }
            if (reset_stopped_worker) {
                second_worker_started =
                    DspAccess::start_synthetic_native_worker(
                        *history_memory,
                        &synthetic_native_dsp_reset_wait_entry,
                        /*stage_seed=*/false,
                        /*capture_audio_causality=*/false,
                        /*capture_selection_dma_causality=*/true);
            }
            prepared_for_exit = history_memory->prepare_for_process_exit();
            terminal_snapshot =
                DspAccess::selection_dma_probe_snapshot(*history_memory);
        }
        const std::string reset_history_text = reset_history_trace.str();
        const bool report_preserves_lifetime_and_current =
            reset_history_text.find(
                "[dsp-selection-dma-causal] "
                "identity=validated-rmge01-selected-channel-render-v1 "
                "lifetime-state=active-publication-with-validated-selected-"
                "channel-dma current-state=no-complete-publication "
                "contract=1/0x3 sessions/resets=2/1 worker-sequence=7 "
                "publication-generation=1") != std::string::npos &&
            reset_history_text.find(
                " selected-channel-dma=1/1/0:no-branch0") !=
                std::string::npos;
        passed &= expect(
            first_worker_started && first_worker_published &&
                reset_stopped_worker && second_worker_started &&
                prepared_for_exit && after_reset.session_count == 1u &&
                after_reset.reset_count == 1u &&
                after_reset.worker_sequence == 7u &&
                after_reset.publication_generation == 1u &&
                after_reset.matched_active_publications == 1u &&
                after_reset.validated_selected_channel_record_dmas == 1u &&
                !after_reset.current_publication_valid &&
                terminal_snapshot.session_count == 2u &&
                terminal_snapshot.reset_count == 1u &&
                terminal_snapshot.publication_generation == 1u &&
                terminal_snapshot.matched_active_publications == 1u &&
                !terminal_snapshot.current_publication_valid &&
                galaxy::dsp_channel_selection_dma_probe_lifetime_state(
                    terminal_snapshot) ==
                    galaxy::DspChannelSelectionDmaProbeLifetimeState::
                        ActivePublicationWithValidatedSelectedChannelDma &&
                report_preserves_lifetime_and_current,
            "DSPCR reset preserves exact selected-channel lifetime evidence across a second worker session while clearing current causal state");
    }
    {
        using DspAccess = galaxy::host::NativeDspBoundaryTestAccess;
        ProcessExitHidBackendState hid_state{};
        std::ostringstream process_exit_trace;
        bool dsp_started = false;
        bool input_started = false;
        bool first_prepare = false;
        bool second_prepare = true;
        bool dsp_stopped = false;
        bool input_stopped = false;
        bool mram_pending_before_prepare = false;
        std::atomic<bool> pending_mram_result{true};
        galaxy::DspMramTransactionBoundary::Snapshot mram_before_prepare{};
        galaxy::DspMramTransactionBoundary::Snapshot mram_after_prepare{};
        {
            ScopedStreamRedirect capture_process_exit_trace(
                std::cerr, process_exit_trace.rdbuf());
            {
                auto exit_memory =
                    std::make_unique<galaxy::host::GuestAddressSpace>();
                dsp_started = DspAccess::start_synthetic_native_worker(
                    *exit_memory,
                    &synthetic_native_dsp_reset_wait_entry,
                    /*stage_seed=*/false,
                    /*capture_audio_causality=*/true,
                    /*capture_selection_dma_causality=*/true);
                input_started = DspAccess::start_synthetic_input_worker(
                    *exit_memory,
                    std::make_unique<ProcessExitHidBackend>(hid_state));
                std::array<std::uint8_t, 16> pending_read{};
                std::thread pending_mram;
                if (dsp_started) {
                    pending_mram = std::thread([&] {
                        pending_mram_result.store(
                            DspAccess::submit_mram_read(
                                *exit_memory,
                                pending_read.data(),
                                static_cast<std::uint32_t>(
                                    pending_read.size())),
                            std::memory_order_release);
                    });
                    mram_pending_before_prepare = wait_for_native_dsp_test(
                        [&] {
                            return DspAccess::mram_snapshot(*exit_memory)
                                       .in_flight == 1u;
                        },
                        std::chrono::seconds(1));
                    mram_before_prepare =
                        DspAccess::mram_snapshot(*exit_memory);
                }
                first_prepare = exit_memory->prepare_for_process_exit();
                if (pending_mram.joinable()) {
                    pending_mram.join();
                }
                mram_after_prepare = DspAccess::mram_snapshot(*exit_memory);
                second_prepare = exit_memory->prepare_for_process_exit();
                dsp_stopped =
                    DspAccess::synthetic_worker_stopped(*exit_memory);
                input_stopped =
                    DspAccess::synthetic_input_worker_stopped(*exit_memory);
            }
        }
        const std::string process_exit_text = process_exit_trace.str();
        const std::size_t first_report =
            process_exit_text.find("[audio-causal]");
        const bool exactly_one_report =
            first_report != std::string::npos &&
            process_exit_text.find(
                "[audio-causal]", first_report + 1u) == std::string::npos;
        const std::size_t first_selection_report =
            process_exit_text.find("[dsp-selection-dma-causal]");
        const bool exactly_one_selection_report =
            first_selection_report != std::string::npos &&
            process_exit_text.find(
                "[dsp-selection-dma-causal]",
                first_selection_report + 1u) == std::string::npos;
        const bool selection_report_has_ordered_empty_state =
            process_exit_text.find(
                "[dsp-selection-dma-causal] "
                "identity=validated-rmge01-selected-channel-render-v1 "
                "lifetime-state=no-complete-publication-ever "
                "current-state=no-complete-publication contract=1/0x3 "
                "sessions/resets=1/0 worker-sequence=0 "
                "publication-generation=0") !=
                std::string::npos &&
            process_exit_text.find(" selected-channel-dma=0/0/0:no-branch0") !=
                std::string::npos &&
            process_exit_text.find(" current-first-match-sequence=0 ") !=
                std::string::npos;
        passed &= expect(
            dsp_started && input_started && first_prepare &&
                !second_prepare && dsp_stopped && input_stopped &&
                mram_pending_before_prepare &&
                mram_before_prepare.in_flight == 1u &&
                mram_before_prepare.timeout_failures == 0u &&
                !pending_mram_result.load(std::memory_order_acquire) &&
                mram_after_prepare.shutdown &&
                mram_after_prepare.in_flight == 0u &&
                mram_after_prepare.timeout_failures == 0u &&
                mram_after_prepare.shutdown_cancellations ==
                    mram_before_prepare.shutdown_cancellations + 1u &&
                hid_state.open_called.load(std::memory_order_acquire) &&
                hid_state.stop_requested.load(std::memory_order_acquire) &&
                hid_state.close_called.load(std::memory_order_acquire) &&
                exactly_one_report && exactly_one_selection_report &&
                selection_report_has_ordered_empty_state,
            "explicit process-exit preparation cancels a pending DSP MRAM rendezvous without a timeout, joins DSP before HID teardown, and preserves exactly one terminal broad-audio and ordered selection/DMA report even when the destructor follows");
    }
    {
        ScopedEnv request_selection_probe_without_boot(
            "GALAXY_TRACE_DSP_SELECTION_DMA_CAUSAL", "1");
        std::ostringstream no_session_trace;
        bool prepared_for_exit = false;
        {
            ScopedStreamRedirect capture_no_session_trace(
                std::cerr, no_session_trace.rdbuf());
            auto no_session_memory =
                std::make_unique<galaxy::host::GuestAddressSpace>();
            prepared_for_exit =
                no_session_memory->prepare_for_process_exit();
        }
        const std::string no_session_text = no_session_trace.str();
        const std::size_t first_no_session_report =
            no_session_text.find("[dsp-selection-dma-causal]");
        const bool exactly_one_no_session_report =
            first_no_session_report != std::string::npos &&
            no_session_text.find(
                "[dsp-selection-dma-causal]",
                first_no_session_report + 1u) == std::string::npos;
        const bool explicit_no_session_identity =
            no_session_text.find(
                "[dsp-selection-dma-causal] "
                "identity=not-validated-no-native-session "
                "lifetime-state=no-complete-publication-ever "
                "current-state=no-complete-publication contract=0/0x0 "
                "sessions/resets=0/0 worker-sequence=0 "
                "publication-generation=0") != std::string::npos;
        passed &= expect(
            prepared_for_exit && exactly_one_no_session_report &&
                explicit_no_session_identity,
            "requested ordered selection/DMA diagnostic reports an explicit unvalidated zero-session identity when native DSP boot never starts");
    }
    passed &= expect(
        memory.preinitialize_native_audio(),
        "explicit audio-disable mode skips pre-guest endpoint initialization");
    passed &= expect(
        !memory.native_audio_sink_started(),
        "audio-disable pre-initialization skip does not construct a sink");
    passed &= expect_runtime_error(
        [&] {
            memory.open_game_pak(
                "__missing_native_host_test_game_pak_9B0E7A43.pak");
        },
        "missing game.pak must hard-fail instead of allowing fake DVD data");
    std::memset(memory.pointer(0x80004004, 8), 0xFF, 8);
    galaxy::host::BootImage synthetic_boot_image{};
    synthetic_boot_image.bss_address = 0x80004004u;
    synthetic_boot_image.bss_size = 8u;
    synthetic_boot_image.bytes.assign(code.begin(), code.end());
    synthetic_boot_image.sections.push_back({
        0u,
        0x80004000u,
        static_cast<std::uint32_t>(code.size()),
        galaxy::host::BootImageSectionKind::text,
        0u});
    galaxy::host::load_boot_image(memory, synthetic_boot_image);
    passed &= expect(
        memory.read_u32(0x80004000) == 0x12345678,
        "boot-image section copied to guest memory");
    passed &= expect(
        memory.read_u32(0x80004004) == 0x9ABCDEF0,
        "initialized data wins over overlapping BSS clearing");
    passed &= expect(
        memory.read_u32(0xC0004000) == 0x12345678,
        "MEM1 cached and uncached aliases share storage");

    memory.write_u32(0x90000000, 0xCAFEBABE);
    passed &= expect(
        memory.read_u32(0x10000000) == 0xCAFEBABE,
        "MEM2 physical and cached aliases share storage");
    std::uint64_t fake_ticks = 1234;
    memory.set_tick_source(&fake_tick_source, &fake_ticks);
    AiDeadlineProbe ai_deadline_probe{};
    ai_deadline_probe.address_space = &memory;
    memory.set_ai_dma_deadline_callback(
        &capture_ai_deadline, &ai_deadline_probe);

    galaxy::host::initialize_wii_memory_values(memory);
    passed &= expect(
        memory.read_u32(0x80003100) == galaxy::host::GuestAddressSpace::kMem1Size,
        "IOS reports the retail MEM1 size");
    passed &= expect(
        memory.read_u32(0x8000311C) == galaxy::host::GuestAddressSpace::kMem2Size,
        "IOS reports the retail MEM2 size");
    passed &= expect(
        memory.read_u32(0x80003120) == 0x93600000,
        "IOS33 reports its retail MEM2 end, not the pre-IOS28 legacy end");
    passed &= expect(
        memory.read_u32(0x80003124) == 0x90000800,
        "MEM2 arena starts after the low-memory vectors");
    passed &= expect(
        memory.read_u32(0x80003128) == 0x935E0000,
        "IOS33 MEM2 arena ends before IPC and retains the missing two MiB");
    passed &= expect(
        memory.read_u32(0x80003130) == 0x935E0000 &&
            memory.read_u32(0x80003134) == 0x93600000,
        "IOS33 initializes the retail 128 KiB IPC range");
    passed &= expect(
        memory.read_u32(0x80003118) == 0x04000000u &&
            memory.read_u32(0x80003120) - memory.read_u32(0x80003128) == 0x20000u &&
            memory.read_u32(0x80003128) - 0x933E0000u == 0x200000u,
        "IOS33 corrects arena ownership without increasing physical MEM2 or IPC size");

    {
        constexpr std::uint32_t kBoundaryArBackingBaseGlobal = 0x806A2C54u;
        constexpr std::uint32_t kBoundaryArSizeGlobal = 0x806A2C5Cu;
        constexpr std::uint32_t kBoundaryArBackingBase = 0x90000800u;
        constexpr std::uint32_t kBoundaryArSize = 0x00E00000u;
        constexpr std::array<std::uint32_t, 5> kBoundaryOffsets{
            0u,
            1u,
            0x00003fffu,
            0x00004000u,
            kBoundaryArSize - 1u,
        };

        const std::uint32_t saved_ar_backing =
            memory.read_u32(kBoundaryArBackingBaseGlobal);
        const std::uint32_t saved_ar_size =
            memory.read_u32(kBoundaryArSizeGlobal);
        // The native bus has the RMGE01 hardware-sized 14 MiB address space;
        // this deliberately-small allocator report proves routing does not
        // reuse the retired mixer's max(size, dummy-size) heuristic.
        memory.write_u32(kBoundaryArSizeGlobal, 0x20u);
        passed &= expect(
            galaxy::host::NativeDspBoundaryTestAccess::reset_aram_boundary(
                memory),
            "native DSP ARAM integration test resets the CPU-owned mirror before staging");
        memory.write_u32(kBoundaryArBackingBaseGlobal, 0x80000000u);
        passed &= expect(
            !galaxy::host::NativeDspBoundaryTestAccess::pin_aram_backing(
                memory, 0x80000000u),
            "native DSP refuses to pin an ARAM backing pointer into MEM1");
        memory.write_u32(
            kBoundaryArBackingBaseGlobal, kBoundaryArBackingBase);
        galaxy::host::NativeDspBoundaryTestAccess::observe_command_mail(
            memory, 2u, 1u);
        galaxy::host::NativeDspBoundaryTestAccess::observe_command_mail(
            memory, 0x8e000000u, 2u);
        galaxy::host::NativeDspBoundaryTestAccess::observe_command_mail(
            memory, kBoundaryArBackingBase, 3u);
        const std::array<std::uint32_t, 3> outbound_offsets{6u, 7u, 0x40u};
        std::array<std::byte, outbound_offsets.size()> saved_outbound_bytes{};
        for (std::size_t index = 0u; index < outbound_offsets.size(); ++index) {
            saved_outbound_bytes[index] = *memory.pointer(
                kBoundaryArBackingBase + outbound_offsets[index], 1u);
        }
        passed &= expect(
            galaxy::host::NativeDspBoundaryTestAccess::pin_aram_backing(
                memory, kBoundaryArBackingBase),
            "native DSP DsetVARAM pins one complete 14 MiB MEM2 ARAM backing span before worker publication");
        passed &= expect(
            !galaxy::host::NativeDspBoundaryTestAccess::pin_aram_backing(
                memory, 0x91000000u),
            "native DSP rejects an ARAM backing change during one service lifetime");

        std::array<std::byte, kBoundaryOffsets.size()> saved_mram_bytes{};
        std::array<std::byte, kBoundaryOffsets.size()> saved_aram_bytes{};
        for (std::size_t index = 0; index < kBoundaryOffsets.size(); ++index) {
            const std::uint32_t offset = kBoundaryOffsets[index];
            std::byte* const mram = memory.pointer(offset, 1u);
            std::byte* const aram =
                memory.pointer(kBoundaryArBackingBase + offset, 1u);
            saved_mram_bytes[index] = *mram;
            saved_aram_bytes[index] = *aram;
            *mram = static_cast<std::byte>(0x10u + index);
            *aram = static_cast<std::byte>(0x80u + index);
        }
        // Re-stage after the sentinels are initialized so the full seed is an
        // exact snapshot of the pinned CPU backing.
        galaxy::host::NativeDspBoundaryTestAccess::cancel_aram_boundary(memory);
        passed &= expect(
            galaxy::host::NativeDspBoundaryTestAccess::reset_aram_boundary(
                memory),
            "native DSP ARAM mirror can be reset after a quiescent cancel");
        galaxy::host::NativeDspBoundaryTestAccess::observe_command_mail(
            memory, 2u, 1u);
        galaxy::host::NativeDspBoundaryTestAccess::observe_command_mail(
            memory, 0x8e000000u, 2u);
        galaxy::host::NativeDspBoundaryTestAccess::observe_command_mail(
            memory, kBoundaryArBackingBase, 3u);

        auto bypass_context = std::make_unique<galaxy::DspContext>();
        bypass_context->pc = 0x0788u;
        bypass_context->hardware =
            galaxy::host::NativeDspBoundaryTestAccess::services();
        bypass_context->hardware_user = &memory;
        galaxy::dsp_mailbox_write_high(
            galaxy::dsp_cpu_mailbox(*bypass_context), 0x1111u);
        (void)galaxy::dsp_mailbox_write_low(
            galaxy::dsp_cpu_mailbox(*bypass_context), 0x2222u);
        passed &= expect_dsp_hard_trap(
            [&] {
                static_cast<void>(galaxy::dsp_ifx_read(
                    *bypass_context, galaxy::kDspIfxCmbl));
            },
            "active native DSP ARAM rejects busy CMBL consume without an exact published generation");
        passed &= expect(
            (galaxy::dsp_mailbox_read_high(
                 galaxy::dsp_cpu_mailbox(*bypass_context)) &
             0x8000u) != 0u,
            "rejected direct CMBL bypass leaves the hardware mail occupied");

        galaxy::DspContext boundary_context{};
        boundary_context.pc = 0x0789u;
        boundary_context.hardware =
            galaxy::host::NativeDspBoundaryTestAccess::services();
        boundary_context.hardware_user = &memory;
        std::atomic<std::uint64_t> boundary_generation{3u};
        boundary_context.host_cpu_mail_generation = &boundary_generation;
        galaxy::dsp_mailbox_write_high(
            galaxy::dsp_cpu_mailbox(boundary_context), 0x1234u);
        (void)galaxy::dsp_mailbox_write_low(
            galaxy::dsp_cpu_mailbox(boundary_context), 0x5678u);

        std::atomic<bool> worker_seed_checked{false};
        std::atomic<bool> worker_publication_entered{false};
        std::atomic<bool> worker_publication_done{false};
        std::atomic<bool> worker_integration_ok{true};
        std::thread aram_worker([&] {
            auto& boundary =
                galaxy::host::NativeDspBoundaryTestAccess::aram_boundary(
                    memory);
            if (!boundary.worker_bind()) {
                worker_integration_ok.store(false, std::memory_order_release);
                return;
            }
            try {
                const std::uint16_t low = galaxy::dsp_ifx_read(
                    boundary_context, galaxy::kDspIfxCmbl);
                bool seed_ok = low == 0x5678u;
                for (std::size_t index = 0u;
                     index < kBoundaryOffsets.size();
                     ++index) {
                    seed_ok = seed_ok &&
                        galaxy::dsp_accelerator_read_byte(
                            boundary_context, kBoundaryOffsets[index]) ==
                            static_cast<std::uint8_t>(0x80u + index);
                }
                worker_seed_checked.store(seed_ok, std::memory_order_release);
                galaxy::dsp_accelerator_set_current_address(
                    boundary_context, 0x80000003u);
                galaxy::dsp_accelerator_write_raw(
                    boundary_context, 0xbeefu);
                galaxy::dsp_accelerator_write_byte(
                    boundary_context, 0x40u, 0x7cu);
                galaxy::dsp_ifx_write(
                    boundary_context, galaxy::kDspIfxDmbh, 0xdcd1u);
                worker_publication_entered.store(
                    true, std::memory_order_release);
                galaxy::dsp_ifx_write(
                    boundary_context, galaxy::kDspIfxDmbl, 0x0004u);
                worker_publication_done.store(
                    true, std::memory_order_release);
            } catch (...) {
                worker_integration_ok.store(false, std::memory_order_release);
            }
            if (!boundary.worker_detach()) {
                worker_integration_ok.store(false, std::memory_order_release);
            }
        });

        while (!worker_publication_entered.load(std::memory_order_acquire)) {
            std::this_thread::yield();
        }
        passed &= expect(
            worker_seed_checked.load(std::memory_order_acquire) &&
                (galaxy::dsp_mailbox_read_high(
                     galaxy::dsp_dsp_mailbox(boundary_context)) &
                 0x8000u) == 0u &&
                *memory.pointer(kBoundaryArBackingBase + 6u, 1u) !=
                    std::byte{0xbe},
            "native DSP seed is applied before exact CMBL consume and DMBL remains invisible before outbound CPU commit");
        while (!worker_publication_done.load(std::memory_order_acquire)) {
            galaxy::host::NativeDspBoundaryTestAccess::service_transactions(
                memory);
            std::this_thread::yield();
        }
        aram_worker.join();
        passed &= expect(
            worker_integration_ok.load(std::memory_order_acquire) &&
                (galaxy::dsp_mailbox_read_high(
                     galaxy::dsp_dsp_mailbox(boundary_context)) &
                 0x8000u) != 0u &&
                *memory.pointer(kBoundaryArBackingBase + 6u, 1u) ==
                    std::byte{0xbe} &&
                *memory.pointer(kBoundaryArBackingBase + 7u, 1u) ==
                    std::byte{0xef} &&
                *memory.pointer(kBoundaryArBackingBase + 0x40u, 1u) ==
                    std::byte{0x7c},
            "native DSP DMBL publishes only after all mirror writes commit through CPU-owned whole-span transactions");

        // Both physical and cached CPU aliases must feed one page in the
        // CPU-only tracker, while renderer/shared dirtiness remains
        // independently drainable. The native task-control mail is a real
        // generation even though it is not command-parser payload.
        galaxy::GuestMemoryV1* const boundary_memory = memory.guest_memory();
        const auto saved_shared_words = boundary_memory->dirty_page_words;
        const auto saved_shared_base = boundary_memory->dirty_tracked_base;
        const auto saved_shared_size = boundary_memory->dirty_tracked_size;
        const auto saved_shared_shift = boundary_memory->dirty_page_shift;
        const auto saved_shared_count =
            boundary_memory->dirty_page_word_count;
        std::array<std::atomic_uint64_t, 1> shared_dirty{};
        boundary_memory->dirty_page_words = shared_dirty.data();
        boundary_memory->dirty_tracked_base = 0x10000800u;
        boundary_memory->dirty_tracked_size = 128u;
        boundary_memory->dirty_page_shift = 7u;
        boundary_memory->dirty_page_word_count = 1u;

        constexpr std::uint32_t kAliasDirtyOffset = 0x40u;
        const std::byte saved_alias_dirty = *memory.pointer(
            kBoundaryArBackingBase + kAliasDirtyOffset, 1u);
        memory.write_u32(0x10000840u, 0xA1A2A3A4u);
        memory.write_u32(0xD0000840u, 0xB1B2B3B4u);
        const bool aliases_marked_one_page =
            (boundary_memory->cpu_dirty_page_words[0] & 1u) != 0u &&
            (shared_dirty[0].load(std::memory_order_acquire) & 1u) != 0u;
        galaxy::host::NativeDspBoundaryTestAccess::observe_command_mail(
            memory, 0xcdd10003u, 4u);
        const bool independent_drains =
            boundary_memory->cpu_dirty_page_words[0] == 0u &&
            (shared_dirty[0].load(std::memory_order_acquire) & 1u) != 0u;

        auto dirq_context = std::make_unique<galaxy::DspContext>();
        dirq_context->pc = 0x0771u;
        dirq_context->hardware =
            galaxy::host::NativeDspBoundaryTestAccess::services();
        dirq_context->hardware_user = &memory;
        std::atomic<std::uint64_t> dirq_generation{4u};
        dirq_context->host_cpu_mail_generation = &dirq_generation;
        galaxy::dsp_mailbox_write_high(
            galaxy::dsp_cpu_mailbox(*dirq_context), 0xcdd1u);
        (void)galaxy::dsp_mailbox_write_low(
            galaxy::dsp_cpu_mailbox(*dirq_context), 0x0003u);
        galaxy::host::NativeDspBoundaryTestAccess::clear_interrupt(memory);
        std::atomic<bool> dirq_publication_entered{};
        std::atomic<bool> dirq_publication_done{};
        std::atomic<bool> dirq_worker_ok{true};
        std::thread dirq_worker([&] {
            auto& boundary =
                galaxy::host::NativeDspBoundaryTestAccess::aram_boundary(
                    memory);
            if (!boundary.worker_bind()) {
                dirq_worker_ok.store(false, std::memory_order_release);
                return;
            }
            try {
                const bool consumed = galaxy::dsp_ifx_read(
                    *dirq_context, galaxy::kDspIfxCmbl) == 0x0003u;
                std::uint8_t mirrored = 0u;
                const bool mirror_updated = consumed &&
                    boundary.worker_read_span(
                        kAliasDirtyOffset, &mirrored, 1u) &&
                    mirrored == 0xB1u;
                const std::uint8_t worker_value = 0x55u;
                const bool wrote = mirror_updated &&
                    boundary.worker_write_span(
                        kAliasDirtyOffset, &worker_value, 1u);
                dirq_publication_entered.store(
                    true, std::memory_order_release);
                if (wrote) {
                    galaxy::dsp_ifx_write(
                        *dirq_context, galaxy::kDspIfxDirq, 1u);
                }
                dirq_publication_done.store(
                    true, std::memory_order_release);
                if (!wrote) {
                    dirq_worker_ok.store(false, std::memory_order_release);
                }
            } catch (...) {
                dirq_worker_ok.store(false, std::memory_order_release);
            }
            if (!boundary.worker_detach()) {
                dirq_worker_ok.store(false, std::memory_order_release);
            }
        });
        while (!dirq_publication_entered.load(std::memory_order_acquire)) {
            std::this_thread::yield();
        }
        passed &= expect(
            aliases_marked_one_page && independent_drains &&
                !galaxy::host::NativeDspBoundaryTestAccess::interrupt_pending(
                    memory) &&
                *memory.pointer(
                    kBoundaryArBackingBase + kAliasDirtyOffset, 1u) ==
                    std::byte{0xB1},
            "native DSP task-control generation drains alias-coalesced CPU dirtiness without draining renderer state, and DIRQ remains invisible before commit");
        while (!dirq_publication_done.load(std::memory_order_acquire)) {
            galaxy::host::NativeDspBoundaryTestAccess::service_transactions(
                memory);
            std::this_thread::yield();
        }
        dirq_worker.join();
        passed &= expect(
            dirq_worker_ok.load(std::memory_order_acquire) &&
                galaxy::host::NativeDspBoundaryTestAccess::interrupt_pending(
                    memory) &&
                *memory.pointer(
                    kBoundaryArBackingBase + kAliasDirtyOffset, 1u) ==
                    std::byte{0x55} &&
                boundary_memory->cpu_dirty_page_words[0] == 0u &&
                (shared_dirty[0].load(std::memory_order_acquire) & 1u) != 0u,
            "native DSP DIRQ publishes after renderer-visible outbound commit without echoing into the CPU-to-mirror tracker");
        galaxy::host::NativeDspBoundaryTestAccess::clear_interrupt(memory);

        // Every post-activation mail, including a count word, occupies the
        // generation slot. Consume that empty packet before proving replayed
        // DsetVARAM is rejected at its first word.
        galaxy::host::NativeDspBoundaryTestAccess::observe_command_mail(
            memory, 2u, 5u);
        auto replay_context = std::make_unique<galaxy::DspContext>();
        replay_context->pc = 0x0772u;
        replay_context->hardware =
            galaxy::host::NativeDspBoundaryTestAccess::services();
        replay_context->hardware_user = &memory;
        std::atomic<std::uint64_t> replay_generation{5u};
        replay_context->host_cpu_mail_generation = &replay_generation;
        galaxy::dsp_mailbox_write_high(
            galaxy::dsp_cpu_mailbox(*replay_context), 0u);
        (void)galaxy::dsp_mailbox_write_low(
            galaxy::dsp_cpu_mailbox(*replay_context), 2u);
        std::atomic<bool> replay_count_consumed{};
        std::thread replay_worker([&] {
            auto& boundary =
                galaxy::host::NativeDspBoundaryTestAccess::aram_boundary(
                    memory);
            const bool bound = boundary.worker_bind();
            bool consumed = false;
            try {
                consumed = bound && galaxy::dsp_ifx_read(
                    *replay_context, galaxy::kDspIfxCmbl) == 2u;
            } catch (...) {
                consumed = false;
            }
            const bool detached = bound && boundary.worker_detach();
            replay_count_consumed.store(
                consumed && detached, std::memory_order_release);
        });
        replay_worker.join();
        passed &= expect(
            replay_count_consumed.load(std::memory_order_acquire),
            "native DSP post-activation command count stages and consumes an explicit empty ARAM generation");
        passed &= expect_runtime_error(
            [&] {
                galaxy::host::NativeDspBoundaryTestAccess::observe_command_mail(
                    memory, 0x8e000000u, 6u);
            },
            "native DSP rejects replayed DsetVARAM at its first command word");

        galaxy::host::NativeDspBoundaryTestAccess::cancel_aram_boundary(
            memory);
        passed &= expect(
            galaxy::host::NativeDspBoundaryTestAccess::reset_aram_boundary(
                memory),
            "native DSP parser reset is quiescent after replay rejection");
        galaxy::host::NativeDspBoundaryTestAccess::observe_command_mail(
            memory, 3u, 1u);
        passed &= expect_runtime_error(
            [&] {
                galaxy::host::NativeDspBoundaryTestAccess::observe_command_mail(
                    memory, 0x8e000000u, 2u);
            },
            "native DSP rejects DsetVARAM whose count is not exactly two immediately at the first word");

        galaxy::host::NativeDspBoundaryTestAccess::cancel_aram_boundary(
            memory);
        passed &= expect(
            galaxy::host::NativeDspBoundaryTestAccess::reset_aram_boundary(
                memory),
            "native DSP parser reset is quiescent after malformed Dset count");
        galaxy::host::NativeDspBoundaryTestAccess::observe_command_mail(
            memory, 2u, 1u);
        passed &= expect_runtime_error(
            [&] {
                galaxy::host::NativeDspBoundaryTestAccess::observe_command_mail(
                    memory, 0xcdd10003u, 2u);
            },
            "native DSP rejects task-control mail interleaved inside a command group");

        galaxy::host::NativeDspBoundaryTestAccess::cancel_aram_boundary(
            memory);
        passed &= expect(
            galaxy::host::NativeDspBoundaryTestAccess::reset_aram_boundary(
                memory),
            "native DSP parser reset is quiescent after interleaved task control");
        passed &= expect_runtime_error(
            [&] {
                galaxy::host::NativeDspBoundaryTestAccess::observe_command_mail(
                    memory, 65u, 1u);
            },
            "native DSP rejects command counts above the audited 64-word maximum");

        galaxy::host::NativeDspBoundaryTestAccess::cancel_aram_boundary(
            memory);
        passed &= expect(
            galaxy::host::NativeDspBoundaryTestAccess::reset_aram_boundary(
                memory),
            "native DSP parser reset is quiescent after oversized count");
        galaxy::host::NativeDspBoundaryTestAccess::observe_command_mail(
            memory, 0u, 1u);
        const bool zero_count_becomes_one =
            galaxy::host::NativeDspBoundaryTestAccess::command_words_remaining(
                memory) == 1u;
        galaxy::host::NativeDspBoundaryTestAccess::observe_command_mail(
            memory, 0x81000000u, 2u);
        passed &= expect(
            zero_count_becomes_one &&
                galaxy::host::NativeDspBoundaryTestAccess::command_words_remaining(
                    memory) == 0u,
            "native DSP parser maps count zero to one word without fabricating an empty group");

        galaxy::host::NativeDspBoundaryTestAccess::cancel_aram_boundary(
            memory);
        passed &= expect(
            galaxy::host::NativeDspBoundaryTestAccess::reset_aram_boundary(
                memory),
            "native DSP parser reset is quiescent before invalid Dset base");
        galaxy::host::NativeDspBoundaryTestAccess::observe_command_mail(
            memory, 2u, 1u);
        galaxy::host::NativeDspBoundaryTestAccess::observe_command_mail(
            memory, 0x8e000000u, 2u);
        passed &= expect_runtime_error(
            [&] {
                galaxy::host::NativeDspBoundaryTestAccess::observe_command_mail(
                    memory, 0x93400000u, 3u);
            },
            "native DSP rejects a changed or non-contiguous 14 MiB DsetVARAM base before publication");

        constexpr std::uint32_t kOverlapPhysical = 0x10001000u;
        constexpr std::uint32_t kNonoverlapPhysical = 0x10002000u;
        const std::array<std::uint8_t, 4> overlap_value{
            0x11u, 0x22u, 0x33u, 0x44u};
        std::array<std::byte, overlap_value.size()> saved_nonoverlap{};
        std::memcpy(
            saved_nonoverlap.data(),
            memory.pointer(
                kNonoverlapPhysical,
                static_cast<std::uint32_t>(overlap_value.size())),
            overlap_value.size());
        galaxy::host::NativeDspBoundaryTestAccess::set_aram_dma_overlap_probe(
            memory, true, 0x1000u);
        const bool overlapping_alias_rejected =
            !galaxy::host::NativeDspBoundaryTestAccess::service_aram_commit(
                memory,
                kOverlapPhysical | 0x80000000u,
                overlap_value.data(),
                static_cast<std::uint32_t>(overlap_value.size()));
        const bool nonoverlap_committed =
            galaxy::host::NativeDspBoundaryTestAccess::service_aram_commit(
                memory,
                kNonoverlapPhysical,
                overlap_value.data(),
                static_cast<std::uint32_t>(overlap_value.size()));
        galaxy::host::NativeDspBoundaryTestAccess::set_aram_dma_overlap_probe(
            memory, false, 0u);
        passed &= expect(
            overlapping_alias_rejected && nonoverlap_committed &&
                std::memcmp(
                    memory.pointer(
                        kNonoverlapPhysical,
                        static_cast<std::uint32_t>(overlap_value.size())),
                    overlap_value.data(),
                    overlap_value.size()) == 0,
            "native DSP ARAM commit hard-rejects a physical-alias overlap with active Broadway ARAM DMA while committing a non-overlap deterministically");
        std::memcpy(
            memory.pointer(
                kNonoverlapPhysical,
                static_cast<std::uint32_t>(overlap_value.size())),
            saved_nonoverlap.data(),
            saved_nonoverlap.size());

        boundary_memory->dirty_page_words = saved_shared_words;
        boundary_memory->dirty_tracked_base = saved_shared_base;
        boundary_memory->dirty_tracked_size = saved_shared_size;
        boundary_memory->dirty_page_shift = saved_shared_shift;
        boundary_memory->dirty_page_word_count = saved_shared_count;
        *memory.pointer(
            kBoundaryArBackingBase + kAliasDirtyOffset, 1u) =
            saved_alias_dirty;

        passed &= expect_dsp_hard_trap(
            [&] {
                static_cast<void>(galaxy::dsp_external_read_byte(
                    boundary_context, 0x81800000u));
            },
            "native DSP MRAM invalid read becomes DspHardTrap");
        passed &= expect_dsp_hard_trap(
            [&] {
                galaxy::dsp_external_write_byte(
                    boundary_context, 0x81800000u, 0x5au);
            },
            "native DSP MRAM invalid write becomes DspHardTrap");
        passed &= expect_dsp_hard_trap(
            [&] {
                static_cast<void>(galaxy::dsp_accelerator_read_byte(
                    boundary_context, kBoundaryArSize));
            },
            "native DSP ARAM first-invalid read becomes DspHardTrap");
        passed &= expect_dsp_hard_trap(
            [&] {
                galaxy::dsp_accelerator_write_byte(
                    boundary_context, kBoundaryArSize, 0x5au);
            },
            "native DSP ARAM first-invalid write becomes DspHardTrap");
        bool worker_preflight_is_arithmetic_only = true;
        try {
            galaxy::dsp_external_validate_span(
                boundary_context, 0x817fffffu, 2u);
        } catch (const galaxy::DspHardTrap&) {
            worker_preflight_is_arithmetic_only = false;
        }
        passed &= expect(
            worker_preflight_is_arithmetic_only,
            "native DSP worker preflight defers live MRAM mapping lookup to the CPU service");
        passed &= expect_dsp_hard_trap(
            [&] {
                galaxy::dsp_external_validate_span(
                    boundary_context, 0xfffffffeu, 4u);
            },
            "native DSP MRAM rejects a wrapping address span");
        passed &= expect_dsp_hard_trap(
            [&] {
                galaxy::dsp_accelerator_validate_span(
                    boundary_context,
                    kBoundaryArSize - 1u,
                    2u,
                    false);
            },
            "native DSP ARAM rejects a span crossing the 14 MiB boundary");

        galaxy::dsp_accelerator_set_current_address(
            boundary_context, 0x80700000u);
        passed &= expect_dsp_hard_trap(
            [&] {
                galaxy::dsp_accelerator_write_raw(
                    boundary_context, 0xcafeu);
            },
            "native DSP raw accelerator preflights both bytes at ARAM end");
        passed &= expect(
            galaxy::dsp_accelerator_current_address(boundary_context) ==
                0x80700000u,
            "failed native DSP raw accelerator write preserves current address");

        galaxy::NativeDspCoprocessor boundary_dsp;
        boundary_dsp.install_host_services(
            &memory,
            galaxy::host::NativeDspBoundaryTestAccess::services());
        galaxy::NativeDspWorker boundary_worker(
            boundary_dsp,
            invalid_native_dsp_mram_worker_entry,
            /*free_running=*/true);
        boundary_worker.start();
        const bool worker_failed = boundary_worker.wait_for_completed_runs(
            1u, std::chrono::seconds(2));
        passed &= expect(
            worker_failed && boundary_dsp.hard_trap_active() &&
                boundary_dsp.hard_trap_pc() == 0x1234u,
            "native DSP worker records host MRAM boundary rejection as DspHardTrap");
        boundary_worker.stop();

        galaxy::host::NativeDspBoundaryTestAccess::cancel_aram_boundary(
            memory);
        for (std::size_t index = 0u; index < kBoundaryOffsets.size(); ++index) {
            *memory.pointer(kBoundaryOffsets[index], 1u) =
                saved_mram_bytes[index];
            *memory.pointer(
                kBoundaryArBackingBase + kBoundaryOffsets[index], 1u) =
                saved_aram_bytes[index];
        }
        for (std::size_t index = 0u; index < outbound_offsets.size(); ++index) {
            *memory.pointer(
                kBoundaryArBackingBase + outbound_offsets[index], 1u) =
                saved_outbound_bytes[index];
        }
        memory.write_u32(kBoundaryArBackingBaseGlobal, saved_ar_backing);
        memory.write_u32(kBoundaryArSizeGlobal, saved_ar_size);
    }

    galaxy::GuestMemoryV1* guest_memory = memory.guest_memory();
    passed &= expect(
        memory.pointer_or_null(0xC8000000u, 4u) == nullptr,
        "EFB CPU-access aperture is device-backed, not a stale RAM alias");
    {
        EfbPeekProbe probe{};
        probe.value = 0x11223344u;
        memory.set_efb_peek_callback(&efb_peek_probe_callback, &probe);
        const std::uint32_t color = galaxy::guest_load_u32(
            guest_memory,
            efb_peek_addr(123u, 45u, false),
            nullptr,
            0x80004000u);
        passed &= expect(
            color == 0x11223344u &&
                probe.calls == 1u &&
                probe.x == 123u &&
                probe.y == 45u &&
                !probe.depth &&
                probe.memory == guest_memory,
            "GXPeekARGB aperture reads dispatch to the native EFB callback");

        probe.value = 0x00ABCDEFu;
        const std::uint32_t depth = galaxy::guest_load_u32(
            guest_memory,
            efb_peek_addr(639u, 527u, true),
            nullptr,
            0x80004000u);
        passed &= expect(
            depth == 0x00ABCDEFu &&
                probe.calls == 2u &&
                probe.x == 639u &&
                probe.y == 527u &&
                probe.depth,
            "GXPeekZ aperture reads dispatch as native EFB depth queries");
        memory.set_efb_peek_callback(nullptr, nullptr);
    }
    {
        galaxy::NativeServicesV1 fatal_services{};
        fatal_services.fatal = &fatal_throw;
        passed &= expect_runtime_error(
            [&] {
                (void)galaxy::guest_load_u32(
                    guest_memory,
                    efb_peek_addr(0u, 0u, false),
                    &fatal_services,
                    0x80004000u);
            },
            "GXPeek aperture without a native renderer callback must hard-fail");

        EfbPeekProbe failing_probe{};
        memory.set_efb_peek_callback(
            &efb_peek_failing_callback, &failing_probe);
        passed &= expect_runtime_error(
            [&] {
                (void)galaxy::guest_load_u32(
                    guest_memory,
                    efb_peek_addr(12u, 34u, true),
                    &fatal_services,
                    0x80004000u);
            },
            "GXPeek callback failure must hard-fail instead of returning stale data");
        passed &= expect(
            failing_probe.calls == 1u &&
                failing_probe.x == 12u &&
                failing_probe.y == 34u &&
                failing_probe.depth &&
                failing_probe.memory == guest_memory,
            "GXPeek callback failure still receives the decoded native query");

        EfbPeekProbe invalid_probe{};
        invalid_probe.value = 0xFEED1234u;
        memory.set_efb_peek_callback(&efb_peek_probe_callback, &invalid_probe);
        passed &= expect_runtime_error(
            [&] {
                (void)galaxy::guest_load_u16(
                    guest_memory,
                    efb_peek_addr(0u, 0u, false),
                    &fatal_services,
                    0x80004000u);
            },
            "non-u32 EFB aperture reads must hard-fail");
        passed &= expect_runtime_error(
            [&] {
                (void)galaxy::guest_load_u32(
                    guest_memory,
                    0xC8000000u | (2u << 22u),
                    &fatal_services,
                    0x80004000u);
            },
            "unsupported EFB aperture type must hard-fail");
        passed &= expect_runtime_error(
            [&] {
                (void)galaxy::guest_load_u32(
                    guest_memory,
                    efb_peek_addr(640u, 0u, false),
                    &fatal_services,
                    0x80004000u);
            },
            "out-of-range EFB aperture x coordinate must hard-fail");
        passed &= expect_runtime_error(
            [&] {
                (void)galaxy::guest_load_u32(
                    guest_memory,
                    efb_peek_addr(0u, 528u, false),
                    &fatal_services,
                    0x80004000u);
            },
            "out-of-range EFB aperture y coordinate must hard-fail");
        passed &= expect(
            invalid_probe.calls == 0u,
            "invalid EFB aperture shapes do not reach the native renderer callback");
        memory.set_efb_peek_callback(nullptr, nullptr);
    }
    galaxy::guest_store_u64(
        guest_memory,
        0xCC008000,
        0x6145000002000000ull,
        nullptr,
        0x80004000);
    passed &= expect(
        memory.pe_finish_pending(),
        "batched WGPIPE BP 0x45 draw-done raises PE finish");
    galaxy::guest_store_u16(
        guest_memory, 0xCC00100Au, 0x0008u, nullptr, 0x80004000u);
    galaxy::guest_store_u64(
        guest_memory,
        0xCC008000,
        0x614800A000000000ull,
        nullptr,
        0x80004000);
    passed &= expect(
        memory.pe_token_pending() && memory.pe_token_value() == 0xA000u,
        "batched WGPIPE BP 0x48 draw-sync raises PE token");
    galaxy::guest_store_u16(
        guest_memory, 0xCC00100Au, 0x0004u, nullptr, 0x80004000u);
    const std::array<std::uint8_t, 5> fragmented_finish{
        0x61u, 0x45u, 0x00u, 0x00u, 0x02u};
    for (std::uint8_t byte : fragmented_finish) {
        galaxy::guest_store_u8(
            guest_memory,
            0xCC008000,
            byte,
            nullptr,
            0x80004000);
    }
    passed &= expect(
        memory.pe_finish_pending(),
        "fragmented WGPIPE BP 0x45 draw-done raises PE finish");
    galaxy::guest_store_u16(
        guest_memory, 0xCC00100Au, 0x0008u, nullptr, 0x80004000u);
    const std::array<std::uint8_t, 5> fragmented_token{
        0x61u, 0x48u, 0x00u, 0xBEu, 0xEFu};
    for (std::uint8_t byte : fragmented_token) {
        galaxy::guest_store_u8(
            guest_memory,
            0xCC008000,
            byte,
            nullptr,
            0x80004000);
    }
    passed &= expect(
        memory.pe_token_pending() && memory.pe_token_value() == 0xBEEFu,
        "fragmented WGPIPE BP 0x48 draw-sync raises PE token");
    galaxy::guest_store_u16(
        guest_memory, 0xCC00100Au, 0x0004u, nullptr, 0x80004000u);

    {
        auto tail_memory_storage =
            std::make_unique<galaxy::host::GuestAddressSpace>();
        auto& tail_memory = *tail_memory_storage;
        galaxy::GuestMemoryV1* tail_guest_memory = tail_memory.guest_memory();
        tail_memory.pre_wire_cp_fifo(0x00010000u, 0x00011000u);
        galaxy::guest_store_u32(
            tail_guest_memory, 0xCC00300C, 0x00010000u, nullptr, 0x80004000);
        galaxy::guest_store_u32(
            tail_guest_memory, 0xCC003010, 0x00011000u, nullptr, 0x80004000);
        const std::array<std::uint8_t, 5> draw_done_tail{
            0x61u, 0x45u, 0x00u, 0x00u, 0x02u};
        for (std::uint8_t byte : draw_done_tail) {
            galaxy::guest_store_u8(
                tail_guest_memory,
                0xCC008000,
                byte,
                nullptr,
                0x80004000);
        }
        passed &= expect(
            tail_memory.gx_fifo_data().empty(),
            "sub-burst WGPIPE tail stays staged before an explicit boundary");
        tail_memory.flush_wgpipe_gather_tail_for_frame();
        const auto& flushed_tail = tail_memory.gx_fifo_data();
        bool tail_matches = flushed_tail.size() == draw_done_tail.size();
        for (std::size_t i = 0; tail_matches && i < draw_done_tail.size(); ++i) {
            tail_matches =
                flushed_tail[i] == static_cast<std::byte>(draw_done_tail[i]);
        }
        passed &= expect(
            tail_matches,
            "explicit WGPIPE tail flush exposes draw-done bytes to FIFO scans");
    }
    for (bool scan_hints : {false, true}) {
        constexpr std::uint32_t kHintFifoBase = 0x00010000u;
        constexpr std::uint32_t kHintFifoTop = 0x00011000u;
        std::array<std::byte, 64> bytes{};
        // The finish command spans a gather-burst boundary; the token spans
        // separate byte writes. Hints must retain that fragmented context.
        bytes[28] = std::byte{0x61};
        bytes[29] = std::byte{0x45};
        bytes[32] = std::byte{0x02};
        bytes[47] = std::byte{0x61};
        bytes[48] = std::byte{0x48};
        bytes[50] = std::byte{0xAB};
        bytes[51] = std::byte{0xCD};
        const std::vector<std::byte> expected(bytes.begin(), bytes.end());

        auto live_owner =
            std::make_unique<galaxy::host::GuestAddressSpace>(
                galaxy::input::kNativeVirtualHidTimingProfile, scan_hints);
        auto& live = *live_owner;
        auto* live_guest = live.guest_memory();
        live.pre_wire_cp_fifo(kHintFifoBase, kHintFifoTop);
        galaxy::guest_store_u32(
            live_guest, 0xCC00300Cu, kHintFifoBase, nullptr, 0x80004000u);
        galaxy::guest_store_u32(
            live_guest, 0xCC003010u, kHintFifoTop, nullptr, 0x80004000u);
        galaxy::guest_store_u32(
            live_guest, 0xCC003014u, kHintFifoBase, nullptr, 0x80004000u);
        for (std::size_t i = 0; i < bytes.size(); ++i) {
            galaxy::guest_store_u8(
                live_guest, 0xCC008000u,
                std::to_integer<std::uint8_t>(bytes[i]),
                nullptr, 0x80004000u);
            if (i == 31u) {
                passed &= expect(
                    !live.gx_fifo_pe_scan_pending(0u),
                    "an incomplete fragmented PE command does not set a hint");
            } else if (i == 32u) {
                passed &= expect(
                    live.gx_fifo_pe_scan_pending(0u) == scan_hints,
                    "only the enabled scanner tracks a finish across gather bursts");
                live.clear_gx_fifo_pe_scan_hint();
            }
        }
        passed &= expect(
            live.gx_fifo_data() == expected &&
                live.gx_fifo_pe_scan_pending(0u) == scan_hints,
            "optional live-FIFO token hints preserve the exact command stream");
        live.clear_gx_fifo_pe_scan_hint();
        passed &= expect(
            !live.gx_fifo_pe_scan_pending(0u) && live.gx_fifo_data() == expected,
            "consuming a scan hint clears it without discarding FIFO bytes");

        auto ring_owner =
            std::make_unique<galaxy::host::GuestAddressSpace>(
                galaxy::input::kNativeVirtualHidTimingProfile, scan_hints);
        auto& ring = *ring_owner;
        auto* ring_guest = ring.guest_memory();
        ring.pre_wire_cp_fifo(kHintFifoBase, kHintFifoTop);
        // A separate CPU FIFO forces the CP producer to capture the RAM ring.
        galaxy::guest_store_u32(
            ring_guest, 0xCC00300Cu, 0x00020000u, nullptr, 0x80004000u);
        galaxy::guest_store_u32(
            ring_guest, 0xCC003010u, 0x00021000u, nullptr, 0x80004000u);
        for (std::size_t i = 0; i < bytes.size(); ++i) {
            galaxy::guest_store_u8(
                ring_guest, 0x80000000u + kHintFifoBase +
                    static_cast<std::uint32_t>(i),
                std::to_integer<std::uint8_t>(bytes[i]),
                nullptr, 0x80004000u);
        }
        galaxy::guest_store_u16(
            ring_guest, 0xCC000034u, 0x0040u, nullptr, 0x80004000u);
        galaxy::guest_store_u16(
            ring_guest, 0xCC000036u, 0x0001u, nullptr, 0x80004000u);
        passed &= expect(
            ring.gx_fifo_data() == expected &&
                ring.gx_fifo_pe_scan_pending(0u) == scan_hints &&
                !ring.pe_finish_pending() && !ring.pe_token_pending(),
            "optional CP-ring hints preserve FIFO bytes and never publish PE completion");
    }
    {
        auto gather_owner = std::make_unique<galaxy::host::GuestAddressSpace>();
        auto& gather = *gather_owner;
        auto* gather_guest = gather.guest_memory();
        gather.pre_wire_cp_fifo(0x00010000u, 0x00011000u);
        galaxy::guest_store_u32(
            gather_guest, 0xCC00300Cu, 0x00010000u, nullptr, 0x80004000u);
        galaxy::guest_store_u32(
            gather_guest, 0xCC003010u, 0x00011000u, nullptr, 0x80004000u);
        galaxy::guest_store_u32(
            gather_guest, 0xCC003014u, 0x00010000u, nullptr, 0x80004000u);
        std::array<std::byte, 96> source{};
        for (std::size_t i = 0; i < source.size(); ++i) {
            source[i] = static_cast<std::byte>(i + 0x80u);
        }
        bool exact = true;
        for (std::size_t offset = 0; offset < 32u; ++offset) {
            for (std::size_t width = 0; width <= 64u; ++width) {
                gather.clear_gx_fifo();
                const std::span<const std::byte> input(source);
                exact &= gather.write_gx_fifo_bytes(input.first(offset));
                exact &= gather.write_gx_fifo_bytes(input.subspan(offset, width));
                const std::size_t complete_bytes = (offset + width) / 32u * 32u;
                exact &= gather.gx_fifo_data().size() == complete_bytes;
                exact &= std::equal(
                    gather.gx_fifo_data().begin(), gather.gx_fifo_data().end(),
                    source.begin(), source.begin() + complete_bytes);
                gather.flush_wgpipe_gather_tail_for_frame();
                exact &= gather.gx_fifo_data().size() == offset + width;
                exact &= std::equal(
                    gather.gx_fifo_data().begin(), gather.gx_fifo_data().end(),
                    source.begin(), source.begin() + offset + width);
            }
        }
        passed &= expect(
            exact,
            "all FIFO write widths and gather offsets preserve bytes and burst visibility");
    }
    {
        auto alternate_fifo_memory_storage =
            std::make_unique<galaxy::host::GuestAddressSpace>();
        auto& alternate_fifo_memory = *alternate_fifo_memory_storage;
        galaxy::GuestMemoryV1* alternate_guest_memory =
            alternate_fifo_memory.guest_memory();
        constexpr std::uint32_t kGpFifoBase = 0x00010000u;
        constexpr std::uint32_t kGpFifoTop = 0x00011000u;
        constexpr std::uint32_t kCpuFifoBase = 0x00020000u;
        constexpr std::uint32_t kCpuFifoTop = 0x00021000u;
        alternate_fifo_memory.pre_wire_cp_fifo(kGpFifoBase, kGpFifoTop);
        galaxy::guest_store_u32(
            alternate_guest_memory,
            0xCC00300Cu,
            kCpuFifoBase,
            nullptr,
            0x80004000u);
        galaxy::guest_store_u32(
            alternate_guest_memory,
            0xCC003010u,
            kCpuFifoTop,
            nullptr,
            0x80004000u);
        galaxy::guest_store_u32(
            alternate_guest_memory,
            0xCC003014u,
            kCpuFifoBase,
            nullptr,
            0x80004000u);

        std::array<std::uint8_t, 32> alternate_burst{};
        for (std::size_t i = 0; i < alternate_burst.size(); ++i) {
            alternate_burst[i] = static_cast<std::uint8_t>(0x80u + i);
        }
        for (std::size_t i = 0; i < 5u; ++i) {
            galaxy::guest_store_u8(
                alternate_guest_memory,
                0xCC008000u,
                alternate_burst[i],
                nullptr,
                0x80004000u);
        }
        alternate_fifo_memory.flush_wgpipe_gather_tail_for_frame();
        passed &= expect(
            alternate_fifo_memory.gx_fifo_data().empty(),
            "frame boundary does not expose an alternate CPU-FIFO tail");
        for (std::size_t i = 5u; i < alternate_burst.size(); ++i) {
            galaxy::guest_store_u8(
                alternate_guest_memory,
                0xCC008000u,
                alternate_burst[i],
                nullptr,
                0x80004000u);
        }

        bool alternate_burst_matches =
            alternate_fifo_memory.gx_fifo_data().empty();
        for (std::size_t i = 0;
             alternate_burst_matches && i < alternate_burst.size();
             ++i) {
            alternate_burst_matches = galaxy::guest_load_u8(
                alternate_guest_memory,
                0x80000000u + kCpuFifoBase +
                    static_cast<std::uint32_t>(i),
                nullptr,
                0x80004000u) == alternate_burst[i];
        }
        passed &= expect(
            alternate_burst_matches,
            "non-live CPU-FIFO tail survives a frame boundary and completes "
            "one ordered gather burst");
        passed &= expect(
            galaxy::guest_load_u32(
                alternate_guest_memory,
                0xCC003014u,
                nullptr,
                0x80004000u) == kCpuFifoBase + 32u,
            "completed alternate CPU-FIFO gather burst advances its guest-visible "
            "write pointer exactly once");
    }
    {
        ScopedEnv enable_wgpipe_pe_ownership_trace(
            "GALAXY_TRACE_WGPIPE_PE_OWNERSHIP", "1");
        auto diagnostic_memory_storage =
            std::make_unique<galaxy::host::GuestAddressSpace>();
        auto& diagnostic_memory = *diagnostic_memory_storage;
        galaxy::GuestMemoryV1* diagnostic_guest_memory =
            diagnostic_memory.guest_memory();
        std::uint64_t diagnostic_tick_source_calls = 0u;
        diagnostic_memory.set_tick_source(
            [](void* user) -> std::uint64_t {
                auto& calls = *static_cast<std::uint64_t*>(user);
                ++calls;
                return 0x12345678u;
            },
            &diagnostic_tick_source_calls);
        constexpr std::uint32_t kDiagnosticGpBase = 0x00010000u;
        constexpr std::uint32_t kDiagnosticGpTop = 0x00011000u;
        constexpr std::uint32_t kDiagnosticAlternateBase = 0x00020000u;
        constexpr std::uint32_t kDiagnosticAlternateTop = 0x00021000u;
        diagnostic_memory.pre_wire_cp_fifo(
            kDiagnosticGpBase, kDiagnosticGpTop);
        galaxy::guest_store_u32(
            diagnostic_guest_memory,
            0xCC00300Cu,
            kDiagnosticAlternateBase,
            nullptr,
            0x80004000u);
        galaxy::guest_store_u32(
            diagnostic_guest_memory,
            0xCC003010u,
            kDiagnosticAlternateTop,
            nullptr,
            0x80004000u);
        galaxy::guest_store_u32(
            diagnostic_guest_memory,
            0xCC003014u,
            kDiagnosticAlternateBase,
            nullptr,
            0x80004000u);

        const auto emit_fragmented_pe_commands = [](
            galaxy::GuestMemoryV1* target_memory) {
            // Match GXDrawDone's split command-byte/value-word stores.  The
            // diagnostic scanner must retain command context across both
            // writes even while the CPU FIFO is not linked to the GP FIFO.
            galaxy::guest_store_u8(
                target_memory,
                0xCC008000u,
                0x61u,
                nullptr,
                0x80004000u);
            galaxy::guest_store_u32(
                target_memory,
                0xCC008000u,
                0x45000002u,
                nullptr,
                0x80004000u);

            // Exercise both token registers with boundaries inside their BP
            // packets.
            galaxy::guest_store_u16(
                target_memory,
                0xCC008000u,
                0x6147u,
                nullptr,
                0x80004000u);
            galaxy::guest_store_u16(
                target_memory,
                0xCC008000u,
                0x0012u,
                nullptr,
                0x80004000u);
            galaxy::guest_store_u8(
                target_memory,
                0xCC008000u,
                0x34u,
                nullptr,
                0x80004000u);
            galaxy::guest_store_u32(
                target_memory,
                0xCC008000u,
                0x614800ABu,
                nullptr,
                0x80004000u);
            galaxy::guest_store_u8(
                target_memory,
                0xCC008000u,
                0xCDu,
                nullptr,
                0x80004000u);
        };
        emit_fragmented_pe_commands(diagnostic_guest_memory);

        // The native-host test process deliberately enables the independent
        // direct-PE implementation.  Compare against an otherwise-identical
        // trace-disabled address space to prove this observer adds no PE state
        // transition of its own.
        bool control_finish_pending = false;
        bool control_token_pending = false;
        std::uint16_t control_token_value = 0u;
        {
            ScopedEnv disable_control_wgpipe_pe_ownership_trace(
                "GALAXY_TRACE_WGPIPE_PE_OWNERSHIP", "0");
            auto control_memory =
                std::make_unique<galaxy::host::GuestAddressSpace>();
            galaxy::GuestMemoryV1* control_guest_memory =
                control_memory->guest_memory();
            control_memory->pre_wire_cp_fifo(
                kDiagnosticGpBase, kDiagnosticGpTop);
            galaxy::guest_store_u32(
                control_guest_memory,
                0xCC00300Cu,
                kDiagnosticAlternateBase,
                nullptr,
                0x80004000u);
            galaxy::guest_store_u32(
                control_guest_memory,
                0xCC003010u,
                kDiagnosticAlternateTop,
                nullptr,
                0x80004000u);
            galaxy::guest_store_u32(
                control_guest_memory,
                0xCC003014u,
                kDiagnosticAlternateBase,
                nullptr,
                0x80004000u);
            emit_fragmented_pe_commands(control_guest_memory);
            control_finish_pending = control_memory->pe_finish_pending();
            control_token_pending = control_memory->pe_token_pending();
            control_token_value = control_memory->pe_token_value();
        }
        passed &= expect(
            diagnostic_memory.pe_finish_pending() == control_finish_pending &&
                diagnostic_memory.pe_token_pending() == control_token_pending &&
                diagnostic_memory.pe_token_value() == control_token_value,
            "WGPIPE PE ownership observer leaves fragmented BP45/BP47/BP48 "
            "pending state identical to a trace-disabled control");

        // Record a CP write-pointer transition only after the native drain has
        // advanced the read pointer, so the snapshot describes the completed
        // transition rather than its pre-state.
        galaxy::guest_store_u16(
            diagnostic_guest_memory,
            0xCC000034u,
            static_cast<std::uint16_t>(kDiagnosticGpBase + 32u),
            nullptr,
            0x80004000u);
        galaxy::guest_store_u16(
            diagnostic_guest_memory,
            0xCC000036u,
            static_cast<std::uint16_t>(kDiagnosticGpBase >> 16u),
            nullptr,
            0x80004000u);

        std::ostringstream diagnostic_trace;
        {
            ScopedStreamRedirect capture_diagnostic_trace(
                std::cerr, diagnostic_trace.rdbuf());
            passed &= expect(
                diagnostic_memory.prepare_for_process_exit(),
                "WGPIPE PE ownership diagnostic prepares exactly once");
        }
        const std::string diagnostic_output = diagnostic_trace.str();
        passed &= expect(
            diagnostic_output.find(
                "kind=pe reg=0x0045 previous=0x00000000 "
                "value=0x00000002 write-bytes=4 live-before=0 live-after=0 "
                "gather-before=1 gather-after=5") != std::string::npos,
            "WGPIPE PE ownership diagnostic detects fragmented BP45 on a "
            "non-live CPU FIFO with exact gather state");
        passed &= expect(
            diagnostic_output.find(
                "kind=pi-cpu-fifo-register reg=0x300c") !=
                std::string::npos &&
            diagnostic_output.find("ticks=0") != std::string::npos &&
            diagnostic_output.find("cpu-base=0x00020000") !=
                std::string::npos &&
            diagnostic_output.find("cp-base=0x00010000") !=
                std::string::npos,
            "WGPIPE PE ownership diagnostic defers bounded PI/CP ownership "
            "snapshots without observing the mutable native tick source");
        passed &= expect(
            diagnostic_output.find(
                "kind=pe reg=0x0047 previous=0x00000000 "
                "value=0x00001234") != std::string::npos &&
                diagnostic_output.find(
                    "kind=pe reg=0x0048 previous=0x00000000 "
                    "value=0x0000abcd") != std::string::npos,
            "WGPIPE PE ownership diagnostic detects fragmented BP47/BP48 "
            "without changing PE pending state");
        passed &= expect(
            diagnostic_output.find(
                "kind=cp-fifo-register reg=0x0036 "
                "previous=0x00010000 value=0x00010020") !=
                std::string::npos &&
                diagnostic_output.find("cp-read=0x00010020") !=
                std::string::npos,
            "WGPIPE PE ownership diagnostic records CP write-pointer state "
            "after the corresponding drain");
        passed &= expect(
            diagnostic_tick_source_calls == 0u,
            "WGPIPE PE ownership diagnostic never calls the mutable runtime "
            "tick source");
    }

    const std::array<std::byte, 19> ios_path{
        std::byte{'/'},
        std::byte{'d'},
        std::byte{'e'},
        std::byte{'v'},
        std::byte{'/'},
        std::byte{'s'},
        std::byte{'t'},
        std::byte{'m'},
        std::byte{'/'},
        std::byte{'i'},
        std::byte{'m'},
        std::byte{'m'},
        std::byte{'e'},
        std::byte{'d'},
        std::byte{'i'},
        std::byte{'a'},
        std::byte{'t'},
        std::byte{'e'},
        std::byte{0},
    };
    constexpr std::uint32_t kIosRequest = 0x133E1000;
    constexpr std::uint32_t kIosPath = 0x133E1080;
    memory.copy(kIosPath, ios_path);
    memory.write_u32(kIosRequest, 1);
    memory.write_u32(kIosRequest + 0x0C, kIosPath);
    memory.write_u32(kIosRequest + 0x20, 0);
    submit_ios_request(guest_memory, kIosRequest);
    passed &= expect(
        memory.read_u32(kIosRequest + 4) == 1,
        "native IOS assigns a handle to a valid open request");
    passed &= expect(
        expect_ios_reply(
            memory,
            guest_memory,
            kIosRequest,
            1,
            1,
            "synchronous IOS open reply is posted through IPC"),
        "synchronous IOS open reply is posted through IPC");
    passed &= expect(
        memory.read_u32(0x8069E390) == 1,
        "native IOS returns the mailbox acknowledgement token");
    passed &= expect(
        galaxy::guest_load_u32(
            guest_memory, 0xCD000008, nullptr, 0x80004000) == kIosRequest,
        "native IOS publishes the reply request address");
    passed &= expect(
        (galaxy::guest_load_u32(
             guest_memory, 0xCD000004, nullptr, 0x80004000) &
         0x06) == 0x06,
        "native IOS publishes acknowledgement and reply status");
    acknowledge_ios_reply(guest_memory);
    passed &= expect(
        !ios_reply_available(guest_memory),
        "synchronous IOS reply is consumed exactly once");

    constexpr std::uint32_t kAsyncRequest = 0x133E1100;
    memory.write_u32(kAsyncRequest, 1);
    memory.write_u32(kAsyncRequest + 0x0C, kIosPath);
    memory.write_u32(kAsyncRequest + 0x20, 0x80400000);
    memory.write_u32(kAsyncRequest + 0x24, 0x81234560);
    submit_ios_request(guest_memory, kAsyncRequest);
    passed &= expect(
        expect_ios_reply(
            memory,
            guest_memory,
            kAsyncRequest,
            2,
            1,
            "asynchronous IOS open reply is posted through IPC"),
        "asynchronous IOS open reply is posted through IPC");
    passed &= expect(
        memory.read_u32(kAsyncRequest + 0x20) == 0x80400000 &&
            memory.read_u32(kAsyncRequest + 0x24) == 0x81234560,
        "asynchronous IOS reply preserves callback state for the guest handler");
    acknowledge_ios_reply(guest_memory);
    passed &= expect(
        !ios_reply_available(guest_memory),
        "asynchronous IOS reply is consumed exactly once");

    constexpr std::uint32_t kBluetoothPath = 0x133E1200;
    constexpr std::uint32_t kBluetoothOpen = 0x133E1280;
    constexpr std::array<std::byte, 22> bluetooth_path{
        std::byte{'/'}, std::byte{'d'}, std::byte{'e'}, std::byte{'v'},
        std::byte{'/'}, std::byte{'u'}, std::byte{'s'}, std::byte{'b'},
        std::byte{'/'}, std::byte{'o'}, std::byte{'h'}, std::byte{'1'},
        std::byte{'/'}, std::byte{'5'}, std::byte{'7'}, std::byte{'e'},
        std::byte{'/'}, std::byte{'3'}, std::byte{'0'}, std::byte{'5'},
        std::byte{0}, std::byte{0},
    };
    memory.copy(kBluetoothPath, bluetooth_path);
    memory.write_u32(kBluetoothOpen, 1);
    memory.write_u32(kBluetoothOpen + 0x0C, kBluetoothPath);
    submit_ios_request(guest_memory, kBluetoothOpen);
    const std::uint32_t bluetooth_handle = memory.read_u32(kBluetoothOpen + 4);
    passed &= expect(
        bluetooth_handle == 3,
        "native IOS tracks the Bluetooth USB device handle");
    passed &= expect(
        expect_ios_reply(
            memory,
            guest_memory,
            kBluetoothOpen,
            bluetooth_handle,
            1,
            "Bluetooth open posts an IOS reply"),
        "Bluetooth open posts an IOS reply");
    acknowledge_ios_reply(guest_memory);

    constexpr std::uint32_t kInterruptRequest = 0x133E1300;
    constexpr std::uint32_t kInterruptVectors = 0x133E1380;
    constexpr std::uint32_t kInterruptEndpoint = 0x133E1400;
    constexpr std::uint32_t kInterruptLength = 0x133E1420;
    constexpr std::uint32_t kInterruptBuffer = 0x133E1440;
    *reinterpret_cast<std::uint8_t*>(memory.pointer(kInterruptEndpoint, 1)) =
        0x81;
    *reinterpret_cast<std::uint16_t*>(memory.pointer(kInterruptLength, 2)) =
        0x2000;
    memory.write_u32(kInterruptVectors, kInterruptEndpoint);
    memory.write_u32(kInterruptVectors + 4, 1);
    memory.write_u32(kInterruptVectors + 8, kInterruptLength);
    memory.write_u32(kInterruptVectors + 12, 2);
    memory.write_u32(kInterruptVectors + 16, kInterruptBuffer);
    memory.write_u32(kInterruptVectors + 20, 32);
    memory.write_u32(kInterruptVectors + 20, 32);
    memory.write_u32(kInterruptRequest, 7);
    memory.write_u32(kInterruptRequest + 8, bluetooth_handle);
    memory.write_u32(kInterruptRequest + 0x0C, 2);
    memory.write_u32(kInterruptRequest + 0x10, 2);
    memory.write_u32(kInterruptRequest + 0x14, 1);
    memory.write_u32(kInterruptRequest + 0x18, kInterruptVectors);
    memory.write_u32(kInterruptRequest + 0x20, 0x80401000);
    memory.write_u32(kInterruptRequest + 0x24, 0x81234000);
    submit_ios_request(guest_memory, kInterruptRequest);
    passed &= expect(
        !ios_reply_available(guest_memory),
        "Bluetooth interrupt read waits for a controller event");
    acknowledge_ios_ack(guest_memory);

    constexpr std::uint32_t kControlRequest = 0x133E1500;
    constexpr std::uint32_t kControlVectors = 0x133E1580;
    constexpr std::uint32_t kHciCommand = 0x133E1680;
    const std::array<std::byte, 3> reset_command{
        std::byte{0x03}, std::byte{0x0C}, std::byte{0}};
    memory.copy(kHciCommand, reset_command);
    for (std::uint32_t index = 0; index < 6; ++index) {
        memory.write_u32(kControlVectors + index * 8, kInterruptEndpoint);
        memory.write_u32(kControlVectors + index * 8 + 4, 1);
    }
    memory.write_u32(kControlVectors + 48, kHciCommand);
    memory.write_u32(kControlVectors + 52, 3);
    memory.write_u32(kControlRequest, 7);
    memory.write_u32(kControlRequest + 8, bluetooth_handle);
    memory.write_u32(kControlRequest + 0x0C, 0);
    memory.write_u32(kControlRequest + 0x10, 6);
    memory.write_u32(kControlRequest + 0x14, 1);
    memory.write_u32(kControlRequest + 0x18, kControlVectors);
    memory.write_u32(kControlRequest + 0x20, 0x80402000);
    memory.write_u32(kControlRequest + 0x24, 0x81235000);
    submit_ios_request(guest_memory, kControlRequest);
    passed &= expect(
        expect_ios_reply(
            memory,
            guest_memory,
            kControlRequest,
            0,
            7,
            "Bluetooth control transfer posts before its HCI event"),
        "Bluetooth control transfer posts before its HCI event");
    acknowledge_ios_reply(guest_memory);
    passed &= expect(
        expect_ios_reply(
            memory,
            guest_memory,
            kInterruptRequest,
            6,
            7,
            "Bluetooth interrupt read receives the HCI reset completion"),
        "Bluetooth interrupt read receives the HCI reset completion");
    acknowledge_ios_reply(guest_memory);
    const auto* reset_event = reinterpret_cast<const std::uint8_t*>(
        memory.pointer(kInterruptBuffer, 6));
    passed &= expect(
        reset_event[0] == 0x0E && reset_event[1] == 4 &&
            reset_event[2] == 1 && reset_event[3] == 0x03 &&
            reset_event[4] == 0x0C && reset_event[5] == 0,
        "Bluetooth HCI command-complete packet has the expected wire format");
    passed &= expect(
        !ios_reply_available(guest_memory),
        "Bluetooth event creates no duplicate callbacks");

    memory.write_u32(kInterruptRequest, 7);
    memory.write_u32(kInterruptRequest + 8, bluetooth_handle);
    memory.write_u32(kInterruptRequest + 0x0C, 2);
    memory.write_u32(kInterruptRequest + 0x10, 2);
    memory.write_u32(kInterruptRequest + 0x14, 1);
    memory.write_u32(kInterruptRequest + 0x18, kInterruptVectors);
    memory.write_u32(kInterruptRequest + 0x20, 0x80401000);
    memory.write_u32(kInterruptRequest + 0x24, 0x81234000);
    submit_ios_request(guest_memory, kInterruptRequest);
    passed &= expect(
        !ios_reply_available(guest_memory),
        "Bluetooth interrupt read re-arms for the next controller event");
    acknowledge_ios_ack(guest_memory);

    const std::array<std::byte, 6> write_class_command{
        std::byte{0x24},
        std::byte{0x0C},
        std::byte{3},
        std::byte{0x04},
        std::byte{0x25},
        std::byte{0x00},
    };
    memory.copy(kHciCommand, write_class_command);
    memory.write_u32(
        kControlVectors + 52,
        static_cast<std::uint32_t>(write_class_command.size()));
    memory.write_u32(kControlRequest, 7);
    memory.write_u32(kControlRequest + 8, bluetooth_handle);
    memory.write_u32(kControlRequest + 0x0C, 0);
    memory.write_u32(kControlRequest + 0x10, 6);
    memory.write_u32(kControlRequest + 0x14, 1);
    memory.write_u32(kControlRequest + 0x18, kControlVectors);
    submit_ios_request(guest_memory, kControlRequest);
    passed &= expect(
        expect_ios_reply(
            memory,
            guest_memory,
            kControlRequest,
            0,
            7,
            "Bluetooth Write_Class_of_Device control transfer completes"),
        "Bluetooth Write_Class_of_Device control transfer completes");
    acknowledge_ios_reply(guest_memory);
    passed &= expect(
        expect_ios_reply(
            memory,
            guest_memory,
            kInterruptRequest,
            6,
            7,
            "Bluetooth interrupt read receives Write_Class_of_Device completion"),
        "Bluetooth interrupt read receives Write_Class_of_Device completion");
    acknowledge_ios_reply(guest_memory);
    const auto* write_class_event = reinterpret_cast<const std::uint8_t*>(
        memory.pointer(kInterruptBuffer, 6));
    passed &= expect(
        write_class_event[0] == 0x0E && write_class_event[1] == 4 &&
            write_class_event[2] == 1 && write_class_event[3] == 0x24 &&
            write_class_event[4] == 0x0C && write_class_event[5] == 0,
        "Bluetooth Write_Class_of_Device completion has status-only payload");

    memory.write_u32(kInterruptRequest, 7);
    memory.write_u32(kInterruptRequest + 8, bluetooth_handle);
    memory.write_u32(kInterruptRequest + 0x0C, 2);
    memory.write_u32(kInterruptRequest + 0x10, 2);
    memory.write_u32(kInterruptRequest + 0x14, 1);
    memory.write_u32(kInterruptRequest + 0x18, kInterruptVectors);
    memory.write_u32(kInterruptRequest + 0x20, 0x80401000);
    memory.write_u32(kInterruptRequest + 0x24, 0x81234000);
    submit_ios_request(guest_memory, kInterruptRequest);
    passed &= expect(
        !ios_reply_available(guest_memory),
        "Bluetooth interrupt read re-arms before local-name setup");
    acknowledge_ios_ack(guest_memory);

    std::vector<std::byte> write_name_command(251u, std::byte{0});
    write_name_command[0] = std::byte{0x13};
    write_name_command[1] = std::byte{0x0C};
    write_name_command[2] = std::byte{248};
    memory.copy(kHciCommand, write_name_command);
    memory.write_u32(
        kControlVectors + 52,
        static_cast<std::uint32_t>(write_name_command.size()));
    memory.write_u32(kControlRequest, 7);
    memory.write_u32(kControlRequest + 8, bluetooth_handle);
    memory.write_u32(kControlRequest + 0x0C, 0);
    memory.write_u32(kControlRequest + 0x10, 6);
    memory.write_u32(kControlRequest + 0x14, 1);
    memory.write_u32(kControlRequest + 0x18, kControlVectors);
    submit_ios_request(guest_memory, kControlRequest);
    passed &= expect(
        expect_ios_reply(
            memory,
            guest_memory,
            kControlRequest,
            0,
            7,
            "Bluetooth Write_Local_Name control transfer completes"),
        "Bluetooth Write_Local_Name control transfer completes");
    acknowledge_ios_reply(guest_memory);
    passed &= expect(
        expect_ios_reply(
            memory,
            guest_memory,
            kInterruptRequest,
            6,
            7,
            "Bluetooth interrupt read receives Write_Local_Name completion"),
        "Bluetooth interrupt read receives Write_Local_Name completion");
    acknowledge_ios_reply(guest_memory);
    const auto* write_name_event = reinterpret_cast<const std::uint8_t*>(
        memory.pointer(kInterruptBuffer, 6));
    passed &= expect(
        write_name_event[0] == 0x0E && write_name_event[1] == 4 &&
            write_name_event[2] == 1 && write_name_event[3] == 0x13 &&
            write_name_event[4] == 0x0C && write_name_event[5] == 0,
        "Bluetooth Write_Local_Name completion has status-only payload");

    std::vector<std::vector<std::byte>> status_only_setup_commands{
        {std::byte{0x0A}, std::byte{0x0C}, std::byte{1}, std::byte{0}},
        {std::byte{0x18}, std::byte{0x0C}, std::byte{2}, std::byte{0x00},
         std::byte{0x20}},
        {std::byte{0x1A}, std::byte{0x0C}, std::byte{1}, std::byte{0x03}},
        {std::byte{0x1C}, std::byte{0x0C}, std::byte{4}, std::byte{0x00},
         std::byte{0x08}, std::byte{0x12}, std::byte{0x00}},
        {std::byte{0x1E}, std::byte{0x0C}, std::byte{4}, std::byte{0x00},
         std::byte{0x08}, std::byte{0x12}, std::byte{0x00}},
        {std::byte{0x20}, std::byte{0x0C}, std::byte{1}, std::byte{0}},
        {std::byte{0x33}, std::byte{0x0C}, std::byte{7}, std::byte{0x53},
         std::byte{0x01}, std::byte{0x40}, std::byte{0x0A}, std::byte{0},
         std::byte{0}, std::byte{0}},
        {std::byte{0x43}, std::byte{0x0C}, std::byte{1}, std::byte{1}},
        {std::byte{0x45}, std::byte{0x0C}, std::byte{1}, std::byte{2}},
        {std::byte{0x47}, std::byte{0x0C}, std::byte{1}, std::byte{1}},
    };
    std::vector<std::byte> write_patch_command(187, std::byte{0});
    write_patch_command[0] = std::byte{0x4C};
    write_patch_command[1] = std::byte{0xFC};
    write_patch_command[2] = std::byte{184};
    write_patch_command[3] = std::byte{0x78};
    write_patch_command[4] = std::byte{0x56};
    write_patch_command[5] = std::byte{0x34};
    write_patch_command[6] = std::byte{0x12};
    write_patch_command[7] = std::byte{0xA5};
    std::vector<std::byte> install_patch_command(95, std::byte{0});
    install_patch_command[0] = std::byte{0x4F};
    install_patch_command[1] = std::byte{0xFC};
    install_patch_command[2] = std::byte{92};
    install_patch_command[3] = std::byte{7};
    status_only_setup_commands.push_back(
        {std::byte{0x4F}, std::byte{0xFC}, std::byte{1}, std::byte{0}});
    status_only_setup_commands.push_back(std::move(write_patch_command));
    status_only_setup_commands.push_back(std::move(install_patch_command));
    for (const auto& command : status_only_setup_commands) {
        const std::uint16_t opcode = static_cast<std::uint16_t>(
            static_cast<std::uint8_t>(command[0]) |
            static_cast<std::uint16_t>(
                static_cast<std::uint8_t>(command[1])) << 8);
        memory.write_u32(kInterruptRequest, 7);
        memory.write_u32(kInterruptRequest + 8, bluetooth_handle);
        memory.write_u32(kInterruptRequest + 0x0C, 2);
        memory.write_u32(kInterruptRequest + 0x10, 2);
        memory.write_u32(kInterruptRequest + 0x14, 1);
        memory.write_u32(kInterruptRequest + 0x18, kInterruptVectors);
        memory.write_u32(kInterruptRequest + 0x20, 0x80401000);
        memory.write_u32(kInterruptRequest + 0x24, 0x81234000);
        submit_ios_request(guest_memory, kInterruptRequest);
        acknowledge_ios_ack(guest_memory);

        memory.copy(kHciCommand, command);
        memory.write_u32(
            kControlVectors + 52,
            static_cast<std::uint32_t>(command.size()));
        memory.write_u32(kControlRequest, 7);
        memory.write_u32(kControlRequest + 8, bluetooth_handle);
        memory.write_u32(kControlRequest + 0x0C, 0);
        memory.write_u32(kControlRequest + 0x10, 6);
        memory.write_u32(kControlRequest + 0x14, 1);
        memory.write_u32(kControlRequest + 0x18, kControlVectors);
        submit_ios_request(guest_memory, kControlRequest);
        passed &= expect(
            expect_ios_reply(
                memory,
                guest_memory,
                kControlRequest,
                0,
                7,
                "Bluetooth status-only setup control transfer completes"),
            "Bluetooth status-only setup control transfer completes");
        acknowledge_ios_reply(guest_memory);
        passed &= expect(
            expect_ios_reply(
                memory,
                guest_memory,
                kInterruptRequest,
                6,
                7,
                "Bluetooth status-only setup interrupt completion arrives"),
            "Bluetooth status-only setup interrupt completion arrives");
        acknowledge_ios_reply(guest_memory);
        const auto* setup_event = reinterpret_cast<const std::uint8_t*>(
            memory.pointer(kInterruptBuffer, 6));
        const bool event_ok =
            setup_event[0] == 0x0E && setup_event[1] == 4 &&
            setup_event[2] == 1 &&
            setup_event[3] == static_cast<std::uint8_t>(opcode) &&
            setup_event[4] == static_cast<std::uint8_t>(opcode >> 8) &&
            setup_event[5] == 0;
        if (!event_ok) {
            std::cerr << "FAILED: Bluetooth status-only setup completion for "
                         "opcode 0x"
                      << std::hex << opcode << std::dec << '\n';
        }
        passed &= event_ok;
    }

    memory.write_u32(kInterruptRequest, 7);
    memory.write_u32(kInterruptRequest + 8, bluetooth_handle);
    memory.write_u32(kInterruptRequest + 0x0C, 2);
    memory.write_u32(kInterruptRequest + 0x10, 2);
    memory.write_u32(kInterruptRequest + 0x14, 1);
    memory.write_u32(kInterruptRequest + 0x18, kInterruptVectors);
    memory.write_u32(kInterruptRequest + 0x20, 0x80401000);
    memory.write_u32(kInterruptRequest + 0x24, 0x81234000);
    submit_ios_request(guest_memory, kInterruptRequest);
    acknowledge_ios_ack(guest_memory);

    const std::array<std::byte, 10> read_stored_key_command{
        std::byte{0x0D},
        std::byte{0x0C},
        std::byte{7},
        std::byte{0},
        std::byte{0},
        std::byte{0},
        std::byte{0},
        std::byte{0},
        std::byte{0},
        std::byte{1},
    };
    memory.copy(kHciCommand, read_stored_key_command);
    memory.write_u32(
        kControlVectors + 52,
        static_cast<std::uint32_t>(read_stored_key_command.size()));
    memory.write_u32(kControlRequest, 7);
    memory.write_u32(kControlRequest + 8, bluetooth_handle);
    memory.write_u32(kControlRequest + 0x0C, 0);
    memory.write_u32(kControlRequest + 0x10, 6);
    memory.write_u32(kControlRequest + 0x14, 1);
    memory.write_u32(kControlRequest + 0x18, kControlVectors);
    submit_ios_request(guest_memory, kControlRequest);
    passed &= expect(
        expect_ios_reply(
            memory,
            guest_memory,
            kControlRequest,
            0,
            7,
            "Bluetooth Read_Stored_Link_Key control transfer completes"),
        "Bluetooth Read_Stored_Link_Key control transfer completes");
    acknowledge_ios_reply(guest_memory);
    passed &= expect(
        expect_ios_reply(
            memory,
            guest_memory,
            kInterruptRequest,
            25,
            7,
            "Bluetooth interrupt read receives Return_Link_Keys event"),
        "Bluetooth interrupt read receives Return_Link_Keys event");
    acknowledge_ios_reply(guest_memory);
    const auto* read_key_event = reinterpret_cast<const std::uint8_t*>(
        memory.pointer(kInterruptBuffer, 25));
    passed &= expect(
        read_key_event[0] == 0x15 && read_key_event[1] == 0x17 &&
            read_key_event[2] == 1 &&
            read_key_event[3] == 0x11 && read_key_event[4] == 0x02 &&
            read_key_event[5] == 0x19 && read_key_event[6] == 0x79 &&
            read_key_event[7] == 0x00 && read_key_event[8] == 0x00 &&
            std::all_of(
                read_key_event + 9, read_key_event + 25,
                [](std::uint8_t value) { return value == 0xA0; }),
        "Bluetooth Return_Link_Keys reports the seeded primary native Wiimote key");
    memory.write_u32(kInterruptVectors + 20, 32);
    memory.write_u32(kInterruptRequest, 7);
    memory.write_u32(kInterruptRequest + 8, bluetooth_handle);
    memory.write_u32(kInterruptRequest + 0x0C, 2);
    memory.write_u32(kInterruptRequest + 0x10, 2);
    memory.write_u32(kInterruptRequest + 0x14, 1);
    memory.write_u32(kInterruptRequest + 0x18, kInterruptVectors);
    memory.write_u32(kInterruptRequest + 0x20, 0x80401000);
    memory.write_u32(kInterruptRequest + 0x24, 0x81234000);
    submit_ios_request(guest_memory, kInterruptRequest);
    passed &= expect(
        expect_ios_reply(
            memory,
            guest_memory,
            kInterruptRequest,
            10,
            7,
            "Bluetooth interrupt read receives Read_Stored_Link_Key completion"),
        "Bluetooth interrupt read receives Read_Stored_Link_Key completion");
    acknowledge_ios_reply(guest_memory);
    read_key_event = reinterpret_cast<const std::uint8_t*>(
        memory.pointer(kInterruptBuffer, 10));
    passed &= expect(
        read_key_event[0] == 0x0E && read_key_event[1] == 8 &&
            read_key_event[2] == 1 && read_key_event[3] == 0x0D &&
            read_key_event[4] == 0x0C && read_key_event[5] == 0 &&
            read_key_event[6] == 0xFF && read_key_event[7] == 0 &&
            read_key_event[8] == 1 && read_key_event[9] == 0,
        "Bluetooth Read_Stored_Link_Key reports the paired primary native Wiimote key");

    memory.write_u32(kInterruptRequest, 7);
    memory.write_u32(kInterruptRequest + 8, bluetooth_handle);
    memory.write_u32(kInterruptRequest + 0x0C, 2);
    memory.write_u32(kInterruptRequest + 0x10, 2);
    memory.write_u32(kInterruptRequest + 0x14, 1);
    memory.write_u32(kInterruptRequest + 0x18, kInterruptVectors);
    memory.write_u32(kInterruptRequest + 0x20, 0x80401000);
    memory.write_u32(kInterruptRequest + 0x24, 0x81234000);
    submit_ios_request(guest_memory, kInterruptRequest);
    acknowledge_ios_ack(guest_memory);
    const std::array<std::byte, 10> delete_stored_key_command{
        std::byte{0x12},
        std::byte{0x0C},
        std::byte{7},
        std::byte{0x11},
        std::byte{0x02},
        std::byte{0x19},
        std::byte{0x79},
        std::byte{0x00},
        std::byte{0x00},
        std::byte{0},
    };
    memory.copy(kHciCommand, delete_stored_key_command);
    memory.write_u32(
        kControlVectors + 52,
        static_cast<std::uint32_t>(delete_stored_key_command.size()));
    memory.write_u32(kControlRequest, 7);
    memory.write_u32(kControlRequest + 8, bluetooth_handle);
    memory.write_u32(kControlRequest + 0x0C, 0);
    memory.write_u32(kControlRequest + 0x10, 6);
    memory.write_u32(kControlRequest + 0x14, 1);
    memory.write_u32(kControlRequest + 0x18, kControlVectors);
    submit_ios_request(guest_memory, kControlRequest);
    passed &= expect(
        expect_ios_reply(
            memory,
            guest_memory,
            kControlRequest,
            0,
            7,
            "Bluetooth Delete_Stored_Link_Key control transfer completes"),
        "Bluetooth Delete_Stored_Link_Key control transfer completes");
    acknowledge_ios_reply(guest_memory);
    passed &= expect(
        expect_ios_reply(
            memory,
            guest_memory,
            kInterruptRequest,
            8,
            7,
            "Bluetooth interrupt read receives Delete_Stored_Link_Key completion"),
        "Bluetooth interrupt read receives Delete_Stored_Link_Key completion");
    acknowledge_ios_reply(guest_memory);
    const auto* delete_key_event = reinterpret_cast<const std::uint8_t*>(
        memory.pointer(kInterruptBuffer, 8));
    passed &= expect(
        delete_key_event[0] == 0x0E && delete_key_event[1] == 6 &&
            delete_key_event[2] == 1 && delete_key_event[3] == 0x12 &&
            delete_key_event[4] == 0x0C && delete_key_event[5] == 0 &&
            delete_key_event[6] == 0 && delete_key_event[7] == 0,
        "Bluetooth Delete_Stored_Link_Key keeps the SYSCONF native key");

    std::array<std::byte, 10> unknown_read_key_command =
        read_stored_key_command;
    unknown_read_key_command[3] = std::byte{0x10};
    unknown_read_key_command[4] = std::byte{0x20};
    unknown_read_key_command[5] = std::byte{0x30};
    unknown_read_key_command[6] = std::byte{0x40};
    unknown_read_key_command[7] = std::byte{0x50};
    unknown_read_key_command[8] = std::byte{0x60};
    unknown_read_key_command[9] = std::byte{0};
    memory.copy(kHciCommand, unknown_read_key_command);
    memory.write_u32(
        kControlVectors + 52,
        static_cast<std::uint32_t>(unknown_read_key_command.size()));
    memory.write_u32(kControlRequest, 7);
    memory.write_u32(kControlRequest + 8, bluetooth_handle);
    memory.write_u32(kControlRequest + 0x0C, 0);
    memory.write_u32(kControlRequest + 0x10, 6);
    memory.write_u32(kControlRequest + 0x14, 1);
    memory.write_u32(kControlRequest + 0x18, kControlVectors);
    submit_ios_request(guest_memory, kControlRequest);
    passed &= expect(
        expect_ios_reply(
            memory,
            guest_memory,
            kControlRequest,
            0,
            7,
            "Bluetooth unknown Read_Stored_Link_Key control transfer completes"),
        "Bluetooth unknown Read_Stored_Link_Key control transfer completes");
    acknowledge_ios_reply(guest_memory);
    memory.write_u32(kInterruptRequest, 7);
    memory.write_u32(kInterruptRequest + 8, bluetooth_handle);
    memory.write_u32(kInterruptRequest + 0x0C, 2);
    memory.write_u32(kInterruptRequest + 0x10, 2);
    memory.write_u32(kInterruptRequest + 0x14, 1);
    memory.write_u32(kInterruptRequest + 0x18, kInterruptVectors);
    memory.write_u32(kInterruptRequest + 0x20, 0x80401000);
    memory.write_u32(kInterruptRequest + 0x24, 0x81234000);
    submit_ios_request(guest_memory, kInterruptRequest);
    passed &= expect(
        expect_ios_reply(
            memory,
            guest_memory,
            kInterruptRequest,
            10,
            7,
            "Bluetooth interrupt read receives unknown Read_Stored_Link_Key completion"),
        "Bluetooth interrupt read receives unknown Read_Stored_Link_Key completion");
    acknowledge_ios_reply(guest_memory);
    read_key_event = reinterpret_cast<const std::uint8_t*>(
        memory.pointer(kInterruptBuffer, 10));
    passed &= expect(
        read_key_event[0] == 0x0E && read_key_event[1] == 8 &&
            read_key_event[2] == 1 && read_key_event[3] == 0x0D &&
            read_key_event[4] == 0x0C && read_key_event[5] == 0 &&
            read_key_event[6] == 0xFF && read_key_event[7] == 0 &&
            read_key_event[8] == 0 && read_key_event[9] == 0,
        "Bluetooth direct Read_Stored_Link_Key reports zero unknown keys");

    std::array<std::byte, 10> malformed_read_key_command =
        read_stored_key_command;
    malformed_read_key_command[9] = std::byte{2};
    memory.copy(kHciCommand, malformed_read_key_command);
    memory.write_u32(
        kControlVectors + 52,
        static_cast<std::uint32_t>(malformed_read_key_command.size()));
    memory.write_u32(kControlRequest, 7);
    memory.write_u32(kControlRequest + 8, bluetooth_handle);
    memory.write_u32(kControlRequest + 0x0C, 0);
    memory.write_u32(kControlRequest + 0x10, 6);
    memory.write_u32(kControlRequest + 0x14, 1);
    memory.write_u32(kControlRequest + 0x18, kControlVectors);
    passed &= expect_runtime_error(
        [&] { submit_ios_request(guest_memory, kControlRequest); },
        "Bluetooth Read_Stored_Link_Key rejects invalid Read_All_Flag");

    const std::array<std::byte, 7> malformed_write_patch_command{
        std::byte{0x4C}, std::byte{0xFC}, std::byte{4}, std::byte{0},
        std::byte{0}, std::byte{0}, std::byte{0}};
    memory.copy(kHciCommand, malformed_write_patch_command);
    memory.write_u32(
        kControlVectors + 52,
        static_cast<std::uint32_t>(malformed_write_patch_command.size()));
    memory.write_u32(kControlRequest, 7);
    memory.write_u32(kControlRequest + 8, bluetooth_handle);
    memory.write_u32(kControlRequest + 0x0C, 0);
    memory.write_u32(kControlRequest + 0x10, 6);
    memory.write_u32(kControlRequest + 0x14, 1);
    memory.write_u32(kControlRequest + 0x18, kControlVectors);
    passed &= expect_runtime_error(
        [&] { submit_ios_request(guest_memory, kControlRequest); },
        "Bluetooth Nintendo write-patch rejects payload without patch bytes");

    const std::array<std::byte, 5> malformed_install_patch_command{
        std::byte{0x4F}, std::byte{0xFC}, std::byte{2}, std::byte{7},
        std::byte{0}};
    memory.copy(kHciCommand, malformed_install_patch_command);
    memory.write_u32(
        kControlVectors + 52,
        static_cast<std::uint32_t>(malformed_install_patch_command.size()));
    memory.write_u32(kControlRequest, 7);
    memory.write_u32(kControlRequest + 8, bluetooth_handle);
    memory.write_u32(kControlRequest + 0x0C, 0);
    memory.write_u32(kControlRequest + 0x10, 6);
    memory.write_u32(kControlRequest + 0x14, 1);
    memory.write_u32(kControlRequest + 0x18, kControlVectors);
    passed &= expect_runtime_error(
        [&] { submit_ios_request(guest_memory, kControlRequest); },
        "Bluetooth Nintendo install-patch rejects count/size mismatch");

    const std::array<std::byte, 3> unsupported_hci_command{
        std::byte{0x34}, std::byte{0x12}, std::byte{0}};
    memory.copy(kHciCommand, unsupported_hci_command);
    memory.write_u32(
        kControlVectors + 52,
        static_cast<std::uint32_t>(unsupported_hci_command.size()));
    memory.write_u32(kControlRequest, 7);
    memory.write_u32(kControlRequest + 8, bluetooth_handle);
    memory.write_u32(kControlRequest + 0x0C, 0);
    memory.write_u32(kControlRequest + 0x10, 6);
    memory.write_u32(kControlRequest + 0x14, 1);
    memory.write_u32(kControlRequest + 0x18, kControlVectors);
    passed &= expect_runtime_error(
        [&] { submit_ios_request(guest_memory, kControlRequest); },
        "unsupported Bluetooth HCI opcodes must hard-fail");

    galaxy::guest_store_u32(
        guest_memory, 0xCD00680C, 1, nullptr, 0x80004000);
    passed &= expect(
        (galaxy::guest_load_u32(
             guest_memory, 0x0D00680C, nullptr, 0x80004000) &
         1u) == 0,
        "EXI transfer start clears when the immediate transfer completes");
    passed &= expect(
        (galaxy::guest_load_u32(
             guest_memory, 0xCD006800, nullptr, 0x80004000) &
         (1u << 3)) != 0,
        "EXI transfer completion sets TCINT");
    galaxy::guest_store_u32(
        guest_memory, 0xCD006800, 1u << 3, nullptr, 0x80004000);
    const std::uint32_t exi_status =
        galaxy::guest_load_u32(
            guest_memory, 0xCD006800, nullptr, 0x80004000);
    passed &= expect((exi_status & (1u << 3)) == 0, "EXI TCINT is write-one-to-clear");
    passed &= expect(
        (exi_status & (1u << 11)) != 0,
        "EXI hardware status survives software status writes");

    galaxy::guest_store_u16(
        guest_memory, 0xCC00500A, 0x08AD, nullptr, 0x80004000);
    const std::uint16_t dsp_control =
        galaxy::guest_load_u16(
            guest_memory, 0x0C00500A, nullptr, 0x80004000);
    passed &= expect(
        dsp_control == 0x0804,
        "DSP reset completes while CPU writes cannot set hardware interrupt status");
    galaxy::guest_store_u16(
        guest_memory, 0xCC00500A, 0x08AE, nullptr, 0x80004000);
    passed &= expect(
        galaxy::guest_load_u16(
            guest_memory, 0xCC00500A, nullptr, 0x80004000) == 0x0804,
        "DSP PIINT is a write strobe and does not stay latched in DSPCR");
    galaxy::guest_store_u16(
        guest_memory, 0xCC00500A, 0x08A8, nullptr, 0x80004000);
    passed &= expect(
        galaxy::guest_load_u16(
            guest_memory, 0xCC005004, nullptr, 0x80004000) == 0x8000,
        "starting the DSP publishes its bootstrap mailbox");
    static_cast<void>(
        galaxy::guest_load_u16(
            guest_memory, 0xCC005006, nullptr, 0x80004000));
    passed &= expect(
        galaxy::guest_load_u16(
            guest_memory, 0xCC005004, nullptr, 0x80004000) == 0,
        "reading the DSP low mailbox consumes the ready message");

    // Broadway ARAM DMA is a timed native device transaction.  The exact
    // 32-bit write sequence below is the one used by RMGE01's ARInit: high
    // half first, low count half last (which triggers the transfer).
    constexpr std::uint32_t kAramMramAddress = 0x01000000u;
    constexpr std::uint32_t kAramOffset = 0x00000100u;
    std::array<std::byte, 64> aram_dma_pattern{};
    for (std::size_t i = 0; i < aram_dma_pattern.size(); ++i) {
        aram_dma_pattern[i] = static_cast<std::byte>(0x40u + i);
    }
    memory.copy(kAramMramAddress, aram_dma_pattern);
    memory.clear(0x10000000u + kAramOffset, 64u);

    galaxy::guest_store_u32(
        guest_memory, 0xCC005020u, 0xFFFFFFFFu, nullptr, 0x80004000u);
    passed &= expect(
        galaxy::guest_load_u32(
            guest_memory, 0x0C005020u, nullptr, 0x80004000u) == 0x03FFFFE0u,
        "ARAM DMA address register applies its documented high/low writable masks");

    const std::uint64_t aram_dma_start_ticks = fake_ticks;
    galaxy::guest_store_u32(
        guest_memory, 0xCC005020u, kAramMramAddress, nullptr, 0x80004000u);
    // Exercise the equivalent 16-bit ARADDR programming path.
    galaxy::guest_store_u16(
        guest_memory, 0xCC005024u,
        static_cast<std::uint16_t>(kAramOffset >> 16u),
        nullptr, 0x80004000u);
    galaxy::guest_store_u16(
        guest_memory, 0xCC005026u,
        static_cast<std::uint16_t>(kAramOffset),
        nullptr, 0x80004000u);
    galaxy::guest_store_u32(
        guest_memory, 0xCC005028u, 64u, nullptr, 0x80004000u);
    const std::uint64_t first_aram_block_ticks =
        memory.aram_dma_next_block_ticks();
    passed &= expect(
        memory.aram_dma_active() &&
            first_aram_block_ticks == aram_dma_start_ticks + 21u,
        "ARAM DMA starts busy with the measured 246-core-clock block deadline");
    std::uint16_t aram_control = galaxy::guest_load_u16(
        guest_memory, 0xCC00500Au, nullptr, 0x80004000u);
    passed &= expect(
        (aram_control & 0x0200u) != 0u &&
            (aram_control & 0x0020u) == 0u &&
            std::all_of(
                memory.pointer(0x10000000u + kAramOffset, 64u),
                memory.pointer(0x10000000u + kAramOffset, 64u) + 64u,
                [](std::byte value) { return value == std::byte{0}; }),
        "ARAM DMA leaves destination bytes and ARINT untouched before its first deadline");

    fake_ticks = first_aram_block_ticks - 1u;
    aram_control = galaxy::guest_load_u16(
        guest_memory, 0xCC00500Au, nullptr, 0x80004000u);
    passed &= expect(
        (aram_control & 0x0200u) != 0u &&
            std::memcmp(
                memory.pointer(0x10000000u + kAramOffset, 64u),
                std::array<std::byte, 64>{}.data(),
                64u) == 0,
        "ARAM DMA cannot publish a block one timeline tick early");

    fake_ticks = first_aram_block_ticks;
    const auto aram_before_pi_cause =
        galaxy::host::NativeDspBoundaryTestAccess::aram_dma_transaction_state(
            memory);
    const std::uint32_t pi_cause_at_aram_deadline = galaxy::guest_load_u32(
        guest_memory, 0xCC003000u, nullptr, 0x80004000u);
    const auto aram_after_pi_cause =
        galaxy::host::NativeDspBoundaryTestAccess::aram_dma_transaction_state(
            memory);
    passed &= expect(
        (pi_cause_at_aram_deadline & 0x00000040u) == 0u &&
            aram_after_pi_cause == aram_before_pi_cause &&
            std::memcmp(
                memory.pointer(0x10000000u + kAramOffset, 64u),
                std::array<std::byte, 64>{}.data(),
                64u) == 0,
        "PI cause observation does not service or advance a due ARAM DMA block");
    aram_control = galaxy::guest_load_u16(
        guest_memory, 0xCC00500Au, nullptr, 0x80004000u);
    passed &= expect(
        memory.aram_dma_active() && (aram_control & 0x0200u) != 0u &&
            (aram_control & 0x0020u) == 0u &&
            std::memcmp(
                memory.pointer(0x10000000u + kAramOffset, 32u),
                aram_dma_pattern.data(),
                32u) == 0 &&
            std::all_of(
                memory.pointer(0x10000000u + kAramOffset + 32u, 32u),
                memory.pointer(0x10000000u + kAramOffset + 64u, 1u),
                [](std::byte value) { return value == std::byte{0}; }) &&
            galaxy::guest_load_u32(
                guest_memory, 0xCC005020u, nullptr, 0x80004000u) ==
                kAramMramAddress + 32u &&
            galaxy::guest_load_u32(
                guest_memory, 0xCC005024u, nullptr, 0x80004000u) ==
                kAramOffset + 32u &&
            galaxy::guest_load_u32(
                guest_memory, 0xCC005028u, nullptr, 0x80004000u) == 32u,
        "ARAM DMA publishes exactly one due 32-byte block and advances its live registers");
    const std::uint64_t second_aram_block_ticks =
        memory.aram_dma_next_block_ticks();
    passed &= expect(
        second_aram_block_ticks == aram_dma_start_ticks + 41u,
        "ARAM DMA second deadline remains anchored to the immutable transfer origin");

    fake_ticks = second_aram_block_ticks;
    aram_control = galaxy::guest_load_u16(
        guest_memory, 0xCC00500Au, nullptr, 0x80004000u);
    passed &= expect(
        !memory.aram_dma_active() &&
            memory.aram_dma_next_block_ticks() == 0u &&
            (aram_control & 0x0200u) == 0u &&
            (aram_control & 0x0020u) != 0u &&
            std::memcmp(
                memory.pointer(0x10000000u + kAramOffset, 64u),
                aram_dma_pattern.data(),
                aram_dma_pattern.size()) == 0 &&
            galaxy::guest_load_u32(
                guest_memory, 0xCC005028u, nullptr, 0x80004000u) == 0u,
        "ARAM DMA completion copies all bytes, clears busy, latches ARINT, and drains count");

    // ARINT is a masked level, not a synthetic mailbox edge.
    galaxy::guest_store_u16(
        guest_memory, 0xCC00500Au, 0x0040u, nullptr, 0x80004000u);
    passed &= expect(
        memory.aram_dma_interrupt_status() &&
            memory.aram_dma_interrupt_pending() &&
            (galaxy::guest_load_u32(
                 guest_memory, 0x0C003000u, nullptr, 0x80004000u) &
             0x00000040u) != 0u,
        "enabling ARINTMSK asserts the latched ARAM level through PI cause");
    galaxy::guest_store_u16(
        guest_memory, 0xCC00500Au, 0x0060u, nullptr, 0x80004000u);
    passed &= expect(
        !memory.aram_dma_interrupt_status() &&
            !memory.aram_dma_interrupt_pending() &&
            (galaxy::guest_load_u16(
                 guest_memory, 0xCC00500Au, nullptr, 0x80004000u) &
             0x0040u) != 0u &&
            (galaxy::guest_load_u32(
                 guest_memory, 0xCC003000u, nullptr, 0x80004000u) &
             0x00000040u) == 0u,
        "DSPCR W1C acknowledges ARINT, preserves ARINTMSK, and deasserts PI");

    constexpr std::uint32_t kAramReverseMram = 0x01000200u;
    constexpr std::uint32_t kAramReverseOffset = 0x00000400u;
    std::array<std::byte, 32> aram_reverse_pattern{};
    for (std::size_t i = 0; i < aram_reverse_pattern.size(); ++i) {
        aram_reverse_pattern[i] = static_cast<std::byte>(0xE0u - i);
    }
    memory.copy(0x10000000u + kAramReverseOffset, aram_reverse_pattern);
    memory.clear(kAramReverseMram, 32u);
    galaxy::guest_store_u32(
        guest_memory, 0xCC005020u, kAramReverseMram, nullptr, 0x80004000u);
    galaxy::guest_store_u32(
        guest_memory, 0xCC005024u, kAramReverseOffset, nullptr, 0x80004000u);
    galaxy::guest_store_u32(
        guest_memory, 0xCC005028u, 0x80000020u, nullptr, 0x80004000u);
    fake_ticks = memory.aram_dma_next_block_ticks();
    memory.service_aram_dma(fake_ticks);
    passed &= expect(
        std::memcmp(
            memory.pointer(kAramReverseMram, 32u),
            aram_reverse_pattern.data(),
            aram_reverse_pattern.size()) == 0 &&
            (galaxy::guest_load_u32(
                 guest_memory, 0xCC005028u, nullptr, 0x80004000u) &
             0x80000000u) != 0u,
        "reverse ARAM DMA copies raw Wii bytes from MEM2 to MEM1 and preserves direction");
    galaxy::guest_store_u16(
        guest_memory, 0xCC00500Au, 0x0060u, nullptr, 0x80004000u);

    // A late service observes every block that is due from the immutable
    // transaction origin. Register writes, including a second start trigger,
    // must remain atomic while the original transfer is busy.
    constexpr std::uint32_t kAramCatchupMram = 0x01000400u;
    constexpr std::uint32_t kAramCatchupOffset = 0x00000800u;
    std::array<std::byte, 96> aram_catchup_pattern{};
    for (std::size_t i = 0; i < aram_catchup_pattern.size(); ++i) {
        aram_catchup_pattern[i] = static_cast<std::byte>(0x20u + i);
    }
    memory.copy(kAramCatchupMram, aram_catchup_pattern);
    memory.clear(0x10000000u + kAramCatchupOffset, 96u);
    galaxy::guest_store_u32(
        guest_memory, 0xCC005020u, kAramCatchupMram, nullptr, 0x80004000u);
    galaxy::guest_store_u32(
        guest_memory, 0xCC005024u, kAramCatchupOffset, nullptr, 0x80004000u);
    const std::uint64_t aram_catchup_start_ticks = fake_ticks;
    galaxy::guest_store_u32(
        guest_memory, 0xCC005028u, 96u, nullptr, 0x80004000u);
    const auto active_aram_transaction =
        galaxy::host::NativeDspBoundaryTestAccess::aram_dma_transaction_state(
            memory);
    passed &= expect_runtime_error_message(
        [&] {
            galaxy::guest_store_u16(
                guest_memory, 0xCC005020u, 0u, nullptr, 0x80004000u);
        },
        "ARAM DMA register write attempted while a transfer is active",
        "ARAM DMA rejects address-register writes while a transfer is active");
    passed &= expect_runtime_error_message(
        [&] {
            galaxy::guest_store_u16(
                guest_memory, 0xCC00502Au, 96u, nullptr, 0x80004000u);
        },
        "ARAM DMA register write attempted while a transfer is active",
        "ARAM DMA rejects a second start trigger while a transfer is active");
    passed &= expect(
        galaxy::host::NativeDspBoundaryTestAccess::aram_dma_transaction_state(
            memory) == active_aram_transaction &&
            std::all_of(
                memory.pointer(0x10000000u + kAramCatchupOffset, 96u),
                memory.pointer(0x10000000u + kAramCatchupOffset + 96u, 1u),
                [](std::byte value) { return value == std::byte{0}; }),
        "rejected active ARAM DMA writes leave the transaction and destination unchanged");

    fake_ticks = aram_catchup_start_ticks + 41u;
    memory.service_aram_dma(fake_ticks);
    const auto caught_up_aram_transaction =
        galaxy::host::NativeDspBoundaryTestAccess::aram_dma_transaction_state(
            memory);
    passed &= expect(
        caught_up_aram_transaction.active &&
            caught_up_aram_transaction.completed_blocks == 2u &&
            caught_up_aram_transaction.next_block_ticks ==
                aram_catchup_start_ticks + 62u &&
            std::memcmp(
                memory.pointer(0x10000000u + kAramCatchupOffset, 64u),
                aram_catchup_pattern.data(), 64u) == 0 &&
            std::all_of(
                memory.pointer(0x10000000u + kAramCatchupOffset + 64u, 32u),
                memory.pointer(0x10000000u + kAramCatchupOffset + 96u, 1u),
                [](std::byte value) { return value == std::byte{0}; }) &&
            galaxy::guest_load_u32(
                guest_memory, 0xCC005028u, nullptr, 0x80004000u) == 32u,
        "late ARAM DMA service catches up every due block without rephasing the next deadline");
    fake_ticks = aram_catchup_start_ticks + 100u;
    memory.service_aram_dma(fake_ticks);
    passed &= expect(
        !memory.aram_dma_active() &&
            std::memcmp(
                memory.pointer(0x10000000u + kAramCatchupOffset, 96u),
                aram_catchup_pattern.data(), aram_catchup_pattern.size()) == 0,
        "late ARAM DMA completion drains the remaining block exactly once");
    galaxy::guest_store_u16(
        guest_memory, 0xCC00500Au, 0x0060u, nullptr, 0x80004000u);

    // ARADDR zero names raw Wii MEM2 offset zero. It must not alias the
    // separately pinned cached VARAM backing used by the native DSP service.
    constexpr std::uint32_t kRawAramMram = 0x01000600u;
    constexpr std::uint32_t kRawAramAddress = 0x10000000u;
    constexpr std::uint32_t kPinnedDspAramAddress = 0x90000800u;
    std::array<std::byte, 32> raw_aram_source{};
    std::array<std::byte, 32> saved_raw_aram{};
    std::array<std::byte, 32> saved_pinned_dsp_aram{};
    std::array<std::byte, 32> pinned_dsp_aram_sentinel{};
    for (std::size_t i = 0; i < raw_aram_source.size(); ++i) {
        raw_aram_source[i] = static_cast<std::byte>(0x90u + i);
        pinned_dsp_aram_sentinel[i] = static_cast<std::byte>(0xD0u + i);
    }
    std::memcpy(
        saved_raw_aram.data(), memory.pointer(kRawAramAddress, 32u), 32u);
    std::memcpy(
        saved_pinned_dsp_aram.data(),
        memory.pointer(kPinnedDspAramAddress, 32u), 32u);
    memory.copy(kRawAramMram, raw_aram_source);
    memory.clear(kRawAramAddress, 32u);
    memory.copy(kPinnedDspAramAddress, pinned_dsp_aram_sentinel);
    galaxy::guest_store_u32(
        guest_memory, 0xCC005020u, kRawAramMram, nullptr, 0x80004000u);
    galaxy::guest_store_u32(
        guest_memory, 0xCC005024u, 0u, nullptr, 0x80004000u);
    galaxy::guest_store_u32(
        guest_memory, 0xCC005028u, 32u, nullptr, 0x80004000u);
    fake_ticks = memory.aram_dma_next_block_ticks();
    memory.service_aram_dma(fake_ticks);
    passed &= expect(
        std::memcmp(
            memory.pointer(kRawAramAddress, 32u),
            raw_aram_source.data(), raw_aram_source.size()) == 0 &&
            std::memcmp(
                memory.pointer(kPinnedDspAramAddress, 32u),
                pinned_dsp_aram_sentinel.data(),
                pinned_dsp_aram_sentinel.size()) == 0,
        "ARAM DMA ARADDR zero remains distinct from the cached VARAM backing consumed only through the worker mirror");
    galaxy::guest_store_u16(
        guest_memory, 0xCC00500Au, 0x0060u, nullptr, 0x80004000u);
    memory.copy(kRawAramAddress, saved_raw_aram);
    memory.copy(kPinnedDspAramAddress, saved_pinned_dsp_aram);

    galaxy::guest_store_u32(
        guest_memory, 0xCC005020u,
        galaxy::host::GuestAddressSpace::kMem1Size - 32u,
        nullptr, 0x80004000u);
    galaxy::guest_store_u32(
        guest_memory, 0xCC005024u, 0u, nullptr, 0x80004000u);
    const auto aram_before_invalid_mem1 =
        galaxy::host::NativeDspBoundaryTestAccess::aram_dma_transaction_state(
            memory);
    passed &= expect_runtime_error_message(
        [&] {
            galaxy::guest_store_u32(
                guest_memory, 0xCC005028u, 64u, nullptr, 0x80004000u);
        },
        "ARAM DMA MRAM span is outside MEM1",
        "ARAM DMA hard-fails a transfer that crosses the physical MEM1 boundary");
    passed &= expect(
        galaxy::host::NativeDspBoundaryTestAccess::aram_dma_transaction_state(
            memory) == aram_before_invalid_mem1,
        "rejected MEM1 ARAM DMA start leaves every internal transaction field unchanged");
    const auto aram_before_zero_length =
        galaxy::host::NativeDspBoundaryTestAccess::aram_dma_transaction_state(
            memory);
    passed &= expect_runtime_error_message(
        [&] {
            galaxy::guest_store_u32(
                guest_memory, 0xCC005028u, 0u, nullptr, 0x80004000u);
        },
        "ARAM DMA length must be a nonzero multiple of 32 bytes",
        "ARAM DMA hard-fails a zero-length transaction instead of inventing completion");
    passed &= expect(
        galaxy::host::NativeDspBoundaryTestAccess::aram_dma_transaction_state(
            memory) == aram_before_zero_length,
        "rejected zero-length ARAM DMA start leaves every internal transaction field unchanged");

    galaxy::guest_store_u32(
        guest_memory, 0xCC005020u, kAramMramAddress, nullptr, 0x80004000u);
    galaxy::guest_store_u32(
        guest_memory, 0xCC005024u,
        galaxy::host::GuestAddressSpace::kMem2Size - 32u,
        nullptr, 0x80004000u);
    const auto aram_before_invalid_mem2 =
        galaxy::host::NativeDspBoundaryTestAccess::aram_dma_transaction_state(
            memory);
    passed &= expect_runtime_error_message(
        [&] {
            galaxy::guest_store_u32(
                guest_memory, 0xCC005028u, 64u, nullptr, 0x80004000u);
        },
        "ARAM DMA ARAM span is outside Wii MEM2",
        "ARAM DMA hard-fails a transfer that crosses the physical MEM2 boundary");
    passed &= expect(
        galaxy::host::NativeDspBoundaryTestAccess::aram_dma_transaction_state(
            memory) == aram_before_invalid_mem2,
        "rejected MEM2 ARAM DMA start leaves every internal transaction field unchanged");

    constexpr std::uint32_t kDspChannelTable = 0x807B0000;
    constexpr std::uint32_t kDspSource = 0x807C0000;
    constexpr std::uint32_t kDspLeft = 0x807D0000;
    constexpr std::uint32_t kDspRight = 0x807D0200;
    std::vector<std::byte> dsp_channels(0x180u * 64u);
    std::vector<std::byte> dsp_source(9u * 10u);
    std::vector<std::byte> dsp_silence(0x50u * sizeof(std::uint16_t));
    for (std::uint32_t block = 0; block < 10; ++block) {
        dsp_source[block * 9u] = std::byte{0x01};
    }
    write_be16(dsp_channels, 0x000, 1);
    write_be16(dsp_channels, 0x004, 0x1000);
    write_be16(dsp_channels, 0x010, 0x0D00);
    write_be16(dsp_channels, 0x012, 0x4000);
    write_be16(dsp_channels, 0x014, 0x4000);
    write_be16(dsp_channels, 0x064, 16);
    write_be32(dsp_channels, 0x068, 32);
    write_be16(dsp_channels, 0x100, 9);
    write_be16(dsp_channels, 0x104, 0x4000);
    write_be16(dsp_channels, 0x106, 0x0000);
    write_be32(dsp_channels, 0x110, 0);
    write_be32(dsp_channels, 0x114, 160);
    write_be32(dsp_channels, 0x118, kDspSource);
    write_be32(dsp_channels, 0x11C, 160);
    memory.copy(kDspChannelTable, dsp_channels);
    memory.copy(kDspSource, dsp_source);
    memory.copy(kDspLeft, dsp_silence);
    memory.copy(kDspRight, dsp_silence);
    send_dsp_mail(guest_memory, 0x80F3D001);
    send_dsp_mail(guest_memory, 0);
    send_dsp_mail(guest_memory, 2);
    send_dsp_mail(guest_memory, 0x81000000);
    send_dsp_mail(guest_memory, kDspChannelTable);
    drain_dsp_mailbox(guest_memory);
    send_dsp_mail(guest_memory, 3);
    send_dsp_mail(guest_memory, 0x82014000);
    send_dsp_mail(guest_memory, kDspLeft);
    send_dsp_mail(guest_memory, kDspRight);
    passed &= expect(
        galaxy::guest_load_u32(
            guest_memory, kDspChannelTable + 0x068, nullptr,
            0x80004000) == 32,
        "DsyncFrame waits for DSPReleaseHalt2 before rendering a subframe");
    release_dsp_sync_subframe(guest_memory);
    passed &= expect(
        galaxy::guest_load_u32(
            guest_memory, kDspChannelTable + 0x068, nullptr,
            0x80004000) != 32,
        "DSPReleaseHalt2 renders the pending DSP subframe");
    const std::uint16_t first_left =
        galaxy::guest_load_u16(
            guest_memory, kDspLeft, nullptr, 0x80004000);
    const std::uint16_t first_right =
        galaxy::guest_load_u16(
            guest_memory, kDspRight, nullptr, 0x80004000);
    passed &= expect(
        first_left == 0 && first_right == first_left,
        "non-looped ADPCM ignores stale loop predictor fields");
    const galaxy::host::DspAudioStats first_dsp_stats =
        memory.dsp_audio_stats();
    passed &= expect(
        first_dsp_stats.render_batches >= 1 &&
            first_dsp_stats.rendered_channel_visits >= 1 &&
            first_dsp_stats.adpcm_channel_visits >= 1,
        "DSP audio stats record rendered ADPCM channel activity");
    const std::uint16_t history_after =
        galaxy::guest_load_u16(
            guest_memory,
            kDspChannelTable + 0x104,
            nullptr,
            0x80004000);
    passed &= expect(
        history_after == 0x4000,
        "ADPCM render preserves the channel loop predictor field");
    drain_dsp_mailbox(guest_memory);

    constexpr std::uint32_t kDspFullPredictorSource = 0x807C0200;
    std::vector<std::byte> full_predictor_channels(0x180u * 64u);
    std::vector<std::byte> full_predictor_source(18u);
    std::vector<std::byte> full_predictor_silence(
        0x50u * sizeof(std::uint16_t));
    full_predictor_source[9u] = std::byte{0x1E};
    full_predictor_source[10u] = std::byte{0x80};
    write_be16(full_predictor_channels, 0x000, 1);
    write_be16(full_predictor_channels, 0x004, 0x1000);
    write_be16(full_predictor_channels, 0x010, 0x0D00);
    write_be16(full_predictor_channels, 0x012, 0x4000);
    write_be16(full_predictor_channels, 0x014, 0x4000);
    write_be16(full_predictor_channels, 0x064, 16);
    write_be32(full_predictor_channels, 0x068, 16);
    write_be32(full_predictor_channels, 0x070, kDspFullPredictorSource + 9u);
    write_be16(full_predictor_channels, 0x100, 9);
    write_be16(full_predictor_channels, 0x102, 1);
    write_be16(full_predictor_channels, 0x104, 0x1000);
    write_be16(full_predictor_channels, 0x106, 0x0000);
    write_be32(full_predictor_channels, 0x110, 16);
    write_be32(full_predictor_channels, 0x114, 32);
    write_be32(full_predictor_channels, 0x118, kDspFullPredictorSource);
    write_be32(full_predictor_channels, 0x11C, 32);
    memory.copy(kDspChannelTable, full_predictor_channels);
    memory.copy(kDspFullPredictorSource, full_predictor_source);
    memory.copy(kDspLeft, full_predictor_silence);
    memory.copy(kDspRight, full_predictor_silence);
    const galaxy::host::DspAudioStats before_full_predictor_stats =
        memory.dsp_audio_stats();
    render_dsp_sync_2ch(
        guest_memory, 0x82014000, kDspLeft, kDspRight);
    passed &= expect(
        load_guest_s16(guest_memory, kDspLeft) == -1032 &&
            load_guest_s16(guest_memory, kDspRight) == 0,
        "AFC headers use high-nibble scale and full low-nibble predictor index");
    const galaxy::host::DspAudioStats after_full_predictor_stats =
        memory.dsp_audio_stats();
    passed &= expect(
        after_full_predictor_stats.effects_voice_sample_visits >
                before_full_predictor_stats.effects_voice_sample_visits &&
            after_full_predictor_stats.max_effects_voice_peak > 0,
        "DSP audio provenance records ADPCM effects/voice samples and peak");
    passed &= expect(
        after_full_predictor_stats.sfx_sample_visits >
                before_full_predictor_stats.sfx_sample_visits &&
            after_full_predictor_stats.max_sfx_peak > 0,
        "DSP audio provenance classifies ADPCM fallback samples as SFX");
    drain_dsp_mailbox(guest_memory);

    constexpr std::uint32_t kJasDspControlPoolPtr = 0x806A2BD0u;
    constexpr std::uint32_t kJasDspControlPool = 0x807A0000u;
    constexpr std::uint32_t kJasVoiceDspChannel = 0x807A0E00u;
    constexpr std::uint32_t kJasVoiceChannel = 0x807A1000u;
    constexpr std::uint32_t kJasVoiceCallbackData = 0x807A1200u;
    constexpr std::uint32_t kJasVoiceTrack = 0x807A1400u;
    constexpr std::uint32_t kDspVoiceLaneSource = 0x807C0180u;
    std::vector<std::byte> voice_control_pool(0x1Cu * 64u);
    std::vector<std::byte> voice_lane_channels(0x180u * 64u);
    std::vector<std::byte> voice_lane_source(16u * sizeof(std::uint16_t));
    std::vector<std::byte> voice_lane_silence(
        0x50u * sizeof(std::uint16_t));
    for (std::uint32_t sample = 0; sample < 16u; ++sample) {
        write_be16(
            voice_lane_source,
            sample * sizeof(std::uint16_t),
            0x4000);
    }
    write_be32(voice_control_pool, 0x14u, kJasVoiceDspChannel);
    memory.write_u32(kJasDspControlPoolPtr, kJasDspControlPool);
    memory.copy(kJasDspControlPool, voice_control_pool);
    memory.write_u32(kJasVoiceDspChannel + 0x14u, kJasVoiceChannel);
    memory.write_u32(kJasVoiceChannel + 0x10u, kJasVoiceCallbackData);
    memory.write_u32(kJasVoiceCallbackData + 0x4Cu, kJasVoiceTrack);
    memory.write_u32(kJasVoiceTrack - 0xA0u, 0x00010000u);
    memory.write_u32(kJasVoiceTrack - 0x90u, 0xFFFFFFFFu);
    write_be16(voice_lane_channels, 0x000, 1);
    write_be16(voice_lane_channels, 0x004, 0x1000);
    write_be16(voice_lane_channels, 0x010, 0x0D00);
    write_be16(voice_lane_channels, 0x012, 0x7FFF);
    write_be16(voice_lane_channels, 0x014, 0x7FFF);
    write_be16(voice_lane_channels, 0x064, 1);
    write_be16(voice_lane_channels, 0x100, 16);
    write_be32(voice_lane_channels, 0x110, 0);
    write_be32(voice_lane_channels, 0x114, 16);
    write_be32(voice_lane_channels, 0x118, kDspVoiceLaneSource);
    write_be32(voice_lane_channels, 0x11C, 16);
    memory.copy(kDspChannelTable, voice_lane_channels);
    memory.copy(kDspVoiceLaneSource, voice_lane_source);
    memory.copy(kDspLeft, voice_lane_silence);
    memory.copy(kDspRight, voice_lane_silence);
    const galaxy::RuntimeSettings saved_runtime_settings =
        galaxy::get_runtime_settings();
    galaxy::RuntimeSettings muted_voice_settings = saved_runtime_settings;
    muted_voice_settings.audio.voice = 0.0f;
    galaxy::set_runtime_settings(muted_voice_settings);
    const galaxy::host::DspAudioStats before_voice_lane_stats =
        memory.dsp_audio_stats();
    render_dsp_sync_2ch(
        guest_memory, 0x82014000, kDspLeft, kDspRight);
    const galaxy::host::DspAudioStats after_voice_lane_stats =
        memory.dsp_audio_stats();
    passed &= expect(
        after_voice_lane_stats.voice_sample_visits >
                before_voice_lane_stats.voice_sample_visits &&
            after_voice_lane_stats.max_voice_peak > 0 &&
            after_voice_lane_stats.max_voice_lane_peak == 0,
        "DSP audio provenance classifies JAudio voice owners and applies voice gain");
    passed &= expect(
        after_voice_lane_stats.sfx_sample_visits ==
                before_voice_lane_stats.sfx_sample_visits &&
            load_guest_s16(guest_memory, kDspLeft) == 0 &&
            load_guest_s16(guest_memory, kDspRight) == 0,
        "voice gain mutes only the voice-owned lane instead of routing it as SFX");
    galaxy::set_runtime_settings(saved_runtime_settings);
    memory.write_u32(kJasDspControlPoolPtr, 0);
    drain_dsp_mailbox(guest_memory);

    constexpr std::uint32_t kArBackingBaseGlobal = 0x806A2C54u;
    constexpr std::uint32_t kArSizeGlobal = 0x806A2C5Cu;
    constexpr std::uint32_t kArLogicalBase = 0x00004000u;
    constexpr std::uint32_t kArBackingBase = 0x90000800u;
    std::vector<std::byte> aram_channel(0x180u * 64u);
    std::vector<std::byte> aram_source(9u);
    std::vector<std::byte> aram_silence(0x50u * sizeof(std::uint16_t));
    aram_source[0] = std::byte{0xC0};
    for (std::uint32_t byte = 1; byte < 9u; ++byte) {
        aram_source[byte] = std::byte{0x11};
    }
    memory.write_u32(kArBackingBaseGlobal, kArBackingBase);
    memory.write_u32(kArSizeGlobal, 0x20u);
    memory.copy(kArBackingBase, std::vector<std::byte>(9u));
    memory.copy(kArBackingBase + kArLogicalBase, aram_source);
    write_be16(aram_channel, 0x000, 1);
    write_be16(aram_channel, 0x004, 0x1000);
    write_be16(aram_channel, 0x010, 0x0D00);
    write_be16(aram_channel, 0x012, 0x7FFF);
    write_be16(aram_channel, 0x014, 0x7FFF);
    write_be16(aram_channel, 0x064, 16);
    write_be32(aram_channel, 0x070, kArLogicalBase);
    write_be16(aram_channel, 0x100, 9);
    write_be32(aram_channel, 0x110, 0);
    write_be32(aram_channel, 0x114, 16);
    write_be32(aram_channel, 0x118, kArLogicalBase);
    write_be32(aram_channel, 0x11C, 16);
    memory.copy(kDspChannelTable, aram_channel);
    memory.copy(kDspLeft, aram_silence);
    memory.copy(kDspRight, aram_silence);
    render_dsp_sync_2ch(
        guest_memory, 0x82014000, kDspLeft, kDspRight);
    passed &= expect(
        load_guest_s16(guest_memory, kDspLeft) > 512 &&
            load_guest_s16(guest_memory, kDspRight) == 0,
        "DSP ARAM logical source base maps to the matching native AR address");
    drain_dsp_mailbox(guest_memory);

    constexpr std::uint32_t kArHighLogicalSource = 0x00D70AC0u;
    const std::uint32_t ar_high_backing =
        kArBackingBase + kArHighLogicalSource;
    std::vector<std::byte> aram_high_channel(0x180u * 64u);
    std::vector<std::byte> aram_high_source(9u);
    std::vector<std::byte> aram_high_silence(
        0x50u * sizeof(std::uint16_t));
    aram_high_source[0] = std::byte{0xC0};
    for (std::uint32_t byte = 1; byte < 9u; ++byte) {
        aram_high_source[byte] = std::byte{0x11};
    }
    memory.copy(ar_high_backing, aram_high_source);
    write_be16(aram_high_channel, 0x000, 1);
    write_be16(aram_high_channel, 0x004, 0x1000);
    write_be16(aram_high_channel, 0x010, 0x0D00);
    write_be16(aram_high_channel, 0x012, 0x7FFF);
    write_be16(aram_high_channel, 0x014, 0x7FFF);
    write_be16(aram_high_channel, 0x064, 16);
    write_be16(aram_high_channel, 0x100, 9);
    write_be32(aram_high_channel, 0x110, 0);
    write_be32(aram_high_channel, 0x114, 16);
    write_be32(aram_high_channel, 0x118, kArHighLogicalSource);
    write_be32(aram_high_channel, 0x11C, 16);
    memory.copy(kDspChannelTable, aram_high_channel);
    memory.copy(kDspLeft, aram_high_silence);
    memory.copy(kDspRight, aram_high_silence);
    render_dsp_sync_2ch(
        guest_memory, 0x82014000, kDspLeft, kDspRight);
    passed &= expect(
        load_guest_s16(guest_memory, kDspLeft) > 512 &&
            load_guest_s16(guest_memory, kDspRight) == 0,
        "DSP ARAM high logical sources use raw RMGE01 dummy ARAM addresses");
    drain_dsp_mailbox(guest_memory);

    memory.copy(kDspChannelTable, dsp_channels);
    memory.copy(kDspLeft, dsp_silence);
    memory.copy(kDspRight, dsp_silence);
    const std::uint32_t position_before_deferred_release =
        galaxy::guest_load_u32(
            guest_memory,
            kDspChannelTable + 0x068,
            nullptr,
            0x80004000);
    send_dsp_mail(guest_memory, 2);
    send_dsp_mail(guest_memory, 0x81000000);
    send_dsp_mail(guest_memory, kDspChannelTable);
    send_dsp_mail(guest_memory, 3);
    send_dsp_mail(guest_memory, 0x82014000);
    send_dsp_mail(guest_memory, kDspLeft);
    send_dsp_mail(guest_memory, kDspRight);
    release_dsp_sync_subframe(guest_memory);
    passed &= expect(
        galaxy::guest_load_u32(
            guest_memory,
            kDspChannelTable + 0x068,
            nullptr,
            0x80004000) == position_before_deferred_release,
        "DSPReleaseHalt2 defers rendering while earlier DSP replies are queued");
    drain_dsp_mailbox(guest_memory);
    passed &= expect(
        galaxy::guest_load_u32(
            guest_memory,
            kDspChannelTable + 0x068,
            nullptr,
            0x80004000) != position_before_deferred_release,
        "queued DSP reply drain resumes the deferred sync subframe");

    constexpr std::uint32_t kDspForcedStopSource = 0x807C0400;
    std::vector<std::byte> forced_stop_channels(0x180u * 64u);
    std::vector<std::byte> forced_stop_source(16u * sizeof(std::uint16_t));
    std::vector<std::byte> forced_stop_silence(
        0x50u * sizeof(std::uint16_t));
    for (std::uint32_t sample = 0; sample < 16u; ++sample) {
        write_be16(
            forced_stop_source,
            sample * sizeof(std::uint16_t),
            static_cast<std::uint16_t>(0x2000u + sample * 0x0100u));
    }
    write_be16(forced_stop_channels, 0x000, 1);
    write_be16(forced_stop_channels, 0x004, 0x1000);
    write_be16(forced_stop_channels, 0x010, 0x0D00);
    write_be16(forced_stop_channels, 0x012, 0x7FFF);
    write_be16(forced_stop_channels, 0x014, 0x7FFF);
    write_be16(forced_stop_channels, 0x064, 1);
    write_be16(forced_stop_channels, 0x100, 16);
    write_be16(forced_stop_channels, 0x10A, 1);
    write_be32(forced_stop_channels, 0x110, 0);
    write_be32(forced_stop_channels, 0x114, 16);
    write_be32(forced_stop_channels, 0x118, kDspForcedStopSource);
    write_be32(forced_stop_channels, 0x11C, 16);
    memory.copy(kDspChannelTable, forced_stop_channels);
    memory.copy(kDspForcedStopSource, forced_stop_source);
    memory.copy(kDspLeft, forced_stop_silence);
    memory.copy(kDspRight, forced_stop_silence);
    render_dsp_sync_2ch(
        guest_memory, 0x82018000, kDspLeft, kDspRight);
    passed &= expect(
        load_guest_s16(guest_memory, kDspLeft) == 0 &&
            load_guest_s16(guest_memory, kDspRight) == 0 &&
            galaxy::guest_load_u16(
                guest_memory,
                kDspChannelTable + 0x000,
                nullptr,
                0x80004000) == 0 &&
            galaxy::guest_load_u16(
                guest_memory,
                kDspChannelTable + 0x002,
                nullptr,
                0x80004000) == 1 &&
            galaxy::guest_load_u32(
                guest_memory,
                kDspChannelTable + 0x068,
                nullptr,
                0x80004000) == 0,
        "DSP forced-stop flag completes the stopped channel without rendering stale audio");
    drain_dsp_mailbox(guest_memory);

    std::vector<std::byte> loop_channels(0x180u * 64u);
    write_be16(loop_channels, 0x000, 1);
    write_be16(loop_channels, 0x004, 0x1000);
    write_be16(loop_channels, 0x010, 0x0D00);
    write_be16(loop_channels, 0x012, 0x4000);
    write_be16(loop_channels, 0x014, 0x4000);
    write_be16(loop_channels, 0x064, 16);
    write_be32(loop_channels, 0x068, 32);
    write_be32(loop_channels, 0x070, kDspSource + 18u);
    write_be16(loop_channels, 0x100, 9);
    write_be16(loop_channels, 0x102, 1);
    write_be16(loop_channels, 0x104, 0x4000);
    write_be16(loop_channels, 0x106, 0x0000);
    write_be32(loop_channels, 0x110, 16);
    write_be32(loop_channels, 0x114, 32);
    write_be32(loop_channels, 0x118, kDspSource);
    write_be32(loop_channels, 0x11C, 48);
    memory.copy(kDspChannelTable, loop_channels);
    memory.copy(kDspLeft, dsp_silence);
    memory.copy(kDspRight, dsp_silence);
    render_dsp_sync_2ch(
        guest_memory, 0x82014000, kDspLeft, kDspRight);
    passed &= expect(
        galaxy::guest_load_u16(
            guest_memory, kDspLeft, nullptr, 0x80004000) != 0,
        "looped ADPCM applies the loop predictor when wrapping");
    drain_dsp_mailbox(guest_memory);

    constexpr std::uint32_t kDspLoopLookaheadSource = 0x807C0600;
    std::vector<std::byte> loop_lookahead_channels(0x180u * 64u);
    std::vector<std::byte> loop_lookahead_source(9u);
    std::vector<std::byte> loop_lookahead_silence(
        0x50u * sizeof(std::uint16_t));
    loop_lookahead_source[0] = std::byte{0x01};
    write_be16(loop_lookahead_channels, 0x000, 1);
    write_be16(loop_lookahead_channels, 0x004, 0x0800);
    write_be16(loop_lookahead_channels, 0x010, 0x0D00);
    write_be16(loop_lookahead_channels, 0x012, 0x7FFF);
    write_be16(loop_lookahead_channels, 0x014, 0x7FFF);
    write_be16(loop_lookahead_channels, 0x064, 16);
    write_be32(loop_lookahead_channels, 0x068, 15);
    write_be32(loop_lookahead_channels, 0x070, kDspLoopLookaheadSource + 8u);
    write_be16(loop_lookahead_channels, 0x100, 9);
    write_be16(loop_lookahead_channels, 0x102, 1);
    write_be16(loop_lookahead_channels, 0x104, 0x4000);
    write_be16(loop_lookahead_channels, 0x106, 0x0000);
    write_be32(loop_lookahead_channels, 0x110, 5);
    write_be32(loop_lookahead_channels, 0x114, 16);
    write_be32(loop_lookahead_channels, 0x118, kDspLoopLookaheadSource);
    write_be32(loop_lookahead_channels, 0x11C, 16);
    memory.copy(kDspChannelTable, loop_lookahead_channels);
    memory.copy(kDspLoopLookaheadSource, loop_lookahead_source);
    memory.copy(kDspLeft, loop_lookahead_silence);
    memory.copy(kDspRight, loop_lookahead_silence);
    render_dsp_sync_2ch(
        guest_memory, 0x82014000, kDspLeft, kDspRight);
    const std::int16_t second_loop_lookahead_left =
        load_guest_s16(guest_memory, kDspLeft + sizeof(std::uint16_t));
    const std::int16_t second_loop_lookahead_right =
        load_guest_s16(guest_memory, kDspRight + sizeof(std::uint16_t));
    passed &= expect(
        second_loop_lookahead_left > 512 &&
            second_loop_lookahead_right == 0,
        "ADPCM resampler lookahead uses loop history when wrapping mid-block");
    drain_dsp_mailbox(guest_memory);

    constexpr std::uint32_t kDspLoopEntrySource = 0x807C0680;
    constexpr std::uint32_t kDspLoopEntryStart = 741u * 16u;
    constexpr std::uint32_t kDspLoopEntryEnd = 6282u * 16u;
    std::vector<std::byte> loop_entry_channels(0x180u * 64u);
    std::vector<std::byte> loop_entry_source(
        (kDspLoopEntryStart / 16u + 3u) * 9u);
    std::vector<std::byte> loop_entry_silence(0x50u * sizeof(std::uint16_t));
    for (std::uint32_t block = 0; block < kDspLoopEntryStart / 16u + 3u;
         ++block) {
        loop_entry_source[block * 9u] = std::byte{0xC0};
        for (std::uint32_t byte = 1; byte < 9u; ++byte) {
            loop_entry_source[block * 9u + byte] = std::byte{0x11};
        }
    }
    write_be16(loop_entry_channels, 0x000, 1);
    write_be16(loop_entry_channels, 0x004, 0x1000);
    write_be16(loop_entry_channels, 0x010, 0x0D00);
    write_be16(loop_entry_channels, 0x012, 0x7FFF);
    write_be16(loop_entry_channels, 0x014, 0x7FFF);
    write_be16(loop_entry_channels, 0x064, 16);
    write_be32(loop_entry_channels, 0x068, kDspLoopEntryStart - 80u);
    write_be32(
        loop_entry_channels,
        0x070,
        kDspLoopEntrySource + ((kDspLoopEntryStart - 80u) * 9u) / 16u);
    write_be16(loop_entry_channels, 0x100, 9);
    write_be16(loop_entry_channels, 0x102, 1);
    write_be16(loop_entry_channels, 0x104, 0x4000);
    write_be16(loop_entry_channels, 0x106, 0x0000);
    write_be32(loop_entry_channels, 0x110, kDspLoopEntryStart);
    write_be32(loop_entry_channels, 0x114, kDspLoopEntryEnd);
    write_be32(loop_entry_channels, 0x118, kDspLoopEntrySource);
    write_be32(loop_entry_channels, 0x11C, kDspLoopEntryEnd + 16u);
    memory.copy(kDspChannelTable, loop_entry_channels);
    memory.copy(kDspLoopEntrySource, loop_entry_source);
    memory.copy(kDspLeft, loop_entry_silence);
    memory.copy(kDspRight, loop_entry_silence);
    render_dsp_sync_2ch(
        guest_memory, 0x82014000, kDspLeft, kDspRight);
    passed &= expect(
        galaxy::guest_load_u32(
            guest_memory, kDspChannelTable + 0x068, nullptr,
            0x80004000) == kDspLoopEntryStart,
        "ADPCM loop-entry regression setup reaches the nonzero loop start");
    drain_dsp_mailbox(guest_memory);
    memory.copy(kDspLeft, loop_entry_silence);
    memory.copy(kDspRight, loop_entry_silence);
    galaxy::guest_store_u16(
        guest_memory, kDspChannelTable + 0x004, 0x0C00, nullptr,
        0x80004000);
    bool loop_entry_threw = false;
    try {
        render_dsp_sync_2ch(
            guest_memory, 0x82014000, kDspLeft, kDspRight);
    } catch (const std::runtime_error&) {
        loop_entry_threw = true;
    }
    passed &= expect(
        !loop_entry_threw &&
            load_guest_s16(guest_memory, kDspLeft) > 0 &&
            load_guest_s16(guest_memory, kDspRight) == 0,
        "ADPCM first entry at a nonzero loop start does not seek to loop end as the previous resampler tap");
    drain_dsp_mailbox(guest_memory);

    constexpr std::uint32_t kDspLoopStartBaseSource = 0x807C1000;
    constexpr std::uint32_t kDspLoopStartSourceStart = 57728u;
    const std::uint32_t loop_start_source_offset =
        (kDspLoopStartSourceStart / 16u) * 9u;
    std::vector<std::byte> loop_start_base_channels(0x180u * 64u);
    std::vector<std::byte> loop_start_base_source(
        loop_start_source_offset + 9u);
    std::vector<std::byte> loop_start_base_silence(
        0x50u * sizeof(std::uint16_t));
    loop_start_base_source[loop_start_source_offset] = std::byte{0xC0};
    for (std::uint32_t byte = 1; byte < 9u; ++byte) {
        loop_start_base_source[loop_start_source_offset + byte] =
            std::byte{0x11};
    }
    write_be16(loop_start_base_channels, 0x000, 1);
    write_be16(loop_start_base_channels, 0x004, 0x1000);
    write_be16(loop_start_base_channels, 0x010, 0x0D00);
    write_be16(loop_start_base_channels, 0x012, 0x7FFF);
    write_be16(loop_start_base_channels, 0x014, 0x7FFF);
    write_be16(loop_start_base_channels, 0x064, 16);
    write_be32(loop_start_base_channels, 0x068, kDspLoopStartSourceStart);
    write_be32(
        loop_start_base_channels,
        0x070,
        kDspLoopStartBaseSource + loop_start_source_offset);
    write_be16(loop_start_base_channels, 0x100, 9);
    write_be16(loop_start_base_channels, 0x102, 1);
    write_be16(loop_start_base_channels, 0x104, 0x4000);
    write_be16(loop_start_base_channels, 0x106, 0x0000);
    write_be32(loop_start_base_channels, 0x110, kDspLoopStartSourceStart);
    write_be32(loop_start_base_channels, 0x114, 63998u);
    write_be32(loop_start_base_channels, 0x118, kDspLoopStartBaseSource);
    write_be32(loop_start_base_channels, 0x11C, 64896u);
    memory.copy(kDspChannelTable, loop_start_base_channels);
    memory.copy(kDspLoopStartBaseSource, loop_start_base_source);
    memory.copy(kDspLeft, loop_start_base_silence);
    memory.copy(kDspRight, loop_start_base_silence);
    render_dsp_sync_2ch(
        guest_memory, 0x82014000, kDspLeft, kDspRight);
    passed &= expect(
        load_guest_s16(guest_memory, kDspLeft) > 512 &&
            load_guest_s16(guest_memory, kDspRight) == 0,
        "ADPCM loop-start channels keep _118 as the stream base instead of applying partial-start bias");
    drain_dsp_mailbox(guest_memory);

    constexpr std::uint32_t kDspLoopStartAdjustedBaseSource = 0x807E0000;
    const std::uint32_t loop_start_adjusted_source =
        kDspLoopStartAdjustedBaseSource + loop_start_source_offset;
    std::vector<std::byte> loop_start_adjusted_channels(0x180u * 64u);
    std::vector<std::byte> loop_start_adjusted_source_data(
        loop_start_source_offset + 9u);
    std::vector<std::byte> loop_start_adjusted_silence(
        0x50u * sizeof(std::uint16_t));
    loop_start_adjusted_source_data[loop_start_source_offset] =
        std::byte{0xC0};
    for (std::uint32_t byte = 1; byte < 9u; ++byte) {
        loop_start_adjusted_source_data[loop_start_source_offset + byte] =
            std::byte{0x11};
    }
    write_be16(loop_start_adjusted_channels, 0x000, 1);
    write_be16(loop_start_adjusted_channels, 0x004, 0x23E2);
    write_be16(loop_start_adjusted_channels, 0x010, 0x0D00);
    write_be16(loop_start_adjusted_channels, 0x012, 0x7FFF);
    write_be16(loop_start_adjusted_channels, 0x014, 0x7FFF);
    write_be16(loop_start_adjusted_channels, 0x064, 16);
    write_be32(
        loop_start_adjusted_channels, 0x068, kDspLoopStartSourceStart);
    write_be32(loop_start_adjusted_channels, 0x070, loop_start_adjusted_source);
    write_be16(loop_start_adjusted_channels, 0x100, 9);
    write_be16(loop_start_adjusted_channels, 0x102, 1);
    write_be16(loop_start_adjusted_channels, 0x104, 0x4000);
    write_be16(loop_start_adjusted_channels, 0x106, 0x0000);
    write_be32(
        loop_start_adjusted_channels, 0x110, kDspLoopStartSourceStart);
    write_be32(loop_start_adjusted_channels, 0x114, 63998u);
    write_be32(loop_start_adjusted_channels, 0x118, loop_start_adjusted_source);
    write_be32(loop_start_adjusted_channels, 0x11C, 64896u);
    memory.copy(kDspChannelTable, loop_start_adjusted_channels);
    memory.copy(
        kDspLoopStartAdjustedBaseSource, loop_start_adjusted_source_data);
    memory.copy(kDspLeft, loop_start_adjusted_silence);
    memory.copy(kDspRight, loop_start_adjusted_silence);
    render_dsp_sync_2ch(
        guest_memory, 0x82014000, kDspLeft, kDspRight);
    passed &= expect(
        load_guest_s16(guest_memory, kDspLeft) > 512 &&
            load_guest_s16(guest_memory, kDspRight) == 0,
        "ADPCM loop-start channels recover the origin when _118 is already adjusted to the start block");
    drain_dsp_mailbox(guest_memory);

    constexpr std::uint32_t kDspPartialSource = 0x807C0800;
    std::vector<std::byte> partial_channels(0x180u * 64u);
    std::vector<std::byte> partial_source(9u * 3u);
    partial_source[9u] = std::byte{0xC0};
    for (std::uint32_t byte = 1; byte < 9; ++byte) {
        partial_source[9u + byte] = std::byte{0x11};
    }
    write_be16(partial_channels, 0x000, 1);
    write_be16(partial_channels, 0x004, 0x1000);
    write_be16(partial_channels, 0x010, 0x0D00);
    write_be16(partial_channels, 0x012, 0x7FFF);
    write_be16(partial_channels, 0x014, 0x7FFF);
    write_be16(partial_channels, 0x064, 16);
    write_be32(partial_channels, 0x068, 16);
    write_be16(partial_channels, 0x100, 9);
    write_be32(partial_channels, 0x110, 0);
    write_be32(partial_channels, 0x114, 48);
    write_be32(partial_channels, 0x118, kDspPartialSource + 9u);
    write_be32(partial_channels, 0x11C, 48);
    memory.copy(kDspChannelTable, partial_channels);
    memory.copy(kDspPartialSource, partial_source);
    memory.copy(kDspLeft, dsp_silence);
    memory.copy(kDspRight, dsp_silence);
    render_dsp_sync_2ch(
        guest_memory, 0x82014000, kDspLeft, kDspRight);
    const std::int16_t first_partial_left =
        load_guest_s16(guest_memory, kDspLeft);
    const std::int16_t first_partial_right =
        load_guest_s16(guest_memory, kDspRight);
    passed &= expect(
        first_partial_left > 512 && first_partial_right == 0,
        "ADPCM partial starts read from the adjusted _118 source block");
    drain_dsp_mailbox(guest_memory);

    constexpr std::uint32_t kDspMidBlockPartialSource = 0x807C0A00;
    std::vector<std::byte> mid_block_partial_channels(0x180u * 64u);
    std::vector<std::byte> mid_block_partial_source(9u);
    mid_block_partial_source[0] = std::byte{0xC0};
    for (std::uint32_t byte = 1; byte < 9; ++byte) {
        mid_block_partial_source[byte] = std::byte{0x11};
    }
    write_be16(mid_block_partial_channels, 0x000, 1);
    write_be16(mid_block_partial_channels, 0x004, 0x1000);
    write_be16(mid_block_partial_channels, 0x010, 0x0D00);
    write_be16(mid_block_partial_channels, 0x012, 0x7FFF);
    write_be16(mid_block_partial_channels, 0x014, 0x7FFF);
    write_be16(mid_block_partial_channels, 0x064, 16);
    write_be32(mid_block_partial_channels, 0x068, 5);
    write_be16(mid_block_partial_channels, 0x100, 9);
    write_be32(mid_block_partial_channels, 0x110, 0);
    write_be32(mid_block_partial_channels, 0x114, 16);
    write_be32(
        mid_block_partial_channels,
        0x118,
        kDspMidBlockPartialSource + ((5u * 9u) / 16u));
    write_be32(mid_block_partial_channels, 0x11C, 16);
    memory.copy(kDspChannelTable, mid_block_partial_channels);
    memory.copy(kDspMidBlockPartialSource, mid_block_partial_source);
    memory.copy(kDspLeft, dsp_silence);
    memory.copy(kDspRight, dsp_silence);
    render_dsp_sync_2ch(
        guest_memory, 0x82014000, kDspLeft, kDspRight);
    const std::int16_t first_mid_block_partial_left =
        load_guest_s16(guest_memory, kDspLeft);
    const std::int16_t first_mid_block_partial_right =
        load_guest_s16(guest_memory, kDspRight);
    passed &= expect(
        first_mid_block_partial_left > 1500 &&
            first_mid_block_partial_right == 0,
        "ADPCM mid-block partial starts recover the original block header");
    drain_dsp_mailbox(guest_memory);

    constexpr std::uint32_t kDspAdjustedPartialSource = 0x807C0A80;
    std::vector<std::byte> adjusted_partial_channels(0x180u * 64u);
    std::vector<std::byte> adjusted_partial_source(9u);
    adjusted_partial_source[0] = std::byte{0xC0};
    for (std::uint32_t byte = 1; byte < 9; ++byte) {
        adjusted_partial_source[byte] = std::byte{0x11};
    }
    write_be16(adjusted_partial_channels, 0x000, 1);
    write_be16(adjusted_partial_channels, 0x004, 0x1000);
    write_be16(adjusted_partial_channels, 0x010, 0x0D00);
    write_be16(adjusted_partial_channels, 0x012, 0x7FFF);
    write_be16(adjusted_partial_channels, 0x014, 0x7FFF);
    write_be16(adjusted_partial_channels, 0x064, 16);
    write_be32(adjusted_partial_channels, 0x068, 5);
    write_be16(adjusted_partial_channels, 0x100, 9);
    write_be32(adjusted_partial_channels, 0x110, 0);
    write_be32(adjusted_partial_channels, 0x114, 11);
    write_be32(
        adjusted_partial_channels,
        0x118,
        kDspAdjustedPartialSource + ((5u * 9u) / 16u));
    write_be32(adjusted_partial_channels, 0x11C, 16);
    memory.copy(kDspChannelTable, adjusted_partial_channels);
    memory.copy(kDspAdjustedPartialSource, adjusted_partial_source);
    memory.copy(kDspLeft, dsp_silence);
    memory.copy(kDspRight, dsp_silence);
    render_dsp_sync_2ch(
        guest_memory, 0x82014000, kDspLeft, kDspRight);
    passed &= expect(
        load_guest_s16(
            guest_memory,
            kDspLeft + static_cast<std::uint32_t>(10u * sizeof(std::uint16_t))) >
                0 &&
            load_guest_s16(
                guest_memory,
                kDspLeft + static_cast<std::uint32_t>(11u * sizeof(std::uint16_t))) ==
                0 &&
            galaxy::guest_load_u32(
                guest_memory,
                kDspChannelTable + 0x070,
                nullptr,
                0x80004000) == kDspAdjustedPartialSource + 9u,
        "ADPCM adjusted partial-start loop-end plays the full remaining tail");
    drain_dsp_mailbox(guest_memory);

    constexpr std::uint32_t kDspSequentialHistorySource = 0x807C0B00;
    std::vector<std::byte> sequential_history_channels(0x180u * 64u);
    std::vector<std::byte> sequential_history_source(9u * 3u);
    sequential_history_source[0] = std::byte{0xC0};
    for (std::uint32_t byte = 1; byte < 9u; ++byte) {
        sequential_history_source[byte] = std::byte{0x11};
    }
    sequential_history_source[9u] = std::byte{0x01};
    sequential_history_source[18u] = std::byte{0x01};
    write_be16(sequential_history_channels, 0x000, 1);
    write_be16(sequential_history_channels, 0x004, 0x1000);
    write_be16(sequential_history_channels, 0x010, 0x0D00);
    write_be16(sequential_history_channels, 0x012, 0x7FFF);
    write_be16(sequential_history_channels, 0x014, 0x7FFF);
    write_be16(sequential_history_channels, 0x064, 16);
    write_be32(sequential_history_channels, 0x068, 32);
    write_be16(sequential_history_channels, 0x100, 9);
    write_be32(sequential_history_channels, 0x110, 0);
    write_be32(sequential_history_channels, 0x114, 48);
    write_be32(
        sequential_history_channels, 0x118, kDspSequentialHistorySource);
    write_be32(sequential_history_channels, 0x11C, 48);
    memory.copy(kDspChannelTable, sequential_history_channels);
    memory.copy(kDspSequentialHistorySource, sequential_history_source);
    memory.copy(kDspLeft, dsp_silence);
    memory.copy(kDspRight, dsp_silence);
    render_dsp_sync_2ch(
        guest_memory, 0x82014000, kDspLeft, kDspRight);
    const std::int16_t sequential_history_left =
        load_guest_s16(guest_memory, kDspLeft);
    const std::int16_t sequential_history_right =
        load_guest_s16(guest_memory, kDspRight);
    passed &= expect(
        sequential_history_left > 512 &&
            sequential_history_right == 0,
        "ADPCM invalid-cache seeks replay predictor history through skipped blocks");
    drain_dsp_mailbox(guest_memory);

    constexpr std::uint32_t kDspCurrentPointerHistorySource = 0x807C0B80;
    std::vector<std::byte> current_pointer_history_channels(0x180u * 64u);
    std::vector<std::byte> current_pointer_history_source(9u * 3u);
    current_pointer_history_source[0] = std::byte{0xC0};
    for (std::uint32_t byte = 1; byte < 9u; ++byte) {
        current_pointer_history_source[byte] = std::byte{0x11};
    }
    current_pointer_history_source[9u] = std::byte{0x01};
    current_pointer_history_source[18u] = std::byte{0x01};
    write_be16(current_pointer_history_channels, 0x000, 1);
    write_be16(current_pointer_history_channels, 0x004, 0x1000);
    write_be16(current_pointer_history_channels, 0x010, 0x0D00);
    write_be16(current_pointer_history_channels, 0x012, 0x7FFF);
    write_be16(current_pointer_history_channels, 0x014, 0x7FFF);
    write_be16(current_pointer_history_channels, 0x064, 16);
    write_be32(current_pointer_history_channels, 0x068, 32);
    write_be32(
        current_pointer_history_channels,
        0x070,
        kDspCurrentPointerHistorySource + 18u);
    write_be16(current_pointer_history_channels, 0x100, 9);
    write_be32(current_pointer_history_channels, 0x110, 0);
    write_be32(current_pointer_history_channels, 0x114, 48);
    write_be32(
        current_pointer_history_channels,
        0x118,
        kDspCurrentPointerHistorySource);
    write_be32(current_pointer_history_channels, 0x11C, 48);
    memory.copy(kDspChannelTable, current_pointer_history_channels);
    memory.copy(
        kDspCurrentPointerHistorySource, current_pointer_history_source);
    memory.copy(kDspLeft, dsp_silence);
    memory.copy(kDspRight, dsp_silence);
    render_dsp_sync_2ch(
        guest_memory, 0x82014000, kDspLeft, kDspRight);
    passed &= expect(
        load_guest_s16(guest_memory, kDspLeft) > 512 &&
            load_guest_s16(guest_memory, kDspRight) == 0,
        "ADPCM base-source channels keep predictor history when _70 points at the current compressed block");
    drain_dsp_mailbox(guest_memory);

    constexpr std::uint32_t kDspAfcNoRoundSource = 0x807C0BC0;
    std::vector<std::byte> afc_no_round_channels(0x180u * 64u);
    std::vector<std::byte> afc_no_round_source(9u * 2u);
    std::vector<std::byte> afc_no_round_silence(
        0x50u * sizeof(std::uint16_t));
    afc_no_round_source[9u] = std::byte{0x03};
    afc_no_round_source[10u] = std::byte{0x80};
    write_be16(afc_no_round_channels, 0x000, 1);
    write_be16(afc_no_round_channels, 0x004, 0x1000);
    write_be16(afc_no_round_channels, 0x010, 0x0D00);
    write_be16(afc_no_round_channels, 0x012, 0x4000);
    write_be16(afc_no_round_channels, 0x014, 0x4000);
    write_be16(afc_no_round_channels, 0x064, 16);
    write_be32(afc_no_round_channels, 0x068, 16);
    write_be32(afc_no_round_channels, 0x070, kDspAfcNoRoundSource + 9u);
    write_be16(afc_no_round_channels, 0x100, 9);
    write_be16(afc_no_round_channels, 0x102, 1);
    write_be16(afc_no_round_channels, 0x104, 0xFC00);
    write_be16(afc_no_round_channels, 0x106, 0xFC01);
    write_be32(afc_no_round_channels, 0x110, 16);
    write_be32(afc_no_round_channels, 0x114, 32);
    write_be32(afc_no_round_channels, 0x118, kDspAfcNoRoundSource);
    write_be32(afc_no_round_channels, 0x11C, 32);
    memory.copy(kDspChannelTable, afc_no_round_channels);
    memory.copy(kDspAfcNoRoundSource, afc_no_round_source);
    memory.copy(kDspLeft, afc_no_round_silence);
    memory.copy(kDspRight, afc_no_round_silence);
    render_dsp_sync_2ch(
        guest_memory, 0x82018000, kDspLeft, kDspRight);
    passed &= expect(
        load_guest_s16(guest_memory, kDspLeft) == -1032 &&
            load_guest_s16(guest_memory, kDspRight) == 0,
        "AFC decode does not apply a rounding bias before the predictor shift");
    drain_dsp_mailbox(guest_memory);

    constexpr std::uint32_t kDspSaturateSource = 0x807C0C00;
    std::vector<std::byte> saturate_channels(0x180u * 64u);
    std::vector<std::byte> saturate_source(9u);
    std::vector<std::byte> saturate_silence(0x50u * sizeof(std::uint16_t));
    saturate_source[0] = std::byte{0xF0};
    for (std::uint32_t byte = 1; byte < 9; ++byte) {
        saturate_source[byte] = std::byte{0x88};
    }
    write_be16(saturate_channels, 0x000, 1);
    write_be16(saturate_channels, 0x004, 0x1000);
    write_be16(saturate_channels, 0x010, 0x0D00);
    write_be16(saturate_channels, 0x012, 0x7FFF);
    write_be16(saturate_channels, 0x014, 0x7FFF);
    write_be16(saturate_channels, 0x064, 16);
    write_be16(saturate_channels, 0x100, 9);
    write_be32(saturate_channels, 0x110, 0);
    write_be32(saturate_channels, 0x114, 16);
    write_be32(saturate_channels, 0x118, kDspSaturateSource);
    write_be32(saturate_channels, 0x11C, 16);
    memory.copy(kDspChannelTable, saturate_channels);
    memory.copy(kDspSaturateSource, saturate_source);
    memory.copy(kDspLeft, saturate_silence);
    memory.copy(kDspRight, saturate_silence);
    const galaxy::host::DspAudioStats before_saturate_stats =
        memory.dsp_audio_stats();
    render_dsp_sync_2ch(
        guest_memory, 0x82014000, kDspLeft, kDspRight);
    const galaxy::host::DspAudioStats after_saturate_stats =
        memory.dsp_audio_stats();
    passed &= expect(
        load_guest_s16(guest_memory, kDspLeft) == -32766 &&
            load_guest_s16(guest_memory, kDspRight) == 0,
        "ADPCM saturation clamps to the DSP accelerator range before mixing");
    passed &= expect(
        after_saturate_stats.adpcm_decode_clamp_output_count >
                before_saturate_stats.adpcm_decode_clamp_output_count &&
            after_saturate_stats.adpcm_decode_clamp_output_low_count >
                before_saturate_stats.adpcm_decode_clamp_output_low_count,
        "ADPCM diagnostics distinguish consumed output clamps from replay/lookahead clamps");
    drain_dsp_mailbox(guest_memory);

    constexpr std::uint32_t kDspWideMixSourceA = 0x807C0D00;
    constexpr std::uint32_t kDspWideMixSourceB = 0x807C0D80;
    std::vector<std::byte> wide_mix_channels(0x180u * 64u);
    std::vector<std::byte> wide_mix_source_a(16u * sizeof(std::uint16_t));
    std::vector<std::byte> wide_mix_source_b(16u * sizeof(std::uint16_t));
    std::vector<std::byte> wide_mix_silence(0x50u * sizeof(std::uint16_t));
    for (std::uint32_t sample = 0; sample < 16u; ++sample) {
        write_be16(
            wide_mix_source_a,
            sample * sizeof(std::uint16_t),
            0x6000);
        write_be16(
            wide_mix_source_b,
            sample * sizeof(std::uint16_t),
            0xC000);
    }
    write_be16(wide_mix_channels, 0x000, 1);
    write_be16(wide_mix_channels, 0x004, 0x1000);
    write_be16(wide_mix_channels, 0x010, 0x0D00);
    write_be16(wide_mix_channels, 0x012, 0x7FFF);
    write_be16(wide_mix_channels, 0x014, 0x7FFF);
    write_be16(wide_mix_channels, 0x064, 1);
    write_be16(wide_mix_channels, 0x100, 16);
    write_be32(wide_mix_channels, 0x110, 0);
    write_be32(wide_mix_channels, 0x114, 16);
    write_be32(wide_mix_channels, 0x118, kDspWideMixSourceA);
    write_be32(wide_mix_channels, 0x11C, 16);
    constexpr std::uint32_t kDspSecondChannel = 0x180u;
    write_be16(wide_mix_channels, kDspSecondChannel + 0x000, 1);
    write_be16(wide_mix_channels, kDspSecondChannel + 0x004, 0x1000);
    write_be16(wide_mix_channels, kDspSecondChannel + 0x010, 0x0D00);
    write_be16(wide_mix_channels, kDspSecondChannel + 0x012, 0x7FFF);
    write_be16(wide_mix_channels, kDspSecondChannel + 0x014, 0x7FFF);
    write_be16(wide_mix_channels, kDspSecondChannel + 0x064, 1);
    write_be16(wide_mix_channels, kDspSecondChannel + 0x100, 16);
    write_be32(wide_mix_channels, kDspSecondChannel + 0x110, 0);
    write_be32(wide_mix_channels, kDspSecondChannel + 0x114, 16);
    write_be32(wide_mix_channels, kDspSecondChannel + 0x118, kDspWideMixSourceB);
    write_be32(wide_mix_channels, kDspSecondChannel + 0x11C, 16);
    memory.copy(kDspChannelTable, wide_mix_channels);
    memory.copy(kDspWideMixSourceA, wide_mix_source_a);
    memory.copy(kDspWideMixSourceB, wide_mix_source_b);
    memory.copy(kDspLeft, wide_mix_silence);
    memory.copy(kDspRight, wide_mix_silence);
    render_dsp_sync_2ch(
        guest_memory, 0x82018000, kDspLeft, kDspRight);
    passed &= expect(
        load_guest_s16(guest_memory, kDspLeft) > 12000 &&
            load_guest_s16(guest_memory, kDspRight) == 0,
        "DSP mixer accumulates voices in a wide buffer before final output clamp");
    drain_dsp_mailbox(guest_memory);

    constexpr std::uint32_t kDspIirSource = 0x807C0E00;
    std::vector<std::byte> iir_channels(0x180u * 64u);
    std::vector<std::byte> iir_source(16u * sizeof(std::uint16_t));
    std::vector<std::byte> iir_silence(0x50u * sizeof(std::uint16_t));
    for (std::uint32_t sample = 0; sample < 16u; ++sample) {
        write_be16(iir_source, sample * sizeof(std::uint16_t), 0x4000);
    }
    write_be16(iir_channels, 0x000, 1);
    write_be16(iir_channels, 0x004, 0x1000);
    write_be16(iir_channels, 0x010, 0x0D00);
    write_be16(iir_channels, 0x012, 0x7FFF);
    write_be16(iir_channels, 0x014, 0x7FFF);
    write_be16(iir_channels, 0x064, 1);
    write_be16(iir_channels, 0x100, 16);
    write_be16(iir_channels, 0x108, 0x20);
    write_be32(iir_channels, 0x110, 0);
    write_be32(iir_channels, 0x114, 16);
    write_be32(iir_channels, 0x118, kDspIirSource);
    write_be32(iir_channels, 0x11C, 16);
    write_be16(iir_channels, 0x148, 0x4000);
    memory.copy(kDspChannelTable, iir_channels);
    memory.copy(kDspIirSource, iir_source);
    memory.copy(kDspLeft, iir_silence);
    memory.copy(kDspRight, iir_silence);
    render_dsp_sync_2ch(
        guest_memory, 0x82014000, kDspLeft, kDspRight);
    const std::int16_t iir_first =
        load_guest_s16(guest_memory, kDspLeft);
    passed &= expect(
        iir_first > 7000 && iir_first < 9000 &&
            galaxy::guest_load_u16(
                guest_memory,
                kDspChannelTable + 0x0A8,
                nullptr,
                0x80004000) == 0x4000,
        "DSP IIR filter coefficient is applied and filter history persists");
    drain_dsp_mailbox(guest_memory);

    constexpr std::uint32_t kDspRestartSource = 0x807C0E00;
    std::vector<std::byte> restart_channels(0x180u * 64u);
    std::vector<std::byte> restart_source(9u * 10u);
    std::vector<std::byte> restart_silence(0x50u * sizeof(std::uint16_t));
    for (std::uint32_t block = 0; block < 5u; ++block) {
        restart_source[block * 9u] = std::byte{0xC0};
        for (std::uint32_t byte = 1; byte < 9u; ++byte) {
            restart_source[block * 9u + byte] = std::byte{0x11};
        }
    }
    restart_source[5u * 9u] = std::byte{0x01};
    write_be16(restart_channels, 0x000, 1);
    write_be16(restart_channels, 0x004, 0x1000);
    write_be16(restart_channels, 0x008, 1);
    write_be16(restart_channels, 0x010, 0x0D00);
    write_be16(restart_channels, 0x012, 0x7FFF);
    write_be16(restart_channels, 0x014, 0x7FFF);
    write_be16(restart_channels, 0x064, 16);
    write_be16(restart_channels, 0x100, 9);
    write_be32(restart_channels, 0x110, 0);
    write_be32(restart_channels, 0x114, 160);
    write_be32(restart_channels, 0x118, kDspRestartSource);
    write_be32(restart_channels, 0x11C, 160);
    memory.copy(kDspChannelTable, restart_channels);
    memory.copy(kDspRestartSource, restart_source);
    memory.copy(kDspLeft, restart_silence);
    memory.copy(kDspRight, restart_silence);
    render_dsp_sync_2ch(
        guest_memory, 0x82014000, kDspLeft, kDspRight);
    const std::uint32_t restart_position =
        galaxy::guest_load_u32(
            guest_memory, kDspChannelTable + 0x068, nullptr, 0x80004000);
    drain_dsp_mailbox(guest_memory);
    memory.copy(kDspLeft, restart_silence);
    memory.copy(kDspRight, restart_silence);
    galaxy::guest_store_u16(
        guest_memory, kDspChannelTable + 0x008, 1, nullptr, 0x80004000);
    galaxy::guest_store_u32(
        guest_memory,
        kDspChannelTable + 0x068,
        restart_position,
        nullptr,
        0x80004000);
    galaxy::guest_store_u32(
        guest_memory,
        kDspChannelTable + 0x070,
        kDspRestartSource + 45u,
        nullptr,
        0x80004000);
    render_dsp_sync_2ch(
        guest_memory, 0x82014000, kDspLeft, kDspRight);
    passed &= expect(
        load_guest_s16(guest_memory, kDspLeft) == 0 &&
            load_guest_s16(guest_memory, kDspRight) == 0,
        "ADPCM channel start clears stale predictor/cache state on reused channels");
    drain_dsp_mailbox(guest_memory);

    constexpr std::uint32_t kDspInterpSource = 0x807C1000;
    std::vector<std::byte> interp_channels(0x180u * 64u);
    std::vector<std::byte> interp_source(16u * sizeof(std::uint16_t));
    std::vector<std::byte> interp_silence(0x50u * sizeof(std::uint16_t));
    for (std::uint32_t sample = 0; sample < 16; ++sample) {
        write_be16(
            interp_source,
            sample * sizeof(std::uint16_t),
            static_cast<std::uint16_t>(sample * 2048u));
    }
    write_be16(interp_channels, 0x000, 1);
    write_be16(interp_channels, 0x004, 0x0800);
    write_be16(interp_channels, 0x010, 0x0D00);
    write_be16(interp_channels, 0x012, 0x7FFF);
    write_be16(interp_channels, 0x014, 0x7FFF);
    write_be16(interp_channels, 0x064, 1);
    write_be16(interp_channels, 0x100, 16);
    write_be32(interp_channels, 0x110, 0);
    write_be32(interp_channels, 0x114, 16);
    write_be32(interp_channels, 0x118, kDspInterpSource);
    write_be32(interp_channels, 0x11C, 16);
    memory.copy(kDspChannelTable, interp_channels);
    memory.copy(kDspInterpSource, interp_source);
    memory.copy(kDspLeft, interp_silence);
    memory.copy(kDspRight, interp_silence);
    render_dsp_sync_2ch(
        guest_memory, 0x82018000, kDspLeft, kDspRight);
    const std::int16_t first_interp_left =
        load_guest_s16(guest_memory, kDspLeft);
    const std::int16_t second_interp_left =
        load_guest_s16(guest_memory, kDspLeft + sizeof(std::uint16_t));
    const std::int16_t second_interp_right =
        load_guest_s16(guest_memory, kDspRight + sizeof(std::uint16_t));
    passed &= expect(
        second_interp_left > first_interp_left + 256,
        "DSP pitch resampler filters fractional positions instead of repeating the nearest sample");
    passed &= expect(
        second_interp_right == 0,
        "DSP pitch resampler keeps front-left routing isolated");
    drain_dsp_mailbox(guest_memory);

    constexpr std::uint32_t kDspInterpPhaseSource = 0x807C1400;
    std::vector<std::byte> interp_phase_channels(0x180u * 64u);
    std::vector<std::byte> interp_phase_source(16u * sizeof(std::uint16_t));
    std::vector<std::byte> interp_phase_silence(
        0x50u * sizeof(std::uint16_t));
    for (std::uint32_t sample = 0; sample < 16; ++sample) {
        write_be16(
            interp_phase_source,
            sample * sizeof(std::uint16_t),
            static_cast<std::uint16_t>(sample * 2048u));
    }
    write_be16(interp_phase_channels, 0x000, 1);
    write_be16(interp_phase_channels, 0x004, 0x0800);
    write_be16(interp_phase_channels, 0x008, 1);
    write_be16(interp_phase_channels, 0x010, 0x0D00);
    write_be16(interp_phase_channels, 0x012, 0x7FFF);
    write_be16(interp_phase_channels, 0x014, 0x7FFF);
    write_be16(interp_phase_channels, 0x064, 1);
    write_be32(interp_phase_channels, 0x068, 0);
    write_be16(interp_phase_channels, 0x100, 16);
    write_be32(interp_phase_channels, 0x110, 0);
    write_be32(interp_phase_channels, 0x114, 16);
    write_be32(interp_phase_channels, 0x118, kDspInterpPhaseSource);
    write_be32(interp_phase_channels, 0x11C, 16);
    memory.copy(kDspChannelTable, interp_phase_channels);
    memory.copy(kDspInterpPhaseSource, interp_phase_source);
    memory.copy(kDspLeft, interp_phase_silence);
    memory.copy(kDspRight, interp_phase_silence);
    render_dsp_sync_2ch(
        guest_memory, 0x82018000, kDspLeft, kDspRight);
    const std::int16_t first_interp_phase_left =
        load_guest_s16(guest_memory, kDspLeft);
    passed &= expect(
        first_interp_phase_left >= 0 && first_interp_phase_left < 512,
        "DSP pitch resampler uses a centered 4-tap phase window");
    drain_dsp_mailbox(guest_memory);

    constexpr std::uint32_t kDspLoopOvershootSource = 0x807C1800;
    std::vector<std::byte> loop_overshoot_channels(0x180u * 64u);
    std::vector<std::byte> loop_overshoot_source(8u * sizeof(std::uint16_t));
    std::vector<std::byte> loop_overshoot_silence(0x50u * sizeof(std::uint16_t));
    const std::array<std::uint16_t, 8> loop_overshoot_samples{
        0, 0, 0, 30000, 10000, 0, 0, 0};
    for (std::uint32_t sample = 0; sample < 8; ++sample) {
        write_be16(
            loop_overshoot_source,
            sample * sizeof(std::uint16_t),
            loop_overshoot_samples[sample]);
    }
    write_be16(loop_overshoot_channels, 0x000, 1);
    write_be16(loop_overshoot_channels, 0x004, 0x3000);
    write_be16(loop_overshoot_channels, 0x010, 0x0D00);
    write_be16(loop_overshoot_channels, 0x012, 0x4000);
    write_be16(loop_overshoot_channels, 0x014, 0x4000);
    write_be16(loop_overshoot_channels, 0x064, 1);
    write_be16(loop_overshoot_channels, 0x100, 16);
    write_be32(loop_overshoot_channels, 0x068, 4);
    write_be32(loop_overshoot_channels, 0x110, 2);
    write_be32(loop_overshoot_channels, 0x114, 6);
    write_be16(loop_overshoot_channels, 0x102, 1);
    write_be32(loop_overshoot_channels, 0x118, kDspLoopOvershootSource);
    write_be32(loop_overshoot_channels, 0x11C, 8);
    memory.copy(kDspChannelTable, loop_overshoot_channels);
    memory.copy(kDspLoopOvershootSource, loop_overshoot_source);
    memory.copy(kDspLeft, loop_overshoot_silence);
    memory.copy(kDspRight, loop_overshoot_silence);
    render_dsp_sync_2ch(
        guest_memory, 0x82014000, kDspLeft, kDspRight);
    const std::int16_t loop_overshoot_first =
        load_guest_s16(guest_memory, kDspLeft);
    const std::int16_t loop_overshoot_second =
        load_guest_s16(guest_memory, kDspLeft + sizeof(std::uint16_t));
    passed &= expect(
        loop_overshoot_second > loop_overshoot_first + 2000,
        "DSP loop wrap preserves high-pitch overshoot instead of snapping to loop start");
    drain_dsp_mailbox(guest_memory);

    const auto render_pcm16_route =
        [&](std::uint16_t connect,
            bool auto_mixer = false,
            std::uint16_t auto_volume = 0x4000,
            std::uint16_t bus_target = 0x4000,
            std::uint16_t bus_current = 0x4000)
            -> std::array<std::int16_t, 2> {
        constexpr std::uint32_t kDspRouteSource = 0x807C2000;
        std::vector<std::byte> route_channels(0x180u * 64u);
        std::vector<std::byte> route_source(16u * sizeof(std::uint16_t));
        for (std::uint32_t sample = 0; sample < 16; ++sample) {
            write_be16(route_source, sample * sizeof(std::uint16_t), 0x4000);
        }
        write_be16(route_channels, 0x000, 1);
        write_be16(route_channels, 0x004, 0x1000);
        write_be16(route_channels, 0x010, connect);
        write_be16(route_channels, 0x012, bus_target);
        write_be16(route_channels, 0x014, bus_current);
        if (auto_mixer) {
            write_be16(route_channels, 0x050, 0x4000);
            write_be16(route_channels, 0x054, auto_volume);
            write_be16(route_channels, 0x056, auto_volume);
            write_be16(route_channels, 0x058, 1);
        }
        write_be16(route_channels, 0x064, 1);
        write_be16(route_channels, 0x100, 16);
        write_be32(route_channels, 0x110, 0);
        write_be32(route_channels, 0x114, 16);
        write_be32(route_channels, 0x118, kDspRouteSource);
        write_be32(route_channels, 0x11C, 16);
        memory.copy(kDspChannelTable, route_channels);
        memory.copy(kDspRouteSource, route_source);
        memory.copy(kDspLeft, dsp_silence);
        memory.copy(kDspRight, dsp_silence);
        render_dsp_sync_2ch(
            guest_memory, 0x82014000, kDspLeft, kDspRight);
        const std::array result{
            load_guest_s16(guest_memory, kDspLeft),
            load_guest_s16(guest_memory, kDspRight),
        };
        drain_dsp_mailbox(guest_memory);
        return result;
    };
    const auto front_left_route = render_pcm16_route(0x0D00);
    passed &= expect(
        front_left_route[0] > 0 && front_left_route[1] == 0,
        "DSP front-left connect routes to the left mixdown only");
    const auto front_right_route = render_pcm16_route(0x0D60);
    passed &= expect(
        front_right_route[1] > 0 && front_right_route[0] == 0,
        "DSP front-right connect routes to the right mixdown only");
    const auto internal_route = render_pcm16_route(0x0E20);
    passed &= expect(
        internal_route[0] == 0 && internal_route[1] == 0,
        "DSP internal/reverb connect does not fold directly into stereo");
    const auto zero_route = render_pcm16_route(0x0000);
    passed &= expect(
        zero_route[0] == 0 && zero_route[1] == 0,
        "DSP zero connect without auto-mixer stays silent");
    const auto auto_route = render_pcm16_route(0x0000, true);
    passed &= expect(
        auto_route[0] > 0 && auto_route[1] > 0 &&
            std::abs(auto_route[0] - auto_route[1]) <= 128,
        "DSP auto-mixer zero-connect path produces balanced native stereo");
    const auto auto_stale_direct_route =
        render_pcm16_route(0x0D60, true, 0, 0x7FFF, 0x8001);
    passed &= expect(
        auto_stale_direct_route[0] == 0 && auto_stale_direct_route[1] == 0,
        "DSP auto-mixer flag mutes zero-volume channels instead of falling back to stale bus gain");
    const galaxy::host::DspAudioStats route_dsp_stats =
        memory.dsp_audio_stats();
    passed &= expect(
        route_dsp_stats.music_sample_visits > 0 &&
            route_dsp_stats.max_music_peak >= 0x4000u,
        "DSP audio provenance records PCM16 music samples and peak");

    constexpr std::uint32_t kDspAuxA = 0x807D0400;
    constexpr std::uint32_t kDspAuxB = 0x807D0600;
    constexpr std::uint32_t kDspAuxSource = 0x807C2400;
    std::vector<std::byte> aux_channels(0x180u * 64u);
    std::vector<std::byte> aux_source(16u * sizeof(std::uint16_t));
    for (std::uint32_t sample = 0; sample < 16; ++sample) {
        write_be16(aux_source, sample * sizeof(std::uint16_t), 0x4000);
    }
    write_be16(aux_channels, 0x000, 1);
    write_be16(aux_channels, 0x004, 0x1000);
    write_be16(aux_channels, 0x010, 0x0E20);
    write_be16(aux_channels, 0x012, 0x4000);
    write_be16(aux_channels, 0x014, 0x4000);
    write_be16(aux_channels, 0x064, 1);
    write_be16(aux_channels, 0x100, 16);
    write_be32(aux_channels, 0x110, 0);
    write_be32(aux_channels, 0x114, 16);
    write_be32(aux_channels, 0x118, kDspAuxSource);
    write_be32(aux_channels, 0x11C, 16);
    memory.copy(kDspChannelTable, aux_channels);
    memory.copy(kDspAuxSource, aux_source);
    memory.copy(kDspLeft, dsp_silence);
    memory.copy(kDspRight, dsp_silence);
    memory.copy(kDspAuxA, dsp_silence);
    memory.copy(kDspAuxB, dsp_silence);
    render_dsp_sync_4ch(
        guest_memory, 0x82014000, kDspLeft, kDspRight, kDspAuxA, kDspAuxB);
    passed &= expect(
        load_guest_s16(guest_memory, kDspLeft) == 0 &&
            load_guest_s16(guest_memory, kDspRight) == 0 &&
            load_guest_s16(guest_memory, kDspAuxA) > 0,
        "DSP aux-A route writes the aux plane instead of being ignored");
    drain_dsp_mailbox(guest_memory);

    std::vector<std::byte> unsupported_channels(0x180u * 64u);
    std::vector<std::byte> unsupported_source(0x50u);
    write_be16(unsupported_channels, 0x000, 1);
    write_be16(unsupported_channels, 0x004, 0x1000);
    write_be16(unsupported_channels, 0x010, 0x0D00);
    write_be16(unsupported_channels, 0x012, 0x4000);
    write_be16(unsupported_channels, 0x014, 0x4000);
    write_be16(unsupported_channels, 0x064, 16);
    write_be16(unsupported_channels, 0x100, 5);
    write_be32(unsupported_channels, 0x114, 160);
    write_be32(unsupported_channels, 0x118, kDspSource);
    write_be32(unsupported_channels, 0x11C, 160);
    memory.copy(kDspChannelTable, unsupported_channels);
    memory.copy(kDspSource, unsupported_source);
    memory.copy(kDspLeft, dsp_silence);
    memory.copy(kDspRight, dsp_silence);
    passed &= expect_runtime_error(
        [&] {
            render_dsp_sync_2ch(
                guest_memory, 0x82014000, kDspLeft, kDspRight);
        },
        "unsupported active DSP channel formats hard-fail instead of rendering silence");

    constexpr std::uint32_t kAiBuffer = 0x00100000;
    std::vector<std::byte> ai_pcm(64);
    for (std::size_t i = 0; i < ai_pcm.size(); ++i) {
        ai_pcm[i] = static_cast<std::byte>(i);
    }
    memory.copy(kAiBuffer, ai_pcm);
    const std::uint16_t dsp_control_before_ai = galaxy::guest_load_u16(
        guest_memory, 0xCC00500A, nullptr, 0x80004000);
    galaxy::guest_store_u16(
        guest_memory,
        0xCC00500A,
        static_cast<std::uint16_t>(dsp_control_before_ai & ~0x00B8u),
        nullptr,
        0x80004000);
    galaxy::guest_store_u32(
        guest_memory, 0xCD006C00, 0x40, nullptr, 0x80004000);
    const std::uint32_t exi_status_before_ai =
        galaxy::guest_load_u32(
            guest_memory, 0xCD006800, nullptr, 0x80004000);
    const std::uint64_t first_ai_start_ticks = fake_ticks;
    galaxy::guest_store_u16(
        guest_memory, 0xCC005030, 0x0010, nullptr, 0x80004000);
    galaxy::guest_store_u16(
        guest_memory, 0xCC005032, 0x0000, nullptr, 0x80004000);
    galaxy::guest_store_u16(
        guest_memory, 0xCC005036, 0x8002, nullptr, 0x80004000);
    passed &= expect(
        memory.ai_dma_playing(),
        "AI DMA play bit is tracked by the native host");
    passed &= expect(
        memory.ai_audio_submit_count() == 1,
        "AI DMA start latches and submits one active PCM buffer");
    passed &= expect(
        memory.ai_dma_active_sample_rate() == 32'000u,
        "AI DMA start latches the hardware-selected 32 kHz rate");
    const std::uint64_t first_ai_duration_32khz =
        memory.ai_dma_last_duration_ticks();
    const std::uint64_t first_ai_interrupt =
        memory.ai_dma_next_completion_ticks();
    passed &= expect(
        first_ai_interrupt ==
            fake_ticks + galaxy::host::ai_dma_timing::
                             kInitialInterruptDelayTimelineTicks,
        "AI DMA start arms its documented provisional timeline deadline");
    passed &= expect(
        ai_deadline_probe.deadlines.size() == 1u &&
            ai_deadline_probe.deadlines.back() == first_ai_interrupt &&
            ai_deadline_probe.submit_counts_at_callback.back() == 0u,
        "AI DMA start arms its exact first deadline before sink submission");
    memory.service_ai_dma(
        first_ai_interrupt - 1u,
        galaxy::host::AiDmaServiceMode::Interruptible);
    passed &= expect(
        !memory.ai_dma_interrupt_pending(),
        "AI DMA interrupt is not pending before its provisional start deadline");
    memory.service_ai_dma(
        first_ai_interrupt,
        galaxy::host::AiDmaServiceMode::Interruptible);
    passed &= expect(
        memory.ai_dma_interrupt_status(),
        "AI DMA start sets the hardware-owned DSPCR AID status bit");
    passed &= expect(
        !memory.ai_dma_interrupt_pending(),
        "AID status does not assert the processor line while AID_mask is clear");
    const std::uint16_t dsp_control_with_aid_status = galaxy::guest_load_u16(
        guest_memory, 0xCC00500A, nullptr, 0x80004000);
    galaxy::guest_store_u16(
        guest_memory,
        0xCC00500A,
        static_cast<std::uint16_t>(
            (dsp_control_with_aid_status & ~0x00A8u) | 0x0010u),
        nullptr,
        0x80004000);
    passed &= expect(
        memory.ai_dma_interrupt_pending() &&
            (galaxy::guest_load_u32(
                 guest_memory, 0xCC003000u, nullptr, 0x80004000u) &
             0x00000040u) != 0u,
        "enabling AID_mask asserts the latched AI level through PI cause");
    galaxy::guest_store_u16(
        guest_memory,
        0xCC00500A,
        static_cast<std::uint16_t>(
            (galaxy::guest_load_u16(
                 guest_memory, 0xCC00500A, nullptr, 0x80004000) &
             ~0x00A8u) |
            0x0018u),
        nullptr,
        0x80004000);
    passed &= expect(
        !memory.ai_dma_interrupt_status() &&
            !memory.ai_dma_interrupt_pending() &&
            (galaxy::guest_load_u32(
                 guest_memory, 0x0C003000u, nullptr, 0x80004000u) &
             0x00000040u) == 0u,
        "guest DSPCR W1C acknowledges AID and deasserts PI without dropping its mask");
    passed &= expect(
        memory.ai_dma_completion_armed(),
        "AI DMA start interrupt leaves transfer completion armed");
    const std::uint64_t first_ai_completion =
        memory.ai_dma_next_completion_ticks();
    passed &= expect(
        first_ai_completion > first_ai_interrupt,
        "AI DMA active transfer has a later completion tick");
    passed &= expect(
        ai_deadline_probe.deadlines.size() == 2u &&
            ai_deadline_probe.deadlines.back() == first_ai_completion,
        "AI DMA start interrupt rearms the exact transfer completion deadline");
    passed &= expect(
        galaxy::guest_load_u16(
            guest_memory, 0xCC00503A, nullptr, 0x80004000) == 1,
        "AID_CNT reports zero-based blocks remaining at transfer start");
    galaxy::guest_store_u32(
        guest_memory, 0xCD006C00, 0x00, nullptr, 0x80004000);
    passed &= expect(
        memory.ai_dma_active_sample_rate() == 32'000u &&
            memory.ai_dma_last_duration_ticks() == first_ai_duration_32khz &&
            memory.ai_dma_next_completion_ticks() == first_ai_completion,
        "mid-transfer AIDFR change cannot retime or relabel active 32 kHz PCM");
    fake_ticks =
        first_ai_start_ticks +
        (first_ai_completion - first_ai_start_ticks) / 2u + 1u;
    passed &= expect(
        galaxy::guest_load_u16(
            guest_memory, 0xCC00503A, nullptr, 0x80004000) == 0,
        "AID_CNT decreases as the active transfer drains");

    constexpr std::uint32_t kNextAiBuffer = 0x00100040;
    memory.copy(kNextAiBuffer, ai_pcm);
    galaxy::guest_store_u16(
        guest_memory, 0xCC005030, 0x0010, nullptr, 0x80004000);
    galaxy::guest_store_u16(
        guest_memory, 0xCC005032, 0x0040, nullptr, 0x80004000);
    galaxy::guest_store_u16(
        guest_memory, 0xCC005036, 0x8002, nullptr, 0x80004000);
    passed &= expect(
        memory.ai_audio_submit_count() == 1,
        "AID_LEN write while enabled updates the shadow transfer only");
    memory.service_ai_dma(
        first_ai_completion,
        galaxy::host::AiDmaServiceMode::Interruptible);
    passed &= expect(
        memory.ai_dma_interrupt_pending(),
        "AI DMA completion autoloads the shadow transfer and raises interrupt");
    galaxy::guest_store_u16(
        guest_memory,
        0xCC00500A,
        static_cast<std::uint16_t>(
            (galaxy::guest_load_u16(
                 guest_memory, 0xCC00500A, nullptr, 0x80004000) &
             ~0x00A8u) |
            0x0018u),
        nullptr,
        0x80004000);
    passed &= expect(
        !memory.ai_dma_interrupt_pending(),
        "AI DMA completion stays asserted until the guest acknowledges DSPCR AID");
    passed &= expect(
        memory.ai_audio_submit_count() == 2,
        "AI DMA autoload submits the shadow buffer exactly once");
    passed &= expect(
        ai_deadline_probe.deadlines.size() == 3u &&
            ai_deadline_probe.deadlines.back() ==
                memory.ai_dma_next_completion_ticks() &&
            ai_deadline_probe.submit_counts_at_callback.back() == 1u,
        "AI DMA autoload arms the next immutable deadline before sink submission");
    passed &= expect(
        memory.ai_dma_active_sample_rate() == 48'000u &&
            memory.ai_dma_last_duration_ticks() * 3u ==
                first_ai_duration_32khz * 2u,
        "next DMA autoload latches 48 kHz and carries that exact rate to timing and playback");
    const std::uint64_t second_ai_completion =
        memory.ai_dma_next_completion_ticks();
    const std::uint64_t ai_duration_ticks =
        memory.ai_dma_last_duration_ticks();
    constexpr std::uint32_t kLateAiBuffer = 0x00100080;
    memory.copy(kLateAiBuffer, ai_pcm);
    galaxy::guest_store_u16(
        guest_memory, 0xCC005030, 0x0010, nullptr, 0x80004000);
    galaxy::guest_store_u16(
        guest_memory, 0xCC005032, 0x0080, nullptr, 0x80004000);
    galaxy::guest_store_u16(
        guest_memory, 0xCC005036, 0x8002, nullptr, 0x80004000);
    const std::uint64_t late_ai_now =
        second_ai_completion + ai_duration_ticks * 18u + 123u;
    passed &= expect_runtime_error(
        [&] {
            memory.service_ai_dma(
                late_ai_now,
                galaxy::host::AiDmaServiceMode::ExternalInterruptsMasked);
        },
        "EE masking cannot authorize retrospective reads of mutable PCM history");
    passed &= expect(
        memory.ai_audio_submit_count() == 2,
        "failed masked AI DMA catch-up has no sink or transfer side effects");
    memory.service_ai_dma(
        late_ai_now,
        galaxy::host::AiDmaServiceMode::Interruptible);
    passed &= expect(
        memory.ai_dma_interrupt_pending() &&
            memory.ai_audio_submit_count() == 3 &&
            memory.ai_dma_next_completion_ticks() ==
                second_ai_completion + ai_duration_ticks,
        "late interruptible AI service submits one boundary and waits for the guest callback");
    memory.service_ai_dma(
        late_ai_now,
        galaxy::host::AiDmaServiceMode::Interruptible);
    passed &= expect(
        memory.ai_audio_submit_count() == 3,
        "pending AID prevents repeated stale shadow submission before guest acknowledgement");
    // The direct late-service call above injects a future observation; reset
    // this fixture's MMIO clock to the first completed edge for the remaining
    // independent on-time register checks below.
    fake_ticks = second_ai_completion;
    memory.service_ai_dma(
        second_ai_completion,
        galaxy::host::AiDmaServiceMode::ExternalInterruptsMasked);
    passed &= expect(
        memory.ai_dma_interrupt_pending(),
        "the interruptible completion retains one guest-visible AID level");
    passed &= expect(
        memory.ai_audio_submit_count() == 3,
        "the already-serviced interruptible AI boundary is not submitted again");
    // Two assertions were REMOVED here (agent 6, fix 43):
    //   ai_dma_resync_events() == 0, "late AI DMA catch-up does not skip ahead by resyncing"
    //   ai_dma_resync_missed_buffers() == 0, "… records no skipped buffer periods"
    //
    // Both were TAUTOLOGIES. Their counters are zero-initialised and nothing in
    // the runtime ever writes them (verified: 0 `++`/`+=`, 0 `=` assignments, no
    // `resync` logic anywhere), so the getters can only return 0 and the
    // assertions could never fail — including in a build that resynced on every
    // buffer. Asserting that a constant equals zero is not a check.
    //
    // The invariant they were reaching for IS covered, by the real assertions
    // immediately above and below: `ai_audio_submit_count() == 3` pins "submits
    // one boundary and waits", and the masked-catch-up case is covered at
    // "masked catch-up republishes the completion and following exact deadlines".
    // The inert pair added no coverage.
    //
    // If a resync path is ever implemented, restore these assertions ONLY once
    // the counters are actually incremented — at that point they become
    // meaningful and this note is obsolete. See finding 39 and fix 42.
    passed &= expect(
        memory.ai_dma_next_completion_ticks() > second_ai_completion,
        "on-time masked AI service arms exactly the next real hardware boundary");
    galaxy::guest_store_u16(
        guest_memory,
        0xCC00500A,
        static_cast<std::uint16_t>(
            (galaxy::guest_load_u16(
                 guest_memory, 0xCC00500A, nullptr, 0x80004000) &
             ~0x00A8u) |
            0x0018u),
        nullptr,
        0x80004000);
    passed &= expect(
        !memory.ai_dma_interrupt_pending(),
        "one DSPCR acknowledgement clears the coalesced masked AID level");

    galaxy::guest_store_u32(
        guest_memory, 0xCD006C00, 0x00, nullptr, 0x80004000);
    const std::uint32_t exi_status_after_ai =
        galaxy::guest_load_u32(
            guest_memory, 0xCD006800, nullptr, 0x80004000);
    passed &= expect(
        exi_status_after_ai == exi_status_before_ai,
        "AI_CONTROL writes do not enter EXI channel bookkeeping");

    galaxy::guest_store_u16(
        guest_memory, 0xCC005036, 0x0000, nullptr, 0x80004000);
    passed &= expect(
        !memory.ai_dma_playing(),
        "AI DMA stop clears the native host play state");
    passed &= expect(
        !ai_deadline_probe.deadlines.empty() &&
            ai_deadline_probe.deadlines.back() == 0u,
        "AI DMA stop cancels the armed hardware deadline");

    // A guest stop/update is itself an MMIO observation point. If a transfer
    // edge elapsed first, hardware must autoload and submit that boundary
    // before the stop can cancel the following deadline.
    fake_ticks = second_ai_completion + ai_duration_ticks * 3u;
    const std::uint64_t submit_count_before_elapsed_stop =
        memory.ai_audio_submit_count();
    galaxy::guest_store_u16(
        guest_memory, 0xCC005030, 0x0010, nullptr, 0x80004000);
    galaxy::guest_store_u16(
        guest_memory, 0xCC005032, 0x0000, nullptr, 0x80004000);
    galaxy::guest_store_u16(
        guest_memory, 0xCC005036, 0x8002, nullptr, 0x80004000);
    const std::uint64_t elapsed_stop_completion =
        fake_ticks + memory.ai_dma_last_duration_ticks();
    fake_ticks = elapsed_stop_completion;
    galaxy::guest_store_u16(
        guest_memory, 0xCC005036, 0x0000, nullptr, 0x80004000);
    passed &= expect(
        memory.ai_audio_submit_count() ==
            submit_count_before_elapsed_stop + 2u,
        "AI DMA MMIO defaults to masked hardware advancement when no runtime provider is installed");
    passed &= expect(
        !memory.ai_dma_playing() &&
            ai_deadline_probe.deadlines.back() == 0u,
        "elapsed-boundary stop leaves hardware stopped with no armed deadline");

    const auto acknowledge_ai_level = [&] {
        const std::uint16_t control = galaxy::guest_load_u16(
            guest_memory, 0xCC00500A, nullptr, 0x80004000);
        galaxy::guest_store_u16(
            guest_memory,
            0xCC00500A,
            static_cast<std::uint16_t>(
                (control & ~0x00A8u) | 0x0018u),
            nullptr,
            0x80004000);
    };
    acknowledge_ai_level();

    AiMmioServiceDecisionProbe ai_mmio_decision{};
    memory.set_ai_dma_mmio_service_decision_provider(
        &provide_ai_mmio_service_decision, &ai_mmio_decision);

    // With EE masked, an MMIO observation must advance both elapsed hardware
    // boundaries and retain one coalesced level interrupt.  There is no guest
    // callback opportunity in this mode.
    ai_mmio_decision.decision =
        galaxy::host::AiDmaMmioServiceDecision::ExternalInterruptsMasked;
    fake_ticks += 10'000u;
    galaxy::guest_store_u16(
        guest_memory, 0xCC005030, 0x0010, nullptr, 0x80004000);
    galaxy::guest_store_u16(
        guest_memory, 0xCC005032, 0x0000, nullptr, 0x80004000);
    const std::uint64_t masked_callback_base =
        ai_deadline_probe.deadlines.size();
    galaxy::guest_store_u16(
        guest_memory, 0xCC005036, 0x8002, nullptr, 0x80004000);
    const auto masked_started = memory.dsp_debug_snapshot();
    const std::uint64_t masked_submit_after_start =
        memory.ai_audio_submit_count();
    fake_ticks = masked_started.ai_dma_next_completion_ticks;
    (void)galaxy::guest_load_u16(
        guest_memory, 0xCC00503A, nullptr, 0x80004000);
    const auto masked_serviced = memory.dsp_debug_snapshot();
    passed &= expect(
        memory.ai_audio_submit_count() == masked_submit_after_start + 1u &&
            masked_serviced.ai_dma_active_address == kAiBuffer &&
            masked_serviced.ai_dma_next_completion_ticks > fake_ticks &&
            memory.ai_dma_interrupt_pending(),
        "EE-masked AI MMIO advances initial IRQ plus completion and leaves one level pending");
    passed &= expect(
        ai_deadline_probe.deadlines.size() == masked_callback_base + 3u &&
            ai_deadline_probe.deadlines.back() ==
                masked_serviced.ai_dma_next_completion_ticks,
        "masked catch-up republishes the completion and following exact deadlines without losing a wake");
    galaxy::guest_store_u16(
        guest_memory, 0xCC005036, 0x0000, nullptr, 0x80004000);
    acknowledge_ai_level();

    // With EE enabled, the same overdue initial-IRQ + completion pair must
    // stop at the initial guest-visible level.  While that guest handler runs,
    // W1C and shadow writes cannot autoload the stale pre-callback address.
    ai_mmio_decision.decision =
        galaxy::host::AiDmaMmioServiceDecision::Interruptible;
    fake_ticks += 10'000u;
    galaxy::guest_store_u16(
        guest_memory, 0xCC005030, 0x0010, nullptr, 0x80004000);
    galaxy::guest_store_u16(
        guest_memory, 0xCC005032, 0x0000, nullptr, 0x80004000);
    const std::uint64_t interruptible_callback_base =
        ai_deadline_probe.deadlines.size();
    galaxy::guest_store_u16(
        guest_memory, 0xCC005036, 0x8002, nullptr, 0x80004000);
    const auto interruptible_started = memory.dsp_debug_snapshot();
    const std::uint64_t interruptible_submit_after_start =
        memory.ai_audio_submit_count();
    const std::uint64_t overdue_completion =
        interruptible_started.ai_dma_next_completion_ticks;
    const std::uint64_t late_callback_now =
        overdue_completion +
        memory.ai_dma_last_duration_ticks() * 18u + 123u;
    fake_ticks = late_callback_now;
    (void)galaxy::guest_load_u16(
        guest_memory, 0xCC00503A, nullptr, 0x80004000);
    const auto stopped_at_initial_irq = memory.dsp_debug_snapshot();
    passed &= expect(
        memory.ai_audio_submit_count() == interruptible_submit_after_start &&
            stopped_at_initial_irq.ai_dma_active_address == kAiBuffer &&
            stopped_at_initial_irq.ai_dma_next_completion_ticks ==
                overdue_completion &&
            memory.ai_dma_interrupt_pending(),
        "EE-enabled AI MMIO stops at the first guest-visible overdue boundary");
    passed &= expect(
        ai_deadline_probe.deadlines.size() ==
                interruptible_callback_base + 2u &&
            ai_deadline_probe.deadlines.back() == overdue_completion,
        "initial overdue IRQ republishes its still-pending completion deadline exactly once");

    ai_mmio_decision.decision = galaxy::host::AiDmaMmioServiceDecision::
        DeferForGuestInterruptHandler;
    acknowledge_ai_level();
    galaxy::guest_store_u16(
        guest_memory, 0xCC005030, 0x0010, nullptr, 0x80004000);
    galaxy::guest_store_u16(
        guest_memory, 0xCC005032, 0x0040, nullptr, 0x80004000);
    galaxy::guest_store_u16(
        guest_memory, 0xCC005036, 0x8002, nullptr, 0x80004000);
    const auto callback_shadow_written = memory.dsp_debug_snapshot();
    passed &= expect(
        memory.ai_audio_submit_count() == interruptible_submit_after_start &&
            callback_shadow_written.ai_dma_active_address == kAiBuffer &&
            callback_shadow_written.ai_dma_shadow_address == kNextAiBuffer &&
            callback_shadow_written.ai_dma_next_completion_ticks ==
                overdue_completion,
        "AI handler W1C and shadow MMIO cannot latch the stale active address");
    passed &= expect(
        ai_deadline_probe.deadlines.size() ==
            interruptible_callback_base + 2u,
        "deferred guest callback MMIO does not consume or replace the overdue completion wake");

    ai_mmio_decision.decision =
        galaxy::host::AiDmaMmioServiceDecision::Interruptible;
    memory.service_ai_dma(
        late_callback_now,
        galaxy::host::AiDmaServiceMode::Interruptible);
    const auto callback_completion_serviced = memory.dsp_debug_snapshot();
    passed &= expect(
        memory.ai_audio_submit_count() ==
                interruptible_submit_after_start + 1u &&
            callback_completion_serviced.ai_dma_active_address ==
                kNextAiBuffer &&
            callback_completion_serviced.ai_dma_next_completion_ticks >
                overdue_completion &&
            memory.ai_dma_interrupt_pending(),
        "late post-handler completion autoloads the callback-written address once and raises its own IRQ");
    passed &= expect(
        ai_deadline_probe.deadlines.size() ==
                interruptible_callback_base + 3u &&
            ai_deadline_probe.deadlines.back() ==
                callback_completion_serviced.ai_dma_next_completion_ticks,
        "post-handler autoload republishes the following exact DMA deadline");
    galaxy::guest_store_u16(
        guest_memory, 0xCC005036, 0x0000, nullptr, 0x80004000);
    acknowledge_ai_level();
    memory.set_ai_dma_mmio_service_decision_provider(nullptr, nullptr);

    memory.write_u32(0xCD800180, 0x13579BDF);
    passed &= expect(
        memory.read_u32(0x0D000180) == 0x13579BDF,
        "Hollywood canonical and second register mirrors share storage");

    // RMGE01 uses IOS /dev/di. The inaccurate direct Broadway DI scaffold is
    // fail-closed for every access shape that touches [0x6000,0x6028), and a
    // rejected write cannot mutate even the bytes outside the block that a
    // cross-boundary access also spans.
    for (const std::uint32_t alias :
         std::array{0x0C000000u, 0xCC000000u}) {
        constexpr std::uint32_t kBefore = 0x11223344u;
        constexpr std::uint32_t kBegin = 0x55667788u;
        constexpr std::uint32_t kEnd = 0x99AABBCCu;
        constexpr std::uint32_t kAfter = 0xDDEEFF00u;
        memory.write_u32(alias + 0x5FFCu, kBefore);
        memory.write_u32(alias + 0x6000u, kBegin);
        memory.write_u32(alias + 0x6024u, kEnd);
        memory.write_u32(alias + 0x6028u, kAfter);

        passed &= expect_runtime_error(
            [&] {
                static_cast<void>(galaxy::guest_load_u32(
                    guest_memory,
                    alias + 0x6000u,
                    nullptr,
                    0x80004000u));
            },
            "exact direct DI MMIO read hard-fails for both Broadway aliases");
        passed &= expect_runtime_error(
            [&] {
                static_cast<void>(galaxy::guest_load_u16(
                    guest_memory,
                    alias + 0x6000u,
                    nullptr,
                    0x80004000u));
            },
            "partial direct DI MMIO read hard-fails for both Broadway aliases");
        passed &= expect_runtime_error(
            [&] {
                static_cast<void>(galaxy::guest_load_u32(
                    guest_memory,
                    alias + 0x6001u,
                    nullptr,
                    0x80004000u));
            },
            "misaligned direct DI MMIO read hard-fails for both Broadway aliases");
        passed &= expect_runtime_error(
            [&] {
                static_cast<void>(galaxy::guest_load_u16(
                    guest_memory,
                    alias + 0x5FFFu,
                    nullptr,
                    0x80004000u));
            },
            "lower-crossing direct DI MMIO read hard-fails for both Broadway aliases");
        passed &= expect_runtime_error(
            [&] {
                static_cast<void>(galaxy::guest_load_u16(
                    guest_memory,
                    alias + 0x6027u,
                    nullptr,
                    0x80004000u));
            },
            "upper-crossing direct DI MMIO read hard-fails for both Broadway aliases");

        passed &= expect_runtime_error(
            [&] {
                galaxy::guest_store_u32(
                    guest_memory,
                    alias + 0x6000u,
                    0u,
                    nullptr,
                    0x80004000u);
            },
            "exact direct DI MMIO write hard-fails for both Broadway aliases");
        passed &= expect_runtime_error(
            [&] {
                galaxy::guest_store_u16(
                    guest_memory,
                    alias + 0x6000u,
                    0u,
                    nullptr,
                    0x80004000u);
            },
            "partial direct DI MMIO write hard-fails for both Broadway aliases");
        passed &= expect_runtime_error(
            [&] {
                galaxy::guest_store_u32(
                    guest_memory,
                    alias + 0x6001u,
                    0u,
                    nullptr,
                    0x80004000u);
            },
            "misaligned direct DI MMIO write hard-fails for both Broadway aliases");
        passed &= expect_runtime_error(
            [&] {
                galaxy::guest_store_u16(
                    guest_memory,
                    alias + 0x5FFFu,
                    0u,
                    nullptr,
                    0x80004000u);
            },
            "lower-crossing direct DI MMIO write hard-fails for both Broadway aliases");
        passed &= expect_runtime_error(
            [&] {
                galaxy::guest_store_u16(
                    guest_memory,
                    alias + 0x6027u,
                    0u,
                    nullptr,
                    0x80004000u);
            },
            "upper-crossing direct DI MMIO write hard-fails for both Broadway aliases");
        passed &= expect(
            memory.read_u32(alias + 0x5FFCu) == kBefore &&
                memory.read_u32(alias + 0x6000u) == kBegin &&
                memory.read_u32(alias + 0x6024u) == kEnd &&
                memory.read_u32(alias + 0x6028u) == kAfter,
            "rejected direct DI MMIO writes do not mutate backing bytes");
    }
    memory.write_u32(0xCD006014u, 0x13572468u);
    memory.write_u32(0xCD006024u, 0x24681357u);
    passed &= expect(
        galaxy::guest_load_u32(
            guest_memory, 0xCD006014u, nullptr, 0x80004000u) ==
                0x13572468u &&
            galaxy::guest_load_u32(
                guest_memory, 0xCD006024u, nullptr, 0x80004000u) ==
                0x24681357u,
        "Hollywood 0xCD006014/0xCD006024 reads remain outside direct Broadway DI retirement");

    const std::filesystem::path mod_test_root =
        std::filesystem::temp_directory_path() /
        "galaxy_native_host_tests_disc_mods";
    std::error_code mod_test_ec;
    std::filesystem::remove_all(mod_test_root, mod_test_ec);
    std::filesystem::create_directories(mod_test_root, mod_test_ec);
    passed &= expect(
        !mod_test_ec,
        "disc mod test directory can be created");

    std::vector<std::byte> pak_bytes(0x100);
    for (std::size_t i = 0; i < pak_bytes.size(); ++i) {
        pak_bytes[i] = static_cast<std::byte>(i & 0xFFu);
    }
    constexpr char kDiscModTestGameId[] = "RMGE01";
    for (std::size_t i = 0; i < 6u; ++i) {
        pak_bytes[i] = static_cast<std::byte>(kDiscModTestGameId[i]);
    }
    const std::filesystem::path pak_path = mod_test_root / "game.pak";
    write_binary_file(pak_path, pak_bytes);

    auto run_di_ios_read = [](
                               galaxy::host::GuestAddressSpace& target,
                               std::uint64_t disc_offset,
                               std::uint32_t destination,
                               std::uint32_t length) {
        constexpr std::uint32_t kPath = 0x133F0000u;
        constexpr std::uint32_t kOpenRequest = 0x133F0100u;
        constexpr std::uint32_t kCommand = 0x133F0200u;
        constexpr std::uint32_t kReadRequest = 0x133F0300u;
        galaxy::GuestMemoryV1* const target_memory = target.guest_memory();
        const std::uint32_t handle = ios_open_path(
            target, target_memory, kOpenRequest, kPath, "/dev/di");
        if (handle == 0u || handle >= 0x80000000u ||
            (disc_offset & 3u) != 0u) {
            return false;
        }
        target.write_u32(kCommand, 0x71000000u);
        target.write_u32(kCommand + 4u, length);
        target.write_u32(
            kCommand + 8u,
            static_cast<std::uint32_t>(disc_offset / 4u));
        target.write_u32(kReadRequest, 6u);
        target.write_u32(kReadRequest + 8u, handle);
        target.write_u32(kReadRequest + 0x0Cu, 0x71u);
        target.write_u32(kReadRequest + 0x10u, kCommand);
        target.write_u32(kReadRequest + 0x14u, 12u);
        target.write_u32(kReadRequest + 0x18u, destination);
        target.write_u32(kReadRequest + 0x1Cu, length);
        target.write_u32(kReadRequest + 0x20u, 0u);
        submit_ios_request(target_memory, kReadRequest);
        const bool completed = expect_ios_reply(
            target,
            target_memory,
            kReadRequest,
            1u,
            6u,
            "/dev/di IOS read posts a transfer-complete reply");
        acknowledge_ios_reply(target_memory);
        return completed;
    };

    {
        ScopedWideEnv clear_manifest(L"GALAXY_DISC_MOD_MANIFEST");
        galaxy::host::GuestAddressSpace pak_memory;
        pak_memory.open_game_pak(pak_path);
        constexpr std::uint64_t kPlainOffset = 0x30;
        constexpr std::uint32_t kPlainLength = 0x20;
        constexpr std::uint32_t kPlainDestination = 0x80102000;
        passed &= expect(
            run_di_ios_read(
                pak_memory,
                kPlainOffset,
                kPlainDestination,
                kPlainLength),
            "/dev/di IOS reads game.pak through Hollywood IPC");
        const auto* plain_bytes = static_cast<const std::byte*>(
            pak_memory.pointer(kPlainDestination, kPlainLength));
        passed &= expect(
            std::equal(
                plain_bytes,
                plain_bytes + kPlainLength,
                pak_bytes.data() + kPlainOffset),
            "/dev/di IOS reads game.pak bytes when no mod manifest is set");
    }

    std::vector<std::byte> override_bytes(0x10);
    for (std::size_t i = 0; i < override_bytes.size(); ++i) {
        override_bytes[i] = static_cast<std::byte>(0xA0u + i);
    }
    const std::filesystem::path override_path =
        mod_test_root / "mods" / "airship_patch.bin";
    write_binary_file(override_path, override_bytes);
    const std::filesystem::path manifest_path =
        mod_test_root / "disc_overrides.tsv";
    constexpr char kRmge01DolSha1[] =
        "9a71008ae1ee9010e267fa67d1f0b0d4f0e895dd";
    write_text_file(
        manifest_path,
        "version 1\n"
        "game RMGE01\n"
        "main_dol_sha1 " + std::string(kRmge01DolSha1) + "\n" +
        "0x40 0x10 mods/airship_patch.bin\n");

    {
        ScopedWideEnv set_manifest(
            L"GALAXY_DISC_MOD_MANIFEST",
            manifest_path);
        galaxy::host::GuestAddressSpace mod_memory;
        mod_memory.open_game_pak(pak_path);
        constexpr std::uint64_t kMixedOffset = 0x38;
        constexpr std::uint32_t kMixedLength = 0x20;
        constexpr std::uint32_t kMixedDestination = 0x80103000;
        passed &= expect(
            run_di_ios_read(
                mod_memory,
                kMixedOffset,
                kMixedDestination,
                kMixedLength),
            "/dev/di IOS applies disc overrides through Hollywood IPC");
        const auto* mixed_bytes = static_cast<const std::byte*>(
            mod_memory.pointer(kMixedDestination, kMixedLength));
        passed &= expect(
            std::equal(
                mixed_bytes,
                mixed_bytes + 0x08,
                pak_bytes.data() + kMixedOffset),
            "disc mod read keeps game.pak prefix bytes before an override");
        passed &= expect(
            std::equal(
                mixed_bytes + 0x08,
                mixed_bytes + 0x18,
                override_bytes.data()),
            "disc mod read substitutes override bytes for the covered range");
        passed &= expect(
            std::equal(
                mixed_bytes + 0x18,
                mixed_bytes + kMixedLength,
                pak_bytes.data() + 0x50),
            "disc mod read falls back to game.pak after an override");
    }

    const std::filesystem::path bad_manifest_path =
        mod_test_root / "bad_disc_overrides.tsv";
    write_text_file(
        bad_manifest_path,
        "version 1\n"
        "game RMGE01\n"
        "main_dol_sha1 " + std::string(kRmge01DolSha1) + "\n" +
        "0x20 0x20 mods/airship_patch.bin\n");
    {
        ScopedWideEnv set_bad_manifest(
            L"GALAXY_DISC_MOD_MANIFEST",
            bad_manifest_path);
        galaxy::host::GuestAddressSpace bad_memory;
        passed &= expect_runtime_error(
            [&] { bad_memory.open_game_pak(pak_path); },
            "disc mod manifest size mismatches hard-fail");
    }

    const std::filesystem::path missing_version_manifest_path =
        mod_test_root / "missing_version_disc_overrides.tsv";
    write_text_file(
        missing_version_manifest_path,
        "0x40 0x10 mods/airship_patch.bin\n");
    {
        ScopedWideEnv set_missing_version_manifest(
            L"GALAXY_DISC_MOD_MANIFEST",
            missing_version_manifest_path);
        galaxy::host::GuestAddressSpace missing_version_memory;
        passed &= expect_runtime_error(
            [&] { missing_version_memory.open_game_pak(pak_path); },
            "disc mod manifest requires a version declaration");
    }

    const std::filesystem::path unsupported_version_manifest_path =
        mod_test_root / "unsupported_version_disc_overrides.tsv";
    write_text_file(
        unsupported_version_manifest_path,
        "version 999\n"
        "0x40 0x10 mods/airship_patch.bin\n");
    {
        ScopedWideEnv set_unsupported_version_manifest(
            L"GALAXY_DISC_MOD_MANIFEST",
            unsupported_version_manifest_path);
        galaxy::host::GuestAddressSpace unsupported_version_memory;
        passed &= expect_runtime_error(
            [&] { unsupported_version_memory.open_game_pak(pak_path); },
            "unsupported disc mod manifest version hard-fails");
    }

    const std::filesystem::path missing_game_manifest_path =
        mod_test_root / "missing_game_disc_overrides.tsv";
    write_text_file(
        missing_game_manifest_path,
        "version 1\n"
        "0x40 0x10 mods/airship_patch.bin\n");
    {
        ScopedWideEnv set_missing_game_manifest(
            L"GALAXY_DISC_MOD_MANIFEST",
            missing_game_manifest_path);
        galaxy::host::GuestAddressSpace missing_game_memory;
        passed &= expect_runtime_error(
            [&] { missing_game_memory.open_game_pak(pak_path); },
            "disc mod manifest requires a game declaration");
    }

    const std::filesystem::path unsupported_game_manifest_path =
        mod_test_root / "unsupported_game_disc_overrides.tsv";
    write_text_file(
        unsupported_game_manifest_path,
        "version 1\n"
        "game TEST01\n"
        "0x40 0x10 mods/airship_patch.bin\n");
    {
        ScopedWideEnv set_unsupported_game_manifest(
            L"GALAXY_DISC_MOD_MANIFEST",
            unsupported_game_manifest_path);
        galaxy::host::GuestAddressSpace unsupported_game_memory;
        passed &= expect_runtime_error(
            [&] { unsupported_game_memory.open_game_pak(pak_path); },
            "unsupported disc mod game id hard-fails");
    }

    const std::filesystem::path missing_sha1_manifest_path =
        mod_test_root / "missing_sha1_disc_overrides.tsv";
    write_text_file(
        missing_sha1_manifest_path,
        "version 1\n"
        "game RMGE01\n"
        "0x40 0x10 mods/airship_patch.bin\n");
    {
        ScopedWideEnv set_missing_sha1_manifest(
            L"GALAXY_DISC_MOD_MANIFEST",
            missing_sha1_manifest_path);
        galaxy::host::GuestAddressSpace missing_sha1_memory;
        passed &= expect_runtime_error(
            [&] { missing_sha1_memory.open_game_pak(pak_path); },
            "disc mod manifest requires a supported main.dol SHA-1 declaration");
    }

    const std::filesystem::path unsupported_sha1_manifest_path =
        mod_test_root / "unsupported_sha1_disc_overrides.tsv";
    write_text_file(
        unsupported_sha1_manifest_path,
        "version 1\n"
        "game RMGE01\n"
        "main_dol_sha1 0000000000000000000000000000000000000000\n"
        "0x40 0x10 mods/airship_patch.bin\n");
    {
        ScopedWideEnv set_unsupported_sha1_manifest(
            L"GALAXY_DISC_MOD_MANIFEST",
            unsupported_sha1_manifest_path);
        galaxy::host::GuestAddressSpace unsupported_sha1_memory;
        passed &= expect_runtime_error(
            [&] { unsupported_sha1_memory.open_game_pak(pak_path); },
            "unsupported disc mod main.dol SHA-1 hard-fails");
    }
    const auto rooted_manifest_path = mod_test_root / "rooted_disc_overrides.tsv";
    for (const std::string& rooted_path : std::array<std::string, 4>{
            R"(\outside.bin)", "D:outside.bin", R"(\\server\share\outside.bin)",
            mod_test_root.root_name().string() + "mods/airship_patch.bin"}) {
        write_text_file(rooted_manifest_path,
            "version 1\ngame RMGE01\nmain_dol_sha1 " +
            std::string(kRmge01DolSha1) + "\n0x40 0x10 " + rooted_path + "\n");
        ScopedWideEnv set_rooted_manifest(L"GALAXY_DISC_MOD_MANIFEST", rooted_manifest_path);
        auto rooted_memory = std::make_unique<galaxy::host::GuestAddressSpace>();
        bool rejected_root = false;
        try { rooted_memory->open_game_pak(pak_path); }
        catch (const std::runtime_error& error) {
            rejected_root = std::string_view(error.what()).find(
                "path escapes manifest directory") != std::string_view::npos;
        }
        passed &= expect(rejected_root,
            "root-directory, drive-relative, and UNC manifest paths fail the containment contract");
    }
    std::filesystem::remove_all(mod_test_root, mod_test_ec);

    // ── DVD /dev/di backend tests ─────────────────────────────────────────
    // Open /dev/di and verify that DVDLowReadDiskID returns the stored header.
    constexpr std::array<std::byte, 7> di_path_bytes{
        std::byte{'/'}, std::byte{'d'}, std::byte{'e'}, std::byte{'v'},
        std::byte{'/'}, std::byte{'d'}, std::byte{'i'},
    };
    // Store a synthetic disc ID and a NUL-terminated device path.
    constexpr std::uint32_t kDiPath = 0x133E1700;
    constexpr std::uint32_t kDiOpenReq = 0x133E1780;
    memory.copy(kDiPath, di_path_bytes);
    // NUL terminator.
    *reinterpret_cast<std::uint8_t*>(memory.pointer(kDiPath + 7, 1)) = 0;
    const std::filesystem::path di_test_root =
        std::filesystem::temp_directory_path() /
        L"galaxy_native_host_tests_di";
    std::error_code di_test_ec;
    std::filesystem::remove_all(di_test_root, di_test_ec);
    std::filesystem::create_directories(di_test_root, di_test_ec);
    passed &= expect(
        !di_test_ec,
        "DI test directory can be created");
    std::vector<std::byte> di_pak_bytes(0x400);
    for (std::size_t i = 0; i < di_pak_bytes.size(); ++i) {
        di_pak_bytes[i] = static_cast<std::byte>((i * 3u) & 0xFFu);
    }
    const std::filesystem::path di_pak_path = di_test_root / "game.pak";
    write_binary_file(di_pak_path, di_pak_bytes);
    memory.open_game_pak(di_pak_path);
    // Set a synthetic 0x20-byte disc ID ("TESTID01" padded with zeros).
    const std::array<std::byte, 0x20> fake_disc_id = {
        std::byte{'T'}, std::byte{'E'}, std::byte{'S'}, std::byte{'T'},
        std::byte{'I'}, std::byte{'D'}, std::byte{'0'}, std::byte{'1'},
    };
    memory.set_disc_id(fake_disc_id);

    // Submit the open request for /dev/di.
    memory.write_u32(kDiOpenReq, 1);             // kIosOpenCommand
    memory.write_u32(kDiOpenReq + 0x08, 0);      // handle (unused for open)
    memory.write_u32(kDiOpenReq + 0x0C, kDiPath);
    memory.write_u32(kDiOpenReq + 0x20, 0);      // synchronous (no callback)
    submit_ios_request(guest_memory, kDiOpenReq);
    const std::uint32_t di_handle = memory.read_u32(kDiOpenReq + 4);
    passed &= expect(di_handle >= 1, "/dev/di open returns a valid handle");
    passed &= expect(
        expect_ios_reply(
            memory,
            guest_memory,
            kDiOpenReq,
            di_handle,
            1,
            "/dev/di open posts an IOS reply"),
        "/dev/di open posts an IOS reply");
    acknowledge_ios_reply(guest_memory);

    const auto submit_di_no_output = [&memory, guest_memory, di_handle](
                                         std::uint32_t request,
                                         std::uint32_t command_buffer,
                                         std::uint32_t command,
                                         const char* message) {
        memory.write_u32(command_buffer, command << 24u);
        memory.write_u32(command_buffer + 4u, 0u);
        memory.write_u32(command_buffer + 8u, 0u);
        memory.write_u32(request, 6u);
        memory.write_u32(request + 8u, di_handle);
        memory.write_u32(request + 0x0Cu, command);
        memory.write_u32(request + 0x10u, command_buffer);
        memory.write_u32(request + 0x14u, 12u);
        memory.write_u32(request + 0x18u, 0u);
        memory.write_u32(request + 0x1Cu, 0u);
        memory.write_u32(request + 0x20u, 0u);
        submit_ios_request(guest_memory, request);
        const bool completed = expect_ios_reply(
            memory,
            guest_memory,
            request,
            1u,
            6u,
            message);
        acknowledge_ios_reply(guest_memory);
        return completed;
    };
    passed &= expect(
        submit_di_no_output(
            0x133E1D00u,
            0x133E2000u,
            0x8Au,
            "DVDLowReset ioctl 0x8A posts transfer-complete"),
        "DVDLowReset uses RMGE01 ioctl 0x8A");
    passed &= expect(
        submit_di_no_output(
            0x133E1D40u,
            0x133E2020u,
            0xABu,
            "DVDLowSeek ioctl 0xAB posts transfer-complete"),
        "DVDLowSeek uses RMGE01 ioctl 0xAB");
    passed &= expect(
        submit_di_no_output(
            0x133E1D80u,
            0x133E2040u,
            0xDDu,
            "DVDLowSetMaximumRotation ioctl 0xDD posts transfer-complete"),
        "DVDLowSetMaximumRotation uses RMGE01 ioctl 0xDD");

    // Build a DVDLowReadDiskID IOCTL request.
    constexpr std::uint32_t kDiIoctlReq = 0x133E1800;
    constexpr std::uint32_t kDiOutBuf = 0x133E1880;
    // Zero the output buffer so the test can verify the fill.
    memory.clear(kDiOutBuf, 0x20);
    memory.write_u32(kDiIoctlReq, 6);              // kIosIoctlCommand
    memory.write_u32(kDiIoctlReq + 0x08, di_handle);
    memory.write_u32(kDiIoctlReq + 0x0C, 0x70);    // DVDLowReadDiskID ioctl
    memory.write_u32(kDiIoctlReq + 0x10, 0);       // in_buf (not needed)
    memory.write_u32(kDiIoctlReq + 0x14, 0);       // in_len
    memory.write_u32(kDiIoctlReq + 0x18, kDiOutBuf);
    memory.write_u32(kDiIoctlReq + 0x1C, 0x20);    // out_len
    memory.write_u32(kDiIoctlReq + 0x20, 0);       // synchronous
    submit_ios_request(guest_memory, kDiIoctlReq);
    passed &= expect(
        memory.read_u32(kDiIoctlReq + 4) == 1,
        "DVDLowReadDiskID returns DI transfer-complete");
    passed &= expect(
        expect_ios_reply(
            memory,
            guest_memory,
            kDiIoctlReq,
            1,
            6,
            "DVDLowReadDiskID posts an IOS reply"),
        "DVDLowReadDiskID posts an IOS reply");

    constexpr std::uint32_t kDiShortDiskIdReq = 0x133E18C0;
    memory.write_u32(kDiShortDiskIdReq, 6);
    memory.write_u32(kDiShortDiskIdReq + 0x08, di_handle);
    memory.write_u32(kDiShortDiskIdReq + 0x0C, 0x70);
    memory.write_u32(kDiShortDiskIdReq + 0x10, 0);
    memory.write_u32(kDiShortDiskIdReq + 0x14, 0);
    memory.write_u32(kDiShortDiskIdReq + 0x18, kDiOutBuf);
    memory.write_u32(kDiShortDiskIdReq + 0x1C, 0x1F);
    memory.write_u32(kDiShortDiskIdReq + 0x20, 0);
    submit_ios_request(guest_memory, kDiShortDiskIdReq);
    const galaxy::host::IpcDebugSnapshot queued_after_disk_id =
        memory.ipc_debug_snapshot();
    passed &= expect(
        queued_after_disk_id.latched_reply == kDiIoctlReq &&
            queued_after_disk_id.next_pending_reply == kDiShortDiskIdReq,
        "second DI reply queues behind the currently latched reply");
    memory.begin_ipc_interrupt_handler_pass();
    passed &= expect(
        galaxy::guest_load_u32(
            guest_memory, 0xCD000008, nullptr, 0x80004000) == kDiIoctlReq,
        "IPC handler reads the latched DI reply once");
    acknowledge_ios_reply_register_only(guest_memory);
    const galaxy::host::IpcDebugSnapshot after_early_y2_ack =
        memory.ipc_debug_snapshot();
    passed &= expect(
        after_early_y2_ack.latched_reply == kDiIoctlReq &&
            after_early_y2_ack.current_reply == kDiIoctlReq &&
            after_early_y2_ack.next_pending_reply == kDiShortDiskIdReq &&
            !ios_reply_available(guest_memory),
        "DI reply is not retired by PPCCTRL.Y2 before IPC IRQ flag clear");
    passed &= expect(
        galaxy::guest_load_u32(
            guest_memory, 0xCD000008, nullptr, 0x80004000) == 0u,
        "replayed IPC handler cannot reread an already-acked DI reply");
    acknowledge_ios_reply_register_only(guest_memory);
    passed &= expect(
        memory.ipc_debug_snapshot().next_pending_reply == kDiShortDiskIdReq,
        "replayed PPCCTRL.Y2 ack cannot advance the next DI reply");
    acknowledge_ios_reply(guest_memory);
    passed &= expect(
        memory.ipc_debug_snapshot().latched_reply == kDiIoctlReq &&
            memory.ipc_debug_snapshot().next_pending_reply == kDiShortDiskIdReq,
        "DI latch release inside the IPC handler stays deferred until handler pass return");
    passed &= expect(
        galaxy::guest_load_u32(
            guest_memory, 0xCD000008, nullptr, 0x80004000) == 0u,
        "replayed IPC handler cannot reread a DI reply after IRQ flag clear");
    memory.end_ipc_interrupt_handler_pass();
    passed &= expect(
        expect_ios_reply(
            memory,
            guest_memory,
            kDiShortDiskIdReq,
            2,
            6,
            "DVDLowReadDiskID short output posts after the handler pass returns"),
        "DI latch release posts the queued reply after the handler pass returns");
    acknowledge_ios_reply(guest_memory);

    constexpr std::uint32_t kDiAsyncReadReq = 0x133E1C00;
    constexpr std::uint32_t kDiAsyncReadCommand = 0x133E1C80;
    constexpr std::uint32_t kDiAsyncReadOut = 0x133E1CC0;
    memory.clear(kDiAsyncReadOut, 0x20);
    memory.write_u32(kDiAsyncReadCommand + 0x00, 0x71000000u);
    memory.write_u32(kDiAsyncReadCommand + 0x04, 0x20u);
    memory.write_u32(kDiAsyncReadCommand + 0x08, 0u);
    memory.write_u32(kDiAsyncReadReq, 6);
    memory.write_u32(kDiAsyncReadReq + 0x04, 0xDEADBEEFu);
    memory.write_u32(kDiAsyncReadReq + 0x08, di_handle);
    memory.write_u32(kDiAsyncReadReq + 0x0C, 0x71);
    memory.write_u32(kDiAsyncReadReq + 0x10, kDiAsyncReadCommand);
    memory.write_u32(kDiAsyncReadReq + 0x14, 12);
    memory.write_u32(kDiAsyncReadReq + 0x18, kDiAsyncReadOut);
    memory.write_u32(kDiAsyncReadReq + 0x1C, 0x20u);
    memory.write_u32(kDiAsyncReadReq + 0x20, 0x80401000u);
    memory.write_u32(kDiAsyncReadReq + 0x24, 0x81234000u);
    memory.begin_ipc_interrupt_handler_pass();
    submit_ios_request(guest_memory, kDiAsyncReadReq);
    passed &= expect(
        (ipc_control(guest_memory) & 0x02u) != 0u,
        "async DI submit inside IPC handler still raises IOS ack");
    passed &= expect(
        !ios_reply_available(guest_memory) &&
            memory.ipc_debug_snapshot().latched_reply == 0 &&
            memory.ipc_debug_snapshot().pending_reply_count == 0,
        "async DI completion submitted inside IPC handler is deferred");
    passed &= expect(
        memory.read_u32(kDiAsyncReadReq) == 6 &&
            memory.read_u32(kDiAsyncReadReq + 4) == 0xDEADBEEFu,
        "deferred async DI reply does not rewrite the live request yet");
    passed &= expect(
        std::memcmp(
            memory.pointer(kDiAsyncReadOut, 0x20),
            di_pak_bytes.data(),
            0x20) == 0,
        "async DI read still copies game.pak bytes immediately");
    acknowledge_ios_ack(guest_memory);
    memory.end_ipc_interrupt_handler_pass();
    passed &= expect(
        expect_ios_reply(
            memory,
            guest_memory,
            kDiAsyncReadReq,
            1,
            6,
            "deferred async DI completion posts after IPC handler return"),
        "deferred async DI completion posts after IPC handler return");
    acknowledge_ios_reply(guest_memory);
    // Verify the disc ID was written to the output buffer.
    passed &= expect(
        memory.pointer(kDiOutBuf, 4)[0] == std::byte{'T'} &&
            memory.pointer(kDiOutBuf, 4)[1] == std::byte{'E'} &&
            memory.pointer(kDiOutBuf, 4)[2] == std::byte{'S'} &&
            memory.pointer(kDiOutBuf, 4)[3] == std::byte{'T'},
        "DVDLowReadDiskID writes the stored disc ID to the output buffer");

    passed &= expect(
        memory.read_u32(kDiShortDiskIdReq + 4) == 2,
        "DVDLowReadDiskID rejects a short output buffer");

    // DVDLowInquiry should succeed and write drive version info.
    constexpr std::uint32_t kDiInqReq = 0x133E1900;
    constexpr std::uint32_t kDiInqBuf = 0x133E1980;
    memory.clear(kDiInqBuf, 12);
    memory.write_u32(kDiInqReq, 6);
    memory.write_u32(kDiInqReq + 0x08, di_handle);
    memory.write_u32(kDiInqReq + 0x0C, 0x12);       // DVDLowInquiry
    memory.write_u32(kDiInqReq + 0x10, 0);
    memory.write_u32(kDiInqReq + 0x14, 0);
    memory.write_u32(kDiInqReq + 0x18, kDiInqBuf);
    memory.write_u32(kDiInqReq + 0x1C, 12);
    memory.write_u32(kDiInqReq + 0x20, 0);
    submit_ios_request(guest_memory, kDiInqReq);
    passed &= expect(
        memory.read_u32(kDiInqReq + 4) == 1,
        "DVDLowInquiry returns DI transfer-complete");
    passed &= expect(
        memory.read_u32(kDiInqBuf + 4) == 0x20060526u,
        "DVDLowInquiry writes the drive code to the output buffer");
    passed &= expect(
        expect_ios_reply(
            memory,
            guest_memory,
            kDiInqReq,
            1,
            6,
            "DVDLowInquiry posts an IOS reply"),
        "DVDLowInquiry posts an IOS reply");
    acknowledge_ios_reply(guest_memory);

    constexpr std::uint32_t kDiNullInqReq = 0x133E19C0;
    memory.write_u32(kDiNullInqReq, 6);
    memory.write_u32(kDiNullInqReq + 0x08, di_handle);
    memory.write_u32(kDiNullInqReq + 0x0C, 0x12);
    memory.write_u32(kDiNullInqReq + 0x10, 0);
    memory.write_u32(kDiNullInqReq + 0x14, 0);
    memory.write_u32(kDiNullInqReq + 0x18, 0);
    memory.write_u32(kDiNullInqReq + 0x1C, 12);
    memory.write_u32(kDiNullInqReq + 0x20, 0);
    submit_ios_request(guest_memory, kDiNullInqReq);
    passed &= expect(
        memory.read_u32(kDiNullInqReq + 4) == 2,
        "DVDLowInquiry rejects a null output buffer");
    passed &= expect(
        expect_ios_reply(
            memory,
            guest_memory,
            kDiNullInqReq,
            2,
            6,
            "DVDLowInquiry null output posts a drive-error reply"),
        "DVDLowInquiry null output posts a drive-error reply");
    acknowledge_ios_reply(guest_memory);

    constexpr std::uint32_t kDiReadReq = 0x133E1A00;
    constexpr std::uint32_t kDiReadCommand = 0x133E1A80;
    memory.write_u32(kDiReadCommand + 0x00, 0x71000000u);
    memory.write_u32(kDiReadCommand + 0x04, 0x20u);
    memory.write_u32(kDiReadCommand + 0x08, 0u);
    memory.write_u32(kDiReadReq, 6);
    memory.write_u32(kDiReadReq + 0x08, di_handle);
    memory.write_u32(kDiReadReq + 0x0C, 0x71);
    memory.write_u32(kDiReadReq + 0x10, kDiReadCommand);
    memory.write_u32(kDiReadReq + 0x14, 12);
    memory.write_u32(kDiReadReq + 0x18, 0);
    memory.write_u32(kDiReadReq + 0x1C, 0x20u);
    memory.write_u32(kDiReadReq + 0x20, 0);
    submit_ios_request(guest_memory, kDiReadReq);
    passed &= expect(
        memory.read_u32(kDiReadReq + 4) == 2,
        "DVDLowRead rejects a null output buffer");
    passed &= expect(
        expect_ios_reply(
            memory,
            guest_memory,
            kDiReadReq,
            2,
            6,
            "DVDLowRead null output posts a drive-error reply"),
        "DVDLowRead null output posts a drive-error reply");
    acknowledge_ios_reply(guest_memory);

    constexpr std::uint32_t kDiReadvReq = 0x133E1B00;
    constexpr std::uint32_t kDiReadvVectors = 0x133E1B80;
    memory.write_u32(kDiReadvVectors + 0x00, kDiReadCommand);
    memory.write_u32(kDiReadvVectors + 0x04, 12);
    memory.write_u32(kDiReadvVectors + 0x08, kDiOutBuf);
    memory.write_u32(kDiReadvVectors + 0x0C, 0x1Fu);
    memory.write_u32(kDiReadvReq, 7);
    memory.write_u32(kDiReadvReq + 0x08, di_handle);
    memory.write_u32(kDiReadvReq + 0x0C, 0x71);
    memory.write_u32(kDiReadvReq + 0x10, 1);
    memory.write_u32(kDiReadvReq + 0x14, 1);
    memory.write_u32(kDiReadvReq + 0x18, kDiReadvVectors);
    memory.write_u32(kDiReadvReq + 0x20, 0);
    submit_ios_request(guest_memory, kDiReadvReq);
    passed &= expect(
        memory.read_u32(kDiReadvReq + 4) == 2,
        "DVDLowRead ioctlv rejects a short output buffer");
    passed &= expect(
        expect_ios_reply(
            memory,
            guest_memory,
            kDiReadvReq,
            2,
            7,
            "DVDLowRead ioctlv short output posts a drive-error reply"),
        "DVDLowRead ioctlv short output posts a drive-error reply");
    acknowledge_ios_reply(guest_memory);

    // The first output vector follows all input vectors, not always input0.
    memory.clear(kDiOutBuf, 0x20u);
    memory.write_u32(kDiReadvVectors + 0x08u, 0u);
    memory.write_u32(kDiReadvVectors + 0x0Cu, 0u);
    memory.write_u32(kDiReadvVectors + 0x10u, kDiOutBuf);
    memory.write_u32(kDiReadvVectors + 0x14u, 0x20u);
    ios_ioctlv_request(memory, guest_memory, kDiReadvReq, di_handle,
        0x71u, 2u, 1u, kDiReadvVectors);
    passed &= expect(memory.read_u32(kDiReadvReq + 4u) == 1u &&
        std::memcmp(memory.pointer(kDiOutBuf, 0x20u), di_pak_bytes.data(), 0x20u) == 0,
        "DVDLowRead ioctlv selects output0 after two input descriptors");
    acknowledge_ios_reply(guest_memory);
    ios_ioctlv_request(memory, guest_memory, kDiReadvReq, di_handle,
        0x71u, std::numeric_limits<std::uint32_t>::max(), 1u, kDiReadvVectors);
    passed &= expect(memory.read_u32(kDiReadvReq + 4u) == 2u &&
        ios_reply_available(guest_memory),
        "DVDLowRead rejects vector-count extent overflow without wrapping");
    acknowledge_ios_reply(guest_memory);

    std::filesystem::remove_all(di_test_root, di_test_ec);

    constexpr std::uint32_t kSystemPath = 0x133E3000;
    constexpr std::uint32_t kSystemReq = 0x133E3100;
    constexpr std::uint32_t kSystemBuf = 0x133E3200;
    const std::uint32_t sysconf_handle = ios_open_path(
        memory,
        guest_memory,
        kSystemReq,
        kSystemPath,
        "/shared2/sys/SYSCONF",
        1u);
    passed &= expect(
        sysconf_handle > 0 && sysconf_handle < 0x80000000u,
        "modeled SYSCONF opens as a host-backed NAND file");
    memory.clear(kSystemBuf, 0x100);
    ios_read_request(
        memory, guest_memory, kSystemReq + 0x40, sysconf_handle, kSystemBuf, 4);
    passed &= expect(
        memory.read_u32(kSystemReq + 0x44) == 4,
        "SYSCONF read returns real byte count");
    passed &= expect(
        memory.pointer(kSystemBuf, 4)[0] == std::byte{'S'} &&
            memory.pointer(kSystemBuf, 4)[1] == std::byte{'C'} &&
            memory.pointer(kSystemBuf, 4)[2] == std::byte{'v'} &&
            memory.pointer(kSystemBuf, 4)[3] == std::byte{'0'},
        "SYSCONF starts with the SCv0 header");
    acknowledge_ios_reply(guest_memory);
    ios_seek_request(
        memory, guest_memory, kSystemReq + 0x80, sysconf_handle, 0x3FFCu, 0);
    passed &= expect(
        memory.read_u32(kSystemReq + 0x84) == 0x3FFCu,
        "SYSCONF seek updates file position");
    acknowledge_ios_reply(guest_memory);
    ios_read_request(
        memory, guest_memory, kSystemReq + 0xC0, sysconf_handle, kSystemBuf, 4);
    passed &= expect(
        memory.pointer(kSystemBuf, 4)[0] == std::byte{'S'} &&
            memory.pointer(kSystemBuf, 4)[1] == std::byte{'C'} &&
            memory.pointer(kSystemBuf, 4)[2] == std::byte{'e'} &&
            memory.pointer(kSystemBuf, 4)[3] == std::byte{'d'},
        "SYSCONF ends with the SCed footer");
    acknowledge_ios_reply(guest_memory);

    {
        const std::filesystem::path sysconf_path =
            nand_test_root / L"shared2" / L"sys" / L"SYSCONF";
        std::ifstream sysconf_file(sysconf_path, std::ios::binary);
        std::vector<std::byte> sysconf(0x4000, std::byte{0});
        if (sysconf_file) {
            sysconf_file.read(
                reinterpret_cast<char*>(sysconf.data()),
                static_cast<std::streamsize>(sysconf.size()));
        }
        passed &= expect(
            sysconf_file && sysconf_file.gcount() == 0x4000,
            "modeled SYSCONF host file is complete");

        const auto read16 = [&](std::size_t offset) -> std::uint16_t {
            return static_cast<std::uint16_t>(
                (std::to_integer<std::uint16_t>(sysconf[offset]) << 8u) |
                std::to_integer<std::uint16_t>(sysconf[offset + 1u]));
        };
        const auto find_bigarray =
            [&](std::string_view name,
                std::size_t expected_size,
                std::size_t& out_offset) -> bool {
            const std::uint16_t count = read16(4);
            for (std::uint16_t i = 0; i < count; ++i) {
                const std::uint16_t item_offset = read16(6u + i * 2u);
                const std::uint8_t header =
                    std::to_integer<std::uint8_t>(sysconf[item_offset]);
                const std::uint8_t type = header >> 5u;
                const std::uint8_t name_length =
                    static_cast<std::uint8_t>((header & 0x1Fu) + 1u);
                if (type != 1u || name_length != name.size()) {
                    continue;
                }
                bool name_matches = true;
                for (std::size_t j = 0; j < name.size(); ++j) {
                    if (sysconf[item_offset + 1u + j] !=
                        static_cast<std::byte>(
                            static_cast<std::uint8_t>(name[j]))) {
                        name_matches = false;
                        break;
                    }
                }
                if (!name_matches) {
                    continue;
                }
                const std::size_t length_offset =
                    item_offset + 1u + name_length;
                const std::size_t value_size =
                    static_cast<std::size_t>(read16(length_offset)) + 1u;
                if (value_size != expected_size) {
                    return false;
                }
                out_offset = length_offset + 2u;
                return true;
            }
            return false;
        };
        const auto bytes_equal =
            [&](std::size_t offset, std::span<const std::byte> expected) {
            return std::equal(
                expected.begin(), expected.end(), sysconf.begin() + offset);
        };
        const auto ascii_equal =
            [&](std::size_t offset, std::string_view expected) {
            for (std::size_t i = 0; i < expected.size(); ++i) {
                if (sysconf[offset + i] !=
                    static_cast<std::byte>(
                        static_cast<std::uint8_t>(expected[i]))) {
                    return false;
                }
            }
            return sysconf[offset + expected.size()] == std::byte{0};
        };
        constexpr std::array<std::byte, 6> kExpectedSysconfWiimoteBd{
            std::byte{0x00}, std::byte{0x00}, std::byte{0x79},
            std::byte{0x19}, std::byte{0x02}, std::byte{0x11}};
        constexpr std::array<std::byte, 16> kExpectedLinkKey{
            std::byte{0xA0}, std::byte{0xA0}, std::byte{0xA0},
            std::byte{0xA0}, std::byte{0xA0}, std::byte{0xA0},
            std::byte{0xA0}, std::byte{0xA0}, std::byte{0xA0},
            std::byte{0xA0}, std::byte{0xA0}, std::byte{0xA0},
            std::byte{0xA0}, std::byte{0xA0}, std::byte{0xA0},
            std::byte{0xA0}};
        constexpr std::string_view kExpectedName = "Nintendo RVL-CNT-01";
        std::size_t dinf = 0;
        std::size_t cdif = 0;
        passed &= expect(
            find_bigarray("BT.DINF", 0x461, dinf),
            "SYSCONF contains BT.DINF with expected size");
        passed &= expect(
            find_bigarray("BT.CDIF", 0x205, cdif),
            "SYSCONF contains BT.CDIF with expected size");
        passed &= expect(
            sysconf[dinf] == std::byte{1} &&
                bytes_equal(dinf + 0x01, kExpectedSysconfWiimoteBd) &&
                ascii_equal(dinf + 0x07, kExpectedName) &&
                bytes_equal(dinf + 0x27, kExpectedLinkKey) &&
                bytes_equal(dinf + 0x02BD, kExpectedSysconfWiimoteBd) &&
                ascii_equal(dinf + 0x02C3, kExpectedName) &&
                bytes_equal(dinf + 0x02E3, kExpectedLinkKey),
            "SYSCONF BT.DINF seeds the primary registered and active native Wiimote");
        passed &= expect(
            sysconf[cdif] == std::byte{1} &&
                bytes_equal(cdif + 0x01, kExpectedSysconfWiimoteBd) &&
                ascii_equal(cdif + 0x07, kExpectedName) &&
                bytes_equal(cdif + 0x47, kExpectedLinkKey),
            "SYSCONF BT.CDIF seeds the primary guest native Wiimote link key");
    }

    const std::uint32_t setting_handle = ios_open_path(
        memory,
        guest_memory,
        kSystemReq + 0x100,
        kSystemPath,
        "/title/00000001/00000002/data/setting.txt",
        1u);
    passed &= expect(
        setting_handle > 0 && setting_handle < 0x80000000u,
        "modeled setting.txt opens as a host-backed NAND file");
    ios_read_request(
        memory,
        guest_memory,
        kSystemReq + 0x140,
        setting_handle,
        kSystemBuf,
        0x100);
    passed &= expect(
        memory.read_u32(kSystemReq + 0x144) == 0x100,
        "setting.txt read returns its encoded 256-byte buffer");
    std::string decoded_setting;
    std::uint32_t setting_key = 0x73B5DBFAu;
    for (std::uint32_t i = 0; i < 0x100u; ++i) {
        const auto encoded = std::to_integer<std::uint8_t>(
            memory.pointer(kSystemBuf + i, 1)[0]);
        decoded_setting.push_back(static_cast<char>(
            encoded ^ static_cast<std::uint8_t>(setting_key)));
        setting_key = (setting_key >> 31u) | (setting_key << 1u);
    }
    passed &= expect(
        decoded_setting.find("AREA=USA") != std::string::npos &&
            decoded_setting.find("MODEL=RVL-001(USA)") != std::string::npos,
        "setting.txt decodes to modeled Wii region settings");
    acknowledge_ios_reply(guest_memory);

    const std::uint32_t play_rec_handle = ios_open_path(
        memory,
        guest_memory,
        kSystemReq + 0x180,
        kSystemPath,
        "/title/00000001/00000002/data/play_rec.dat",
        3u);
    passed &= expect(
        play_rec_handle > 0 && play_rec_handle < 0x80000000u,
        "modeled play_rec.dat opens as a host-backed NAND file");
    memory.clear(kSystemBuf, 0x100);
    ios_read_request(
        memory,
        guest_memory,
        kSystemReq + 0x1C0,
        play_rec_handle,
        kSystemBuf,
        0x80);
    passed &= expect(
        memory.read_u32(kSystemReq + 0x1C4) == 0x80,
        "play_rec.dat read returns its 128-byte record");
    acknowledge_ios_reply(guest_memory);
    ios_seek_request(
        memory, guest_memory, kSystemReq + 0x200, play_rec_handle, 0, 0);
    passed &= expect(
        memory.read_u32(kSystemReq + 0x204) == 0,
        "play_rec.dat seek rewinds the record");
    acknowledge_ios_reply(guest_memory);
    ios_write_request(
        memory,
        guest_memory,
        kSystemReq + 0x240,
        play_rec_handle,
        kSystemBuf,
        1);
    passed &= expect(
        memory.read_u32(kSystemReq + 0x244) == 1,
        "play_rec.dat mode-3 round-trip write is host-backed without corrupting its checksum");
    acknowledge_ios_reply(guest_memory);

    constexpr std::uint32_t kBadDiReq = 0x133E1A00;
    memory.write_u32(kBadDiReq, 6);
    memory.write_u32(kBadDiReq + 0x08, di_handle);
    memory.write_u32(kBadDiReq + 0x0C, 0x80u);
    memory.write_u32(kBadDiReq + 0x10, 0);
    memory.write_u32(kBadDiReq + 0x14, 0);
    memory.write_u32(kBadDiReq + 0x18, 0);
    memory.write_u32(kBadDiReq + 0x1C, 0);
    passed &= expect_runtime_error(
        [&] { submit_ios_request(guest_memory, kBadDiReq); },
        "retired incorrect DVDLowSeek ioctl 0x80 must hard-fail");

    constexpr std::uint32_t kFsPath = 0x133E1B00;
    constexpr std::uint32_t kFsOpenReq = 0x133E1B80;
    constexpr std::array<std::byte, 8> fs_path{
        std::byte{'/'}, std::byte{'d'}, std::byte{'e'}, std::byte{'v'},
        std::byte{'/'}, std::byte{'f'}, std::byte{'s'}, std::byte{0},
    };
    memory.copy(kFsPath, fs_path);
    memory.write_u32(kFsOpenReq, 1);
    memory.write_u32(kFsOpenReq + 0x0C, kFsPath);
    submit_ios_request(guest_memory, kFsOpenReq);
    const std::uint32_t fs_handle = memory.read_u32(kFsOpenReq + 4);

    constexpr std::uint32_t kFsOpenReq2 = 0x133E1BC0;
    memory.write_u32(kFsOpenReq2, 1);
    memory.write_u32(kFsOpenReq2 + 0x08, 0);
    memory.write_u32(kFsOpenReq2 + 0x0C, kFsPath);
    memory.write_u32(kFsOpenReq2 + 0x20, 0);
    submit_ios_request(guest_memory, kFsOpenReq2);
    const galaxy::host::IpcDebugSnapshot queued_after_fs_open =
        memory.ipc_debug_snapshot();
    passed &= expect(
        queued_after_fs_open.latched_reply == kFsOpenReq &&
            queued_after_fs_open.next_pending_reply == kFsOpenReq2,
        "second /dev/fs reply queues behind the currently latched reply");
    acknowledge_ios_reply_register_only(guest_memory);
    const galaxy::host::IpcDebugSnapshot after_fs_early_y2_ack =
        memory.ipc_debug_snapshot();
    passed &= expect(
        after_fs_early_y2_ack.latched_reply == kFsOpenReq &&
            after_fs_early_y2_ack.current_reply == kFsOpenReq &&
            after_fs_early_y2_ack.next_pending_reply == kFsOpenReq2 &&
            !ios_reply_available(guest_memory),
        "/dev/fs reply is not retired by PPCCTRL.Y2 before IPC IRQ flag clear");
    acknowledge_ios_reply_register_only(guest_memory);
    passed &= expect(
        memory.ipc_debug_snapshot().next_pending_reply == kFsOpenReq2,
        "replayed /dev/fs PPCCTRL.Y2 ack cannot advance the next reply");
    acknowledge_ios_reply(guest_memory);
    passed &= expect(
        expect_ios_reply(
            memory,
            guest_memory,
            kFsOpenReq2,
            memory.read_u32(kFsOpenReq2 + 4),
            1,
            "second /dev/fs open posts after IRQ flag clear"),
        "generic IPC latch release posts queued /dev/fs reply after IRQ flag clear");
    acknowledge_ios_reply(guest_memory);

    constexpr std::uint32_t kBadFsReq = 0x133E1C00;
    memory.write_u32(kBadFsReq, 6);
    memory.write_u32(kBadFsReq + 0x08, fs_handle);
    memory.write_u32(kBadFsReq + 0x0C, 0xABCDEFu);
    memory.write_u32(kBadFsReq + 0x10, 0);
    memory.write_u32(kBadFsReq + 0x14, 0);
    memory.write_u32(kBadFsReq + 0x18, 0);
    memory.write_u32(kBadFsReq + 0x1C, 0);
    passed &= expect_runtime_error(
        [&] { submit_ios_request(guest_memory, kBadFsReq); },
        "unknown /dev/fs ioctl must hard-fail");

    constexpr std::uint32_t kBadBtReq = 0x133E1D00;
    constexpr std::uint32_t kBadBtVectors = 0x133E1D80;
    memory.write_u32(kBadBtReq, 7);
    memory.write_u32(kBadBtReq + 0x08, bluetooth_handle);
    memory.write_u32(kBadBtReq + 0x0C, 0xFEEDu);
    memory.write_u32(kBadBtReq + 0x10, 0);
    memory.write_u32(kBadBtReq + 0x14, 0);
    memory.write_u32(kBadBtReq + 0x18, kBadBtVectors);
    passed &= expect_runtime_error(
        [&] { submit_ios_request(guest_memory, kBadBtReq); },
        "unknown Bluetooth ioctlv must hard-fail");

    constexpr std::uint32_t kStmImmediatePath = 0x133E1D90;
    constexpr std::uint32_t kStmImmediateOpenReq = 0x133E1DC0;
    constexpr std::uint32_t kStmImmediateReq = 0x133E1DF0;
    const std::uint32_t stm_immediate_handle = ios_open_path(
        memory,
        guest_memory,
        kStmImmediateOpenReq,
        kStmImmediatePath,
        "/dev/stm/immediate");
    memory.write_u32(kStmImmediateReq, 6);
    memory.write_u32(kStmImmediateReq + 0x08, stm_immediate_handle);
    memory.write_u32(kStmImmediateReq + 0x0C, 0x5001);
    memory.write_u32(kStmImmediateReq + 0x10, 0);
    memory.write_u32(kStmImmediateReq + 0x14, 0x20);
    memory.write_u32(kStmImmediateReq + 0x18, 0);
    memory.write_u32(kStmImmediateReq + 0x1C, 0x20);
    memory.write_u32(kStmImmediateReq + 0x20, 0);
    submit_ios_request(guest_memory, kStmImmediateReq);
    passed &= expect(
        expect_ios_reply(
            memory,
            guest_memory,
            kStmImmediateReq,
            0,
            6,
            "STM VI dimming ioctl replies successfully"),
        "STM VI dimming ioctl replies successfully");
    acknowledge_ios_reply(guest_memory);

    constexpr std::uint32_t kStmEventPath = 0x133E1E80;
    constexpr std::uint32_t kStmEventOpenReq = 0x133E1F00;
    constexpr std::uint32_t kStmEventReq = 0x133E1F80;
    const std::uint32_t stm_event_handle = ios_open_path(
        memory,
        guest_memory,
        kStmEventOpenReq,
        kStmEventPath,
        "/dev/stm/eventhook");
    memory.write_u32(kStmEventReq, 6);
    memory.write_u32(kStmEventReq + 0x08, stm_event_handle);
    memory.write_u32(kStmEventReq + 0x0C, 0x1000);
    memory.write_u32(kStmEventReq + 0x10, 0);
    memory.write_u32(kStmEventReq + 0x14, 0);
    memory.write_u32(kStmEventReq + 0x18, 0);
    memory.write_u32(kStmEventReq + 0x1C, 0);
    memory.write_u32(kStmEventReq + 0x20, 0);
    submit_ios_request(guest_memory, kStmEventReq);
    passed &= expect(
        (ipc_control(guest_memory) & 0x02u) != 0u &&
            !ios_reply_available(guest_memory),
        "STM eventhook ioctl is accepted but held pending until a real event");
    acknowledge_ios_ack(guest_memory);

    constexpr std::uint32_t kBadIosCommandReq = 0x133E1E00;
    memory.write_u32(kBadIosCommandReq, 0xFE);
    memory.write_u32(kBadIosCommandReq + 0x08, di_handle);
    memory.write_u32(kBadIosCommandReq + 0x20, 0);
    passed &= expect_runtime_error(
        [&] { submit_ios_request(guest_memory, kBadIosCommandReq); },
        "unknown IOS command must hard-fail");

    if (!passed) {
        return 1;
    }
    std::cout << "Native host loader tests passed\n";
    return 0;
    } catch (const std::exception& ex) {
        std::cerr << "FAILED: unhandled native host test exception: "
                  << ex.what() << '\n';
        return 1;
    }
}
