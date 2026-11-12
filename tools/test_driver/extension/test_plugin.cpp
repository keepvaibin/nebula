// SPDX-License-Identifier: GPL-3.0-only
// The test runs' game half: a mod plugin in the test library, which run.sh also installs into the run's mods. From the
// run's GALAXY_TEST_* environment it drives tours, test events, event flags, Grand Stars, teleports and walks through
// the mod API (nebula_mod_api.h), once a scene frame at the start of GameScene::draw. Each scene frame, the tour's
// stops and its end go to the library's extension instance (the one the host loaded with --extension), which keeps the
// script clock and names the pictures.
#include <nebula_mod_api.h>

#include <algorithm>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <psapi.h>
#define NEBULA_TEST_PLUGIN_EXPORT extern "C" __declspec(dllexport)
#else
#include <dlfcn.h>
#include <link.h>
#define NEBULA_TEST_PLUGIN_EXPORT extern "C" __attribute__((visibility("default")))
#endif

// test_extension.cpp's, in this instance of the library.
extern "C" void nebula_test_scene_frame(std::uint64_t scene_frames, std::uint64_t stop_frames, std::uint64_t picture_number);
extern "C" void nebula_test_done();

namespace {

const NebulaModApi* g_api = nullptr;
const NebulaPlugin* g_self = nullptr;

constexpr std::uint64_t kNone = ~std::uint64_t{0};  // no stop yet; no picture number

void say(const std::string& line) {
    g_api->log(g_self, line.c_str());
}

std::optional<std::string> env(const char* name) {
#if defined(_WIN32)
    std::string value(256u, '\0');
    DWORD length = GetEnvironmentVariableA(name, value.data(), static_cast<DWORD>(value.size()));
    if (length >= value.size()) {
        value.resize(length);
        length = GetEnvironmentVariableA(name, value.data(), static_cast<DWORD>(value.size()));
    }
    if (length == 0u || length >= value.size()) {
        return std::nullopt;
    }
    value.resize(length);
    return value;
#else
    const char* const value = std::getenv(name);
    if (value == nullptr || value[0] == '\0') {
        return std::nullopt;
    }
    return std::string(value);
#endif
}

std::vector<std::string> split(std::string_view text, char by) {
    std::vector<std::string> items;
    std::size_t start = 0u;
    while (start < text.size()) {
        const std::size_t end = std::min(text.find(by, start), text.size());
        if (end > start) {
            items.emplace_back(text.substr(start, end - start));
        }
        start = end + 1u;
    }
    return items;
}

std::uint64_t number(std::string_view text, std::uint64_t otherwise) {
    std::uint64_t value = 0u;
    if (text.empty()) {
        return otherwise;
    }
    for (const char c : text) {
        if (c < '0' || c > '9') {
            return otherwise;
        }
        value = value * 10u + static_cast<std::uint64_t>(c - '0');
    }
    return value;
}

// --- The extension's instance ----------------------------------------------------------------------------------------
// The mods load a copy of this library as code/<platform>/mod.<ending> (Windows runs a plugin from a copy of its
// folder), so the extension the host attached is another instance under another name: found among the loaded
// libraries, not this one, as the one exporting both of the extension's entry points.

using SceneFrameFn = void (*)(std::uint64_t, std::uint64_t, std::uint64_t);
using DoneFn = void (*)();
SceneFrameFn g_scene_frame = nebula_test_scene_frame;
DoneFn g_done = nebula_test_done;

#if defined(_WIN32)
void find_extension() {
    HMODULE mine = nullptr;
    if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           reinterpret_cast<LPCWSTR>(&find_extension), &mine) == 0) {
        return;
    }
    std::vector<HMODULE> modules(1024u);
    DWORD bytes = 0u;
    if (EnumProcessModules(GetCurrentProcess(), modules.data(), static_cast<DWORD>(modules.size() * sizeof(HMODULE)), &bytes) == 0) {
        return;
    }
    modules.resize(std::min<std::size_t>(modules.size(), bytes / sizeof(HMODULE)));
    for (const HMODULE module : modules) {
        if (module == mine) {
            continue;
        }
        const FARPROC scene_frame = GetProcAddress(module, "nebula_test_scene_frame");
        const FARPROC done = GetProcAddress(module, "nebula_test_done");
        if (scene_frame != nullptr && done != nullptr) {
            g_scene_frame = reinterpret_cast<SceneFrameFn>(reinterpret_cast<void*>(scene_frame));
            g_done = reinterpret_cast<DoneFn>(reinterpret_cast<void*>(done));
            return;
        }
    }
}
#else
struct Search {
    const void* mine;
    void* scene_frame;
    void* done;
};

void find_extension() {
    Dl_info info{};
    if (dladdr(reinterpret_cast<void*>(&find_extension), &info) == 0 || info.dli_fname == nullptr) {
        return;
    }
    Search search{info.dli_fbase, nullptr, nullptr};
    dl_iterate_phdr(
        [](dl_phdr_info* object, std::size_t, void* user) {
            Search& s = *static_cast<Search*>(user);
            if (object->dlpi_name == nullptr || object->dlpi_name[0] == '\0' ||
                reinterpret_cast<const void*>(object->dlpi_addr) == s.mine) {
                return 0;
            }
            // Already loaded (NOLOAD); the one kept stays open as long as the process, as the host's does.
            void* const library = dlopen(object->dlpi_name, RTLD_NOW | RTLD_NOLOAD);
            if (library == nullptr) {
                return 0;
            }
            void* const scene_frame = dlsym(library, "nebula_test_scene_frame");
            void* const done = dlsym(library, "nebula_test_done");
            if (scene_frame == nullptr || done == nullptr) {
                dlclose(library);
                return 0;
            }
            s.scene_frame = scene_frame;
            s.done = done;
            return 1;
        },
        &search);
    if (search.scene_frame != nullptr) {
        g_scene_frame = reinterpret_cast<SceneFrameFn>(search.scene_frame);
        g_done = reinterpret_cast<DoneFn>(search.done);
    }
}
#endif

// --- What the run asks ---------------------------------------------------------------------------------------------------

struct Stop {
    std::string stage;
    std::int32_t scenario;
};

struct Teleport {
    float xyz[3];
    std::uint64_t frame;
};

enum class Kind {
    Wait,
    KillPlayer,
    CircleWipe,
    Star,
    GrandStar,
    Character,
    Shake,
    Actors,
    Materials,
    Appear,
    Defeat,
};

struct Event {
    Kind kind;
    std::uint64_t frame;
    std::int32_t value;  // -1: the stop's star
    std::string name;    // its text (materials:, appear:, defeat:)
    std::string label;   // as written
};

struct Walk {
    bool on = false;
    bool water = false;
    std::uint64_t every = 300u;
    std::uint64_t seed = 0u;
    bool forms = false;
};

struct Run {
    std::vector<Stop> stops;
    std::vector<Teleport> teleports;
    std::vector<Event> events;
    std::vector<std::string> flags;
    std::int32_t grand_stars = 0;
    bool grand_stars_set = false;
    std::uint64_t tour_frames = 1200u;
    bool touring = false;
    bool done = false;
    std::size_t next = 0u;
    std::uint64_t scene_frames = 0u, since_stop = 0u, stop_frame = 0u;
    std::uint64_t last_frame = 0u;  // the last stop stays until 20 s after the last event or teleport
    Walk walk;
    // The walk's place: the index it is at, and the actor it goes to (its kind once a scene while kinds remain).
    std::int64_t walk_index = -1;
    NebulaActor* walk_target = nullptr;
    std::vector<std::string> walked;
    std::uint32_t swim_frames = 0u;
} g_run;

void read_tour() {
    Run& run = g_run;
    const std::optional<std::string> tour = env("GALAXY_TEST_GALAXY_TOUR");
    if (!tour) {
        return;
    }
    std::ifstream file(*tour);
    std::string stage;
    std::int32_t scenario = 0;
    while (file >> stage >> scenario) {
        run.stops.push_back({stage, scenario});
    }
    run.touring = true;
    if (const std::optional<std::string> frames = env("GALAXY_TEST_GALAXY_TOUR_FRAMES")) {
        run.tour_frames = std::max<std::uint64_t>(number(*frames, run.tour_frames), 60u);
    }
    say("tour: stops " + std::to_string(run.stops.size()) + ", " + std::to_string(run.tour_frames) + " frames apart");
}

void read_events() {
    Run& run = g_run;
    if (const std::optional<std::string> list = env("GALAXY_TEST_EVENTS")) {
        for (const std::string& entry : split(*list, '/')) {
            const std::size_t at = entry.find('@');
            if (at == std::string::npos) {
                continue;
            }
            const std::string kind = entry.substr(0u, at);
            const std::uint64_t frame = number(std::string_view(entry).substr(at + 1u), 0u);
            const auto add = [&](Kind k, std::int32_t value, std::string name = {}) {
                run.events.push_back({k, frame, value, std::move(name), kind});
            };
            if (kind == "kill") {
                add(Kind::KillPlayer, 0);
            } else if (kind == "circleclose" || kind == "circleopen") {
                add(Kind::CircleWipe, kind == "circleopen" ? 1 : 0);
            } else if (kind == "star") {
                add(Kind::Star, -1);
            } else if (kind == "grandstar") {
                add(Kind::GrandStar, -1);
            } else if (kind == "mario" || kind == "luigi") {
                add(Kind::Character, kind == "luigi" ? 1 : 0);
            } else if (kind == "shake") {
                add(Kind::Shake, 0);
            } else if (kind == "actors") {
                add(Kind::Actors, 0);
            } else if (kind.rfind("materials:", 0) == 0) {
                add(Kind::Materials, 0, kind.substr(10));
            } else if (kind.rfind("appear:", 0) == 0) {
                add(Kind::Appear, 0, kind.substr(7));
            } else if (kind.rfind("defeat:", 0) == 0) {
                add(Kind::Defeat, 0, kind.substr(7));
            } else if (kind == "wait") {
                add(Kind::Wait, 0);
            } else {
                say("test: no event " + kind);
            }
        }
    }
    if (const std::optional<std::string> list = env("GALAXY_TEST_TELEPORT")) {
        for (const std::string& entry : split(*list, '/')) {
            // x,y,z,frame
            const std::vector<std::string> parts = split(entry, ',');
            Teleport t{};
            bool good = parts.size() == 4u;
            for (std::size_t i = 0u; good && i < 3u; ++i) {
                const char* const end = parts[i].data() + parts[i].size();
                const std::from_chars_result read = std::from_chars(parts[i].data(), end, t.xyz[i]);
                good = read.ec == std::errc{} && read.ptr == end;
            }
            t.frame = good ? number(parts[3], kNone) : kNone;
            if (good && t.frame != kNone) {
                run.teleports.push_back(t);
            }
        }
    }
    if (const std::optional<std::string> list = env("GALAXY_TEST_EVENT_FLAGS")) {
        run.flags = split(*list, ',');
    }
    if (const std::optional<std::string> count = env("GALAXY_TEST_GRAND_STARS")) {
        run.grand_stars = static_cast<std::int32_t>(std::min<std::uint64_t>(number(*count, 0u), 7u));
    }
    // GALAXY_TEST_WALK=water|actor[,every[,seed[,forms]]]: from 300 frames into each stop, every `every` frames (300),
    // the player placed in the next body of water or at the next live actor, held 30 frames; with forms 1, each place in
    // the next power-up form.
    if (const std::optional<std::string> walk = env("GALAXY_TEST_WALK")) {
        const std::vector<std::string> parts = split(*walk, ',');
        run.walk.on = !parts.empty();
        run.walk.water = !parts.empty() && parts[0][0] == 'w';
        run.walk.every = parts.size() > 1u ? std::max<std::uint64_t>(number(parts[1], 300u), 1u) : 300u;
        run.walk.seed = parts.size() > 2u ? number(parts[2], 0u) : 0u;
        run.walk.forms = parts.size() > 3u && number(parts[3], 0u) != 0u;
    }
    for (const Event& event : run.events) {
        run.last_frame = std::max(run.last_frame, event.frame + 1200u);
    }
    for (const Teleport& t : run.teleports) {
        run.last_frame = std::max(run.last_frame, t.frame + 1200u);
    }
}

// --- What each frame does ------------------------------------------------------------------------------------------------

std::string where(NebulaActor* actor) {
    float xyz[3] = {};
    g_api->get_position(actor, xyz);
    char text[96];
    std::snprintf(text, sizeof(text), "%.0f %.0f %.0f", static_cast<double>(xyz[0]), static_cast<double>(xyz[1]), static_cast<double>(xyz[2]));
    return text;
}

void list_actors() {
    for (NebulaActor* actor = g_api->find_actor(nullptr, nullptr); actor != nullptr; actor = g_api->find_actor(nullptr, actor)) {
        if (g_api->is_dead(actor) != 0) {
            continue;
        }
        const char* const name = g_api->actor_name(actor);
        say(std::string("actor ") + (name != nullptr ? name : "?") + " " + where(actor));
    }
}

void list_materials(const std::string& model) {
    if (!NEBULA_MOD_API_HAS(g_api, list_materials)) {
        return;
    }
    struct Out {
        std::string model;
    } out{model};
    const std::int32_t count = g_api->list_materials(
        model.c_str(),
        [](const NebulaMaterialInfo* material, void* user) {
            const std::string& name = static_cast<Out*>(user)->model;
            say("test: material " + name + " " + std::to_string(material->index) + " " + (material->name != nullptr ? material->name : "?"));
            for (std::int32_t c = 0; c < material->coord_count; ++c) {
                const NebulaMaterialCoord& coord = material->coords[c];
                char text[160];
                std::snprintf(text, sizeof(text), "test: coordinate %d %d type %u source %u matrix %u info %u projection %u used %u",
                              material->index, c, static_cast<unsigned>(coord.type), static_cast<unsigned>(coord.source),
                              static_cast<unsigned>(coord.matrix), static_cast<unsigned>(coord.matrix_kind),
                              static_cast<unsigned>(coord.projection), static_cast<unsigned>(coord.used));
                say(text);
            }
        },
        &out);
    if (count == 0) {
        say("test: no materials of " + model);
    }
}

// Each actor of that level name, dead ones appearing (`appear`) or live ones killed as their defeat ends.
void appear_or_defeat(const std::string& name, bool appear) {
    for (NebulaActor* actor = g_api->find_actor(name.c_str(), nullptr); actor != nullptr; actor = g_api->find_actor(name.c_str(), actor)) {
        const bool dead = g_api->is_dead(actor) != 0;
        if (dead != appear) {
            continue;
        }
        say(std::string(appear ? "test: appear " : "test: kill ") + name + " " + where(actor));
        if (appear) {
            g_api->appear(actor);
        } else {
            g_api->kill(actor);
        }
    }
}

void run_event(const Event& event, std::int32_t stop_star) {
    const std::int32_t value = event.value >= 0 ? event.value : stop_star;
    switch (event.kind) {
    case Kind::Wait:
        return;
    case Kind::KillPlayer:
        g_api->kill_player();
        break;
    case Kind::CircleWipe:
        if (NEBULA_MOD_API_HAS(g_api, circle_wipe)) {
            (void)g_api->circle_wipe(value, 60);
            say(std::string("test: circle ") + (value != 0 ? "open" : "close") + ", 60 frames");
        }
        break;
    case Kind::Star:
    case Kind::GrandStar:
        if (NEBULA_MOD_API_HAS(g_api, give_star)) {
            (void)g_api->give_star(nullptr, value, event.kind == Kind::GrandStar ? 1 : 0);
        }
        break;
    case Kind::Character:
        if (NEBULA_MOD_API_HAS(g_api, set_player_character)) {
            (void)g_api->set_player_character(value);
        }
        break;
    case Kind::Shake:
        g_api->shake_camera();
        break;
    case Kind::Actors:
        list_actors();
        break;
    case Kind::Materials:
        list_materials(event.name);
        break;
    case Kind::Appear:
        appear_or_defeat(event.name, true);
        break;
    case Kind::Defeat:
        appear_or_defeat(event.name, false);
        break;
    }
}

// An event flag: SpinAndStarPointer (the Gateway Luma's grant), LuigiFile (the file played as Luigi), name=value (an
// event value: LibraryOpenNewStarCount=0 with Grand Star 3 opens the Library), or a stored flag by name.
void set_flag(const std::string& flag) {
    say("test: event flag " + flag);
    const std::size_t equals = flag.find('=');
    if (flag == "SpinAndStarPointer") {
        if (NEBULA_MOD_API_HAS(g_api, grant_spin_and_pointer)) {
            (void)g_api->grant_spin_and_pointer();
        }
    } else if (flag == "LuigiFile") {
        if (NEBULA_MOD_API_HAS(g_api, set_player_character)) {
            (void)g_api->set_player_character(1);
        }
    } else if (equals != std::string::npos) {
        g_api->set_event_value(flag.substr(0u, equals).c_str(), static_cast<std::int32_t>(number(std::string_view(flag).substr(equals + 1u), 0u)));
    } else {
        g_api->set_event_flag_on(flag.c_str());
    }
}

// The walk: the player in body of water `index` of the stage's, or at the index-th live actor kind, held 30 frames.
void walk_place(std::uint64_t index) {
    Run& run = g_run;
    NebulaActor* const player = g_api->player();
    if (player == nullptr) {
        return;
    }
    if (g_api->value(NEBULA_VALUE_SWIMMING) != 0) {
        ++run.swim_frames;
    }
    const bool fresh = static_cast<std::int64_t>(index) != run.walk_index;
    run.walk_index = static_cast<std::int64_t>(index);
    if (fresh && run.walk.forms) {
        static constexpr std::int32_t kForms[] = {NEBULA_POWER_UP_NONE, NEBULA_POWER_UP_INVINCIBLE, NEBULA_POWER_UP_FIRE,
                                                  NEBULA_POWER_UP_ICE,  NEBULA_POWER_UP_BEE,        NEBULA_POWER_UP_SPRING,
                                                  NEBULA_POWER_UP_BOO,  NEBULA_POWER_UP_FLYING,     NEBULA_POWER_UP_TORNADO};
        const std::int32_t form = kForms[index % (sizeof(kForms) / sizeof(kForms[0]))];
        say("test: form " + std::to_string(form));
        g_api->set_value(NEBULA_SET_POWER_UP, form);
    }
    if (run.walk.water) {
        if (!NEBULA_MOD_API_HAS(g_api, water_point)) {
            return;
        }
        float xyz[3] = {};
        const std::int32_t count = g_api->water_point(static_cast<std::int32_t>(index % 0x7FFFFFFFu), xyz);
        if (count > 0) {
            if (fresh) {
                char text[160];
                std::snprintf(text, sizeof(text), "test: water %d/%d %.0f %.0f %.0f in %d swam %u",
                              static_cast<int>(index % static_cast<std::uint64_t>(count)), count, static_cast<double>(xyz[0]),
                              static_cast<double>(xyz[1]), static_cast<double>(xyz[2]), g_api->is_in_water(xyz), run.swim_frames);
                say(text);
            }
            g_api->set_position(player, xyz);
        } else if (fresh) {
            say("test: water none swam " + std::to_string(run.swim_frames));
        }
        return;
    }
    if (fresh) {
        // In a stride through the list, so each stop visits actors of every part of the level; each kind once a scene
        // while kinds remain; the player's own models are no place.
        std::vector<NebulaActor*> actors;
        for (NebulaActor* actor = g_api->find_actor(nullptr, nullptr); actor != nullptr; actor = g_api->find_actor(nullptr, actor)) {
            actors.push_back(actor);
        }
        run.walk_target = nullptr;
        const std::uint64_t total = actors.size();
        for (int pass = 0; pass < 2 && run.walk_target == nullptr && total > 0u; ++pass) {
            for (std::uint64_t i = 0u; i < total && run.walk_target == nullptr; ++i) {
                NebulaActor* const actor = actors[(index * 2654435761u + i) % total];
                if (actor == player || g_api->is_dead(actor) != 0 || g_api->distance_to_player(actor) < 1.0f) {
                    continue;
                }
                const char* const name = g_api->actor_name(actor);
                const std::string kind = name != nullptr ? name : "";
                if (kind.rfind("Mario", 0) == 0 || kind.rfind("mario", 0) == 0 ||
                    (pass == 0 && std::find(run.walked.begin(), run.walked.end(), kind) != run.walked.end())) {
                    continue;
                }
                run.walk_target = actor;
                if (pass == 0) {
                    run.walked.push_back(kind);
                }
            }
        }
        if (run.walk_target != nullptr) {
            const char* const name = g_api->actor_name(run.walk_target);
            say(std::string("test: actor ") + (name != nullptr ? name : "?") + " " + where(run.walk_target));
        }
    }
    if (run.walk_target != nullptr && g_api->is_dead(run.walk_target) == 0) {
        float xyz[3] = {}, gravity[3] = {};
        g_api->get_position(run.walk_target, xyz);
        g_api->get_gravity(run.walk_target, gravity);
        for (int i = 0; i < 3; ++i) {
            xyz[i] -= gravity[i] * 150.0f;
        }
        g_api->set_position(player, xyz);
    }
}

// Once a scene frame, at the start of GameScene::draw: at most one of the run's actions, in the order the host's test
// driver kept (flags, Grand Stars, the tour's end, its next stop, then the stop's events, walk and teleports).
void scene_frame() {
    Run& run = g_run;
    ++run.scene_frames;
    const std::uint64_t stop_frames = run.touring && run.stop_frame == 0u ? kNone : run.scene_frames - run.stop_frame;
    g_scene_frame(run.scene_frames, stop_frames, kNone);
    if (!run.flags.empty() && run.scene_frames >= 2u) {
        set_flag(run.flags.front());
        run.flags.erase(run.flags.begin());
        return;
    }
    if (run.grand_stars > 0 && !run.grand_stars_set && run.scene_frames >= 2u) {
        run.grand_stars_set = true;
        if (NEBULA_MOD_API_HAS(g_api, give_grand_stars)) {
            (void)g_api->give_grand_stars(run.grand_stars);
        }
        say("test: grand stars set");
        return;
    }
    if (run.touring && run.next == run.stops.size() && !run.done && ++run.since_stop >= std::max(run.tour_frames, run.last_frame)) {
        run.done = true;
        say("tour: done");
        g_done();
    }
    if (run.touring && run.next < run.stops.size() && ++run.since_stop >= run.tour_frames) {
        run.since_stop = 0u;
        run.stop_frame = run.scene_frames;
        const Stop& stop = run.stops[run.next++];
        g_api->change_stage(stop.stage.c_str(), stop.scenario);
        say("tour: " + std::to_string(run.next) + "/" + std::to_string(run.stops.size()) + " " + stop.stage + " " + std::to_string(stop.scenario));
        g_scene_frame(run.scene_frames, 0u, 0u);  // a load follows: its pictures are held
        return;
    }
    if (run.touring) {
        // Pictures named by stop and scene frame: a build's load frames do not move them.
        g_scene_frame(run.scene_frames, stop_frames, run.next * 1000u + run.since_stop);
    }
    if (run.touring && run.stop_frame == 0u) {
        return;
    }
    const std::uint64_t frame = run.scene_frames - run.stop_frame;
    const std::int32_t stop_star = run.next > 0u ? run.stops[run.next - 1u].scenario : 1;
    for (const Event& event : run.events) {
        if (frame == event.frame && event.kind != Kind::Wait) {
            say("test: event " + event.label + " at frame " + std::to_string(frame));
            run_event(event, stop_star);
            return;
        }
    }
    if (run.walk.on && frame >= 300u && (frame - 300u) % run.walk.every < 30u) {
        walk_place((frame - 300u) / run.walk.every + run.walk.seed);
        return;
    }
    for (const Teleport& t : run.teleports) {
        if (frame >= t.frame && frame < t.frame + 30u) {
            if (NebulaActor* const player = g_api->player()) {
                g_api->set_position(player, t.xyz);
            }
            return;
        }
    }
}

// GameScene::draw, hooked: the run's frame first, then the game's draw.
using DrawFn = void (*)(const void* scene);
DrawFn g_original_draw = nullptr;

void draw(const void* scene) {
    scene_frame();
    g_original_draw(scene);
}

}  // namespace

NEBULA_TEST_PLUGIN_EXPORT std::uint32_t nebula_plugin_api(void) {
    return NEBULA_MOD_API_VERSION;
}

NEBULA_TEST_PLUGIN_EXPORT void nebula_plugin_start(const NebulaModApi* api, const NebulaPlugin* self) {
    g_api = api;
    g_self = self;
    find_extension();
    read_tour();
    read_events();
    if (!g_run.touring && g_run.events.empty() && g_run.teleports.empty() && g_run.flags.empty() && g_run.grand_stars == 0 && !g_run.walk.on) {
        return;  // nothing asked: the game's draw stays its own
    }
#if defined(_WIN32)
    const char* const symbol = "?draw@GameScene@@UEBAXXZ";
#else
    const char* const symbol = "_ZNK9GameScene4drawEv";
#endif
    void* const target = api->find_function(symbol);
    if (target == nullptr || api->hook(target, reinterpret_cast<void*>(&draw), reinterpret_cast<void**>(&g_original_draw)) != 0) {
        say("test: GameScene::draw could not be hooked; the run's tour and events do not run");
    }
}
