// SPDX-License-Identifier: GPL-3.0-only
#include "galaxy/diag_report.h"

#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>

#if defined(_WIN32)
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
#endif

namespace {
namespace diag = galaxy::diag;

void check(bool value, const char* label) {
    if (!value) throw std::runtime_error(label);
}

diag::Bytes bytes(std::string_view hex) {
    diag::Bytes out;
    for (std::size_t i = 0; i + 1u < hex.size(); i += 2u) {
        out.push_back(static_cast<std::uint8_t>(std::stoi(std::string(hex.substr(i, 2u)), nullptr, 16)));
    }
    return out;
}
diag::Key key(std::string_view hex) {
    diag::Key k{};
    check(diag::key_from_hex(hex, k), "a key's hex");
    return k;
}
std::string hex_of(const diag::Bytes& b) { return diag::hex(b.data(), b.size()); }
template <std::size_t N>
std::string hex_of(const std::array<std::uint8_t, N>& b) {
    return diag::hex(b.data(), N);
}

// A result as benchmark.cpp writes it (schema 1), one scene and one probe run.
constexpr std::string_view kBenchmark = R"({"schema":1,"app":{"version":"1.0.0","commit":null},"created_utc":"2026-11-13T20:00:00Z",
"status":"complete","failure":"","backend":"d3d12","device":{"gpu":"Intel(R) UHD Graphics 600","vendor_id":32902,
"device_id":12677,"driver_version":null,"api_level":null,"shared_memory":true,"gpu_budget_mb":2048,
"cpu":"Intel(R) Celeron(R) N4000 CPU @ 1.10GHz","logical_cores":2,"ram_mb":4096,"os":"Windows 10.0.19045",
"output":[1366,768],"refresh_hz":60.000},"mods_loaded":false,
"settings":{"internal_height":0,"upscaler":0,"msaa_samples":1,"ambient_occlusion":0,"ambient_occlusion_ssao":0,
"enhanced_lighting":0,"per_pixel_lighting":0,"aa":0,"smaa_quality":2,"bloom":100,"vsync_interval":1,"max_fps":60.000,
"frame_generation":0,"ray_tracing_enabled":0,"display_mode":0},
"measured_with":{"internal_height":0,"upscaler":0,"msaa_samples":1,"ambient_occlusion":0,"ambient_occlusion_ssao":0,
"enhanced_lighting":0,"per_pixel_lighting":0,"aa":0,"smaa_quality":2,"bloom":100,"vsync_interval":0,"max_fps":60.000,
"frame_generation":0,"ray_tracing_enabled":0,"display_mode":0},"limits":["draws and triangles: no back-end-neutral counter"],
"scenes":[{"stage":"CosmosGardenGalaxy","scenario":1,"grand_stars":0,"load_ms":2301.500,"frames":540,"pixels":1049088.000,
"settings_held":true,"interval_ms":{"p50":21.100,"p95":30.400,"p99":41.000,"worst":66.700,"over_16_7":300,"over_33_3":4},
"gpu_ms":19.250,"gpu_pass_ms":{"scene":15.000,"bloom":2.250,"post":2.000},"cpu_ms":{"game":4.100,"render":6.200,
"audio":0.400,"process":12.000},"draws":null,"triangles":null,"gpu_memory_peak_mb":900,"process_peak_mb":1200}],
"probe":{"stage":"BattleShipGalaxy","runs":[{"case":"480 base","load_ms":null,"frames":300,"pixels":409920.000,
"settings_held":true,"interval_ms":{"p50":12.000,"p95":14.000,"p99":15.000,"worst":20.000,"over_16_7":0,"over_33_3":0},
"gpu_ms":9.000,"gpu_pass_ms":{"scene":8.000},"cpu_ms":{"game":3.000,"render":4.000,"audio":null,"process":9.000},
"draws":null,"triangles":null,"gpu_memory_peak_mb":800,"process_peak_mb":1100}],
"fit":{"valid":true,"fixed_ms":2.000,"ms_per_mpixel":17.000},"price_ms_per_mpixel":{"ambient occlusion":4.000,"msaa 4":null}},
"thermal":null,"recommendation":{"budget_ms":13.333,"predicted_ms":12.900,"fits":true,"cpu_bound":false,"cpu_thread":null,
"height_left_out":false,"settings":[{"key":"internal_height","current":0,"recommended":480}]},
"summary":{"verdict":"fits","worst_stage":"CosmosGardenGalaxy","worst_gpu_ms":19.250,"worst_p95_ms":30.400}}
)";

std::string replaced(std::string_view text, std::string_view from, std::string_view to) {
    std::string out(text);
    const std::size_t at = out.find(from);
    check(at != std::string::npos, "the sample holds the text a test changes");
    out.replace(at, from.size(), to);
    return out;
}

void primitives() {
    const std::string abc = "abc";
    check(hex_of(diag::sha256(reinterpret_cast<const std::uint8_t*>(abc.data()), abc.size())) ==
              "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
          "SHA-256 of abc (FIPS 180-4)");
    const std::string jefe = "Jefe", what = "what do ya want for nothing?";
    check(hex_of(diag::hmac_sha256(reinterpret_cast<const std::uint8_t*>(jefe.data()), jefe.size(),
                                   reinterpret_cast<const std::uint8_t*>(what.data()), what.size())) ==
              "5bdcc146bf60754e6a042426089575c75a003f089d2739839dec58b964ec3843",
          "HMAC-SHA256, RFC 4231 case 2");
    const diag::Bytes ikm(22u, 0x0bu), salt = bytes("000102030405060708090a0b0c"), info = bytes("f0f1f2f3f4f5f6f7f8f9");
    const auto prk = diag::hmac_sha256(salt.data(), salt.size(), ikm.data(), ikm.size());
    check(hex_of(prk) == "077709362c2e32df0ddc3f0dc47bba6390b6c73bb50f9c3122ec844ad7c2b3e5", "HKDF extract, RFC 5869 case 1");
    check(hex_of(diag::hkdf_expand(prk.data(), info.data(), info.size(), 42u)) ==
              "3cb25f25faacd57a90434f64d0362f2a2d2d0a90cf1a5a4c5db02d56ecc4c5bf34007208d5b887185865",
          "HKDF expand, RFC 5869 case 1");
    check(hex_of(diag::x25519(key("a546e36bf0527c9d3b16154b82465edd62144c0ac1fc5a18506a2244ba449ac4"),
                              key("e6db6867583030db3594c1a424b15f7c726624ec26b3353b10a903a6d0ab1c4c"))) ==
              "c3da55379de9c6908e94ea4df28d084f32eccf03491c71f754b4075577a28552",
          "X25519, RFC 7748 5.2");
    const diag::Key alice = key("77076d0a7318a57d3c16c17251b26645df4c2f87ebc0992ab177fba51db92c2a");
    const diag::Key bob = key("5dab087e624a8a4b79e17f8b83800ee66f3bb1292618b6fd1c2f8b27ff88e0eb");
    check(hex_of(diag::x25519_public(alice)) == "8520f0098930a754748b7ddcb43ef75a0dbf3a0d26381af4eba4a98eaa9b4e6a",
          "X25519 public key, RFC 7748 6.1");
    check(hex_of(diag::x25519(alice, diag::x25519_public(bob))) ==
              "4a5d9d5ba4ce2de1728e3bf480350f25e07e21c947d19e3376f09b3c1e161742",
          "X25519 shared secret, RFC 7748 6.1");
    const diag::Key aead_key = key("808182838485868788898a8b8c8d8e8f909192939495969798999a9b9c9d9e9f");
    const diag::Bytes nonce = bytes("070000004041424344454647"), aad = bytes("50515253c0c1c2c3c4c5c6c7");
    const std::string plain =
        "Ladies and Gentlemen of the class of '99: If I could offer you only one tip for the future, sunscreen would be it.";
    const diag::Bytes sealed = diag::chacha20_poly1305_seal(aead_key, nonce.data(), aad.data(), aad.size(),
                                                            reinterpret_cast<const std::uint8_t*>(plain.data()), plain.size());
    check(hex_of(sealed) ==
              "d31a8d34648e60db7b86afbc53ef7ec2a4aded51296e08fea9e2b5a736ee62d63dbea45e8ca9671282fafb69da92728b1a71de0a9e060b29"
              "05d6a5b67ecd3b3692ddbd7f2d778b8c9803aee328091b58fab324e4fad675945585808b4831d7bc3ff4def08e4b7a9de576d26586cec64"
              "b6116"
              "1ae10b594f09e26a7e902ecbd0600691",
          "ChaCha20-Poly1305, RFC 8439 2.8.2");
    diag::Bytes opened;
    check(diag::chacha20_poly1305_open(aead_key, nonce.data(), aad.data(), aad.size(), sealed.data(), sealed.size(), opened) &&
              std::string(opened.begin(), opened.end()) == plain,
          "ChaCha20-Poly1305 opens its own");
    diag::Bytes bent = sealed;
    bent[10] ^= 1u;
    check(!diag::chacha20_poly1305_open(aead_key, nonce.data(), aad.data(), aad.size(), bent.data(), bent.size(), opened) &&
              opened.empty(),
          "ChaCha20-Poly1305 refuses a changed byte");
}

// RFC 9180 A.2.1: DHKEM(X25519, HKDF-SHA256), HKDF-SHA256, ChaCha20Poly1305, base mode.
void hpke_vector() {
    const diag::Bytes ikm_e = bytes("909a9b35d3dc4713a5e72a4da274b55d3d3821a37e5d099e74a647db583a904b");
    const diag::Bytes ikm_r = bytes("1ac01f181fdf9f352797655161c58b75c656a6cc2716dcb66372da835542e1df");
    const diag::Key sk_e = diag::hpke_derive_secret(ikm_e.data(), ikm_e.size());
    const diag::Key sk_r = diag::hpke_derive_secret(ikm_r.data(), ikm_r.size());
    check(hex_of(sk_e) == "f4ec9b33b792c372c1d2c2063507b684ef925b8c75a42dbcbf57d63ccd381600", "A.2.1 skEm");
    check(hex_of(sk_r) == "8057991eef8f1f1af18f4a9491d16a1ce333f695d4db8e38da75975c4478e0fb", "A.2.1 skRm");
    const diag::Key pk_r = diag::x25519_public(sk_r);
    check(hex_of(pk_r) == "4310ee97d88cc1f088a5576c77ab0cf5c3ac797f3d95139c6c84b5429c59662a", "A.2.1 pkRm");
    const diag::Bytes info = bytes("4f6465206f6e2061204772656369616e2055726e");
    const std::string_view info_text(reinterpret_cast<const char*>(info.data()), info.size());
    diag::Key enc{};
    diag::HpkeContext sender;
    check(diag::hpke_setup_sender(pk_r, sk_e, info_text, enc, sender), "A.2.1 SetupBaseS");
    check(hex_of(enc) == "1afa08d3dec047a643885163f1180476fa7ddb54c6a8029ea33f95796bf2ac4a", "A.2.1 enc");
    check(hex_of(sender.key) == "ad2744de8e17f4ebba575b3f5f5a8fa1f69c2a07f6e7500bc60ca6e3e3ec1c91", "A.2.1 key");
    check(hex_of(sender.base_nonce) == "5c4d98150661b848853b547f", "A.2.1 base_nonce");
    const diag::Bytes pt = bytes("4265617574792069732074727574682c20747275746820626561757479"), aad = bytes("436f756e742d30");
    check(hex_of(diag::chacha20_poly1305_seal(sender.key, sender.base_nonce.data(), aad.data(), aad.size(), pt.data(),
                                              pt.size())) ==
              "1c5250d8034ec2b784ba2cfd69dbdb8af406cfe3ff938e131f0def8c8b60b4db21993c62ce81883d2dd1b51a28",
          "A.2.1 sequence 0 ciphertext");
    diag::HpkeContext recipient;
    check(diag::hpke_setup_recipient(sk_r, enc, info_text, recipient) && recipient.key == sender.key &&
              recipient.base_nonce == sender.base_nonce,
          "A.2.1 SetupBaseR agrees");
    check(!diag::hpke_setup_sender(diag::Key{}, sk_e, info_text, enc, sender), "an all-zero shared secret is refused");
}

void envelope() {
    diag::Key secret{};
    check(diag::random_bytes(secret.data(), secret.size()), "the random source");
    const diag::Key public_key = diag::x25519_public(secret);
    const std::string report = diag::make_report(kBenchmark, {"line one", "line \"two\""}, true);
    check(diag::report_error(report).empty(), "the report wrapper passes its schema");
    const diag::Bytes sealed = diag::seal_report(report, public_key);
    check(sealed.size() > diag::kEnvelopeHeader + 16u && std::memcmp(sealed.data(), "NBDR\x01\x01\0\0", 8u) == 0,
          "the header: magic, version 1, zlib, reserved");
    const auto id = diag::key_id(public_key);
    check(std::memcmp(sealed.data() + 8, id.data(), 8u) == 0, "the key id");
    check(sealed.size() < report.size(), "the report is compressed");
    std::string opened;
    check(diag::open_report(sealed.data(), sealed.size(), secret, opened) == diag::OpenResult::Ok && opened == report,
          "seal then open round trip");
    check(diag::seal_report(report, public_key) != sealed, "every seal has its own ephemeral key");
    // Every header byte, every 7th byte of the ciphertext and the whole tag.
    for (std::size_t i = 0; i < sealed.size(); i += i < diag::kEnvelopeHeader || i + 16u >= sealed.size() ? 1u : 7u) {
        diag::Bytes bent = sealed;
        bent[i] ^= 0x40u;
        const diag::OpenResult result = diag::open_report(bent.data(), bent.size(), secret, opened);
        check(result != diag::OpenResult::Ok && opened.empty(), "every changed byte is refused");
    }
    check(diag::open_report(sealed.data(), sealed.size() - 1u, secret, opened) == diag::OpenResult::Tampered,
          "a truncated report is refused");
    check(diag::open_report(sealed.data(), 20u, secret, opened) == diag::OpenResult::NotEnvelope, "a short file is not a report");
    diag::Key other{};
    check(diag::random_bytes(other.data(), other.size()), "the random source");
    check(diag::open_report(sealed.data(), sealed.size(), other, opened) == diag::OpenResult::OtherKey, "a wrong key");
    diag::Bytes forged_id = sealed;
    std::memcpy(forged_id.data() + 8, diag::key_id(diag::x25519_public(other)).data(), 8u);
    check(diag::open_report(forged_id.data(), forged_id.size(), other, opened) == diag::OpenResult::Tampered,
          "a wrong key behind a forged key id still fails the tag");
    check(diag::seal_report(std::string(diag::kReportMax + 1u, ' '), public_key).empty(), "an oversized report is refused");
}

void schema() {
    check(diag::schema_error(kBenchmark).empty(), "the benchmark's own result passes");
    check(!diag::schema_error(replaced(kBenchmark, "\"mods_loaded\":false", "\"mods_loaded\":false,\"user\":\"x\"")).empty(),
          "an unknown field is refused");
    check(!diag::schema_error(replaced(kBenchmark, "\"frames\":540", "\"frames\":540,\"path\":\"C:/x\"")).empty(),
          "an unknown field deep inside is refused");
    check(!diag::schema_error(replaced(kBenchmark, "\"schema\":1", "\"schema\":2")).empty(), "another schema version");
    check(!diag::schema_error(replaced(kBenchmark, "\"logical_cores\":2", "\"logical_cores\":\"2\"")).empty(), "a wrong type");
    check(!diag::schema_error(replaced(kBenchmark, "\"logical_cores\":2", "\"logical_cores\":2.5")).empty(), "a fraction");
    check(!diag::schema_error(replaced(kBenchmark, "\"status\":\"complete\"", "\"status\":\"other\"")).empty(),
          "an unknown status");
    check(!diag::schema_error(replaced(kBenchmark, "\"failure\":\"\"", "\"failure\":\"" + std::string(97u, 'a') + "\"")).empty(),
          "a text over its size");
    check(!diag::schema_error(replaced(kBenchmark, "\"failure\":\"\"", "\"failure\":\"\\u00e9\"")).empty(),
          "a character outside printable ASCII");
    check(!diag::schema_error(replaced(kBenchmark, "\"mods_loaded\":false", "\"mods_loaded\":false,\"mods_loaded\":true")).empty(),
          "a duplicate key");
    check(!diag::schema_error(std::string(kBenchmark) + "x").empty(), "trailing bytes");
    check(!diag::schema_error("[1]").empty() && !diag::schema_error("").empty() && !diag::schema_error("{").empty(),
          "not an object");
    check(!diag::schema_error(std::string(64u, '[') + std::string(64u, ']')).empty(), "too deep");
    check(!diag::report_error(diag::make_report(kBenchmark, {std::string(201u, 'a')}, true)).empty(), "a log line over 200");
    check(diag::report_error(diag::make_report(kBenchmark, {}, false)).find("log") == std::string::npos &&
              diag::report_error(diag::make_report(kBenchmark, {}, false)).empty(),
          "a report without a log");
}

void stripping() {
    const std::vector<std::string> names = {"Alice Smith", "ALICE-PC"};
    check(diag::strip_log_line("[host:] pak C:\\Users\\Alice Smith\\Games\\game.pak loaded", names) ==
              "[host:] pak <path> loaded",
          "a Windows path with the user name in it");
    check(diag::strip_log_line("open /home/alice/.local/share/Nebula/logs/game.log", {}) == "open <path>",
          "a POSIX path");
    check(diag::strip_log_line("cache=\"D:/Nebula Data/cache\" ok", {}) == "cache=\"<path> <path>\" ok",
          "a quoted path with a space");
    check(diag::strip_log_line("machine ALICE-pc ready, user alice smith", names) == "machine <user> ready, user <user>",
          "names in any case");
    check(diag::strip_log_line("mail alice@example.com from 192.168.1.20", {}) == "mail <email> from <address>",
          "e-mail and IPv4 addresses");
    check(diag::strip_log_line("[benchmark] 3/13 BattleShipGalaxy probe", {}) == "[benchmark] 3/13 BattleShipGalaxy probe",
          "progress counts stay");
    check(diag::strip_log_line("~/x and \\\\server\\share and file:///c", {}) == "<path> and <path> and <path>",
          "home, UNC and file URLs");
    check(diag::strip_log_line(std::string("a\x01\xc3\xa9z"), {}) == "a???z", "non-printable bytes");
    check(diag::strip_log_line(std::string(300u, 'x'), {}).size() == 200u, "the line is cut at 200 bytes");
    std::string log;
    for (int i = 0; i < 300; ++i) log += "line " + std::to_string(i) + " C:\\private\\" + std::to_string(i) + "\r\n";
    const std::vector<std::string> tail = diag::log_tail(log, {});
    check(tail.size() == 200u && tail.front() == "line 100 <path>" && tail.back() == "line 299 <path>",
          "the last 200 lines, stripped");
}

#if defined(_WIN32)
// One loopback HTTP exchange: the request is captured, the answer is `status`.
struct LoopbackServer {
    SOCKET listener = INVALID_SOCKET;
    unsigned short port = 0;
    std::string request;
    std::thread thread;

    explicit LoopbackServer(int status) {
        WSADATA data{};
        check(WSAStartup(MAKEWORD(2, 2), &data) == 0, "winsock");
        listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        check(bind(listener, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == 0 && listen(listener, 1) == 0,
              "a loopback listener");
        int size = sizeof(address);
        getsockname(listener, reinterpret_cast<sockaddr*>(&address), &size);
        port = ntohs(address.sin_port);
        thread = std::thread([this, status] {
            const SOCKET client = accept(listener, nullptr, nullptr);
            if (client == INVALID_SOCKET) return;
            char buffer[4096];
            std::size_t want = std::string::npos;
            for (;;) {
                const int got = recv(client, buffer, sizeof(buffer), 0);
                if (got <= 0) break;
                request.append(buffer, static_cast<std::size_t>(got));
                const std::size_t end = request.find("\r\n\r\n");
                if (end != std::string::npos && want == std::string::npos) {
                    const std::size_t length = request.find("Content-Length: ");
                    want = end + 4u + (length == std::string::npos ? 0u : std::stoul(request.substr(length + 16u)));
                }
                if (want != std::string::npos && request.size() >= want) break;
            }
            const std::string answer = "HTTP/1.1 " + std::to_string(status) + " X\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
            send(client, answer.data(), static_cast<int>(answer.size()), 0);
            closesocket(client);
        });
    }
    ~LoopbackServer() {
        closesocket(listener);
        if (thread.joinable()) thread.join();
        WSACleanup();
    }
};

void posting() {
    const diag::Bytes body = {'N', 'B', 'D', 'R', 1, 1, 0, 0, 9, 9};
    {
        LoopbackServer server(204);
        const std::string url = "http://127.0.0.1:" + std::to_string(server.port) + "/v1/report";
        const diag::SendResult result = diag::post_envelope(url, body, true);
        server.thread.join();
        check(result.exit == diag::kOk, "a 204 is a sent report");
        check(server.request.rfind("POST /v1/report HTTP/1.1\r\n", 0) == 0, "one POST to the path");
        check(server.request.find("Content-Type: application/x-nebula-report\r\n") != std::string::npos, "the content type");
        check(server.request.find("Cookie") == std::string::npos, "no cookie");
        check(server.request.size() >= body.size() &&
                  server.request.compare(server.request.size() - body.size(), body.size(),
                                         reinterpret_cast<const char*>(body.data()), body.size()) == 0,
              "the body is the envelope, unchanged");
    }
    {
        LoopbackServer server(429);
        const diag::SendResult result =
            diag::post_envelope("http://127.0.0.1:" + std::to_string(server.port) + "/v1/report", body, true);
        server.thread.join();
        check(result.exit == diag::kRefused && result.line.find("429") != std::string::npos, "a refusal shows its status");
    }
    {
        LoopbackServer server(204);
        const diag::SendResult result =
            diag::post_envelope("http://127.0.0.1:" + std::to_string(server.port) + "/v1/report", body, false);
        check(result.exit == diag::kUsage, "plain http is refused outside the tests");
    }
    check(diag::post_envelope("http://example.invalid/v1/report", body, true).exit == diag::kUsage,
          "plain http to anything but loopback is refused");
    check(diag::post_envelope("https://127.0.0.1:1/v1/report", {}, true).exit == diag::kInvalid, "an empty body");
}
#endif

}  // namespace

int main() {
    try {
        primitives();
        hpke_vector();
        envelope();
        schema();
        stripping();
#if defined(_WIN32)
        posting();
#endif
        {
            const std::filesystem::path file = std::filesystem::temp_directory_path() / "diag_report_tests_benchmark.json";
            { std::ofstream(file, std::ios::binary) << kBenchmark; }
            std::string preview;
            check(diag::report_preview(file, false, preview).exit == diag::kOk &&
                      preview == diag::make_report(kBenchmark, {}, false),
                  "the preview is the report that would be sealed");
            { std::ofstream(file, std::ios::binary) << replaced(kBenchmark, "\"schema\":1", "\"schema\":1,\"x\":1"); }
            check(diag::report_preview(file, false, preview).exit == diag::kInvalid && preview.empty(),
                  "a refused result has no preview");
            std::filesystem::remove(file);
        }
        check(!diag::uploads_enabled(), "the sources name no endpoint: uploads are off");
        check(diag::send_report_file("missing.json", false).exit == diag::kDisabled, "--send-report refuses without one");
    } catch (const std::exception& failure) {
        std::cerr << "diag_report: " << failure.what() << '\n';
        return 1;
    }
    std::cout << "diag_report: all checks passed\n";
    return 0;
}
