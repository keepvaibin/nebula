// SPDX-License-Identifier: GPL-3.0-only
// diag_report.h: the report's schema, the log tail's stripping, the one HTTPS POST and the host modes.
#include "galaxy/diag_report.h"

#include "galaxy/diag_endpoint.h"
#include "galaxy/platform_paths.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <functional>
#include <initializer_list>
#include <iterator>
#include <utility>

#if defined(_WIN32)
#include "galaxy/windows_api.h"
#include <shellapi.h>
#include <winhttp.h>
#pragma comment(lib, "winhttp.lib")
#pragma comment(lib, "shell32.lib")
#else
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif
#if defined(__ANDROID__)
#include <SDL3/SDL_system.h>
#include <jni.h>
#elif defined(NEBULA_UPDATE_CHECK)
#include "update/update_internal.h"
#endif

namespace galaxy::diag {
namespace {

// ---- strict JSON (RFC 8259): one value, no duplicate keys, depth <= 12 ----

struct Value {
    enum Type { Null, Bool, Number, String, Array, Object } type = Null;
    bool boolean = false;
    double number = 0.0;
    bool integer = false;  // no fraction, no exponent
    std::string text;
    std::vector<Value> items;
    std::vector<std::pair<std::string, Value>> fields;
};

class Parser {
public:
    explicit Parser(std::string_view text) : s_(text) {}
    bool parse(Value& out) {
        if (!value(out, 0)) return false;
        space();
        return i_ == s_.size();
    }

private:
    void space() {
        while (i_ < s_.size() && (s_[i_] == ' ' || s_[i_] == '\t' || s_[i_] == '\n' || s_[i_] == '\r')) ++i_;
    }
    bool literal(std::string_view word) {
        if (s_.substr(i_, word.size()) != word) return false;
        i_ += word.size();
        return true;
    }
    bool string(std::string& out) {
        if (i_ >= s_.size() || s_[i_] != '"') return false;
        ++i_;
        while (i_ < s_.size()) {
            const unsigned char c = static_cast<unsigned char>(s_[i_++]);
            if (c == '"') return true;
            if (c < 0x20u || c >= 0x7fu) return false;  // the schema's texts are printable ASCII
            if (c != '\\') {
                out.push_back(static_cast<char>(c));
                continue;
            }
            if (i_ >= s_.size()) return false;
            const char e = s_[i_++];
            switch (e) {
                case '"': out.push_back('"'); break;
                case '\\': out.push_back('\\'); break;
                case '/': out.push_back('/'); break;
                case 'b': case 'f': case 'n': case 'r': case 't': return false;  // control characters
                case 'u': {
                    if (i_ + 4u > s_.size()) return false;
                    unsigned code = 0;
                    for (int k = 0; k < 4; ++k) {
                        const char h = s_[i_++];
                        code = code * 16u + (h >= '0' && h <= '9'   ? static_cast<unsigned>(h - '0')
                                             : h >= 'a' && h <= 'f' ? static_cast<unsigned>(h - 'a' + 10)
                                             : h >= 'A' && h <= 'F' ? static_cast<unsigned>(h - 'A' + 10)
                                                                    : 0x10000u);
                    }
                    if (code < 0x20u || code >= 0x7fu) return false;
                    out.push_back(static_cast<char>(code));
                    break;
                }
                default: return false;
            }
        }
        return false;
    }
    bool number(Value& out) {
        const std::size_t start = i_;
        if (i_ < s_.size() && s_[i_] == '-') ++i_;
        if (i_ >= s_.size() || !std::isdigit(static_cast<unsigned char>(s_[i_]))) return false;
        if (s_[i_] == '0') {
            ++i_;
        } else {
            while (i_ < s_.size() && std::isdigit(static_cast<unsigned char>(s_[i_]))) ++i_;
        }
        bool integer = true;
        if (i_ < s_.size() && s_[i_] == '.') {
            integer = false;
            ++i_;
            if (i_ >= s_.size() || !std::isdigit(static_cast<unsigned char>(s_[i_]))) return false;
            while (i_ < s_.size() && std::isdigit(static_cast<unsigned char>(s_[i_]))) ++i_;
        }
        if (i_ < s_.size() && (s_[i_] == 'e' || s_[i_] == 'E')) {
            integer = false;
            ++i_;
            if (i_ < s_.size() && (s_[i_] == '+' || s_[i_] == '-')) ++i_;
            if (i_ >= s_.size() || !std::isdigit(static_cast<unsigned char>(s_[i_]))) return false;
            while (i_ < s_.size() && std::isdigit(static_cast<unsigned char>(s_[i_]))) ++i_;
        }
        if (i_ - start > 32u) return false;
        const std::string digits(s_.substr(start, i_ - start));
        out.type = Value::Number;
        out.number = std::strtod(digits.c_str(), nullptr);
        out.integer = integer;
        return std::isfinite(out.number);
    }
    bool value(Value& out, int depth) {
        if (depth > 12) return false;
        space();
        if (i_ >= s_.size()) return false;
        const char c = s_[i_];
        if (c == '{') {
            ++i_;
            out.type = Value::Object;
            space();
            if (i_ < s_.size() && s_[i_] == '}') {
                ++i_;
                return true;
            }
            for (;;) {
                space();
                std::string key;
                if (!string(key)) return false;
                for (const auto& field : out.fields) {
                    if (field.first == key) return false;
                }
                space();
                if (i_ >= s_.size() || s_[i_++] != ':') return false;
                Value item;
                if (!value(item, depth + 1)) return false;
                out.fields.emplace_back(std::move(key), std::move(item));
                space();
                if (i_ < s_.size() && s_[i_] == ',') {
                    ++i_;
                    continue;
                }
                return i_ < s_.size() && s_[i_++] == '}';
            }
        }
        if (c == '[') {
            ++i_;
            out.type = Value::Array;
            space();
            if (i_ < s_.size() && s_[i_] == ']') {
                ++i_;
                return true;
            }
            for (;;) {
                Value item;
                if (!value(item, depth + 1)) return false;
                out.items.push_back(std::move(item));
                space();
                if (i_ < s_.size() && s_[i_] == ',') {
                    ++i_;
                    continue;
                }
                return i_ < s_.size() && s_[i_++] == ']';
            }
        }
        if (c == '"') {
            out.type = Value::String;
            return string(out.text);
        }
        if (c == 't' || c == 'f') {
            out.type = Value::Bool;
            out.boolean = c == 't';
            return literal(c == 't' ? "true" : "false");
        }
        if (c == 'n') return literal("null");
        return number(out);
    }

    std::string_view s_;
    std::size_t i_ = 0;
};

// ---- the schema: benchmark result schema 1 (benchmark.cpp write_result) ----

using Rule = std::function<std::string(const Value&)>;
struct Field {
    const char* name;
    Rule rule;
};

Rule nullable(Rule rule) {
    return [rule](const Value& v) { return v.type == Value::Null ? std::string() : rule(v); };
}
Rule integer() {
    return [](const Value& v) {
        return v.type == Value::Number && v.integer && v.number >= 0.0 && v.number <= 9007199254740992.0
                   ? std::string()
                   : std::string("not a whole number");
    };
}
Rule number() {
    return [](const Value& v) { return v.type == Value::Number ? std::string() : std::string("not a number"); };
}
Rule boolean() {
    return [](const Value& v) { return v.type == Value::Bool ? std::string() : std::string("not true or false"); };
}
// Printable ASCII; quotes and backslashes only where `quotes` (log lines).
Rule text(std::size_t max, bool quotes = false) {
    return [max, quotes](const Value& v) {
        if (v.type != Value::String) return std::string("not a text");
        if (v.text.size() > max) return std::string("text too long");
        for (const char c : v.text) {
            if (!quotes && (c == '"' || c == '\\')) return std::string("a quote in a text");
        }
        return std::string();
    };
}
Rule one_of(std::initializer_list<const char*> names) {
    std::vector<std::string> allowed(names.begin(), names.end());
    return [allowed](const Value& v) {
        if (v.type == Value::String && std::find(allowed.begin(), allowed.end(), v.text) != allowed.end()) return std::string();
        return std::string("not one of the known values");
    };
}
Rule object(std::vector<Field> fields) {
    return [fields](const Value& v) {
        if (v.type != Value::Object) return std::string("not an object");
        for (const auto& [key, item] : v.fields) {
            const auto field = std::find_if(fields.begin(), fields.end(), [&](const Field& f) { return key == f.name; });
            if (field == fields.end()) return "field \"" + key + "\" is not in the schema";
            if (std::string error = field->rule(item); !error.empty()) return key + ": " + error;
        }
        return std::string();
    };
}
Rule list(Rule item, std::size_t max) {
    return [item, max](const Value& v) {
        if (v.type != Value::Array) return std::string("not a list");
        if (v.items.size() > max) return std::string("too many entries");
        for (std::size_t i = 0; i < v.items.size(); ++i) {
            if (std::string error = item(v.items[i]); !error.empty()) return "[" + std::to_string(i) + "] " + error;
        }
        return std::string();
    };
}
// An object whose keys are names (pass names, probe cases): short printable keys, each value by `item`.
Rule names_to(Rule item, std::size_t max) {
    const Rule key_rule = text(48);
    return [item, max, key_rule](const Value& v) {
        if (v.type != Value::Object) return std::string("not an object");
        if (v.fields.size() > max) return std::string("too many entries");
        for (const auto& [key, value] : v.fields) {
            Value name;
            name.type = Value::String;
            name.text = key;
            if (!key_rule(name).empty()) return std::string("a name that is not a short text");
            if (std::string error = item(value); !error.empty()) return key + ": " + error;
        }
        return std::string();
    };
}

std::vector<Field> with(std::vector<Field> a, const std::vector<Field>& b) {
    a.insert(a.end(), b.begin(), b.end());
    return a;
}

const Rule& benchmark_schema() {
    static const Rule schema = [] {
        const Rule n = nullable(number());
        const Rule settings = object({{"internal_height", integer()}, {"upscaler", integer()}, {"msaa_samples", integer()},
                                      {"ambient_occlusion", integer()}, {"ambient_occlusion_ssao", integer()},
                                      {"enhanced_lighting", integer()}, {"per_pixel_lighting", integer()},
                                      {"aa", integer()}, {"smaa_quality", integer()}, {"bloom", integer()},
                                      {"vsync_interval", integer()}, {"max_fps", n}, {"frame_generation", integer()},
                                      {"ray_tracing_enabled", integer()}, {"display_mode", integer()}});
        const std::vector<Field> result = {
            {"load_ms", n},
            {"frames", integer()},
            {"pixels", n},
            {"settings_held", boolean()},
            {"interval_ms", object({{"p50", n}, {"p95", n}, {"p99", n}, {"worst", n}, {"over_16_7", integer()},
                                    {"over_33_3", integer()}})},
            {"gpu_ms", n},
            {"gpu_pass_ms", names_to(n, 64)},
            {"cpu_ms", object({{"game", n}, {"render", n}, {"audio", n}, {"process", n}})},
            {"draws", nullable(integer())},
            {"triangles", nullable(integer())},
            {"gpu_memory_peak_mb", integer()},
            {"process_peak_mb", integer()},
        };
        const Rule scene =
            object(with({{"stage", text(64)}, {"scenario", integer()}, {"grand_stars", integer()}}, result));
        const Rule probe_run = object(with({{"case", text(48)}}, result));
        return object({
            {"schema", [](const Value& v) {
                 return v.type == Value::Number && v.integer && v.number == 1.0 ? std::string() : std::string("not 1");
             }},
            {"app", object({{"version", text(48)}, {"commit", nullable(text(48))}})},
            {"created_utc", text(24)},
            {"status", one_of({"complete", "aborted", "failed"})},
            {"failure", text(96)},
            {"backend", text(16)},
            {"device", object({{"gpu", text(96)}, {"vendor_id", integer()}, {"device_id", integer()},
                               {"driver_version", nullable(text(48))}, {"api_level", nullable(text(48))},
                               {"shared_memory", boolean()}, {"gpu_budget_mb", integer()}, {"cpu", text(96)},
                               {"logical_cores", integer()}, {"ram_mb", integer()}, {"os", text(96)},
                               {"output", list(integer(), 2)}, {"refresh_hz", n}})},
            {"mods_loaded", boolean()},
            {"settings", settings},
            {"measured_with", settings},
            {"limits", list(text(256), 8)},
            {"scenes", list(scene, 16)},
            {"probe", object({{"stage", text(64)},
                              {"runs", list(probe_run, 16)},
                              {"fit", object({{"valid", boolean()}, {"fixed_ms", n}, {"ms_per_mpixel", n}})},
                              {"price_ms_per_mpixel", names_to(n, 16)}})},
            {"thermal", nullable(object({{"stage", text(64)}, {"gpu_ms_first", n}, {"gpu_ms_last", n}, {"gpu_ratio", n},
                                         {"p95_first", n}, {"p95_last", n}}))},
            {"recommendation",
             nullable(object({{"budget_ms", n}, {"predicted_ms", n}, {"fits", boolean()}, {"cpu_bound", boolean()},
                              {"cpu_thread", nullable(text(16))}, {"height_left_out", boolean()},
                              {"settings", list(object({{"key", text(32)}, {"current", integer()},
                                                        {"recommended", integer()}}),
                                                16)}}))},
            {"summary", object({{"verdict", text(16)}, {"worst_stage", text(64)}, {"worst_gpu_ms", n},
                                {"worst_p95_ms", n}})},
        });
    }();
    return schema;
}

constexpr std::size_t kLogLines = 200;
constexpr std::size_t kLogBytes = 32u * 1024u;
constexpr std::size_t kLogLineBytes = 200;

const Rule& report_schema() {
    static const Rule schema = object({
        {"report", [](const Value& v) {
             return v.type == Value::Number && v.integer && v.number == 1.0 ? std::string() : std::string("not 1");
         }},
        {"benchmark", benchmark_schema()},
        {"log", list(text(kLogLineBytes, true), kLogLines)},
    });
    return schema;
}

std::string validate(std::string_view json, const Rule& schema, std::size_t max_size) {
    if (json.size() > max_size) return "too large";
    Value root;
    if (!Parser(json).parse(root)) return "not strict JSON";
    if (root.type != Value::Object) return "not an object";
    return schema(root);
}

bool read_file(const std::filesystem::path& path, std::size_t max, std::string& out) {
    std::ifstream file(path, std::ios::binary);
    if (!file) return false;
    out.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
    return !file.bad() && out.size() <= max;
}

// The last `max` bytes of a file (a log can be long).
std::string read_tail(const std::filesystem::path& path, std::size_t max) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) return {};
    const std::streamoff size = file.tellg();
    const std::streamoff start = size > static_cast<std::streamoff>(max) ? size - static_cast<std::streamoff>(max) : 0;
    file.seekg(start);
    std::string out(static_cast<std::size_t>(size - start), '\0');
    file.read(out.data(), static_cast<std::streamsize>(out.size()));
    out.resize(static_cast<std::size_t>(file.gcount()));
    return out;
}

bool write_file(const std::filesystem::path& path, const void* data, std::size_t size) {
    std::filesystem::path part = path;
    part += ".part";
    {
        std::ofstream file(part, std::ios::binary | std::ios::trunc);
        if (!file) return false;
        file.write(static_cast<const char*>(data), static_cast<std::streamsize>(size));
        if (!file) return false;
    }
    std::error_code error;
    std::filesystem::rename(part, path, error);
    if (error) std::filesystem::remove(part, error);
    return !error;
}

std::string json_text(std::string_view text) {
    std::string out = "\"";
    for (const char c : text) {
        if (c == '"' || c == '\\') out.push_back('\\');
        out.push_back(c);
    }
    out.push_back('"');
    return out;
}

std::string lower(std::string_view text) {
    std::string out(text);
    for (char& c : out) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return out;
}

// An environment variable as UTF-8 (empty when unset).
std::string environment(const char* name) {
#if defined(_WIN32)
    const std::wstring wide_name(name, name + std::strlen(name));
    const std::wstring value = windows::environment_text(wide_name.c_str());
    if (value.empty()) return {};
    const int size = WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
    std::string out(static_cast<std::size_t>(std::max(size, 0)), '\0');
    WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), out.data(), size, nullptr, nullptr);
    return out;
#else
    const char* value = std::getenv(name);
    return value != nullptr ? std::string(value) : std::string();
#endif
}

// The names a log can hold that identify the player or the machine (strip_log_line).
std::vector<std::string> identifying_names() {
    std::vector<std::string> names;
    const auto add = [&names](std::string name) {
        while (!name.empty() && (name.back() == '/' || name.back() == '\\')) name.pop_back();
        if (name.size() >= 2u && std::find(names.begin(), names.end(), name) == names.end()) names.push_back(name);
    };
    for (const char* variable : {"USERNAME", "USER", "LOGNAME", "COMPUTERNAME", "HOSTNAME"}) {
        add(environment(variable));
    }
    for (const char* variable : {"USERPROFILE", "HOME"}) {
        std::string path = environment(variable);
        while (!path.empty() && (path.back() == '/' || path.back() == '\\')) path.pop_back();
        const std::size_t slash = path.find_last_of("/\\");
        add(slash == std::string::npos ? path : path.substr(slash + 1u));
    }
#if !defined(_WIN32)
    char host[256] = {};
    if (gethostname(host, sizeof(host) - 1u) == 0) add(host);
#endif
    return names;
}

// The last run's log: logs/latest.log beside the settings on Windows, <data>/logs/game.log elsewhere.
std::filesystem::path last_log() {
#if defined(_WIN32)
    const std::filesystem::path folder =
        !launch_args().settings.empty() ? launch_args().settings : nebula_data_directory();
    return folder.empty() ? std::filesystem::path{} : folder / "logs" / "latest.log";
#else
    const std::filesystem::path data = nebula_data_directory();
    return data.empty() ? std::filesystem::path{} : data / "logs" / "game.log";
#endif
}

bool build_key(Key& key) { return key_from_hex(kPublicKey, key); }

// ---- the one POST, per platform ----

constexpr char kContentType[] = "application/x-nebula-report";

SendResult answered(long status, std::size_t bytes) {
    if (status >= 200 && status < 300) return {kOk, "sent: " + std::to_string(bytes) + " bytes"};
    return {kRefused, "the server refused the report (HTTP " + std::to_string(status) + ")"};
}

[[maybe_unused]] bool loopback_http(std::string_view url) { return url.rfind("http://127.0.0.1:", 0) == 0 || url.rfind("http://127.0.0.1/", 0) == 0; }

#if defined(_WIN32)
SendResult post(std::string_view url, const Bytes& body, bool allow_http) {
    std::wstring wide(url.begin(), url.end());
    URL_COMPONENTS parts{};
    parts.dwStructSize = sizeof(parts);
    wchar_t host[256] = {};
    wchar_t path[1024] = {};
    parts.lpszHostName = host;
    parts.dwHostNameLength = static_cast<DWORD>(std::size(host));
    parts.lpszUrlPath = path;
    parts.dwUrlPathLength = static_cast<DWORD>(std::size(path));
    if (!WinHttpCrackUrl(wide.c_str(), 0u, 0u, &parts)) return {kUsage, "the endpoint is not a URL"};
    const bool secure = parts.nScheme == INTERNET_SCHEME_HTTPS;
    if (!secure && !(allow_http && loopback_http(url))) return {kUsage, "the endpoint is not https"};
    struct Handle {
        HINTERNET h = nullptr;
        ~Handle() {
            if (h != nullptr) WinHttpCloseHandle(h);
        }
    } session, connection, request;
    session.h = WinHttpOpen(L"Nebula", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0u);
    if (session.h == nullptr) return {kNetwork, "no HTTPS client"};
    DWORD protocols = WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2 | WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_3;
    WinHttpSetOption(session.h, WINHTTP_OPTION_SECURE_PROTOCOLS, &protocols, sizeof(protocols));
    // Name resolution, connection, sending and the answer share the 10 seconds.
    WinHttpSetTimeouts(session.h, 2000, 3000, 2500, 2500);
    connection.h = WinHttpConnect(session.h, host, parts.nPort, 0u);
    if (connection.h == nullptr) return {kNetwork, "cannot connect"};
    request.h = WinHttpOpenRequest(connection.h, L"POST", path, nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                                   secure ? WINHTTP_FLAG_SECURE : 0u);
    if (request.h == nullptr) return {kNetwork, "cannot open the request"};
    DWORD never = WINHTTP_OPTION_REDIRECT_POLICY_NEVER;
    WinHttpSetOption(request.h, WINHTTP_OPTION_REDIRECT_POLICY, &never, sizeof(never));
    DWORD no_cookies = WINHTTP_DISABLE_COOKIES;
    WinHttpSetOption(request.h, WINHTTP_OPTION_DISABLE_FEATURE, &no_cookies, sizeof(no_cookies));
    const std::wstring headers = L"Content-Type: application/x-nebula-report\r\n";
    if (!WinHttpSendRequest(request.h, headers.c_str(), static_cast<DWORD>(headers.size()),
                            const_cast<std::uint8_t*>(body.data()), static_cast<DWORD>(body.size()),
                            static_cast<DWORD>(body.size()), 0u) ||
        !WinHttpReceiveResponse(request.h, nullptr)) {
        return {kNetwork, "no answer (error " + std::to_string(GetLastError()) + ")"};
    }
    DWORD status = 0, size = sizeof(status);
    if (!WinHttpQueryHeaders(request.h, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX,
                             &status, &size, WINHTTP_NO_HEADER_INDEX)) {
        return {kNetwork, "no status in the answer"};
    }
    return answered(static_cast<long>(status), body.size());
}
#elif defined(__ANDROID__)
// The platform's own HTTPS stack (java.net.HttpURLConnection) through JNI: its TLS and trust store.
SendResult post(std::string_view url, const Bytes& body, bool allow_http) {
    if (url.rfind("https://", 0) != 0 && !(allow_http && loopback_http(url))) return {kUsage, "the endpoint is not https"};
    auto* env = static_cast<JNIEnv*>(SDL_GetAndroidJNIEnv());
    if (env == nullptr || env->PushLocalFrame(32) != 0) return {kNetwork, "no HTTPS client"};
    // Every call is checked: a pending Java exception ends the send, and no JNI call is made while one is pending.
    const auto failed = [env]() {
        if (!env->ExceptionCheck()) return false;
        env->ExceptionClear();
        return true;
    };
    const auto text = [&](const char* value) {
        jstring s = env->NewStringUTF(value);
        return failed() ? nullptr : s;
    };
    const auto method = [&](jclass type, const char* name, const char* signature) {
        jmethodID id = env->GetMethodID(type, name, signature);
        return failed() ? nullptr : id;
    };
    const auto call = [&](jobject object, jclass type, const char* name, const char* signature, auto... args) {
        const jmethodID id = method(type, name, signature);
        if (id == nullptr) return false;
        env->CallVoidMethod(object, id, args...);
        return !failed();
    };
    SendResult result{kNetwork, "no answer"};
    do {
        jclass url_class = env->FindClass("java/net/URL");
        if (failed() || url_class == nullptr) break;
        jclass http_class = env->FindClass("java/net/HttpURLConnection");
        if (failed() || http_class == nullptr) break;
        jclass stream_class = env->FindClass("java/io/OutputStream");
        if (failed() || stream_class == nullptr) break;
        const std::string url_text(url);
        jstring address = text(url_text.c_str());
        jstring post_method = address != nullptr ? text("POST") : nullptr;
        jstring type_key = post_method != nullptr ? text("Content-Type") : nullptr;
        jstring type_value = type_key != nullptr ? text(kContentType) : nullptr;
        jstring agent_key = type_value != nullptr ? text("User-Agent") : nullptr;
        jstring agent_value = agent_key != nullptr ? text("Nebula") : nullptr;
        if (agent_value == nullptr) break;
        const jmethodID make_url = method(url_class, "<init>", "(Ljava/lang/String;)V");
        if (make_url == nullptr) break;
        jobject url_object = env->NewObject(url_class, make_url, address);
        if (failed() || url_object == nullptr) break;
        const jmethodID open = method(url_class, "openConnection", "()Ljava/net/URLConnection;");
        if (open == nullptr) break;
        jobject connection = env->CallObjectMethod(url_object, open);
        if (failed() || connection == nullptr || !env->IsInstanceOf(connection, http_class)) break;
        constexpr const char* kSetProperty = "(Ljava/lang/String;Ljava/lang/String;)V";
        if (!call(connection, http_class, "setRequestMethod", "(Ljava/lang/String;)V", post_method) ||
            !call(connection, http_class, "setInstanceFollowRedirects", "(Z)V", JNI_FALSE) ||
            !call(connection, http_class, "setUseCaches", "(Z)V", JNI_FALSE) ||
            !call(connection, http_class, "setDoOutput", "(Z)V", JNI_TRUE) ||
            !call(connection, http_class, "setConnectTimeout", "(I)V", static_cast<jint>(5000)) ||
            !call(connection, http_class, "setReadTimeout", "(I)V", static_cast<jint>(5000)) ||
            !call(connection, http_class, "setFixedLengthStreamingMode", "(I)V", static_cast<jint>(body.size())) ||
            !call(connection, http_class, "setRequestProperty", kSetProperty, type_key, type_value) ||
            !call(connection, http_class, "setRequestProperty", kSetProperty, agent_key, agent_value)) {
            break;
        }
        jbyteArray bytes = env->NewByteArray(static_cast<jsize>(body.size()));
        if (failed() || bytes == nullptr) break;
        env->SetByteArrayRegion(bytes, 0, static_cast<jsize>(body.size()), reinterpret_cast<const jbyte*>(body.data()));
        const jmethodID output = method(http_class, "getOutputStream", "()Ljava/io/OutputStream;");
        if (output == nullptr) break;
        jobject stream = env->CallObjectMethod(connection, output);
        if (failed() || stream == nullptr) {
            result.line = "cannot connect";
            break;
        }
        const bool written = call(stream, stream_class, "write", "([B)V", bytes);
        const bool closed = call(stream, stream_class, "close", "()V");
        jint status = 0;
        if (written && closed) {
            if (const jmethodID code = method(http_class, "getResponseCode", "()I"); code != nullptr) {
                status = env->CallIntMethod(connection, code);
                if (failed()) status = 0;
            }
        }
        call(connection, http_class, "disconnect", "()V");
        if (status > 0) result = answered(status, body.size());
    } while (false);
    env->PopLocalFrame(nullptr);
    return result;
}
#elif defined(NEBULA_UPDATE_CHECK)
// The update check's transport (update_internal.h): libcurl on Linux, NSURLSession on macOS.
SendResult post(std::string_view url, const Bytes& body, bool allow_http) {
    (void)allow_http;
    if (url.rfind("https://", 0) != 0) return {kUsage, "the endpoint is not https"};
    const int status = update::detail::platform_transport().post(std::string(url), kContentType, body, 10000);
    if (status <= 0) return {kNetwork, "no answer (no connection, timeout or no HTTPS client)"};
    return answered(status, body.size());
}
#else
SendResult post(std::string_view, const Bytes&, bool) { return {kNetwork, "no HTTPS client in this build"}; }
#endif

const char* open_result_text(OpenResult result) {
    switch (result) {
        case OpenResult::Ok: return "opened";
        case OpenResult::NotEnvelope: return "not a Nebula report";
        case OpenResult::OtherKey: return "sealed to another key";
        case OpenResult::Tampered: return "changed after sealing, or a forged report";
        case OpenResult::BadContent: return "its content does not decompress";
    }
    return "?";
}

int print(int code, const std::string& line) {
    std::fprintf(code == kOk ? stdout : stderr, "[diag] %s\n", line.c_str());
    std::fflush(code == kOk ? stdout : stderr);
    return code;
}

}  // namespace

std::string hex(const std::uint8_t* data, std::size_t size) {
    static constexpr char kDigits[] = "0123456789abcdef";
    std::string out;
    out.reserve(size * 2u);
    for (std::size_t i = 0; i < size; ++i) {
        out.push_back(kDigits[data[i] >> 4u]);
        out.push_back(kDigits[data[i] & 15u]);
    }
    return out;
}

bool key_from_hex(std::string_view text, Key& key) {
    if (text.size() != 64u) return false;
    std::uint8_t any = 0;
    for (std::size_t i = 0; i < 32u; ++i) {
        unsigned byte = 0;
        for (std::size_t k = 0; k < 2u; ++k) {
            const char c = text[2u * i + k];
            const int digit = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
            if (digit < 0) return false;
            byte = byte * 16u + static_cast<unsigned>(digit);
        }
        key[i] = static_cast<std::uint8_t>(byte);
        any |= key[i];
    }
    return any != 0u;
}

std::string schema_error(std::string_view benchmark_json) {
    return validate(benchmark_json, benchmark_schema(), kEnvelopeMax);
}

std::string report_error(std::string_view report_json) { return validate(report_json, report_schema(), kReportMax); }

std::string make_report(std::string_view benchmark_json, const std::vector<std::string>& log_lines, bool with_log) {
    while (!benchmark_json.empty() && std::isspace(static_cast<unsigned char>(benchmark_json.back()))) {
        benchmark_json.remove_suffix(1u);
    }
    while (!benchmark_json.empty() && std::isspace(static_cast<unsigned char>(benchmark_json.front()))) {
        benchmark_json.remove_prefix(1u);
    }
    std::string out = "{\"report\":1,\"benchmark\":";
    out += benchmark_json;
    if (with_log) {
        out += ",\"log\":[";
        for (std::size_t i = 0; i < log_lines.size(); ++i) {
            if (i != 0u) out += ',';
            out += json_text(log_lines[i]);
        }
        out += ']';
    }
    out += '}';
    return out;
}

// User and computer names are replaced first, anywhere, in any case, by <user> (so a name with a space inside a path
// leaves the path one token). Then a token (a run between spaces, quotes, brackets, commas, semicolons, '=' and '|')
// becomes <path> when it holds a backslash, or a slash and any letter, or starts with '~' or a drive letter and
// colon; <email> when it holds '@' with a dot after it; <address> when it is four dot-separated numbers. Characters
// outside printable ASCII become '?' first; the line is cut at 200 bytes last.
std::string strip_log_line(std::string_view line, const std::vector<std::string>& names) {
    // A name outside ASCII (UTF-8) is matched byte for byte after the same mapping to '?'.
    const auto printable = [](std::string_view in) {
        std::string out;
        for (const char c : in) {
            out.push_back(static_cast<unsigned char>(c) >= 0x20u && static_cast<unsigned char>(c) < 0x7fu ? c : '?');
        }
        return out;
    };
    std::string text = printable(line);
    std::string folded = lower(text);
    for (const std::string& name : names) {
        const std::string needle = lower(printable(name));
        if (needle.size() < 2u) continue;
        for (std::size_t at = folded.find(needle); at != std::string::npos; at = folded.find(needle, at + 6u)) {
            text.replace(at, needle.size(), "<user>");
            folded.replace(at, needle.size(), "<user>");
        }
    }
    const auto delimiter = [](char c) { return c != '\0' && std::strchr(" \t\"'`()[]{},;=|", c) != nullptr; };
    std::string out;
    std::size_t i = 0;
    while (i < text.size()) {
        if (delimiter(text[i])) {
            out.push_back(text[i++]);
            continue;
        }
        std::size_t end = i;
        while (end < text.size() && !delimiter(text[end])) ++end;
        const std::string_view token(text.data() + i, end - i);
        const bool letters = std::any_of(token.begin(), token.end(), [](char c) { return std::isalpha(static_cast<unsigned char>(c)) != 0; });
        const bool path = token.find('\\') != std::string_view::npos ||
                          (token.find('/') != std::string_view::npos && letters) || token.front() == '~' ||
                          (token.size() >= 2u && std::isalpha(static_cast<unsigned char>(token[0])) && token[1] == ':' &&
                           (token.size() == 2u || token[2] == '/' || token[2] == '\\'));
        const std::size_t at = token.find('@');
        const bool email = at != std::string_view::npos && token.find('.', at) != std::string_view::npos;
        const bool address = std::count(token.begin(), token.end(), '.') == 3 &&
                             std::all_of(token.begin(), token.end(), [](char c) { return c == '.' || std::isdigit(static_cast<unsigned char>(c)); });
        if (path) {
            out += "<path>";
        } else if (email) {
            out += "<email>";
        } else if (address) {
            out += "<address>";
        } else {
            out.append(token);
        }
        i = end;
    }
    if (out.size() > kLogLineBytes) out.resize(kLogLineBytes);
    return out;
}

std::vector<std::string> log_tail(const std::string& text, const std::vector<std::string>& names) {
    std::vector<std::string_view> lines;
    std::size_t start = 0;
    while (start < text.size()) {
        std::size_t end = text.find('\n', start);
        if (end == std::string::npos) end = text.size();
        std::string_view line(text.data() + start, end - start);
        if (!line.empty() && line.back() == '\r') line.remove_suffix(1u);
        lines.push_back(line);
        start = end + 1u;
    }
    const std::size_t first = lines.size() > kLogLines ? lines.size() - kLogLines : 0u;
    std::vector<std::string> out;
    std::size_t bytes = 0;
    for (std::size_t i = first; i < lines.size(); ++i) {
        out.push_back(strip_log_line(lines[i], names));
        bytes += out.back().size() + 3u;
    }
    std::size_t drop = 0;
    while (bytes > kLogBytes && drop < out.size()) bytes -= out[drop++].size() + 3u;
    out.erase(out.begin(), out.begin() + static_cast<std::ptrdiff_t>(drop));
    return out;
}

bool uploads_enabled() {
    Key key{};
    return std::string_view(kEndpoint).rfind("https://", 0) == 0 && build_key(key);
}

SendResult post_envelope(std::string_view url, const Bytes& body, bool allow_loopback_http) {
    if (body.empty() || body.size() > kEnvelopeMax) return {kInvalid, "the sealed report is empty or too large"};
    return post(url, body, allow_loopback_http);
}

namespace {

// The report a file makes, sealed to `key`; the error line when it cannot.
SendResult seal_file(const std::filesystem::path& file, bool with_log, const Key& key, Bytes& envelope) {
    std::string report;
    if (SendResult built = report_preview(file, with_log, report); built.exit != kOk) return built;
    envelope = seal_report(report, key);
    if (envelope.empty()) return {kInvalid, "the report could not be sealed"};
    return {kOk, std::string()};
}

std::filesystem::path sent_marker(const std::filesystem::path& file) {
    std::filesystem::path marker = file;
    marker += ".sent";
    return marker;
}

}  // namespace

SendResult report_preview(const std::filesystem::path& file, bool with_log, std::string& report) {
    report.clear();
    std::string benchmark;
    if (!read_file(file, kEnvelopeMax, benchmark)) return {kInvalid, "the benchmark result cannot be read"};
    if (std::string error = schema_error(benchmark); !error.empty()) {
        return {kInvalid, "refused: the benchmark result does not match its schema (" + error + ")"};
    }
    std::vector<std::string> lines;
    if (with_log) {
        const std::filesystem::path log = last_log();
        if (!log.empty()) lines = log_tail(read_tail(log, 256u * 1024u), identifying_names());
    }
    std::string built = make_report(benchmark, lines, with_log);
    if (std::string error = report_error(built); !error.empty()) return {kInvalid, "refused: " + error};
    report = std::move(built);
    return {kOk, std::string()};
}

SendResult send_report_file(const std::filesystem::path& file, bool with_log) {
    Key key{};
    if (!uploads_enabled() || !build_key(key)) return {kDisabled, "uploads are off in this build"};
    std::error_code error;
    if (std::filesystem::exists(sent_marker(file), error)) return {kAlreadySent, "this report was sent before"};
    Bytes envelope;
    if (SendResult sealed = seal_file(file, with_log, key, envelope); sealed.exit != kOk) return sealed;
    SendResult result = post_envelope(kEndpoint, envelope);
    if (result.exit == kOk) write_file(sent_marker(file), "", 0u);
    return result;
}

int run_command_line(int argc, char** argv) {
    if (argc < 2) return -1;
    const std::string_view mode = argv[1];
    if (mode != "--send-report" && mode != "--save-sealed" && mode != "--report-enabled" && mode != "--open-report" &&
        mode != "--report-keygen") {
        return -1;
    }
#if defined(_WIN32)
    int wide_count = 0;
    LPWSTR* const wide = CommandLineToArgvW(GetCommandLineW(), &wide_count);
#endif
    const auto path_at = [&](int index) {
#if defined(_WIN32)
        if (wide != nullptr && index < wide_count) return std::filesystem::path(wide[index]);
#endif
        return std::filesystem::path(argv[index]);
    };
    std::filesystem::path report, sealed_out, opened, key_file, keygen;
    std::string public_key;
    bool with_log = false, enabled_query = false, preview = false, usage = (argc - 1) % 2 != 0;
    for (int i = 1; i + 1 < argc; i += 2) {
        const std::string_view key = argv[i];
        if (key == "--send-report") {
            report = path_at(i + 1);
        } else if (key == "--preview") {
            preview = std::strcmp(argv[i + 1], "1") == 0;
        } else if (key == "--with-log") {
            with_log = std::strcmp(argv[i + 1], "1") == 0;
        } else if (key == "--save-sealed") {
            sealed_out = path_at(i + 1);
        } else if (key == "--public-key") {
            public_key = argv[i + 1];
        } else if (key == "--report-enabled") {
            enabled_query = true;
        } else if (key == "--open-report") {
            opened = path_at(i + 1);
        } else if (key == "--key") {
            key_file = path_at(i + 1);
        } else if (key == "--report-keygen") {
            keygen = path_at(i + 1);
        } else if (key != "--data" && key != "--settings") {  // read by read_launch_args: the log's folder
            usage = true;
        }
    }
#if defined(_WIN32)
    if (wide != nullptr) LocalFree(wide);
#endif
    if (usage) {
        return print(kUsage, "usage: --send-report <benchmark.json> [--with-log 1] [--preview 1 | --save-sealed <out> [--public-key <hex>]]"
                             " | --report-enabled 1 | --open-report <file> --key <private key file>"
                             " | --report-keygen <folder> (docs/DIAGNOSTICS_UPLOAD.md)");
    }
    if (enabled_query) {
        return uploads_enabled() ? print(kOk, "uploads on") : print(kDisabled, "uploads are off in this build");
    }
    if (!keygen.empty()) {
        Key secret{};
        if (!random_bytes(secret.data(), secret.size())) return print(kWrite, "the system's random source failed");
        const std::filesystem::path file = keygen / "nebula-diag-private.key";
        const std::string text = "nebula-diag-private-key-v1\n" + hex(secret.data(), secret.size()) + "\n";
        std::error_code error;
        std::filesystem::create_directories(keygen, error);
        if (std::filesystem::exists(file, error)) return print(kWrite, "a private key is already there: not replaced");
#if defined(_WIN32)
        if (!write_file(file, text.data(), text.size())) return print(kWrite, "the private key cannot be written");
#else
        const std::string native = file.native();
        const int fd = ::open(native.c_str(), O_WRONLY | O_CREAT | O_EXCL, 0600);
        const bool written = fd >= 0 && ::write(fd, text.data(), text.size()) == static_cast<ssize_t>(text.size());
        if (fd >= 0) ::close(fd);
        if (!written) return print(kWrite, "the private key cannot be written");
#endif
        const Key public_half = x25519_public(secret);
        const auto id = key_id(public_half);
        std::printf("public key: %s\nkey id: %s\n", hex(public_half.data(), 32u).c_str(), hex(id.data(), 8u).c_str());
        std::printf("runtime/include/galaxy/diag_endpoint.h: kPublicKey[] = \"%s\";\n", hex(public_half.data(), 32u).c_str());
        const std::u8string shown = file.u8string();
        std::printf("private key: %s\nKeep the private key offline and backed up; never commit or share it.\n",
                    reinterpret_cast<const char*>(shown.c_str()));
        std::fflush(stdout);
        return kOk;
    }
    if (!opened.empty()) {
        std::string key_text;
        Key secret{};
        if (key_file.empty() || !read_file(key_file, 4096u, key_text) ||
            key_text.rfind("nebula-diag-private-key-v1\n", 0) != 0 || !key_from_hex(key_text.substr(27u, 64u), secret)) {
            return print(kUsage, "--open-report needs --key <private key file from --report-keygen>");
        }
        std::string envelope;
        if (!read_file(opened, kEnvelopeMax, envelope)) return print(kNotOpened, "not a Nebula report (unreadable or too large)");
        std::string plain;
        const OpenResult result =
            open_report(reinterpret_cast<const std::uint8_t*>(envelope.data()), envelope.size(), secret, plain);
        if (result != OpenResult::Ok) return print(kNotOpened, open_result_text(result));
        if (std::string error = report_error(plain); !error.empty()) {
            return print(kInvalid, "opened, but its report does not match the schema (" + error + ")");
        }
        std::fwrite(plain.data(), 1u, plain.size(), stdout);
        std::fputc('\n', stdout);
        std::fflush(stdout);
        return kOk;
    }
    if (report.empty()) return print(kUsage, "--send-report <benchmark.json> names the benchmark result");
    if (preview) {
        std::string text;
        if (SendResult built = report_preview(report, with_log, text); built.exit != kOk) return print(built.exit, built.line);
        std::fwrite(text.data(), 1u, text.size(), stdout);
        std::fputc('\n', stdout);
        std::fflush(stdout);
        return kOk;
    }
    if (!sealed_out.empty()) {
        Key key{};
        if (!(public_key.empty() ? build_key(key) : key_from_hex(public_key, key))) {
            return print(kDisabled, "no public key: this build has none and --public-key is not given");
        }
        Bytes envelope;
        if (SendResult sealed = seal_file(report, with_log, key, envelope); sealed.exit != kOk) {
            return print(sealed.exit, sealed.line);
        }
        if (!write_file(sealed_out, envelope.data(), envelope.size())) return print(kWrite, "the sealed report cannot be written");
        return print(kOk, "sealed: " + std::to_string(envelope.size()) + " bytes");
    }
    if (!public_key.empty()) return print(kUsage, "--public-key is for --save-sealed only");
    const SendResult result = send_report_file(report, with_log);
    return print(result.exit, result.line);
}

}  // namespace galaxy::diag
