#include "LegionGoFrameLimiter.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <cstdint>
#include <string>

namespace LegionGoFrameLimiter {
namespace {

using Result = int;
using Uint = std::uint32_t;
using Uint64 = std::uint64_t;
using Long = long;
using Bool = bool;
constexpr Result kOk = 0;

struct Gpu;
struct GpuList;
struct System;
struct Settings3D;
struct Chill;
struct Frtc;
struct IntRange { int minimum; int maximum; int step; };

struct GpuVtable { Long (__stdcall* acquire)(Gpu*); Long (__stdcall* release)(Gpu*); };
struct Gpu { const GpuVtable* vtable; };
struct GpuListVtable {
    Long (__stdcall* acquire)(GpuList*); Long (__stdcall* release)(GpuList*); void* queryInterface;
    Uint (__stdcall* size)(GpuList*); void* empty; void* begin; void* end; void* at; void* clear;
    void* removeBack; void* addBack; Result (__stdcall* atGpu)(GpuList*, Uint, Gpu**);
};
struct GpuList { const GpuListVtable* vtable; };
struct ChillVtable {
    Long (__stdcall* acquire)(Chill*); Long (__stdcall* release)(Chill*); void* queryInterface;
    Result (__stdcall* isSupported)(Chill*, Bool*); Result (__stdcall* isEnabled)(Chill*, Bool*);
    Result (__stdcall* getRange)(Chill*, IntRange*); Result (__stdcall* getMinFps)(Chill*, int*);
    Result (__stdcall* getMaxFps)(Chill*, int*); Result (__stdcall* setEnabled)(Chill*, Bool);
    Result (__stdcall* setMinFps)(Chill*, int); Result (__stdcall* setMaxFps)(Chill*, int);
};
struct Chill { const ChillVtable* vtable; };
struct FrtcVtable {
    Long (__stdcall* acquire)(Frtc*); Long (__stdcall* release)(Frtc*); void* queryInterface;
    Result (__stdcall* isSupported)(Frtc*, Bool*); Result (__stdcall* isEnabled)(Frtc*, Bool*);
    Result (__stdcall* getRange)(Frtc*, IntRange*); Result (__stdcall* getFps)(Frtc*, int*);
    Result (__stdcall* setEnabled)(Frtc*, Bool); Result (__stdcall* setFps)(Frtc*, int);
};
struct Frtc { const FrtcVtable* vtable; };
struct Settings3DVtable {
    Long (__stdcall* acquire)(Settings3D*); Long (__stdcall* release)(Settings3D*); void* queryInterface;
    void* getAntiLag; Result (__stdcall* getChill)(Settings3D*, Gpu*, Chill**); void* getBoost;
    void* getImageSharpening; void* getEnhancedSync; void* getWaitForVerticalRefresh;
    Result (__stdcall* getFrtc)(Settings3D*, Gpu*, Frtc**);
};
struct Settings3D { const Settings3DVtable* vtable; };
struct SystemVtable {
    void* getHybridGraphicsType; Result (__stdcall* getGpus)(System*, GpuList**); void* queryInterface;
    void* getDisplayServices; void* getDesktopServices; void* getGpusChangedHandling; void* enableLog;
    Result (__stdcall* get3DSettings)(System*, Settings3D**);
};
struct System { const SystemVtable* vtable; };

using QueryFullVersion = Result (__cdecl*)(Uint64*);
using Initialize2 = Result (__cdecl*)(Uint64, System**, void**);
using Initialize1 = Result (__cdecl*)(Uint64, System**);
using Terminate = Result (__cdecl*)();

template <typename T> void Release(T*& value) {
    if (value != nullptr && value->vtable != nullptr && value->vtable->release != nullptr) value->vtable->release(value);
    value = nullptr;
}

class Session {
public:
    ~Session() { Close(); }
    bool Open(State& state, bool requireChill = true) {
        module_ = LoadLibraryExW(L"amdadlx64.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
        if (!module_) { state.error = L"AMD ADLX driver runtime is unavailable"; return false; }
        const auto queryVersion = reinterpret_cast<QueryFullVersion>(GetProcAddress(module_, "ADLXQueryFullVersion"));
        terminate_ = reinterpret_cast<Terminate>(GetProcAddress(module_, "ADLXTerminate"));
        if (!queryVersion || !terminate_) { state.error = L"AMD ADLX exports are incomplete"; return false; }
        Uint64 version = 0;
        if (queryVersion(&version) != kOk || version == 0) { state.error = L"AMD ADLX version query failed"; return false; }
        Result result = -1;
        const auto initialize2 = reinterpret_cast<Initialize2>(GetProcAddress(module_, "ADLXInitialize2"));
        if (initialize2) { void* mapping = nullptr; result = initialize2(version, &system_, &mapping); }
        else {
            const auto initialize1 = reinterpret_cast<Initialize1>(GetProcAddress(module_, "ADLXInitialize"));
            if (initialize1) result = initialize1(version, &system_);
        }
        if (result != kOk || !system_ || !system_->vtable) {
            state.error = L"AMD ADLX initialization failed (" + std::to_wstring(result) + L")"; return false;
        }
        initialized_ = true; state.available = true;
        if (system_->vtable->getGpus(system_, &gpus_) != kOk || !gpus_ || !gpus_->vtable ||
            gpus_->vtable->size(gpus_) == 0 || gpus_->vtable->atGpu(gpus_, 0, &gpu_) != kOk || !gpu_) {
            state.error = L"AMD GPU enumeration failed"; return false;
        }
        if (system_->vtable->get3DSettings(system_, &settings_) != kOk || !settings_ || !settings_->vtable) {
            state.error = L"AMD 3D settings interface is unavailable"; return false;
        }
        if (requireChill && (settings_->vtable->getChill(settings_, gpu_, &chill_) != kOk ||
                             !chill_ || !chill_->vtable)) {
            state.error = L"AMD Radeon Chill interface is unavailable"; return false;
        }
        return true;
    }
    Chill* Interface() const { return chill_; }
    Frtc* LegacyFrtc(State& state) {
        if (!settings_ || !settings_->vtable || settings_->vtable->getFrtc(settings_, gpu_, &frtc_) != kOk ||
            !frtc_ || !frtc_->vtable) state.error = L"Legacy AMD FRTC interface is unavailable";
        return frtc_;
    }
private:
    void Close() {
        Release(frtc_); Release(chill_); Release(settings_); Release(gpu_); Release(gpus_);
        if (initialized_ && terminate_) terminate_();
        initialized_ = false; system_ = nullptr; terminate_ = nullptr;
        if (module_) FreeLibrary(module_); module_ = nullptr;
    }
    HMODULE module_ = nullptr; Terminate terminate_ = nullptr; bool initialized_ = false;
    System* system_ = nullptr; GpuList* gpus_ = nullptr; Gpu* gpu_ = nullptr;
    Settings3D* settings_ = nullptr; Chill* chill_ = nullptr; Frtc* frtc_ = nullptr;
};

bool ReadState(Chill* chill, State& state) {
    Bool supported = false, enabled = false; IntRange range{}; int minimumFps = 0, maximumFps = 0;
    if (chill->vtable->isSupported(chill, &supported) != kOk) {
        state.error = L"AMD Radeon Chill support query failed"; return false;
    }
    state.supported = supported;
    if (!supported) { state.error = L"AMD Radeon Chill is not supported by this GPU"; return false; }
    if (chill->vtable->isEnabled(chill, &enabled) != kOk || chill->vtable->getRange(chill, &range) != kOk ||
        chill->vtable->getMinFps(chill, &minimumFps) != kOk || chill->vtable->getMaxFps(chill, &maximumFps) != kOk) {
        state.error = L"AMD Radeon Chill state query failed"; return false;
    }
    state.enabled = enabled; state.minFps = minimumFps; state.maxFps = maximumFps; state.fps = maximumFps;
    state.minimum = range.minimum; state.maximum = range.maximum; return true;
}

bool ProgramRange(Chill* chill, int desiredMin, int desiredMax) {
    State current;
    if (!ReadState(chill, current)) return false;
    Result result = kOk;
    // Expand the current interval before contracting it, so every intermediate
    // state respects Chill's MinFPS <= MaxFPS rule.
    if (desiredMax > current.maxFps) result = chill->vtable->setMaxFps(chill, desiredMax);
    if (result == kOk && desiredMin < current.minFps) result = chill->vtable->setMinFps(chill, desiredMin);
    if (result == kOk) result = chill->vtable->setMinFps(chill, desiredMin);
    if (result == kOk) result = chill->vtable->setMaxFps(chill, desiredMax);
    return result == kOk;
}

} // namespace

State Query() {
    State state; Session session;
    if (session.Open(state)) ReadState(session.Interface(), state);
    return state;
}

bool SetState(bool enabled, int minFps, int maxFps, State& verified) {
    verified = {}; Session session;
    if (!session.Open(verified) || !ReadState(session.Interface(), verified)) return false;
    Chill* chill = session.Interface();
    const bool oldEnabled = verified.enabled; const int oldMin = verified.minFps; const int oldMax = verified.maxFps;
    if (minFps < verified.minimum || maxFps > verified.maximum || minFps > maxFps) {
        verified.error = L"Requested FPS is outside the AMD Radeon Chill range"; return false;
    }
    const auto restoreOriginal = [&]() {
        if (chill->vtable->setEnabled(chill, true) != kOk || !ProgramRange(chill, oldMin, oldMax)) return false;
        if (!oldEnabled && chill->vtable->setEnabled(chill, false) != kOk) return false;
        State restored;
        return ReadState(chill, restored) && restored.enabled == oldEnabled &&
               restored.minFps == oldMin && restored.maxFps == oldMax;
    };
    Result result = chill->vtable->setEnabled(chill, true);
    if (result == kOk && !ProgramRange(chill, minFps, maxFps)) result = -1;
    if (result == kOk && !enabled) result = chill->vtable->setEnabled(chill, false);
    if (result != kOk) {
        const bool restored = restoreOriginal();
        verified.error = L"AMD Radeon Chill write failed" +
            std::wstring(restored ? L"; previous state restored" : L"; WARNING: previous-state restore was not verified");
        return false;
    }
    State readback; readback.available = true;
    if (!ReadState(chill, readback) || readback.enabled != enabled ||
        readback.minFps != minFps || readback.maxFps != maxFps) {
        const bool restored = restoreOriginal(); verified = readback;
        verified.error = restored ? L"AMD Radeon Chill read-back mismatch; previous state restored" :
                                    L"AMD Radeon Chill read-back mismatch; WARNING: previous-state restore was not verified";
        return false;
    }
    verified = readback; return true;
}

bool Set(bool enabled, int fps, State& verified) { return SetState(enabled, fps, fps, verified); }

bool RestoreLegacyFrtc(bool enabled, int fps, std::wstring& error) {
    State sessionState; Session session;
    if (!session.Open(sessionState, false)) { error = sessionState.error; return false; }
    Frtc* frtc = session.LegacyFrtc(sessionState);
    if (!frtc) { error = sessionState.error; return false; }
    Bool supported = false; IntRange range{};
    if (frtc->vtable->isSupported(frtc, &supported) != kOk || !supported ||
        frtc->vtable->getRange(frtc, &range) != kOk || fps < range.minimum || fps > range.maximum) {
        error = L"Legacy AMD FRTC backup cannot be restored"; return false;
    }
    Bool previousEnabled = false; int previousFps = 0;
    if (frtc->vtable->isEnabled(frtc, &previousEnabled) != kOk || frtc->vtable->getFps(frtc, &previousFps) != kOk) {
        error = L"Legacy AMD FRTC pre-restore query failed"; return false;
    }
    const auto program = [frtc](bool targetEnabled, int targetFps) {
        Result value = frtc->vtable->setEnabled(frtc, true);
        if (value == kOk) value = frtc->vtable->setFps(frtc, targetFps);
        if (value == kOk && !targetEnabled) value = frtc->vtable->setEnabled(frtc, false);
        return value;
    };
    Result result = program(enabled, fps);
    Bool actualEnabled = false; int actualFps = 0;
    if (result != kOk || frtc->vtable->isEnabled(frtc, &actualEnabled) != kOk ||
        frtc->vtable->getFps(frtc, &actualFps) != kOk || actualEnabled != enabled || actualFps != fps) {
        Bool rollbackEnabled = false; int rollbackFps = 0;
        const bool rollbackOk = program(previousEnabled, previousFps) == kOk &&
            frtc->vtable->isEnabled(frtc, &rollbackEnabled) == kOk &&
            frtc->vtable->getFps(frtc, &rollbackFps) == kOk &&
            rollbackEnabled == previousEnabled && rollbackFps == previousFps;
        error = rollbackOk ? L"Legacy AMD FRTC restore failed; previous state restored" :
                             L"Legacy AMD FRTC restore failed; WARNING: rollback was not verified";
        return false;
    }
    error.clear(); return true;
}

} // namespace LegionGoFrameLimiter
