#include "LegionGoOverlay.h"
#include "LegionGoPresentTrace.h"

#include <dxgi1_4.h>
#include <pdh.h>
#include <pdhmsg.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cwchar>
#include <deque>
#include <iomanip>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "pdh.lib")
#pragma comment(lib, "user32.lib")

namespace LegionGoOverlay {
namespace {

constexpr wchar_t kWindowClass[] = L"LegionGoControl.TelemetryOverlay";
constexpr int kHotKeyIdA = 0x4C47;
constexpr int kHotKeyIdB = 0x4C48;
constexpr UINT kMessageRefresh = WM_APP + 0x471U;
constexpr UINT kMessageVisibility = WM_APP + 0x472U;
constexpr UINT kMessageConfig = WM_APP + 0x473U;
constexpr UINT kMessageDestroy = WM_APP + 0x474U;
constexpr DWORD kMetricIntervalMs = 1000U;
constexpr double kBytesPerGiB = 1024.0 * 1024.0 * 1024.0;

#ifndef MOD_NOREPEAT
constexpr UINT MOD_NOREPEAT = 0x4000U;
#endif

using SteadyClock = std::chrono::steady_clock;

struct NumericMetric {
    double value = 0.0;
    bool known = false;
};

struct Snapshot {
    NumericMetric fps;
    NumericMetric frameTimeMs;
    NumericMetric cpuPercent;
    NumericMetric gpuPercent;
    NumericMetric vramUsedBytes;
    NumericMetric vramCapacityBytes;
    NumericMetric vramBudgetBytes;
    NumericMetric ramUsedBytes;
    NumericMetric ramTotalBytes;
    NumericMetric batteryPercent;
    NumericMetric powerWatts;
    int temperatureC = 0;
    int fanRpm = 0;
    bool firmwareKnown = false;
    SYSTEMTIME localTime{};
    bool timeKnown = false;
    std::vector<double> frameHistory;
};

struct FrameSample {
    double milliseconds = 0.0;
    SteadyClock::time_point received{};
};

Config SanitizeConfig(const Config& input) {
    Config result = input;
    result.functionKey = (std::max)(1, (std::min)(24, result.functionKey));
    result.scalePercent = (std::max)(50, (std::min)(200, result.scalePercent));
    result.opacityPercent = (std::max)(0, (std::min)(100, result.opacityPercent));
    result.corner = (std::max)(0, (std::min)(3, result.corner));
    result.layoutStyle = (std::max)(0, (std::min)(1, result.layoutStyle));
    result.marginX = (std::max)(0, (std::min)(10000, result.marginX));
    result.marginY = (std::max)(0, (std::min)(10000, result.marginY));
    return result;
}

class PdhMetrics {
public:
    PdhMetrics() = default;
    PdhMetrics(const PdhMetrics&) = delete;
    PdhMetrics& operator=(const PdhMetrics&) = delete;

    ~PdhMetrics() {
        Close();
    }

    bool Open() {
        Close();
        if (PdhOpenQueryW(nullptr, 0U, &query_) != ERROR_SUCCESS) {
            query_ = nullptr;
            return false;
        }
        AddCounter(L"\\GPU Engine(*)\\Utilization Percentage", gpuCounter_);
        AddCounter(L"\\GPU Adapter Memory(*)\\Dedicated Usage", memoryCounter_);
        AddCounter(L"\\Energy Meter(*)\\Power", powerCounter_);
        if (gpuCounter_ == nullptr && memoryCounter_ == nullptr && powerCounter_ == nullptr) {
            Close();
            return false;
        }
        static_cast<void>(PdhCollectQueryData(query_));
        primed_ = true;
        return true;
    }

    void Close() {
        if (query_ != nullptr) {
            PdhCloseQuery(query_);
        }
        query_ = nullptr;
        gpuCounter_ = nullptr;
        memoryCounter_ = nullptr;
        powerCounter_ = nullptr;
        primed_ = false;
    }

    void Collect(NumericMetric& gpu, NumericMetric& memoryBytes, NumericMetric& powerWatts) {
        gpu = {};
        memoryBytes = {};
        powerWatts = {};
        if (query_ == nullptr) {
            return;
        }
        const PDH_STATUS collectStatus = PdhCollectQueryData(query_);
        if (collectStatus != ERROR_SUCCESS || !primed_) {
            primed_ = true;
            return;
        }

        if (gpuCounter_ != nullptr) {
            double maximum = 0.0;
            if (ReadDoubleArray(gpuCounter_, false, maximum, false, true)) {
                gpu.value = (std::max)(0.0, (std::min)(100.0, maximum));
                gpu.known = true;
            }
        }
        if (memoryCounter_ != nullptr) {
            double sum = 0.0;
            if (ReadDoubleArray(memoryCounter_, true, sum, false, false)) {
                memoryBytes.value = (std::max)(0.0, sum);
                memoryBytes.known = true;
            }
        }
        if (powerCounter_ != nullptr) {
            double milliwatts = 0.0;
            if (ReadDoubleArray(powerCounter_, false, milliwatts, true, false)) {
                powerWatts.value = milliwatts / 1000.0;
                powerWatts.known = true;
            }
        }
    }

private:
    void AddCounter(const wchar_t* path, PDH_HCOUNTER& counter) {
        counter = nullptr;
        if (PdhAddEnglishCounterW(query_, path, 0U, &counter) != ERROR_SUCCESS) {
            counter = nullptr;
        }
    }

    static bool IsValidStatus(DWORD status) {
        return status == PDH_CSTATUS_VALID_DATA || status == PDH_CSTATUS_NEW_DATA;
    }

    static bool ReadDoubleArray(PDH_HCOUNTER counter, bool sumValues, double& result,
                                bool raplPackageOnly, bool aggregateGpuEngines) {
        DWORD bufferSize = 0U;
        DWORD itemCount = 0U;
        const DWORD format = PDH_FMT_DOUBLE | PDH_FMT_NOCAP100;
        PDH_STATUS status = PdhGetFormattedCounterArrayW(counter, format, &bufferSize, &itemCount, nullptr);
        if (status != PDH_MORE_DATA || bufferSize == 0U) {
            return false;
        }

        std::vector<unsigned char> buffer;
        PDH_FMT_COUNTERVALUE_ITEM_W* items = nullptr;
        for (int attempt = 0; attempt < 3; ++attempt) {
            buffer.resize(static_cast<std::size_t>(bufferSize));
            items = reinterpret_cast<PDH_FMT_COUNTERVALUE_ITEM_W*>(buffer.data());
            status = PdhGetFormattedCounterArrayW(counter, format, &bufferSize, &itemCount, items);
            if (status != PDH_MORE_DATA) {
                break;
            }
        }
        if (status != ERROR_SUCCESS || items == nullptr) {
            return false;
        }

        bool found = false;
        double accumulated = 0.0;
        std::map<std::wstring, double> engineTotals;
        for (DWORD index = 0U; index < itemCount; ++index) {
            const wchar_t* instance = items[index].szName;
            if (raplPackageOnly && (instance == nullptr ||
                _wcsnicmp(instance, L"RAPL_Package0_PKG", 17U) != 0)) {
                continue;
            }
            const double value = items[index].FmtValue.doubleValue;
            if (IsValidStatus(items[index].FmtValue.CStatus) && std::isfinite(value) && value >= 0.0) {
                if (aggregateGpuEngines && instance != nullptr) {
                    const std::wstring name(instance); const std::size_t adapter = name.find(L"_luid_");
                    const std::wstring engine = adapter == std::wstring::npos ? name : name.substr(adapter);
                    engineTotals[engine] += value;
                } else {
                    accumulated = found ? (sumValues ? accumulated + value : (std::max)(accumulated, value)) : value;
                }
                found = true;
            }
        }
        if (found) {
            if (aggregateGpuEngines) {
                accumulated = 0.0;
                for (const auto& engine : engineTotals) accumulated = (std::max)(accumulated, engine.second);
            }
            result = accumulated;
        }
        return found;
    }

    PDH_HQUERY query_ = nullptr;
    PDH_HCOUNTER gpuCounter_ = nullptr;
    PDH_HCOUNTER memoryCounter_ = nullptr;
    PDH_HCOUNTER powerCounter_ = nullptr;
    bool primed_ = false;
};

class DxgiMetrics {
public:
    DxgiMetrics() = default;
    DxgiMetrics(const DxgiMetrics&) = delete;
    DxgiMetrics& operator=(const DxgiMetrics&) = delete;

    ~DxgiMetrics() {
        Close();
    }

    void Open() {
        Close();
        IDXGIFactory1* factory = nullptr;
        if (FAILED(CreateDXGIFactory1(__uuidof(IDXGIFactory1), reinterpret_cast<void**>(&factory)))) {
            return;
        }

        for (UINT index = 0U;; ++index) {
            IDXGIAdapter1* adapter = nullptr;
            const HRESULT enumeration = factory->EnumAdapters1(index, &adapter);
            if (enumeration == DXGI_ERROR_NOT_FOUND) {
                break;
            }
            if (FAILED(enumeration) || adapter == nullptr) {
                continue;
            }

            DXGI_ADAPTER_DESC1 description{};
            if (SUCCEEDED(adapter->GetDesc1(&description)) &&
                (description.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) == 0U) {
                dedicatedTotalBytes_ += static_cast<double>(description.DedicatedVideoMemory);
                IDXGIAdapter3* adapter3 = nullptr;
                if (SUCCEEDED(adapter->QueryInterface(__uuidof(IDXGIAdapter3),
                                                      reinterpret_cast<void**>(&adapter3))) && adapter3 != nullptr) {
                    adapters_.push_back(adapter3);
                }
            }
            adapter->Release();
        }
        factory->Release();
    }

    void ReadCapacity(NumericMetric& dedicatedCapacity, NumericMetric& localBudget) const {
        dedicatedCapacity = {};
        localBudget = {};
        if (dedicatedTotalBytes_ > 0.0) {
            dedicatedCapacity.value = dedicatedTotalBytes_;
            dedicatedCapacity.known = true;
        }

        double budgetTotal = 0.0;
        bool foundBudget = false;
        for (IDXGIAdapter3* adapter : adapters_) {
            DXGI_QUERY_VIDEO_MEMORY_INFO information{};
            if (SUCCEEDED(adapter->QueryVideoMemoryInfo(0U, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, &information)) &&
                information.Budget > 0U) {
                budgetTotal += static_cast<double>(information.Budget);
                foundBudget = true;
            }
        }
        if (foundBudget && budgetTotal > 0.0) {
            localBudget.value = budgetTotal;
            localBudget.known = true;
        }
    }

    void Close() {
        for (IDXGIAdapter3* adapter : adapters_) {
            adapter->Release();
        }
        adapters_.clear();
        dedicatedTotalBytes_ = 0.0;
    }

private:
    std::vector<IDXGIAdapter3*> adapters_;
    double dedicatedTotalBytes_ = 0.0;
};

class OverlayModule {
public:
    static OverlayModule& Instance() {
        static OverlayModule instance;
        return instance;
    }

    bool InitializeModule(HINSTANCE instance, const std::wstring& baseDirectory, const Config& inputConfig) {
        std::lock_guard<std::mutex> lifecycleLock(lifecycleMutex_);
        if (initialized_.load()) {
            return true;
        }

        instance_ = instance;
        baseDirectory_ = baseDirectory;
        {
            std::lock_guard<std::mutex> configLock(configMutex_);
            config_ = SanitizeConfig(inputConfig);
        }

        stopEvent_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        wakeEvent_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        if (stopEvent_ == nullptr || wakeEvent_ == nullptr) {
            CleanupInitialization();
            return false;
        }

        WNDCLASSEXW windowClass{};
        windowClass.cbSize = sizeof(windowClass);
        windowClass.lpfnWndProc = &OverlayModule::WindowProcedure;
        windowClass.hInstance = instance_;
        windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        windowClass.lpszClassName = kWindowClass;
        classAtom_ = RegisterClassExW(&windowClass);
        if (classAtom_ == 0U && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
            CleanupInitialization();
            return false;
        }

        const DWORD extendedStyle = WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_NOACTIVATE |
                                    WS_EX_TOOLWINDOW | WS_EX_TOPMOST;
        window_ = CreateWindowExW(extendedStyle, kWindowClass, L"Legion Go Telemetry", WS_POPUP,
                                  0, 0, 1, 1, nullptr, nullptr, instance_, this);
        if (window_ == nullptr) {
            CleanupInitialization();
            return false;
        }

        Config initialConfig{};
        {
            std::lock_guard<std::mutex> configLock(configMutex_);
            initialConfig = config_;
        }
        activeHotKeyId_ = kHotKeyIdA;
        activeFunctionKey_ = initialConfig.functionKey;
        hotKeyRegistered_ = RegisterHotKey(window_, activeHotKeyId_, MOD_NOREPEAT,
            static_cast<UINT>(VK_F1 + activeFunctionKey_ - 1)) != FALSE;
        if (!hotKeyRegistered_) activeFunctionKey_ = 0;

        {
            std::lock_guard<std::mutex> snapshotLock(snapshotMutex_);
            snapshot_ = {};
        }
        visible_.store(initialConfig.enabledAtStartup);
        initialized_.store(true);
        ApplyWindowConfig();
        UpdateWindowVisibility();

        try {
            worker_ = std::thread(&OverlayModule::WorkerMain, this);
        } catch (...) {
            initialized_.store(false);
            CleanupInitialization();
            return false;
        }
        return true;
    }

    void ApplyModuleConfig(const Config& inputConfig) {
        std::lock_guard<std::mutex> lifecycleLock(lifecycleMutex_);
        if (!initialized_.load()) {
            return;
        }
        {
            std::lock_guard<std::mutex> lock(configMutex_);
            config_ = SanitizeConfig(inputConfig);
        }
        const HWND window = window_;
        if (window != nullptr) {
            if (GetWindowThreadProcessId(window, nullptr) == GetCurrentThreadId()) ApplyWindowConfig();
            else PostMessageW(window, kMessageConfig, 0U, 0);
        }
        WakeWorker();
    }

    void SetModuleVisible(bool visible) {
        std::lock_guard<std::mutex> lifecycleLock(lifecycleMutex_);
        if (!initialized_.load()) {
            return;
        }
        visible_.store(visible);
        const HWND window = window_;
        if (window != nullptr) {
            PostMessageW(window, kMessageVisibility, 0U, 0);
        }
        WakeWorker();
    }

    void ToggleModule() {
        std::lock_guard<std::mutex> lifecycleLock(lifecycleMutex_);
        if (!initialized_.load()) {
            return;
        }
        visible_.store(!visible_.load());
        if (window_ != nullptr) {
            PostMessageW(window_, kMessageVisibility, 0U, 0);
        }
        WakeWorker();
    }

    bool IsModuleVisible() const {
        std::lock_guard<std::mutex> lifecycleLock(lifecycleMutex_);
        return initialized_.load() && visible_.load();
    }

    int ActiveModuleFunctionKey() const {
        std::lock_guard<std::mutex> lifecycleLock(lifecycleMutex_);
        return initialized_.load() && hotKeyRegistered_ ? activeFunctionKey_ : 0;
    }

    void SetFirmware(int temperatureC, int rpm, bool known) {
        std::lock_guard<std::mutex> lifecycleLock(lifecycleMutex_);
        {
            std::lock_guard<std::mutex> lock(firmwareMutex_);
            firmwareTemperatureC_ = temperatureC;
            firmwareRpm_ = rpm;
            firmwareKnown_ = known;
        }
        if (initialized_.load()) {
            WakeWorker();
        }
    }

    void ShutdownModule() {
        std::lock_guard<std::mutex> lifecycleLock(lifecycleMutex_);
        if (!initialized_.exchange(false)) {
            return;
        }
        visible_.store(false);
        if (stopEvent_ != nullptr) {
            SetEvent(stopEvent_);
        }
        WakeWorker();

        if (worker_.joinable()) {
            worker_.join();
        }
        {
            std::lock_guard<std::mutex> snapshotLock(snapshotMutex_);
            snapshot_ = {};
        }

        if (window_ != nullptr) {
            const DWORD windowThread = GetWindowThreadProcessId(window_, nullptr);
            if (windowThread == GetCurrentThreadId()) {
                DestroyOverlayWindow();
            } else {
                SendMessageW(window_, kMessageDestroy, 0U, 0);
            }
        }
        CleanupInitialization();
    }

private:
    OverlayModule() = default;
    OverlayModule(const OverlayModule&) = delete;
    OverlayModule& operator=(const OverlayModule&) = delete;

    ~OverlayModule() {
        ShutdownModule();
    }

    static LRESULT CALLBACK WindowProcedure(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
        OverlayModule* self = reinterpret_cast<OverlayModule*>(GetWindowLongPtrW(window, GWLP_USERDATA));
        if (message == WM_NCCREATE) {
            const auto* create = reinterpret_cast<const CREATESTRUCTW*>(lParam);
            self = static_cast<OverlayModule*>(create->lpCreateParams);
            SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        }
        if (self == nullptr) {
            return DefWindowProcW(window, message, wParam, lParam);
        }
        return self->HandleWindowMessage(window, message, wParam, lParam);
    }

    LRESULT HandleWindowMessage(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
        static_cast<void>(lParam);
        switch (message) {
        case WM_NCHITTEST:
            return HTTRANSPARENT;
        case WM_MOUSEACTIVATE:
            return MA_NOACTIVATE;
        case WM_HOTKEY:
            if (static_cast<int>(wParam) == activeHotKeyId_) {
                ToggleModule();
            }
            return 0;
        case kMessageRefresh:
            if (visible_.load()) {
                UpdateWindowLayout();
                InvalidateRect(window, nullptr, FALSE);
            }
            return 0;
        case kMessageVisibility:
            UpdateWindowVisibility();
            return 0;
        case kMessageConfig:
            ApplyWindowConfig();
            return 0;
        case kMessageDestroy:
            DestroyOverlayWindow();
            return 0;
        case WM_DISPLAYCHANGE:
            UpdateWindowLayout();
            InvalidateRect(window, nullptr, FALSE);
            return 0;
        case WM_DPICHANGED:
            UpdateWindowLayout();
            InvalidateRect(window, nullptr, FALSE);
            return 0;
        case WM_ERASEBKGND:
            return 1;
        case WM_PAINT:
            Paint(window);
            return 0;
        case WM_CLOSE:
            SetModuleVisible(false);
            return 0;
        case WM_DESTROY:
            if (window_ == window) {
                window_ = nullptr;
            }
            return 0;
        default:
            return DefWindowProcW(window, message, wParam, lParam);
        }
    }

    void WakeWorker() const {
        if (wakeEvent_ != nullptr) {
            SetEvent(wakeEvent_);
        }
    }

    void CleanupInitialization() {
        if (hotKeyRegistered_ && window_ != nullptr) {
            UnregisterHotKey(window_, activeHotKeyId_);
            hotKeyRegistered_ = false;
        }
        if (window_ != nullptr) {
            if (GetWindowThreadProcessId(window_, nullptr) == GetCurrentThreadId()) {
                DestroyWindow(window_);
            }
            window_ = nullptr;
        }
        if (classAtom_ != 0U) {
            UnregisterClassW(kWindowClass, instance_);
            classAtom_ = 0U;
        }
        if (stopEvent_ != nullptr) {
            CloseHandle(stopEvent_);
            stopEvent_ = nullptr;
        }
        if (wakeEvent_ != nullptr) {
            CloseHandle(wakeEvent_);
            wakeEvent_ = nullptr;
        }
        activeFunctionKey_ = 0;
        activeHotKeyId_ = kHotKeyIdA;
        foregroundProcessId_.store(0U);
        {
            std::lock_guard<std::mutex> firmwareLock(firmwareMutex_);
            firmwareTemperatureC_ = 0;
            firmwareRpm_ = 0;
            firmwareKnown_ = false;
        }
        baseDirectory_.clear();
        instance_ = nullptr;
    }

    void DestroyOverlayWindow() {
        if (window_ == nullptr) {
            return;
        }
        if (hotKeyRegistered_) {
            UnregisterHotKey(window_, activeHotKeyId_);
            hotKeyRegistered_ = false;
        }
        const HWND oldWindow = window_;
        window_ = nullptr;
        DestroyWindow(oldWindow);
    }

    void ApplyWindowConfig() {
        if (window_ == nullptr) {
            return;
        }
        Config config{};
        {
            std::lock_guard<std::mutex> lock(configMutex_);
            config = config_;
        }

        if (!hotKeyRegistered_ || config.functionKey != activeFunctionKey_) {
            const int replacementId = hotKeyRegistered_ && activeHotKeyId_ == kHotKeyIdA ? kHotKeyIdB : kHotKeyIdA;
            if (RegisterHotKey(window_, replacementId, MOD_NOREPEAT,
                               static_cast<UINT>(VK_F1 + config.functionKey - 1)) != FALSE) {
                if (hotKeyRegistered_) UnregisterHotKey(window_, activeHotKeyId_);
                activeHotKeyId_ = replacementId;
                activeFunctionKey_ = config.functionKey;
                hotKeyRegistered_ = true;
            } else if (hotKeyRegistered_) {
                std::lock_guard<std::mutex> lock(configMutex_);
                config_.functionKey = activeFunctionKey_;
            }
        }

        // Opacity is intentionally applied to the complete black overlay:
        // 0% is fully transparent and 100% is opaque black with opaque text.
        // Intermediate values fade the background and lettering together.
        const BYTE alpha = static_cast<BYTE>((config.opacityPercent * 255 + 50) / 100);
        SetLayeredWindowAttributes(window_, 0U, alpha, LWA_ALPHA);
        UpdateWindowLayout();
        InvalidateRect(window_, nullptr, FALSE);
    }

    void UpdateWindowVisibility() {
        if (window_ == nullptr) {
            return;
        }
        if (visible_.load()) {
            UpdateWindowLayout();
            SetWindowPos(window_, HWND_TOPMOST, 0, 0, 0, 0,
                         SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
            ShowWindow(window_, SW_SHOWNOACTIVATE);
            InvalidateRect(window_, nullptr, FALSE);
        } else {
            ShowWindow(window_, SW_HIDE);
        }
    }

    static UINT DpiForMonitor(HMONITOR monitor) {
        using GetDpiForMonitorFunction = HRESULT(WINAPI*)(HMONITOR, int, UINT*, UINT*);
        const HMODULE shcore = LoadLibraryW(L"shcore.dll");
        if (shcore != nullptr) {
            const auto getDpiForMonitor = reinterpret_cast<GetDpiForMonitorFunction>(
                GetProcAddress(shcore, "GetDpiForMonitor"));
            UINT dpiX = 0U;
            UINT dpiY = 0U;
            if (getDpiForMonitor != nullptr && SUCCEEDED(getDpiForMonitor(monitor, 0, &dpiX, &dpiY)) && dpiX != 0U) {
                FreeLibrary(shcore);
                return dpiX;
            }
            FreeLibrary(shcore);
        }
        HDC screen = GetDC(nullptr);
        if (screen == nullptr) {
            return 96U;
        }
        const int dpi = GetDeviceCaps(screen, LOGPIXELSX);
        ReleaseDC(nullptr, screen);
        return dpi > 0 ? static_cast<UINT>(dpi) : 96U;
    }

    double CalculateScale(const RECT& monitorRect, HMONITOR monitor, const Config& config) const {
        const int monitorWidth = (std::max)(1, static_cast<int>(monitorRect.right - monitorRect.left));
        const int monitorHeight = (std::max)(1, static_cast<int>(monitorRect.bottom - monitorRect.top));
        int referenceWidth = monitorWidth;
        int referenceHeight = monitorHeight;
        MONITORINFOEXW monitorInfo{};
        monitorInfo.cbSize = sizeof(monitorInfo);
        if (GetMonitorInfoW(monitor, &monitorInfo) != FALSE) {
            DEVMODEW mode{};
            mode.dmSize = sizeof(mode);
            // Fullscreen games normally change the active mode temporarily;
            // the registry mode remains the panel/native desktop resolution.
            if (EnumDisplaySettingsW(monitorInfo.szDevice, ENUM_REGISTRY_SETTINGS, &mode) != FALSE) {
                referenceWidth = (std::max)(referenceWidth, static_cast<int>(mode.dmPelsWidth));
                referenceHeight = (std::max)(referenceHeight, static_cast<int>(mode.dmPelsHeight));
            }
        }
        const double dpiScale = static_cast<double>(DpiForMonitor(monitor)) / 96.0;
        const double resolutionScale = std::sqrt((static_cast<double>(referenceWidth) / 1920.0) *
                                                 (static_cast<double>(referenceHeight) / 1080.0));
        const double effective = (static_cast<double>(config.scalePercent) / 100.0) * dpiScale *
                                 (std::max)(1.0, (std::min)(2.0, resolutionScale));
        if (config.layoutStyle == 1) return (std::max)(0.50, (std::min)(4.0, effective));
        const double monitorFit = (std::min)(static_cast<double>(monitorWidth) / 360.0,
                                             static_cast<double>(monitorHeight) / 318.0);
        return (std::max)(0.50, (std::min)((std::min)(4.0, monitorFit), effective));
    }

    void UpdateWindowLayout() {
        if (window_ == nullptr) {
            return;
        }
        Config config{};
        {
            std::lock_guard<std::mutex> lock(configMutex_);
            config = config_;
        }

        HWND target = GetForegroundWindow();
        DWORD targetProcessId = 0U;
        if (target != nullptr) {
            GetWindowThreadProcessId(target, &targetProcessId);
        }
        foregroundProcessId_.store(targetProcessId);
        HMONITOR monitor = MonitorFromWindow(target != nullptr ? target : window_, MONITOR_DEFAULTTOPRIMARY);
        MONITORINFO information{};
        information.cbSize = sizeof(information);
        if (GetMonitorInfoW(monitor, &information) == FALSE) {
            return;
        }

        const double scale = CalculateScale(information.rcMonitor, monitor, config);
        currentScale_ = scale;
        const int monitorLeft = static_cast<int>(information.rcMonitor.left);
        const int monitorTop = static_cast<int>(information.rcMonitor.top);
        const int monitorRight = static_cast<int>(information.rcMonitor.right);
        const int monitorBottom = static_cast<int>(information.rcMonitor.bottom);
        const int monitorWidth = monitorRight - monitorLeft;
        const int monitorHeight = monitorBottom - monitorTop;
        const int requestedMarginX = static_cast<int>(std::lround(static_cast<double>(config.marginX) * scale));
        const int requestedMarginY = static_cast<int>(std::lround(static_cast<double>(config.marginY) * scale));
        int width = 0;
        int height = 0;
        int x = monitorLeft;
        int y = monitorTop;
        if (config.layoutStyle == 1) {
            width = monitorWidth;
            const bool lowResolution = width < 1600;
            const double fitScale = lowResolution ? 1.60 : static_cast<double>(width) / 1120.0;
            const double barTextScale = (std::min)(scale, (std::max)(0.75, fitScale));
            height = static_cast<int>(std::lround(31.0 * barTextScale));
            height = (std::max)(1, (std::min)(monitorHeight, height));
            x = monitorLeft;
            y = monitorTop;
        } else {
            width = static_cast<int>(std::lround(360.0 * scale));
            height = static_cast<int>(std::lround(318.0 * scale));
            width = (std::max)(1, (std::min)(monitorWidth, width));
            height = (std::max)(1, (std::min)(monitorHeight, height));

            x = monitorLeft + requestedMarginX;
            y = monitorTop + requestedMarginY;
            if (config.corner == 1 || config.corner == 3) {
                x = monitorRight - width - requestedMarginX;
            }
            if (config.corner == 2 || config.corner == 3) {
                y = monitorBottom - height - requestedMarginY;
            }
            x = (std::max)(monitorLeft, (std::min)(monitorRight - width, x));
            y = (std::max)(monitorTop, (std::min)(monitorBottom - height, y));
        }

        SetWindowPos(window_, HWND_TOPMOST, x, y, width, height, SWP_NOACTIVATE);
    }

    static std::wstring FormatNumber(const NumericMetric& metric, const wchar_t* format) {
        if (!metric.known || !std::isfinite(metric.value)) {
            return L"N/A";
        }
        wchar_t buffer[64]{};
        if (swprintf_s(buffer, format, metric.value) < 0) {
            return L"N/A";
        }
        return std::wstring(buffer);
    }

    static std::wstring FormatMemory(const NumericMetric& used, const NumericMetric& total) {
        if (!used.known || !total.known || used.value < 0.0 || total.value <= 0.0) {
            return L"N/A";
        }
        wchar_t buffer[64]{};
        if (swprintf_s(buffer, L"%.1f / %.1f GB", used.value / kBytesPerGiB,
                       total.value / kBytesPerGiB) < 0) {
            return L"N/A";
        }
        return std::wstring(buffer);
    }

    static std::wstring FormatCompactMemory(const NumericMetric& used, const NumericMetric& total) {
        if (!used.known || !total.known || used.value < 0.0 || total.value <= 0.0) {
            return L"N/A";
        }
        wchar_t buffer[64]{};
        if (swprintf_s(buffer, L"%.1f / %.1f GB", used.value / kBytesPerGiB,
                       total.value / kBytesPerGiB) < 0) {
            return L"N/A";
        }
        return std::wstring(buffer);
    }

    void Paint(HWND window) {
        PAINTSTRUCT paint{};
        HDC windowDc = BeginPaint(window, &paint);
        if (windowDc == nullptr) {
            return;
        }

        RECT client{};
        GetClientRect(window, &client);
        const int width = client.right - client.left;
        const int height = client.bottom - client.top;
        HDC bufferDc = CreateCompatibleDC(windowDc);
        HBITMAP bitmap = bufferDc == nullptr ? nullptr : CreateCompatibleBitmap(windowDc, width, height);
        HGDIOBJ oldBitmap = nullptr;
        HDC targetDc = windowDc;
        if (bufferDc != nullptr && bitmap != nullptr) {
            oldBitmap = SelectObject(bufferDc, bitmap);
            targetDc = bufferDc;
        }

        HBRUSH background = CreateSolidBrush(RGB(0, 0, 0));
        FillRect(targetDc, &client, background != nullptr ? background :
                 static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));
        if (background != nullptr) {
            DeleteObject(background);
        }
        SetBkMode(targetDc, TRANSPARENT);

        Snapshot snapshot{};
        {
            std::lock_guard<std::mutex> lock(snapshotMutex_);
            snapshot = snapshot_;
        }
        Config config{};
        {
            std::lock_guard<std::mutex> lock(configMutex_);
            config = config_;
        }

        const double scale = currentScale_;
        RECT physicalWindow{};
        GetWindowRect(window, &physicalWindow);
        const int physicalWidth = (std::max)(1, static_cast<int>(physicalWindow.right - physicalWindow.left));
        const bool lowResolutionMode = config.layoutStyle == 1 && physicalWidth < 1600;
        const double topBarFitScale = lowResolutionMode ? 1.60 : static_cast<double>(width) / 1120.0;
        const double textScale = config.layoutStyle == 1 ?
            (std::min)(scale, (std::max)(0.75, topBarFitScale)) : scale;
        const int fontHeight = (std::max)(8, static_cast<int>(std::lround(15.0 * textScale)));
        HFONT font = CreateFontW(-fontHeight, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
                                 DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                 CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
        HGDIOBJ oldFont = nullptr;
        if (font != nullptr) {
            oldFont = SelectObject(targetDc, font);
        }

        std::wstring temperature = L"N/A";
        std::wstring fan = L"N/A";
        if (snapshot.firmwareKnown) {
            temperature = std::to_wstring(snapshot.temperatureC) + L" C";
            fan = std::to_wstring(snapshot.fanRpm) + L" RPM";
        }
        std::wstring battery = FormatNumber(snapshot.batteryPercent, L"%.0f%%");
        std::wstring time = L"N/A";
        if (snapshot.timeKnown) {
            wchar_t timeBuffer[32]{};
            if (swprintf_s(timeBuffer, L"%02u:%02u:%02u", static_cast<unsigned int>(snapshot.localTime.wHour),
                           static_cast<unsigned int>(snapshot.localTime.wMinute),
                           static_cast<unsigned int>(snapshot.localTime.wSecond)) >= 0) {
                time = timeBuffer;
            }
        }

        if (config.layoutStyle == 1) {
            const COLORREF yellow = RGB(255, 214, 64);
            const COLORREF orange = RGB(255, 145, 55);
            const COLORREF green = RGB(91, 220, 112);
            const COLORREF cyan = RGB(62, 215, 255);
            const COLORREF white = RGB(242, 246, 250);
            const int requestedPadding = (std::max)(3, static_cast<int>(std::lround(8.0 * textScale)));
            const int padding = (std::min)(requestedPadding, (std::max)(0, (width - 1) / 2));
            const int gap = (std::max)(4, static_cast<int>(std::lround(10.0 * textScale)));
            const int layoutWidth = lowResolutionMode ? physicalWidth : width;
            const int layoutHeight = lowResolutionMode ?
                (std::max)(1, static_cast<int>(std::lround(static_cast<double>(height) * layoutWidth /
                                                          static_cast<double>((std::max)(1, width))))) : height;
            const int usableWidth = (std::max)(1, layoutWidth - (2 * padding));
            RECT segments[7]{};
            const int weights[7]{13, 20, 21, 15, 12, 10, 9};
            int segmentLeft = padding, accumulatedWeight = 0;
            for (int index = 0; index < 7; ++index) {
                accumulatedWeight += weights[index];
                const int segmentRight = index == 6 ? layoutWidth - padding :
                    padding + ((usableWidth * accumulatedWeight) / 100);
                segments[index] = RECT{segmentLeft, 0, segmentRight, layoutHeight};
                segmentLeft = segmentRight;
            }

            const auto textWidth = [targetDc](const std::wstring& textValue) {
                SIZE size{};
                return GetTextExtentPoint32W(targetDc, textValue.c_str(),
                    static_cast<int>(textValue.size()), &size) != FALSE ? size.cx : 0;
            };
            // Reserve exactly two rendered spaces after the FPS area. Shift
            // every following metric together; keep the clock right edge fixed.
            const int afterFpsShift = textWidth(L"  ");
            const auto shiftedCoordinate = [layoutWidth, afterFpsShift](LONG coordinate) {
                return static_cast<LONG>((std::min)(layoutWidth, static_cast<int>(coordinate) + afterFpsShift));
            };
            segments[0].right = shiftedCoordinate(segments[0].right);
            for (int index = 1; index <= 5; ++index) {
                segments[index].left = shiftedCoordinate(segments[index].left);
                segments[index].right = shiftedCoordinate(segments[index].right);
            }
            segments[6].left = (std::min)(segments[6].right,
                                          static_cast<LONG>(segments[6].left + afterFpsShift));

            const auto drawPair = [targetDc, gap, &textWidth, white](RECT rect, const wchar_t* label,
                                                                    COLORREF color,
                                                                    const std::wstring& value) {
                const int segmentWidth = static_cast<int>(rect.right - rect.left);
                const int inset = (std::min)(gap, (std::max)(0, segmentWidth / 2));
                rect.left += inset;
                rect.right -= inset;
                const int labelWidth = textWidth(label);
                RECT labelRect{rect.left, rect.top, (std::min)(rect.right, rect.left + labelWidth), rect.bottom};
                SetTextColor(targetDc, color);
                DrawTextW(targetDc, label, -1, &labelRect,
                          DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
                RECT valueRect{(std::min)(rect.right, labelRect.right + gap), rect.top, rect.right, rect.bottom};
                SetTextColor(targetDc, white);
                DrawTextW(targetDc, value.c_str(), -1, &valueRect,
                          DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
            };

            // Numeric frame time is intentionally hidden for now. The compact
            // graph remains available as the immediate pacing indicator.
            const std::wstring fpsValue = FormatNumber(snapshot.fps, L"%.0f");
            RECT fpsRect = segments[0];
            const int fpsWidth = static_cast<int>(fpsRect.right - fpsRect.left);
            const int fpsInset = (std::min)(gap, (std::max)(0, fpsWidth / 2));
            fpsRect.left += fpsInset;
            fpsRect.right -= fpsInset;
            const int fpsLabelWidth = textWidth(L"FPS");
            RECT fpsLabel{fpsRect.left, fpsRect.top, (std::min)(fpsRect.right, fpsRect.left + fpsLabelWidth), fpsRect.bottom};
            SetTextColor(targetDc, yellow);
            DrawTextW(targetDc, L"FPS", -1, &fpsLabel,
                      DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
            const int fpsValueWidth = textWidth(fpsValue);
            RECT fpsValueRect{(std::min)(fpsRect.right, fpsLabel.right + gap), fpsRect.top,
                              (std::min)(fpsRect.right, fpsLabel.right + gap + fpsValueWidth), fpsRect.bottom};
            SetTextColor(targetDc, white);
            DrawTextW(targetDc, fpsValue.c_str(), -1, &fpsValueRect,
                      DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
            const int graphInset = (std::max)(2, static_cast<int>(std::lround((lowResolutionMode ? 3.0 : 9.0) * textScale)));
            RECT miniGraph{fpsValueRect.right + gap, fpsRect.top + graphInset,
                           fpsRect.right, fpsRect.bottom - graphInset};
            PaintMiniGraph(targetDc, miniGraph, snapshot.frameHistory, textScale, yellow);

            const std::wstring cpuValue = FormatNumber(snapshot.cpuPercent, L"%.0f%%") + L"  " +
                (snapshot.firmwareKnown ? std::to_wstring(snapshot.temperatureC) + L"C" : L"N/A") + L"  " +
                FormatNumber(snapshot.powerWatts, L"%.1fW");
            const NumericMetric& vramTotal = snapshot.vramCapacityBytes.known ?
                snapshot.vramCapacityBytes : snapshot.vramBudgetBytes;
            const auto compactMemoryForWidth = [lowResolutionMode](const NumericMetric& used, const NumericMetric& total) {
                if (!lowResolutionMode) return FormatCompactMemory(used, total);
                if (!used.known || !total.known || used.value < 0.0 || total.value <= 0.0) return std::wstring(L"N/A");
                wchar_t buffer[64]{};
                return swprintf_s(buffer, L"%.1f/%.1fG", used.value / kBytesPerGiB,
                                  total.value / kBytesPerGiB) >= 0 ? std::wstring(buffer) : std::wstring(L"N/A");
            };
            const std::wstring gpuValue = FormatNumber(snapshot.gpuPercent, L"%.0f%%") + L"  " +
                                          compactMemoryForWidth(snapshot.vramUsedBytes, vramTotal);
            drawPair(segments[1], L"Z1E", orange, cpuValue);
            drawPair(segments[2], L"780M", green, gpuValue);
            drawPair(segments[3], L"RAM", cyan,
                     compactMemoryForWidth(snapshot.ramUsedBytes, snapshot.ramTotalBytes));
            drawPair(segments[4], L"FAN", orange,
                     snapshot.firmwareKnown ? std::to_wstring(snapshot.fanRpm) + L"RPM" : L"N/A");
            drawPair(segments[5], L"BATT", green, battery);
            RECT timeRect = segments[6];
            const int timeWidth = static_cast<int>(timeRect.right - timeRect.left);
            const int timeInset = (std::min)(gap, (std::max)(0, timeWidth / 2));
            timeRect.left += timeInset;
            timeRect.right -= timeInset;
            SetTextColor(targetDc, cyan);
            DrawTextW(targetDc, time.c_str(), -1, &timeRect,
                      DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
        } else {
            // Keep this order synchronized with the documented RTSS-style layout.
            const std::vector<std::pair<std::wstring, std::wstring>> rows{
                {L"FPS", FormatNumber(snapshot.fps, L"%.1f")},
                {L"CPU USAGE", FormatNumber(snapshot.cpuPercent, L"%.0f%%")},
                {L"CPU TEMP", temperature},
                {L"CPU POWER", FormatNumber(snapshot.powerWatts, L"%.1f W")},
                {L"GPU USAGE", FormatNumber(snapshot.gpuPercent, L"%.0f%%")},
                {L"VRAM", FormatMemory(snapshot.vramUsedBytes,
                                        snapshot.vramCapacityBytes.known ? snapshot.vramCapacityBytes :
                                                                           snapshot.vramBudgetBytes)},
                {L"RAM USED", FormatMemory(snapshot.ramUsedBytes, snapshot.ramTotalBytes)},
                {L"FAN", fan},
                {L"BATTERY", battery},
                {L"TIME", time}
            };

            const int padding = (std::max)(4, static_cast<int>(std::lround(12.0 * scale)));
            const int rowHeight = (std::max)(11, static_cast<int>(std::lround(22.0 * scale)));
            const int graphHeight = (std::max)(20, static_cast<int>(std::lround(48.0 * scale)));
            const int valueLeft = static_cast<int>(std::lround(130.0 * scale));
            int y = padding;
            for (std::size_t index = 0U; index < rows.size(); ++index) {
                RECT labelRect{padding, y, (std::min)(valueLeft, width - padding), y + rowHeight};
                RECT valueRect{valueLeft, y, width - padding, y + rowHeight};
                SetTextColor(targetDc, RGB(145, 158, 171));
                DrawTextW(targetDc, rows[index].first.c_str(), -1, &labelRect,
                          DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
                SetTextColor(targetDc, RGB(238, 243, 248));
                DrawTextW(targetDc, rows[index].second.c_str(), -1, &valueRect,
                          DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
                y += rowHeight;
                if (index == 0U) {
                    RECT graphRect{padding, y, width - padding, (std::min)(height - padding, y + graphHeight)};
                    PaintGraph(targetDc, graphRect, snapshot.frameHistory, scale);
                    y += graphHeight;
                }
            }
        }

        if (oldFont != nullptr) {
            SelectObject(targetDc, oldFont);
        }
        if (font != nullptr) {
            DeleteObject(font);
        }
        if (bufferDc != nullptr && bitmap != nullptr) {
            BitBlt(windowDc, 0, 0, width, height, bufferDc, 0, 0, SRCCOPY);
            SelectObject(bufferDc, oldBitmap);
            DeleteObject(bitmap);
        }
        if (bufferDc != nullptr) {
            DeleteDC(bufferDc);
        }
        EndPaint(window, &paint);
    }

    static void PaintMiniGraph(HDC dc, const RECT& rect, const std::vector<double>& history,
                               double scale, COLORREF color) {
        const int graphWidth = rect.right - rect.left;
        const int graphHeight = rect.bottom - rect.top;
        if (graphWidth <= 2 || graphHeight <= 2 || history.empty()) {
            return;
        }

        double maximum = 50.0;
        for (double value : history) {
            if (std::isfinite(value)) {
                maximum = (std::max)(maximum, (std::min)(100.0, value));
            }
        }
        const int penWidth = (std::max)(1, static_cast<int>(std::lround(scale)));
        HPEN pen = CreatePen(PS_SOLID, penWidth, color);
        if (pen == nullptr) {
            return;
        }
        HGDIOBJ oldPen = SelectObject(dc, pen);
        const std::size_t count = history.size();
        for (std::size_t index = 0U; index < count; ++index) {
            const int x = count <= 1U ? rect.right - 1 :
                rect.left + static_cast<int>((static_cast<unsigned long long>(index) *
                static_cast<unsigned long long>(graphWidth - 1)) /
                static_cast<unsigned long long>(count - 1U));
            const double normalized = (std::max)(0.0, (std::min)(maximum, history[index])) / maximum;
            const int y = rect.bottom - 1 -
                static_cast<int>(std::lround(normalized * static_cast<double>(graphHeight - 2)));
            if (index == 0U) {
                MoveToEx(dc, x, y, nullptr);
            } else {
                LineTo(dc, x, y);
            }
        }
        SelectObject(dc, oldPen);
        DeleteObject(pen);
    }

    static void PaintGraph(HDC dc, const RECT& rect, const std::vector<double>& history, double scale) {
        HBRUSH graphBackground = CreateSolidBrush(RGB(22, 27, 34));
        FillRect(dc, &rect, graphBackground != nullptr ? graphBackground :
                 static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));
        if (graphBackground != nullptr) {
            DeleteObject(graphBackground);
        }

        const int graphWidth = rect.right - rect.left;
        const int graphHeight = rect.bottom - rect.top;
        if (graphWidth <= 2 || graphHeight <= 2) {
            return;
        }

        double maximum = 50.0;
        for (double value : history) {
            if (std::isfinite(value)) {
                maximum = (std::max)(maximum, (std::min)(100.0, value));
            }
        }
        const auto mapY = [&rect, graphHeight, maximum](double value) {
            const double normalized = (std::max)(0.0, (std::min)(maximum, value)) / maximum;
            return rect.bottom - 1 - static_cast<int>(std::lround(normalized * static_cast<double>(graphHeight - 2)));
        };

        HPEN guidePen = CreatePen(PS_DOT, 1, RGB(62, 70, 79));
        HGDIOBJ oldPen = guidePen == nullptr ? nullptr : SelectObject(dc, guidePen);
        if (guidePen != nullptr) {
            for (double guide : {16.67, 33.33}) {
                const int guideY = mapY(guide);
                MoveToEx(dc, rect.left, guideY, nullptr);
                LineTo(dc, rect.right, guideY);
            }
            SelectObject(dc, oldPen);
            DeleteObject(guidePen);
        }

        if (history.empty()) {
            return;
        }
        const int penWidth = (std::max)(1, static_cast<int>(std::lround(scale)));
        HPEN linePen = CreatePen(PS_SOLID, penWidth, RGB(89, 211, 153));
        oldPen = linePen == nullptr ? nullptr : SelectObject(dc, linePen);
        if (linePen != nullptr) {
            const std::size_t count = history.size();
            for (std::size_t index = 0U; index < count; ++index) {
                const int x = count <= 1U ? rect.right - 1 :
                    rect.left + static_cast<int>((static_cast<unsigned long long>(index) *
                    static_cast<unsigned long long>(graphWidth - 1)) /
                    static_cast<unsigned long long>(count - 1U));
                const int pointY = mapY(history[index]);
                if (index == 0U) {
                    MoveToEx(dc, x, pointY, nullptr);
                } else {
                    LineTo(dc, x, pointY);
                }
            }
            SelectObject(dc, oldPen);
            DeleteObject(linePen);
        }
    }

    static NumericMetric ReadCpuUsage(ULARGE_INTEGER& previousIdle, ULARGE_INTEGER& previousKernel,
                                      ULARGE_INTEGER& previousUser, bool& havePrevious) {
        NumericMetric result{};
        FILETIME idleTime{};
        FILETIME kernelTime{};
        FILETIME userTime{};
        if (GetSystemTimes(&idleTime, &kernelTime, &userTime) == FALSE) {
            havePrevious = false;
            return result;
        }
        ULARGE_INTEGER idle{};
        ULARGE_INTEGER kernel{};
        ULARGE_INTEGER user{};
        idle.LowPart = idleTime.dwLowDateTime;
        idle.HighPart = idleTime.dwHighDateTime;
        kernel.LowPart = kernelTime.dwLowDateTime;
        kernel.HighPart = kernelTime.dwHighDateTime;
        user.LowPart = userTime.dwLowDateTime;
        user.HighPart = userTime.dwHighDateTime;

        if (havePrevious && kernel.QuadPart >= previousKernel.QuadPart && user.QuadPart >= previousUser.QuadPart &&
            idle.QuadPart >= previousIdle.QuadPart) {
            const ULONGLONG kernelDelta = kernel.QuadPart - previousKernel.QuadPart;
            const ULONGLONG userDelta = user.QuadPart - previousUser.QuadPart;
            const ULONGLONG idleDelta = idle.QuadPart - previousIdle.QuadPart;
            const ULONGLONG total = kernelDelta + userDelta;
            if (total > 0ULL && idleDelta <= total) {
                result.value = 100.0 * static_cast<double>(total - idleDelta) / static_cast<double>(total);
                result.value = (std::max)(0.0, (std::min)(100.0, result.value));
                result.known = true;
            }
        }
        previousIdle = idle;
        previousKernel = kernel;
        previousUser = user;
        havePrevious = true;
        return result;
    }

    static void ReadMemory(Snapshot& snapshot) {
        MEMORYSTATUSEX memory{};
        memory.dwLength = sizeof(memory);
        if (GlobalMemoryStatusEx(&memory) != FALSE && memory.ullTotalPhys > 0ULL &&
            memory.ullAvailPhys <= memory.ullTotalPhys) {
            snapshot.ramTotalBytes.value = static_cast<double>(memory.ullTotalPhys);
            snapshot.ramTotalBytes.known = true;
            snapshot.ramUsedBytes.value = static_cast<double>(memory.ullTotalPhys - memory.ullAvailPhys);
            snapshot.ramUsedBytes.known = true;
        }
    }

    static void ReadBattery(Snapshot& snapshot) {
        SYSTEM_POWER_STATUS status{};
        if (GetSystemPowerStatus(&status) != FALSE && status.BatteryFlag != 128U &&
            status.BatteryLifePercent <= 100U) {
            snapshot.batteryPercent.value = static_cast<double>(status.BatteryLifePercent);
            snapshot.batteryPercent.known = true;
        }
    }

    void UpdateFrameMetrics(Snapshot& snapshot, SteadyClock::time_point now) {
        const SteadyClock::time_point historyStart = now - std::chrono::seconds(5);
        for (auto process = frameSamplesByProcess_.begin(); process != frameSamplesByProcess_.end();) {
            std::deque<FrameSample>& samples = process->second;
            while (!samples.empty() && samples.front().received < historyStart) {
                samples.pop_front();
            }
            if (samples.empty()) {
                process = frameSamplesByProcess_.erase(process);
            } else {
                ++process;
            }
        }

        // Retain an overlap across one-second publications so a short ETW
        // scheduling gap does not make otherwise continuous presents blink.
        const SteadyClock::time_point rollingStart = now - std::chrono::milliseconds(2500);
        const DWORD foregroundProcess = foregroundProcessId_.load();
        DWORD targetProcess = 0U;
        const auto foregroundSamples = frameSamplesByProcess_.find(foregroundProcess);
        if (foregroundSamples != frameSamplesByProcess_.end()) {
            std::size_t count = 0U;
            for (const FrameSample& sample : foregroundSamples->second) {
                if (sample.received >= rollingStart) {
                    ++count;
                }
            }
            if (count >= 2U) {
                targetProcess = foregroundProcess;
            }
        }

        // If the foreground PID is not presenting (launchers and games often
        // use different PIDs), select the busiest stream that is still active.
        std::size_t bestCount = 0U;
        std::size_t bestContinuousCount = 0U;
        bool bestActive = false;
        SteadyClock::time_point bestLast{};
        if (targetProcess == 0U) {
            for (const auto& process : frameSamplesByProcess_) {
                std::size_t count = 0U;
                std::size_t continuousCount = 0U;
                SteadyClock::time_point previous{};
                SteadyClock::time_point last{};
                for (const FrameSample& sample : process.second) {
                    if (sample.received < rollingStart) {
                        continue;
                    }
                    ++count;
                    if (continuousCount == 0U || sample.received - previous <= std::chrono::milliseconds(250)) {
                        ++continuousCount;
                    } else {
                        continuousCount = 1U;
                    }
                    previous = sample.received;
                    last = sample.received;
                }
                if (count < 2U) {
                    continue;
                }
                const bool active = last >= now - std::chrono::milliseconds(350);
                const bool equallyDominant = active == bestActive &&
                    continuousCount == bestContinuousCount && count == bestCount;
                const bool preferCurrent = equallyDominant && process.first == selectedFrameProcessId_ &&
                                           targetProcess != selectedFrameProcessId_;
                const bool better = targetProcess == 0U || (active && !bestActive) ||
                    (active == bestActive && (continuousCount > bestContinuousCount ||
                    (continuousCount == bestContinuousCount && (count > bestCount ||
                    (count == bestCount && (preferCurrent ||
                    (targetProcess != selectedFrameProcessId_ && last > bestLast)))))));
                if (better) {
                    targetProcess = process.first;
                    bestCount = count;
                    bestContinuousCount = continuousCount;
                    bestActive = active;
                    bestLast = last;
                }
            }
        }

        if (targetProcess != selectedFrameProcessId_) {
            selectedFrameProcessId_ = targetProcess;
            // Never carry a previous target's five-second graph into a newly
            // selected stream. Keep only the new target's current rolling data.
            const auto selected = frameSamplesByProcess_.find(selectedFrameProcessId_);
            if (selected != frameSamplesByProcess_.end()) {
                while (!selected->second.empty() && selected->second.front().received < rollingStart) {
                    selected->second.pop_front();
                }
            }
        }

        const auto selected = frameSamplesByProcess_.find(selectedFrameProcessId_);
        if (selected == frameSamplesByProcess_.end()) {
            return;
        }
        double sum = 0.0;
        std::size_t rollingCount = 0U;
        for (const FrameSample& sample : selected->second) {
            snapshot.frameHistory.push_back(sample.milliseconds);
            if (sample.received >= rollingStart) {
                sum += sample.milliseconds;
                ++rollingCount;
            }
        }
        if (rollingCount != 0U && sum > 0.0) {
            snapshot.frameTimeMs.value = sum / static_cast<double>(rollingCount);
            snapshot.frameTimeMs.known = true;
            snapshot.fps.value = 1000.0 * static_cast<double>(rollingCount) / sum;
            snapshot.fps.known = std::isfinite(snapshot.fps.value) && snapshot.fps.value > 0.0;
        }
    }

    void AddFrame(DWORD processId, double frameTime, DWORD presentMonProcessId) {
        if (processId == 0U || processId == GetCurrentProcessId() || processId == presentMonProcessId) {
            return;
        }
        const SteadyClock::time_point now = SteadyClock::now();
        std::deque<FrameSample>& samples = frameSamplesByProcess_[processId];
        samples.push_back(FrameSample{frameTime, now});
        while (!samples.empty() && samples.front().received < now - std::chrono::seconds(5)) {
            samples.pop_front();
        }
        while (samples.size() > 360U) {
            samples.pop_front();
        }
    }

    void PublishSnapshot(Snapshot snapshot) {
        {
            std::lock_guard<std::mutex> firmwareLock(firmwareMutex_);
            snapshot.temperatureC = firmwareTemperatureC_;
            snapshot.fanRpm = firmwareRpm_;
            snapshot.firmwareKnown = firmwareKnown_;
        }
        GetLocalTime(&snapshot.localTime);
        snapshot.timeKnown = true;
        {
            std::lock_guard<std::mutex> snapshotLock(snapshotMutex_);
            snapshot_ = std::move(snapshot);
        }
        const HWND window = window_;
        if (window != nullptr) {
            PostMessageW(window, kMessageRefresh, 0U, 0);
        }
    }

    void WorkerMain() {
        LegionGoPresentTrace::Collector presentTrace;
        PdhMetrics pdh;
        DxgiMetrics dxgi;
        ULARGE_INTEGER previousIdle{};
        ULARGE_INTEGER previousKernel{};
        ULARGE_INTEGER previousUser{};
        bool havePreviousCpu = false;
        bool telemetryOpen = false;
        bool wasVisible = false;
        SteadyClock::time_point nextMetric = SteadyClock::now();
        SteadyClock::time_point nextPresentTraceStart = SteadyClock::now();

        HANDLE waitHandles[2]{stopEvent_, wakeEvent_};
        for (;;) {
            if (WaitForSingleObject(stopEvent_, 0U) == WAIT_OBJECT_0) {
                break;
            }
            // FPS capture has an application lifetime independent from F10.
            // Hiding the window must never restart ETW or discard graph data.
            const SteadyClock::time_point now = SteadyClock::now();
            bool fpsCaptureEnabled = true;
            {
                std::lock_guard<std::mutex> lock(configMutex_);
                fpsCaptureEnabled = config_.fpsCaptureEnabled;
            }
            DWORD observedForegroundProcess = 0U;
            const HWND observedForegroundWindow = GetForegroundWindow();
            if (observedForegroundWindow != nullptr) {
                GetWindowThreadProcessId(observedForegroundWindow, &observedForegroundProcess);
            }
            foregroundProcessId_.store(observedForegroundProcess);
            if (fpsCaptureEnabled) {
                if (!presentTrace.Running() && now >= nextPresentTraceStart) {
                    if (!presentTrace.Start()) nextPresentTraceStart = now + std::chrono::seconds(2);
                }
                for (const auto& frame : presentTrace.Drain()) AddFrame(frame.processId, frame.milliseconds, 0U);
            } else if (presentTrace.Running()) {
                presentTrace.Stop();
                frameSamplesByProcess_.clear();
                selectedFrameProcessId_ = 0U;
            }

            const bool visible = visible_.load();
            if (!visible) {
                if (wasVisible) {
                    pdh.Close();
                    dxgi.Close();
                    telemetryOpen = false;
                    havePreviousCpu = false;
                    PublishSnapshot(Snapshot{});
                }
                wasVisible = false;
                const DWORD waitResult = WaitForMultipleObjects(2U, waitHandles, FALSE,
                    fpsCaptureEnabled ? 50U : INFINITE);
                if (waitResult == WAIT_OBJECT_0) break;
                continue;
            }

            if (!wasVisible) {
                pdh.Open();
                dxgi.Open();
                telemetryOpen = true;
                nextMetric = now;
                wasVisible = true;
            }

            if (telemetryOpen && now >= nextMetric) {
                Snapshot snapshot{};
                snapshot.cpuPercent = ReadCpuUsage(previousIdle, previousKernel, previousUser, havePreviousCpu);
                pdh.Collect(snapshot.gpuPercent, snapshot.vramUsedBytes, snapshot.powerWatts);
                dxgi.ReadCapacity(snapshot.vramCapacityBytes, snapshot.vramBudgetBytes);
                ReadMemory(snapshot);
                ReadBattery(snapshot);
                UpdateFrameMetrics(snapshot, now);
                PublishSnapshot(std::move(snapshot));
                nextMetric = now + std::chrono::milliseconds(kMetricIntervalMs);
            }

            const DWORD waitResult = WaitForMultipleObjects(2U, waitHandles, FALSE, 50U);
            if (waitResult == WAIT_OBJECT_0) {
                break;
            }
        }

        presentTrace.Stop();
        pdh.Close();
        frameSamplesByProcess_.clear();
        selectedFrameProcessId_ = 0U;
    }

    mutable std::mutex lifecycleMutex_;
    mutable std::mutex configMutex_;
    mutable std::mutex snapshotMutex_;
    mutable std::mutex firmwareMutex_;
    std::atomic<bool> initialized_{false};
    std::atomic<bool> visible_{false};
    std::atomic<DWORD> foregroundProcessId_{0U};
    HINSTANCE instance_ = nullptr;
    HWND window_ = nullptr;
    ATOM classAtom_ = 0U;
    HANDLE stopEvent_ = nullptr;
    HANDLE wakeEvent_ = nullptr;
    std::thread worker_;
    Config config_{};
    Snapshot snapshot_{};
    std::wstring baseDirectory_;
    bool hotKeyRegistered_ = false;
    int activeHotKeyId_ = kHotKeyIdA;
    int activeFunctionKey_ = 0;
    double currentScale_ = 1.0;
    int firmwareTemperatureC_ = 0;
    int firmwareRpm_ = 0;
    bool firmwareKnown_ = false;
    std::map<DWORD, std::deque<FrameSample>> frameSamplesByProcess_;
    DWORD selectedFrameProcessId_ = 0U;
};

} // namespace

bool Initialize(HINSTANCE instance, const std::wstring& baseDir, const Config& config) {
    return OverlayModule::Instance().InitializeModule(instance, baseDir, config);
}

void ApplyConfig(const Config& config) {
    OverlayModule::Instance().ApplyModuleConfig(config);
}

void Toggle() {
    OverlayModule::Instance().ToggleModule();
}

void SetVisible(bool visible) {
    OverlayModule::Instance().SetModuleVisible(visible);
}

bool IsVisible() {
    return OverlayModule::Instance().IsModuleVisible();
}

int ActiveFunctionKey() {
    return OverlayModule::Instance().ActiveModuleFunctionKey();
}

void SetFirmwareTelemetry(int tempC, int rpm, bool known) {
    OverlayModule::Instance().SetFirmware(tempC, rpm, known);
}

void Shutdown() {
    OverlayModule::Instance().ShutdownModule();
}

} // namespace LegionGoOverlay
