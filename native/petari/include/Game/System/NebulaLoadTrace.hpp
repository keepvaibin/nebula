#pragma once

// The times of the load measurements (GALAXY_TRACE_LOADS).
namespace NebulaLoadTrace {
    // Development measurements of scene construction, without changing its ordering.
    class Scope {
    public:
#if defined(NEBULA_DEV)
        explicit Scope(const char* name);
        ~Scope();
    private:
        const char* mName;
        unsigned long long mStart;
#else
        explicit Scope(const char*) {}
#endif
    };
    template<class F> void measure(const char* name, F&& work) { Scope span(name); work(); }
    void frame();
    void gameScene();
    // Creation order of the placed objects and the game's random state after them (the N-63 order proof).
    void placed(const char* pName, int zoneId);
    void placementDone();
}  // namespace NebulaLoadTrace
