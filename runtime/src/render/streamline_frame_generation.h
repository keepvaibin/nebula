// SPDX-License-Identifier: GPL-3.0-only
#pragma once

// DLSS Frame Generation, Reflex and the latency markers: the same on both renderers (streamline_bridge.cpp, streamline_vk.cpp).

#include "galaxy/render/streamline_bridge.h"

#include <sl.h>
#include <sl_consts.h>
#include <sl_dlss_g.h>
#include <sl_pcl.h>
#include <sl_reflex.h>

#include <algorithm>
#include <cstdint>
#include <iostream>
#include <string>

namespace galaxy::render::streamline_common {

inline const char* result_name(sl::Result result) {
    switch (result) {
    case sl::Result::eOk: return "ok";
    case sl::Result::eErrorOSOutOfDate: return "Windows is too old";
    case sl::Result::eErrorDriverOutOfDate: return "the NVIDIA driver is too old";
    case sl::Result::eErrorNoSupportedAdapterFound: return "no supported NVIDIA adapter";
    case sl::Result::eErrorAdapterNotSupported: return "the adapter is not supported";
    case sl::Result::eErrorFeatureMissing: return "the DLSS plugin is missing";
    case sl::Result::eErrorFeatureNotSupported: return "DLSS is not supported";
    default: return "error";
    }
}

class FrameGeneration {
public:
    // The interposer's functions: set before attach.
    PFun_slIsFeatureSupported* is_feature_supported{};
    PFun_slGetFeatureFunction* get_feature_function{};
    PFun_slGetNewFrameToken* get_new_frame_token{};
    PFun_slSetTagForFrame* set_tag_for_frame{};

    // Frame generation on the attached adapter: its functions, Reflex low latency (it requires it), the most frames it generates.
    void attach(const sl::AdapterInfo& adapter) {
        sl::Result result = is_feature_supported(sl::kFeatureDLSS_G, adapter);
        if (result != sl::Result::eOk) {
            std::cerr << "[streamline] DLSS Frame Generation unavailable: " << result_name(result) << '\n';
            return;
        }
        void* set_options = nullptr;
        void* get_state = nullptr;
        void* reflex_options = nullptr;
        void* reflex_sleep = nullptr;
        void* pcl_marker = nullptr;
        if (get_feature_function(sl::kFeatureDLSS_G, "slDLSSGSetOptions", set_options) != sl::Result::eOk ||
            get_feature_function(sl::kFeatureDLSS_G, "slDLSSGGetState", get_state) != sl::Result::eOk ||
            get_feature_function(sl::kFeatureReflex, "slReflexSetOptions", reflex_options) != sl::Result::eOk ||
            get_feature_function(sl::kFeatureReflex, "slReflexSleep", reflex_sleep) != sl::Result::eOk ||
            get_feature_function(sl::kFeaturePCL, "slPCLSetMarker", pcl_marker) != sl::Result::eOk ||
            set_options == nullptr || get_state == nullptr || reflex_options == nullptr || reflex_sleep == nullptr ||
            pcl_marker == nullptr) {
            std::cerr << "[streamline] DLSS Frame Generation plugin functions missing\n";
            return;
        }
        set_options_ = reinterpret_cast<PFun_slDLSSGSetOptions*>(set_options);
        get_state_ = reinterpret_cast<PFun_slDLSSGGetState*>(get_state);
        reflex_sleep_ = reinterpret_cast<PFun_slReflexSleep*>(reflex_sleep);
        pcl_set_marker_ = reinterpret_cast<PFun_slPCLSetMarker*>(pcl_marker);
        sl::ReflexOptions reflex{};
        reflex.mode = sl::ReflexMode::eLowLatency;
        result = reinterpret_cast<PFun_slReflexSetOptions*>(reflex_options)(reflex);
        if (result != sl::Result::eOk) {
            std::cerr << "[streamline] Reflex: " << result_name(result) << '\n';
            return;
        }
        sl::DLSSGState state{};
        if (get_state_(sl::ViewportHandle{0u}, state, nullptr) == sl::Result::eOk) {
            max_ = std::max(state.numFramesToGenerateMax, 1u);
            dynamic_ = state.bIsDynamicMFGSupported == sl::Boolean::eTrue;
            vsync_ = state.bIsVsyncSupportAvailable == sl::Boolean::eTrue;
        } else {
            max_ = 1u;
        }
        markers_ = true;
        supported_ = true;
        std::cerr << "[streamline] DLSS Frame Generation ready (up to " << max_ + 1u << "x" << (dynamic_ ? ", dynamic" : "")
                  << (vsync_ ? ", vsync" : "") << ")\n";
    }

    void shutdown() noexcept {
        supported_ = false;
        markers_ = false;
    }

    [[nodiscard]] bool supported() const noexcept { return supported_; }
    [[nodiscard]] unsigned max() const noexcept { return supported_ ? max_ : 0u; }
    [[nodiscard]] bool dynamic() const noexcept { return supported_ && dynamic_; }
    [[nodiscard]] bool vsync() const noexcept { return vsync_; }
    [[nodiscard]] unsigned presented() const noexcept { return presented_; }
    [[nodiscard]] std::uint32_t frame_index() const noexcept { return frame_index_; }
    // A second evaluation before the present (an interpolated generated frame, a second scene): Streamline rejects different
    // common constants under one token.
    void next_frame_index() noexcept { ++frame_index_; }

    // The presented frame's token, null on failure.
    [[nodiscard]] sl::FrameToken* token() const {
        sl::FrameToken* token = nullptr;
        const std::uint32_t index = frame_index_;
        return get_new_frame_token != nullptr && get_new_frame_token(token, &index) == sl::Result::eOk ? token : nullptr;
    }

    void marker(streamline::Marker m) {
        if (markers_ && pcl_set_marker_ != nullptr) {
            if (sl::FrameToken* const frame = token(); frame != nullptr) {
                (void)pcl_set_marker_(static_cast<sl::PCLMarker>(static_cast<unsigned>(m)), *frame);
            }
        }
        if (m == streamline::Marker::kPresentEnd) {
            ++frame_index_;
            tagged_ = false;
        }
    }

    void sleep() {
        if (supported_ && reflex_sleep_ != nullptr) {
            if (sl::FrameToken* const frame = token(); frame != nullptr) {
                (void)reflex_sleep_(*frame);
            }
        }
    }

    // The tags of this frame's generation inputs went in: frames are generated from it.
    void note_tagged() noexcept { tagged_ = true; }
    [[nodiscard]] bool tagged() const noexcept { return tagged_; }

    void set(unsigned generated, bool dynamic, float target_fps) {
        if (!supported_) {
            return;
        }
        // Only frames whose scene was tagged are generated from.
        dynamic = dynamic && dynamic_ && generated != 0u;
        const unsigned wanted = tagged_ ? std::min(generated, max_) : 0u;
        const float target = dynamic && wanted != 0u ? target_fps : -1.0f;
        if (wanted != applied_ || target != applied_target_) {
            applied_target_ = target;
            sl::DLSSGOptions options{};
            options.mode = wanted == 0u ? sl::DLSSGMode::eOff : dynamic ? sl::DLSSGMode::eDynamic : sl::DLSSGMode::eOn;
            options.numFramesToGenerate = std::max(wanted, 1u);
            options.dynamicTargetFrameRate = dynamic ? target_fps : 0.0f;
            // Off between scenes (menus, loading) keeps its resources: no stutter coming back.
            options.flags = sl::DLSSGFlags::eRetainResourcesWhenOff;
            const sl::Result result = set_options_(sl::ViewportHandle{0u}, options);
            if (result != sl::Result::eOk) {
                std::cerr << "[dlss-g] options failed: " << result_name(result) << '\n';
            } else if (applied_ == ~0u || (applied_ == 0u) != (wanted == 0u)) {
                std::cerr << "[dlss-g] " << (wanted != 0u ? "on, generated frames " : "off")
                          << (wanted != 0u ? std::to_string(wanted) : std::string()) << '\n';
            }
            applied_ = wanted;
        }
        sl::DLSSGState state{};
        if (applied_ != 0u && get_state_(sl::ViewportHandle{0u}, state, nullptr) == sl::Result::eOk) {
            presented_ = std::max(state.numFramesActuallyPresented, 1u);
            const unsigned status = static_cast<unsigned>(state.status);
            if (status != status_ || frame_index_ - reported_index_ >= 600u) {
                reported_index_ = frame_index_;
                std::cerr << "[dlss-g] frame " << frame_index_ << " status " << status << " presented "
                          << state.numFramesActuallyPresented << " max " << state.numFramesToGenerateMax << " min-size "
                          << state.minWidthOrHeight << '\n';
                status_ = status;
            }
        } else if (applied_ == 0u) {
            presented_ = 1u;
        }
    }

private:
    PFun_slDLSSGSetOptions* set_options_{};
    PFun_slDLSSGGetState* get_state_{};
    PFun_slReflexSleep* reflex_sleep_{};
    PFun_slPCLSetMarker* pcl_set_marker_{};
    bool supported_ = false;
    bool markers_ = false;
    bool dynamic_ = false;
    bool vsync_ = false;
    bool tagged_ = false;
    unsigned max_ = 0u;
    unsigned applied_ = ~0u;
    float applied_target_ = -1.0f;
    unsigned presented_ = 1u;
    unsigned status_ = 0u;
    std::uint32_t frame_index_ = 0u;
    std::uint32_t reported_index_ = 0u;
};

}  // namespace galaxy::render::streamline_common
