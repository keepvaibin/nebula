// SPDX-License-Identifier: GPL-3.0-only
// svc_test_scene_frame's body in a test build: tours, events, teleports, walks.
    struct Stop {
        std::string stage;
        int32_t scenario;
    };
    struct Teleport {
        float x, y, z;
        uint64_t frame;
    };
    static std::vector<Stop> stops;
    static std::vector<Teleport> teleports;
    struct Event {
        uint32_t kind;
        uint64_t frame;
        int32_t value;
        std::string name;  // NEBULA_TEST_KILL_ACTOR's model
    };
    static std::vector<Event> events;
    constexpr uint32_t kAudioCounters = 0xFFFFFFFFu;  // audio@frame: the host's, no game action
    static uint64_t tour_frames = 1200;
    static bool touring = false;
    static bool loaded = false;
    static size_t next = 0;
    static uint64_t scene_frames = 0, since_stop = 0, stop_frame = 0;
    if (!loaded) {
        loaded = true;
        if (const char* tour = nebula_switch::test_galaxy_tour()) {
            if (FILE* file = std::fopen(tour, "r")) {
                char stage[64];
                int scenario = 0;
                while (std::fscanf(file, "%63s %d", stage, &scenario) == 2) {
                    stops.push_back({stage, scenario});
                }
                std::fclose(file);
            }
            touring = true;
            if (const char* frames = nebula_switch::test_galaxy_tour_frames()) {
                tour_frames = std::max<uint64_t>(std::strtoull(frames, nullptr, 10), 60u);
            }
            char text[64];
            std::snprintf(text, sizeof(text), "stops %zu, %llu frames apart", stops.size(),
                          static_cast<unsigned long long>(tour_frames));
            log_line("tour:", text);
        }
        if (const char* list = nebula_switch::test_events()) {
            std::string entries = list;
            size_t start = 0;
            while (start < entries.size()) {
                const size_t end = std::min(entries.find('/', start), entries.size());
                const std::string entry = entries.substr(start, end - start);
                const size_t at = entry.find('@');
                if (at != std::string::npos) {
                    const std::string kind = entry.substr(0, at);
                    const uint64_t frame = std::strtoull(entry.c_str() + at + 1, nullptr, 10);
                    if (kind == "kill") {
                        events.push_back({NEBULA_TEST_KILL_PLAYER, frame, 0});
                    } else if (kind == "circleclose" || kind == "circleopen") {
                        events.push_back({NEBULA_TEST_CIRCLE_WIPE, frame, kind == "circleopen" ? 1 : 0});
                    } else if (kind == "star") {
                        events.push_back({NEBULA_TEST_GET_STAR, frame, -1});
                    } else if (kind == "grandstar") {
                        events.push_back({NEBULA_TEST_GET_GRAND_STAR, frame, -1});
                    } else if (kind == "mario" || kind == "luigi") {
                        events.push_back({NEBULA_TEST_SET_PLAYER, frame, kind == "luigi" ? 1 : 0});
                    } else if (kind == "shake") {
                        events.push_back({NEBULA_TEST_SHAKE_CAMERA, frame, 0});
                    } else if (kind == "actors") {
                        events.push_back({NEBULA_TEST_LIST_ACTORS, frame, 0});
                    } else if (kind == "domelaunch") {
                        events.push_back({NEBULA_TEST_DOME_LAUNCH, frame, 0});
                    } else if (kind == "koopahit") {
                        events.push_back({NEBULA_TEST_KOOPA_HIT, frame, 0});
                    } else if (kind.rfind("materials:", 0) == 0) {
                        events.push_back({NEBULA_TEST_LIST_MATERIALS, frame, 0, kind.substr(10)});
                    } else if (kind.rfind("appear:", 0) == 0) {
                        events.push_back({NEBULA_TEST_APPEAR_ACTOR, frame, 0, kind.substr(7)});
                    } else if (kind.rfind("defeat:", 0) == 0) {
                        events.push_back({NEBULA_TEST_KILL_ACTOR, frame, 0, kind.substr(7)});
                    } else if (kind == "wait") {
                        events.push_back({0, frame, 0});
                    } else if (kind == "audio") {
                        events.push_back({kAudioCounters, frame, 0});
                    } else if (kind.rfind("movie", 0) == 0) {
                        events.push_back({NEBULA_TEST_START_MOVIE, frame, std::atoi(kind.c_str() + 5)});
                    }
                }
                start = end + 1;
            }
        }
        if (const char* list = nebula_switch::test_teleport()) {
            std::string entries = list;
            size_t start = 0;
            while (start < entries.size()) {
                const size_t end = std::min(entries.find('/', start), entries.size());
                Teleport t{};
                unsigned long long frame = 0;
                if (std::sscanf(entries.substr(start, end - start).c_str(), "%f,%f,%f,%llu", &t.x, &t.y, &t.z,
                                &frame) == 4) {
                    t.frame = frame;
                    teleports.push_back(t);
                }
                start = end + 1;
            }
        }
    }
    ++scene_frames;
    gSceneFrames.store(scene_frames, std::memory_order_relaxed);
    gStopFrames.store(touring && stop_frame == 0 ? kNoStopFrames : scene_frames - stop_frame, std::memory_order_relaxed);
#if defined(NEBULA_HOST_RENDERER)
    // The settings' galaxy select (ADVANCED page): a travel the player asked for, through the game's own stage change,
    // and the Grand Stars with the Observatory they power.
    {
        std::size_t stage = 0u;
        std::uint32_t star = 0u;
        if (galaxy::render::take_galaxy_travel(stage, star)) {
            action->kind = NEBULA_TEST_CHANGE_STAGE;
            action->scenario = static_cast<int32_t>(star);
            std::snprintf(action->stage, sizeof(action->stage), "%s", galaxy::render::kGalaxyStages[stage].id);
            return 1;
        }
        if (galaxy::render::take_grand_stars()) {
            action->kind = NEBULA_TEST_SET_GRAND_STARS;
            action->scenario = 6;
            std::snprintf(action->stage, sizeof(action->stage), "%s", galaxy::render::kObservatoryId);
            return 1;
        }
    }
#endif
    static std::vector<std::string> event_flags = [] {
        std::vector<std::string> names;
        if (const char* value = nebula_switch::test_event_flags()) {
            std::string list = value;
            for (std::size_t start = 0; start < list.size();) {
                const std::size_t comma = std::min(list.find(',', start), list.size());
                if (comma > start) names.push_back(list.substr(start, comma - start));
                start = comma + 1;
            }
        }
        return names;
    }();
    if (!event_flags.empty() && scene_frames >= 2u) {
        action->kind = NEBULA_TEST_SET_EVENT_FLAG;
        std::snprintf(action->stage, sizeof(action->stage), "%s", event_flags.front().c_str());
        log_line("test: event flag", action->stage);
        event_flags.erase(event_flags.begin());
        return 1;
    }
    static const int32_t grand_stars = [] {
        const char* value = nebula_switch::test_grand_stars();
        return value != nullptr ? std::atoi(value) : 0;
    }();
    static bool grand_stars_set = false;
    if (grand_stars > 0 && !grand_stars_set && scene_frames >= 2u) {
        grand_stars_set = true;
        action->kind = NEBULA_TEST_SET_GRAND_STARS;
        action->scenario = grand_stars;
        action->stage[0] = '\0';  // the stage played starts again with them
        log_line("test:", "grand stars set");
        return 1;
    }
    // The last stop stays for its frames, or until 20 s after the last test event, then the run ends.
    static const uint64_t last_frame = [] {
        uint64_t last = 0;
        for (const Event& event : events) {
            last = std::max(last, event.frame + 1200u);
        }
        for (const Teleport& t : teleports) {
            last = std::max(last, t.frame + 1200u);
        }
        return last;
    }();
    if (touring && next == stops.size() && !gTourDone.load(std::memory_order_relaxed) &&
        ++since_stop >= std::max(tour_frames, last_frame)) {
        gTourDone.store(true, std::memory_order_relaxed);  // the run ends once the last stop has had its frames
#if defined(_WIN32) && defined(NEBULA_HOST_RENDERER) && !defined(NEBULA_HOST_SDL)
        SetEvent(gHostWake);
#endif
        log_line("tour:", "done");
    }
    if (touring && next < stops.size() && ++since_stop >= tour_frames) {
        since_stop = 0;
        stop_frame = scene_frames;
        gStopFrames.store(0, std::memory_order_relaxed);
        const Stop& stop = stops[next++];
        action->kind = NEBULA_TEST_CHANGE_STAGE;
        action->scenario = stop.scenario;
        std::snprintf(action->stage, sizeof(action->stage), "%s", stop.stage.c_str());
        char text[128];
        std::snprintf(text, sizeof(text), "%zu/%zu %s %d", next, stops.size(), stop.stage.c_str(), stop.scenario);
        log_line("tour:", text);
        return 1;
    }
    if (touring && stop_frame == 0) {
        return 0;
    }
    const uint64_t frame = scene_frames - stop_frame;
    for (const Event& event : events) {
        if (frame == event.frame && event.kind == kAudioCounters) {
#if defined(NEBULA_HOST_RENDERER)
            if (gAudio != nullptr) {
                log_line("host:", ("jaudio " + gAudio->stats_line()).c_str());
            }
#endif
            continue;
        }
        if (frame == event.frame && event.kind != 0) {
            action->kind = event.kind;
            action->scenario = event.value >= 0 ? event.value : (next > 0 ? stops[next - 1].scenario : 1);
            std::snprintf(action->stage, sizeof(action->stage), "%s", event.name.c_str());
            char text[64];
            std::snprintf(text, sizeof(text), "event %u at frame %llu", event.kind, static_cast<unsigned long long>(frame));
            log_line("test:", text);
            return 1;
        }
    }
    // GALAXY_TEST_WALK=water|actor[,every[,seed[,forms]]]: from 300 frames into each stop, every `every` frames (default
    // 300), the player placed in the next body of water or at the next live actor, held 30 frames; with forms 1, each
    // place in the next power-up form.
    static const struct {
        int32_t kind;
        uint32_t every;
        uint32_t seed;
        uint32_t forms;
    } walk = [] {
        const char* value = nebula_switch::test_walk();
        unsigned every = 300u, seed = 0u, forms = 0u;
        if (value == nullptr) {
            return decltype(walk){0, 0u, 0u, 0u};
        }
        if (const char* comma = std::strchr(value, ',')) {
            (void)std::sscanf(comma + 1, "%u,%u,%u", &every, &seed, &forms);
        }
        return decltype(walk){value[0] == 'w' ? NEBULA_TEST_PLACE_PLAYER_WATER : NEBULA_TEST_PLACE_PLAYER_ACTOR,
                              every != 0u ? every : 300u, seed, forms};
    }();
    if (walk.kind != 0 && frame >= 300u && (frame - 300u) % walk.every < 30u) {
        action->kind = walk.kind;
        action->scenario = static_cast<int32_t>((frame - 300u) / walk.every + walk.seed);
        action->position[0] = static_cast<float>(walk.forms);
        return 1;
    }
    for (const Teleport& t : teleports) {
        if (frame >= t.frame && frame < t.frame + 30u) {
            action->kind = NEBULA_TEST_PLACE_PLAYER;
            action->position[0] = t.x;
            action->position[1] = t.y;
            action->position[2] = t.z;
            return 1;
        }
    }
    return 0;
