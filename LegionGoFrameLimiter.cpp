#include "LegionGoFrameLimiter.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <algorithm>
#include <cstdint>
#include <string>

namespace LegionGoFrameLimiter {
namespace {

// Minimal ABI declarations for the documented AMD ADLX interfaces used here.
// Keeping these declarations local avoids redistributing AMD SDK source/header
// material; the implementation dynamically uses the driver-provided DLL.
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
struct Frtc;

struct IntRange { int minimum; int maximum; int step; };

struct GpuVtable {
    Long (__stdcall* acquire)(Gpu*);
    Long (__stdcall* release)(Gpu*);
};
struct Gpu { const GpuVtable* vtable; };

struct GpuListVtable {
    Long (__stdcall* acquire)(GpuList*);
    Long (__stdcall* release)(GpuList*);
    void* queryInterface;
    Uint (__stdcall* size)(GpuList*);
    void* empty;
    void* begin;
    void* end;
    void* at;
    void* clear;
    void* removeBack;
    void* addBack;
    Result (__stdcall* atGpu)(GpuList*, Uint, Gpu**);
};
struct GpuList { const GpuListVtable* vtable; };

struct FrtcVtable {
    Long (__stdcall* acquire)(Frtc*);
    Long (__stdcall* release)(Frtc*);
    void* queryInterface;
    Result (__stdcall* isSupported)(Frtc*, Bool*);
    Result (__stdcall* isEnabled)(Frtc*, Bool*);
    Result (__stdcall* getRange)(Frtc*, IntRange*);
    Result (__stdcall* getFps)(Frtc*, int*);
    Result (__stdcall* setEnabled)(Frtc*, Bool);
    Result (__stdcall* setFps)(Frtc*, int);
};
struct Frtc { const FrtcVtable* vtable; };

struct Settings3DVtable {
    Long (__stdcall* acquire)(Settings3D*);
    Long (__stdcall* release)(Settings3D*);
    void* queryInterface;
    void* getAntiLag;
    void* getChill;
    void* getBoost;
    void* getImageSharpening;
    void* getEnhancedSync;
    void* getWaitForVerticalRefresh;
    Result (__stdcall* getFrtc)(Settings3D*, Gpu*, Frtc**);
};
struct Settings3D { const Settings3DVtable* vtable; };

struct SystemVtable {
    void* getHybridGraphicsType;
    Result (__stdcall* getGpus)(System*, GpuList**);
    void* queryInterface;
    void* getDisplayServices;
    void* getDesktopServices;
    void* getGpusChangedHandling;
    void* enableLog;
    Result (__stdcall* get3DSettings)(System*, Settings3D**);
};
struct System { const SystemVtable* vtable; };

using QueryFullVersion = Result (__cdecl*)(Uint64*);
using Initialize2 = Result (__cdecl*)(Uint64, System**, void**);
using Initialize1 = Result (__cdecl*)(Uint64, System**);
using Terminate = Result (__cdecl*)();

template <typename T>
void Release(T*& value) {
    if (value != nullptr && value->vtable != nullptr && value->vtable->release != nullptr)
        value->vtable->release(value);
    value = nullptr;
}

class Session {
public:
    Session() = default;
    Session(const Session&) = delete;
    Session& operator=(const Session&) = delete;
    ~Session() { Close(); }

    bool Open(State& state) {
        module_ = LoadLibraryExW(L"amdadlx64.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
        if (module_ == nullptr) { state.error = L"AMD ADLX driver runtime is unavailable"; return false; }
        const auto queryVersion = reinterpret_cast<QueryFullVersion>(GetProcAddress(module_, "ADLXQueryFullVersion"));
        terminate_ = reinterpret_cast<Terminate>(GetProcAddress(module_, "ADLXTerminate"));
        if (queryVersion == nullptr || terminate_ == nullptr) { state.error = L"AMD ADLX exports are incomplete"; return false; }
        Uint64 version = 0;
        if (queryVersion(&version) != kOk || version == 0) { state.error = L"AMD ADLX version query failed"; return false; }
        const auto initialize2 = reinterpret_cast<Initialize2>(GetProcAddress(module_, "ADLXInitialize2"));
        Result result = -1;
        if (initialize2 != nullptr) {
            void* mapping = nullptr;
            result = initialize2(version, &system_, &mapping);
        } else {
            const auto initialize1 = reinterpret_cast<Initialize1>(GetProcAddress(module_, "ADLXInitialize"));
            if (initialize1 != nullptr) result = initialize1(version, &system_);
        }
        if (result != kOk || system_ == nullptr || system_->vtable == nullptr) {
            state.error = L"AMD ADLX initialization failed (" + std::to_wstring(result) + L")"; return false;
        }
        initialized_ = true;
        state.available = true;

        if (system_->vtable->getGpus(system_, &gpus_) != kOk || gpus_ == nullptr ||
            gpus_->vtable == nullptr || gpus_->vtable->size(gpus_) == 0 ||
            gpus_->vtable->atGpu(gpus_, 0, &gpu_) != kOk || gpu_ == nullptr) {
            state.error = L"AMD GPU enumeration failed"; return false;
        }
        if (system_->vtable->get3DSettings(system_, &settings_) != kOk || settings_ == nullptr ||
            settings_->vtable == nullptr || settings_->vtable->getFrtc(settings_, gpu_, &frtc_) != kOk ||
            frtc_ == nullptr || frtc_->vtable == nullptr) {
            state.error = L"AMD FRTC interface is unavailable"; return false;
        }
        return true;
    }

    Frtc* Interface() const { return frtc_; }

private:
    void Close() {
        Release(frtc_); Release(settings_); Release(gpu_); Release(gpus_);
        if (initialized_ && terminate_ != nullptr) terminate_();
        initialized_ = false; system_ = nullptr; terminate_ = nullptr;
        if (module_ != nullptr) FreeLibrary(module_);
        module_ = nullptr;
    }

    HMODULE module_ = nullptr;
    Terminate terminate_ = nullptr;
    bool initialized_ = false;
    System* system_ = nullptr;
    GpuList* gpus_ = nullptr;
    Gpu* gpu_ = nullptr;
    Settings3D* settings_ = nullptr;
    Frtc* frtc_ = nullptr;
};

bool ReadState(Frtc* frtc, State& state) {
    Bool supported = false, enabled = false;
    IntRange range{}; int fps = 0;
    if (frtc->vtable->isSupported(frtc, &supported) != kOk) {
        state.error = L"AMD FRTC support query failed"; return false;
    }
    state.supported = supported;
    if (!supported) { state.error = L"AMD FRTC is not supported by this GPU"; return false; }
    if (frtc->vtable->isEnabled(frtc, &enabled) != kOk ||
        frtc->vtable->getRange(frtc, &range) != kOk ||
        frtc->vtable->getFps(frtc, &fps) != kOk) {
        state.error = L"AMD FRTC state query failed"; return false;
    }
    state.enabled = enabled; state.fps = fps;
    state.minimum = range.minimum; state.maximum = range.maximum;
    return true;
}

} // namespace

State Query() {
    State state; Session session;
    if (!session.Open(state)) return state;
    ReadState(session.Interface(), state);
    return state;
}

bool Set(bool enabled, int fps, State& verified) {
    verified = {}; Session session;
    if (!session.Open(verified) || !ReadState(session.Interface(), verified)) return false;
    Frtc* frtc = session.Interface();
    const bool oldEnabled = verified.enabled; const int oldFps = verified.fps;
    Result result = kOk;
    if (enabled) {
        if (fps < verified.minimum || fps > verified.maximum) {
            verified.error = L"Requested FPS is outside the AMD FRTC range"; return false;
        }
        result = frtc->vtable->setEnabled(frtc, true);
        if (result == kOk) result = frtc->vtable->setFps(frtc, fps);
    } else {
        // ADLX rejects SetFPS while disabled. Program the remembered value via
        // a short enable/set/disable sequence so restore is exact.
        if (fps >= verified.minimum && fps <= verified.maximum) {
            result = frtc->vtable->setEnabled(frtc, true);
            if (result == kOk) result = frtc->vtable->setFps(frtc, fps);
            if (result == kOk) result = frtc->vtable->setEnabled(frtc, false);
        } else result = frtc->vtable->setEnabled(frtc, false);
    }
    const auto rollback = [&]() {
        Result rollbackResult = frtc->vtable->setEnabled(frtc, true);
        if (rollbackResult == kOk) rollbackResult = frtc->vtable->setFps(frtc, oldFps);
        if (rollbackResult == kOk && !oldEnabled) rollbackResult = frtc->vtable->setEnabled(frtc, false);
        State rollbackReadback;
        return rollbackResult == kOk && ReadState(frtc, rollbackReadback) &&
               rollbackReadback.enabled == oldEnabled && rollbackReadback.fps == oldFps;
    };
    if (result != kOk) {
        const bool rollbackOk = rollback();
        verified.error = L"AMD FRTC write failed (" + std::to_wstring(result) + L")" +
                         (rollbackOk ? L"; previous state restored" : L"; WARNING: previous-state restore was not verified");
        return false;
    }
    State readback; readback.available = true;
    if (!ReadState(frtc, readback) || readback.enabled != enabled || (fps >= readback.minimum && fps <= readback.maximum && readback.fps != fps)) {
        const bool rollbackOk = rollback();
        verified = readback;
        verified.error = rollbackOk ? L"AMD FRTC read-back mismatch; previous state restored" :
                                      L"AMD FRTC read-back mismatch; WARNING: previous-state restore was not verified";
        return false;
    }
    verified = readback; return true;
}

} // namespace LegionGoFrameLimiter
