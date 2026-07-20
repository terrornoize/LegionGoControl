#pragma once

#include <windows.h>

#include <vector>

namespace LegionGoPresentTrace {

struct Frame {
    DWORD processId = 0;
    double milliseconds = 0.0;
};

// Lightweight real-time ETW collector for D3D9 and DXGI Present_Start events.
// Start/Stop are called by the overlay metrics thread; Drain is non-blocking.
class Collector {
public:
    Collector();
    ~Collector();
    Collector(const Collector&) = delete;
    Collector& operator=(const Collector&) = delete;

    bool Start();
    void Stop();
    bool Running() const;
    std::vector<Frame> Drain();

private:
    struct Impl;
    Impl* impl_ = nullptr;
};

} // namespace LegionGoPresentTrace
