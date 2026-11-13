// SPDX-License-Identifier: GPL-3.0-only
// vendor_download.h.
#include "galaxy/vendor_download.h"

#include "galaxy/windows_api.h"
#include <bcrypt.h>
#include <winhttp.h>

#include <zlib.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <mutex>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <vector>

#pragma comment(lib, "winhttp.lib")
#pragma comment(lib, "bcrypt.lib")

namespace galaxy::vendor_download {

namespace {

struct Part {
    const char* entry;
    const wchar_t* file;
    const char* sha256;
    std::uint64_t size;
    std::uint64_t compressed;
};

struct Release {
    const wchar_t* host;
    const wchar_t* path;
    std::uint64_t archive_size;
    const wchar_t* folder;
    const char* license_entry;
    const Part* parts;
    std::size_t part_count;
};

// Streamline's interposer and plug-ins, DLSS, DLSS Frame Generation and DLSS Ray Reconstruction (the ray tracer's
// denoiser), Reflex (Frame Generation needs it), the licenses.
constexpr Part kDlssParts[] = {
    {"bin/x64/sl.interposer.dll", L"sl.interposer.dll", "8c87c9499461da561edd529aa9bf7831d67d7b94ebb1c1a5ed54ef4934e1ea4c", 652928u, 284550u},
    {"bin/x64/sl.common.dll", L"sl.common.dll", "82924a8954dd671e09351c5de0eb87ad0eb25b944cc9f9ab955ca1d9950de15d", 843392u, 390298u},
    {"bin/x64/sl.dlss.dll", L"sl.dlss.dll", "73bf52c0cfaa5900a8f3f4a91306e4625e7cca696dfb305aae44c9f97b582e1f", 422016u, 194964u},
    {"bin/x64/nvngx_dlss.dll", L"nvngx_dlss.dll", "3975567b8943c53acce397f2b72380092f84f162d00b0d2c7d08a1025c563983", 58956912u, 31123319u},
    {"bin/x64/nvngx_dlss.license.txt", L"nvngx_dlss.license.txt", "3027f23ca5a46dd9cb8183fbd522983a86f64d7daac5982912bf9f214671f294", 26727u, 9256u},
    {"license.txt", L"streamline.license.txt", "7b6f23e7d6f3ad6292f9308d2b42cdc3d82ae4e9b2abb55f230279d83bedd43d", 1591u, 883u},
    {"bin/x64/sl.dlss_g.dll", L"sl.dlss_g.dll", "f4a6b2b14dcc0b1485989e430d3b4e3a44ac1800b92ba1ad74f476e64fb2b09c", 636032u, 290880u},
    {"bin/x64/nvngx_dlssg.dll", L"nvngx_dlssg.dll", "ff6e90eb78b827927dff5b4ecc6b1c870c2e9bca29ed9f48c7d348cc9e170b82", 7460976u, 3717448u},
    {"bin/x64/sl.reflex.dll", L"sl.reflex.dll", "0ce9725e3e03ea9e7f81d008b57f33ee365973d2e349131c8b1c3e3378fe2db0", 388736u, 180983u},
    {"bin/x64/sl.pcl.dll", L"sl.pcl.dll", "f13d51cfa05f4cd514df2026049e2db8adf359221713170ad386fd499915b582", 360064u, 167265u},
    {"bin/x64/reflex.license.txt", L"reflex.license.txt", "ebf83c07fb3b2939908c3795d887afde3161c89a28ba391724efc784ce1bdabe", 20504u, 7617u},
    {"bin/x64/sl.dlss_d.dll", L"sl.dlss_d.dll", "026b9f4f9ea848f2f5aa0e2024fc6b1c885f57eea5d41078124dba14f873b86b", 437376u, 198360u},
    {"bin/x64/nvngx_dlssd.dll", L"nvngx_dlssd.dll", "4bc7ea5fcb2f32cf86bc2cb072e8d2860914a0be511cbdcb2fc206c1d9d83b80", 48343664u, 21565278u},
};

constexpr Part kXessParts[] = {
    {"bin/libxess.dll", L"libxess.dll", "251659dd84a3e84de67c886a4186e01f3eca49b00641906fe38bb6b807e5d5b7", 77795704u, 59737353u},
    {"LICENSE.txt", L"LICENSE.txt", "784be42c39a4a4d03cf85d88e15e59d75828fca773ce45b8dbfffbd7eaf208df", 4194u, 1970u},
    {"third-party-programs.txt", L"third-party-programs.txt", "e8d8899dbfc4de0e8ca25560be9358e03c1405d7c956d20a1838c3c709b9b51c", 35788u, 9217u},
};

constexpr Release kReleases[] = {
    {L"github.com", L"/NVIDIA-RTX/Streamline/releases/download/v2.14.1/streamline-sdk-v2.14.1.zip", 275994000u, L"streamline-2.14.1",
     "bin/x64/nvngx_dlss.license.txt", kDlssParts, std::size(kDlssParts)},
    {L"github.com", L"/intel/xess/releases/download/v3.0.2/XeSS_SDK_3.0.2.zip", 76839749u, L"xess-3.0.2", "LICENSE.txt", kXessParts, std::size(kXessParts)},
};

const Release& release_of(Package package) { return kReleases[static_cast<unsigned>(package)]; }

// One connection, following GitHub's redirect to its storage once; ranges then go there directly.
class Remote {
public:
    explicit Remote(const Release& release) : release_(release) {
        session_ = WinHttpOpen(L"Nebula", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0u);
        if (session_ != nullptr) {
            DWORD protocols = WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2 | WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_3;
            WinHttpSetOption(session_, WINHTTP_OPTION_SECURE_PROTOCOLS, &protocols, sizeof(protocols));
            WinHttpSetTimeouts(session_, 30000, 30000, 30000, 30000);
        }
        host_ = release.host;
        path_ = release.path;
    }
    ~Remote() {
        close_request();
        if (connection_ != nullptr) WinHttpCloseHandle(connection_);
        if (session_ != nullptr) WinHttpCloseHandle(session_);
    }
    Remote(const Remote&) = delete;
    Remote& operator=(const Remote&) = delete;

    // Opens [offset, offset + count): false when it failed.
    bool open(std::uint64_t offset, std::uint64_t count) {
        for (int hop = 0; hop < 6; ++hop) {
            close_request();
            if (session_ == nullptr || !connect()) {
                return false;
            }
            request_ = WinHttpOpenRequest(connection_, L"GET", path_.c_str(), nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
            if (request_ == nullptr) {
                return false;
            }
            DWORD policy = WINHTTP_OPTION_REDIRECT_POLICY_NEVER;
            WinHttpSetOption(request_, WINHTTP_OPTION_REDIRECT_POLICY, &policy, sizeof(policy));
            wchar_t range[80]{};
            std::swprintf(range, std::size(range), L"Range: bytes=%llu-%llu", static_cast<unsigned long long>(offset),
                          static_cast<unsigned long long>(offset + count - 1u));
            if (!WinHttpSendRequest(request_, range, static_cast<DWORD>(-1), WINHTTP_NO_REQUEST_DATA, 0u, 0u, 0u) ||
                !WinHttpReceiveResponse(request_, nullptr)) {
                return false;
            }
            DWORD status = 0u;
            DWORD size = sizeof(status);
            if (!WinHttpQueryHeaders(request_, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &status, &size,
                                     WINHTTP_NO_HEADER_INDEX)) {
                return false;
            }
            if (status == 206u) {
                return true;
            }
            if (status < 300u || status >= 400u) {
                std::cerr << "[download] the server answered " << status << '\n';
                return false;
            }
            wchar_t location[4096]{};
            size = sizeof(location);
            if (!WinHttpQueryHeaders(request_, WINHTTP_QUERY_LOCATION, WINHTTP_HEADER_NAME_BY_INDEX, location, &size, WINHTTP_NO_HEADER_INDEX) ||
                !follow(location)) {
                return false;
            }
        }
        return false;
    }

    std::size_t read(void* buffer, std::size_t bytes) {
        DWORD got = 0u;
        return request_ != nullptr && WinHttpReadData(request_, buffer, static_cast<DWORD>(bytes), &got) ? got : 0u;
    }

    void restart() {
        host_ = release_.host;
        path_ = release_.path;
        close_request();
        if (connection_ != nullptr) {
            WinHttpCloseHandle(connection_);
            connection_ = nullptr;
        }
    }

private:
    bool connect() {
        if (connection_ != nullptr) {
            return true;
        }
        connection_ = WinHttpConnect(session_, host_.c_str(), INTERNET_DEFAULT_HTTPS_PORT, 0u);
        return connection_ != nullptr;
    }
    void close_request() {
        if (request_ != nullptr) {
            WinHttpCloseHandle(request_);
            request_ = nullptr;
        }
    }
    bool follow(const wchar_t* location) {
        URL_COMPONENTSW parts{};
        parts.dwStructSize = sizeof(parts);
        wchar_t host[256]{};
        wchar_t path[3800]{};
        wchar_t extra[2048]{};
        parts.lpszHostName = host;
        parts.dwHostNameLength = static_cast<DWORD>(std::size(host));
        parts.lpszUrlPath = path;
        parts.dwUrlPathLength = static_cast<DWORD>(std::size(path));
        parts.lpszExtraInfo = extra;
        parts.dwExtraInfoLength = static_cast<DWORD>(std::size(extra));
        if (!WinHttpCrackUrl(location, 0u, 0u, &parts) || parts.nScheme != INTERNET_SCHEME_HTTPS) {
            return false;
        }
        if (host_ != host && connection_ != nullptr) {
            WinHttpCloseHandle(connection_);
            connection_ = nullptr;
        }
        host_ = host;
        path_ = std::wstring(path) + extra;
        return true;
    }

    const Release& release_;
    HINTERNET session_ = nullptr;
    HINTERNET connection_ = nullptr;
    HINTERNET request_ = nullptr;
    std::wstring host_;
    std::wstring path_;
};

std::uint32_t u16_at(const std::vector<unsigned char>& b, std::size_t i) { return b[i] | static_cast<std::uint32_t>(b[i + 1u]) << 8u; }
std::uint32_t u32_at(const std::vector<unsigned char>& b, std::size_t i) { return u16_at(b, i) | u16_at(b, i + 2u) << 16u; }

struct Entry {
    std::uint32_t method = 0u;
    std::uint64_t compressed = 0u;
    std::uint64_t size = 0u;
    std::uint64_t offset = 0u;
};

class Sha256 {
public:
    Sha256() {
        if (BCryptOpenAlgorithmProvider(&algorithm_, BCRYPT_SHA256_ALGORITHM, nullptr, 0u) >= 0) {
            BCryptCreateHash(algorithm_, &hash_, nullptr, 0u, nullptr, 0u, 0u);
        }
    }
    ~Sha256() {
        if (hash_ != nullptr) BCryptDestroyHash(hash_);
        if (algorithm_ != nullptr) BCryptCloseAlgorithmProvider(algorithm_, 0u);
    }
    void add(const void* data, std::size_t bytes) {
        if (hash_ != nullptr) BCryptHashData(hash_, static_cast<PUCHAR>(const_cast<void*>(data)), static_cast<ULONG>(bytes), 0u);
    }
    [[nodiscard]] std::string finish() {
        std::array<unsigned char, 32> digest{};
        if (hash_ == nullptr || BCryptFinishHash(hash_, digest.data(), static_cast<ULONG>(digest.size()), 0u) < 0) {
            return {};
        }
        std::string text;
        char hex[3]{};
        for (const unsigned char byte : digest) {
            std::snprintf(hex, sizeof(hex), "%02x", byte);
            text += hex;
        }
        return text;
    }

private:
    BCRYPT_ALG_HANDLE algorithm_ = nullptr;
    BCRYPT_HASH_HANDLE hash_ = nullptr;
};

std::string file_sha256(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return {};
    }
    Sha256 hash;
    std::vector<char> buffer(1u << 20u);
    while (in) {
        in.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
        hash.add(buffer.data(), static_cast<std::size_t>(in.gcount()));
    }
    return hash.finish();
}

std::atomic<State> s_state{State::Idle};
std::atomic<std::uint64_t> s_done{0u};
std::atomic<std::uint64_t> s_total{0u};
std::atomic<bool> s_cancel{false};
// Outlives the process's static destructors: a detached thread may still be reading when the game exits.
struct LicenseStore {
    std::mutex mutex;
    License license;
    unsigned fetch = 0u;
};
LicenseStore& license_store() {
    static LicenseStore* const store = new LicenseStore;
    return *store;
}

// Exactly `bytes` from `offset`, a dropped connection tried again.
bool read_range(Remote& remote, std::uint64_t offset, std::size_t bytes, std::vector<unsigned char>& out) {
    out.resize(bytes);
    for (int attempt = 0; attempt < 3; ++attempt) {
        if (attempt != 0) {
            remote.restart();
            Sleep(1500u * static_cast<DWORD>(attempt));
        }
        if (!remote.open(offset, bytes)) {
            continue;
        }
        std::size_t filled = 0u;
        while (filled < bytes) {
            const std::size_t got = remote.read(out.data() + filled, bytes - filled);
            if (got == 0u) {
                break;
            }
            filled += got;
        }
        if (filled == bytes) {
            return true;
        }
    }
    return false;
}

bool read_directory(const Release& release, Remote& remote, std::unordered_map<std::string, Entry>& entries) {
    const std::size_t tail_bytes = static_cast<std::size_t>(std::min<std::uint64_t>(release.archive_size, 65536u + 22u));
    std::vector<unsigned char> tail;
    if (!read_range(remote, release.archive_size - tail_bytes, tail_bytes, tail)) {
        return false;
    }
    std::size_t end = SIZE_MAX;
    for (std::size_t i = tail_bytes - 22u; i != SIZE_MAX; --i) {
        if (u32_at(tail, i) == 0x06054B50u) {
            end = i;
            break;
        }
    }
    if (end == SIZE_MAX) {
        std::cerr << "[download] the release zip has no directory\n";
        return false;
    }
    const std::uint64_t directory_size = u32_at(tail, end + 12u);
    const std::uint64_t directory_offset = u32_at(tail, end + 16u);
    if (directory_offset + directory_size > release.archive_size) {
        return false;
    }
    std::vector<unsigned char> directory;
    if (!read_range(remote, directory_offset, static_cast<std::size_t>(directory_size), directory)) {
        return false;
    }
    for (std::size_t p = 0u; p + 46u <= directory.size() && u32_at(directory, p) == 0x02014B50u;) {
        const std::size_t name = u16_at(directory, p + 28u);
        if (p + 46u + name > directory.size()) {
            break;
        }
        Entry entry;
        entry.method = u16_at(directory, p + 10u);
        entry.compressed = u32_at(directory, p + 20u);
        entry.size = u32_at(directory, p + 24u);
        entry.offset = u32_at(directory, p + 42u);
        entries[std::string(reinterpret_cast<const char*>(directory.data()) + p + 46u, name)] = entry;
        p += 46u + name + u16_at(directory, p + 30u) + u16_at(directory, p + 32u);
    }
    return true;
}

// The entry's bytes, inflated, to `sink`; `on_read` gets the compressed bytes read in this attempt. False when the entry
// holds more than `limit` bytes (declared or inflated).
template <typename Sink, typename OnRead>
bool extract(Remote& remote, const Entry& entry, std::uint64_t limit, Sink&& sink, OnRead&& on_read) {
    if ((entry.method != 0u && entry.method != 8u) || entry.size > limit) {
        return false;
    }
    std::vector<unsigned char> header;
    if (!read_range(remote, entry.offset, 30u, header) || u32_at(header, 0u) != 0x04034B50u) {
        return false;
    }
    const std::uint64_t data = entry.offset + 30u + u16_at(header, 26u) + u16_at(header, 28u);
    if (!remote.open(data, entry.compressed)) {
        return false;
    }
    z_stream stream{};
    if (entry.method == 8u && inflateInit2(&stream, -15) != Z_OK) {
        return false;
    }
    struct End {
        z_stream* stream;
        bool used;
        ~End() {
            if (used) inflateEnd(stream);
        }
    } end{&stream, entry.method == 8u};
    std::vector<unsigned char> in(1u << 18u);
    std::vector<unsigned char> out(1u << 20u);
    std::uint64_t read_total = 0u;
    std::uint64_t produced = 0u;
    int status = Z_OK;
    while (read_total < entry.compressed) {
        if (s_cancel.load(std::memory_order_relaxed)) {
            return false;
        }
        const std::size_t want = static_cast<std::size_t>(std::min<std::uint64_t>(in.size(), entry.compressed - read_total));
        const std::size_t got = remote.read(in.data(), want);
        if (got == 0u) {
            return false;
        }
        read_total += got;
        on_read(read_total);
        if (entry.method == 0u) {
            produced += got;
            if (produced > limit) {
                return false;
            }
            sink(in.data(), got);
            continue;
        }
        stream.next_in = in.data();
        stream.avail_in = static_cast<uInt>(got);
        do {
            stream.next_out = out.data();
            stream.avail_out = static_cast<uInt>(out.size());
            status = inflate(&stream, Z_NO_FLUSH);
            if (status != Z_OK && status != Z_STREAM_END) {
                return false;
            }
            produced += out.size() - stream.avail_out;
            if (produced > limit) {
                return false;
            }
            sink(out.data(), out.size() - stream.avail_out);
        } while (stream.avail_out == 0u);
    }
    return entry.method == 0u || status == Z_STREAM_END;
}

bool part_in_place(const Part& part, const std::filesystem::path& directory) {
    std::error_code error;
    return std::filesystem::file_size(directory / part.file, error) == part.size && !error && file_sha256(directory / part.file) == part.sha256;
}

bool install_part(Remote& remote, const Part& part, const Entry& entry, const std::filesystem::path& directory, std::uint64_t before) {
    const std::filesystem::path target = directory / part.file;
    std::filesystem::path partial = target;
    partial += L".partial";
    bool good = false;
    for (int attempt = 0; attempt < 3 && !good && !s_cancel.load(std::memory_order_relaxed); ++attempt) {
        if (attempt != 0) {
            remote.restart();
            Sleep(1500u * static_cast<DWORD>(attempt));
        }
        std::ofstream output(partial, std::ios::binary | std::ios::trunc);
        Sha256 hash;
        std::uint64_t written = 0u;
        good = output.good() && extract(
                                    remote, entry, part.size,
                                    [&](const unsigned char* data, std::size_t bytes) {
                                        output.write(reinterpret_cast<const char*>(data), static_cast<std::streamsize>(bytes));
                                        hash.add(data, bytes);
                                        written += bytes;
                                    },
                                    [&](std::uint64_t read) { s_done.store(before + read, std::memory_order_relaxed); });
        output.close();
        good = good && output.good() && written == part.size && hash.finish() == part.sha256;
        if (!good) {
            std::cerr << "[download] " << entry.offset << ": attempt " << attempt << " failed\n";
        }
    }
    std::error_code error;
    if (good) {
        std::filesystem::rename(partial, target, error);
        good = !error;
    }
    std::filesystem::remove(partial, error);
    return good;
}

void run(const Release& release, const std::filesystem::path& directory) {
    State finished = State::Failed;
    try {
        std::error_code error;
        std::filesystem::create_directories(directory, error);
        std::vector<const Part*> missing;
        std::uint64_t total = 0u;
        for (std::size_t i = 0u; i < release.part_count; ++i) {
            if (!part_in_place(release.parts[i], directory)) {
                missing.push_back(&release.parts[i]);
                total += release.parts[i].compressed;
            }
        }
        s_total.store(total, std::memory_order_relaxed);
        s_done.store(0u, std::memory_order_relaxed);
        Remote remote(release);
        std::unordered_map<std::string, Entry> entries;
        bool good = missing.empty() || read_directory(release, remote, entries);
        std::uint64_t before = 0u;
        for (const Part* part : missing) {
            if (!good) {
                break;
            }
            const auto found = entries.find(part->entry);
            good = found != entries.end() && found->second.size == part->size && install_part(remote, *part, found->second, directory, before);
            before += part->compressed;
        }
        finished = good ? State::Done : s_cancel.load(std::memory_order_relaxed) ? State::Canceled : State::Failed;
    } catch (const std::exception& error) {
        std::cerr << "[download] " << error.what() << '\n';
    }
    s_state.store(finished, std::memory_order_release);
}

}  // namespace

bool installed(Package package, const std::filesystem::path& directory) {
    const Release& release = release_of(package);
    for (std::size_t i = 0u; i < release.part_count; ++i) {
        std::error_code error;
        if (std::filesystem::file_size(directory / release.parts[i].file, error) != release.parts[i].size || error) {
            return false;
        }
    }
    return true;
}

unsigned download_megabytes(Package package) {
    const Release& release = release_of(package);
    std::uint64_t bytes = 0u;
    for (std::size_t i = 0u; i < release.part_count; ++i) {
        bytes += release.parts[i].compressed;
    }
    return static_cast<unsigned>((bytes + 500'000u) / 1'000'000u);
}

bool start(Package package, const std::filesystem::path& directory) {
    State expected = State::Idle;
    if (!s_state.compare_exchange_strong(expected, State::Running, std::memory_order_acq_rel)) {
        return false;
    }
    s_cancel.store(false, std::memory_order_relaxed);
    s_done.store(0u, std::memory_order_relaxed);
    s_total.store(0u, std::memory_order_relaxed);
    std::thread(run, std::cref(release_of(package)), directory).detach();
    return true;
}

void cancel() { s_cancel.store(true, std::memory_order_relaxed); }

Progress progress() {
    Progress out;
    out.state = s_state.load(std::memory_order_acquire);
    out.done_bytes = s_done.load(std::memory_order_relaxed);
    out.total_bytes = s_total.load(std::memory_order_relaxed);
    return out;
}

void acknowledge() {
    const State state = s_state.load(std::memory_order_acquire);
    if (state == State::Done || state == State::Failed || state == State::Canceled) {
        s_state.store(State::Idle, std::memory_order_release);
    }
}

// The licence text is read whole into memory: an entry that inflates past this is refused.
constexpr std::uint64_t kLicenseLimit = 1u << 20u;

void fetch_license(Package package) {
    LicenseStore& store = license_store();
    unsigned fetch = 0u;
    {
        const std::lock_guard<std::mutex> lock(store.mutex);
        store.license = License{};
        fetch = ++store.fetch;
    }
    std::thread([&release = release_of(package), &store, fetch] {
        std::string text;
        bool good = false;
        try {
            Remote remote(release);
            std::unordered_map<std::string, Entry> entries;
            if (read_directory(release, remote, entries)) {
                const auto found = entries.find(release.license_entry);
                good = found != entries.end() && extract(
                                                     remote, found->second, kLicenseLimit,
                                                     [&](const unsigned char* data, std::size_t bytes) {
                                                         text.append(reinterpret_cast<const char*>(data), bytes);
                                                     },
                                                     [](std::uint64_t) {});
            }
        } catch (const std::exception& error) {
            std::cerr << "[download] " << error.what() << '\n';
        }
        const std::lock_guard<std::mutex> lock(store.mutex);
        if (store.fetch == fetch) {
            store.license.ready = good;
            store.license.failed = !good;
            store.license.text = std::move(text);
        }
    }).detach();
}

License license() {
    LicenseStore& store = license_store();
    const std::lock_guard<std::mutex> lock(store.mutex);
    return store.license;
}

}  // namespace galaxy::vendor_download
