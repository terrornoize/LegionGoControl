#pragma once

#include <string>

namespace LegionGoFrameLimiter {

struct State {
    bool available = false;
    bool supported = false;
    bool enabled = false;
    int fps = 0; // Compatibility alias for maxFps.
    int minFps = 0;
    int maxFps = 0;
    int minimum = 0;
    int maximum = 0;
    std::wstring error;
};

// Uses the documented C ABI exported by the AMD display driver's
// amdadlx64.dll. No AMD SDK runtime is redistributed by LegionGoControl.
State Query();
bool Set(bool enabled, int fps, State& verified);
bool SetState(bool enabled, int minFps, int maxFps, State& verified);
// Upgrade recovery only: restores backups written by the previous FRTC backend.
bool RestoreLegacyFrtc(bool enabled, int fps, std::wstring& error);

} // namespace LegionGoFrameLimiter
