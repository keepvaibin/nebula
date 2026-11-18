// SPDX-License-Identifier: GPL-3.0-only
// A replacement movie on Linux and other systems (movie_source.h): GStreamer 1.x decodes it with the system's codecs
// (VA-API and the like where the system has them), opened at run time as libcurl is, so a system without it still
// runs the game (the movie is named in the log and the disc's plays). filesrc ! decodebin ! videoconvert ! appsink,
// pictures as NV12 or I420 in GStreamer's default layout (the sink takes no video meta, so upstream writes that layout).
// Under the sanitizers the unit test leaves this source out: the system's libraries are not instrumented.
//
// GStreamer's ABI is fixed within 1.x: the few structures read here (a buffer's times, a map, an error) are declared
// as 1.x lays them out.

#include "movie_source.h"

#include <dlfcn.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string_view>

namespace galaxy::render {
namespace {

// GStreamer's and GLib's types as 1.x lays them out (gst/gstminiobject.h, gstbuffer.h, gstmemory.h;
// glib/gerror.h).
struct MiniObject {
    std::size_t type;
    int refcount;
    int lockstate;
    unsigned flags;
    void* copy;
    void* dispose;
    void* free;
    unsigned priv_uint;
    void* priv_pointer;
};
struct Buffer {
    MiniObject mini_object;
    void* pool;
    std::uint64_t pts;  // GstClockTime, ns; GST_CLOCK_TIME_NONE when not given
    std::uint64_t dts;
    std::uint64_t duration;
    std::uint64_t offset;
    std::uint64_t offset_end;
};
struct MapInfo {
    void* memory;
    int flags;
    std::uint8_t* data;
    std::size_t size;
    std::size_t maxsize;
    void* user_data[4];
    void* reserved[4];
};
struct Error {
    std::uint32_t domain;
    int code;
    char* message;
};

constexpr int kStateNull = 1;
constexpr int kStatePlaying = 4;
constexpr int kChangeFailure = 0;  // GST_STATE_CHANGE_FAILURE
constexpr int kMapRead = 1;
constexpr int kFormatTime = 3;
constexpr int kSeekFlush = 1;
constexpr int kSeekKeyUnit = 4;
constexpr int kSeekSnapBefore = 32;
constexpr unsigned kMessageError = 2u;
constexpr std::uint64_t kNoTime = ~std::uint64_t{0};
constexpr std::uint64_t kPrerollWaitNs = 5'000'000'000;  // open waits this long for the first picture's decoder
constexpr std::uint64_t kPullWaitNs = 20'000'000;  // a pull waits this long before the bus is read
// A pipeline that gives nothing for this many pulls in a row (two seconds) has stopped.
constexpr int kIdleLimit = 100;

struct Gst {
    bool loaded = false;
    std::string missing;
    int (*init_check)(int*, char***, Error**) = nullptr;
    void* (*parse_launch)(const char*, Error**) = nullptr;
    void* (*bin_get_by_name)(void*, const char*) = nullptr;
    int (*element_set_state)(void*, int) = nullptr;
    int (*element_get_state)(void*, int*, int*, std::uint64_t) = nullptr;
    int (*element_seek_simple)(void*, int, int, std::int64_t) = nullptr;
    void* (*element_get_bus)(void*) = nullptr;
    void* (*bus_pop_filtered)(void*, unsigned) = nullptr;
    void (*message_parse_error)(void*, Error**, char**) = nullptr;
    void (*object_unref)(void*) = nullptr;
    void (*mini_object_unref)(void*) = nullptr;
    void* (*sample_get_buffer)(void*) = nullptr;
    void* (*sample_get_caps)(void*) = nullptr;
    void* (*caps_get_structure)(void*, unsigned) = nullptr;
    int (*structure_get_int)(void*, const char*, int*) = nullptr;
    const char* (*structure_get_string)(void*, const char*) = nullptr;
    int (*structure_get_fraction)(void*, const char*, int*, int*) = nullptr;
    int (*buffer_map)(void*, MapInfo*, int) = nullptr;
    void (*buffer_unmap)(void*, MapInfo*) = nullptr;
    void* (*app_sink_try_pull_sample)(void*, std::uint64_t) = nullptr;
    int (*app_sink_is_eos)(void*) = nullptr;
    void (*object_set)(void*, const char*, ...) = nullptr;
    void (*error_free)(Error*) = nullptr;
    void (*free)(void*) = nullptr;
};

template <typename F>
bool bind(void* library, const char* name, F& out, std::string& missing) {
    out = reinterpret_cast<F>(dlsym(library, name));
    if (out == nullptr && missing.empty()) missing = name;
    return out != nullptr;
}

// The libraries, opened and initialised once (any thread).
const Gst& gst() {
    static Gst g;
    static std::once_flag once;
    std::call_once(once, [] {
        void* const core = dlopen("libgstreamer-1.0.so.0", RTLD_NOW | RTLD_LOCAL);
        void* const app = dlopen("libgstapp-1.0.so.0", RTLD_NOW | RTLD_LOCAL);
        void* const gobject = dlopen("libgobject-2.0.so.0", RTLD_NOW | RTLD_LOCAL);
        void* const glib = dlopen("libglib-2.0.so.0", RTLD_NOW | RTLD_LOCAL);
        if (core == nullptr || app == nullptr || gobject == nullptr || glib == nullptr) {
            g.missing = core == nullptr ? "libgstreamer-1.0.so.0" : app == nullptr ? "libgstapp-1.0.so.0"
                        : gobject == nullptr ? "libgobject-2.0.so.0" : "libglib-2.0.so.0";
            return;
        }
        std::string& m = g.missing;
        bind(core, "gst_init_check", g.init_check, m);
        bind(core, "gst_parse_launch", g.parse_launch, m);
        bind(core, "gst_bin_get_by_name", g.bin_get_by_name, m);
        bind(core, "gst_element_set_state", g.element_set_state, m);
        bind(core, "gst_element_get_state", g.element_get_state, m);
        bind(core, "gst_element_seek_simple", g.element_seek_simple, m);
        bind(core, "gst_element_get_bus", g.element_get_bus, m);
        bind(core, "gst_bus_pop_filtered", g.bus_pop_filtered, m);
        bind(core, "gst_message_parse_error", g.message_parse_error, m);
        bind(core, "gst_object_unref", g.object_unref, m);
        bind(core, "gst_mini_object_unref", g.mini_object_unref, m);
        bind(core, "gst_sample_get_buffer", g.sample_get_buffer, m);
        bind(core, "gst_sample_get_caps", g.sample_get_caps, m);
        bind(core, "gst_caps_get_structure", g.caps_get_structure, m);
        bind(core, "gst_structure_get_int", g.structure_get_int, m);
        bind(core, "gst_structure_get_string", g.structure_get_string, m);
        bind(core, "gst_structure_get_fraction", g.structure_get_fraction, m);
        bind(core, "gst_buffer_map", g.buffer_map, m);
        bind(core, "gst_buffer_unmap", g.buffer_unmap, m);
        bind(app, "gst_app_sink_try_pull_sample", g.app_sink_try_pull_sample, m);
        bind(app, "gst_app_sink_is_eos", g.app_sink_is_eos, m);
        bind(gobject, "g_object_set", g.object_set, m);
        bind(glib, "g_error_free", g.error_free, m);
        bind(glib, "g_free", g.free, m);
        if (!m.empty()) return;
        Error* error = nullptr;
        if (g.init_check(nullptr, nullptr, &error) == 0) {
            m = std::string("gst_init: ") + (error != nullptr && error->message != nullptr ? error->message : "failed");
            if (error != nullptr) g.error_free(error);
            return;
        }
        g.loaded = true;
    });
    return g;
}

// A caps' colorimetry ("bt709", "bt601", or "range:matrix:transfer:primaries") into the format; none given: HD
// BT.709, SD BT.601, limited range (as on Windows).
void colours_of(const char* colorimetry, MovieSourceFormat& format) {
    bool bt709 = format.height >= 720u;
    bool full = false;
    if (colorimetry != nullptr) {
        const std::string_view text(colorimetry);
        if (text == "bt709" || text == "bt2020" || text == "bt2100-pq" || text == "bt2100-hlg") bt709 = true;
        else if (text == "bt601" || text == "smpte240m") bt709 = false;
        else if (text == "sRGB" || text == "jpeg") full = true, bt709 = text == "sRGB";
        unsigned range = 0, matrix = 0;
        if (std::sscanf(colorimetry, "%u:%u", &range, &matrix) == 2) {
            full = range == 1u;  // GST_VIDEO_COLOR_RANGE_0_255
            bt709 = matrix == 3u ? true : matrix == 4u ? false : bt709;  // BT709, BT601
        }
    }
    format.bt709 = bt709;
    format.full_range = full;
}

std::size_t round_up(std::size_t value, std::size_t to) {
    return (value + to - 1u) / to * to;
}

class GstSource final : public MovieSource {
public:
    ~GstSource() override {
        release();
        const Gst& g = gst();
        if (pipeline_ != nullptr) {
            g.element_set_state(pipeline_, kStateNull);
            if (sink_ != nullptr) g.object_unref(sink_);
            if (bus_ != nullptr) g.object_unref(bus_);
            g.object_unref(pipeline_);
        }
    }

    bool open(const std::filesystem::path& file) {
        const Gst& g = gst();
        if (!g.loaded) {
            error_ = "GStreamer 1.x is not on this system (" + g.missing + ")";
            return false;
        }
        std::error_code exists;
        if (!std::filesystem::is_regular_file(file, exists)) {
            error_ = "cannot open";
            return false;
        }
        Error* error = nullptr;
        pipeline_ = g.parse_launch(
            "filesrc name=file ! decodebin ! videoconvert ! video/x-raw,format={NV12,I420} ! "
            "appsink name=pictures sync=false max-buffers=2 enable-last-sample=false",
            &error);
        if (pipeline_ == nullptr) {
            error_ = std::string("pipeline: ") + (error != nullptr && error->message != nullptr ? error->message : "?");
            if (error != nullptr) g.error_free(error);
            return false;
        }
        if (error != nullptr) g.error_free(error);  // a warning with a pipeline made
        void* const source = g.bin_get_by_name(pipeline_, "file");
        sink_ = g.bin_get_by_name(pipeline_, "pictures");
        bus_ = g.element_get_bus(pipeline_);
        if (source == nullptr || sink_ == nullptr || bus_ == nullptr) {
            if (source != nullptr) g.object_unref(source);
            error_ = "pipeline: no file source or sink";
            return false;
        }
        g.object_set(source, "location", file.c_str(), nullptr);
        g.object_unref(source);
        // Playing once the first picture is decoded (the preroll): a container or codec nothing on the system takes
        // fails here, as the other systems' decoders refuse a file when it opens.
        if (g.element_set_state(pipeline_, kStatePlaying) == kChangeFailure ||
            g.element_get_state(pipeline_, nullptr, nullptr, kPrerollWaitNs) == kChangeFailure) {
            if (!bus_error()) error_ = "cannot open";
            return false;
        }
        return true;
    }

    const MovieSourceFormat& format() const override { return format_; }

    Read read(MovieSourcePicture& out) override {
        release();
        const Gst& g = gst();
        for (int idle = 0;; ++idle) {
            sample_ = g.app_sink_try_pull_sample(sink_, kPullWaitNs);
            if (sample_ != nullptr) break;
            if (g.app_sink_is_eos(sink_) != 0) return Read::End;
            if (bus_error()) return Read::Failed;
            if (idle >= kIdleLimit) return failed("no picture came");
        }
        void* const caps = g.sample_get_caps(sample_);
        void* const structure = caps != nullptr ? g.caps_get_structure(caps, 0u) : nullptr;
        int width = 0, height = 0, numerator = 0, denominator = 0;
        if (structure == nullptr || g.structure_get_int(structure, "width", &width) == 0 ||
            g.structure_get_int(structure, "height", &height) == 0 || width < 2 || height < 2) {
            return failed("a picture without a size");
        }
        const char* const name = g.structure_get_string(structure, "format");
        const bool nv12 = name != nullptr && std::strcmp(name, "NV12") == 0;
        format_.width = static_cast<std::uint32_t>(width);
        format_.height = static_cast<std::uint32_t>(height);
        format_.pixels = MoviePixels::Yuv420;
        colours_of(g.structure_get_string(structure, "colorimetry"), format_);
        if (g.structure_get_fraction(structure, "framerate", &numerator, &denominator) != 0 && numerator > 0 &&
            denominator > 0) {
            format_.rate = static_cast<double>(numerator) / denominator;
        }
        buffer_ = g.sample_get_buffer(sample_);
        if (buffer_ == nullptr || g.buffer_map(buffer_, &map_, kMapRead) == 0) {
            buffer_ = nullptr;
            return failed("a picture that cannot be read");
        }
        mapped_ = true;
        // GstVideoInfo's default layout: rows rounded up to 4 bytes, planes after whole (even) row counts.
        const std::size_t w = static_cast<std::size_t>(width);
        const std::size_t rows = round_up(static_cast<std::size_t>(height), 2u);
        const std::size_t y_pitch = round_up(w, 4u);
        const std::size_t c_pitch = nv12 ? y_pitch : round_up(round_up(w, 2u) / 2u, 4u);
        const std::size_t luma = y_pitch * rows;
        const std::size_t chroma = c_pitch * (rows / 2u);
        if (map_.size < luma + chroma * (nv12 ? 1u : 2u)) return failed("a picture smaller than its format");
        out.time = static_cast<const Buffer*>(buffer_)->pts != kNoTime
                       ? static_cast<std::int64_t>(static_cast<const Buffer*>(buffer_)->pts / 100u)
                       : 0;
        const std::uint8_t* const cb = map_.data + luma;
        out.planes = {map_.data, cb, nv12 ? cb + 1 : cb + chroma};
        out.pitches = {static_cast<std::int32_t>(y_pitch), static_cast<std::int32_t>(c_pitch),
                       static_cast<std::int32_t>(c_pitch)};
        out.chroma_step = nv12 ? 2u : 1u;
        return Read::Picture;
    }

    bool seek(std::int64_t time) override {
        release();
        return gst().element_seek_simple(pipeline_, kFormatTime, kSeekFlush | kSeekKeyUnit | kSeekSnapBefore,
                                           time * 100) != 0;
    }

private:
    // An error the pipeline posted, into error_; whether there was one.
    bool bus_error() {
        const Gst& g = gst();
        void* const message = g.bus_pop_filtered(bus_, kMessageError);
        if (message == nullptr) return false;
        Error* error = nullptr;
        char* debug = nullptr;
        g.message_parse_error(message, &error, &debug);
        error_ = error != nullptr && error->message != nullptr ? error->message : "decoding stopped";
        if (error != nullptr) g.error_free(error);
        if (debug != nullptr) g.free(debug);
        g.mini_object_unref(message);
        return true;
    }

    Read failed(const char* what) {
        error_ = what;
        return Read::Failed;
    }

    void release() {
        const Gst& g = gst();
        if (mapped_) {
            g.buffer_unmap(buffer_, &map_);
            mapped_ = false;
        }
        buffer_ = nullptr;  // the sample's
        if (sample_ != nullptr) {
            g.mini_object_unref(sample_);
            sample_ = nullptr;
        }
    }

    void* pipeline_ = nullptr;
    void* sink_ = nullptr;
    void* bus_ = nullptr;
    void* sample_ = nullptr;  // the last read's
    void* buffer_ = nullptr;
    MapInfo map_{};
    bool mapped_ = false;
    MovieSourceFormat format_;
};

}  // namespace

std::unique_ptr<MovieSource> open_movie_source(const std::filesystem::path& file, std::string& error) {
    auto source = std::make_unique<GstSource>();
    if (!source->open(file)) {
        error = source->error();
        return nullptr;
    }
    return source;
}

void prepare_movie_sources() {
    (void)gst();
}

}  // namespace galaxy::render
