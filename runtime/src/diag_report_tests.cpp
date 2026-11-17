// SPDX-License-Identifier: GPL-3.0-only
#include "galaxy/diag_report.h"
#include "galaxy/diag_endpoint.h"

#include <openssl/hpke.h>
#include <zlib.h>

#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
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

// A result as benchmark.cpp writes it (schema 3, from a real run): tools/telemetry/testdata/benchmark.json, which the
// telemetry tool's tests read too. Read by main before the tests run.
std::string kBenchmark;

std::string read_fixture() {
    std::ifstream in(NEBULA_BENCHMARK_FIXTURE, std::ios::binary);
    std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    while (!text.empty() && (text.back() == '\n' || text.back() == '\r')) text.pop_back();
    check(!text.empty(), "the benchmark fixture " NEBULA_BENCHMARK_FIXTURE " is readable");
    return text;
}

std::string replaced(std::string_view text, std::string_view from, std::string_view to) {
    std::string out(text);
    const std::size_t at = out.find(from);
    check(at != std::string::npos, "the sample holds the text a test changes");
    out.replace(at, from.size(), to);
    return out;
}

// RFC 7748 6.1 for the public key; RFC 9180 A.2.1 for the HPKE suite the envelope uses (BoringSSL's, from the
// sender's fixed ephemeral key).
void hpke_vector() {
    const diag::Key alice = key("77076d0a7318a57d3c16c17251b26645df4c2f87ebc0992ab177fba51db92c2a");
    check(hex_of(diag::x25519_public(alice)) == "8520f0098930a754748b7ddcb43ef75a0dbf3a0d26381af4eba4a98eaa9b4e6a",
          "X25519 public key, RFC 7748 6.1");
    const diag::Key sk_r = key("8057991eef8f1f1af18f4a9491d16a1ce333f695d4db8e38da75975c4478e0fb");
    const diag::Key pk_r = diag::x25519_public(sk_r);
    check(hex_of(pk_r) == "4310ee97d88cc1f088a5576c77ab0cf5c3ac797f3d95139c6c84b5429c59662a", "A.2.1 pkRm");
    const diag::Key sk_e = key("f4ec9b33b792c372c1d2c2063507b684ef925b8c75a42dbcbf57d63ccd381600");
    const diag::Bytes info = bytes("4f6465206f6e2061204772656369616e2055726e");
    const diag::Bytes pt = bytes("4265617574792069732074727574682c20747275746820626561757479"), aad = bytes("436f756e742d30");
    bssl::ScopedEVP_HPKE_CTX sender;
    std::uint8_t enc[EVP_HPKE_MAX_ENC_LENGTH];
    std::size_t enc_size = 0;
    check(EVP_HPKE_CTX_setup_sender_with_seed_for_testing(sender.get(), enc, &enc_size, sizeof(enc),
                                                          EVP_hpke_x25519_hkdf_sha256(), EVP_hpke_hkdf_sha256(),
                                                          EVP_hpke_chacha20_poly1305(), pk_r.data(), pk_r.size(),
                                                          info.data(), info.size(), sk_e.data(), sk_e.size()) == 1 &&
              hex_of(diag::Bytes(enc, enc + enc_size)) == "1afa08d3dec047a643885163f1180476fa7ddb54c6a8029ea33f95796bf2ac4a",
          "A.2.1 enc");
    diag::Bytes ct(pt.size() + EVP_HPKE_CTX_max_overhead(sender.get()));
    std::size_t ct_size = 0;
    check(EVP_HPKE_CTX_seal(sender.get(), ct.data(), &ct_size, ct.size(), pt.data(), pt.size(), aad.data(), aad.size()) == 1,
          "A.2.1 seal");
    ct.resize(ct_size);
    check(hex_of(ct) == "1c5250d8034ec2b784ba2cfd69dbdb8af406cfe3ff938e131f0def8c8b60b4db21993c62ce81883d2dd1b51a28",
          "A.2.1 sequence 0 ciphertext");
}

// The envelope's bytes as diag_report.h gives them, made by hand: open_report opens it, and a hand-made recipient
// opens what seal_report writes. Reports sealed before and after a change of library open the same way.
void envelope_format() {
    const diag::Key sk_r = key("8057991eef8f1f1af18f4a9491d16a1ce333f695d4db8e38da75975c4478e0fb");
    const diag::Key pk_r = diag::x25519_public(sk_r);
    const diag::Key sk_e = key("f4ec9b33b792c372c1d2c2063507b684ef925b8c75a42dbcbf57d63ccd381600");
    const std::string report = "a report, sealed by hand";
    uLongf packed_size = compressBound(static_cast<uLong>(report.size()));
    diag::Bytes packed(packed_size);
    check(compress2(packed.data(), &packed_size, reinterpret_cast<const Bytef*>(report.data()),
                    static_cast<uLong>(report.size()), Z_BEST_COMPRESSION) == Z_OK,
          "zlib");
    packed.resize(packed_size);
    const auto info = reinterpret_cast<const std::uint8_t*>(diag::kHpkeInfo.data());
    bssl::ScopedEVP_HPKE_CTX sender;
    std::uint8_t enc[EVP_HPKE_MAX_ENC_LENGTH];
    std::size_t enc_size = 0;
    check(EVP_HPKE_CTX_setup_sender_with_seed_for_testing(sender.get(), enc, &enc_size, sizeof(enc),
                                                          EVP_hpke_x25519_hkdf_sha256(), EVP_hpke_hkdf_sha256(),
                                                          EVP_hpke_chacha20_poly1305(), pk_r.data(), pk_r.size(), info,
                                                          diag::kHpkeInfo.size(), sk_e.data(), sk_e.size()) == 1 &&
              enc_size == 32u,
          "a hand-made sender");
    diag::Bytes envelope = {'N', 'B', 'D', 'R', 1, 1, 0, 0};
    const auto id = diag::key_id(pk_r);
    envelope.insert(envelope.end(), id.begin(), id.end());
    envelope.insert(envelope.end(), enc, enc + 32);
    check(envelope.size() == diag::kEnvelopeHeader, "the header's size");
    diag::Bytes sealed(packed.size() + EVP_HPKE_CTX_max_overhead(sender.get()));
    std::size_t sealed_size = 0;
    check(EVP_HPKE_CTX_seal(sender.get(), sealed.data(), &sealed_size, sealed.size(), packed.data(), packed.size(),
                            envelope.data(), envelope.size()) == 1,
          "a hand-made seal");
    envelope.insert(envelope.end(), sealed.begin(), sealed.begin() + static_cast<std::ptrdiff_t>(sealed_size));
    std::string opened;
    check(diag::open_report(envelope.data(), envelope.size(), sk_r, opened) == diag::OpenResult::Ok && opened == report,
          "open_report opens an envelope made from the format");

    const diag::Bytes written = diag::seal_report(report, pk_r);
    check(written.size() > diag::kEnvelopeHeader + 16u, "seal_report");
    bssl::ScopedEVP_HPKE_KEY recipient_key;
    bssl::ScopedEVP_HPKE_CTX recipient;
    diag::Bytes plain(written.size());
    std::size_t plain_size = 0;
    check(EVP_HPKE_KEY_init(recipient_key.get(), EVP_hpke_x25519_hkdf_sha256(), sk_r.data(), sk_r.size()) == 1 &&
              EVP_HPKE_CTX_setup_recipient(recipient.get(), recipient_key.get(), EVP_hpke_hkdf_sha256(),
                                           EVP_hpke_chacha20_poly1305(), written.data() + 16, 32u, info,
                                           diag::kHpkeInfo.size()) == 1 &&
              EVP_HPKE_CTX_open(recipient.get(), plain.data(), &plain_size, plain.size(),
                                written.data() + diag::kEnvelopeHeader, written.size() - diag::kEnvelopeHeader,
                                written.data(), diag::kEnvelopeHeader) == 1,
          "a hand-made recipient opens what seal_report writes");
    uLongf inflated_size = static_cast<uLongf>(report.size());
    std::string inflated(report.size(), '\0');
    check(uncompress(reinterpret_cast<Bytef*>(inflated.data()), &inflated_size, plain.data(),
                     static_cast<uLong>(plain_size)) == Z_OK &&
              inflated == report,
          "its content is the report");
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
    check(!diag::schema_error(replaced(kBenchmark, "\"stage\":\"CosmosGardenGalaxy\"", "\"stage\":\"CosmosGardenGalaxy\",\"path\":\"C:/x\"")).empty(),
          "an unknown field deep inside is refused");
    // The render thread's time by part (benchmark.h RenderPart), on a slow frame and on a stage: the run's result has
    // both, and passes (above).
    const std::string slow_part = "\"cause\":\"waiting\",\"render_parts\":{";
    check(diag::schema_error(replaced(kBenchmark, slow_part, slow_part + "\"texture create\":21.5,")).empty(),
          "a slow frame's further render part passes");
    check(!diag::schema_error(replaced(kBenchmark, slow_part, slow_part + "\"C:/x\":1,")).empty(),
          "a render part that is not one is refused");
    check(!diag::schema_error(replaced(kBenchmark, slow_part, slow_part + "\"texture create\":\"x\",")).empty(),
          "a render part's time that is no number is refused");
    const std::string stage_part = "\"render_parts\":{\"frame wait\":{";
    check(!diag::schema_error(replaced(kBenchmark, stage_part, stage_part + "\"name\":\"x\",")).empty(),
          "an unknown field in a stage's render part is refused");
    check(!diag::schema_error(replaced(kBenchmark, stage_part, "\"render_parts\":{\"C:/x\":{\"average\":0.1},\"frame wait\":{"))
               .empty(),
          "a stage's render part that is not one is refused");
    check(!diag::schema_error(replaced(kBenchmark, "\"schema\":3", "\"schema\":1")).empty(), "another schema version");
    check(!diag::schema_error(replaced(kBenchmark, "\"logical_cores\":32", "\"logical_cores\":\"32\"")).empty(), "a wrong type");
    check(!diag::schema_error(replaced(kBenchmark, "\"logical_cores\":32", "\"logical_cores\":32.5")).empty(), "a fraction");
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
    // release-check: allow (made-up user paths, which the report strips)
    check(diag::strip_log_line("[host:] pak C:\\Users\\Alice Smith\\Games\\game.pak loaded", names) ==
              "[host:] pak <path> loaded",
          "a Windows path with the user name in it");
    // release-check: allow
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
        kBenchmark = read_fixture();
        hpke_vector();
        envelope_format();
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
            { std::ofstream(file, std::ios::binary) << replaced(kBenchmark, "\"schema\":3", "\"schema\":3,\"x\":1"); }
            check(diag::report_preview(file, false, preview).exit == diag::kInvalid && preview.empty(),
                  "a refused result has no preview");
            std::filesystem::remove(file);
        }
        // The sources name no endpoint; a developer's build names one in diag_endpoint_local.h (git ignores it).
        if (std::string_view(diag::kEndpoint).empty()) {
            check(!diag::uploads_enabled(), "the sources name no endpoint: uploads are off");
            check(diag::send_report_file("missing.json", false).exit == diag::kDisabled,
                  "--send-report refuses without one");
        } else {
            check(diag::uploads_enabled(), "a named https endpoint with a valid key turns uploads on");
            check(diag::send_report_file("missing.json", false).exit != diag::kOk, "a missing file is not sent");
        }
    } catch (const std::exception& failure) {
        std::cerr << "diag_report: " << failure.what() << '\n';
        return 1;
    }
    std::cout << "diag_report: all checks passed\n";
    return 0;
}
