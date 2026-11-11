// SPDX-License-Identifier: GPL-3.0-only
// test.svc and test.secret (luma_test_services.h): services whose versions and permission the registry's tests control.
// Everything but the posting thread runs on the game thread, as the kernel calls a provider; the posting thread only
// calls the kernel's post with nodes made before it starts.
#include "test_services.h"

#include "luma_test_services.h"

#include <cstring>
#include <initializer_list>
#include <map>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace test_services {
namespace {

const LumaServiceKernel* g_kernel = nullptr;  // set at registration (game thread), then only read
std::uint64_t g_seq = 0;                       // the provider's moments in order (game thread)
std::uint64_t g_request = 0;                   // request ids, from 1 (game thread)

struct Node {
    LumaEventNode node{};  // first: the kernel's pointer is the Node's
    LumaEventFn fn = nullptr;
    void* user = nullptr;
    std::uint64_t value = 0;
    std::uint64_t request = 0;
    bool again = false;  // post once more from the delivery (post_again)
    bool busy = false;   // posted and not yet delivered or dropped
};

struct Mod {
    std::string id;
    LumaTestSvcStats stats{};
    std::vector<std::unique_ptr<Node>> nodes;  // kept until the mod's end; idle ones are used again
    std::uint64_t next_value = 0;
    bool gone = false;
};

// By mod while it lives (game thread); stats by id for the whole session, so a mod can ask after another's end.
std::map<const LumaMod*, Mod> g_mods;
std::map<std::string, LumaTestSvcStats> g_ended;

Mod* mod_of(const LumaMod* mod) {
    const auto found = g_mods.find(mod);
    return found != g_mods.end() ? &found->second : nullptr;
}

void report(const LumaMod* mod, const char* where, std::uint32_t code, std::initializer_list<const char*> args) {
    // The registry's rows name "where" first (services-registry 11): it is the first argument as well as the place.
    std::vector<const char*> all{where};
    all.insert(all.end(), args.begin(), args.end());
    g_kernel->report(mod, where, code, all.data(), static_cast<std::uint32_t>(all.size()));
}

// Each function's first checks (services-registry 4): the mod, known to the kernel (any thread), then the thread.
int32_t enter(const LumaMod* mod, const char* where) {
    if (mod == nullptr || g_kernel->mod_id(mod) == nullptr) {
        g_kernel->unknown_mod(where);
        return LUMA_E_ARGUMENT;
    }
    if (g_kernel->game_thread() == 0) {
        report(mod, where, 100u, {});
        return LUMA_E_THREAD;
    }
    return LUMA_OK;
}

void deliver(LumaEventNode* raw);
void drop(LumaEventNode* raw);

Node* take_node(Mod& owner, const LumaMod* mod, LumaEventFn fn, void* user, std::uint64_t request) {
    Node* picked = nullptr;
    for (const std::unique_ptr<Node>& node : owner.nodes) {
        if (!node->busy) {
            picked = node.get();
            break;
        }
    }
    if (picked == nullptr) {
        owner.nodes.push_back(std::make_unique<Node>());
        picked = owner.nodes.back().get();
    }
    picked->node = {};
    picked->node.mod = mod;
    picked->node.deliver = deliver;
    picked->node.drop = drop;
    picked->fn = fn;
    picked->user = user;
    picked->value = owner.next_value++;
    picked->request = request;
    picked->again = false;
    picked->busy = true;
    return picked;
}

void deliver(LumaEventNode* raw) {
    Node* const node = reinterpret_cast<Node*>(raw);
    Mod* const owner = mod_of(raw->mod);
    if (owner == nullptr) {
        node->busy = false;
        return;
    }
    if (owner->gone) {
        ++owner->stats.after_gone;
    }
    LumaEvent event{};
    event.size = sizeof(event);
    event.kind = LUMA_TEST_SVC_EVENT_POSTED;
    event.status = LUMA_OK;
    event.handle = node->request;
    event.value = node->value;
    const bool again = node->again;
    node->again = false;
    if (again) {
        // Queued from inside its own delivery: the kernel gives it at the next frame's start (services-registry 8).
        node->value = owner->next_value++;
        ++owner->stats.posted;
        if (g_kernel->post(raw) != LUMA_OK) {
            --owner->stats.posted;
            node->busy = false;
        }
    } else {
        node->busy = false;
    }
    // Counted before the call: the mod's function may end in its taking out, after which `owner` is not touched.
    // LUMA_E_CANCELLED means the function was not called and the mod is still known.
    owner->stats.delivered_seq = ++g_seq;
    ++owner->stats.delivered;
    if (g_kernel->call(raw->mod, "test.svc", node->fn, node->user, &event) == LUMA_E_CANCELLED) {
        --owner->stats.delivered;
    }
}

void drop(LumaEventNode* raw) {
    Node* const node = reinterpret_cast<Node*>(raw);
    node->busy = false;
    if (Mod* const owner = mod_of(raw->mod)) {
        ++owner->stats.dropped;
    }
}

int32_t echo(const LumaMod* mod, std::uint64_t value, std::uint64_t* out) {
    if (const int32_t status = enter(mod, "test.svc.echo"); status != LUMA_OK) {
        return status;
    }
    if (out == nullptr) {
        report(mod, "test.svc.echo", 101u, {"out"});
        return LUMA_E_ARGUMENT;
    }
    *out = value + 1u;
    return LUMA_OK;
}

int32_t post_events(const LumaMod* mod, std::uint32_t count, LumaEventFn fn, void* user) {
    const char* const where = "test.svc.post_events";
    if (const int32_t status = enter(mod, where); status != LUMA_OK) {
        return status;
    }
    if (fn == nullptr) {
        report(mod, where, 101u, {"fn"});
        return LUMA_E_ARGUMENT;
    }
    if (count == 0u) {
        report(mod, where, 101u, {"count"});
        return LUMA_E_ARGUMENT;
    }
    if (count > LUMA_TEST_SVC_MOST_EVENTS) {
        // Service.Limit's {count} is the limit reached (services-registry 11).
        const std::string limit = std::to_string(LUMA_TEST_SVC_MOST_EVENTS);
        report(mod, where, 104u, {limit.c_str()});
        return LUMA_E_LIMIT;
    }
    Mod* const owner = mod_of(mod);
    if (owner == nullptr || owner->gone) {
        return LUMA_E_CANCELLED;
    }
    const std::uint64_t request = ++g_request;
    std::vector<LumaEventNode*> nodes;
    nodes.reserve(count);
    for (std::uint32_t i = 0; i < count; ++i) {
        nodes.push_back(&take_node(*owner, mod, fn, user, request)->node);
    }
    owner->stats.posted += count;
    // Posted from another thread, as a service's worker posts. The test waits for it so that every post lands before
    // the next frame's start (2110's "in one frame"); a real service never waits.
    std::thread poster([&nodes] {
        for (LumaEventNode* node : nodes) {
            g_kernel->post(node);
        }
    });
    poster.join();
    return LUMA_PENDING;
}

int32_t post_one(const LumaMod* mod, const char* where, LumaEventFn fn, void* user, bool twice, bool again) {
    if (const int32_t status = enter(mod, where); status != LUMA_OK) {
        return status;
    }
    if (fn == nullptr) {
        report(mod, where, 101u, {"fn"});
        return LUMA_E_ARGUMENT;
    }
    Mod* const owner = mod_of(mod);
    if (owner == nullptr || owner->gone) {
        return LUMA_E_CANCELLED;
    }
    Node* const node = take_node(*owner, mod, fn, user, ++g_request);
    node->again = again;
    ++owner->stats.posted;
    if (g_kernel->post(&node->node) != LUMA_OK) {
        --owner->stats.posted;
        node->busy = false;
        return LUMA_E_CANCELLED;
    }
    if (twice) {
        g_kernel->post(&node->node);  // queued already: LUMA_OK, nothing more
    }
    return LUMA_PENDING;
}

int32_t post_twice(const LumaMod* mod, LumaEventFn fn, void* user) {
    return post_one(mod, "test.svc.post_twice", fn, user, true, false);
}

int32_t post_again(const LumaMod* mod, LumaEventFn fn, void* user) {
    return post_one(mod, "test.svc.post_again", fn, user, false, true);
}

int32_t take_struct(const LumaMod* mod, const LumaTestSvcStruct* s) {
    const char* const where = "test.svc.take_struct";
    if (const int32_t status = enter(mod, where); status != LUMA_OK) {
        return status;
    }
    if (s == nullptr) {
        report(mod, where, 101u, {"s"});
        return LUMA_E_ARGUMENT;
    }
    if (s->size < sizeof(LumaTestSvcStruct)) {
        const std::string found = std::to_string(s->size);
        const std::string expected = std::to_string(sizeof(LumaTestSvcStruct));
        report(mod, where, 103u, {"LumaTestSvcStruct", found.c_str(), expected.c_str()});
        return LUMA_E_ARGUMENT;
    }
    return LUMA_OK;
}

int32_t stats(const LumaMod* mod, const char* mod_id, LumaTestSvcStats* out) {
    const char* const where = "test.svc.stats";
    if (const int32_t status = enter(mod, where); status != LUMA_OK) {
        return status;
    }
    if (mod_id == nullptr || out == nullptr || out->size < 8u) {
        report(mod, where, 101u, {mod_id == nullptr ? "mod_id" : "out"});
        return LUMA_E_ARGUMENT;
    }
    LumaTestSvcStats found{};
    for (const auto& entry : g_mods) {
        if (entry.second.id == mod_id) {
            found = entry.second.stats;
        }
    }
    if (found.known == 0u) {
        if (const auto ended = g_ended.find(mod_id); ended != g_ended.end()) {
            found = ended->second;
        }
    }
    const std::uint32_t size = out->size < sizeof(found) ? out->size : static_cast<std::uint32_t>(sizeof(found));
    found.size = out->size;
    std::memcpy(out, &found, size);
    return LUMA_OK;
}

int32_t ping(const LumaMod* mod) {
    return enter(mod, "test.secret.ping");
}

void registered(void*, const LumaServiceKernel* kernel) {
    g_kernel = kernel;
}

void mod_begin(void*, const LumaMod* mod) {
    Mod& owner = g_mods[mod];
    const char* const id = g_kernel->mod_id(mod);
    owner.id = id != nullptr ? id : "";
    owner.stats.size = sizeof(owner.stats);
    owner.stats.known = 1u;
    owner.stats.begin_seq = ++g_seq;
}

void mod_gone(void*, const LumaMod* mod) {
    if (Mod* const owner = mod_of(mod)) {
        owner->gone = true;
        owner->stats.gone_seq = ++g_seq;
    }
}

void mod_end(void*, const LumaMod* mod) {
    const auto found = g_mods.find(mod);
    if (found == g_mods.end()) {
        return;
    }
    found->second.stats.end_seq = ++g_seq;
    g_ended[found->second.id] = found->second.stats;
    g_mods.erase(found);  // its nodes go with it: the kernel holds none after the mod's gone
}

const LumaTestSvcService kSvc = {
    {sizeof(LumaTestSvcService), LUMA_TEST_SVC_MAJOR, LUMA_TEST_SVC_MINOR},
    echo,
    post_events,
    post_twice,
    post_again,
    take_struct,
    stats,
};

const LumaTestSecretService kSecret = {
    {sizeof(LumaTestSecretService), LUMA_TEST_SECRET_MAJOR, LUMA_TEST_SECRET_MINOR},
    ping,
};

// One provider has the moments for both services: they are one service's two halves for the tests.
const LumaServiceProvider kSvcProvider = {
    sizeof(LumaServiceProvider), "test.svc", &kSvc.base, nullptr, nullptr, 0u, nullptr,
    mod_begin, mod_gone, mod_end, registered,
};

const LumaServiceProvider kSecretProvider = {
    sizeof(LumaServiceProvider), "test.secret", &kSecret.base, "test.secret", nullptr, 0u, nullptr,
    nullptr, nullptr, nullptr, nullptr,
};

const LumaServiceProvider* const kProviders[] = {&kSvcProvider, &kSecretProvider};

}  // namespace

const LumaServiceProvider* const* providers(std::uint32_t* count) {
    *count = static_cast<std::uint32_t>(sizeof(kProviders) / sizeof(kProviders[0]));
    return kProviders;
}

}  // namespace test_services
