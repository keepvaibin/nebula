// The Bluetooth LE side of switch2_ble.h, through Windows' GATT API.
#include "galaxy/switch2_ble.h"

#include <windows.h>

#include <winrt/Windows.Devices.Bluetooth.Advertisement.h>
#include <winrt/Windows.Devices.Bluetooth.GenericAttributeProfile.h>
#include <winrt/Windows.Devices.Bluetooth.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Storage.Streams.h>

#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstring>
#include <deque>
#include <iostream>
#include <map>
#include <mutex>
#include <thread>

namespace galaxy::input::switch2_ble {

namespace {

namespace sw = switch_hid;
using namespace winrt::Windows::Devices::Bluetooth;
using namespace winrt::Windows::Devices::Bluetooth::Advertisement;
using namespace winrt::Windows::Devices::Bluetooth::GenericAttributeProfile;
using winrt::Windows::Storage::Streams::Buffer;
using winrt::Windows::Storage::Streams::IBuffer;

constexpr winrt::guid kControllerService{"ab7de9be-89fe-49ad-828f-118f09df7fd0"};
constexpr winrt::guid kCommonInput{"ab7de9be-89fe-49ad-828f-118f09df7fd2"};
constexpr winrt::guid kCommandWrite{"649d4ac9-8eb7-4e6c-af44-1ea54fe5f005"};
constexpr winrt::guid kCommandReply{"c765a961-d9d8-4d36-a20a-5315b111836a"};
constexpr winrt::guid kMotorLeft{"289326cb-a471-485d-a8f4-240c14f18241"};
constexpr winrt::guid kMotorRight{"fa19b0fb-cd1f-46a7-84a1-bbb09e00c149"};
constexpr winrt::guid kMotorPro{"cc483f51-9258-427d-a939-630c31f72b05"};

IBuffer to_buffer(std::span<const std::uint8_t> bytes) {
    Buffer buffer(static_cast<std::uint32_t>(bytes.size()));
    std::memcpy(buffer.data(), bytes.data(), bytes.size());
    buffer.Length(static_cast<std::uint32_t>(bytes.size()));
    return buffer;
}

class WinLink final : public Link {
public:
    // Connected and set up, or an exception or null.
    static std::shared_ptr<WinLink> connect(std::uint64_t address, std::uint16_t product, HANDLE changed) {
        auto link = std::make_shared<WinLink>();
        link->changed_ = changed;
        link->device_ = BluetoothLEDevice::FromBluetoothAddressAsync(address).get();
        if (link->device_ == nullptr) return nullptr;
        const auto services = link->device_.GetGattServicesForUuidAsync(kControllerService, BluetoothCacheMode::Uncached).get();
        if (services.Status() != GattCommunicationStatus::Success || services.Services().Size() == 0u) return nullptr;
        const auto characteristics = services.Services().GetAt(0).GetCharacteristicsAsync(BluetoothCacheMode::Uncached).get();
        if (characteristics.Status() != GattCommunicationStatus::Success) return nullptr;
        const winrt::guid motor = product == sw::kJoyCon2LeftProductId    ? kMotorLeft
                                  : product == sw::kJoyCon2RightProductId ? kMotorRight
                                                                          : kMotorPro;
        for (const auto& characteristic : characteristics.Characteristics()) {
            const winrt::guid id = characteristic.Uuid();
            if (id == kCommonInput) link->input_ = characteristic;
            else if (id == kCommandWrite) link->write_ = characteristic;
            else if (id == kCommandReply) link->reply_ = characteristic;
            else if (id == motor) link->motor_ = characteristic;
        }
        if (link->input_ == nullptr || link->write_ == nullptr || link->reply_ == nullptr) return nullptr;
        const std::weak_ptr<WinLink> weak = link;
        link->status_token_ = link->device_.ConnectionStatusChanged([weak](const BluetoothLEDevice& device, const auto&) {
            if (const auto self = weak.lock(); self && device.ConnectionStatus() == BluetoothConnectionStatus::Disconnected) self->lose();
        });
        link->reply_token_ = link->reply_.ValueChanged([weak](const GattCharacteristic&, const GattValueChangedEventArgs& args) {
            if (const auto self = weak.lock()) self->take_reply(args.CharacteristicValue());
        });
        if (link->reply_.WriteClientCharacteristicConfigurationDescriptorAsync(GattClientCharacteristicConfigurationDescriptorValue::Notify).get() !=
            GattCommunicationStatus::Success) {
            return nullptr;
        }
        link->read_calibration(product);
        for (const auto& command : set_up_commands()) link->exchange(command);
        link->input_token_ = link->input_.ValueChanged([weak](const GattCharacteristic&, const GattValueChangedEventArgs& args) {
            if (const auto self = weak.lock()) self->take_report(args.CharacteristicValue());
        });
        if (link->input_.WriteClientCharacteristicConfigurationDescriptorAsync(GattClientCharacteristicConfigurationDescriptorValue::Notify).get() !=
            GattCommunicationStatus::Success) {
            return nullptr;
        }
        return link;
    }

    ~WinLink() override {
        try {
            if (device_ != nullptr) device_.ConnectionStatusChanged(status_token_);
            if (reply_ != nullptr) reply_.ValueChanged(reply_token_);
            if (input_ != nullptr) input_.ValueChanged(input_token_);
            if (device_ != nullptr) device_.Close();
        } catch (...) {
        }
    }

    const Calibration& calibration() const noexcept override { return calibration_; }

    void set_event(void* event) noexcept override {
        const std::lock_guard lock(lock_);
        event_ = event;
        if (!reports_.empty() || lost_) SetEvent(event_);
    }

    bool next_report(std::array<std::uint8_t, 64>& report) noexcept override {
        const std::lock_guard lock(lock_);
        if (reports_.empty()) return false;
        report = reports_.front();
        reports_.pop_front();
        return true;
    }

    bool lost() const noexcept override { return lost_.load(std::memory_order_acquire); }

    void send_motor(std::span<const std::uint8_t, kMotorBytes> value) noexcept override {
        if (motor_ == nullptr || lost()) return;
        try {
            (void)motor_.WriteValueAsync(to_buffer(value), GattWriteOption::WriteWithoutResponse);
        } catch (...) {
        }
    }

private:
    void lose() {
        lost_.store(true, std::memory_order_release);
        const std::lock_guard lock(lock_);
        if (event_ != nullptr) SetEvent(event_);
        SetEvent(changed_);
    }

    void take_report(const IBuffer& value) {
        const auto report = usb_report(std::span<const std::uint8_t>(value.data(), value.Length()));
        if (!report) return;
        const std::lock_guard lock(lock_);
        if (reports_.size() >= 32u) reports_.pop_front();
        reports_.push_back(*report);
        if (event_ != nullptr) SetEvent(event_);
    }

    void take_reply(const IBuffer& value) {
        const std::lock_guard lock(reply_lock_);
        last_reply_.assign(value.data(), value.data() + value.Length());
        have_reply_ = true;
        reply_ready_.notify_all();
    }

    // A command and its reply: empty if none came.
    std::vector<std::uint8_t> exchange(std::span<const std::uint8_t> bytes) {
        std::unique_lock lock(reply_lock_);
        have_reply_ = false;
        lock.unlock();
        (void)write_.WriteValueAsync(to_buffer(bytes), GattWriteOption::WriteWithoutResponse).get();
        lock.lock();
        (void)reply_ready_.wait_for(lock, std::chrono::milliseconds(200), [this] { return have_reply_; });
        return have_reply_ ? last_reply_ : std::vector<std::uint8_t>{};
    }

    void read_calibration(std::uint16_t product) {
        const auto read = [this](std::uint32_t address) { return exchange(flash_read(address)); };
        const auto stick = [](std::span<const std::uint8_t> block, std::size_t at) {
            return sw::parse_switch2_stick_cal(std::span<const std::uint8_t, 9>(block.data() + at, 9));
        };
        auto reply = read(0x13040);
        auto block = flash_block(reply);
        if (!block.empty()) {
            std::array<float, 3> bias{};
            std::memcpy(bias.data(), block.data() + 4, sizeof(bias));
            for (std::size_t i = 0; i < bias.size(); ++i) calibration_.gyro_bias[i] = std::isfinite(bias[i]) ? bias[i] : 0.0f;
        }
        reply = read(0x13080);
        block = flash_block(reply);
        if (!block.empty()) calibration_.left = stick(block, 0x28);
        reply = read(0x130c0);
        block = flash_block(reply);
        if (!block.empty()) calibration_.right = stick(block, 0x28);
        reply = read(0x1fc040);
        block = flash_block(reply);
        if (!block.empty() && block[0] == 0xb2 && block[1] == 0xa1) calibration_.left = stick(block, 2);
        reply = read(0x1fc080);
        block = flash_block(reply);
        if (!block.empty() && block[0] == 0xb2 && block[1] == 0xa1) calibration_.right = stick(block, 2);
        if (product == sw::kGameCubeProductId) {
            reply = read(0x13140);
            block = flash_block(reply);
            if (!block.empty()) calibration_.gamecube_zero = {block[0], block[1]};
        }
    }

    HANDLE changed_ = nullptr;
    BluetoothLEDevice device_{nullptr};
    GattCharacteristic input_{nullptr};
    GattCharacteristic write_{nullptr};
    GattCharacteristic reply_{nullptr};
    GattCharacteristic motor_{nullptr};
    winrt::event_token status_token_{};
    winrt::event_token reply_token_{};
    winrt::event_token input_token_{};
    Calibration calibration_{};
    std::mutex lock_;
    HANDLE event_ = nullptr;
    std::deque<std::array<std::uint8_t, 64>> reports_;
    std::atomic<bool> lost_{false};
    std::mutex reply_lock_;
    std::condition_variable reply_ready_;
    std::vector<std::uint8_t> last_reply_;
    bool have_reply_ = false;
};

struct Entry {
    std::uint64_t address = 0;
    std::uint16_t product = 0;
    std::shared_ptr<WinLink> link;
};

struct State {
    std::mutex lock;
    std::condition_variable wake;
    std::thread thread;
    bool stopping = false;
    HANDLE changed = nullptr;
    std::deque<std::pair<std::uint64_t, std::uint16_t>> waiting;
    std::map<std::wstring, Entry> connected;
};
State& state() {
    static State s;
    return s;
}

std::wstring path_of(std::uint64_t address) { return L"ble:" + std::to_wstring(address); }

void run() {
    winrt::init_apartment(winrt::apartment_type::multi_threaded);
    State& s = state();
    std::uint64_t host = 0;
    try {
        const auto adapter = BluetoothAdapter::GetDefaultAsync().get();
        if (adapter == nullptr || !adapter.IsLowEnergySupported()) return;
        host = adapter.BluetoothAddress();
    } catch (const winrt::hresult_error&) {
        return;
    }
    BluetoothLEAdvertisementWatcher watcher;
    BluetoothLEManufacturerData filter;
    filter.CompanyId(kManufacturerId);
    watcher.AdvertisementFilter().Advertisement().ManufacturerData().Append(filter);
    watcher.ScanningMode(BluetoothLEScanningMode::Passive);
    watcher.Received([&s, host](const BluetoothLEAdvertisementWatcher&, const BluetoothLEAdvertisementReceivedEventArgs& args) {
        for (const auto& item : args.Advertisement().GetManufacturerDataByCompanyId(kManufacturerId)) {
            const IBuffer data = item.Data();
            const auto advert = parse_advert(std::span<const std::uint8_t>(data.data(), data.Length()), host);
            if (!advert) continue;
            const std::lock_guard lock(s.lock);
            if (s.connected.count(path_of(args.BluetoothAddress())) != 0u) continue;
            bool queued = false;
            for (const auto& waiting : s.waiting) queued = queued || waiting.first == args.BluetoothAddress();
            if (!queued) {
                s.waiting.emplace_back(args.BluetoothAddress(), advert->product);
                s.wake.notify_all();
            }
        }
    });
    try {
        watcher.Start();
    } catch (const winrt::hresult_error& error) {
        std::cerr << "[controller] bluetooth LE watcher: 0x" << std::hex << static_cast<std::uint32_t>(error.code()) << std::dec << '\n';
        return;
    }
    std::unique_lock lock(s.lock);
    while (!s.stopping) {
        s.wake.wait_for(lock, std::chrono::seconds(1), [&s] { return s.stopping || !s.waiting.empty(); });
        bool gone = false;
        for (auto it = s.connected.begin(); it != s.connected.end();) {
            if (it->second.link->lost()) {
                it = s.connected.erase(it);
                gone = true;
            } else {
                ++it;
            }
        }
        if (gone) SetEvent(s.changed);
        if (s.stopping || s.waiting.empty()) continue;
        const auto [address, product] = s.waiting.front();
        lock.unlock();
        std::shared_ptr<WinLink> link;
        try {
            link = WinLink::connect(address, product, s.changed);
        } catch (const winrt::hresult_error& error) {
            std::cerr << "[controller] bluetooth LE controller: 0x" << std::hex << static_cast<std::uint32_t>(error.code()) << std::dec << '\n';
        }
        lock.lock();
        s.waiting.pop_front();
        if (link) {
            s.connected[path_of(address)] = Entry{address, product, link};
            SetEvent(s.changed);
        }
    }
    lock.unlock();
    try {
        watcher.Stop();
    } catch (...) {
    }
    lock.lock();
    s.connected.clear();
}

}  // namespace

void start(void* changed) {
    State& s = state();
    const std::lock_guard lock(s.lock);
    if (s.thread.joinable()) return;
    s.changed = changed;
    s.stopping = false;
    s.thread = std::thread([] {
        SetThreadDescription(GetCurrentThread(), L"nebula-controller-ble");
        run();
    });
}

void stop() {
    State& s = state();
    {
        const std::lock_guard lock(s.lock);
        s.stopping = true;
        s.wake.notify_all();
    }
    if (s.thread.joinable()) s.thread.join();
}

std::vector<Found> found() {
    State& s = state();
    const std::lock_guard lock(s.lock);
    std::vector<Found> out;
    for (const auto& [path, entry] : s.connected) out.push_back({path, entry.product});
    return out;
}

std::shared_ptr<Link> link(const std::wstring& path) {
    State& s = state();
    const std::lock_guard lock(s.lock);
    const auto it = s.connected.find(path);
    return it != s.connected.end() ? it->second.link : nullptr;
}

}  // namespace galaxy::input::switch2_ble
