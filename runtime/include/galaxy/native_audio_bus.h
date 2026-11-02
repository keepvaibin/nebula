// The volume settings the game's voices follow (NebulaVoice.category).
#pragma once

namespace galaxy::audio {

// `master` multiplies each here; the host passes 1 because it applies the master gain once, at the final output, so that it also
// covers audio mixed outside these channels (the movie player).
struct NativeAudioBusGains {
    float master{1.0f};
    float music{1.0f};
    float voice{1.0f};
    float sfx{1.0f};
    float ambience{1.0f};
    float cutscene{1.0f};
};

}  // namespace galaxy::audio
