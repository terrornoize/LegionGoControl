#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include <windowsx.h>
#include <shellapi.h>
#include <commctrl.h>
#include <commdlg.h>
#include <tlhelp32.h>
#include <objbase.h>
#include <wbemidl.h>
#include <oleauto.h>

#include "LegionGoCore.h"
#include "LegionGoFrameLimiter.h"
#include "LegionGoOverlay.h"
#include "resource.h"

#if __has_include("build/LegionGoBuildVersion.h")
#include "build/LegionGoBuildVersion.h"
#else
#define LEGIONGO_COMMIT_DATE L"unknown-date"
#define LEGIONGO_COMMIT_SHORT L"local"
#endif

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <chrono>
#include <climits>
#include <cstdlib>
#include <cwctype>
#include <deque>
#include <fstream>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#pragma comment(lib, "user32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "comdlg32.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "oleaut32.lib")
#pragma comment(lib, "wbemuuid.lib")

namespace {

constexpr wchar_t APP_TITLE[] = L"LegionGoControl V2.1";
constexpr wchar_t APP_VERSION[] = L"LegionGoControl v2.1 " LEGIONGO_COMMIT_DATE L" #" LEGIONGO_COMMIT_SHORT;
constexpr wchar_t APP_VERSION_SHORT[] = L"v2.1 " LEGIONGO_COMMIT_DATE L" #" LEGIONGO_COMMIT_SHORT;
constexpr wchar_t REPOSITORY_URL[] = L"https://github.com/terrornoize/LegionGoControl";
constexpr wchar_t HIDDEN_CLASS[] = L"LegionGoControlHiddenWindow";
constexpr wchar_t SETTINGS_CLASS[] = L"LegionGoControlSettingsWindow";
constexpr wchar_t PROFILES_CLASS[] = L"LegionGoControlProfilesWindow";
constexpr wchar_t PROFILE_EDITOR_CLASS[] = L"LegionGoControlProfileEditor";
constexpr wchar_t FAN_CURVE_CLASS[] = L"LegionGoControlFanCurve";
constexpr wchar_t SINGLETON_MUTEX[] = L"Global\\LegionGoControlSingletonMutex";
constexpr wchar_t BACKEND_MUTEX[] = L"Global\\LegionGoControlBackendMutex";

constexpr UINT WMAPP_TRAY = WM_APP + 100;
constexpr UINT WMAPP_SHOW_SETTINGS = WM_APP + 101;
constexpr UINT WMAPP_WORKER_UPDATE = WM_APP + 102;
constexpr UINT WMAPP_FAN_CURVE_CHANGED = WM_APP + 103;
constexpr UINT_PTR FAN_TIMER_ID = 1;

constexpr UINT ID_TRAY_SETTINGS = 40001;
constexpr UINT ID_TRAY_PROFILES = 40002;
constexpr UINT ID_TRAY_EXIT = 40005;

constexpr int IDC_TAB = 5000;
constexpr int IDC_OK = 5001;
constexpr int IDC_CANCEL = 5002;
constexpr int IDC_APPLY = 5003;
constexpr int IDC_LOGGING = 5010;
constexpr int IDC_STARTUP = 5011;
constexpr int IDC_DEBOUNCE = 5012;
constexpr int IDC_COOLDOWN = 5013;
constexpr int IDC_DEBOUNCE_SPIN = 5018;
constexpr int IDC_COOLDOWN_SPIN = 5019;
constexpr int IDC_OPEN_LOG = 5015;
constexpr int IDC_BATTERY = 5016;
constexpr int IDC_BATTERY_STATUS = 5017;
constexpr int IDC_BRIGHTNESS = 5020;
constexpr int IDC_BRIGHTNESS_VALUE = 5021;
constexpr int IDC_BUTTON_SELECT = 5030;
constexpr int IDC_BUTTON_ENABLED = 5031;
constexpr int IDC_BUTTON_TRIGGER = 5032;
constexpr int IDC_BUTTON_ACTION = 5033;
constexpr int IDC_BUTTON_KEYS = 5034;
constexpr int IDC_BUTTON_PATH = 5035;
constexpr int IDC_BUTTON_ARGS = 5036;
constexpr int IDC_BUTTON_WORKDIR = 5037;
constexpr int IDC_BUTTON_INTERNAL = 5038;
constexpr int IDC_BUTTON_BROWSE = 5040;
constexpr int IDC_BASE_STAPM = 5050;
constexpr int IDC_BASE_FAST = 5051;
constexpr int IDC_BASE_SLOW = 5052;
constexpr int IDC_BASE_STAPM_SPIN = 5053;
constexpr int IDC_BASE_FAST_SPIN = 5054;
constexpr int IDC_BASE_SLOW_SPIN = 5055;
constexpr int IDC_TDP_PRESET_FIRST = 5060;
constexpr int IDC_MANAGE_PROFILES = 5070;
constexpr int IDC_TDP_STATUS = 5071;
constexpr int IDC_INFO_REPOSITORY = 5080;
constexpr int IDC_INFO_ICON_LICENSE = 5081;
constexpr int IDC_FAN_ENABLE = 5090;
constexpr int IDC_FAN_CURVE = 5091;
constexpr int IDC_FAN_STATUS = 5092;
constexpr int IDC_FAN_POINT = 5093;
constexpr int IDC_FAN_DUTY = 5094;
constexpr int IDC_FAN_DUTY_SPIN = 5095;
constexpr int IDC_FAN_APPLY = 5096;
constexpr int IDC_FAN_RESTORE = 5097;
constexpr int IDC_FAN_PROFILE = 5098;
constexpr int IDC_FAN_PROFILE_NEW = 5099;
constexpr int IDC_FAN_PROFILE_SAVE = 5161;
constexpr int IDC_FAN_PROFILE_DELETE = 5162;
constexpr int IDC_OVERLAY_ENABLE = 5200;
constexpr int IDC_OVERLAY_HOTKEY = 5201;
constexpr int IDC_OVERLAY_SCALE = 5202;
constexpr int IDC_OVERLAY_SCALE_VALUE = 5203;
constexpr int IDC_OVERLAY_OPACITY = 5204;
constexpr int IDC_OVERLAY_OPACITY_VALUE = 5205;
constexpr int IDC_OVERLAY_CORNER = 5206;
constexpr int IDC_OVERLAY_MARGIN_X = 5207;
constexpr int IDC_OVERLAY_MARGIN_X_SPIN = 5208;
constexpr int IDC_OVERLAY_MARGIN_Y = 5209;
constexpr int IDC_OVERLAY_MARGIN_Y_SPIN = 5210;
constexpr int IDC_OVERLAY_STATUS = 5211;
constexpr int IDC_OVERLAY_STYLE = 5212;
constexpr int IDC_OVERLAY_FPS_CAPTURE = 5213;
constexpr int IDC_FPS_LIMIT_ENABLE = 5220;
constexpr int IDC_FPS_LIMIT = 5221;
constexpr int IDC_FPS_LIMIT_VALUE = 5222;
constexpr int IDC_PE_FPS_ENABLE = 5223;
constexpr int IDC_PE_FPS_LIMIT = 5224;
constexpr int IDC_PE_FPS_VALUE = 5225;
constexpr int IDC_PROFILE_LIST = 5100;
constexpr int IDC_PROFILE_ADD = 5101;
constexpr int IDC_PROFILE_EDIT = 5102;
constexpr int IDC_PROFILE_REMOVE = 5103;
constexpr int IDC_PROFILE_UP = 5104;
constexpr int IDC_PROFILE_DOWN = 5105;
constexpr int IDC_PROFILE_CLOSE = 5106;
constexpr int IDC_PE_NAME = 5150;
constexpr int IDC_PE_PATH = 5151;
constexpr int IDC_PE_BROWSE = 5152;
constexpr int IDC_PE_STAPM = 5153;
constexpr int IDC_PE_FAST = 5154;
constexpr int IDC_PE_SLOW = 5155;
constexpr int IDC_PE_OK = 5156;
constexpr int IDC_PE_CANCEL = 5157;
constexpr int IDC_PE_STAPM_SPIN = 5158;
constexpr int IDC_PE_FAST_SPIN = 5159;
constexpr int IDC_PE_SLOW_SPIN = 5160;

HINSTANCE g_instance = nullptr;
HWND g_hidden = nullptr;
HWND g_settings = nullptr;
HWND g_profilesWindow = nullptr;
NOTIFYICONDATAW g_nid{};
HICON g_icon = nullptr;
HICON g_windowIcon = nullptr;
HFONT g_font = nullptr;
HANDLE g_singleton = nullptr;
UINT g_taskbarCreated = 0;

std::wstring g_exePath;
std::wstring g_baseDir;
std::wstring g_iniPath;
std::wstring g_logPath;
std::wstring g_backendPath;
std::wstring g_tdpStatePath;
std::wstring g_batteryStatePath;
std::wstring g_fanStatePath;
std::wstring g_fpsBackupPath;

std::atomic_bool g_logging{false};
int g_debounceMs = 40;
int g_actionCooldownMs = 250;
LegionGoCore::TdpTriple g_baseTdp{16, 20, 20};
bool g_fanEnabled = false;
LegionGoCore::FanCurve g_fanCurve{{44,48,48,51,51,55,60,71,87,87}};
LegionGoOverlay::Config g_overlayConfig{};
bool g_baseFpsLimitEnabled = false;
int g_baseFpsLimit = 60;
bool g_fpsLimiterReady = true;

std::wstring Trim(const std::wstring& value) {
    std::size_t first = 0;
    while (first < value.size() && (std::iswspace(value[first]) || value[first] == 0xFEFF)) ++first;
    std::size_t last = value.size();
    while (last > first && std::iswspace(value[last - 1])) --last;
    return value.substr(first, last - first);
}

std::wstring Upper(std::wstring value) {
    std::transform(value.begin(), value.end(), value.begin(), [](wchar_t c) {
        return static_cast<wchar_t>(std::towupper(c));
    });
    return value;
}

std::vector<std::wstring> Split(const std::wstring& value, wchar_t separator) {
    std::vector<std::wstring> result;
    std::wstringstream stream(value);
    std::wstring part;
    while (std::getline(stream, part, separator)) result.push_back(Trim(part));
    return result;
}

bool FileExists(const std::wstring& path) {
    const DWORD attributes = GetFileAttributesW(path.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES && !(attributes & FILE_ATTRIBUTE_DIRECTORY);
}

std::wstring DirectoryOf(const std::wstring& path) {
    const std::size_t at = path.find_last_of(L"\\/");
    return at == std::wstring::npos ? L"." : path.substr(0, at);
}

void InitializePaths() {
    std::vector<wchar_t> path(32768);
    const DWORD count = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
    g_exePath.assign(path.data(), count);
    g_baseDir = DirectoryOf(g_exePath);
    g_iniPath = g_baseDir + L"\\LegionGoControl.ini";
    g_logPath = g_baseDir + L"\\LegionGoControl.log";
    g_backendPath = g_baseDir + L"\\LegionGoNativeWmiProbe.exe";
    g_tdpStatePath = g_baseDir + L"\\LegionGoNativeWmiProbe_state.txt";
    g_batteryStatePath = g_baseDir + L"\\LegionGoBatteryLimitState.txt";
    g_fanStatePath = g_baseDir + L"\\LegionGoFanState.txt";
    g_fpsBackupPath = g_baseDir + L"\\LegionGoFpsBackup.ini";
}

std::wstring NowText() {
    SYSTEMTIME value{};
    GetLocalTime(&value);
    wchar_t text[64]{};
    swprintf_s(text, L"%04u-%02u-%02u %02u:%02u:%02u.%03u", value.wYear, value.wMonth,
               value.wDay, value.wHour, value.wMinute, value.wSecond, value.wMilliseconds);
    return text;
}

std::mutex g_logMutex;
void LogAlways(const std::wstring& text) {
    std::lock_guard<std::mutex> lock(g_logMutex);
    std::wofstream file(g_logPath.c_str(), std::ios::app);
    if (file) file << L"[" << NowText() << L"] " << text << L"\n";
}
void Log(const std::wstring& text) { if (g_logging.load()) LogAlways(text); }

std::wstring IniString(const wchar_t* section, const wchar_t* key, const wchar_t* fallback = L"") {
    std::vector<wchar_t> buffer(32768);
    GetPrivateProfileStringW(section, key, fallback, buffer.data(), static_cast<DWORD>(buffer.size()), g_iniPath.c_str());
    return Trim(buffer.data());
}
int IniInt(const wchar_t* section, const wchar_t* key, int fallback) {
    return static_cast<int>(GetPrivateProfileIntW(section, key, fallback, g_iniPath.c_str()));
}
void IniWrite(const wchar_t* section, const wchar_t* key, const std::wstring& value) {
    WritePrivateProfileStringW(section, key, value.c_str(), g_iniPath.c_str());
}
void IniWriteInt(const wchar_t* section, const wchar_t* key, int value) {
    IniWrite(section, key, std::to_wstring(value));
}

bool FpsBackupActive() {
    return GetPrivateProfileIntW(L"Backup", L"Active", 0, g_fpsBackupPath.c_str()) != 0;
}
bool WriteFpsBackup(bool active, bool enabled, int minFps, int maxFps) {
    const auto write = [&](const wchar_t* key, const std::wstring& value) {
        return WritePrivateProfileStringW(L"Backup", key, value.c_str(), g_fpsBackupPath.c_str()) != FALSE;
    };
    if (!write(L"Backend", L"Chill") || !write(L"Enabled", enabled ? L"1" : L"0") ||
        !write(L"MinFPS", std::to_wstring(minFps)) || !write(L"MaxFPS", std::to_wstring(maxFps)) ||
        !write(L"FPS", std::to_wstring(maxFps)) || !write(L"Active", active ? L"1" : L"0")) return false;
    // Flush is best-effort: the profile API may return FALSE when no cached
    // mapping exists even though all three writes above succeeded.
    WritePrivateProfileStringW(nullptr, nullptr, nullptr, g_fpsBackupPath.c_str());
    return true;
}
bool EnsureFpsBackup(std::wstring& error) {
    if (FpsBackupActive()) return true;
    const LegionGoFrameLimiter::State current = LegionGoFrameLimiter::Query();
    if (!current.available || !current.supported) { error = current.error; return false; }
    if (!WriteFpsBackup(true, current.enabled, current.minFps, current.maxFps)) { error = L"FPS backup could not be written"; return false; }
    return true;
}
bool ApplyFpsTarget(bool enabled, int fps, std::wstring& error) {
    if (!g_fpsLimiterReady) { error = L"FPS limiter disabled because crash recovery failed"; return false; }
    if (!EnsureFpsBackup(error)) return false;
    LegionGoFrameLimiter::State verified;
    if (!LegionGoFrameLimiter::Set(enabled, fps, verified)) { error = verified.error; return false; }
    error.clear(); return true;
}
bool RestoreFpsBackup(std::wstring& error) {
    if (!FpsBackupActive()) { error.clear(); return true; }
    wchar_t backendText[32]{}, enabledText[16]{}, legacyFpsText[32]{}, minFpsText[32]{}, maxFpsText[32]{};
    GetPrivateProfileStringW(L"Backup", L"Backend", L"", backendText, _countof(backendText), g_fpsBackupPath.c_str());
    GetPrivateProfileStringW(L"Backup", L"Enabled", L"", enabledText, _countof(enabledText), g_fpsBackupPath.c_str());
    GetPrivateProfileStringW(L"Backup", L"FPS", L"", legacyFpsText, _countof(legacyFpsText), g_fpsBackupPath.c_str());
    GetPrivateProfileStringW(L"Backup", L"MinFPS", L"", minFpsText, _countof(minFpsText), g_fpsBackupPath.c_str());
    GetPrivateProfileStringW(L"Backup", L"MaxFPS", L"", maxFpsText, _countof(maxFpsText), g_fpsBackupPath.c_str());
    const auto parseFps = [](const wchar_t* text, int& value) {
        wchar_t* end = nullptr; const long parsed = wcstol(text, &end, 10);
        if (end == text || *end != L'\0' || parsed < 1 || parsed > 1000) return false;
        value = static_cast<int>(parsed); return true;
    };
    const bool enabledValid = wcscmp(enabledText, L"0") == 0 || wcscmp(enabledText, L"1") == 0;
    const bool enabled = wcscmp(enabledText, L"1") == 0;
    int legacyFps = 0;
    if (backendText[0] == L'\0') {
        if (!enabledValid || !parseFps(legacyFpsText, legacyFps)) {
            error = L"Legacy FRTC backup is incomplete or invalid; Radeon state was not changed";
            return false;
        }
        bool restoredLegacy = LegionGoFrameLimiter::RestoreLegacyFrtc(enabled, legacyFps, error);
        if (!restoredLegacy) restoredLegacy = LegionGoFrameLimiter::RestoreLegacyFrtc(enabled, legacyFps, error);
        if (!restoredLegacy) return false;
        if (WritePrivateProfileStringW(L"Backup", L"Active", L"0", g_fpsBackupPath.c_str()) == FALSE) {
            error = L"Legacy FRTC restore verified but backup marker cleanup failed"; return false;
        }
        error.clear(); return true;
    }
    int minFps = 0, maxFps = 0;
    if (_wcsicmp(backendText, L"Chill") != 0 || !enabledValid ||
        !parseFps(minFpsText, minFps) || !parseFps(maxFpsText, maxFps) || minFps > maxFps) {
        error = L"FPS backup is incomplete or invalid; Radeon state was not changed";
        return false;
    }
    LegionGoFrameLimiter::State verified;
    bool restored = LegionGoFrameLimiter::SetState(enabled, minFps, maxFps, verified);
    if (!restored) restored = LegionGoFrameLimiter::SetState(enabled, minFps, maxFps, verified);
    if (!restored) { error = verified.error; return false; }
    if (!WriteFpsBackup(false, enabled, minFps, maxFps)) { error = L"FPS restore verified but backup marker cleanup failed"; return false; }
    error.clear(); return true;
}

void CreateDefaultConfiguration() {
    if (FileExists(g_iniPath)) return;
    IniWriteInt(L"General", L"logging", 0);
    IniWriteInt(L"General", L"debounce_ms", 40);
    IniWriteInt(L"General", L"action_cooldown_ms", 250);
    IniWriteInt(L"General", L"FpsLimitEnabled", 0);
    IniWriteInt(L"General", L"FpsLimit", 60);
    IniWriteInt(L"TDP", L"BaseStapmW", 16);
    IniWriteInt(L"TDP", L"BaseFastW", 20);
    IniWriteInt(L"TDP", L"BaseSlowW", 20);
    IniWriteInt(L"Fan", L"Enabled", 0);
    const int fanDefaults[] = {44,48,48,51,51,55,60,71,87,87};
    for (int index = 0; index < 10; ++index)
        IniWriteInt(L"Fan", (L"Duty" + std::to_wstring(index)).c_str(), fanDefaults[index]);
    IniWrite(L"FanProfiles", L"Order", L"");
    IniWriteInt(L"Overlay", L"VisibleAtStartup", 0);
    IniWriteInt(L"Overlay", L"FpsCaptureEnabled", 1);
    IniWriteInt(L"Overlay", L"FunctionKey", 10);
    IniWriteInt(L"Overlay", L"ScalePercent", 100);
    IniWriteInt(L"Overlay", L"OpacityPercent", 85);
    IniWriteInt(L"Overlay", L"Corner", 1);
    IniWriteInt(L"Overlay", L"MarginX", 20);
    IniWriteInt(L"Overlay", L"MarginY", 20);
    IniWriteInt(L"Overlay", L"Style", 0);

    struct DefaultButton { const wchar_t* name; int enabled; const wchar_t* action; const wchar_t* valueKey; const wchar_t* value; };
    const DefaultButton defaults[] = {
        {L"Menu", 1, L"keys", L"keys", L"WIN+CTRL+O"},
        {L"View", 1, L"keys", L"keys", L"WIN+TAB"},
        {L"Y1", 0, L"keys", L"keys", L"F13"},
        {L"Y2", 0, L"keys", L"keys", L"ALT+TAB"},
        {L"Y3", 0, L"launch", L"path", L""},
        {L"M2", 0, L"keys", L"keys", L"F14"},
        {L"M3", 1, L"internal", L"internal", L"show_tdp"}
    };
    for (const auto& item : defaults) {
        IniWriteInt(item.name, L"enabled", item.enabled);
        IniWrite(item.name, L"trigger", L"down");
        IniWrite(item.name, L"action", item.action);
        IniWrite(item.name, item.valueKey, item.value);
        if (std::wstring(item.action) == L"launch") {
            IniWrite(item.name, L"args", L"");
            IniWrite(item.name, L"working_dir", L"");
        }
    }
    // Keep the V1 custom keys readable and useful to older installations.
    IniWriteInt(L"TDP", L"CustomStapmW", 12);
    IniWriteInt(L"TDP", L"CustomFastW", 16);
    IniWriteInt(L"TDP", L"CustomSlowW", 16);
    IniWrite(L"GameProfiles", L"Order", L"");
    WritePrivateProfileStringW(nullptr, nullptr, nullptr, g_iniPath.c_str());
}

bool IsAdministrator() {
    BOOL result = FALSE;
    PSID group = nullptr;
    SID_IDENTIFIER_AUTHORITY authority = SECURITY_NT_AUTHORITY;
    if (AllocateAndInitializeSid(&authority, 2, SECURITY_BUILTIN_DOMAIN_RID,
                                 DOMAIN_ALIAS_RID_ADMINS, 0, 0, 0, 0, 0, 0, &group)) {
        CheckTokenMembership(nullptr, group, &result);
        FreeSid(group);
    }
    return result == TRUE;
}

bool RunHiddenCommand(const std::wstring& command, DWORD& exitCode, DWORD timeout = 30000) {
    exitCode = static_cast<DWORD>(-1);
    std::wstring commandLine = L"cmd.exe /C " + command;
    std::vector<wchar_t> mutableLine(commandLine.begin(), commandLine.end());
    mutableLine.push_back(0);
    STARTUPINFOW startup{};
    PROCESS_INFORMATION process{};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESHOWWINDOW;
    startup.wShowWindow = SW_HIDE;
    if (!CreateProcessW(nullptr, mutableLine.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW,
                        nullptr, g_baseDir.c_str(), &startup, &process)) return false;
    const DWORD wait = WaitForSingleObject(process.hProcess, timeout);
    if (wait == WAIT_OBJECT_0) GetExitCodeProcess(process.hProcess, &exitCode);
    else { TerminateProcess(process.hProcess, 99); exitCode = 99; }
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    return wait == WAIT_OBJECT_0;
}

bool IsStartupEnabled() {
    DWORD code = 1;
    return RunHiddenCommand(L"schtasks /Query /TN \"LegionGoControl\" >NUL 2>NUL", code) && code == 0;
}
bool SetStartupEnabled(bool enabled) {
    DWORD code = 1;
    if (!enabled) return RunHiddenCommand(L"schtasks /Delete /TN \"LegionGoControl\" /F", code) && code == 0;
    const std::wstring command = L"schtasks /Create /TN \"LegionGoControl\" /TR \"\\\"" +
        g_exePath + L"\\\"\" /SC ONLOGON /RL HIGHEST /F";
    return RunHiddenCommand(command, code) && code == 0;
}

void Message(HWND owner, const std::wstring& title, const std::wstring& text, UINT flags = MB_OK | MB_ICONINFORMATION) {
    MessageBoxW(owner ? owner : g_hidden, text.c_str(), title.c_str(), flags);
}
void Balloon(const std::wstring& text, DWORD icon = NIIF_INFO) {
    if (!g_hidden) return;
    NOTIFYICONDATAW copy = g_nid;
    copy.uFlags = NIF_INFO;
    copy.dwInfoFlags = icon;
    wcsncpy_s(copy.szInfoTitle, APP_TITLE, _TRUNCATE);
    wcsncpy_s(copy.szInfo, text.c_str(), _TRUNCATE);
    Shell_NotifyIconW(NIM_MODIFY, &copy);
}

bool ReadUtf16File(const std::wstring& path, std::wstring& output) {
    output.clear();
    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                              nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    const DWORD size = GetFileSize(file, nullptr);
    if (size == INVALID_FILE_SIZE || size == 0) { CloseHandle(file); return false; }
    std::vector<BYTE> bytes(size);
    DWORD count = 0;
    const BOOL ok = ReadFile(file, bytes.data(), size, &count, nullptr);
    CloseHandle(file);
    if (!ok || count < 2) return false;
    std::size_t offset = count >= 2 && bytes[0] == 0xFF && bytes[1] == 0xFE ? 2 : 0;
    const std::size_t characters = (count - offset) / sizeof(wchar_t);
    if (!characters) return false;
    output.assign(reinterpret_cast<const wchar_t*>(bytes.data() + offset), characters);
    return true;
}
std::wstring Field(const std::wstring& text, const std::wstring& key) {
    std::wstringstream stream(text);
    std::wstring line;
    const std::wstring prefix = key + L"=";
    while (std::getline(stream, line)) {
        line = Trim(line);
        if (line.compare(0, prefix.size(), prefix) == 0) return Trim(line.substr(prefix.size()));
    }
    return L"";
}

struct BatteryStatus { bool known = false; bool enabled = false; std::wstring error; };
BatteryStatus ReadBatteryState() {
    BatteryStatus result;
    std::wstring text;
    if (!ReadUtf16File(g_batteryStatePath, text)) { result.error = L"Battery status unavailable"; return result; }
    const std::wstring value = Upper(Field(text, L"BatteryLimit80"));
    if (value == L"TRUE" || value == L"1") { result.known = true; result.enabled = true; }
    else if (value == L"FALSE" || value == L"0") { result.known = true; }
    else result.error = Field(text, L"Error");
    return result;
}

struct FanStatus {
    bool known = false;
    bool backupActive = false;
    int rpm = 0;
    int temperatureC = 0;
    int fullSpeed = 0;
    LegionGoCore::FanCurve curve{};
    std::wstring error;
};
FanStatus ReadFanState() {
    FanStatus result; std::wstring text;
    if (!ReadUtf16File(g_fanStatePath, text)) { result.error = L"Fan telemetry unavailable"; return result; }
    result.known = Upper(Field(text, L"Known")) == L"TRUE";
    result.backupActive = Upper(Field(text, L"BackupActive")) == L"TRUE";
    if (!result.known) { result.error = Field(text, L"Error"); return result; }
    auto readInteger = [&](const std::wstring& key, int& value) {
        const std::wstring field = Field(text, key); if (field.empty()) return false;
        wchar_t* end = nullptr; const long parsed = wcstol(field.c_str(), &end, 10);
        if (!end || *end) return false; value = static_cast<int>(parsed); return true;
    };
    if (!readInteger(L"Rpm", result.rpm) || !readInteger(L"TemperatureC", result.temperatureC) ||
        !readInteger(L"FullSpeed", result.fullSpeed)) { result.known = false; result.error = L"Incomplete fan telemetry"; return result; }
    for (std::size_t index = 0; index < LegionGoCore::kFanPointCount; ++index)
        if (!readInteger(L"Speed" + std::to_wstring(index), result.curve.dutyPercent[index])) {
            result.known = false; result.error = L"Incomplete fan curve read-back"; return result;
        }
    if (!LegionGoCore::ValidateFanCurve(result.curve, &result.error)) result.known = false;
    return result;
}

bool RunBackend(const std::wstring& arguments, DWORD& exitCode, DWORD timeout = 7000) {
    static std::mutex inProcessBackendMutex;
    std::lock_guard<std::mutex> localLock(inProcessBackendMutex);
    exitCode = static_cast<DWORD>(-1);
    if (!FileExists(g_backendPath)) { LogAlways(L"Backend not found: " + g_backendPath); return false; }
    HANDLE crossProcessMutex = CreateMutexW(nullptr, FALSE, BACKEND_MUTEX);
    if (!crossProcessMutex) {
        LogAlways(L"Could not create backend mutex: " + std::to_wstring(GetLastError()));
        return false;
    }
    bool ownsCrossProcessMutex = false;
    const DWORD mutexWait = WaitForSingleObject(crossProcessMutex, 10000);
    ownsCrossProcessMutex = mutexWait == WAIT_OBJECT_0 || mutexWait == WAIT_ABANDONED;
    if (!ownsCrossProcessMutex) {
        CloseHandle(crossProcessMutex);
        LogAlways(L"Timed out waiting for backend mutex.");
        return false;
    }
    std::wstring commandLine = L"\"" + g_backendPath + L"\" " + arguments;
    std::vector<wchar_t> mutableLine(commandLine.begin(), commandLine.end());
    mutableLine.push_back(0);
    STARTUPINFOW startup{};
    PROCESS_INFORMATION process{};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESHOWWINDOW;
    startup.wShowWindow = SW_HIDE;
    const BOOL made = CreateProcessW(g_backendPath.c_str(), mutableLine.data(), nullptr, nullptr, FALSE,
                                     CREATE_NO_WINDOW, nullptr, g_baseDir.c_str(), &startup, &process);
    bool completed = false;
    if (made) {
        const DWORD wait = WaitForSingleObject(process.hProcess, timeout);
        if (wait == WAIT_OBJECT_0) { GetExitCodeProcess(process.hProcess, &exitCode); completed = true; }
        else {
            // Do not release the firmware mutex while this child can still be
            // running.  A process we created and can wait on is expected to be
            // terminable; if termination races natural exit, the wait still
            // provides the serialization barrier.
            TerminateProcess(process.hProcess, 99);
            WaitForSingleObject(process.hProcess, INFINITE);
            exitCode = 99;
        }
        CloseHandle(process.hThread);
        CloseHandle(process.hProcess);
    } else LogAlways(L"CreateProcess backend failed: " + std::to_wstring(GetLastError()));
    if (crossProcessMutex) {
        if (ownsCrossProcessMutex) ReleaseMutex(crossProcessMutex);
        CloseHandle(crossProcessMutex);
    }
    return completed;
}
bool BackendSetTdp(const LegionGoCore::TdpTriple& value, DWORD& code) {
    // The backend owns and publishes its state file.  Do not delete it outside
    // the backend mutex: another instance may legitimately be reading it.
    return RunBackend(L"set " + std::to_wstring(value.stapm) + L" " +
                      std::to_wstring(value.fast) + L" " + std::to_wstring(value.slow), code) && code == 0;
}
bool BackendSetFan(const LegionGoCore::FanCurve& curve, DWORD& code) {
    std::wstring arguments = L"fan-set";
    for (const int duty : curve.dutyPercent) arguments += L" " + std::to_wstring(duty);
    return RunBackend(arguments, code, 30000) && code == 0;
}
bool BackendFanCommand(const wchar_t* command, DWORD& code) {
    return RunBackend(command, code, 30000) && code == 0;
}

std::wstring HResultText(HRESULT value) {
    wchar_t text[32]{}; swprintf_s(text, L"0x%08X", static_cast<unsigned int>(value)); return text;
}
class BrightnessWmiSession final {
public:
    ~BrightnessWmiSession() {
        if (services_) services_->Release();
        if (locator_) locator_->Release();
        if (uninitialize_) CoUninitialize();
    }
    bool Open(std::wstring& error) {
        HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        if (SUCCEEDED(hr)) uninitialize_ = true;
        else if (hr != RPC_E_CHANGED_MODE) { error = L"Brightness COM initialization failed"; return false; }
        hr = CoInitializeSecurity(nullptr, -1, nullptr, nullptr, RPC_C_AUTHN_LEVEL_DEFAULT,
                                  RPC_C_IMP_LEVEL_IMPERSONATE, nullptr, EOAC_NONE, nullptr);
        if (hr == RPC_E_TOO_LATE) hr = S_OK;
        if (FAILED(hr)) { error = L"Brightness COM security initialization failed"; return false; }
        hr = CoCreateInstance(CLSID_WbemLocator, nullptr, CLSCTX_INPROC_SERVER, IID_IWbemLocator,
                              reinterpret_cast<void**>(&locator_));
        if (FAILED(hr) || !locator_) { error = L"Brightness WMI locator unavailable"; return false; }
        BSTR namespaceName = SysAllocString(L"ROOT\\WMI");
        hr = locator_->ConnectServer(namespaceName, nullptr, nullptr, nullptr, 0, nullptr, nullptr, &services_);
        SysFreeString(namespaceName);
        if (FAILED(hr) || !services_) { error = L"Brightness ROOT\\WMI connection failed"; return false; }
        hr = CoSetProxyBlanket(services_, RPC_C_AUTHN_WINNT, RPC_C_AUTHZ_NONE, nullptr,
                               RPC_C_AUTHN_LEVEL_CALL, RPC_C_IMP_LEVEL_IMPERSONATE, nullptr, EOAC_NONE);
        if (FAILED(hr)) { error = L"Brightness WMI proxy setup failed"; return false; }
        return true;
    }
    bool Read(int& brightness, std::wstring& error) {
        IWbemClassObject* instance = nullptr;
        if (!FirstInstance(L"SELECT * FROM WmiMonitorBrightness WHERE Active=True", instance, error)) return false;
        VARIANT value{}; VariantInit(&value);
        const HRESULT hr = instance->Get(L"CurrentBrightness", 0, &value, nullptr, nullptr);
        instance->Release();
        if (FAILED(hr)) { VariantClear(&value); error = L"Current screen brightness is unavailable"; return false; }
        if (value.vt == VT_UI1) brightness = value.bVal;
        else if (value.vt == VT_I4) brightness = value.lVal;
        else if (value.vt == VT_UI4) brightness = static_cast<int>(value.ulVal);
        else { VariantClear(&value); error = L"Unsupported screen brightness value"; return false; }
        VariantClear(&value);
        if (brightness < 0 || brightness > 100) { error = L"Screen brightness returned an invalid value"; return false; }
        return true;
    }
    bool Write(int brightness, std::wstring& error) {
        IWbemClassObject* instance = nullptr;
        if (!FirstInstance(L"SELECT * FROM WmiMonitorBrightnessMethods", instance, error)) return false;
        VARIANT path{}; VariantInit(&path);
        HRESULT hr = instance->Get(L"__PATH", 0, &path, nullptr, nullptr);
        instance->Release();
        if (FAILED(hr) || path.vt != VT_BSTR || !path.bstrVal) { VariantClear(&path); error = L"Brightness method path unavailable"; return false; }
        IWbemClassObject* methodClass = nullptr; IWbemClassObject* definition = nullptr; IWbemClassObject* input = nullptr; IWbemClassObject* output = nullptr;
        BSTR className = SysAllocString(L"WmiMonitorBrightnessMethods");
        hr = services_->GetObject(className, 0, nullptr, &methodClass, nullptr); SysFreeString(className);
        BSTR methodName = SysAllocString(L"WmiSetBrightness");
        if (SUCCEEDED(hr) && methodClass) hr = methodClass->GetMethod(methodName, 0, &definition, nullptr);
        if (SUCCEEDED(hr) && definition) hr = definition->SpawnInstance(0, &input);
        if (SUCCEEDED(hr) && input) {
            // WmiSetBrightness declares Timeout as CIM_UINT32, but the working
            // Windows WMI provider convention is a VT_UI1 value plus the
            // explicit CIM type (matching Microsoft's provider clients).
            VARIANT timeout{}; VariantInit(&timeout); timeout.vt = VT_UI1; timeout.bVal = 0;
            hr = input->Put(L"Timeout", 0, &timeout, CIM_UINT32); VariantClear(&timeout);
        }
        if (SUCCEEDED(hr) && input) {
            VARIANT level{}; VariantInit(&level); level.vt = VT_UI1; level.bVal = static_cast<BYTE>(brightness);
            hr = input->Put(L"Brightness", 0, &level, CIM_UINT8); VariantClear(&level);
        }
        if (SUCCEEDED(hr)) hr = services_->ExecMethod(path.bstrVal, methodName, 0, nullptr, input, &output, nullptr);
        SysFreeString(methodName); VariantClear(&path);
        if (output) output->Release(); if (input) input->Release(); if (definition) definition->Release(); if (methodClass) methodClass->Release();
        if (FAILED(hr)) { error = L"Windows could not set screen brightness (HRESULT " + HResultText(hr) + L")"; return false; }
        int actual = 0;
        if (!Read(actual, error)) return false;
        brightness_ = actual;
        return true;
    }
    int WrittenBrightness() const noexcept { return brightness_; }
private:
    bool FirstInstance(const wchar_t* queryText, IWbemClassObject*& instance, std::wstring& error) {
        instance = nullptr; IEnumWbemClassObject* enumerator = nullptr;
        BSTR language = SysAllocString(L"WQL"); BSTR query = SysAllocString(queryText);
        const HRESULT hr = services_->ExecQuery(language, query,
            WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY, nullptr, &enumerator);
        SysFreeString(language); SysFreeString(query);
        if (FAILED(hr) || !enumerator) { error = L"This display does not expose Windows brightness control"; return false; }
        ULONG returned = 0; const HRESULT next = enumerator->Next(5000, 1, &instance, &returned); enumerator->Release();
        if (FAILED(next) || returned == 0 || !instance) { error = L"No active brightness-capable display found"; return false; }
        return true;
    }
    IWbemLocator* locator_ = nullptr;
    IWbemServices* services_ = nullptr;
    bool uninitialize_ = false;
    int brightness_ = 0;
};
bool ReadScreenBrightness(int& value, std::wstring& error) {
    BrightnessWmiSession session; return session.Open(error) && session.Read(value, error);
}
bool WriteScreenBrightness(int requested, int& actual, std::wstring& error) {
    BrightnessWmiSession session;
    if (!session.Open(error) || !session.Write((std::max)(0, (std::min)(100, requested)), error)) return false;
    actual = session.WrittenBrightness(); return true;
}

// ---------- V1-compatible controller actions ----------
enum class ActionType { None, Keys, Launch, Internal };
struct ButtonBinding {
    std::wstring name;
    std::size_t byteIndex = 0;
    BYTE mask = 0;
    bool enabled = false;
    bool triggerDown = true;
    ActionType action = ActionType::None;
    std::wstring keys, path, args, workingDir, internal;
    bool pressed = false;
    ULONGLONG lastChange = 0, lastFire = 0;
};
std::vector<ButtonBinding> g_buttons;

ActionType ParseAction(const std::wstring& value) {
    const std::wstring upper = Upper(Trim(value));
    if (upper == L"KEYS") return ActionType::Keys;
    if (upper == L"LAUNCH") return ActionType::Launch;
    if (upper == L"INTERNAL") return ActionType::Internal;
    return ActionType::None;
}
const wchar_t* ActionName(ActionType value) {
    switch (value) { case ActionType::Keys: return L"keys"; case ActionType::Launch: return L"launch";
                     case ActionType::Internal: return L"internal"; default: return L"none"; }
}
void InitializeButtons() {
    g_buttons.clear();
    g_buttons.push_back({L"Y1", 20, 0x80});
    g_buttons.push_back({L"Y2", 20, 0x40});
    g_buttons.push_back({L"Y3", 20, 0x20});
    g_buttons.push_back({L"M2", 20, 0x08});
    g_buttons.push_back({L"M3", 20, 0x04});
    g_buttons.push_back({L"View", 18, 0x40});
    g_buttons.push_back({L"Menu", 18, 0x80});
}
void LoadButton(ButtonBinding& button) {
    const std::wstring legacy = IniString(L"Buttons", button.name.c_str());
    const std::wstring explicitAction = IniString(button.name.c_str(), L"action");
    if (!legacy.empty() && explicitAction.empty()) {
        const std::wstring upper = Upper(legacy);
        button.enabled = upper != L"NONE" && upper != L"NOOP" && upper != L"DISABLED";
        button.triggerDown = true; button.action = button.enabled ? ActionType::Keys : ActionType::None;
        button.keys = legacy; button.path.clear(); button.args.clear(); button.workingDir.clear(); button.internal.clear();
    } else {
        int enabled = 0; const wchar_t* action = L"none"; const wchar_t* keys = L""; const wchar_t* internal = L"";
        if (button.name == L"Menu") { enabled = 1; action = L"keys"; keys = L"WIN+CTRL+O"; }
        else if (button.name == L"View") { enabled = 1; action = L"keys"; keys = L"WIN+TAB"; }
        else if (button.name == L"M3") { enabled = 1; action = L"internal"; internal = L"show_tdp"; }
        else if (button.name == L"Y1") { action = L"keys"; keys = L"F13"; }
        else if (button.name == L"Y2") { action = L"keys"; keys = L"ALT+TAB"; }
        else if (button.name == L"M2") { action = L"keys"; keys = L"F14"; }
        button.enabled = IniInt(button.name.c_str(), L"enabled", enabled) != 0;
        button.triggerDown = Upper(IniString(button.name.c_str(), L"trigger", L"down")) != L"UP";
        button.action = ParseAction(IniString(button.name.c_str(), L"action", action));
        button.keys = IniString(button.name.c_str(), L"keys", keys);
        button.path = IniString(button.name.c_str(), L"path");
        button.args = IniString(button.name.c_str(), L"args");
        button.workingDir = IniString(button.name.c_str(), L"working_dir");
        button.internal = IniString(button.name.c_str(), L"internal", internal);
    }
    button.pressed = false; button.lastChange = 0; button.lastFire = 0;
}

struct StoredProfile { std::wstring id; LegionGoCore::GameProfile value; };
struct StoredFanProfile { std::wstring id; std::wstring name; LegionGoCore::FanCurve curve; };
std::vector<StoredProfile> g_profiles;
std::vector<StoredFanProfile> g_fanProfiles;
std::wstring g_selectedFanProfileId;
std::mutex g_configurationMutex;

std::wstring ProfileSection(const std::wstring& id) { return L"GameProfile." + id; }
std::wstring FanProfileSection(const std::wstring& id) { return L"FanProfile." + id; }
std::wstring NewGuid() {
    GUID value{};
    if (FAILED(CoCreateGuid(&value))) {
        LARGE_INTEGER counter{};
        QueryPerformanceCounter(&counter);
        value.Data1 = static_cast<unsigned long>(counter.QuadPart ^ GetTickCount64());
        value.Data2 = static_cast<unsigned short>(GetCurrentProcessId());
        value.Data3 = static_cast<unsigned short>(GetCurrentThreadId());
        for (int index = 0; index < 8; ++index) value.Data4[index] = static_cast<unsigned char>((counter.QuadPart >> (index * 8)) ^ (GetTickCount64() >> (index * 5)));
        value.Data3 = static_cast<unsigned short>((value.Data3 & 0x0FFFU) | 0x4000U);
        value.Data4[0] = static_cast<unsigned char>((value.Data4[0] & 0x3FU) | 0x80U);
    }
    wchar_t text[64]{};
    if (!StringFromGUID2(value, text, static_cast<int>(_countof(text)))) return L"{00000000-0000-4000-8000-000000000000}";
    return text;
}
bool HasId(const std::vector<StoredProfile>& profiles, const std::wstring& id) {
    return std::any_of(profiles.begin(), profiles.end(), [&](const StoredProfile& item) { return Upper(item.id) == Upper(id); });
}
StoredProfile ReadProfile(const std::wstring& id) {
    const std::wstring section = ProfileSection(id);
    StoredProfile result;
    result.id = id;
    result.value.name = IniString(section.c_str(), L"Name");
    result.value.path = IniString(section.c_str(), L"Path");
    result.value.tdp.stapm = IniInt(section.c_str(), L"StapmW", 16);
    result.value.tdp.fast = IniInt(section.c_str(), L"FastW", 20);
    result.value.tdp.slow = IniInt(section.c_str(), L"SlowW", 20);
    result.value.tdp = LegionGoCore::NormalizeTdpHierarchy(result.value.tdp);
    result.value.fpsLimitEnabled = IniInt(section.c_str(), L"FpsLimitEnabled", 0) != 0;
    result.value.fpsLimit = (std::max)(30, (std::min)(144, IniInt(section.c_str(), L"FpsLimit", 60)));
    return result;
}
std::vector<StoredProfile> LoadProfilesFromIni() {
    std::vector<StoredProfile> result;
    // Order is the authoritative index.  Unlisted Profile.* sections and all
    // of their keys remain untouched, rather than being adopted and rewritten.
    for (const auto& id : Split(IniString(L"GameProfiles", L"Order", IniString(L"Profiles", L"Order").c_str()), L',')) {
        if (!id.empty() && !HasId(result, id)) result.push_back(ReadProfile(id));
    }
    return result;
}
void WriteProfilesToIni(const std::vector<StoredProfile>& profiles) {
    std::wstring order;
    for (const auto& item : profiles) {
        if (!order.empty()) order += L",";
        order += item.id;
        const std::wstring section = ProfileSection(item.id);
        IniWrite(section.c_str(), L"Name", item.value.name);
        IniWrite(section.c_str(), L"Path", item.value.path);
        IniWriteInt(section.c_str(), L"StapmW", item.value.tdp.stapm);
        IniWriteInt(section.c_str(), L"FastW", item.value.tdp.fast);
        IniWriteInt(section.c_str(), L"SlowW", item.value.tdp.slow);
        IniWriteInt(section.c_str(), L"FpsLimitEnabled", item.value.fpsLimitEnabled ? 1 : 0);
        IniWriteInt(section.c_str(), L"FpsLimit", item.value.fpsLimit);
    }
    IniWrite(L"GameProfiles", L"Order", order);
    WritePrivateProfileStringW(nullptr, nullptr, nullptr, g_iniPath.c_str());
}
std::vector<LegionGoCore::GameProfile> CoreProfiles(const std::vector<StoredProfile>& source) {
    std::vector<LegionGoCore::GameProfile> result;
    result.reserve(source.size());
    for (const auto& item : source) result.push_back(item.value);
    return result;
}
bool HasFanId(const std::vector<StoredFanProfile>& profiles, const std::wstring& id) {
    return std::any_of(profiles.begin(), profiles.end(), [&](const StoredFanProfile& item) { return Upper(item.id) == Upper(id); });
}
std::vector<StoredFanProfile> LoadFanProfilesFromIni(const LegionGoCore::FanCurve& legacyCurve) {
    std::vector<StoredFanProfile> result;
    for (const auto& id : Split(IniString(L"FanProfiles", L"Order"), L',')) {
        if (id.empty() || HasFanId(result, id)) continue;
        const std::wstring section = FanProfileSection(id);
        StoredFanProfile item; item.id = id; item.name = IniString(section.c_str(), L"Name");
        for (std::size_t index = 0; index < LegionGoCore::kFanPointCount; ++index)
            item.curve.dutyPercent[index] = IniInt(section.c_str(), (L"Duty" + std::to_wstring(index)).c_str(), legacyCurve.dutyPercent[index]);
        if (!Trim(item.name).empty() && LegionGoCore::ValidateFanCurve(item.curve)) result.push_back(std::move(item));
    }
    if (result.empty()) result.push_back({NewGuid(), L"Default", legacyCurve});
    return result;
}
void WriteFanProfilesToIni(const std::vector<StoredFanProfile>& profiles, const std::wstring& selectedId) {
    std::wstring order;
    for (const auto& item : profiles) {
        if (!order.empty()) order += L",";
        order += item.id;
        const std::wstring section = FanProfileSection(item.id);
        IniWrite(section.c_str(), L"Name", item.name);
        for (std::size_t index = 0; index < LegionGoCore::kFanPointCount; ++index)
            IniWriteInt(section.c_str(), (L"Duty" + std::to_wstring(index)).c_str(), item.curve.dutyPercent[index]);
    }
    IniWrite(L"FanProfiles", L"Order", order);
    IniWrite(L"Fan", L"SelectedProfile", selectedId);
}

void LoadConfiguration() {
    CreateDefaultConfiguration();
    g_logging.store(IniInt(L"General", L"logging", IniInt(L"General", L"Logging", 0)) != 0);
    g_debounceMs = (std::max)(0, (std::min)(1000, IniInt(L"General", L"debounce_ms", 40)));
    g_actionCooldownMs = (std::max)(50, (std::min)(5000, IniInt(L"General", L"action_cooldown_ms",
                                                                    IniInt(L"General", L"ActionCooldownMs", 250))));
    const bool baseFpsLimitEnabled = IniInt(L"General", L"FpsLimitEnabled", 0) != 0;
    const int baseFpsLimit = (std::max)(30, (std::min)(144, IniInt(L"General", L"FpsLimit", 60)));
    LegionGoCore::TdpTriple base{
        IniInt(L"TDP", L"BaseStapmW", IniInt(L"General", L"BaseStapmW", IniInt(L"TDP", L"CustomStapmW", 16))),
        IniInt(L"TDP", L"BaseFastW", IniInt(L"General", L"BaseFastW", IniInt(L"TDP", L"CustomFastW", 20))),
        IniInt(L"TDP", L"BaseSlowW", IniInt(L"General", L"BaseSlowW", IniInt(L"TDP", L"CustomSlowW", 20)))};
    base = LegionGoCore::NormalizeTdpHierarchy(base);
    if (!LegionGoCore::ValidateTdpTriple(base)) base = {16, 20, 20};
    LegionGoCore::FanCurve fanCurve;
    const int fanFallbacks[] = {44,48,48,51,51,55,60,71,87,87};
    for (std::size_t index = 0; index < LegionGoCore::kFanPointCount; ++index)
        fanCurve.dutyPercent[index] = IniInt(L"Fan", (L"Duty" + std::to_wstring(index)).c_str(), fanFallbacks[index]);
    if (!LegionGoCore::ValidateFanCurve(fanCurve))
        fanCurve = LegionGoCore::FanCurve{{44,48,48,51,51,55,60,71,87,87}};
    const bool fanEnabled = IniInt(L"Fan", L"Enabled", 0) != 0;
    auto fanProfiles = LoadFanProfilesFromIni(fanCurve);
    std::wstring selectedFanProfileId = IniString(L"Fan", L"SelectedProfile");
    auto selectedFan = std::find_if(fanProfiles.begin(), fanProfiles.end(), [&](const StoredFanProfile& item) {
        return Upper(item.id) == Upper(selectedFanProfileId);
    });
    if (selectedFan == fanProfiles.end()) selectedFan = fanProfiles.begin();
    selectedFanProfileId = selectedFan->id;
    fanCurve = selectedFan->curve;
    LegionGoOverlay::Config overlay;
    overlay.enabledAtStartup = IniInt(L"Overlay", L"VisibleAtStartup", 0) != 0;
    overlay.fpsCaptureEnabled = IniInt(L"Overlay", L"FpsCaptureEnabled", 1) != 0;
    overlay.functionKey = (std::max)(1, (std::min)(24, IniInt(L"Overlay", L"FunctionKey", 10)));
    overlay.scalePercent = (std::max)(50, (std::min)(200, IniInt(L"Overlay", L"ScalePercent", 100)));
    overlay.opacityPercent = (std::max)(0, (std::min)(100, IniInt(L"Overlay", L"OpacityPercent", 85)));
    overlay.corner = (std::max)(0, (std::min)(3, IniInt(L"Overlay", L"Corner", 1)));
    overlay.marginX = (std::max)(0, (std::min)(500, IniInt(L"Overlay", L"MarginX", 20)));
    overlay.marginY = (std::max)(0, (std::min)(500, IniInt(L"Overlay", L"MarginY", 20)));
    overlay.layoutStyle = (std::max)(0, (std::min)(1, IniInt(L"Overlay", L"Style", 0)));
    if (g_buttons.empty()) InitializeButtons();
    for (auto& button : g_buttons) {
        const bool pressed = button.pressed;
        const ULONGLONG lastChange = button.lastChange;
        const ULONGLONG lastFire = button.lastFire;
        LoadButton(button);
        button.pressed = pressed;
        button.lastChange = lastChange;
        button.lastFire = lastFire;
    }
    const auto profiles = LoadProfilesFromIni();
    std::lock_guard<std::mutex> lock(g_configurationMutex);
    g_baseTdp = base;
    g_fanCurve = fanCurve;
    g_fanEnabled = fanEnabled;
    g_fanProfiles = std::move(fanProfiles);
    g_selectedFanProfileId = std::move(selectedFanProfileId);
    g_overlayConfig = overlay;
    g_baseFpsLimitEnabled = baseFpsLimitEnabled;
    g_baseFpsLimit = baseFpsLimit;
    g_profiles = profiles;
}

int VirtualKey(const std::wstring& raw) {
    const std::wstring value = Upper(Trim(raw));
    if (value == L"CTRL" || value == L"CONTROL") return VK_CONTROL;
    if (value == L"SHIFT") return VK_SHIFT;
    if (value == L"ALT") return VK_MENU;
    if (value == L"WIN" || value == L"WINDOWS" || value == L"META") return VK_LWIN;
    if (value == L"ENTER" || value == L"RETURN") return VK_RETURN;
    if (value == L"ESC" || value == L"ESCAPE") return VK_ESCAPE;
    if (value == L"TAB") return VK_TAB;
    if (value == L"SPACE") return VK_SPACE;
    if (value == L"BACKSPACE") return VK_BACK;
    if (value == L"DELETE" || value == L"DEL") return VK_DELETE;
    if (value == L"INSERT" || value == L"INS") return VK_INSERT;
    if (value == L"HOME") return VK_HOME;
    if (value == L"END") return VK_END;
    if (value == L"PGUP" || value == L"PAGEUP") return VK_PRIOR;
    if (value == L"PGDN" || value == L"PAGEDOWN") return VK_NEXT;
    if (value == L"UP") return VK_UP; if (value == L"DOWN") return VK_DOWN;
    if (value == L"LEFT") return VK_LEFT; if (value == L"RIGHT") return VK_RIGHT;
    if (value.size() == 1 && ((value[0] >= L'A' && value[0] <= L'Z') || (value[0] >= L'0' && value[0] <= L'9'))) return value[0];
    if (value.size() >= 2 && value[0] == L'F') { const int number = _wtoi(value.c_str() + 1); if (number >= 1 && number <= 24) return VK_F1 + number - 1; }
    return 0;
}
bool Modifier(int value) { return value == VK_CONTROL || value == VK_SHIFT || value == VK_MENU || value == VK_LWIN || value == VK_RWIN; }
void KeyEvent(WORD key, bool down) { INPUT input{}; input.type = INPUT_KEYBOARD; input.ki.wVk = key; input.ki.dwFlags = down ? 0 : KEYEVENTF_KEYUP; SendInput(1, &input, sizeof(input)); }
bool SendHotkey(const std::wstring& text) {
    const std::wstring upper = Upper(Trim(text));
    if (upper.empty() || upper == L"NONE" || upper == L"NOOP" || upper == L"DISABLED") return true;
    std::vector<int> modifiers, keys;
    for (const auto& token : Split(text, L'+')) {
        const int key = VirtualKey(token);
        if (!key) { Log(L"Unknown key token: " + token); return false; }
        (Modifier(key) ? modifiers : keys).push_back(key);
    }
    if (keys.empty()) return false;
    for (int key : modifiers) KeyEvent(static_cast<WORD>(key), true);
    for (int key : keys) KeyEvent(static_cast<WORD>(key), true);
    Sleep(20);
    for (auto at = keys.rbegin(); at != keys.rend(); ++at) KeyEvent(static_cast<WORD>(*at), false);
    for (auto at = modifiers.rbegin(); at != modifiers.rend(); ++at) KeyEvent(static_cast<WORD>(*at), false);
    return true;
}
void ShowSettings(int tab);
void Fire(ButtonBinding& button) {
    const ULONGLONG now = GetTickCount64();
    if (!button.enabled || now - button.lastFire < static_cast<ULONGLONG>(g_actionCooldownMs)) return;
    button.lastFire = now;
    if (button.action == ActionType::Keys) SendHotkey(button.keys);
    else if (button.action == ActionType::Launch && !Trim(button.path).empty()) {
        const std::wstring path = Trim(button.path);
        const std::wstring arguments = Trim(button.args);
        const std::wstring workingDirectory = Trim(button.workingDir);
        ShellExecuteW(nullptr, L"open", path.c_str(), arguments.empty() ? nullptr : arguments.c_str(),
                      workingDirectory.empty() ? nullptr : workingDirectory.c_str(), SW_SHOWNORMAL);
    } else if (button.action == ActionType::Internal) {
        const std::wstring action = Upper(Trim(button.internal));
        if (action == L"SHOW_MENU" || action == L"SHOW_TDP" || action == L"TDP" || action == L"SHOW_TDP_WINDOW")
            PostMessageW(g_hidden, WMAPP_SHOW_SETTINGS, 2, 0);
    }
}
void ProcessButton(ButtonBinding& button, bool down) {
    const ULONGLONG now = GetTickCount64();
    if (down == button.pressed) return;
    if (button.lastChange && now - button.lastChange < static_cast<ULONGLONG>(g_debounceMs)) return;
    button.pressed = down; button.lastChange = now;
    if ((down && button.triggerDown) || (!down && !button.triggerDown)) Fire(button);
}
void HandleRawInput(HRAWINPUT input) {
    UINT size = 0;
    if (GetRawInputData(input, RID_INPUT, nullptr, &size, sizeof(RAWINPUTHEADER)) != 0 || !size) return;
    std::vector<BYTE> data(size);
    if (GetRawInputData(input, RID_INPUT, data.data(), &size, sizeof(RAWINPUTHEADER)) != size) return;
    RAWINPUT* raw = reinterpret_cast<RAWINPUT*>(data.data());
    if (raw->header.dwType != RIM_TYPEHID || !raw->data.hid.dwCount || !raw->data.hid.dwSizeHid) return;
    for (UINT report = 0; report < raw->data.hid.dwCount; ++report) {
        const BYTE* bytes = raw->data.hid.bRawData + report * raw->data.hid.dwSizeHid;
        for (auto& button : g_buttons) if (button.byteIndex < raw->data.hid.dwSizeHid) ProcessButton(button, (bytes[button.byteIndex] & button.mask) != 0);
    }
}

// ---------- asynchronous profile/backend worker ----------
enum class WorkerJob { Wake, BatteryStatus, BatteryToggle, BrightnessStatus, BrightnessSet, FanStatus, FanApply, FanRestore, FanRecover };
struct RuntimeStatus {
    bool profileActive = false;
    std::wstring profileId, profileName;
    LegionGoCore::TdpTriple desired{16, 20, 20};
    bool applyKnown = false, applyOk = false;
    DWORD backendCode = 0;
    std::wstring error;
    BatteryStatus battery;
    unsigned batteryToggleSequence = 0;
    bool batteryToggleOk = true;
    bool brightnessKnown = false;
    int brightness = 50;
    std::wstring brightnessError;
    unsigned brightnessSetSequence = 0;
    bool brightnessSetOk = true;
    FanStatus fan;
    unsigned fanOperationSequence = 0;
    bool fanOperationOk = true;
    bool fpsLimitEnabled = false;
    int fpsLimit = 60;
    bool fpsLimitKnown = false;
    bool fpsLimitOk = true;
    std::wstring fpsLimitError;
};
std::mutex g_workerMutex;
std::condition_variable g_workerCondition;
std::deque<WorkerJob> g_workerJobs;
bool g_workerStop = false;
RuntimeStatus g_runtime;
std::atomic_int g_requestedBrightness{50};
std::thread g_worker;

ULONGLONG FileTimeValue(const FILETIME& value) {
    ULARGE_INTEGER result{}; result.LowPart = value.dwLowDateTime; result.HighPart = value.dwHighDateTime; return result.QuadPart;
}
bool EnumerateProcesses(std::vector<LegionGoCore::ProcessSample>& result) {
    std::vector<LegionGoCore::ProcessSample> current;
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) return false;
    PROCESSENTRY32W entry{}; entry.dwSize = sizeof(entry);
    if (!Process32FirstW(snapshot, &entry)) { CloseHandle(snapshot); return false; }
    do {
        HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, entry.th32ProcessID);
        if (!process) continue;
        FILETIME creation{}, exit{}, kernel{}, user{};
        if (!GetProcessTimes(process, &creation, &exit, &kernel, &user)) { CloseHandle(process); continue; }
        LegionGoCore::ProcessSample record;
        record.identity = {static_cast<std::uint32_t>(entry.th32ProcessID), FileTimeValue(creation)};
        record.parentPid = static_cast<std::uint32_t>(entry.th32ParentProcessID);
        std::vector<wchar_t> path(32768); DWORD count = static_cast<DWORD>(path.size());
        if (QueryFullProcessImageNameW(process, 0, path.data(), &count)) record.path.assign(path.data(), count);
        CloseHandle(process); current.push_back(std::move(record));
    } while (Process32NextW(snapshot, &entry));
    CloseHandle(snapshot); result.swap(current); return true;
}
void PublishRuntime(const RuntimeStatus& value) {
    { std::lock_guard<std::mutex> lock(g_workerMutex); g_runtime = value; }
    if (g_hidden) PostMessageW(g_hidden, WMAPP_WORKER_UPDATE, 0, 0);
}
void WorkerMain() {
    std::wstring activeId;
    bool haveAttemptedTarget = false;
    std::wstring attemptedIdentity;
    ULONGLONG lastAttemptTick = 0;
    std::vector<LegionGoCore::ProcessSample> processes;
    std::vector<std::wstring> activeProfilePaths;
    LegionGoCore::ProcessFamilyTracker familyTracker;
    unsigned consecutiveSnapshotFailures = 0;
    bool fpsTargetAttempted = false, fpsOwned = false;
    std::wstring attemptedFpsIdentity;
    ULONGLONG lastFpsAttemptTick = 0;
    RuntimeStatus status;
    for (;;) {
        std::deque<WorkerJob> jobs;
        {
            std::unique_lock<std::mutex> lock(g_workerMutex);
            g_workerCondition.wait_for(lock, std::chrono::milliseconds(750), [] { return g_workerStop || !g_workerJobs.empty(); });
            if (g_workerStop) break;
            jobs.swap(g_workerJobs);
        }
        bool stopRequested = false;
        for (WorkerJob job : jobs) {
            {
                std::lock_guard<std::mutex> lock(g_workerMutex);
                stopRequested = g_workerStop;
            }
            if (stopRequested) break;
            if (job == WorkerJob::BatteryStatus || job == WorkerJob::BatteryToggle) {
                DWORD code = 0;
                const bool ran = RunBackend(job == WorkerJob::BatteryToggle ? L"battery-toggle" : L"battery-status", code);
                if (ran && code == 0) {
                    status.battery = ReadBatteryState();
                } else {
                    status.battery = {};
                    status.battery.error = L"Battery backend failed (" + std::to_wstring(code) + L")";
                }
                if (job == WorkerJob::BatteryToggle) {
                    ++status.batteryToggleSequence;
                    status.batteryToggleOk = ran && code == 0;
                }
            } else if (job == WorkerJob::BrightnessStatus || job == WorkerJob::BrightnessSet) {
                int actual = status.brightness;
                std::wstring error;
                const bool ok = job == WorkerJob::BrightnessSet
                    ? WriteScreenBrightness(g_requestedBrightness.load(), actual, error)
                    : ReadScreenBrightness(actual, error);
                status.brightnessKnown = ok;
                status.brightness = actual;
                status.brightnessError = ok ? L"" : error;
                if (job == WorkerJob::BrightnessSet) { ++status.brightnessSetSequence; status.brightnessSetOk = ok; }
                if (!ok) LogAlways(L"Brightness operation failed: " + error);
            } else if (job == WorkerJob::FanStatus || job == WorkerJob::FanApply || job == WorkerJob::FanRestore || job == WorkerJob::FanRecover) {
                DWORD code = 0; bool ok = false;
                if (job == WorkerJob::FanApply) {
                    LegionGoCore::FanCurve curve;
                    { std::lock_guard<std::mutex> lock(g_configurationMutex); curve = g_fanCurve; }
                    ok = BackendSetFan(curve, code);
                } else {
                    ok = BackendFanCommand((job == WorkerJob::FanRestore || job == WorkerJob::FanRecover) ? L"fan-restore" : L"fan-status", code);
                }
                status.fan = ReadFanState();
                if (job != WorkerJob::FanStatus && job != WorkerJob::FanRecover) {
                    ++status.fanOperationSequence; status.fanOperationOk = ok;
                }
                if (!ok) LogAlways(L"Fan backend failed, code=" + std::to_wstring(code) + L" " + status.fan.error);
            }
        }
        if (stopRequested) break;
        LegionGoCore::TdpTriple base;
        bool baseFpsEnabled = false; int baseFps = 60;
        std::vector<StoredProfile> stored;
        { std::lock_guard<std::mutex> lock(g_configurationMutex);
          base = g_baseTdp; stored = g_profiles; baseFpsEnabled = g_baseFpsLimitEnabled; baseFps = g_baseFpsLimit; }
        auto profiles = CoreProfiles(stored);
        if (!LegionGoCore::ValidateGameProfiles(profiles).empty()) {
            // A malformed or ambiguous profile file must never reach firmware.
            Log(L"Profile configuration is invalid; using the base target.");
            stored.clear();
            profiles.clear();
            activeId.clear();
        }
        if (EnumerateProcesses(processes)) {
            consecutiveSnapshotFailures = 0;
            activeProfilePaths.clear();
            for (const std::size_t index : familyTracker.Update(processes, profiles, GetTickCount64())) {
                if (index < profiles.size()) activeProfilePaths.push_back(profiles[index].path);
            }
        } else {
            ++consecutiveSnapshotFailures;
            Log(L"Toolhelp snapshot failed; retaining the previous process-family sample.");
            // A transient failure must not cause a disruptive restore. After
            // repeated failures, fail safe to the base target.
            if (consecutiveSnapshotFailures >= 3) activeProfilePaths.clear();
        }
        std::size_t current = LegionGoCore::kNoProfile;
        for (std::size_t index = 0; index < stored.size(); ++index) if (stored[index].id == activeId) current = index;
        const std::size_t selected = LegionGoCore::ArbitrateProfile(profiles, activeProfilePaths, current);
        status.profileActive = selected < stored.size();
        status.profileId = status.profileActive ? stored[selected].id : L"";
        status.profileName = status.profileActive ? stored[selected].value.name : L"";
        status.desired = LegionGoCore::ResolveTdpTarget(base, profiles, selected).tdp;
        activeId = status.profileId;
        const std::wstring identity = (status.profileActive ? L"P:" + status.profileId : L"B:") +
            std::to_wstring(status.desired.stapm) + L"/" + std::to_wstring(status.desired.fast) + L"/" + std::to_wstring(status.desired.slow);
        const ULONGLONG now = GetTickCount64();
        const bool retryFailedTarget = haveAttemptedTarget && !status.applyOk && now - lastAttemptTick >= 10000;
        if (!haveAttemptedTarget || identity != attemptedIdentity || retryFailedTarget) {
            DWORD code = 0;
            status.applyKnown = true;
            status.applyOk = BackendSetTdp(status.desired, code);
            status.backendCode = code;
            status.error = status.applyOk ? L"" : L"TDP backend failed (" + std::to_wstring(code) + L")";
            haveAttemptedTarget = true;
            attemptedIdentity = identity;
            lastAttemptTick = now;
            Log(status.applyOk ? L"Applied TDP " + identity : status.error + L" for " + identity);
        }
        bool desiredFpsEnabled = baseFpsEnabled; int desiredFps = baseFps;
        if (selected < profiles.size() && profiles[selected].fpsLimitEnabled) {
            desiredFpsEnabled = true; desiredFps = profiles[selected].fpsLimit;
        }
        status.fpsLimitEnabled = desiredFpsEnabled; status.fpsLimit = desiredFps;
        const std::wstring fpsIdentity = std::wstring(desiredFpsEnabled ? L"1:" : L"0:") + std::to_wstring(desiredFps);
        const bool retryFps = fpsTargetAttempted && !status.fpsLimitOk && now - lastFpsAttemptTick >= 10000;
        const bool fpsBackupActive = FpsBackupActive();
        if ((desiredFpsEnabled || fpsOwned || fpsBackupActive) && (!fpsTargetAttempted || fpsIdentity != attemptedFpsIdentity || retryFps)) {
            std::wstring fpsError;
            status.fpsLimitKnown = true;
            if (desiredFpsEnabled) {
                status.fpsLimitOk = ApplyFpsTarget(true, desiredFps, fpsError);
                if (status.fpsLimitOk) fpsOwned = true;
            } else {
                // Relinquishing the limiter restores the exact Radeon state
                // captured before LegionGoControl took ownership.
                status.fpsLimitOk = RestoreFpsBackup(fpsError);
                if (status.fpsLimitOk) fpsOwned = false;
            }
            status.fpsLimitError = fpsError;
            fpsTargetAttempted = true; attemptedFpsIdentity = fpsIdentity; lastFpsAttemptTick = now;
            if (status.fpsLimitOk) Log(desiredFpsEnabled ? L"Applied FPS target " + fpsIdentity : L"Restored original Radeon FPS state");
            else LogAlways(L"FPS limiter failed: " + fpsError);
        }
        PublishRuntime(status);
    }
    // Restore fan state first and always: a crash-recovery backup can remain
    // active even when the desired INI preference has already been disabled.
    DWORD code = 0;
    if (!BackendFanCommand(L"fan-restore", code))
        LogAlways(L"Fan curve restore failed during shutdown; code=" + std::to_wstring(code));
    std::wstring fpsRestoreError;
    if (!RestoreFpsBackup(fpsRestoreError)) LogAlways(L"FPS limiter restore failed during shutdown: " + fpsRestoreError);
    // Always make the configured base the final clean-shutdown TDP target.
    LegionGoCore::TdpTriple base;
    { std::lock_guard<std::mutex> lock(g_configurationMutex); base = g_baseTdp; }
    bool restored = BackendSetTdp(base, code);
    if (!restored) restored = BackendSetTdp(base, code);
    if (!restored) LogAlways(L"Base TDP restore failed during shutdown; code=" + std::to_wstring(code));
    else Log(L"Base TDP restored during shutdown.");
}
void QueueWorker(WorkerJob job) {
    {
        std::lock_guard<std::mutex> lock(g_workerMutex);
        if (g_workerStop) return;
        // Slider movement can emit many notifications. Keep at most one queued
        // brightness write; it always consumes the latest atomic value.
        const bool coalesced = job == WorkerJob::BrightnessSet || job == WorkerJob::FanStatus || job == WorkerJob::FanApply;
        if (!coalesced || std::find(g_workerJobs.begin(), g_workerJobs.end(), job) == g_workerJobs.end())
            g_workerJobs.push_back(job);
    }
    g_workerCondition.notify_one();
}
void StartWorker() {
    { std::lock_guard<std::mutex> lock(g_workerMutex); g_workerStop = false;
      g_workerJobs.push_back(WorkerJob::BatteryStatus); g_workerJobs.push_back(WorkerJob::BrightnessStatus);
      g_workerJobs.push_back(g_fanEnabled ? WorkerJob::FanApply : WorkerJob::FanRecover); }
    g_worker = std::thread(WorkerMain);
}
void StopWorker() {
    { std::lock_guard<std::mutex> lock(g_workerMutex); g_workerStop = true; }
    g_workerCondition.notify_all();
    if (g_worker.joinable()) g_worker.join();
}
RuntimeStatus RuntimeSnapshot() { std::lock_guard<std::mutex> lock(g_workerMutex); return g_runtime; }

// ---------- UI helpers ----------
UINT SystemDpi() {
    using GetDpiForSystemFn = UINT (WINAPI*)();
    static const auto function = reinterpret_cast<GetDpiForSystemFn>(GetProcAddress(GetModuleHandleW(L"user32.dll"), "GetDpiForSystem"));
    if (function) return function();
    HDC screen = GetDC(nullptr); const UINT dpi = screen ? static_cast<UINT>(GetDeviceCaps(screen, LOGPIXELSX)) : 96U;
    if (screen) ReleaseDC(nullptr, screen); return dpi ? dpi : 96U;
}
UINT WindowDpi(HWND window) {
    using GetDpiForWindowFn = UINT (WINAPI*)(HWND);
    static const auto function = reinterpret_cast<GetDpiForWindowFn>(GetProcAddress(GetModuleHandleW(L"user32.dll"), "GetDpiForWindow"));
    const UINT dpi = function && window ? function(window) : SystemDpi(); return dpi ? dpi : 96U;
}
int DpiScale(int value, UINT dpi) { return MulDiv(value, static_cast<int>(dpi), 96); }
int DpiScale(HWND window, int value) { return DpiScale(value, WindowDpi(window)); }
void SetControlFont(HWND control) { if (control && g_font) SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(g_font), TRUE); }
HWND Control(HWND parent, const wchar_t* cls, const wchar_t* text, DWORD style, int x, int y, int w, int h, int id, DWORD ex = 0) {
    const UINT dpi = WindowDpi(parent);
    HWND result = CreateWindowExW(ex, cls, text, WS_CHILD | WS_VISIBLE | style,
                                  DpiScale(x, dpi), DpiScale(y, dpi), DpiScale(w, dpi), DpiScale(h, dpi), parent,
                                  reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), g_instance, nullptr);
    SetControlFont(result); return result;
}
void ConfigureSpinner(HWND spinner, HWND buddy, int minimum, int maximum) {
    SendMessageW(spinner, UDM_SETBUDDY, reinterpret_cast<WPARAM>(buddy), 0);
    SendMessageW(spinner, UDM_SETRANGE32, static_cast<WPARAM>(minimum), static_cast<LPARAM>(maximum));
}
void SetNumericValue(HWND parent, int editId, int spinnerId, int value) {
    SetWindowTextW(GetDlgItem(parent, editId), std::to_wstring(value).c_str());
    HWND spinner = GetDlgItem(parent, spinnerId);
    if (spinner) SendMessageW(spinner, UDM_SETPOS32, 0, static_cast<LPARAM>(value));
}
std::wstring WindowText(HWND control) {
    const int length = GetWindowTextLengthW(control);
    std::vector<wchar_t> text(static_cast<std::size_t>(length) + 1);
    GetWindowTextW(control, text.data(), length + 1); return text.data();
}
void SetText(HWND parent, int id, const std::wstring& value) { SetWindowTextW(GetDlgItem(parent, id), value.c_str()); }
bool ParseInteger(HWND parent, int id, int& value) {
    const std::wstring text = Trim(WindowText(GetDlgItem(parent, id)));
    if (text.empty()) return false;
    wchar_t* end = nullptr; const long parsed = wcstol(text.c_str(), &end, 10);
    if (!end || *end) return false; value = static_cast<int>(parsed); return true;
}
void CenterWindow(HWND window, HWND owner) {
    RECT wr{}, orc{}; GetWindowRect(window, &wr);
    if (!owner || !GetWindowRect(owner, &orc)) SystemParametersInfoW(SPI_GETWORKAREA, 0, &orc, 0);
    const int x = orc.left + ((orc.right - orc.left) - (wr.right - wr.left)) / 2;
    const int y = orc.top + ((orc.bottom - orc.top) - (wr.bottom - wr.top)) / 2;
    SetWindowPos(window, nullptr, x, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER);
}

constexpr int SETTINGS_PAGE_COUNT = 6;
struct SettingsState {
    int tab = 0, selectedButton = 0, selectedFanPoint = 4;
    bool loading = false;
    bool startup = false;
    bool normalizingTdp = false;
    bool normalizingFan = false;
    bool brightnessDragging = false;
    bool fanEnabled = false;
    bool baseFpsEnabled = false;
    int debounce = 40, cooldown = 250, baseFps = 60;
    int selectedFanProfile = 0;
    LegionGoCore::TdpTriple base;
    LegionGoCore::FanCurve fanDraft{{44,48,48,51,51,55,60,71,87,87}};
    LegionGoOverlay::Config overlay;
    std::vector<StoredFanProfile> fanProfiles;
    std::vector<ButtonBinding> buttons;
    std::vector<HWND> pages[SETTINGS_PAGE_COUNT];
};
SettingsState* SettingsData(HWND hwnd) { return reinterpret_cast<SettingsState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA)); }
void PageControl(SettingsState& state, int page, HWND control) { state.pages[page].push_back(control); }
void Label(SettingsState& state, int page, HWND parent, const wchar_t* text, int x, int y, int w, int h) {
    PageControl(state, page, Control(parent, L"STATIC", text, SS_LEFT, x, y, w, h, 0));
}
HWND PageField(SettingsState& state, int page, HWND parent, const wchar_t* cls, const wchar_t* text,
               DWORD style, int x, int y, int w, int h, int id, DWORD ex = 0) {
    HWND result = Control(parent, cls, text, style, x, y, w, h, id, ex); PageControl(state, page, result); return result;
}
void ShowSettingsPage(HWND hwnd, int page) {
    SettingsState* state = SettingsData(hwnd); if (!state) return;
    state->tab = (std::max)(0, (std::min)(SETTINGS_PAGE_COUNT - 1, page));
    TabCtrl_SetCurSel(GetDlgItem(hwnd, IDC_TAB), state->tab);
    for (int index = 0; index < SETTINGS_PAGE_COUNT; ++index) for (HWND control : state->pages[index]) ShowWindow(control, index == state->tab ? SW_SHOW : SW_HIDE);
}

void UpdateFanPointEditor(HWND hwnd) {
    SettingsState* state = SettingsData(hwnd); if (!state) return;
    const int index = state->selectedFanPoint;
    SetText(hwnd, IDC_FAN_POINT, L"Node " + std::to_wstring(index + 1) + L": " +
        std::to_wstring(LegionGoCore::kFanTemperaturesC[static_cast<size_t>(index)]) + L" C");
    state->normalizingFan = true;
    SetNumericValue(hwnd, IDC_FAN_DUTY, IDC_FAN_DUTY_SPIN, state->fanDraft.dutyPercent[static_cast<size_t>(index)]);
    state->normalizingFan = false;
}
void RefreshFanProfileCombo(HWND hwnd) {
    SettingsState* state = SettingsData(hwnd); if (!state) return;
    HWND combo = GetDlgItem(hwnd, IDC_FAN_PROFILE); if (!combo) return;
    const bool previousLoading = state->loading; state->loading = true;
    SendMessageW(combo, CB_RESETCONTENT, 0, 0);
    for (const auto& profile : state->fanProfiles)
        SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(profile.name.c_str()));
    if (!state->fanProfiles.empty()) {
        state->selectedFanProfile = (std::max)(0, (std::min)(static_cast<int>(state->fanProfiles.size()) - 1, state->selectedFanProfile));
        SendMessageW(combo, CB_SETCURSEL, state->selectedFanProfile, 0);
        SetWindowTextW(combo, state->fanProfiles[static_cast<std::size_t>(state->selectedFanProfile)].name.c_str());
    }
    state->loading = previousLoading;
}
void SelectFanProfile(HWND hwnd, int selected) {
    SettingsState* state = SettingsData(hwnd); if (!state || selected < 0 || selected >= static_cast<int>(state->fanProfiles.size())) return;
    if (state->selectedFanProfile >= 0 && state->selectedFanProfile < static_cast<int>(state->fanProfiles.size()))
        state->fanProfiles[static_cast<std::size_t>(state->selectedFanProfile)].curve = state->fanDraft;
    state->selectedFanProfile = selected;
    state->fanDraft = state->fanProfiles[static_cast<std::size_t>(selected)].curve;
    UpdateFanPointEditor(hwnd); InvalidateRect(GetDlgItem(hwnd, IDC_FAN_CURVE), nullptr, FALSE);
}
bool SaveCurrentFanProfile(HWND hwnd) {
    SettingsState* state = SettingsData(hwnd); if (!state || state->fanProfiles.empty()) return false;
    const std::wstring name = Trim(WindowText(GetDlgItem(hwnd, IDC_FAN_PROFILE)));
    if (name.empty()) { Message(hwnd, L"Fan profiles", L"Enter a profile name.", MB_OK | MB_ICONERROR); return false; }
    for (std::size_t index = 0; index < state->fanProfiles.size(); ++index) {
        if (static_cast<int>(index) != state->selectedFanProfile && Upper(state->fanProfiles[index].name) == Upper(name)) {
            Message(hwnd, L"Fan profiles", L"Profile names must be unique.", MB_OK | MB_ICONERROR); return false;
        }
    }
    auto& profile = state->fanProfiles[static_cast<std::size_t>(state->selectedFanProfile)];
    profile.name = name; profile.curve = state->fanDraft; RefreshFanProfileCombo(hwnd); return true;
}

int FanDutyFloor(int index) {
    if (index == 7) return 60;
    if (index == 8) return 80;
    if (index == 9) return 85;
    return 20;
}

LRESULT CALLBACK FanCurveProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    auto data = [&]() -> SettingsState* { return SettingsData(GetParent(hwnd)); };
    auto graphRect = [&](RECT client) {
        RECT graph{client.left + DpiScale(hwnd, 54), client.top + DpiScale(hwnd, 18),
                   client.right - DpiScale(hwnd, 24), client.bottom - DpiScale(hwnd, 42)};
        return graph;
    };
    auto pointPosition = [&](const RECT& graph, int index, int duty) {
        POINT point{};
        point.x = graph.left + MulDiv(LegionGoCore::kFanTemperaturesC[static_cast<size_t>(index)] - 10,
                                     graph.right - graph.left, 90);
        point.y = graph.bottom - MulDiv(duty - 20, graph.bottom - graph.top, 80);
        return point;
    };
    switch (message) {
        case WM_ERASEBKGND: return 1;
        case WM_GETDLGCODE: return DLGC_WANTARROWS | DLGC_WANTCHARS;
        case WM_PAINT: {
            PAINTSTRUCT paint{}; HDC target = BeginPaint(hwnd, &paint); RECT client{}; GetClientRect(hwnd, &client);
            HDC dc = CreateCompatibleDC(target); HBITMAP bitmap = CreateCompatibleBitmap(target, client.right, client.bottom);
            HGDIOBJ oldBitmap = SelectObject(dc, bitmap);
            HBRUSH background = CreateSolidBrush(RGB(247,248,250)); FillRect(dc, &client, background); DeleteObject(background);
            RECT graph = graphRect(client); HBRUSH graphBrush = CreateSolidBrush(RGB(31,36,43)); FillRect(dc, &graph, graphBrush); DeleteObject(graphBrush);
            SetBkMode(dc, TRANSPARENT); SelectObject(dc, g_font);
            SetTextColor(dc, RGB(70,74,80));
            RECT axisTitle{graph.left, client.top, graph.right, graph.top};
            DrawTextW(dc, L"Fan duty % / estimated RPM", -1, &axisTitle, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
            HPEN grid = CreatePen(PS_SOLID, 1, RGB(74,81,91)); HGDIOBJ oldPen = SelectObject(dc, grid);
            SetTextColor(dc, RGB(70,74,80));
            for (int duty = 20; duty <= 100; duty += 20) {
                const int y = graph.bottom - MulDiv(duty - 20, graph.bottom - graph.top, 80);
                MoveToEx(dc, graph.left, y, nullptr); LineTo(dc, graph.right, y);
                RECT label{client.left, y - DpiScale(hwnd, 12), graph.left - DpiScale(hwnd, 6), y + DpiScale(hwnd, 12)};
                const std::wstring text = std::to_wstring(duty) + L"%"; DrawTextW(dc, text.c_str(), -1, &label, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
            }
            for (int index = 0; index < 10; ++index) {
                const int x = graph.left + MulDiv(index, graph.right - graph.left, 9);
                MoveToEx(dc, x, graph.top, nullptr); LineTo(dc, x, graph.bottom);
                RECT label{x - DpiScale(hwnd, 30), graph.bottom + DpiScale(hwnd, 5), x + DpiScale(hwnd, 30), client.bottom};
                const std::wstring text = std::to_wstring((index + 1) * 10) + L"C"; DrawTextW(dc, text.c_str(), -1, &label, DT_CENTER | DT_TOP | DT_SINGLELINE);
            }
            DeleteObject(SelectObject(dc, oldPen));
            SettingsState* state = data();
            if (state) {
                HPEN curvePen = CreatePen(PS_SOLID, DpiScale(hwnd, 3), RGB(65,174,255)); oldPen = SelectObject(dc, curvePen);
                for (int index = 0; index < 10; ++index) {
                    const POINT point = pointPosition(graph, index, state->fanDraft.dutyPercent[static_cast<size_t>(index)]);
                    if (index == 0) MoveToEx(dc, point.x, point.y, nullptr); else LineTo(dc, point.x, point.y);
                }
                DeleteObject(SelectObject(dc, oldPen));
                for (int index = 0; index < 10; ++index) {
                    const int duty = state->fanDraft.dutyPercent[static_cast<size_t>(index)]; const POINT point = pointPosition(graph, index, duty);
                    const int radius = DpiScale(hwnd, index == state->selectedFanPoint ? 7 : 5);
                    HBRUSH node = CreateSolidBrush(index == state->selectedFanPoint ? RGB(255,166,61) : RGB(65,174,255));
                    HBRUSH old = static_cast<HBRUSH>(SelectObject(dc, node)); Ellipse(dc, point.x-radius, point.y-radius, point.x+radius, point.y+radius);
                    SelectObject(dc, old); DeleteObject(node);
                    SetTextColor(dc, RGB(238,242,247));
                    const int stagger = index % 2 ? DpiScale(hwnd, 18) : 0;
                    const int labelHeight = DpiScale(hwnd, 39);
                    int labelBottom = point.y - DpiScale(hwnd, 5) - stagger;
                    int labelTop = labelBottom - labelHeight;
                    if (labelTop < graph.top + DpiScale(hwnd, 2)) {
                        labelTop = point.y + DpiScale(hwnd, 5); labelBottom = labelTop + labelHeight;
                    }
                    RECT value{point.x-DpiScale(hwnd, 48), labelTop,
                               point.x+DpiScale(hwnd, 48), labelBottom};
                    const std::wstring label = std::to_wstring(duty) + L"%\r\n~" + std::to_wstring(LegionGoCore::EstimateFanRpm(duty)) + L" RPM";
                    DrawTextW(dc, label.c_str(), -1, &value, DT_CENTER);
                }
                const RuntimeStatus runtime = RuntimeSnapshot();
                if (runtime.fan.known) {
                    const int clampedTemperature = (std::max)(10, (std::min)(100, runtime.fan.temperatureC));
                    const int x = graph.left + MulDiv(clampedTemperature - 10, static_cast<int>(graph.right - graph.left), 90);
                    HPEN marker = CreatePen(PS_DOT, 1, RGB(255,93,93)); oldPen = SelectObject(dc, marker);
                    MoveToEx(dc, x, graph.top, nullptr); LineTo(dc, x, graph.bottom); DeleteObject(SelectObject(dc, oldPen));
                }
            }
            BitBlt(target, 0, 0, client.right, client.bottom, dc, 0, 0, SRCCOPY);
            SelectObject(dc, oldBitmap); DeleteObject(bitmap); DeleteDC(dc); EndPaint(hwnd, &paint); return 0;
        }
        case WM_LBUTTONDOWN: {
            SettingsState* state = data(); if (!state) return 0; SetFocus(hwnd);
            RECT client{}, graph{}; GetClientRect(hwnd, &client); graph = graphRect(client);
            const POINT mouse{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)}; int best = -1, distance = INT_MAX;
            for (int index = 0; index < 10; ++index) {
                POINT point = pointPosition(graph, index, state->fanDraft.dutyPercent[static_cast<size_t>(index)]);
                const int value = abs(point.x-mouse.x) + abs(point.y-mouse.y); if (value < distance) { distance=value; best=index; }
            }
            if (best >= 0 && distance <= DpiScale(hwnd, 28)) { state->selectedFanPoint=best; SetCapture(hwnd); UpdateFanPointEditor(GetParent(hwnd)); InvalidateRect(hwnd,nullptr,FALSE); }
            return 0;
        }
        case WM_MOUSEMOVE: {
            if (GetCapture() != hwnd) return 0; SettingsState* state = data(); if (!state) return 0;
            RECT client{}, graph{}; GetClientRect(hwnd,&client); graph=graphRect(client);
            const int index=state->selectedFanPoint; const int y=GET_Y_LPARAM(lParam);
            const int graphHeight = (std::max)(1, static_cast<int>(graph.bottom - graph.top));
            int duty = 20 + MulDiv(static_cast<int>(graph.bottom) - y, 80, graphHeight);
            const int lower=(std::max)(FanDutyFloor(index), index ? state->fanDraft.dutyPercent[static_cast<size_t>(index-1)] : 20);
            const int upper=index<9 ? state->fanDraft.dutyPercent[static_cast<size_t>(index+1)] : 100;
            duty=(std::max)(lower,(std::min)(upper,duty)); state->fanDraft.dutyPercent[static_cast<size_t>(index)]=duty;
            UpdateFanPointEditor(GetParent(hwnd)); InvalidateRect(hwnd,nullptr,FALSE); PostMessageW(GetParent(hwnd),WMAPP_FAN_CURVE_CHANGED,0,0); return 0;
        }
        case WM_LBUTTONUP: if (GetCapture()==hwnd) ReleaseCapture(); return 0;
        case WM_KEYDOWN: {
            SettingsState* state=data(); if(!state) return 0;
            if (wParam == VK_LEFT || wParam == VK_RIGHT) {
                state->selectedFanPoint = (std::max)(0, (std::min)(9, state->selectedFanPoint + (wParam == VK_RIGHT ? 1 : -1)));
                UpdateFanPointEditor(GetParent(hwnd)); InvalidateRect(hwnd, nullptr, FALSE); return 0;
            }
            if (wParam!=VK_UP && wParam!=VK_DOWN) return 0;
            const int index=state->selectedFanPoint; int value=state->fanDraft.dutyPercent[static_cast<size_t>(index)]+(wParam==VK_UP?1:-1);
            const int lower=(std::max)(FanDutyFloor(index),index?state->fanDraft.dutyPercent[static_cast<size_t>(index-1)]:20);
            const int upper=index<9?state->fanDraft.dutyPercent[static_cast<size_t>(index+1)]:100;
            state->fanDraft.dutyPercent[static_cast<size_t>(index)]=(std::max)(lower,(std::min)(upper,value));
            UpdateFanPointEditor(GetParent(hwnd));InvalidateRect(hwnd,nullptr,FALSE);return 0;
        }
        default: return DefWindowProcW(hwnd,message,wParam,lParam);
    }
}

void UpdateControllerActionFields(HWND hwnd) {
    const LRESULT selection = SendDlgItemMessageW(hwnd, IDC_BUTTON_ACTION, CB_GETCURSEL, 0, 0);
    const ActionType action = selection >= 0 && selection <= 3 ? static_cast<ActionType>(selection) : ActionType::None;
    EnableWindow(GetDlgItem(hwnd, IDC_BUTTON_KEYS), action == ActionType::Keys);
    EnableWindow(GetDlgItem(hwnd, IDC_BUTTON_PATH), action == ActionType::Launch);
    EnableWindow(GetDlgItem(hwnd, IDC_BUTTON_ARGS), action == ActionType::Launch);
    EnableWindow(GetDlgItem(hwnd, IDC_BUTTON_WORKDIR), action == ActionType::Launch);
    EnableWindow(GetDlgItem(hwnd, IDC_BUTTON_BROWSE), action == ActionType::Launch);
    EnableWindow(GetDlgItem(hwnd, IDC_BUTTON_INTERNAL), action == ActionType::Internal);
}
void LoadControllerEditor(HWND hwnd) {
    SettingsState* state = SettingsData(hwnd); if (!state || state->buttons.empty()) return;
    state->loading = true;
    const ButtonBinding& button = state->buttons[static_cast<std::size_t>(state->selectedButton)];
    SendDlgItemMessageW(hwnd, IDC_BUTTON_ENABLED, BM_SETCHECK, button.enabled ? BST_CHECKED : BST_UNCHECKED, 0);
    SendDlgItemMessageW(hwnd, IDC_BUTTON_TRIGGER, CB_SETCURSEL, button.triggerDown ? 0 : 1, 0);
    SendDlgItemMessageW(hwnd, IDC_BUTTON_ACTION, CB_SETCURSEL, static_cast<int>(button.action), 0);
    SetText(hwnd, IDC_BUTTON_KEYS, button.keys); SetText(hwnd, IDC_BUTTON_PATH, button.path);
    SetText(hwnd, IDC_BUTTON_ARGS, button.args); SetText(hwnd, IDC_BUTTON_WORKDIR, button.workingDir);
    SetText(hwnd, IDC_BUTTON_INTERNAL, button.internal);
    state->loading = false;
    UpdateControllerActionFields(hwnd);
}
void NormalizeTdpControls(HWND hwnd, int stapmId, int slowId, int fastId,
                          int stapmSpinId, int slowSpinId, int fastSpinId) {
    int stapm = 0, slow = 0, fast = 0;
    if (!ParseInteger(hwnd, stapmId, stapm) || !ParseInteger(hwnd, slowId, slow) || !ParseInteger(hwnd, fastId, fast)) return;
    if (stapm < 5 || stapm > 35 || slow < 5 || slow > 35 || fast < 5 || fast > 35) return;
    const LegionGoCore::TdpTriple normalized = LegionGoCore::NormalizeTdpHierarchy({stapm, fast, slow});
    SetNumericValue(hwnd, stapmId, stapmSpinId, normalized.stapm);
    SetNumericValue(hwnd, slowId, slowSpinId, normalized.slow);
    SetNumericValue(hwnd, fastId, fastSpinId, normalized.fast);
}
void NormalizeSettingsTdp(HWND hwnd) {
    SettingsState* state = SettingsData(hwnd); if (!state || state->normalizingTdp) return;
    state->normalizingTdp = true;
    NormalizeTdpControls(hwnd, IDC_BASE_STAPM, IDC_BASE_SLOW, IDC_BASE_FAST,
                         IDC_BASE_STAPM_SPIN, IDC_BASE_SLOW_SPIN, IDC_BASE_FAST_SPIN);
    state->normalizingTdp = false;
}
void StoreControllerEditor(HWND hwnd) {
    SettingsState* state = SettingsData(hwnd); if (!state || state->loading || state->buttons.empty()) return;
    ButtonBinding& button = state->buttons[static_cast<std::size_t>(state->selectedButton)];
    button.enabled = SendDlgItemMessageW(hwnd, IDC_BUTTON_ENABLED, BM_GETCHECK, 0, 0) == BST_CHECKED;
    button.triggerDown = SendDlgItemMessageW(hwnd, IDC_BUTTON_TRIGGER, CB_GETCURSEL, 0, 0) != 1;
    const LRESULT action = SendDlgItemMessageW(hwnd, IDC_BUTTON_ACTION, CB_GETCURSEL, 0, 0);
    button.action = action >= 0 && action <= 3 ? static_cast<ActionType>(action) : ActionType::None;
    button.keys = WindowText(GetDlgItem(hwnd, IDC_BUTTON_KEYS)); button.path = WindowText(GetDlgItem(hwnd, IDC_BUTTON_PATH));
    button.args = WindowText(GetDlgItem(hwnd, IDC_BUTTON_ARGS)); button.workingDir = WindowText(GetDlgItem(hwnd, IDC_BUTTON_WORKDIR));
    button.internal = WindowText(GetDlgItem(hwnd, IDC_BUTTON_INTERNAL));
}
void PopulateSettings(HWND hwnd) {
    SettingsState* state = SettingsData(hwnd); if (!state) return;
    LoadConfiguration();
    state->loading = true; state->buttons = g_buttons; state->selectedButton = 0;
    state->debounce = g_debounceMs; state->cooldown = g_actionCooldownMs;
    { std::lock_guard<std::mutex> lock(g_configurationMutex);
      state->base = g_baseTdp; state->fanDraft = g_fanCurve; state->fanEnabled = g_fanEnabled;
      state->baseFpsEnabled = g_baseFpsLimitEnabled; state->baseFps = g_baseFpsLimit;
      state->fanProfiles = g_fanProfiles; state->overlay = g_overlayConfig;
      state->selectedFanProfile = 0;
      for (std::size_t index = 0; index < state->fanProfiles.size(); ++index)
          if (Upper(state->fanProfiles[index].id) == Upper(g_selectedFanProfileId)) state->selectedFanProfile = static_cast<int>(index);
    }
    state->startup = IsStartupEnabled();
    SendDlgItemMessageW(hwnd, IDC_LOGGING, BM_SETCHECK, g_logging.load() ? BST_CHECKED : BST_UNCHECKED, 0);
    SendDlgItemMessageW(hwnd, IDC_STARTUP, BM_SETCHECK, state->startup ? BST_CHECKED : BST_UNCHECKED, 0);
    SetNumericValue(hwnd, IDC_DEBOUNCE, IDC_DEBOUNCE_SPIN, state->debounce);
    SetNumericValue(hwnd, IDC_COOLDOWN, IDC_COOLDOWN_SPIN, state->cooldown);
    SendDlgItemMessageW(hwnd, IDC_FPS_LIMIT_ENABLE, BM_SETCHECK, state->baseFpsEnabled ? BST_CHECKED : BST_UNCHECKED, 0);
    SendDlgItemMessageW(hwnd, IDC_FPS_LIMIT, TBM_SETPOS, TRUE, state->baseFps);
    SetText(hwnd, IDC_FPS_LIMIT_VALUE, std::to_wstring(state->baseFps) + L" FPS");
    HWND choices = GetDlgItem(hwnd, IDC_BUTTON_SELECT); SendMessageW(choices, CB_RESETCONTENT, 0, 0);
    for (const auto& button : state->buttons) SendMessageW(choices, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(button.name.c_str()));
    SendMessageW(choices, CB_SETCURSEL, 0, 0);
    SetNumericValue(hwnd, IDC_BASE_STAPM, IDC_BASE_STAPM_SPIN, state->base.stapm);
    SetNumericValue(hwnd, IDC_BASE_FAST, IDC_BASE_FAST_SPIN, state->base.fast);
    SetNumericValue(hwnd, IDC_BASE_SLOW, IDC_BASE_SLOW_SPIN, state->base.slow);
    SendDlgItemMessageW(hwnd, IDC_FAN_ENABLE, BM_SETCHECK, state->fanEnabled ? BST_CHECKED : BST_UNCHECKED, 0);
    RefreshFanProfileCombo(hwnd);
    SendDlgItemMessageW(hwnd, IDC_OVERLAY_ENABLE, BM_SETCHECK, LegionGoOverlay::IsVisible() ? BST_CHECKED : BST_UNCHECKED, 0);
    SendDlgItemMessageW(hwnd, IDC_OVERLAY_FPS_CAPTURE, BM_SETCHECK, state->overlay.fpsCaptureEnabled ? BST_CHECKED : BST_UNCHECKED, 0);
    SendDlgItemMessageW(hwnd, IDC_OVERLAY_HOTKEY, CB_SETCURSEL, state->overlay.functionKey - 1, 0);
    SendDlgItemMessageW(hwnd, IDC_OVERLAY_SCALE, TBM_SETPOS, TRUE, state->overlay.scalePercent);
    SendDlgItemMessageW(hwnd, IDC_OVERLAY_OPACITY, TBM_SETPOS, TRUE, state->overlay.opacityPercent);
    SendDlgItemMessageW(hwnd, IDC_OVERLAY_CORNER, CB_SETCURSEL, state->overlay.corner, 0);
    SendDlgItemMessageW(hwnd, IDC_OVERLAY_STYLE, CB_SETCURSEL, state->overlay.layoutStyle, 0);
    SetNumericValue(hwnd, IDC_OVERLAY_MARGIN_X, IDC_OVERLAY_MARGIN_X_SPIN, state->overlay.marginX);
    SetNumericValue(hwnd, IDC_OVERLAY_MARGIN_Y, IDC_OVERLAY_MARGIN_Y_SPIN, state->overlay.marginY);
    SetText(hwnd, IDC_OVERLAY_SCALE_VALUE, std::to_wstring(state->overlay.scalePercent) + L"%");
    SetText(hwnd, IDC_OVERLAY_OPACITY_VALUE, std::to_wstring(state->overlay.opacityPercent) + L"%");
    UpdateFanPointEditor(hwnd); InvalidateRect(GetDlgItem(hwnd, IDC_FAN_CURVE), nullptr, FALSE);
    state->loading = false; LoadControllerEditor(hwnd);
}
bool ApplySettings(HWND hwnd) {
    SettingsState* state = SettingsData(hwnd); if (!state) return false;
    StoreControllerEditor(hwnd);
    int debounce = 0, cooldown = 0;
    NormalizeSettingsTdp(hwnd);
    LegionGoCore::TdpTriple base;
    if (!ParseInteger(hwnd, IDC_DEBOUNCE, debounce) || debounce < 0 || debounce > 1000) {
        Message(hwnd, L"Settings", L"Debounce must be between 0 and 1000 ms.", MB_OK | MB_ICONERROR); ShowSettingsPage(hwnd, 0); return false;
    }
    if (!ParseInteger(hwnd, IDC_COOLDOWN, cooldown) || cooldown < 50 || cooldown > 5000) {
        Message(hwnd, L"Settings", L"Action cooldown must be between 50 and 5000 ms.", MB_OK | MB_ICONERROR); ShowSettingsPage(hwnd, 0); return false;
    }
    const bool baseFpsEnabled = SendDlgItemMessageW(hwnd, IDC_FPS_LIMIT_ENABLE, BM_GETCHECK, 0, 0) == BST_CHECKED;
    const int baseFps = static_cast<int>(SendDlgItemMessageW(hwnd, IDC_FPS_LIMIT, TBM_GETPOS, 0, 0));
    if (baseFps < 30 || baseFps > 144) {
        Message(hwnd, L"FPS limiter", L"FPS limit must be between 30 and 144.", MB_OK | MB_ICONERROR); ShowSettingsPage(hwnd, 0); return false;
    }
    if (!ParseInteger(hwnd, IDC_BASE_STAPM, base.stapm) || !ParseInteger(hwnd, IDC_BASE_SLOW, base.slow) || !ParseInteger(hwnd, IDC_BASE_FAST, base.fast)) {
        Message(hwnd, L"Settings", L"All base TDP values must be whole numbers.", MB_OK | MB_ICONERROR); ShowSettingsPage(hwnd, 2); return false;
    }
    std::wstring error;
    if (!LegionGoCore::ValidateTdpTriple(base, &error)) { Message(hwnd, L"Settings", error, MB_OK | MB_ICONERROR); ShowSettingsPage(hwnd, 2); return false; }
    int selectedFanDuty = 0;
    if (!ParseInteger(hwnd, IDC_FAN_DUTY, selectedFanDuty) || selectedFanDuty < 20 || selectedFanDuty > 100) {
        Message(hwnd, L"Fan curve", L"Fan duty must be a whole percentage between 20 and 100.", MB_OK | MB_ICONERROR);
        ShowSettingsPage(hwnd, 3); return false;
    }
    state->fanDraft.dutyPercent[static_cast<size_t>(state->selectedFanPoint)] = selectedFanDuty;
    if (!LegionGoCore::ValidateFanCurve(state->fanDraft, &error)) { Message(hwnd, L"Fan curve", error, MB_OK | MB_ICONERROR); ShowSettingsPage(hwnd, 3); return false; }
    if (!SaveCurrentFanProfile(hwnd)) { ShowSettingsPage(hwnd, 3); return false; }
    const bool fanEnabled = SendDlgItemMessageW(hwnd, IDC_FAN_ENABLE, BM_GETCHECK, 0, 0) == BST_CHECKED;
    LegionGoOverlay::Config overlay;
    overlay.enabledAtStartup = SendDlgItemMessageW(hwnd, IDC_OVERLAY_ENABLE, BM_GETCHECK, 0, 0) == BST_CHECKED;
    overlay.fpsCaptureEnabled = SendDlgItemMessageW(hwnd, IDC_OVERLAY_FPS_CAPTURE, BM_GETCHECK, 0, 0) == BST_CHECKED;
    overlay.functionKey = static_cast<int>(SendDlgItemMessageW(hwnd, IDC_OVERLAY_HOTKEY, CB_GETCURSEL, 0, 0)) + 1;
    overlay.scalePercent = static_cast<int>(SendDlgItemMessageW(hwnd, IDC_OVERLAY_SCALE, TBM_GETPOS, 0, 0));
    overlay.opacityPercent = static_cast<int>(SendDlgItemMessageW(hwnd, IDC_OVERLAY_OPACITY, TBM_GETPOS, 0, 0));
    overlay.corner = static_cast<int>(SendDlgItemMessageW(hwnd, IDC_OVERLAY_CORNER, CB_GETCURSEL, 0, 0));
    overlay.layoutStyle = static_cast<int>(SendDlgItemMessageW(hwnd, IDC_OVERLAY_STYLE, CB_GETCURSEL, 0, 0));
    if (!ParseInteger(hwnd, IDC_OVERLAY_MARGIN_X, overlay.marginX) || !ParseInteger(hwnd, IDC_OVERLAY_MARGIN_Y, overlay.marginY) ||
        overlay.marginX < 0 || overlay.marginX > 500 || overlay.marginY < 0 || overlay.marginY > 500) {
        Message(hwnd, L"Overlay", L"Overlay margins must be between 0 and 500.", MB_OK | MB_ICONERROR); ShowSettingsPage(hwnd, 4); return false;
    }
    if (overlay.functionKey < 1 || overlay.functionKey > 24 || overlay.scalePercent < 50 || overlay.scalePercent > 200 ||
        overlay.opacityPercent < 0 || overlay.opacityPercent > 100 || overlay.corner < 0 || overlay.corner > 3 ||
        overlay.layoutStyle < 0 || overlay.layoutStyle > 1) {
        Message(hwnd, L"Overlay", L"Overlay settings are invalid.", MB_OK | MB_ICONERROR); ShowSettingsPage(hwnd, 4); return false;
    }
    const bool logging = SendDlgItemMessageW(hwnd, IDC_LOGGING, BM_GETCHECK, 0, 0) == BST_CHECKED;
    const bool startup = SendDlgItemMessageW(hwnd, IDC_STARTUP, BM_GETCHECK, 0, 0) == BST_CHECKED;
    LegionGoOverlay::ApplyConfig(overlay);
    if (LegionGoOverlay::ActiveFunctionKey() != overlay.functionKey) {
        LegionGoOverlay::ApplyConfig(state->overlay);
        Message(hwnd, L"Overlay", L"That function key is already in use. Choose another key.", MB_OK | MB_ICONERROR);
        ShowSettingsPage(hwnd, 4); return false;
    }
    if (startup != state->startup && !SetStartupEnabled(startup)) {
        Message(hwnd, L"Settings", L"The Windows scheduled task could not be changed.", MB_OK | MB_ICONERROR); return false;
    }
    IniWriteInt(L"General", L"logging", logging ? 1 : 0); IniWriteInt(L"General", L"debounce_ms", debounce);
    IniWriteInt(L"General", L"action_cooldown_ms", cooldown);
    IniWriteInt(L"General", L"FpsLimitEnabled", baseFpsEnabled ? 1 : 0); IniWriteInt(L"General", L"FpsLimit", baseFps);
    IniWriteInt(L"TDP", L"BaseStapmW", base.stapm);
    IniWriteInt(L"TDP", L"BaseFastW", base.fast); IniWriteInt(L"TDP", L"BaseSlowW", base.slow);
    IniWriteInt(L"Fan", L"Enabled", fanEnabled ? 1 : 0);
    for (std::size_t index = 0; index < LegionGoCore::kFanPointCount; ++index)
        IniWriteInt(L"Fan", (L"Duty" + std::to_wstring(index)).c_str(), state->fanDraft.dutyPercent[index]);
    const std::wstring selectedFanProfileId = state->fanProfiles[static_cast<std::size_t>(state->selectedFanProfile)].id;
    WriteFanProfilesToIni(state->fanProfiles, selectedFanProfileId);
    IniWriteInt(L"Overlay", L"VisibleAtStartup", overlay.enabledAtStartup ? 1 : 0);
    IniWriteInt(L"Overlay", L"FpsCaptureEnabled", overlay.fpsCaptureEnabled ? 1 : 0);
    IniWriteInt(L"Overlay", L"FunctionKey", overlay.functionKey);
    IniWriteInt(L"Overlay", L"ScalePercent", overlay.scalePercent);
    IniWriteInt(L"Overlay", L"OpacityPercent", overlay.opacityPercent);
    IniWriteInt(L"Overlay", L"Corner", overlay.corner);
    IniWriteInt(L"Overlay", L"MarginX", overlay.marginX); IniWriteInt(L"Overlay", L"MarginY", overlay.marginY);
    IniWriteInt(L"Overlay", L"Style", overlay.layoutStyle);
    for (const auto& button : state->buttons) {
        IniWriteInt(button.name.c_str(), L"enabled", button.enabled ? 1 : 0);
        IniWrite(button.name.c_str(), L"trigger", button.triggerDown ? L"down" : L"up");
        IniWrite(button.name.c_str(), L"action", ActionName(button.action));
        IniWrite(button.name.c_str(), L"keys", button.keys); IniWrite(button.name.c_str(), L"path", button.path);
        IniWrite(button.name.c_str(), L"args", button.args); IniWrite(button.name.c_str(), L"working_dir", button.workingDir);
        IniWrite(button.name.c_str(), L"internal", button.internal);
    }
    WritePrivateProfileStringW(nullptr, nullptr, nullptr, g_iniPath.c_str());
    state->startup = startup; state->base = base; state->debounce = debounce; state->cooldown = cooldown;
    state->baseFpsEnabled = baseFpsEnabled; state->baseFps = baseFps; state->overlay = overlay;
    LoadConfiguration(); LegionGoOverlay::SetVisible(overlay.enabledAtStartup);
    QueueWorker(fanEnabled ? WorkerJob::FanApply : WorkerJob::FanRestore);
    QueueWorker(WorkerJob::Wake); Balloon(L"Settings applied."); return true;
}

void CreateSettingsControls(HWND hwnd, SettingsState& state) {
    HWND tab = Control(hwnd, WC_TABCONTROLW, L"", WS_TABSTOP | TCS_FIXEDWIDTH | TCS_OWNERDRAWFIXED, 12, 12, 876, 548, IDC_TAB);
    TCITEMW item{}; item.mask = TCIF_TEXT;
    wchar_t general[] = L"General", controller[] = L"Controller", tdp[] = L"TDP", fan[] = L"Fan", overlay[] = L"Overlay", info[] = L"Info";
    item.pszText = general; TabCtrl_InsertItem(tab, 0, &item); item.pszText = controller; TabCtrl_InsertItem(tab, 1, &item);
    item.pszText = tdp; TabCtrl_InsertItem(tab, 2, &item); item.pszText = fan; TabCtrl_InsertItem(tab, 3, &item);
    item.pszText = overlay; TabCtrl_InsertItem(tab, 4, &item); item.pszText = info; TabCtrl_InsertItem(tab, 5, &item);
    TabCtrl_SetItemSize(tab, DpiScale(hwnd, 130), DpiScale(hwnd, 32));
    Control(hwnd, L"BUTTON", L"OK", BS_DEFPUSHBUTTON | WS_TABSTOP, 638, 578, 76, 32, IDC_OK);
    Control(hwnd, L"BUTTON", L"Cancel", BS_PUSHBUTTON | WS_TABSTOP, 722, 578, 76, 32, IDC_CANCEL);
    Control(hwnd, L"BUTTON", L"Apply", BS_PUSHBUTTON | WS_TABSTOP, 806, 578, 76, 32, IDC_APPLY);
    const int x = 34, y = 62;
    Label(state, 0, hwnd, L"Screen brightness:", x, y + 2, 155, 24);
    HWND brightness = PageField(state, 0, hwnd, TRACKBAR_CLASSW, L"", TBS_HORZ | TBS_AUTOTICKS | WS_TABSTOP,
                                x + 160, y - 3, 470, 38, IDC_BRIGHTNESS);
    SendMessageW(brightness, TBM_SETRANGE, TRUE, MAKELONG(0, 100)); SendMessageW(brightness, TBM_SETTICFREQ, 10, 0);
    SendMessageW(brightness, TBM_SETPAGESIZE, 0, 10);
    PageField(state, 0, hwnd, L"STATIC", L"Reading...", SS_LEFT, x + 650, y + 2, 150, 24, IDC_BRIGHTNESS_VALUE);
    PageField(state, 0, hwnd, L"BUTTON", L"Enable diagnostic logging", BS_AUTOCHECKBOX | WS_TABSTOP, x, y + 58, 260, 24, IDC_LOGGING);
    PageField(state, 0, hwnd, L"BUTTON", L"Start with Windows (elevated task)", BS_AUTOCHECKBOX | WS_TABSTOP, x, y + 94, 300, 24, IDC_STARTUP);
    PageField(state, 0, hwnd, L"BUTTON", L"Battery charge limit 80%", BS_AUTOCHECKBOX | WS_TABSTOP, x, y + 130, 320, 28, IDC_BATTERY);
    PageField(state, 0, hwnd, L"STATIC", L"Reading firmware state...", SS_LEFT, x + 340, y + 133, 430, 24, IDC_BATTERY_STATUS);
    Label(state, 0, hwnd, L"HID debounce (0-1000 ms):", x, y + 183, 210, 22);
    HWND debounceEdit = PageField(state, 0, hwnd, L"EDIT", L"", ES_NUMBER | WS_BORDER | WS_TABSTOP, x + 220, y + 180, 70, 26, IDC_DEBOUNCE, WS_EX_CLIENTEDGE);
    HWND debounceSpin = PageField(state, 0, hwnd, UPDOWN_CLASSW, L"", UDS_ARROWKEYS | UDS_SETBUDDYINT, x + 292, y + 180, 22, 26, IDC_DEBOUNCE_SPIN);
    ConfigureSpinner(debounceSpin, debounceEdit, 0, 1000);
    Label(state, 0, hwnd, L"Action cooldown (50-5000 ms):", x, y + 221, 230, 22);
    HWND cooldownEdit = PageField(state, 0, hwnd, L"EDIT", L"", ES_NUMBER | WS_BORDER | WS_TABSTOP, x + 220, y + 218, 70, 26, IDC_COOLDOWN, WS_EX_CLIENTEDGE);
    HWND cooldownSpin = PageField(state, 0, hwnd, UPDOWN_CLASSW, L"", UDS_ARROWKEYS | UDS_SETBUDDYINT, x + 292, y + 218, 22, 26, IDC_COOLDOWN_SPIN);
    ConfigureSpinner(cooldownSpin, cooldownEdit, 50, 5000);
    PageField(state, 0, hwnd, L"BUTTON", L"Open log", BS_PUSHBUTTON, x, y + 275, 110, 28, IDC_OPEN_LOG);
    PageField(state, 0, hwnd, L"BUTTON", L"Enable base FPS limit (AMD Radeon Chill)", BS_AUTOCHECKBOX | WS_TABSTOP,
              x, y + 325, 280, 26, IDC_FPS_LIMIT_ENABLE);
    HWND fpsLimit = PageField(state, 0, hwnd, TRACKBAR_CLASSW, L"", TBS_HORZ | TBS_AUTOTICKS | WS_TABSTOP,
                              x + 285, y + 317, 360, 40, IDC_FPS_LIMIT);
    SendMessageW(fpsLimit, TBM_SETRANGE, TRUE, MAKELONG(30, 144)); SendMessageW(fpsLimit, TBM_SETTICFREQ, 6, 0);
    PageField(state, 0, hwnd, L"STATIC", L"60 FPS", SS_LEFT, x + 660, y + 325, 110, 24, IDC_FPS_LIMIT_VALUE);
    Label(state, 0, hwnd, L"The limiter uses AMD Radeon Chill (minimum = maximum FPS), verifies read-back and restores its previous state on exit.", x, y + 375, 780, 42);
    Label(state, 0, hwnd, L"Brightness uses standard Windows WMI. Battery changes are verified by the Lenovo backend.", x, y + 425, 760, 30);

    Label(state, 1, hwnd, L"Button:", x, y, 65, 22);
    HWND select = PageField(state, 1, hwnd, WC_COMBOBOXW, L"", CBS_DROPDOWNLIST | WS_TABSTOP, x + 65, y - 3, 150, 200, IDC_BUTTON_SELECT);
    (void)select;
    PageField(state, 1, hwnd, L"BUTTON", L"Enabled", BS_AUTOCHECKBOX, x + 230, y, 90, 22, IDC_BUTTON_ENABLED);
    Label(state, 1, hwnd, L"Trigger:", x, y + 40, 65, 22);
    HWND trigger = PageField(state, 1, hwnd, WC_COMBOBOXW, L"", CBS_DROPDOWNLIST, x + 65, y + 36, 150, 150, IDC_BUTTON_TRIGGER);
    SendMessageW(trigger, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Button down")); SendMessageW(trigger, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Button up"));
    Label(state, 1, hwnd, L"Action:", x, y + 80, 65, 22);
    HWND action = PageField(state, 1, hwnd, WC_COMBOBOXW, L"", CBS_DROPDOWNLIST, x + 65, y + 76, 150, 160, IDC_BUTTON_ACTION);
    for (const wchar_t* value : {L"None", L"Keys", L"Launch", L"Internal"}) SendMessageW(action, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(value));
    Label(state, 1, hwnd, L"Keys:", x, y + 120, 65, 22); PageField(state, 1, hwnd, L"EDIT", L"", WS_BORDER | ES_AUTOHSCROLL, x + 65, y + 116, 650, 26, IDC_BUTTON_KEYS, WS_EX_CLIENTEDGE);
    Label(state, 1, hwnd, L"Path:", x, y + 158, 65, 22); PageField(state, 1, hwnd, L"EDIT", L"", WS_BORDER | ES_AUTOHSCROLL, x + 65, y + 154, 585, 26, IDC_BUTTON_PATH, WS_EX_CLIENTEDGE);
    PageField(state, 1, hwnd, L"BUTTON", L"Browse...", BS_PUSHBUTTON, x + 660, y + 153, 90, 28, IDC_BUTTON_BROWSE);
    Label(state, 1, hwnd, L"Arguments:", x, y + 196, 65, 22); PageField(state, 1, hwnd, L"EDIT", L"", WS_BORDER | ES_AUTOHSCROLL, x + 65, y + 192, 650, 26, IDC_BUTTON_ARGS, WS_EX_CLIENTEDGE);
    Label(state, 1, hwnd, L"Work dir:", x, y + 234, 65, 22); PageField(state, 1, hwnd, L"EDIT", L"", WS_BORDER | ES_AUTOHSCROLL, x + 65, y + 230, 650, 26, IDC_BUTTON_WORKDIR, WS_EX_CLIENTEDGE);
    Label(state, 1, hwnd, L"Internal:", x, y + 272, 65, 22); PageField(state, 1, hwnd, L"EDIT", L"", WS_BORDER | ES_AUTOHSCROLL, x + 65, y + 268, 650, 26, IDC_BUTTON_INTERNAL, WS_EX_CLIENTEDGE);
    Label(state, 1, hwnd, L"Key examples: WIN+TAB, ALT+TAB, CTRL+SHIFT+ESC, F13. Launch actions support arguments and a working directory.", x, y + 325, 800, 42);
    Label(state, 1, hwnd, L"M1 remains intentionally unmapped because it overlaps the normal RB/gamepad path.", x, y + 380, 800, 35);

    Label(state, 2, hwnd, L"Base TDP is restored whenever no configured game is running and on a clean exit.", x, y, 650, 24);
    Label(state, 2, hwnd, L"STAPM (W)", x, y + 50, 100, 22); Label(state, 2, hwnd, L"SLOW (W)", x + 145, y + 50, 100, 22); Label(state, 2, hwnd, L"FAST (W)", x + 290, y + 50, 100, 22);
    HWND baseStapm = PageField(state, 2, hwnd, L"EDIT", L"", ES_NUMBER | WS_BORDER, x, y + 75, 76, 27, IDC_BASE_STAPM, WS_EX_CLIENTEDGE);
    HWND baseStapmSpin = PageField(state, 2, hwnd, UPDOWN_CLASSW, L"", UDS_ARROWKEYS | UDS_SETBUDDYINT, x + 78, y + 75, 22, 27, IDC_BASE_STAPM_SPIN); ConfigureSpinner(baseStapmSpin, baseStapm, 5, 35);
    HWND baseSlow = PageField(state, 2, hwnd, L"EDIT", L"", ES_NUMBER | WS_BORDER, x + 145, y + 75, 76, 27, IDC_BASE_SLOW, WS_EX_CLIENTEDGE);
    HWND baseSlowSpin = PageField(state, 2, hwnd, UPDOWN_CLASSW, L"", UDS_ARROWKEYS | UDS_SETBUDDYINT, x + 223, y + 75, 22, 27, IDC_BASE_SLOW_SPIN); ConfigureSpinner(baseSlowSpin, baseSlow, 5, 35);
    HWND baseFast = PageField(state, 2, hwnd, L"EDIT", L"", ES_NUMBER | WS_BORDER, x + 290, y + 75, 76, 27, IDC_BASE_FAST, WS_EX_CLIENTEDGE);
    HWND baseFastSpin = PageField(state, 2, hwnd, UPDOWN_CLASSW, L"", UDS_ARROWKEYS | UDS_SETBUDDYINT, x + 368, y + 75, 22, 27, IDC_BASE_FAST_SPIN); ConfigureSpinner(baseFastSpin, baseFast, 5, 35);
    Label(state, 2, hwnd, L"Required order: STAPM <= SLOW <= FAST", x + 430, y + 78, 330, 24);
    Label(state, 2, hwnd, L"Balanced presets:", x, y + 125, 120, 22);
    const int presets[] = {5, 8, 10, 12, 16, 20, 25, 30};
    for (int index = 0; index < 8; ++index) PageField(state, 2, hwnd, L"BUTTON", (std::to_wstring(presets[index]) + L" W").c_str(), BS_PUSHBUTTON,
                                                        x + index * 82, y + 150, 72, 28, IDC_TDP_PRESET_FIRST + index);
    PageField(state, 2, hwnd, L"BUTTON", L"Manage game profiles...", BS_PUSHBUTTON, x, y + 205, 190, 30, IDC_MANAGE_PROFILES);
    PageField(state, 2, hwnd, L"STATIC", L"", SS_LEFT, x, y + 260, 790, 45, IDC_TDP_STATUS);

    Label(state, 3, hwnd, L"Profile:", x, y + 3, 55, 24);
    PageField(state, 3, hwnd, WC_COMBOBOXW, L"", CBS_DROPDOWN | WS_TABSTOP, x + 58, y, 250, 220, IDC_FAN_PROFILE);
    PageField(state, 3, hwnd, L"BUTTON", L"New", BS_PUSHBUTTON | WS_TABSTOP, x + 318, y, 70, 28, IDC_FAN_PROFILE_NEW);
    PageField(state, 3, hwnd, L"BUTTON", L"Save", BS_PUSHBUTTON | WS_TABSTOP, x + 398, y, 70, 28, IDC_FAN_PROFILE_SAVE);
    PageField(state, 3, hwnd, L"BUTTON", L"Delete", BS_PUSHBUTTON | WS_TABSTOP, x + 478, y, 75, 28, IDC_FAN_PROFILE_DELETE);
    PageField(state, 3, hwnd, L"BUTTON", L"Enable custom firmware fan curve", BS_AUTOCHECKBOX | WS_TABSTOP,
              x, y + 36, 280, 26, IDC_FAN_ENABLE);
    PageField(state, 3, hwnd, L"STATIC", L"Reading fan telemetry...", SS_LEFT, x + 300, y + 38, 500, 28, IDC_FAN_STATUS);
    Label(state, 3, hwnd, L"Drag nodes vertically or select one and use the numeric control. Temperature breakpoints are fixed by Legion Go firmware.", x, y + 69, 800, 24);
    PageField(state, 3, hwnd, FAN_CURVE_CLASS, L"", WS_TABSTOP | WS_BORDER, x, y + 94, 800, 299, IDC_FAN_CURVE, WS_EX_CLIENTEDGE);
    PageField(state, 3, hwnd, L"STATIC", L"Node", SS_LEFT, x, y + 410, 125, 24, IDC_FAN_POINT);
    HWND fanDuty = PageField(state, 3, hwnd, L"EDIT", L"", ES_NUMBER | WS_BORDER | WS_TABSTOP, x + 130, y + 405, 65, 27, IDC_FAN_DUTY, WS_EX_CLIENTEDGE);
    HWND fanDutySpin = PageField(state, 3, hwnd, UPDOWN_CLASSW, L"", UDS_ARROWKEYS | UDS_SETBUDDYINT, x + 197, y + 405, 22, 27, IDC_FAN_DUTY_SPIN);
    ConfigureSpinner(fanDutySpin, fanDuty, 20, 100);
    Label(state, 3, hwnd, L"% duty (estimated RPM shown on graph)", x + 230, y + 410, 280, 24);
    PageField(state, 3, hwnd, L"BUTTON", L"Apply curve now", BS_PUSHBUTTON | WS_TABSTOP, x + 540, y + 402, 125, 32, IDC_FAN_APPLY);
    PageField(state, 3, hwnd, L"BUTTON", L"Restore firmware curve", BS_PUSHBUTTON | WS_TABSTOP, x + 675, y + 402, 150, 32, IDC_FAN_RESTORE);
    Label(state, 3, hwnd, L"Safety floors: 80 C >= 60%, 90 C >= 80%, 100 C >= 85%. Firmware emergency protection always has priority.", x, y + 452, 800, 30);

    PageField(state, 4, hwnd, L"BUTTON", L"Show overlay now and at startup", BS_AUTOCHECKBOX | WS_TABSTOP,
              x, y, 300, 26, IDC_OVERLAY_ENABLE);
    Label(state, 4, hwnd, L"Style:", x + 330, y + 3, 60, 24);
    HWND overlayStyle = PageField(state, 4, hwnd, WC_COMBOBOXW, L"", CBS_DROPDOWNLIST | WS_TABSTOP,
                                  x + 390, y, 190, 120, IDC_OVERLAY_STYLE);
    SendMessageW(overlayStyle, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Vertical panel"));
    SendMessageW(overlayStyle, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Top performance bar"));
    Label(state, 4, hwnd, L"Toggle key:", x, y + 48, 110, 24);
    HWND overlayHotkey = PageField(state, 4, hwnd, WC_COMBOBOXW, L"", CBS_DROPDOWNLIST | WS_TABSTOP,
                                   x + 120, y + 44, 130, 300, IDC_OVERLAY_HOTKEY);
    for (int key = 1; key <= 24; ++key) SendMessageW(overlayHotkey, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>((L"F" + std::to_wstring(key)).c_str()));
    PageField(state, 4, hwnd, L"BUTTON", L"Capture FPS (continuous ETW)", BS_AUTOCHECKBOX | WS_TABSTOP,
              x + 330, y + 46, 330, 26, IDC_OVERLAY_FPS_CAPTURE);
    Label(state, 4, hwnd, L"Scale:", x, y + 94, 110, 24);
    HWND overlayScale = PageField(state, 4, hwnd, TRACKBAR_CLASSW, L"", TBS_HORZ | TBS_AUTOTICKS | WS_TABSTOP,
                                  x + 120, y + 86, 430, 40, IDC_OVERLAY_SCALE);
    SendMessageW(overlayScale, TBM_SETRANGE, TRUE, MAKELONG(50, 200)); SendMessageW(overlayScale, TBM_SETTICFREQ, 25, 0);
    PageField(state, 4, hwnd, L"STATIC", L"100%", SS_LEFT, x + 570, y + 94, 100, 24, IDC_OVERLAY_SCALE_VALUE);
    Label(state, 4, hwnd, L"Black opacity (text follows):", x, y + 140, 230, 24);
    HWND overlayOpacity = PageField(state, 4, hwnd, TRACKBAR_CLASSW, L"", TBS_HORZ | TBS_AUTOTICKS | WS_TABSTOP,
                                    x + 235, y + 132, 315, 40, IDC_OVERLAY_OPACITY);
    SendMessageW(overlayOpacity, TBM_SETRANGE, TRUE, MAKELONG(0, 100)); SendMessageW(overlayOpacity, TBM_SETTICFREQ, 10, 0);
    PageField(state, 4, hwnd, L"STATIC", L"85%", SS_LEFT, x + 570, y + 140, 100, 24, IDC_OVERLAY_OPACITY_VALUE);
    Label(state, 4, hwnd, L"Corner:", x, y + 188, 110, 24);
    HWND overlayCorner = PageField(state, 4, hwnd, WC_COMBOBOXW, L"", CBS_DROPDOWNLIST | WS_TABSTOP,
                                   x + 120, y + 184, 190, 180, IDC_OVERLAY_CORNER);
    for (const wchar_t* corner : {L"Top left", L"Top right", L"Bottom left", L"Bottom right"}) SendMessageW(overlayCorner, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(corner));
    Label(state, 4, hwnd, L"X margin:", x, y + 236, 110, 24);
    HWND marginX = PageField(state, 4, hwnd, L"EDIT", L"", ES_NUMBER | WS_BORDER | WS_TABSTOP, x + 120, y + 232, 70, 27, IDC_OVERLAY_MARGIN_X, WS_EX_CLIENTEDGE);
    HWND marginXSpin = PageField(state, 4, hwnd, UPDOWN_CLASSW, L"", UDS_ARROWKEYS | UDS_SETBUDDYINT, x + 192, y + 232, 22, 27, IDC_OVERLAY_MARGIN_X_SPIN); ConfigureSpinner(marginXSpin, marginX, 0, 500);
    Label(state, 4, hwnd, L"Y margin:", x + 260, y + 236, 110, 24);
    HWND marginY = PageField(state, 4, hwnd, L"EDIT", L"", ES_NUMBER | WS_BORDER | WS_TABSTOP, x + 370, y + 232, 70, 27, IDC_OVERLAY_MARGIN_Y, WS_EX_CLIENTEDGE);
    HWND marginYSpin = PageField(state, 4, hwnd, UPDOWN_CLASSW, L"", UDS_ARROWKEYS | UDS_SETBUDDYINT, x + 442, y + 232, 22, 27, IDC_OVERLAY_MARGIN_Y_SPIN); ConfigureSpinner(marginYSpin, marginY, 0, 500);
    Label(state, 4, hwnd, L"The overlay is topmost and click-through. FPS capture stays continuous when F10 hides or shows the window.", x, y + 300, 800, 48);
    Label(state, 4, hwnd, L"Some protected or exclusive-fullscreen games may hide a normal Windows overlay. Unavailable sensors are shown as N/A.", x, y + 360, 800, 40);
    PageField(state, 4, hwnd, L"STATIC", L"Updates once per second. Hotkey changes take effect after Apply.", SS_LEFT, x, y + 425, 800, 28, IDC_OVERLAY_STATUS);

    Label(state, 5, hwnd, APP_VERSION, x, y, 760, 30);
    Label(state, 5, hwnd,
          L"LegionGoControl is a lightweight native Windows tray utility for Lenovo Legion Go. It maps extra controller buttons, manages Lenovo battery charge limiting, provides manual TDP controls, custom fan curves, an AMD Radeon Chill limiter, performance overlay, and automatically applies per-application TDP profiles while following launcher child processes.",
          x, y + 48, 790, 100);
    Label(state, 5, hwnd, L"Repository:", x, y + 175, 120, 24);
    PageField(state, 5, hwnd, L"BUTTON", L"Open GitHub repository", BS_PUSHBUTTON | WS_TABSTOP,
              x, y + 205, 210, 34, IDC_INFO_REPOSITORY);
    Label(state, 5, hwnd, REPOSITORY_URL, x + 230, y + 212, 540, 24);
    Label(state, 5, hwnd, L"Application icon: Nintendo Switch by Nick Taras from The Noun Project (Creative Commons, attribution required).", x, y + 270, 790, 45);
    PageField(state, 5, hwnd, L"BUTTON", L"Open icon and license page", BS_PUSHBUTTON | WS_TABSTOP,
              x, y + 320, 230, 34, IDC_INFO_ICON_LICENSE);
    Label(state, 5, hwnd,
          L"This beta is hardware-specific. TDP, battery, HID, fan, overlay and restore behavior must be validated on the actual Legion Go device.",
          x, y + 390, 790, 55);
    ShowSettingsPage(hwnd, 0);
}

void ShowProfilesWindow();
LRESULT CALLBACK SettingsProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
        case WM_CREATE: {
            auto* state = new SettingsState; SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state));
            CreateSettingsControls(hwnd, *state); PopulateSettings(hwnd); return 0;
        }
        case WM_NOTIFY: {
            const NMHDR* header = reinterpret_cast<NMHDR*>(lParam);
            if (header && header->idFrom == IDC_TAB && header->code == TCN_SELCHANGE) {
                const int page = TabCtrl_GetCurSel(header->hwndFrom); ShowSettingsPage(hwnd, page);
                if (page == 3) QueueWorker(WorkerJob::FanStatus);
            }
            return 0;
        }
        case WM_DRAWITEM: {
            const DRAWITEMSTRUCT* draw = reinterpret_cast<const DRAWITEMSTRUCT*>(lParam);
            if (draw && draw->CtlID == IDC_TAB) {
                const bool selected = (draw->itemState & ODS_SELECTED) != 0;
                HBRUSH brush = CreateSolidBrush(selected ? RGB(244, 246, 249) : RGB(92, 98, 108));
                FillRect(draw->hDC, &draw->rcItem, brush); DeleteObject(brush);
                SetBkMode(draw->hDC, TRANSPARENT); SetTextColor(draw->hDC, selected ? RGB(24, 28, 34) : RGB(242, 244, 247));
                SelectObject(draw->hDC, g_font);
                wchar_t text[64]{}; TCITEMW item{}; item.mask = TCIF_TEXT; item.pszText = text; item.cchTextMax = static_cast<int>(_countof(text));
                TabCtrl_GetItem(draw->hwndItem, static_cast<int>(draw->itemID), &item);
                RECT textRect = draw->rcItem; DrawTextW(draw->hDC, text, -1, &textRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
                FrameRect(draw->hDC, &draw->rcItem, GetSysColorBrush(COLOR_3DSHADOW));
                if (draw->itemState & ODS_FOCUS) DrawFocusRect(draw->hDC, &draw->rcItem);
                return TRUE;
            }
            return FALSE;
        }
        case WM_HSCROLL: {
            SettingsState* state = SettingsData(hwnd);
            if (state && reinterpret_cast<HWND>(lParam) == GetDlgItem(hwnd, IDC_BRIGHTNESS)) {
                const int position = static_cast<int>(SendDlgItemMessageW(hwnd, IDC_BRIGHTNESS, TBM_GETPOS, 0, 0));
                SetText(hwnd, IDC_BRIGHTNESS_VALUE, std::to_wstring(position) + L"%");
                const int notification = LOWORD(wParam);
                state->brightnessDragging = notification == TB_THUMBTRACK;
                g_requestedBrightness.store(position);
                QueueWorker(WorkerJob::BrightnessSet);
                return 0;
            }
            if (state && reinterpret_cast<HWND>(lParam) == GetDlgItem(hwnd, IDC_FPS_LIMIT)) {
                const int value = static_cast<int>(SendDlgItemMessageW(hwnd, IDC_FPS_LIMIT, TBM_GETPOS, 0, 0));
                SetText(hwnd, IDC_FPS_LIMIT_VALUE, std::to_wstring(value) + L" FPS"); return 0;
            }
            if (state && reinterpret_cast<HWND>(lParam) == GetDlgItem(hwnd, IDC_OVERLAY_SCALE)) {
                const int value = static_cast<int>(SendDlgItemMessageW(hwnd, IDC_OVERLAY_SCALE, TBM_GETPOS, 0, 0));
                SetText(hwnd, IDC_OVERLAY_SCALE_VALUE, std::to_wstring(value) + L"%"); return 0;
            }
            if (state && reinterpret_cast<HWND>(lParam) == GetDlgItem(hwnd, IDC_OVERLAY_OPACITY)) {
                const int value = static_cast<int>(SendDlgItemMessageW(hwnd, IDC_OVERLAY_OPACITY, TBM_GETPOS, 0, 0));
                SetText(hwnd, IDC_OVERLAY_OPACITY_VALUE, std::to_wstring(value) + L"%"); return 0;
            }
            return DefWindowProcW(hwnd, message, wParam, lParam);
        }
        case WMAPP_FAN_CURVE_CHANGED: return 0;
        case WM_COMMAND: {
            const int id = LOWORD(wParam), notification = HIWORD(wParam); SettingsState* state = SettingsData(hwnd); if (!state) return 0;
            if (id == IDC_OK) { if (ApplySettings(hwnd)) ShowWindow(hwnd, SW_HIDE); }
            else if (id == IDC_CANCEL) { PopulateSettings(hwnd); ShowWindow(hwnd, SW_HIDE); }
            else if (id == IDC_APPLY) ApplySettings(hwnd);
            else if (id == IDC_OPEN_LOG) { LogAlways(L"Log opened."); ShellExecuteW(hwnd, L"open", g_logPath.c_str(), nullptr, nullptr, SW_SHOWNORMAL); }
            else if (id == IDC_INFO_REPOSITORY) ShellExecuteW(hwnd, L"open", REPOSITORY_URL, nullptr, nullptr, SW_SHOWNORMAL);
            else if (id == IDC_INFO_ICON_LICENSE) ShellExecuteW(hwnd, L"open", L"https://thenounproject.com/icon/nintendo-switch-6146674/", nullptr, nullptr, SW_SHOWNORMAL);
            else if (id == IDC_BATTERY && notification == BN_CLICKED) {
                EnableWindow(GetDlgItem(hwnd, IDC_BATTERY), FALSE);
                SetText(hwnd, IDC_BATTERY_STATUS, L"Applying and verifying...");
                QueueWorker(WorkerJob::BatteryToggle);
            } else if (id == IDC_FAN_DUTY && notification == EN_CHANGE && !state->loading && !state->normalizingFan) {
                int value = 0;
                if (ParseInteger(hwnd, IDC_FAN_DUTY, value) && value >= 20 && value <= 100) {
                    const int index = state->selectedFanPoint;
                    const int lower = (std::max)(FanDutyFloor(index), index ? state->fanDraft.dutyPercent[static_cast<size_t>(index-1)] : 20);
                    const int upper = index < 9 ? state->fanDraft.dutyPercent[static_cast<size_t>(index+1)] : 100;
                    value = (std::max)(lower, (std::min)(upper, value));
                    state->fanDraft.dutyPercent[static_cast<size_t>(index)] = value;
                    state->normalizingFan = true; SetNumericValue(hwnd, IDC_FAN_DUTY, IDC_FAN_DUTY_SPIN, value); state->normalizingFan = false;
                    InvalidateRect(GetDlgItem(hwnd, IDC_FAN_CURVE), nullptr, FALSE);
                }
            } else if (id == IDC_FAN_APPLY) {
                SendDlgItemMessageW(hwnd, IDC_FAN_ENABLE, BM_SETCHECK, BST_CHECKED, 0); ApplySettings(hwnd);
            } else if (id == IDC_FAN_RESTORE) {
                SendDlgItemMessageW(hwnd, IDC_FAN_ENABLE, BM_SETCHECK, BST_UNCHECKED, 0); ApplySettings(hwnd);
            } else if (id == IDC_FAN_PROFILE && notification == CBN_SELCHANGE && !state->loading) {
                const int selected = static_cast<int>(SendDlgItemMessageW(hwnd, IDC_FAN_PROFILE, CB_GETCURSEL, 0, 0));
                SelectFanProfile(hwnd, selected);
            } else if (id == IDC_FAN_PROFILE_SAVE) {
                if (SaveCurrentFanProfile(hwnd)) {
                    const std::wstring selected = state->fanProfiles[static_cast<std::size_t>(state->selectedFanProfile)].id;
                    WriteFanProfilesToIni(state->fanProfiles, selected);
                    WritePrivateProfileStringW(nullptr, nullptr, nullptr, g_iniPath.c_str());
                    Balloon(L"Fan profile saved.");
                }
            } else if (id == IDC_FAN_PROFILE_NEW) {
                if (!state->fanProfiles.empty() && state->selectedFanProfile < static_cast<int>(state->fanProfiles.size()))
                    state->fanProfiles[static_cast<std::size_t>(state->selectedFanProfile)].curve = state->fanDraft;
                int suffix = static_cast<int>(state->fanProfiles.size()) + 1; std::wstring name;
                do { name = L"New profile " + std::to_wstring(suffix++); }
                while (std::any_of(state->fanProfiles.begin(), state->fanProfiles.end(), [&](const StoredFanProfile& profile) { return Upper(profile.name) == Upper(name); }));
                state->fanProfiles.push_back({NewGuid(), name, state->fanDraft});
                state->selectedFanProfile = static_cast<int>(state->fanProfiles.size()) - 1; RefreshFanProfileCombo(hwnd);
            } else if (id == IDC_FAN_PROFILE_DELETE) {
                if (state->fanProfiles.size() <= 1) Message(hwnd, L"Fan profiles", L"At least one fan profile must remain.", MB_OK | MB_ICONWARNING);
                else if (MessageBoxW(hwnd, L"Delete the selected fan profile?", L"Fan profiles", MB_YESNO | MB_ICONQUESTION) == IDYES) {
                    state->fanProfiles.erase(state->fanProfiles.begin() + state->selectedFanProfile);
                    state->selectedFanProfile = (std::min)(state->selectedFanProfile, static_cast<int>(state->fanProfiles.size()) - 1);
                    state->fanDraft = state->fanProfiles[static_cast<std::size_t>(state->selectedFanProfile)].curve;
                    RefreshFanProfileCombo(hwnd); UpdateFanPointEditor(hwnd); InvalidateRect(GetDlgItem(hwnd, IDC_FAN_CURVE), nullptr, FALSE);
                }
            }
            else if (id == IDC_BUTTON_SELECT && notification == CBN_SELCHANGE && !state->loading) {
                StoreControllerEditor(hwnd); const int selected = static_cast<int>(SendDlgItemMessageW(hwnd, IDC_BUTTON_SELECT, CB_GETCURSEL, 0, 0));
                if (selected >= 0 && selected < static_cast<int>(state->buttons.size())) { state->selectedButton = selected; LoadControllerEditor(hwnd); }
            } else if ((id == IDC_BASE_STAPM || id == IDC_BASE_SLOW || id == IDC_BASE_FAST) &&
                       notification == EN_CHANGE && !state->loading) {
                NormalizeSettingsTdp(hwnd);
            } else if (id == IDC_BUTTON_ACTION && notification == CBN_SELCHANGE) {
                UpdateControllerActionFields(hwnd);
            } else if (id == IDC_BUTTON_BROWSE) {
                wchar_t path[32768]{};
                const std::wstring existing = WindowText(GetDlgItem(hwnd, IDC_BUTTON_PATH));
                wcsncpy_s(path, existing.c_str(), _TRUNCATE);
                wchar_t filter[] = L"Applications (*.exe)\0*.exe\0All files (*.*)\0*.*\0\0";
                OPENFILENAMEW dialog{}; dialog.lStructSize = sizeof(dialog); dialog.hwndOwner = hwnd;
                dialog.lpstrFilter = filter; dialog.lpstrFile = path; dialog.nMaxFile = static_cast<DWORD>(_countof(path));
                dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
                if (GetOpenFileNameW(&dialog)) SetText(hwnd, IDC_BUTTON_PATH, path);
            } else if (id >= IDC_TDP_PRESET_FIRST && id < IDC_TDP_PRESET_FIRST + 8) {
                const int presets[] = {5,8,10,12,16,20,25,30}; const int value = presets[id - IDC_TDP_PRESET_FIRST];
                SetNumericValue(hwnd, IDC_BASE_STAPM, IDC_BASE_STAPM_SPIN, value);
                SetNumericValue(hwnd, IDC_BASE_FAST, IDC_BASE_FAST_SPIN, value);
                SetNumericValue(hwnd, IDC_BASE_SLOW, IDC_BASE_SLOW_SPIN, value);
            } else if (id == IDC_MANAGE_PROFILES) ShowProfilesWindow();
            return 0;
        }
        case WM_CLOSE: PopulateSettings(hwnd); ShowWindow(hwnd, SW_HIDE); return 0;
        case WM_DESTROY: delete SettingsData(hwnd); SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0); g_settings = nullptr; return 0;
        default: return DefWindowProcW(hwnd, message, wParam, lParam);
    }
}
void UpdateSettingsRuntime() {
    if (!g_settings || !IsWindow(g_settings)) return;
    const RuntimeStatus status = RuntimeSnapshot();
    std::wstring text = status.profileActive ? L"Active profile: " + status.profileName : L"Active target: Base";
    text += L" - STAPM " + std::to_wstring(status.desired.stapm) + L" / SLOW " +
            std::to_wstring(status.desired.slow) + L" / FAST " + std::to_wstring(status.desired.fast) + L" W";
    text += status.fpsLimitEnabled ? L" | FPS limit " + std::to_wstring(status.fpsLimit) : L" | FPS limit off";
    if (status.applyKnown && !status.applyOk) text += L"\r\n" + status.error;
    if (status.fpsLimitKnown && !status.fpsLimitOk) text += L"\r\nFPS limiter: " + status.fpsLimitError;
    SetText(g_settings, IDC_TDP_STATUS, text);
    HWND battery = GetDlgItem(g_settings, IDC_BATTERY);
    if (battery) {
        SendMessageW(battery, BM_SETCHECK, status.battery.known && status.battery.enabled ? BST_CHECKED : BST_UNCHECKED, 0);
        SetText(g_settings, IDC_BATTERY_STATUS,
                status.battery.known ? (status.battery.enabled ? L"Enabled (firmware verified)" : L"Disabled (firmware verified)")
                                     : (status.battery.error.empty() ? L"Status unknown" : status.battery.error));
        EnableWindow(battery, TRUE);
    }
    SettingsState* settings = SettingsData(g_settings);
    if (settings && !settings->brightnessDragging) {
        HWND slider = GetDlgItem(g_settings, IDC_BRIGHTNESS);
        if (slider && status.brightnessKnown) SendMessageW(slider, TBM_SETPOS, TRUE, status.brightness);
        SetText(g_settings, IDC_BRIGHTNESS_VALUE,
                status.brightnessKnown ? std::to_wstring(status.brightness) + L"%"
                                       : (status.brightnessError.empty() ? L"Unavailable" : L"Set failed - see log"));
    }
    if (status.fan.known) {
        const int command = LegionGoCore::InterpolateFanDuty(status.fan.curve, status.fan.temperatureC);
        SetText(g_settings, IDC_FAN_STATUS, L"CPU " + std::to_wstring(status.fan.temperatureC) + L" C  |  Fan " +
            std::to_wstring(status.fan.rpm) + L" RPM  |  Curve command ~" + std::to_wstring(command) + L"%");
    } else SetText(g_settings, IDC_FAN_STATUS, status.fan.error.empty() ? L"Fan telemetry unavailable" : status.fan.error);
    if (settings) {
        const int activeKey = LegionGoOverlay::ActiveFunctionKey();
        SetText(g_settings, IDC_OVERLAY_STATUS, activeKey > 0
            ? std::wstring(L"Updates once per second. Toggle: F") + std::to_wstring(activeKey) +
              (LegionGoOverlay::IsVisible() ? L". Overlay is active." : L". Overlay is hidden.")
            : L"The selected function key is already in use. Choose another key and press Apply.");
    }
    InvalidateRect(GetDlgItem(g_settings, IDC_FAN_CURVE), nullptr, FALSE);
}
void ShowSettings(int tab) {
    if (!g_settings || !IsWindow(g_settings)) {
        const UINT dpi = SystemDpi();
        g_settings = CreateWindowExW(WS_EX_APPWINDOW, SETTINGS_CLASS, L"LegionGoControl Settings", WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
                                     CW_USEDEFAULT, CW_USEDEFAULT, DpiScale(920, dpi), DpiScale(660, dpi), nullptr, nullptr, g_instance, nullptr);
        if (g_settings) CenterWindow(g_settings, nullptr);
    } else if (!IsWindowVisible(g_settings)) PopulateSettings(g_settings);
    if (!g_settings) return;
    ShowSettingsPage(g_settings, tab); UpdateSettingsRuntime();
    if (tab == 0) { QueueWorker(WorkerJob::BatteryStatus); QueueWorker(WorkerJob::BrightnessStatus); }
    if (tab == 3) QueueWorker(WorkerJob::FanStatus);
    ShowWindow(g_settings, SW_SHOWNORMAL); SetForegroundWindow(g_settings);
}

// ---------- Profiles list and modal editor ----------
struct ProfileEditorContext {
    LegionGoCore::GameProfile value;
    const std::vector<StoredProfile>* all = nullptr;
    std::size_t editing = LegionGoCore::kNoProfile;
    bool done = false, accepted = false, normalizingTdp = false;
};
ProfileEditorContext* EditorData(HWND hwnd) { return reinterpret_cast<ProfileEditorContext*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA)); }
LRESULT CALLBACK ProfileEditorProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
        case WM_CREATE: {
            auto* create = reinterpret_cast<CREATESTRUCTW*>(lParam); auto* context = static_cast<ProfileEditorContext*>(create->lpCreateParams);
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(context));
            Control(hwnd, L"STATIC", L"Profile name:", SS_LEFT, 18, 20, 105, 20, 0);
            Control(hwnd, L"EDIT", context->value.name.c_str(), WS_BORDER | ES_AUTOHSCROLL | WS_TABSTOP, 128, 17, 330, 24, IDC_PE_NAME, WS_EX_CLIENTEDGE);
            Control(hwnd, L"STATIC", L"Executable:", SS_LEFT, 18, 58, 105, 20, 0);
            Control(hwnd, L"EDIT", context->value.path.c_str(), WS_BORDER | ES_AUTOHSCROLL | WS_TABSTOP, 128, 55, 330, 24, IDC_PE_PATH, WS_EX_CLIENTEDGE);
            Control(hwnd, L"BUTTON", L"Browse...", BS_PUSHBUTTON, 468, 54, 82, 26, IDC_PE_BROWSE);
            Control(hwnd, L"STATIC", L"STAPM (W)", SS_LEFT, 18, 110, 95, 20, 0); Control(hwnd, L"STATIC", L"SLOW (W)", SS_LEFT, 155, 110, 95, 20, 0); Control(hwnd, L"STATIC", L"FAST (W)", SS_LEFT, 292, 110, 95, 20, 0);
            HWND stapmEdit = Control(hwnd, L"EDIT", L"", WS_BORDER | ES_NUMBER, 18, 134, 72, 26, IDC_PE_STAPM, WS_EX_CLIENTEDGE);
            HWND stapmSpin = Control(hwnd, UPDOWN_CLASSW, L"", UDS_ARROWKEYS | UDS_SETBUDDYINT, 92, 134, 22, 26, IDC_PE_STAPM_SPIN); ConfigureSpinner(stapmSpin, stapmEdit, 5, 35);
            HWND slowEdit = Control(hwnd, L"EDIT", L"", WS_BORDER | ES_NUMBER, 155, 134, 72, 26, IDC_PE_SLOW, WS_EX_CLIENTEDGE);
            HWND slowSpin = Control(hwnd, UPDOWN_CLASSW, L"", UDS_ARROWKEYS | UDS_SETBUDDYINT, 229, 134, 22, 26, IDC_PE_SLOW_SPIN); ConfigureSpinner(slowSpin, slowEdit, 5, 35);
            HWND fastEdit = Control(hwnd, L"EDIT", L"", WS_BORDER | ES_NUMBER, 292, 134, 72, 26, IDC_PE_FAST, WS_EX_CLIENTEDGE);
            HWND fastSpin = Control(hwnd, UPDOWN_CLASSW, L"", UDS_ARROWKEYS | UDS_SETBUDDYINT, 366, 134, 22, 26, IDC_PE_FAST_SPIN); ConfigureSpinner(fastSpin, fastEdit, 5, 35);
            SetNumericValue(hwnd, IDC_PE_STAPM, IDC_PE_STAPM_SPIN, context->value.tdp.stapm);
            SetNumericValue(hwnd, IDC_PE_SLOW, IDC_PE_SLOW_SPIN, context->value.tdp.slow);
            SetNumericValue(hwnd, IDC_PE_FAST, IDC_PE_FAST_SPIN, context->value.tdp.fast);
            Control(hwnd, L"STATIC", L"Required order: STAPM <= SLOW <= FAST", SS_LEFT, 410, 137, 155, 38, 0);
            Control(hwnd, L"BUTTON", L"Limit FPS for this profile", BS_AUTOCHECKBOX | WS_TABSTOP, 18, 184, 190, 24, IDC_PE_FPS_ENABLE);
            HWND profileFps = Control(hwnd, TRACKBAR_CLASSW, L"", TBS_HORZ | TBS_AUTOTICKS | WS_TABSTOP, 210, 176, 250, 40, IDC_PE_FPS_LIMIT);
            SendMessageW(profileFps, TBM_SETRANGE, TRUE, MAKELONG(30, 144)); SendMessageW(profileFps, TBM_SETTICFREQ, 6, 0);
            SendMessageW(profileFps, TBM_SETPOS, TRUE, context->value.fpsLimit);
            Control(hwnd, L"STATIC", (std::to_wstring(context->value.fpsLimit) + L" FPS").c_str(), SS_LEFT, 470, 184, 90, 24, IDC_PE_FPS_VALUE);
            SendDlgItemMessageW(hwnd, IDC_PE_FPS_ENABLE, BM_SETCHECK, context->value.fpsLimitEnabled ? BST_CHECKED : BST_UNCHECKED, 0);
            Control(hwnd, L"STATIC", L"The executable must be an absolute .exe path. Matching uses the full normalized path.", SS_LEFT, 18, 230, 530, 35, 0);
            Control(hwnd, L"BUTTON", L"OK", BS_DEFPUSHBUTTON, 382, 276, 80, 28, IDC_PE_OK); Control(hwnd, L"BUTTON", L"Cancel", BS_PUSHBUTTON, 470, 276, 80, 28, IDC_PE_CANCEL);
            return 0;
        }
        case WM_HSCROLL:
            if (reinterpret_cast<HWND>(lParam) == GetDlgItem(hwnd, IDC_PE_FPS_LIMIT)) {
                const int value = static_cast<int>(SendDlgItemMessageW(hwnd, IDC_PE_FPS_LIMIT, TBM_GETPOS, 0, 0));
                SetText(hwnd, IDC_PE_FPS_VALUE, std::to_wstring(value) + L" FPS"); return 0;
            }
            return DefWindowProcW(hwnd, message, wParam, lParam);
        case WM_COMMAND: {
            ProfileEditorContext* context = EditorData(hwnd); const int id = LOWORD(wParam); const int notification = HIWORD(wParam); if (!context) return 0;
            if ((id == IDC_PE_STAPM || id == IDC_PE_SLOW || id == IDC_PE_FAST) && notification == EN_CHANGE && !context->normalizingTdp) {
                context->normalizingTdp = true;
                NormalizeTdpControls(hwnd, IDC_PE_STAPM, IDC_PE_SLOW, IDC_PE_FAST,
                                     IDC_PE_STAPM_SPIN, IDC_PE_SLOW_SPIN, IDC_PE_FAST_SPIN);
                context->normalizingTdp = false;
            } else if (id == IDC_PE_BROWSE) {
                wchar_t path[32768]{}; const std::wstring existing = WindowText(GetDlgItem(hwnd, IDC_PE_PATH)); wcsncpy_s(path, existing.c_str(), _TRUNCATE);
                wchar_t filter[] = L"Applications (*.exe)\0*.exe\0All files (*.*)\0*.*\0\0";
                OPENFILENAMEW dialog{}; dialog.lStructSize = sizeof(dialog); dialog.hwndOwner = hwnd; dialog.lpstrFilter = filter; dialog.lpstrFile = path;
                dialog.nMaxFile = static_cast<DWORD>(_countof(path)); dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
                if (GetOpenFileNameW(&dialog)) SetText(hwnd, IDC_PE_PATH, path);
            } else if (id == IDC_PE_OK) {
                context->normalizingTdp = true;
                NormalizeTdpControls(hwnd, IDC_PE_STAPM, IDC_PE_SLOW, IDC_PE_FAST,
                                     IDC_PE_STAPM_SPIN, IDC_PE_SLOW_SPIN, IDC_PE_FAST_SPIN);
                context->normalizingTdp = false;
                LegionGoCore::GameProfile candidate; candidate.name = Trim(WindowText(GetDlgItem(hwnd, IDC_PE_NAME))); candidate.path = Trim(WindowText(GetDlgItem(hwnd, IDC_PE_PATH)));
                candidate.fpsLimitEnabled = SendDlgItemMessageW(hwnd, IDC_PE_FPS_ENABLE, BM_GETCHECK, 0, 0) == BST_CHECKED;
                candidate.fpsLimit = static_cast<int>(SendDlgItemMessageW(hwnd, IDC_PE_FPS_LIMIT, TBM_GETPOS, 0, 0));
                if (!FileExists(candidate.path)) {
                    Message(hwnd, L"Profile", L"Select an existing executable file.", MB_OK | MB_ICONERROR); return 0;
                }
                if (!ParseInteger(hwnd, IDC_PE_STAPM, candidate.tdp.stapm) || !ParseInteger(hwnd, IDC_PE_SLOW, candidate.tdp.slow) || !ParseInteger(hwnd, IDC_PE_FAST, candidate.tdp.fast)) {
                    Message(hwnd, L"Profile", L"TDP values must be whole numbers.", MB_OK | MB_ICONERROR); return 0;
                }
                std::vector<LegionGoCore::GameProfile> check = context->all ? CoreProfiles(*context->all) : std::vector<LegionGoCore::GameProfile>{};
                if (context->editing < check.size()) check[context->editing] = candidate; else check.push_back(candidate);
                std::wstring error; std::size_t bad = 0;
                if (!LegionGoCore::ValidateGameProfiles(check, &error, &bad)) { Message(hwnd, L"Profile", error, MB_OK | MB_ICONERROR); return 0; }
                context->value = candidate; context->accepted = true; DestroyWindow(hwnd);
            } else if (id == IDC_PE_CANCEL) DestroyWindow(hwnd);
            return 0;
        }
        case WM_CLOSE: DestroyWindow(hwnd); return 0;
        case WM_DESTROY: { ProfileEditorContext* context = EditorData(hwnd); if (context) context->done = true; return 0; }
        default: return DefWindowProcW(hwnd, message, wParam, lParam);
    }
}
bool EditProfileModal(HWND owner, ProfileEditorContext& context) {
    EnableWindow(owner, FALSE);
    const UINT dpi = WindowDpi(owner);
    HWND window = CreateWindowExW(WS_EX_DLGMODALFRAME, PROFILE_EDITOR_CLASS, context.editing == LegionGoCore::kNoProfile ? L"Add game profile" : L"Edit game profile",
                                  WS_POPUP | WS_CAPTION | WS_SYSMENU, CW_USEDEFAULT, CW_USEDEFAULT,
                                  DpiScale(584, dpi), DpiScale(360, dpi), owner, nullptr, g_instance, &context);
    if (!window) { EnableWindow(owner, TRUE); return false; }
    CenterWindow(window, owner); ShowWindow(window, SW_SHOW); SetForegroundWindow(window);
    MSG message{};
    int messageResult = 1;
    while (!context.done && (messageResult = static_cast<int>(GetMessageW(&message, nullptr, 0, 0))) > 0) {
        if (!IsDialogMessageW(window, &message)) { TranslateMessage(&message); DispatchMessageW(&message); }
    }
    if (messageResult == 0) PostQuitMessage(static_cast<int>(message.wParam));
    else if (messageResult < 0 && IsWindow(window)) DestroyWindow(window);
    if (IsWindow(owner)) { EnableWindow(owner, TRUE); SetForegroundWindow(owner); }
    return context.accepted;
}
void FillProfileList(HWND hwnd) {
    HWND list = GetDlgItem(hwnd, IDC_PROFILE_LIST); ListView_DeleteAllItems(list);
    std::vector<StoredProfile> profiles; { std::lock_guard<std::mutex> lock(g_configurationMutex); profiles = g_profiles; }
    const RuntimeStatus runtime = RuntimeSnapshot();
    for (std::size_t index = 0; index < profiles.size(); ++index) {
        LVITEMW item{}; item.mask = LVIF_TEXT; item.iItem = static_cast<int>(index); item.pszText = const_cast<wchar_t*>(profiles[index].value.name.c_str());
        ListView_InsertItem(list, &item); ListView_SetItemText(list, static_cast<int>(index), 1, const_cast<wchar_t*>(profiles[index].value.path.c_str()));
        const auto& tdp = profiles[index].value.tdp;
        std::wstring stapm = std::to_wstring(tdp.stapm) + L" W", slow = std::to_wstring(tdp.slow) + L" W", fast = std::to_wstring(tdp.fast) + L" W";
        ListView_SetItemText(list, static_cast<int>(index), 2, const_cast<wchar_t*>(stapm.c_str()));
        ListView_SetItemText(list, static_cast<int>(index), 3, const_cast<wchar_t*>(slow.c_str()));
        ListView_SetItemText(list, static_cast<int>(index), 4, const_cast<wchar_t*>(fast.c_str()));
        std::wstring fps = profiles[index].value.fpsLimitEnabled ? std::to_wstring(profiles[index].value.fpsLimit) : L"Base";
        ListView_SetItemText(list, static_cast<int>(index), 5, const_cast<wchar_t*>(fps.c_str()));
        std::wstring status = runtime.profileActive && runtime.profileId == profiles[index].id ? (runtime.applyOk ? L"Active" : L"Error") : L"Idle";
        ListView_SetItemText(list, static_cast<int>(index), 6, const_cast<wchar_t*>(status.c_str()));
    }
}
int SelectedProfile(HWND hwnd) { return ListView_GetNextItem(GetDlgItem(hwnd, IDC_PROFILE_LIST), -1, LVNI_SELECTED); }
void SaveProfileChanges(const std::vector<StoredProfile>& profiles) {
    WriteProfilesToIni(profiles); { std::lock_guard<std::mutex> lock(g_configurationMutex); g_profiles = profiles; } QueueWorker(WorkerJob::Wake);
}
LRESULT CALLBACK ProfilesProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
        case WM_CREATE: {
            HWND list = Control(hwnd, WC_LISTVIEWW, L"", LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS | WS_TABSTOP, 12, 12, 916, 430, IDC_PROFILE_LIST, WS_EX_CLIENTEDGE);
            ListView_SetExtendedListViewStyle(list, LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER | LVS_EX_LABELTIP);
            for (const auto& column : std::vector<std::pair<const wchar_t*, int>>{{L"Name",125},{L"Executable",385},{L"STAPM",65},{L"SLOW",60},{L"FAST",60},{L"FPS",65},{L"Status",80}}) {
                LVCOLUMNW data{}; data.mask = LVCF_TEXT | LVCF_WIDTH; data.pszText = const_cast<wchar_t*>(column.first);
                data.cx = DpiScale(column.second, WindowDpi(hwnd));
                ListView_InsertColumn(list, Header_GetItemCount(ListView_GetHeader(list)), &data);
            }
            Control(hwnd, L"BUTTON", L"+ Add profile", BS_PUSHBUTTON, 12, 450, 110, 32, IDC_PROFILE_ADD); Control(hwnd, L"BUTTON", L"Edit...", BS_PUSHBUTTON, 130, 450, 82, 32, IDC_PROFILE_EDIT);
            Control(hwnd, L"BUTTON", L"Remove", BS_PUSHBUTTON, 220, 450, 86, 32, IDC_PROFILE_REMOVE); Control(hwnd, L"BUTTON", L"Move up", BS_PUSHBUTTON, 322, 450, 90, 32, IDC_PROFILE_UP);
            Control(hwnd, L"BUTTON", L"Move down", BS_PUSHBUTTON, 420, 450, 100, 32, IDC_PROFILE_DOWN); Control(hwnd, L"BUTTON", L"Close", BS_DEFPUSHBUTTON, 848, 450, 80, 32, IDC_PROFILE_CLOSE);
            FillProfileList(hwnd); return 0;
        }
        case WM_NOTIFY: {
            const NMHDR* header = reinterpret_cast<NMHDR*>(lParam);
            if (header && header->idFrom == IDC_PROFILE_LIST && header->code == NM_DBLCLK) SendMessageW(hwnd, WM_COMMAND, IDC_PROFILE_EDIT, 0);
            return 0;
        }
        case WM_COMMAND: {
            const int id = LOWORD(wParam); if (id == IDC_PROFILE_CLOSE) { ShowWindow(hwnd, SW_HIDE); return 0; }
            std::vector<StoredProfile> profiles; { std::lock_guard<std::mutex> lock(g_configurationMutex); profiles = g_profiles; }
            int selected = SelectedProfile(hwnd);
            if (id == IDC_PROFILE_ADD) {
                ProfileEditorContext editor; editor.value.tdp = {16,20,20}; editor.all = &profiles;
                if (EditProfileModal(hwnd, editor)) { profiles.push_back({NewGuid(), editor.value}); SaveProfileChanges(profiles); FillProfileList(hwnd); ListView_SetItemState(GetDlgItem(hwnd, IDC_PROFILE_LIST), static_cast<int>(profiles.size() - 1), LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED); }
            } else if (id == IDC_PROFILE_EDIT && selected >= 0 && selected < static_cast<int>(profiles.size())) {
                ProfileEditorContext editor; editor.value = profiles[static_cast<std::size_t>(selected)].value; editor.all = &profiles; editor.editing = static_cast<std::size_t>(selected);
                if (EditProfileModal(hwnd, editor)) { profiles[static_cast<std::size_t>(selected)].value = editor.value; SaveProfileChanges(profiles); FillProfileList(hwnd); ListView_SetItemState(GetDlgItem(hwnd, IDC_PROFILE_LIST), selected, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED); }
            } else if (id == IDC_PROFILE_REMOVE && selected >= 0 && selected < static_cast<int>(profiles.size())) {
                if (MessageBoxW(hwnd, L"Remove the selected profile?", L"Profiles", MB_YESNO | MB_ICONQUESTION) == IDYES) {
                    const std::wstring section = ProfileSection(profiles[static_cast<std::size_t>(selected)].id); WritePrivateProfileStringW(section.c_str(), nullptr, nullptr, g_iniPath.c_str());
                    profiles.erase(profiles.begin() + selected); SaveProfileChanges(profiles); FillProfileList(hwnd);
                }
            } else if ((id == IDC_PROFILE_UP || id == IDC_PROFILE_DOWN) && selected >= 0) {
                const int target = selected + (id == IDC_PROFILE_UP ? -1 : 1);
                if (target >= 0 && target < static_cast<int>(profiles.size())) { std::swap(profiles[static_cast<std::size_t>(selected)], profiles[static_cast<std::size_t>(target)]); SaveProfileChanges(profiles); FillProfileList(hwnd); ListView_SetItemState(GetDlgItem(hwnd, IDC_PROFILE_LIST), target, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED); }
            }
            return 0;
        }
        case WM_CLOSE: ShowWindow(hwnd, SW_HIDE); return 0;
        case WM_DESTROY: g_profilesWindow = nullptr; return 0;
        default: return DefWindowProcW(hwnd, message, wParam, lParam);
    }
}
void ShowProfilesWindow() {
    if (!g_profilesWindow || !IsWindow(g_profilesWindow)) {
        const UINT dpi = SystemDpi();
        g_profilesWindow = CreateWindowExW(WS_EX_APPWINDOW, PROFILES_CLASS, L"LegionGoControl Game Profiles", WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
                                           CW_USEDEFAULT, CW_USEDEFAULT, DpiScale(960, dpi), DpiScale(570, dpi), nullptr, nullptr, g_instance, nullptr);
        if (g_profilesWindow) CenterWindow(g_profilesWindow, g_settings);
    } else FillProfileList(g_profilesWindow);
    if (g_profilesWindow) { ShowWindow(g_profilesWindow, SW_SHOWNORMAL); SetForegroundWindow(g_profilesWindow); }
}

// ---------- tray and hidden window ----------
HICON CreateTrayIcon() {
    HDC screen = GetDC(nullptr), memory = CreateCompatibleDC(screen);
    std::vector<BYTE> maskBits(32U * 32U / 8U, 0);
    HBITMAP color = CreateCompatibleBitmap(screen, 32, 32), mask = CreateBitmap(32, 32, 1, 1, maskBits.data());
    HGDIOBJ old = SelectObject(memory, color); RECT area{0,0,32,32}; HBRUSH background = CreateSolidBrush(RGB(25,30,38)); FillRect(memory, &area, background); DeleteObject(background);
    HBRUSH accent = CreateSolidBrush(RGB(0,120,215)); HGDIOBJ oldBrush = SelectObject(memory, accent); RoundRect(memory, 4, 6, 28, 26, 7, 7); SelectObject(memory, oldBrush); DeleteObject(accent);
    SetBkMode(memory, TRANSPARENT); SetTextColor(memory, RGB(255,255,255)); HFONT smallFont = CreateFontW(12,0,0,0,FW_BOLD,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Segoe UI");
    HGDIOBJ oldFont = SelectObject(memory, smallFont); RECT text{0,8,32,25}; DrawTextW(memory,L"GO",-1,&text,DT_CENTER|DT_SINGLELINE|DT_VCENTER); SelectObject(memory, oldFont); DeleteObject(smallFont); SelectObject(memory, old);
    ICONINFO info{}; info.fIcon = TRUE; info.hbmColor = color; info.hbmMask = mask; HICON icon = CreateIconIndirect(&info);
    DeleteObject(color); DeleteObject(mask); DeleteDC(memory); ReleaseDC(nullptr, screen); return icon ? icon : LoadIconW(nullptr, IDI_APPLICATION);
}
bool AddTrayIcon(HWND hwnd) {
    ZeroMemory(&g_nid, sizeof(g_nid)); g_nid.cbSize = sizeof(g_nid); g_nid.hWnd = hwnd; g_nid.uID = 1;
    g_nid.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP | NIF_SHOWTIP; g_nid.uCallbackMessage = WMAPP_TRAY; g_nid.hIcon = g_icon;
    wcsncpy_s(g_nid.szTip, APP_VERSION, _TRUNCATE); const bool result = Shell_NotifyIconW(NIM_ADD, &g_nid) == TRUE;
    g_nid.uVersion = NOTIFYICON_VERSION_4; Shell_NotifyIconW(NIM_SETVERSION, &g_nid); return result;
}
unsigned g_presentedBatteryToggleSequence = 0;
unsigned g_presentedBrightnessSetSequence = 0;
unsigned g_presentedFanOperationSequence = 0;
void UpdateTrayTip() {
    const RuntimeStatus status = RuntimeSnapshot();
    std::wstring tip = std::wstring(APP_VERSION_SHORT) + L" | " +
        (status.profileActive ? L"Active: " + status.profileName : L"Active: Base");
    tip += L" (STAPM " + std::to_wstring(status.desired.stapm) + L" / SLOW " +
           std::to_wstring(status.desired.slow) + L" / FAST " + std::to_wstring(status.desired.fast) + L" W)";
    NOTIFYICONDATAW copy = g_nid; copy.uFlags = NIF_TIP; wcsncpy_s(copy.szTip, tip.c_str(), _TRUNCATE); Shell_NotifyIconW(NIM_MODIFY, &copy);
    if (status.batteryToggleSequence != g_presentedBatteryToggleSequence) {
        g_presentedBatteryToggleSequence = status.batteryToggleSequence;
        if (!status.batteryToggleOk) Balloon(status.battery.error.empty() ? L"Battery limit operation failed." : status.battery.error, NIIF_ERROR);
        else Balloon(status.battery.known ? (status.battery.enabled ? L"Battery limit 80% enabled." : L"Battery limit 80% disabled.") : L"Battery limit toggled.");
    }
    if (status.brightnessSetSequence != g_presentedBrightnessSetSequence) {
        g_presentedBrightnessSetSequence = status.brightnessSetSequence;
        if (!status.brightnessSetOk)
            Balloon(L"Screen brightness could not be changed. Details were written to LegionGoControl.log.", NIIF_ERROR);
    }
    if (status.fanOperationSequence != g_presentedFanOperationSequence) {
        g_presentedFanOperationSequence = status.fanOperationSequence;
        Balloon(status.fanOperationOk ? L"Fan curve operation verified." : L"Fan curve operation failed; see the log.",
                status.fanOperationOk ? NIIF_INFO : NIIF_ERROR);
    }
}
void ShowTrayMenu(HWND hwnd) {
    const RuntimeStatus status = RuntimeSnapshot(); HMENU menu = CreatePopupMenu();
    std::wstring active = status.profileActive ? L"Active: " + status.profileName : L"Active: Base";
    active += L" - STAPM " + std::to_wstring(status.desired.stapm) + L" / SLOW " +
              std::to_wstring(status.desired.slow) + L" / FAST " + std::to_wstring(status.desired.fast) + L" W";
    if (status.applyKnown && !status.applyOk) active += L" (apply failed)";
    AppendMenuW(menu, MF_STRING | MF_DISABLED | MF_GRAYED, 0, APP_VERSION);
    AppendMenuW(menu, MF_STRING | MF_DISABLED | MF_GRAYED, 0, active.c_str()); AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, ID_TRAY_SETTINGS, L"Settings...");
    AppendMenuW(menu, MF_STRING, ID_TRAY_PROFILES, L"Game Profiles...");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, ID_TRAY_EXIT, L"Exit");
    POINT point{}; GetCursorPos(&point); SetForegroundWindow(hwnd);
    const UINT selected = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_NONOTIFY | TPM_RIGHTBUTTON, point.x, point.y, 0, hwnd, nullptr);
    DestroyMenu(menu); PostMessageW(hwnd, WM_NULL, 0, 0); if (selected) PostMessageW(hwnd, WM_COMMAND, selected, 0);
}
void TrayCommand(UINT id) {
    if (id == ID_TRAY_SETTINGS) ShowSettings(0);
    else if (id == ID_TRAY_PROFILES) ShowProfilesWindow();
    else if (id == ID_TRAY_EXIT) DestroyWindow(g_hidden);
}
bool TrayMouseEvent(UINT message) {
    return message == WM_LBUTTONUP || message == WM_RBUTTONUP || message == WM_CONTEXTMENU || message == NIN_SELECT || message == NIN_KEYSELECT;
}
LRESULT CALLBACK HiddenProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
        case WM_CREATE: {
            RAWINPUTDEVICE input{}; input.usUsagePage = 0xFFA0; input.usUsage = 0x0001; input.dwFlags = RIDEV_INPUTSINK; input.hwndTarget = hwnd;
            if (!RegisterRawInputDevices(&input, 1, sizeof(input))) LogAlways(L"Raw Input registration failed: " + std::to_wstring(GetLastError()));
            if (!AddTrayIcon(hwnd)) return -1;
            SetTimer(hwnd, FAN_TIMER_ID, 1000, nullptr);
            return 0;
        }
        case WM_INPUT: HandleRawInput(reinterpret_cast<HRAWINPUT>(lParam)); return 0;
        case WM_COMMAND: TrayCommand(LOWORD(wParam)); return 0;
        case WMAPP_TRAY: { const UINT event = LOWORD(lParam); if (TrayMouseEvent(event) || TrayMouseEvent(static_cast<UINT>(lParam))) ShowTrayMenu(hwnd); return 0; }
        case WMAPP_SHOW_SETTINGS: ShowSettings(static_cast<int>(wParam)); return 0;
        case WM_TIMER:
            if (wParam == FAN_TIMER_ID) {
                bool needTelemetry = LegionGoOverlay::IsVisible();
                if (g_settings && IsWindowVisible(g_settings) && !IsIconic(g_settings)) {
                    SettingsState* settings = SettingsData(g_settings);
                    needTelemetry = needTelemetry || (settings && settings->tab == 3);
                }
                if (needTelemetry) QueueWorker(WorkerJob::FanStatus);
            }
            return 0;
        case WMAPP_WORKER_UPDATE: {
            const RuntimeStatus status = RuntimeSnapshot();
            LegionGoOverlay::SetFirmwareTelemetry(status.fan.temperatureC, status.fan.rpm, status.fan.known);
            UpdateTrayTip(); UpdateSettingsRuntime();
            if (g_profilesWindow && IsWindowVisible(g_profilesWindow)) FillProfileList(g_profilesWindow);
            return 0;
        }
        case WM_QUERYENDSESSION: return TRUE;
        case WM_ENDSESSION:
            if (wParam) { LegionGoOverlay::Shutdown(); StopWorker(); }
            return 0;
        case WM_DESTROY:
            KillTimer(hwnd, FAN_TIMER_ID); LegionGoOverlay::Shutdown(); StopWorker();
            if (g_settings && IsWindow(g_settings)) DestroyWindow(g_settings);
            if (g_profilesWindow && IsWindow(g_profilesWindow)) DestroyWindow(g_profilesWindow);
            Shell_NotifyIconW(NIM_DELETE, &g_nid); PostQuitMessage(0); return 0;
        default:
            if (g_taskbarCreated != 0 && message == g_taskbarCreated) {
                AddTrayIcon(hwnd);
                UpdateTrayTip();
                return 0;
            }
            return DefWindowProcW(hwnd, message, wParam, lParam);
    }
}
void RegisterWindowClass(const wchar_t* name, WNDPROC procedure, HBRUSH background = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1)) {
    WNDCLASSW value{}; value.lpfnWndProc = procedure; value.hInstance = g_instance; value.lpszClassName = name;
    value.hIcon = g_windowIcon ? g_windowIcon : LoadIconW(nullptr, IDI_APPLICATION);
    value.hCursor = LoadCursorW(nullptr, IDC_ARROW); value.hbrBackground = background; RegisterClassW(&value);
}

} // namespace

int APIENTRY wWinMain(HINSTANCE instance, HINSTANCE, LPWSTR, int) {
    g_instance = instance;
    g_taskbarCreated = RegisterWindowMessageW(L"TaskbarCreated");
    InitializePaths(); CreateDefaultConfiguration(); InitializeButtons(); LoadConfiguration();
    if (!IsAdministrator()) { DWORD code = 1; RunHiddenCommand(L"schtasks /Run /TN \"LegionGoControl\"", code); return 0; }
    g_singleton = CreateMutexW(nullptr, TRUE, SINGLETON_MUTEX);
    if (!g_singleton) {
        Message(nullptr, L"LegionGoControl", L"The singleton mutex could not be created.", MB_OK | MB_ICONERROR);
        return 1;
    }
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        HWND existing = FindWindowW(HIDDEN_CLASS, nullptr); if (existing) PostMessageW(existing, WMAPP_SHOW_SETTINGS, 2, 0);
        CloseHandle(g_singleton); g_singleton = nullptr; return 0;
    }
    INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_STANDARD_CLASSES | ICC_TAB_CLASSES | ICC_LISTVIEW_CLASSES | ICC_UPDOWN_CLASS | ICC_BAR_CLASSES}; InitCommonControlsEx(&controls);
    const UINT dpi = SystemDpi();
    g_font = CreateFontW(-MulDiv(10, static_cast<int>(dpi), 72), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                         DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                         DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    g_windowIcon = static_cast<HICON>(LoadImageW(instance, MAKEINTRESOURCEW(IDI_APP_ICON), IMAGE_ICON,
                                                 GetSystemMetrics(SM_CXICON), GetSystemMetrics(SM_CYICON), LR_DEFAULTCOLOR));
    RegisterWindowClass(HIDDEN_CLASS, HiddenProc, nullptr); RegisterWindowClass(SETTINGS_CLASS, SettingsProc);
    RegisterWindowClass(PROFILES_CLASS, ProfilesProc); RegisterWindowClass(PROFILE_EDITOR_CLASS, ProfileEditorProc);
    RegisterWindowClass(FAN_CURVE_CLASS, FanCurveProc);
    g_icon = CreateTrayIcon();
    g_hidden = CreateWindowExW(0, HIDDEN_CLASS, APP_TITLE, WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 300, 200, nullptr, nullptr, instance, nullptr);
    if (!g_hidden) {
        if (g_icon) DestroyIcon(g_icon); if (g_windowIcon) DestroyIcon(g_windowIcon); if (g_font) DeleteObject(g_font);
        if (g_singleton) { ReleaseMutex(g_singleton); CloseHandle(g_singleton); } return 2;
    }
    std::wstring fpsRecoveryError;
    if (!RestoreFpsBackup(fpsRecoveryError)) {
        g_fpsLimiterReady = false; LogAlways(L"FPS limiter crash recovery failed: " + fpsRecoveryError);
    }
    if (!LegionGoOverlay::Initialize(instance, g_baseDir, g_overlayConfig))
        LogAlways(L"Overlay initialization failed (window unavailable).");
    else if (LegionGoOverlay::ActiveFunctionKey() == 0)
        LogAlways(L"Overlay started, but F" + std::to_wstring(g_overlayConfig.functionKey) + L" is already registered by another application.");
    StartWorker();
    ShowWindow(g_hidden, SW_HIDE);
    MSG message{};
    int getMessageResult = 1;
    while ((getMessageResult = static_cast<int>(GetMessageW(&message, nullptr, 0, 0))) > 0) {
        if (g_settings && IsWindowVisible(g_settings) && IsDialogMessageW(g_settings, &message)) continue;
        if (g_profilesWindow && IsWindowVisible(g_profilesWindow) && IsDialogMessageW(g_profilesWindow, &message)) continue;
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    LegionGoOverlay::Shutdown(); StopWorker();
    if (getMessageResult < 0) LogAlways(L"GetMessage failed: " + std::to_wstring(GetLastError()));
    if (g_icon) DestroyIcon(g_icon); if (g_windowIcon) DestroyIcon(g_windowIcon); if (g_font) DeleteObject(g_font);
    if (g_singleton) { ReleaseMutex(g_singleton); CloseHandle(g_singleton); }
    return static_cast<int>(message.wParam);
}
