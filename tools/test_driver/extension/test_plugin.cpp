// SPDX-License-Identifier: GPL-3.0-only
// The test runs' game half: a LumaCore mod in the test library (luma.h), which run.sh also installs into the run's mods
// (tools/verify/test_plugin.py). From the run's GALAXY_TEST_* environment it drives tours, test events, event flags,
// Grand Stars, teleports and walks through the game's own functions, called by name, once a scene frame before
// GameScene::draw (a before hook, added only when the run asks for something). Each scene frame, the tour's stops and
// its end go to the library's extension instance (the one the host loaded with --extension), which keeps the script
// clock and names the pictures.
//
// The library keeps its C runtime (it is also the host's extension), so its luma_mod is written out here without
// luma.h's freestanding runtime (LUMA_MOD): no begin or finish, which the loader allows (LumaModInfo).
#include <luma.h>
#include <nebula_mod_api.h>  // NebulaMaterialInfo, the shape nebula_mod_list_materials gives (NebulaModCalls.cpp)

#include <algorithm>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <initializer_list>
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
#else
#include <dlfcn.h>
#include <link.h>
#endif

// test_extension.cpp's, in this instance of the library.
extern "C" void nebula_test_scene_frame(std::uint64_t scene_frames, std::uint64_t stop_frames, std::uint64_t picture_number);
extern "C" void nebula_test_done();

namespace {

const LumaCore* g_core = nullptr;
const LumaMod* g_mod = nullptr;

constexpr std::uint64_t kNone = ~std::uint64_t{0};  // no stop yet; no picture number

void say(const std::string& line) {
    g_core->log(g_mod, line.c_str());
}

// --- The game's functions and fields, by name (luma_functions.json, luma_types.json) --------------------------------

const LumaFunction* f_draw;            // GameScene::draw, hooked when the run asks for something
const LumaFunction* f_player_actor;    // MarioAccess::getPlayerActor
const LumaFunction* f_file_select;     // MR::isStageFileSelect
const LumaFunction* f_scene_obj;       // MR::isExistSceneObj
const LumaFunction* f_all_actors;      // MR::getAllLiveActorGroup
const LumaFunction* f_actor_at;        // LiveActorGroup::getActor
const LumaFunction* f_appear;          // LiveActor::appear
const LumaFunction* f_kill;            // LiveActor::kill
const LumaFunction* f_is_dead;         // MR::isDead(const LiveActor *)
const LumaFunction* f_set_player_pos;  // MR::setPlayerPos(const Vec3f &)
const LumaFunction* f_force_kill;      // MarioAccess::forceKill
const LumaFunction* f_shake;           // MR::shakeCameraNormal
const LumaFunction* f_distance;        // MR::calcDistanceToPlayer(const LiveActor *)
const LumaFunction* f_change_stage;    // NebulaModCalls::changeStage (keeps its own copy of the name)
const LumaFunction* f_flag_exists;     // GameEventFlagTable::isExist
const LumaFunction* f_flag_on;         // GameDataFunction::onGameEventFlag
const LumaFunction* f_event_value;     // GameDataFunction::setGameEventValue
const LumaFunction* f_in_water;        // MR::isInWater(const Vec3f &)
const LumaFunction* f_swimming;        // Mario::isSwimming
const LumaFunction* f_can_take;        // MarioActor::canTakePowerUp
const LumaFunction* f_set_mode;        // MarioActor::setPlayerMode
const LumaFunction* f_give_star;       // nebula_mod_give_star (NebulaModCalls.cpp: the star's get demo, once in play)
const LumaFunction* f_grand_stars;     // nebula_mod_give_grand_stars
const LumaFunction* f_character;       // nebula_mod_set_player_character
const LumaFunction* f_materials;       // nebula_mod_list_materials
const LumaFunction* f_water_point;     // nebula_mod_water_point
const LumaFunction* f_circle_wipe;     // nebula_mod_circle_wipe
const LumaFunction* f_spin_grant;      // nebula_mod_grant_spin_and_pointer
std::int32_t o_position = -1;          // LiveActor.mPosition (Vec3f)
std::int32_t o_gravity = -1;           // LiveActor.mGravity (Vec3f)
std::int32_t o_name = -1;              // NameObj.mName
std::int32_t o_mario = -1;             // MarioActor.mMario
std::int32_t o_count = -1;             // NameObjGroup.mObjNum

constexpr std::int32_t kSceneMarioHolder = 0x14;      // SceneObj_MarioHolder (SceneObjHolder.hpp)
constexpr std::int32_t kSceneAllLiveActors = 0x30;    // SceneObj_AllLiveActorGroup

// LumaFunction.invoke's arguments: [0] the object for a member, then a pointer to each argument (a reference's object).
template <class R>
R call(const LumaFunction* fn, void* self, std::initializer_list<void*> args) {
    void* slots[8] = {self};
    std::size_t n = 1;
    for (void* a : args) slots[n++] = a;
    R result{};
    fn->invoke(slots, &result);
    return result;
}

void call0(const LumaFunction* fn, void* self, std::initializer_list<void*> args) {
    void* slots[8] = {self};
    std::size_t n = 1;
    for (void* a : args) slots[n++] = a;
    fn->invoke(slots, nullptr);
}

template <class T>
T& field(void* object, std::int32_t offset) {
    return *reinterpret_cast<T*>(static_cast<char*>(object) + offset);
}

bool scene_has(std::int32_t id) {
    return call<bool>(f_scene_obj, nullptr, {&id});
}

// The player in a stage being played: none between scenes or on the file select screen.
void* player() {
    if (call<bool>(f_file_select, nullptr, {}) || !scene_has(kSceneMarioHolder)) {
        return nullptr;
    }
    return call<void*>(f_player_actor, nullptr, {});
}

// The live actors' list: the n-th, or null past its end (and outside a stage).
std::int32_t actor_count() {
    if (!scene_has(kSceneAllLiveActors)) {
        return 0;
    }
    void* const group = call<void*>(f_all_actors, nullptr, {});
    return group != nullptr ? field<std::int32_t>(group, o_count) : 0;
}

void* actor_at(std::int32_t index) {
    void* const group = call<void*>(f_all_actors, nullptr, {});
    return group != nullptr ? call<void*>(f_actor_at, group, {&index}) : nullptr;
}

bool is_dead(void* actor) {
    return call<bool>(f_is_dead, nullptr, {&actor});
}

const char* actor_name(void* actor) {
    return field<const char*>(actor, o_name);
}

const float* position(void* actor) {
    return &field<float>(actor, o_position);
}

void set_position(void* actor, const float* xyz) {
    if (actor == player()) {
        float at[3] = {xyz[0], xyz[1], xyz[2]};
        call0(f_set_player_pos, nullptr, {at});
    } else {
        float* const to = &field<float>(actor, o_position);
        to[0] = xyz[0];
        to[1] = xyz[1];
        to[2] = xyz[2];
    }
}

// Each live actor whose name is `name` (all when null), in the list's order.
std::vector<void*> actors(const char* name) {
    std::vector<void*> out;
    const std::int32_t count = actor_count();
    for (std::int32_t i = 0; i < count; ++i) {
        void* const actor = actor_at(i);
        if (actor == nullptr) continue;
        const char* const its = actor_name(actor);
        if (name == nullptr || (its != nullptr && std::string_view(its) == name)) out.push_back(actor);
    }
    return out;
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
    void* walk_target = nullptr;
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
                // strtof, not std::from_chars: libc++ (Android) has no floating-point from_chars.
                char* end = nullptr;
                t.xyz[i] = std::strtof(parts[i].c_str(), &end);
                good = !parts[i].empty() && end == parts[i].c_str() + parts[i].size();
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

std::string where(void* actor) {
    const float* const xyz = position(actor);
    char text[96];
    std::snprintf(text, sizeof(text), "%.0f %.0f %.0f", static_cast<double>(xyz[0]), static_cast<double>(xyz[1]), static_cast<double>(xyz[2]));
    return text;
}

void list_actors() {
    for (void* const actor : actors(nullptr)) {
        if (is_dead(actor)) {
            continue;
        }
        const char* const name = actor_name(actor);
        say(std::string("actor ") + (name != nullptr ? name : "?") + " " + where(actor));
    }
}

void list_materials(const std::string& model) {
    struct Out {
        std::string model;
    } out{model};
    const NebulaMaterialCallback each = [](const NebulaMaterialInfo* material, void* user) {
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
    };
    const char* name = model.c_str();
    void* user = &out;
    const std::int32_t count = call<std::int32_t>(f_materials, nullptr, {&name, const_cast<NebulaMaterialCallback*>(&each), &user});
    if (count == 0) {
        say("test: no materials of " + model);
    }
}

// Each actor of that level name, dead ones appearing (`appear`) or live ones killed as their defeat ends.
void appear_or_defeat(const std::string& name, bool appear) {
    for (void* const actor : actors(name.c_str())) {
        const bool dead = is_dead(actor);
        if (dead != appear) {
            continue;
        }
        say(std::string(appear ? "test: appear " : "test: kill ") + name + " " + where(actor));
        call0(appear ? f_appear : f_kill, actor, {});
    }
}

void run_event(const Event& event, std::int32_t stop_star) {
    std::int32_t value = event.value >= 0 ? event.value : stop_star;
    switch (event.kind) {
    case Kind::Wait:
        return;
    case Kind::KillPlayer:
        if (player() != nullptr) {
            std::uint32_t zero = 0u;
            call0(f_force_kill, nullptr, {&zero, &zero});
        }
        break;
    case Kind::CircleWipe: {
        std::int32_t frames = 60;
        (void)call<std::int32_t>(f_circle_wipe, nullptr, {&value, &frames});
        say(std::string("test: circle ") + (value != 0 ? "open" : "close") + ", 60 frames");
        break;
    }
    case Kind::Star:
    case Kind::GrandStar: {
        const char* galaxy = nullptr;
        std::int32_t grand = event.kind == Kind::GrandStar ? 1 : 0;
        (void)call<std::int32_t>(f_give_star, nullptr, {&galaxy, &value, &grand});
        break;
    }
    case Kind::Character:
        (void)call<std::int32_t>(f_character, nullptr, {&value});
        break;
    case Kind::Shake:
        call0(f_shake, nullptr, {});
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
        (void)call<std::int32_t>(f_spin_grant, nullptr, {});
    } else if (flag == "LuigiFile") {
        std::int32_t luigi = 1;
        (void)call<std::int32_t>(f_character, nullptr, {&luigi});
    } else if (equals != std::string::npos) {
        // The saved value is 16 bits: out of range is held at its ends.
        const std::string name = flag.substr(0u, equals);
        const char* text = name.c_str();
        std::uint16_t value = static_cast<std::uint16_t>(std::min<std::uint64_t>(number(std::string_view(flag).substr(equals + 1u), 0u), 65535u));
        call0(f_event_value, nullptr, {&text, &value});
    } else {
        const char* text = flag.c_str();
        if (call<bool>(f_flag_exists, nullptr, {&text})) {
            call0(f_flag_on, nullptr, {&text});
        } else {
            say("test: no event flag " + flag);
        }
    }
}

// The walk: the player in body of water `index` of the stage's, or at the index-th live actor kind, held 30 frames.
void walk_place(std::uint64_t index) {
    Run& run = g_run;
    void* const me = player();
    if (me == nullptr) {
        return;
    }
    if (void* const mario = field<void*>(me, o_mario); mario != nullptr && call<bool>(f_swimming, mario, {})) {
        ++run.swim_frames;
    }
    const bool fresh = static_cast<std::int64_t>(index) != run.walk_index;
    run.walk_index = static_cast<std::int64_t>(index);
    if (fresh && run.walk.forms) {
        // The game's forms (MarioActor's player modes): none, invincible, fire, ice, bee, spring, boo, flying, tornado.
        static constexpr std::int32_t kForms[] = {0, 1, 2, 3, 4, 5, 6, 7, 9};
        std::int32_t form = kForms[index % (sizeof(kForms) / sizeof(kForms[0]))];
        say("test: form " + std::to_string(form));
        if (call<bool>(f_can_take, me, {&form})) {
            std::uint16_t mode = static_cast<std::uint16_t>(form);
            bool quiet = false;
            call0(f_set_mode, me, {&mode, &quiet});
        }
    }
    if (run.walk.water) {
        float xyz[3] = {};
        float* out = xyz;
        std::int32_t at = static_cast<std::int32_t>(index % 0x7FFFFFFFu);
        const std::int32_t count = call<std::int32_t>(f_water_point, nullptr, {&at, &out});
        if (count > 0) {
            if (fresh) {
                char text[160];
                std::snprintf(text, sizeof(text), "test: water %d/%d %.0f %.0f %.0f in %d swam %u",
                              static_cast<int>(index % static_cast<std::uint64_t>(count)), count, static_cast<double>(xyz[0]),
                              static_cast<double>(xyz[1]), static_cast<double>(xyz[2]),
                              call<bool>(f_in_water, nullptr, {xyz}) ? 1 : 0, run.swim_frames);
                say(text);
            }
            set_position(me, xyz);
        } else if (fresh) {
            say("test: water none swam " + std::to_string(run.swim_frames));
        }
        return;
    }
    if (fresh) {
        // In a stride through the list, so each stop visits actors of every part of the level; each kind once a scene
        // while kinds remain; the player's own models are no place.
        const std::vector<void*> all = actors(nullptr);
        run.walk_target = nullptr;
        const std::uint64_t total = all.size();
        for (int pass = 0; pass < 2 && run.walk_target == nullptr && total > 0u; ++pass) {
            for (std::uint64_t i = 0u; i < total && run.walk_target == nullptr; ++i) {
                void* actor = all[(index * 2654435761u + i) % total];
                if (actor == me || is_dead(actor) || call<float>(f_distance, nullptr, {&actor}) < 1.0f) {
                    continue;
                }
                const char* const name = actor_name(actor);
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
            const char* const name = actor_name(run.walk_target);
            say(std::string("test: actor ") + (name != nullptr ? name : "?") + " " + where(run.walk_target));
        }
    }
    if (run.walk_target != nullptr && !is_dead(run.walk_target)) {
        const float* const at = position(run.walk_target);
        const float* const down = &field<float>(run.walk_target, o_gravity);
        float xyz[3];
        for (int i = 0; i < 3; ++i) {
            xyz[i] = at[i] - down[i] * 150.0f;
        }
        set_position(me, xyz);
    }
}

// Once a scene frame, before GameScene::draw: at most one of the run's actions, in the order the host's test driver
// kept (flags, Grand Stars, the tour's end, its next stop, then the stop's events, walk and teleports).
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
        std::int32_t count = run.grand_stars;
        (void)call<std::int32_t>(f_grand_stars, nullptr, {&count});
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
        const char* stage = stop.stage.c_str();
        std::int32_t scenario = stop.scenario;
        call0(f_change_stage, nullptr, {&stage, &scenario});
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
            if (void* const me = player()) {
                set_position(me, t.xyz);
            }
            return;
        }
    }
}

// GameScene::draw, before the game's: the run's frame.
std::int32_t before_draw(LumaCall*, void*) {
    scene_frame();
    return 0;
}

std::int32_t start(const LumaCore* core, const LumaMod* mod) {
    g_core = core;
    g_mod = mod;
    find_extension();
    read_tour();
    read_events();
    if (!g_run.touring && g_run.events.empty() && g_run.teleports.empty() && g_run.flags.empty() && g_run.grand_stars == 0 && !g_run.walk.on) {
        return 0;  // nothing asked: the game's draw stays its own
    }
    if (core->hook(mod, f_draw, LUMA_BEFORE, before_draw, nullptr) != 0) {
        say("test: GameScene::draw could not be hooked; the run's tour and events do not run");
    }
    return 0;
}

const LumaImport kImports[] = {
    LUMA_FUNCTION(f_draw, "GameScene::draw", "void() const"),
    LUMA_FUNCTION(f_player_actor, "MarioAccess::getPlayerActor", "MarioActor *()"),
    LUMA_FUNCTION(f_file_select, "MR::isStageFileSelect", "bool()"),
    LUMA_FUNCTION(f_scene_obj, "MR::isExistSceneObj", "bool(int)"),
    LUMA_FUNCTION(f_all_actors, "MR::getAllLiveActorGroup", "AllLiveActorGroup *()"),
    LUMA_FUNCTION(f_actor_at, "LiveActorGroup::getActor", "LiveActor *(int) const"),
    LUMA_FUNCTION(f_appear, "LiveActor::appear", "void()"),
    LUMA_FUNCTION(f_kill, "LiveActor::kill", "void()"),
    LUMA_FUNCTION(f_is_dead, "MR::isDead", "bool(const LiveActor *)"),
    LUMA_FUNCTION(f_set_player_pos, "MR::setPlayerPos", "void(const Vec3f &)"),
    LUMA_FUNCTION(f_force_kill, "MarioAccess::forceKill", "void(u32, u32)"),
    LUMA_FUNCTION(f_shake, "MR::shakeCameraNormal", "void()"),
    LUMA_FUNCTION(f_distance, "MR::calcDistanceToPlayer", "f32(const LiveActor *)"),
    LUMA_FUNCTION(f_change_stage, "NebulaModCalls::changeStage", "void(const char *, s32)"),
    LUMA_FUNCTION(f_flag_exists, "GameEventFlagTable::isExist", "bool(const char *)"),
    LUMA_FUNCTION(f_flag_on, "GameDataFunction::onGameEventFlag", "void(const char *)"),
    LUMA_FUNCTION(f_event_value, "GameDataFunction::setGameEventValue", "void(const char *, u16)"),
    LUMA_FUNCTION(f_in_water, "MR::isInWater", "bool(const Vec3f &)"),
    LUMA_FUNCTION(f_swimming, "Mario::isSwimming", "bool() const"),
    LUMA_FUNCTION(f_can_take, "MarioActor::canTakePowerUp", "bool(s32) const"),
    LUMA_FUNCTION(f_set_mode, "MarioActor::setPlayerMode", "void(u16, bool)"),
    LUMA_FUNCTION(f_give_star, "nebula_mod_give_star", "int32_t(const char *, int32_t, int32_t)"),
    LUMA_FUNCTION(f_grand_stars, "nebula_mod_give_grand_stars", "int32_t(int32_t)"),
    LUMA_FUNCTION(f_character, "nebula_mod_set_player_character", "int32_t(int32_t)"),
    LUMA_FUNCTION(f_materials, "nebula_mod_list_materials", "int32_t(const char *, NebulaMaterialCallback, void *)"),
    LUMA_FUNCTION(f_water_point, "nebula_mod_water_point", "int32_t(int32_t, float *)"),
    LUMA_FUNCTION(f_circle_wipe, "nebula_mod_circle_wipe", "int32_t(int32_t, int32_t)"),
    LUMA_FUNCTION(f_spin_grant, "nebula_mod_grant_spin_and_pointer", "int32_t()"),
    LUMA_FIELD(o_position, "LiveActor.mPosition", "Vec3f"),
    LUMA_FIELD(o_gravity, "LiveActor.mGravity", "Vec3f"),
    LUMA_FIELD(o_name, "NameObj.mName", "const char *"),
    LUMA_FIELD(o_mario, "MarioActor.mMario", "Mario *"),
    LUMA_FIELD(o_count, "NameObjGroup.mObjNum", "s32"),
};

}  // namespace

// The library as a LumaCore mod (loader contract 1): its imports bound by name, whole or not at all, then start.
LUMA_C LUMA_EXPORT const LumaModInfo luma_mod = {
    sizeof(LumaModInfo), LUMA_API, kImports, sizeof(kImports) / sizeof(kImports[0]), start, nullptr, nullptr, nullptr, nullptr, 0u, nullptr, nullptr, 0u};
