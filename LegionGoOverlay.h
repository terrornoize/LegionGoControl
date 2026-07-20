#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <string>

namespace LegionGoOverlay {

// corner: 0 = top-left, 1 = top-right, 2 = bottom-left, 3 = bottom-right.
struct Config {
    bool enabledAtStartup = false;
    int functionKey = 10;
    int scalePercent = 100;
    int opacityPercent = 85;
    int corner = 1;
    int marginX = 20;
    int marginY = 20;
};

// Initialize and Shutdown must be called on the same window-message thread.
bool Initialize(HINSTANCE instance, const std::wstring& baseDir, const Config& config);
void ApplyConfig(const Config& config);
void Toggle();
void SetVisible(bool visible);
bool IsVisible();
// Returns 0 when the configured global function key is already owned by another app.
int ActiveFunctionKey();
void SetFirmwareTelemetry(int tempC, int rpm, bool known);
void Shutdown();

} // namespace LegionGoOverlay
