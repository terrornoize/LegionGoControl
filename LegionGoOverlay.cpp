#include "LegionGoOverlay.h"

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
#include <limits>
#include <locale>
#include <map>
#include <mutex>
#include <sstream>
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
    result.opacityPercent = (std::max)(30, (std::min)(100, result.opacityPercent));
    result.corner = (std::max)(0, (std::min)(3, result.corner));
    result.marginX = (std::max)(0, (std::min)(10000, result.marginX));
    result.marginY = (std::max)(0, (std::min)(10000, result.marginY));
    return result;
}

bool FileExists(const std::wstring& path) {
    const DWORD attributes = GetFileAttributesW(path.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0U;
}

std::wstring JoinPath(const std::wstring& directory, const wchar_t* relativePath) {
    if (directory.empty()) {
        return std::wstring(relativePath);
    }
    const wchar_t last = directory.back();
    if (last == L'\\' || last == L'/') {
        return directory + relativePath;
    }
    return directory + L"\\" + relativePath;
}

std::string NormalizeHeader(const std::string& input) {
    std::string result;
    result.reserve(input.size());
    for (unsigned char character : input) {
        if ((character >= static_cast<unsigned char>('A') && character <= static_cast<unsigned char>('Z')) ||
            (character >= static_cast<unsigned char>('a') && character <= static_cast<unsigned char>('z')) ||
            (character >= static_cast<unsigned char>('0') && character <= static_cast<unsigned char>('9'))) {
            if (character >= static_cast<unsigned char>('A') && character <= static_cast<unsigned char>('Z')) {
                character = static_cast<unsigned char>(character - static_cast<unsigned char>('A') +
                                                       static_cast<unsigned char>('a'));
            }
            result.push_back(static_cast<char>(character));
        }
    }
    return result;
}

std::vector<std::string> ParseCsvRecord(const std::string& line) {
    std::vector<std::string> fields;
    std::string field;
    bool quoted = false;
    for (std::size_t index = 0; index < line.size(); ++index) {
        const char character = line[index];
        if (character == '"') {
            if (quoted && index + 1U < line.size() && line[index + 1U] == '"') {
                field.push_back('"');
                ++index;
            } else {
                quoted = !quoted;
            }
        } else if (character == ',' && !quoted) {
            fields.push_back(field);
            field.clear();
        } else {
            field.push_back(character);
        }
    }
    fields.push_back(field);
    return fields;
}

bool ParseProcessId(const std::string& text, DWORD& processId) {
    std::istringstream stream(text);
    stream.imbue(std::locale::classic());
    unsigned long long parsed = 0ULL;
    stream >> std::ws >> parsed >> std::ws;
    if (!stream.eof() || stream.fail() || parsed == 0ULL ||
        parsed > static_cast<unsigned long long>((std::numeric_limits<DWORD>::max)())) {
        return false;
    }
    processId = static_cast<DWORD>(parsed);
    return true;
}

bool ParsePositiveDouble(const std::string& text, double& value) {
    std::istringstream stream(text);
    stream.imbue(std::locale::classic());
    double parsed = 0.0;
    stream >> std::ws >> parsed >> std::ws;
    if (!stream.eof() || stream.fail() || !std::isfinite(parsed) || parsed <= 0.0 || parsed > 10000.0) {
        return false;
    }
    value = parsed;
    return true;
}

class PresentMonCapture {
public:
    PresentMonCapture() = default;
    PresentMonCapture(const PresentMonCapture&) = delete;
    PresentMonCapture& operator=(const PresentMonCapture&) = delete;

    ~PresentMonCapture() {
        Stop();
    }

    bool Start(const std::wstring& executable) {
        Stop();

        SECURITY_ATTRIBUTES security{};
        security.nLength = sizeof(security);
        security.bInheritHandle = TRUE;

        HANDLE pipeRead = nullptr;
        HANDLE pipeWrite = nullptr;
        if (CreatePipe(&pipeRead, &pipeWrite, &security, 0U) == FALSE) {
            return false;
        }
        if (SetHandleInformation(pipeRead, HANDLE_FLAG_INHERIT, 0U) == FALSE) {
            CloseHandle(pipeRead);
            CloseHandle(pipeWrite);
            return false;
        }

        HANDLE nullHandle = CreateFileW(L"NUL", GENERIC_READ | GENERIC_WRITE,
                                        FILE_SHARE_READ | FILE_SHARE_WRITE, &security, OPEN_EXISTING,
                                        FILE_ATTRIBUTE_NORMAL, nullptr);
        if (nullHandle == INVALID_HANDLE_VALUE) {
            CloseHandle(pipeRead);
            CloseHandle(pipeWrite);
            return false;
        }

        HANDLE job = CreateJobObjectW(nullptr, nullptr);
        if (job == nullptr) {
            CloseHandle(nullHandle);
            CloseHandle(pipeRead);
            CloseHandle(pipeWrite);
            return false;
        }

        JOBOBJECT_EXTENDED_LIMIT_INFORMATION jobLimits{};
        jobLimits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        if (SetInformationJobObject(job, JobObjectExtendedLimitInformation, &jobLimits,
                                    static_cast<DWORD>(sizeof(jobLimits))) == FALSE) {
            CloseHandle(job);
            CloseHandle(nullHandle);
            CloseHandle(pipeRead);
            CloseHandle(pipeWrite);
            return false;
        }

        std::wstring commandLine = L"\"" + executable +
            L"\" --output_stdout --no_console_stats --v1_metrics --session_name LegionGoControlOverlay --stop_existing_session";
        std::vector<wchar_t> mutableCommand(commandLine.begin(), commandLine.end());
        mutableCommand.push_back(L'\0');

        STARTUPINFOEXW startup{};
        startup.StartupInfo.cb = sizeof(startup);
        startup.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
        startup.StartupInfo.hStdInput = nullHandle;
        startup.StartupInfo.hStdOutput = pipeWrite;
        startup.StartupInfo.hStdError = nullHandle;

        SIZE_T attributeBytes = 0U;
        static_cast<void>(InitializeProcThreadAttributeList(nullptr, 1U, 0U, &attributeBytes));
        std::vector<unsigned char> attributeStorage(attributeBytes);
        startup.lpAttributeList = attributeBytes == 0U ? nullptr :
            reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(attributeStorage.data());
        HANDLE inheritedHandles[2]{pipeWrite, nullHandle};
        const bool attributeListInitialized = startup.lpAttributeList != nullptr &&
            InitializeProcThreadAttributeList(startup.lpAttributeList, 1U, 0U, &attributeBytes) != FALSE;
        const bool attributesReady = attributeListInitialized &&
            UpdateProcThreadAttribute(startup.lpAttributeList, 0U, PROC_THREAD_ATTRIBUTE_HANDLE_LIST,
                                      inheritedHandles, sizeof(inheritedHandles), nullptr, nullptr) != FALSE;
        if (!attributesReady) {
            if (attributeListInitialized) {
                DeleteProcThreadAttributeList(startup.lpAttributeList);
            }
            CloseHandle(job);
            CloseHandle(nullHandle);
            CloseHandle(pipeRead);
            CloseHandle(pipeWrite);
            return false;
        }

        PROCESS_INFORMATION processInfo{};
        const DWORD creationFlags = CREATE_NO_WINDOW | CREATE_SUSPENDED | EXTENDED_STARTUPINFO_PRESENT;
        const BOOL created = CreateProcessW(executable.c_str(), mutableCommand.data(), nullptr, nullptr, TRUE,
                                            creationFlags, nullptr, nullptr, &startup.StartupInfo, &processInfo);
        DeleteProcThreadAttributeList(startup.lpAttributeList);
        CloseHandle(pipeWrite);
        CloseHandle(nullHandle);
        if (created == FALSE) {
            CloseHandle(job);
            CloseHandle(pipeRead);
            return false;
        }

        if (AssignProcessToJobObject(job, processInfo.hProcess) == FALSE) {
            TerminateProcess(processInfo.hProcess, 1U);
            CloseHandle(processInfo.hThread);
            CloseHandle(processInfo.hProcess);
            CloseHandle(job);
            CloseHandle(pipeRead);
            return false;
        }
        if (ResumeThread(processInfo.hThread) == static_cast<DWORD>(-1)) {
            TerminateProcess(processInfo.hProcess, 1U);
            CloseHandle(processInfo.hThread);
            CloseHandle(processInfo.hProcess);
            CloseHandle(job);
            CloseHandle(pipeRead);
            return false;
        }

        CloseHandle(processInfo.hThread);
        process_ = processInfo.hProcess;
        job_ = job;
        pipeRead_ = pipeRead;
        executable_ = executable;
        buffer_.clear();
        processColumn_ = kMissingColumn;
        frameTimeColumn_ = kMissingColumn;
        droppedColumn_ = kMissingColumn;
        return true;
    }

    void Stop() {
        if (process_ != nullptr && WaitForSingleObject(process_, 0U) == WAIT_TIMEOUT && !executable_.empty())
            RequestSessionTermination(executable_);
        if (process_ != nullptr) {
            WaitForSingleObject(process_, 750U);
        }
        // Job close is the final bounded fallback if the graceful named-session
        // termination did not make the collector exit.
        if (job_ != nullptr) {
            CloseHandle(job_);
            job_ = nullptr;
        }
        if (process_ != nullptr) {
            WaitForSingleObject(process_, 250U);
            CloseHandle(process_);
            process_ = nullptr;
        }
        if (pipeRead_ != nullptr) {
            CloseHandle(pipeRead_);
            pipeRead_ = nullptr;
        }
        executable_.clear();
        buffer_.clear();
        processColumn_ = kMissingColumn;
        frameTimeColumn_ = kMissingColumn;
        droppedColumn_ = kMissingColumn;
    }

    bool IsRunning() const {
        return process_ != nullptr && WaitForSingleObject(process_, 0U) == WAIT_TIMEOUT;
    }

    template <typename Callback>
    bool Drain(Callback&& callback) {
        if (pipeRead_ == nullptr) {
            return false;
        }

        // Never let a busy all-process trace monopolize the worker. This keeps
        // stop-event handling and firmware restore bounded during shutdown.
        constexpr std::size_t kMaximumBytesPerDrain = 256U * 1024U;
        std::size_t totalBytesRead = 0U;
        bool readAnything = false;
        while (totalBytesRead < kMaximumBytesPerDrain) {
            DWORD available = 0U;
            if (PeekNamedPipe(pipeRead_, nullptr, 0U, nullptr, &available, nullptr) == FALSE) {
                break;
            }
            if (available == 0U) {
                break;
            }

            char chunk[16384]{};
            const DWORD requested = (std::min)(available, static_cast<DWORD>(sizeof(chunk)));
            DWORD bytesRead = 0U;
            if (ReadFile(pipeRead_, chunk, requested, &bytesRead, nullptr) == FALSE || bytesRead == 0U) {
                break;
            }
            buffer_.append(chunk, static_cast<std::size_t>(bytesRead));
            totalBytesRead += static_cast<std::size_t>(bytesRead);
            readAnything = true;
            ConsumeRecords(callback);
            if (buffer_.size() > 1024U * 1024U) {
                buffer_.clear();
                processColumn_ = kMissingColumn;
                frameTimeColumn_ = kMissingColumn;
                droppedColumn_ = kMissingColumn;
            }
        }
        return readAnything;
    }

private:
    static void RequestSessionTermination(const std::wstring& executable) {
        std::wstring command = L"\"" + executable +
            L"\" --terminate_existing_session --session_name LegionGoControlOverlay";
        std::vector<wchar_t> mutableCommand(command.begin(), command.end()); mutableCommand.push_back(L'\0');
        STARTUPINFOW startup{}; startup.cb = sizeof(startup); startup.dwFlags = STARTF_USESHOWWINDOW; startup.wShowWindow = SW_HIDE;
        PROCESS_INFORMATION process{};
        if (CreateProcessW(executable.c_str(), mutableCommand.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW,
                           nullptr, nullptr, &startup, &process) != FALSE) {
            CloseHandle(process.hThread); WaitForSingleObject(process.hProcess, 750U); CloseHandle(process.hProcess);
        }
    }

    template <typename Callback>
    void ConsumeRecords(Callback& callback) {
        std::size_t recordStart = 0U;
        bool quoted = false;
        for (std::size_t index = 0U; index < buffer_.size(); ++index) {
            const char character = buffer_[index];
            if (character == '"') {
                if (quoted && index + 1U < buffer_.size() && buffer_[index + 1U] == '"') {
                    ++index;
                } else {
                    quoted = !quoted;
                }
            } else if ((character == '\r' || character == '\n') && !quoted) {
                std::string record = buffer_.substr(recordStart, index - recordStart);
                if (index + 1U < buffer_.size() && character == '\r' && buffer_[index + 1U] == '\n') {
                    ++index;
                }
                recordStart = index + 1U;
                ProcessRecord(record, callback);
            }
        }
        if (recordStart != 0U) {
            buffer_.erase(0U, recordStart);
        }
    }

    template <typename Callback>
    void ProcessRecord(std::string& record, Callback& callback) {
        if (record.empty()) {
            return;
        }
        if (record.size() >= 3U && static_cast<unsigned char>(record[0]) == 0xEFU &&
            static_cast<unsigned char>(record[1]) == 0xBBU && static_cast<unsigned char>(record[2]) == 0xBFU) {
            record.erase(0U, 3U);
        }

        const std::vector<std::string> fields = ParseCsvRecord(record);
        std::size_t processHeader = kMissingColumn;
        std::size_t frameHeader = kMissingColumn;
        std::size_t droppedHeader = kMissingColumn;
        for (std::size_t index = 0U; index < fields.size(); ++index) {
            const std::string normalized = NormalizeHeader(fields[index]);
            if (normalized == "processid") {
                processHeader = index;
            } else if (normalized == "msbetweenpresents") {
                frameHeader = index;
            } else if (normalized == "dropped") {
                droppedHeader = index;
            }
        }
        if (processHeader != kMissingColumn && frameHeader != kMissingColumn) {
            processColumn_ = processHeader;
            frameTimeColumn_ = frameHeader;
            droppedColumn_ = droppedHeader;
            return;
        }
        if (processColumn_ == kMissingColumn || frameTimeColumn_ == kMissingColumn ||
            processColumn_ >= fields.size() || frameTimeColumn_ >= fields.size()) {
            return;
        }

        if (droppedColumn_ < fields.size()) {
            const std::string dropped = NormalizeHeader(fields[droppedColumn_]);
            if (dropped == "1" || dropped == "true" || dropped == "yes") return;
        }
        DWORD processId = 0U;
        double frameTime = 0.0;
        if (ParseProcessId(fields[processColumn_], processId) &&
            ParsePositiveDouble(fields[frameTimeColumn_], frameTime)) {
            callback(processId, frameTime);
        }
    }

    static constexpr std::size_t kMissingColumn = (std::numeric_limits<std::size_t>::max)();
    HANDLE process_ = nullptr;
    HANDLE job_ = nullptr;
    HANDLE pipeRead_ = nullptr;
    std::wstring executable_;
    std::string buffer_;
    std::size_t processColumn_ = kMissingColumn;
    std::size_t frameTimeColumn_ = kMissingColumn;
    std::size_t droppedColumn_ = kMissingColumn;
};

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
        const double dpiScale = static_cast<double>(DpiForMonitor(monitor)) / 96.0;
        const double resolutionScale = std::sqrt((static_cast<double>(monitorWidth) / 1920.0) *
                                                 (static_cast<double>(monitorHeight) / 1080.0));
        const double effective = (static_cast<double>(config.scalePercent) / 100.0) * dpiScale *
                                 (std::max)(0.70, (std::min)(2.0, resolutionScale));
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
        int width = static_cast<int>(std::lround(360.0 * scale));
        int height = static_cast<int>(std::lround(318.0 * scale));
        width = (std::max)(1, (std::min)(monitorWidth, width));
        height = (std::max)(1, (std::min)(monitorHeight, height));
        const int marginX = static_cast<int>(std::lround(static_cast<double>(config.marginX) * scale));
        const int marginY = static_cast<int>(std::lround(static_cast<double>(config.marginY) * scale));

        int x = monitorLeft + marginX;
        int y = monitorTop + marginY;
        if (config.corner == 1 || config.corner == 3) {
            x = monitorRight - width - marginX;
        }
        if (config.corner == 2 || config.corner == 3) {
            y = monitorBottom - height - marginY;
        }
        x = (std::max)(monitorLeft, (std::min)(monitorRight - width, x));
        y = (std::max)(monitorTop, (std::min)(monitorBottom - height, y));

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

        HBRUSH background = CreateSolidBrush(RGB(14, 17, 22));
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

        const double scale = currentScale_;
        const int fontHeight = (std::max)(8, static_cast<int>(std::lround(15.0 * scale)));
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

        // Keep this order synchronized with the documented RTSS-style layout.
        const std::vector<std::pair<std::wstring, std::wstring>> rows{
            {L"FPS", FormatNumber(snapshot.fps, L"%.1f")},
            {L"FRAME TIME", FormatNumber(snapshot.frameTimeMs, L"%.1f ms")},
            {L"CPU USAGE", FormatNumber(snapshot.cpuPercent, L"%.0f%%")},
            {L"CPU TEMP", temperature},
            {L"CPU POWER", FormatNumber(snapshot.powerWatts, L"%.1f W")},
            {L"GPU USAGE", FormatNumber(snapshot.gpuPercent, L"%.0f%%")},
            {L"VRAM", FormatMemory(snapshot.vramUsedBytes,
                                    snapshot.vramCapacityBytes.known ? snapshot.vramCapacityBytes :
                                                                       snapshot.vramBudgetBytes)},
            {L"RAM FREE", FormatMemory(snapshot.ramUsedBytes, snapshot.ramTotalBytes)},
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
            if (index == 1U) {
                RECT graphRect{padding, y, width - padding, (std::min)(height - padding, y + graphHeight)};
                PaintGraph(targetDc, graphRect, snapshot.frameHistory, scale);
                y += graphHeight;
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
            // The requested overlay reports free / total physical RAM.
            snapshot.ramUsedBytes.value = static_cast<double>(memory.ullAvailPhys);
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
        while (!frameSamples_.empty() && now - frameSamples_.front().received > std::chrono::seconds(5)) {
            frameSamples_.pop_front();
        }
        const SteadyClock::time_point rollingStart = now - std::chrono::milliseconds(1000);
        double sum = 0.0;
        std::size_t rollingCount = 0U;
        for (const FrameSample& sample : frameSamples_) {
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

    void AddFrame(DWORD processId, double frameTime, DWORD foregroundProcessId) {
        if (processId == 0U || processId != foregroundProcessId) {
            return;
        }
        const SteadyClock::time_point now = SteadyClock::now();
        frameSamples_.push_back(FrameSample{frameTime, now});
        while (!frameSamples_.empty() && now - frameSamples_.front().received > std::chrono::seconds(5)) {
            frameSamples_.pop_front();
        }
        while (frameSamples_.size() > 360U) {
            frameSamples_.pop_front();
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

    std::wstring PresentMonPath() const {
        const std::wstring direct = JoinPath(baseDirectory_, L"PresentMon.exe");
        if (FileExists(direct)) {
            return direct;
        }
        const std::wstring fallback = JoinPath(baseDirectory_, L"third_party\\PresentMon\\PresentMon.exe");
        return FileExists(fallback) ? fallback : std::wstring();
    }

    void WorkerMain() {
        PresentMonCapture presentMon;
        PdhMetrics pdh;
        DxgiMetrics dxgi;
        ULARGE_INTEGER previousIdle{};
        ULARGE_INTEGER previousKernel{};
        ULARGE_INTEGER previousUser{};
        bool havePreviousCpu = false;
        bool telemetryOpen = false;
        bool wasVisible = false;
        DWORD foregroundProcess = 0U;
        SteadyClock::time_point nextMetric = SteadyClock::now();
        SteadyClock::time_point nextPresentMonStart = SteadyClock::now();

        HANDLE waitHandles[2]{stopEvent_, wakeEvent_};
        for (;;) {
            if (WaitForSingleObject(stopEvent_, 0U) == WAIT_OBJECT_0) {
                break;
            }
            const bool visible = visible_.load();
            if (!visible) {
                if (wasVisible) {
                    presentMon.Stop();
                    pdh.Close();
                    dxgi.Close();
                    telemetryOpen = false;
                    havePreviousCpu = false;
                    foregroundProcess = 0U;
                    frameSamples_.clear();
                    PublishSnapshot(Snapshot{});
                }
                wasVisible = false;
                const DWORD waitResult = WaitForMultipleObjects(2U, waitHandles, FALSE, INFINITE);
                if (waitResult == WAIT_OBJECT_0) {
                    break;
                }
                continue;
            }

            if (!wasVisible) {
                pdh.Open();
                dxgi.Open();
                telemetryOpen = true;
                nextMetric = SteadyClock::now();
                nextPresentMonStart = SteadyClock::now();
                wasVisible = true;
            }

            const SteadyClock::time_point now = SteadyClock::now();
            const DWORD newForegroundProcess = foregroundProcessId_.load();
            if (newForegroundProcess != foregroundProcess) {
                foregroundProcess = newForegroundProcess;
                frameSamples_.clear();
            }

            if (!presentMon.IsRunning()) {
                presentMon.Stop();
                frameSamples_.clear();
                if (now >= nextPresentMonStart) {
                    const std::wstring path = PresentMonPath();
                    if (!path.empty() && presentMon.Start(path)) {
                        nextPresentMonStart = now + std::chrono::seconds(2);
                    } else {
                        nextPresentMonStart = now + std::chrono::seconds(2);
                    }
                }
            }
            if (presentMon.IsRunning()) {
                presentMon.Drain([this, foregroundProcess](DWORD processId, double frameTime) {
                    AddFrame(processId, frameTime, foregroundProcess);
                });
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

        presentMon.Stop();
        pdh.Close();
        frameSamples_.clear();
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
    std::deque<FrameSample> frameSamples_;
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
