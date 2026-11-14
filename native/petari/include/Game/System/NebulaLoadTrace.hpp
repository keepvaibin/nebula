#pragma once

// The times of the load measurements (--diagnostics scene-loads).
namespace NebulaLoadTrace {
    // Measurements of scene construction, without changing its ordering.
    class Scope {
    public:
        explicit Scope(const char* name);
        ~Scope();
    private:
        const char* mName;
        unsigned long long mStart;
    };
    template<class F> void measure(const char* name, F&& work) { Scope span(name); work(); }
    void frame();
    void gameScene();
    // Creation order of the placed objects and the game's random state after them (the order proof).
    void sorted(const char* pName, int priority, int count);
    void placed(const char* pName, int zoneId);
    void placementDone();
}  // namespace NebulaLoadTrace
