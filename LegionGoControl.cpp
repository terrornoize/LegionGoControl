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
#include <shellapi.h>

#include <algorithm>
#include <cwctype>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#pragma comment(lib, "user32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "advapi32.lib")

static const wchar_t* APP_NAME = L"LegionGoControl";
static const wchar_t* APP_TITLE = L"LegionGoControl";
static const wchar_t* WINDOW_CLASS_NAME = L"LegionGoControlHiddenWindow";
static const wchar_t* TDP_WINDOW_CLASS_NAME = L"LegionGoControlTdpWindow";
static const wchar_t* MUTEX_NAME = L"Global\\LegionGoControlSingletonMutex";

static constexpr UINT WMAPP_TRAYICON = WM_APP + 100;
static constexpr UINT WMAPP_SHOW_TDP = WM_APP + 101;

static constexpr int TDP_MIN_W = 5;
static constexpr int TDP_MAX_W = 35;

static constexpr UINT ID_TRAY_EXIT             = 40001;
static constexpr UINT ID_TRAY_LOGGING          = 40002;
static constexpr UINT ID_TRAY_STARTUP          = 40003;
static constexpr UINT ID_TRAY_RELOAD_CONFIG    = 40004;
static constexpr UINT ID_TRAY_OPEN_CONFIG      = 40005;
static constexpr UINT ID_TRAY_OPEN_LOG         = 40006;
static constexpr UINT ID_TRAY_BATTERY_LIMIT_80 = 40007;
static constexpr UINT ID_TRAY_SHOW_TDP         = 40008;

static constexpr UINT ID_TDP_5                 = 40105;
static constexpr UINT ID_TDP_8                 = 40108;
static constexpr UINT ID_TDP_10                = 40110;
static constexpr UINT ID_TDP_12                = 40112;
static constexpr UINT ID_TDP_16                = 40116;
static constexpr UINT ID_TDP_20                = 40120;
static constexpr UINT ID_TDP_25                = 40125;
static constexpr UINT ID_TDP_30                = 40130;
static constexpr UINT ID_TDP_CUSTOM            = 40199;

static HINSTANCE g_hInst = nullptr;
static HWND g_hwnd = nullptr;
static HWND g_tdpWnd = nullptr;
static NOTIFYICONDATAW g_nid = {};
static HICON g_hIcon = nullptr;
static HANDLE g_mutex = nullptr;

static std::wstring g_exePath;
static std::wstring g_baseDir;
static std::wstring g_iniPath;
static std::wstring g_logPath;
static std::wstring g_backendPath;
static std::wstring g_tdpStatePath;
static std::wstring g_batteryStatePath;

static bool g_loggingEnabled = false;
static int g_debounceMs = 40;
static int g_actionCooldownMs = 250;

static int g_customStapmW = 12;
static int g_customFastW = 16;
static int g_customSlowW = 16;

static std::wstring Trim(const std::wstring& s) {
    size_t a = 0;

    while (a < s.size() && (iswspace(s[a]) || s[a] == 0xFEFF)) {
        ++a;
    }

    size_t b = s.size();

    while (b > a && iswspace(s[b - 1])) {
        --b;
    }

    return s.substr(a, b - a);
}

static std::wstring ToUpper(std::wstring s) {
    std::transform(s.begin(), s.end(), s.begin(), [](wchar_t c) {
        return static_cast<wchar_t>(towupper(c));
    });

    return s;
}

static std::vector<std::wstring> Split(const std::wstring& s, wchar_t sep) {
    std::vector<std::wstring> out;
    std::wstringstream ss(s);
    std::wstring item;

    while (std::getline(ss, item, sep)) {
        out.push_back(Trim(item));
    }

    return out;
}

static bool StartsWith(const std::wstring& s, const std::wstring& prefix) {
    return s.rfind(prefix, 0) == 0;
}

static bool FileExists(const std::wstring& path) {
    DWORD attr = GetFileAttributesW(path.c_str());
    return attr != INVALID_FILE_ATTRIBUTES && !(attr & FILE_ATTRIBUTE_DIRECTORY);
}

static std::wstring GetDirectoryOfPath(const std::wstring& path) {
    size_t p = path.find_last_of(L"\\/");

    if (p == std::wstring::npos) {
        return L".";
    }

    return path.substr(0, p);
}

static std::wstring NowString() {
    SYSTEMTIME st = {};
    GetLocalTime(&st);

    wchar_t buf[64] = {};

    swprintf_s(
        buf,
        L"%04u-%02u-%02u %02u:%02u:%02u.%03u",
        st.wYear,
        st.wMonth,
        st.wDay,
        st.wHour,
        st.wMinute,
        st.wSecond,
        st.wMilliseconds
    );

    return buf;
}

static void InitPaths() {
    wchar_t exe[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, exe, MAX_PATH);

    g_exePath = exe;
    g_baseDir = GetDirectoryOfPath(g_exePath);
    g_iniPath = g_baseDir + L"\\LegionGoControl.ini";
    g_logPath = g_baseDir + L"\\LegionGoControl.log";
    g_backendPath = g_baseDir + L"\\LegionGoNativeWmiProbe.exe";
    g_tdpStatePath = g_baseDir + L"\\LegionGoNativeWmiProbe_state.txt";
    g_batteryStatePath = g_baseDir + L"\\LegionGoBatteryLimitState.txt";
}

static void LogLineAlways(const std::wstring& line) {
    std::wofstream f(g_logPath.c_str(), std::ios::app);

    if (!f) {
        return;
    }

    f << L"[" << NowString() << L"] " << line << L"\n";
}

static void LogLine(const std::wstring& line) {
    if (!g_loggingEnabled) {
        return;
    }

    LogLineAlways(line);
}

static bool IsAdmin() {
    BOOL isAdmin = FALSE;
    PSID adminGroup = nullptr;
    SID_IDENTIFIER_AUTHORITY ntAuthority = SECURITY_NT_AUTHORITY;

    if (AllocateAndInitializeSid(
            &ntAuthority,
            2,
            SECURITY_BUILTIN_DOMAIN_RID,
            DOMAIN_ALIAS_RID_ADMINS,
            0,
            0,
            0,
            0,
            0,
            0,
            &adminGroup)) {
        CheckTokenMembership(nullptr, adminGroup, &isAdmin);
        FreeSid(adminGroup);
    }

    return isAdmin == TRUE;
}

static void ShowMessage(const std::wstring& title, const std::wstring& text, UINT flags = MB_OK | MB_ICONINFORMATION) {
    MessageBoxW(g_hwnd, text.c_str(), title.c_str(), flags);
}

static void ShowBalloon(const std::wstring& title, const std::wstring& text, DWORD icon = NIIF_INFO) {
    if (!g_hwnd) {
        return;
    }

    NOTIFYICONDATAW nid = g_nid;
    nid.uFlags = NIF_INFO;
    nid.dwInfoFlags = icon;

    wcsncpy_s(nid.szInfoTitle, title.c_str(), _TRUNCATE);
    wcsncpy_s(nid.szInfo, text.c_str(), _TRUNCATE);

    Shell_NotifyIconW(NIM_MODIFY, &nid);
}

static bool RunHiddenCommand(const std::wstring& command, DWORD& exitCode, DWORD timeoutMs = 30000) {
    exitCode = 0xFFFFFFFF;

    std::wstring full = L"cmd.exe /C " + command;
    std::vector<wchar_t> buf(full.begin(), full.end());
    buf.push_back(L'\0');

    STARTUPINFOW si = {};
    PROCESS_INFORMATION pi = {};

    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;

    BOOL ok = CreateProcessW(
        nullptr,
        buf.data(),
        nullptr,
        nullptr,
        FALSE,
        CREATE_NO_WINDOW,
        nullptr,
        nullptr,
        &si,
        &pi
    );

    if (!ok) {
        return false;
    }

    DWORD wait = WaitForSingleObject(pi.hProcess, timeoutMs);

    if (wait == WAIT_OBJECT_0) {
        GetExitCodeProcess(pi.hProcess, &exitCode);
    } else {
        TerminateProcess(pi.hProcess, 99);
        exitCode = 99;
    }

    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);

    return wait == WAIT_OBJECT_0;
}

static bool RunBackendProcess(const std::wstring& args, DWORD& exitCode, DWORD timeoutMs = 5000) {
    exitCode = 0xFFFFFFFF;

    if (!FileExists(g_backendPath)) {
        LogLineAlways(L"Backend non trovato: " + g_backendPath);
        return false;
    }

    std::wstring commandLine = L"\"" + g_backendPath + L"\" " + args;
    std::vector<wchar_t> buf(commandLine.begin(), commandLine.end());
    buf.push_back(L'\0');

    STARTUPINFOW si = {};
    PROCESS_INFORMATION pi = {};

    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;

    BOOL ok = CreateProcessW(
        g_backendPath.c_str(),
        buf.data(),
        nullptr,
        nullptr,
        FALSE,
        CREATE_NO_WINDOW,
        nullptr,
        g_baseDir.c_str(),
        &si,
        &pi
    );

    if (!ok) {
        DWORD err = GetLastError();
        LogLineAlways(L"CreateProcess backend fallita. GetLastError=" + std::to_wstring(err));
        return false;
    }

    DWORD wait = WaitForSingleObject(pi.hProcess, timeoutMs);

    if (wait == WAIT_OBJECT_0) {
        GetExitCodeProcess(pi.hProcess, &exitCode);
    } else {
        TerminateProcess(pi.hProcess, 99);
        exitCode = 99;
    }

    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);

    return wait == WAIT_OBJECT_0;
}

static bool RunBackendSetTdp(int stapm, int fast, int slow, DWORD& exitCode) {
    DeleteFileW(g_tdpStatePath.c_str());

    std::wstring args =
        L"set " +
        std::to_wstring(stapm) + L" " +
        std::to_wstring(fast) + L" " +
        std::to_wstring(slow);

    bool ok = RunBackendProcess(args, exitCode, 5000);

    DeleteFileW(g_tdpStatePath.c_str());

    return ok && exitCode == 0;
}

static bool RunBackendBatteryStatus(DWORD& exitCode) {
    return RunBackendProcess(L"battery-status", exitCode, 5000);
}

static bool RunBackendBatteryToggle(DWORD& exitCode) {
    return RunBackendProcess(L"battery-toggle", exitCode, 5000);
}

static bool ReadUtf16File(const std::wstring& path, std::wstring& out) {
    out.clear();

    HANDLE h = CreateFileW(
        path.c_str(),
        GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        nullptr,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        nullptr
    );

    if (h == INVALID_HANDLE_VALUE) {
        return false;
    }

    DWORD size = GetFileSize(h, nullptr);

    if (size == INVALID_FILE_SIZE || size == 0) {
        CloseHandle(h);
        return false;
    }

    std::vector<BYTE> bytes(size);
    DWORD read = 0;

    BOOL ok = ReadFile(h, bytes.data(), size, &read, nullptr);
    CloseHandle(h);

    if (!ok || read == 0) {
        return false;
    }

    size_t offset = 0;

    if (read >= 2 && bytes[0] == 0xFF && bytes[1] == 0xFE) {
        offset = 2;
    }

    if (read <= offset) {
        return false;
    }

    size_t wcharCount = (read - offset) / sizeof(wchar_t);

    if (wcharCount == 0) {
        return false;
    }

    const wchar_t* p = reinterpret_cast<const wchar_t*>(bytes.data() + offset);
    out.assign(p, p + wcharCount);

    return true;
}

static std::wstring GetField(const std::wstring& text, const std::wstring& key) {
    std::wstringstream ss(text);
    std::wstring line;
    std::wstring prefix = key + L"=";

    while (std::getline(ss, line)) {
        line = Trim(line);

        if (StartsWith(line, prefix)) {
            return line.substr(prefix.size());
        }
    }

    return L"";
}

struct BatteryStatus {
    bool ok = false;
    bool enabled = false;
    std::wstring error;
};

static bool ReadBatteryStateFile(BatteryStatus& st) {
    std::wstring text;

    if (!ReadUtf16File(g_batteryStatePath, text)) {
        st.ok = false;
        st.error = L"Nessuno stato batteria disponibile.";
        return false;
    }

    std::wstring value = GetField(text, L"BatteryLimit80");

    if (value == L"True") {
        st.ok = true;
        st.enabled = true;
        st.error.clear();
        return true;
    }

    if (value == L"False") {
        st.ok = true;
        st.enabled = false;
        st.error.clear();
        return true;
    }

    st.ok = false;
    st.enabled = false;
    st.error = GetField(text, L"Error");
    return false;
}

static BatteryStatus GetBatteryStatusForMenu() {
    BatteryStatus st;

    DWORD code = 0xFFFFFFFF;
    RunBackendBatteryStatus(code);

    if (ReadBatteryStateFile(st)) {
        return st;
    }

    st.ok = false;
    st.enabled = false;
    st.error = L"Battery status unavailable.";
    return st;
}

static std::wstring IniReadString(const wchar_t* section, const wchar_t* key, const wchar_t* fallback) {
    wchar_t buf[2048] = {};
    GetPrivateProfileStringW(section, key, fallback, buf, static_cast<DWORD>(_countof(buf)), g_iniPath.c_str());
    return Trim(buf);
}

static int IniReadInt(const wchar_t* section, const wchar_t* key, int fallback) {
    return static_cast<int>(GetPrivateProfileIntW(section, key, fallback, g_iniPath.c_str()));
}

static void CreateDefaultConfigIfMissing() {
    if (FileExists(g_iniPath)) {
        return;
    }

    WritePrivateProfileStringW(L"General", L"logging", L"0", g_iniPath.c_str());
    WritePrivateProfileStringW(L"General", L"debounce_ms", L"40", g_iniPath.c_str());
    WritePrivateProfileStringW(L"General", L"action_cooldown_ms", L"250", g_iniPath.c_str());

    WritePrivateProfileStringW(L"Menu", L"enabled", L"1", g_iniPath.c_str());
    WritePrivateProfileStringW(L"Menu", L"trigger", L"down", g_iniPath.c_str());
    WritePrivateProfileStringW(L"Menu", L"action", L"keys", g_iniPath.c_str());
    WritePrivateProfileStringW(L"Menu", L"keys", L"WIN+CTRL+O", g_iniPath.c_str());

    WritePrivateProfileStringW(L"View", L"enabled", L"1", g_iniPath.c_str());
    WritePrivateProfileStringW(L"View", L"trigger", L"down", g_iniPath.c_str());
    WritePrivateProfileStringW(L"View", L"action", L"keys", g_iniPath.c_str());
    WritePrivateProfileStringW(L"View", L"keys", L"WIN+TAB", g_iniPath.c_str());

    WritePrivateProfileStringW(L"Y1", L"enabled", L"0", g_iniPath.c_str());
    WritePrivateProfileStringW(L"Y1", L"trigger", L"down", g_iniPath.c_str());
    WritePrivateProfileStringW(L"Y1", L"action", L"keys", g_iniPath.c_str());
    WritePrivateProfileStringW(L"Y1", L"keys", L"F13", g_iniPath.c_str());

    WritePrivateProfileStringW(L"Y2", L"enabled", L"0", g_iniPath.c_str());
    WritePrivateProfileStringW(L"Y2", L"trigger", L"down", g_iniPath.c_str());
    WritePrivateProfileStringW(L"Y2", L"action", L"keys", g_iniPath.c_str());
    WritePrivateProfileStringW(L"Y2", L"keys", L"ALT+TAB", g_iniPath.c_str());

    WritePrivateProfileStringW(L"Y3", L"enabled", L"0", g_iniPath.c_str());
    WritePrivateProfileStringW(L"Y3", L"trigger", L"down", g_iniPath.c_str());
    WritePrivateProfileStringW(L"Y3", L"action", L"launch", g_iniPath.c_str());
    WritePrivateProfileStringW(L"Y3", L"path", L"", g_iniPath.c_str());
    WritePrivateProfileStringW(L"Y3", L"args", L"", g_iniPath.c_str());
    WritePrivateProfileStringW(L"Y3", L"working_dir", L"", g_iniPath.c_str());

    WritePrivateProfileStringW(L"M2", L"enabled", L"0", g_iniPath.c_str());
    WritePrivateProfileStringW(L"M2", L"trigger", L"down", g_iniPath.c_str());
    WritePrivateProfileStringW(L"M2", L"action", L"keys", g_iniPath.c_str());
    WritePrivateProfileStringW(L"M2", L"keys", L"F14", g_iniPath.c_str());

    WritePrivateProfileStringW(L"M3", L"enabled", L"1", g_iniPath.c_str());
    WritePrivateProfileStringW(L"M3", L"trigger", L"down", g_iniPath.c_str());
    WritePrivateProfileStringW(L"M3", L"action", L"internal", g_iniPath.c_str());
    WritePrivateProfileStringW(L"M3", L"internal", L"show_tdp", g_iniPath.c_str());

    WritePrivateProfileStringW(L"TDP", L"CustomStapmW", L"12", g_iniPath.c_str());
    WritePrivateProfileStringW(L"TDP", L"CustomFastW", L"16", g_iniPath.c_str());
    WritePrivateProfileStringW(L"TDP", L"CustomSlowW", L"16", g_iniPath.c_str());
}

static bool ValidateTdpTriple(int stapm, int fast, int slow, std::wstring* error) {
    auto fail = [&](const std::wstring& msg) -> bool {
        if (error) {
            *error = msg;
        }
        return false;
    };

    if (stapm < TDP_MIN_W || stapm > TDP_MAX_W) {
        return fail(L"STAPM fuori range 5-35W.");
    }

    if (fast < TDP_MIN_W || fast > TDP_MAX_W) {
        return fail(L"FAST fuori range 5-35W.");
    }

    if (slow < TDP_MIN_W || slow > TDP_MAX_W) {
        return fail(L"SLOW fuori range 5-35W.");
    }

    if (stapm > fast) {
        return fail(L"STAPM non può essere superiore a FAST.");
    }

    if (stapm > slow) {
        return fail(L"STAPM non può essere superiore a SLOW.");
    }

    return true;
}

static bool IsStartupEnabled() {
    DWORD code = 1;
    RunHiddenCommand(L"schtasks /Query /TN \"LegionGoControl\" >NUL 2>NUL", code);
    return code == 0;
}

static bool SetStartupEnabled(bool enabled) {
    DWORD code = 1;

    if (enabled) {
        std::wstring cmd =
            L"schtasks /Create /TN \"LegionGoControl\" /TR \"\\\"" +
            g_exePath +
            L"\\\"\" /SC ONLOGON /RL HIGHEST /F";

        bool ok = RunHiddenCommand(cmd, code);
        return ok && code == 0;
    }

    bool ok = RunHiddenCommand(L"schtasks /Delete /TN \"LegionGoControl\" /F", code);
    return ok && code == 0;
}

enum class ButtonActionType {
    None,
    Keys,
    Launch,
    Internal
};

struct ButtonBinding {
    std::wstring name;
    size_t byteIndex = 0;
    BYTE mask = 0;

    bool enabled = false;
    bool triggerDown = true;
    ButtonActionType actionType = ButtonActionType::None;

    std::wstring keys;
    std::wstring path;
    std::wstring args;
    std::wstring workingDir;
    std::wstring internalAction;

    bool pressed = false;
    ULONGLONG lastChangeTick = 0;
    ULONGLONG lastFireTick = 0;
};

static std::vector<ButtonBinding> g_buttons;

static int VkFromToken(const std::wstring& tokenRaw) {
    std::wstring t = ToUpper(Trim(tokenRaw));

    if (t == L"CTRL" || t == L"CONTROL") return VK_CONTROL;
    if (t == L"SHIFT") return VK_SHIFT;
    if (t == L"ALT") return VK_MENU;
    if (t == L"WIN" || t == L"WINDOWS" || t == L"META") return VK_LWIN;

    if (t == L"ENTER" || t == L"RETURN") return VK_RETURN;
    if (t == L"ESC" || t == L"ESCAPE") return VK_ESCAPE;
    if (t == L"TAB") return VK_TAB;
    if (t == L"SPACE") return VK_SPACE;
    if (t == L"BACKSPACE") return VK_BACK;
    if (t == L"DELETE" || t == L"DEL") return VK_DELETE;
    if (t == L"INSERT" || t == L"INS") return VK_INSERT;
    if (t == L"HOME") return VK_HOME;
    if (t == L"END") return VK_END;
    if (t == L"PGUP" || t == L"PAGEUP") return VK_PRIOR;
    if (t == L"PGDN" || t == L"PAGEDOWN") return VK_NEXT;
    if (t == L"UP") return VK_UP;
    if (t == L"DOWN") return VK_DOWN;
    if (t == L"LEFT") return VK_LEFT;
    if (t == L"RIGHT") return VK_RIGHT;

    if (t.size() == 1) {
        wchar_t c = t[0];

        if (c >= L'A' && c <= L'Z') return static_cast<int>(c);
        if (c >= L'0' && c <= L'9') return static_cast<int>(c);
    }

    if (t.size() >= 2 && t[0] == L'F') {
        int n = _wtoi(t.c_str() + 1);
        if (n >= 1 && n <= 24) return VK_F1 + (n - 1);
    }

    return 0;
}

static bool IsModifierVk(int vk) {
    return vk == VK_CONTROL ||
           vk == VK_SHIFT ||
           vk == VK_MENU ||
           vk == VK_LWIN ||
           vk == VK_RWIN;
}

static void SendKeyEvent(WORD vk, bool down) {
    INPUT input = {};
    input.type = INPUT_KEYBOARD;
    input.ki.wVk = vk;
    input.ki.dwFlags = down ? 0 : KEYEVENTF_KEYUP;

    SendInput(1, &input, sizeof(INPUT));
}

static bool SendHotkey(const std::wstring& actionRaw) {
    std::wstring action = Trim(actionRaw);

    if (action.empty()) return true;

    std::wstring upper = ToUpper(action);

    if (upper == L"NONE" || upper == L"NOOP" || upper == L"DISABLED") {
        return true;
    }

    std::vector<std::wstring> tokens = Split(action, L'+');
    std::vector<int> mods;
    std::vector<int> keys;

    for (const auto& token : tokens) {
        int vk = VkFromToken(token);

        if (vk == 0) {
            LogLine(L"Token tasto sconosciuto: " + token + L" / action=" + action);
            return false;
        }

        if (IsModifierVk(vk)) {
            mods.push_back(vk);
        } else {
            keys.push_back(vk);
        }
    }

    if (keys.empty()) return false;

    for (int vk : mods) SendKeyEvent(static_cast<WORD>(vk), true);
    for (int vk : keys) SendKeyEvent(static_cast<WORD>(vk), true);

    Sleep(20);

    for (auto it = keys.rbegin(); it != keys.rend(); ++it) SendKeyEvent(static_cast<WORD>(*it), false);
    for (auto it = mods.rbegin(); it != mods.rend(); ++it) SendKeyEvent(static_cast<WORD>(*it), false);

    return true;
}

static bool LaunchConfiguredProcess(const ButtonBinding& b) {
    if (Trim(b.path).empty()) return false;

    std::wstring work = Trim(b.workingDir);
    std::wstring args = Trim(b.args);

    HINSTANCE r = ShellExecuteW(
        nullptr,
        L"open",
        b.path.c_str(),
        args.empty() ? nullptr : args.c_str(),
        work.empty() ? nullptr : work.c_str(),
        SW_SHOWNORMAL
    );

    return reinterpret_cast<INT_PTR>(r) > 32;
}

static void ShowTdpWindow();

static void FireButton(ButtonBinding& b) {
    ULONGLONG now = GetTickCount64();

    if (now - b.lastFireTick < static_cast<ULONGLONG>(g_actionCooldownMs)) return;

    b.lastFireTick = now;

    if (!b.enabled) return;

    if (b.actionType == ButtonActionType::Keys) {
        SendHotkey(b.keys);
        return;
    }

    if (b.actionType == ButtonActionType::Launch) {
        LaunchConfiguredProcess(b);
        return;
    }

    if (b.actionType == ButtonActionType::Internal) {
        std::wstring act = ToUpper(Trim(b.internalAction));

        if (act == L"SHOW_MENU" || act == L"SHOW_TDP" || act == L"TDP" || act == L"SHOW_TDP_WINDOW") {
            PostMessageW(g_hwnd, WMAPP_SHOW_TDP, 0, 0);
        }

        return;
    }
}

static void ProcessButtonState(ButtonBinding& b, bool down) {
    ULONGLONG now = GetTickCount64();

    if (down == b.pressed) return;

    if (b.lastChangeTick != 0 &&
        now - b.lastChangeTick < static_cast<ULONGLONG>(g_debounceMs)) {
        return;
    }

    b.pressed = down;
    b.lastChangeTick = now;

    if (down && b.triggerDown) FireButton(b);
    else if (!down && !b.triggerDown) FireButton(b);
}

static void InitButtonDefinitions() {
    g_buttons.clear();

    g_buttons.push_back({ L"Y1",   20, 0x80 });
    g_buttons.push_back({ L"Y2",   20, 0x40 });
    g_buttons.push_back({ L"Y3",   20, 0x20 });
    g_buttons.push_back({ L"M2",   20, 0x08 });
    g_buttons.push_back({ L"M3",   20, 0x04 });
    g_buttons.push_back({ L"View", 18, 0x40 });
    g_buttons.push_back({ L"Menu", 18, 0x80 });
}

static ButtonActionType ParseActionType(const std::wstring& s) {
    std::wstring a = ToUpper(Trim(s));

    if (a == L"KEYS") return ButtonActionType::Keys;
    if (a == L"LAUNCH") return ButtonActionType::Launch;
    if (a == L"INTERNAL") return ButtonActionType::Internal;

    return ButtonActionType::None;
}

static void LoadOneButtonConfig(ButtonBinding& b) {
    std::wstring simple = IniReadString(L"Buttons", b.name.c_str(), L"");
    std::wstring actionText = IniReadString(b.name.c_str(), L"action", L"");

    if (!simple.empty() && actionText.empty()) {
        std::wstring upperSimple = ToUpper(simple);

        b.enabled = !(upperSimple == L"NONE" || upperSimple == L"NOOP" || upperSimple == L"DISABLED");
        b.triggerDown = true;
        b.actionType = b.enabled ? ButtonActionType::Keys : ButtonActionType::None;
        b.keys = simple;
        b.path.clear();
        b.args.clear();
        b.workingDir.clear();
        b.internalAction.clear();
        return;
    }

    int defaultEnabled = 0;
    std::wstring defaultAction = L"none";
    std::wstring defaultKeys;
    std::wstring defaultInternal;

    if (b.name == L"Menu") {
        defaultEnabled = 1;
        defaultAction = L"keys";
        defaultKeys = L"WIN+CTRL+O";
    } else if (b.name == L"View") {
        defaultEnabled = 1;
        defaultAction = L"keys";
        defaultKeys = L"WIN+TAB";
    } else if (b.name == L"M3") {
        defaultEnabled = 1;
        defaultAction = L"internal";
        defaultInternal = L"show_tdp";
    } else if (b.name == L"Y1") {
        defaultAction = L"keys";
        defaultKeys = L"F13";
    } else if (b.name == L"Y2") {
        defaultAction = L"keys";
        defaultKeys = L"ALT+TAB";
    } else if (b.name == L"M2") {
        defaultAction = L"keys";
        defaultKeys = L"F14";
    }

    b.enabled = IniReadInt(b.name.c_str(), L"enabled", defaultEnabled) != 0;
    b.triggerDown = ToUpper(IniReadString(b.name.c_str(), L"trigger", L"down")) != L"UP";
    b.actionType = ParseActionType(IniReadString(b.name.c_str(), L"action", defaultAction.c_str()));
    b.keys = IniReadString(b.name.c_str(), L"keys", defaultKeys.c_str());
    b.path = IniReadString(b.name.c_str(), L"path", L"");
    b.args = IniReadString(b.name.c_str(), L"args", L"");
    b.workingDir = IniReadString(b.name.c_str(), L"working_dir", L"");
    b.internalAction = IniReadString(b.name.c_str(), L"internal", defaultInternal.c_str());

    b.pressed = false;
    b.lastChangeTick = 0;
    b.lastFireTick = 0;
}

static void LoadConfig() {
    CreateDefaultConfigIfMissing();

    g_loggingEnabled = IniReadInt(L"General", L"logging", IniReadInt(L"General", L"Logging", 0)) != 0;
    g_debounceMs = IniReadInt(L"General", L"debounce_ms", 40);
    g_actionCooldownMs = IniReadInt(L"General", L"action_cooldown_ms", IniReadInt(L"General", L"ActionCooldownMs", 250));

    if (g_debounceMs < 0) g_debounceMs = 0;
    if (g_debounceMs > 1000) g_debounceMs = 1000;
    if (g_actionCooldownMs < 50) g_actionCooldownMs = 50;
    if (g_actionCooldownMs > 5000) g_actionCooldownMs = 5000;

    g_customStapmW = IniReadInt(L"TDP", L"CustomStapmW", 12);
    g_customFastW = IniReadInt(L"TDP", L"CustomFastW", 16);
    g_customSlowW = IniReadInt(L"TDP", L"CustomSlowW", 16);

    if (g_buttons.empty()) {
        InitButtonDefinitions();
    }

    for (auto& b : g_buttons) {
        LoadOneButtonConfig(b);
    }
}

static void SaveLoggingConfig() {
    WritePrivateProfileStringW(
        L"General",
        L"logging",
        g_loggingEnabled ? L"1" : L"0",
        g_iniPath.c_str()
    );
}

static void ProcessVendorReport(const BYTE* data, size_t size) {
    for (auto& b : g_buttons) {
        if (b.byteIndex >= size) continue;

        bool down = (data[b.byteIndex] & b.mask) != 0;
        ProcessButtonState(b, down);
    }
}

static void HandleRawInput(HRAWINPUT hRawInput) {
    UINT size = 0;

    if (GetRawInputData(hRawInput, RID_INPUT, nullptr, &size, sizeof(RAWINPUTHEADER)) != 0) {
        return;
    }

    if (size == 0) return;

    std::vector<BYTE> buffer(size);

    UINT read = GetRawInputData(hRawInput, RID_INPUT, buffer.data(), &size, sizeof(RAWINPUTHEADER));

    if (read != size) return;

    RAWINPUT* raw = reinterpret_cast<RAWINPUT*>(buffer.data());

    if (raw->header.dwType != RIM_TYPEHID) return;

    UINT count = raw->data.hid.dwCount;
    UINT reportSize = raw->data.hid.dwSizeHid;

    if (count == 0 || reportSize == 0) return;

    const BYTE* base = raw->data.hid.bRawData;

    for (UINT i = 0; i < count; ++i) {
        const BYTE* report = base + (i * reportSize);
        ProcessVendorReport(report, reportSize);
    }
}

static bool RegisterLegionRawInput(HWND hwnd) {
    RAWINPUTDEVICE rid = {};
    rid.usUsagePage = 0xFFA0;
    rid.usUsage = 0x0001;
    rid.dwFlags = RIDEV_INPUTSINK;
    rid.hwndTarget = hwnd;

    return RegisterRawInputDevices(&rid, 1, sizeof(rid)) == TRUE;
}

static HICON CreateTrayIcon() {
    HDC hdc = GetDC(nullptr);
    HDC mem = CreateCompatibleDC(hdc);

    HBITMAP color = CreateCompatibleBitmap(hdc, 32, 32);
    HBITMAP mask = CreateBitmap(32, 32, 1, 1, nullptr);

    HGDIOBJ old = SelectObject(mem, color);

    HBRUSH bg = CreateSolidBrush(RGB(20, 20, 20));
    RECT rc = { 0, 0, 32, 32 };
    FillRect(mem, &rc, bg);
    DeleteObject(bg);

    HBRUSH accent = CreateSolidBrush(RGB(80, 170, 255));
    HPEN pen = CreatePen(PS_SOLID, 2, RGB(80, 170, 255));

    HGDIOBJ oldBrush = SelectObject(mem, accent);
    HGDIOBJ oldPen = SelectObject(mem, pen);

    RoundRect(mem, 5, 7, 27, 25, 5, 5);

    SelectObject(mem, oldBrush);
    SelectObject(mem, oldPen);

    DeleteObject(accent);
    DeleteObject(pen);

    SetBkMode(mem, TRANSPARENT);
    SetTextColor(mem, RGB(255, 255, 255));

    HFONT font = CreateFontW(
        13,
        0,
        0,
        0,
        FW_BOLD,
        FALSE,
        FALSE,
        FALSE,
        DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS,
        CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY,
        DEFAULT_PITCH,
        L"Segoe UI"
    );

    HGDIOBJ oldFont = SelectObject(mem, font);

    RECT trc = { 0, 8, 32, 25 };
    DrawTextW(mem, L"GO", -1, &trc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

    SelectObject(mem, oldFont);
    DeleteObject(font);
    SelectObject(mem, old);

    ICONINFO ii = {};
    ii.fIcon = TRUE;
    ii.hbmColor = color;
    ii.hbmMask = mask;

    HICON icon = CreateIconIndirect(&ii);

    DeleteObject(color);
    DeleteObject(mask);
    DeleteDC(mem);
    ReleaseDC(nullptr, hdc);

    if (!icon) {
        icon = LoadIconW(nullptr, IDI_APPLICATION);
    }

    return icon;
}

static bool AddTrayIcon(HWND hwnd) {
    ZeroMemory(&g_nid, sizeof(g_nid));

    g_nid.cbSize = sizeof(g_nid);
    g_nid.hWnd = hwnd;
    g_nid.uID = 1;
    g_nid.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    g_nid.uCallbackMessage = WMAPP_TRAYICON;
    g_nid.hIcon = g_hIcon;

    wcsncpy_s(g_nid.szTip, APP_TITLE, _TRUNCATE);

    return Shell_NotifyIconW(NIM_ADD, &g_nid) == TRUE;
}

static void RemoveTrayIcon() {
    if (g_nid.hWnd) {
        Shell_NotifyIconW(NIM_DELETE, &g_nid);
    }
}

static UINT TdpMenuIdToWatts(UINT id) {
    switch (id) {
        case ID_TDP_5: return 5;
        case ID_TDP_8: return 8;
        case ID_TDP_10: return 10;
        case ID_TDP_12: return 12;
        case ID_TDP_16: return 16;
        case ID_TDP_20: return 20;
        case ID_TDP_25: return 25;
        case ID_TDP_30: return 30;
        default: return 0;
    }
}

static void ApplyTdpTriple(int stapm, int fast, int slow) {
    std::wstring validationError;

    if (!ValidateTdpTriple(stapm, fast, slow, &validationError)) {
        ShowMessage(L"TDP", validationError, MB_OK | MB_ICONERROR);
        return;
    }

    DWORD code = 0xFFFFFFFF;
    bool ok = RunBackendSetTdp(stapm, fast, slow, code);

    if (ok) {
        std::wstringstream ss;
        ss << L"TDP impostato: " << stapm << L"/" << fast << L"/" << slow << L"W";
        ShowBalloon(L"LegionGoControl", ss.str());
        return;
    }

    std::wstringstream err;
    err << L"Impostazione TDP fallita.\n\n"
        << L"Richiesto: " << stapm << L"/" << fast << L"/" << slow << L"W\n"
        << L"ExitCode: " << code << L"\n\n"
        << L"Controlla:\n"
        << g_baseDir << L"\\LegionGoNativeWmiProbe.log";

    ShowMessage(L"TDP", err.str(), MB_OK | MB_ICONERROR);
}

static void ToggleBatteryLimit() {
    DWORD code = 0xFFFFFFFF;
    bool ok = RunBackendBatteryToggle(code);

    BatteryStatus st;
    ReadBatteryStateFile(st);

    if (ok) {
        if (st.ok) {
            ShowBalloon(
                L"LegionGoControl",
                st.enabled ? L"Limite batteria 80% attivo." : L"Limite batteria 80% disattivato."
            );
        } else {
            ShowBalloon(L"LegionGoControl", L"Battery charge limit 80% toggled.");
        }
        return;
    }

    std::wstringstream err;
    err << L"Toggle batteria fallito.\n\n"
        << L"ExitCode: " << code << L"\n\n"
        << L"Controlla:\n"
        << g_baseDir << L"\\LegionGoNativeWmiProbe.log";

    ShowMessage(L"Batteria", err.str(), MB_OK | MB_ICONERROR);
}

static void OpenConfigFile() {
    CreateDefaultConfigIfMissing();
    ShellExecuteW(nullptr, L"open", g_iniPath.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

static void OpenLogFile() {
    LogLineAlways(L"Open log.");
    ShellExecuteW(nullptr, L"open", g_logPath.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

static void ReloadConfig() {
    LoadConfig();
    ShowBalloon(L"LegionGoControl", L"Config ricaricata.");
}

static void AddTdpButton(HWND parent, int id, const wchar_t* text, int x, int y, int w, int h) {
    CreateWindowExW(
        0,
        L"BUTTON",
        text,
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        x,
        y,
        w,
        h,
        parent,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
        g_hInst,
        nullptr
    );
}

static LRESULT CALLBACK TdpWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_CREATE: {
            CreateWindowExW(
                0,
                L"STATIC",
                L"Set TDP",
                WS_CHILD | WS_VISIBLE,
                16,
                14,
                220,
                24,
                hwnd,
                nullptr,
                g_hInst,
                nullptr
            );

            AddTdpButton(hwnd, ID_TDP_5,  L"5W",  16,  48, 70, 34);
            AddTdpButton(hwnd, ID_TDP_8,  L"8W",  96,  48, 70, 34);
            AddTdpButton(hwnd, ID_TDP_10, L"10W", 176, 48, 70, 34);

            AddTdpButton(hwnd, ID_TDP_12, L"12W", 16,  92, 70, 34);
            AddTdpButton(hwnd, ID_TDP_16, L"16W", 96,  92, 70, 34);
            AddTdpButton(hwnd, ID_TDP_20, L"20W", 176, 92, 70, 34);

            AddTdpButton(hwnd, ID_TDP_25, L"25W", 16,  136, 70, 34);
            AddTdpButton(hwnd, ID_TDP_30, L"30W", 96,  136, 70, 34);
            AddTdpButton(hwnd, ID_TDP_CUSTOM, L"Custom", 176, 136, 70, 34);

            CreateWindowExW(
                0,
                L"STATIC",
                L"Custom: edit [TDP] in INI, then Reload config.",
                WS_CHILD | WS_VISIBLE,
                16,
                182,
                300,
                24,
                hwnd,
                nullptr,
                g_hInst,
                nullptr
            );

            return 0;
        }

        case WM_COMMAND: {
            UINT id = static_cast<UINT>(LOWORD(wParam));
            UINT presetW = TdpMenuIdToWatts(id);

            if (presetW != 0) {
                ApplyTdpTriple(
                    static_cast<int>(presetW),
                    static_cast<int>(presetW),
                    static_cast<int>(presetW)
                );
                return 0;
            }

            if (id == ID_TDP_CUSTOM) {
                LoadConfig();
                ApplyTdpTriple(g_customStapmW, g_customFastW, g_customSlowW);
                return 0;
            }

            return 0;
        }

        case WM_CLOSE:
            ShowWindow(hwnd, SW_HIDE);
            return 0;

        default:
            return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
}

static void EnsureTdpWindowClass() {
    static bool registered = false;

    if (registered) {
        return;
    }

    WNDCLASSW wc = {};
    wc.lpfnWndProc = TdpWndProc;
    wc.hInstance = g_hInst;
    wc.lpszClassName = TDP_WINDOW_CLASS_NAME;
    wc.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);

    RegisterClassW(&wc);
    registered = true;
}

static void ShowTdpWindow() {
    LoadConfig();
    EnsureTdpWindowClass();

    if (!g_tdpWnd || !IsWindow(g_tdpWnd)) {
        g_tdpWnd = CreateWindowExW(
            WS_EX_TOPMOST,
            TDP_WINDOW_CLASS_NAME,
            L"LegionGoControl - TDP",
            WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU,
            CW_USEDEFAULT,
            CW_USEDEFAULT,
            280,
            255,
            nullptr,
            nullptr,
            g_hInst,
            nullptr
        );
    }

    if (!g_tdpWnd) {
        return;
    }

    ShowWindow(g_tdpWnd, SW_SHOWNORMAL);
    SetForegroundWindow(g_tdpWnd);
}

static void ShowTrayMenu(HWND hwnd) {
    LoadConfig();

    BatteryStatus battery = GetBatteryStatusForMenu();

    HMENU menu = CreatePopupMenu();

    AppendMenuW(menu, MF_STRING | MF_DISABLED | MF_GRAYED, 0, L"LegionGoControl");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);

    AppendMenuW(menu, MF_STRING, ID_TRAY_SHOW_TDP, L"Open TDP setter");

    UINT batteryFlags = MF_STRING;

    if (battery.ok && battery.enabled) {
        batteryFlags |= MF_CHECKED;
    }

    AppendMenuW(menu, batteryFlags, ID_TRAY_BATTERY_LIMIT_80, L"Battery charge limit 80% toggle");

    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);

    AppendMenuW(menu, (g_loggingEnabled ? MF_CHECKED : MF_UNCHECKED) | MF_STRING, ID_TRAY_LOGGING, L"Enable logging");
    AppendMenuW(menu, (IsStartupEnabled() ? MF_CHECKED : MF_UNCHECKED) | MF_STRING, ID_TRAY_STARTUP, L"Start with Windows");

    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);

    AppendMenuW(menu, MF_STRING, ID_TRAY_RELOAD_CONFIG, L"Reload config");
    AppendMenuW(menu, MF_STRING, ID_TRAY_OPEN_CONFIG, L"Open config");
    AppendMenuW(menu, MF_STRING, ID_TRAY_OPEN_LOG, L"Open log");

    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, ID_TRAY_EXIT, L"Exit");

    POINT pt = {};
    GetCursorPos(&pt);

    SetForegroundWindow(hwnd);

    UINT cmd = TrackPopupMenu(
        menu,
        TPM_RETURNCMD | TPM_NONOTIFY | TPM_RIGHTBUTTON,
        pt.x,
        pt.y,
        0,
        hwnd,
        nullptr
    );

    PostMessageW(hwnd, WM_NULL, 0, 0);
    DestroyMenu(menu);

    if (cmd != 0) {
        PostMessageW(hwnd, WM_COMMAND, cmd, 0);
    }
}

static bool IsTrayMouseEvent(UINT msg) {
    switch (msg) {
        case WM_LBUTTONDOWN:
        case WM_LBUTTONUP:
        case WM_LBUTTONDBLCLK:
        case WM_RBUTTONDOWN:
        case WM_RBUTTONUP:
        case WM_RBUTTONDBLCLK:
        case WM_CONTEXTMENU:
        case NIN_SELECT:
        case NIN_KEYSELECT:
            return true;

        default:
            return false;
    }
}

static void HandleCommand(UINT id) {
    switch (id) {
        case ID_TRAY_SHOW_TDP:
            ShowTdpWindow();
            break;

        case ID_TRAY_BATTERY_LIMIT_80:
            ToggleBatteryLimit();
            break;

        case ID_TRAY_LOGGING:
            g_loggingEnabled = !g_loggingEnabled;
            SaveLoggingConfig();
            ShowBalloon(
                L"LegionGoControl",
                g_loggingEnabled ? L"Logging abilitato." : L"Logging disabilitato."
            );
            break;

        case ID_TRAY_STARTUP: {
            bool target = !IsStartupEnabled();

            if (SetStartupEnabled(target)) {
                ShowBalloon(
                    L"LegionGoControl",
                    target ? L"Scheduled task abilitata." : L"Scheduled task rimossa."
                );
            } else {
                ShowMessage(L"Startup", L"Impossibile modificare la scheduled task.", MB_OK | MB_ICONERROR);
            }
            break;
        }

        case ID_TRAY_RELOAD_CONFIG:
            ReloadConfig();
            break;

        case ID_TRAY_OPEN_CONFIG:
            OpenConfigFile();
            break;

        case ID_TRAY_OPEN_LOG:
            OpenLogFile();
            break;

        case ID_TRAY_EXIT:
            DestroyWindow(g_hwnd);
            break;

        default:
            break;
    }
}

static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_CREATE:
            RegisterLegionRawInput(hwnd);

            if (!AddTrayIcon(hwnd)) {
                return -1;
            }

            ShowBalloon(L"LegionGoControl", L"Avviato elevato.");
            return 0;

        case WM_INPUT:
            HandleRawInput(reinterpret_cast<HRAWINPUT>(lParam));
            return 0;

        case WM_COMMAND:
            HandleCommand(static_cast<UINT>(LOWORD(wParam)));
            return 0;

        case WMAPP_TRAYICON: {
            UINT eventA = static_cast<UINT>(lParam);
            UINT eventB = LOWORD(lParam);

            if (IsTrayMouseEvent(eventA) || IsTrayMouseEvent(eventB)) {
                ShowTrayMenu(hwnd);
            }

            return 0;
        }

        case WMAPP_SHOW_TDP:
            ShowTdpWindow();
            return 0;

        case WM_DESTROY:
            if (g_tdpWnd && IsWindow(g_tdpWnd)) {
                DestroyWindow(g_tdpWnd);
                g_tdpWnd = nullptr;
            }

            RemoveTrayIcon();
            PostQuitMessage(0);
            return 0;

        default:
            return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
}

static void SignalExistingInstanceAndExit() {
    HWND existing = FindWindowW(WINDOW_CLASS_NAME, nullptr);

    if (existing) {
        PostMessageW(existing, WMAPP_SHOW_TDP, 0, 0);
    }
}

int APIENTRY wWinMain(HINSTANCE hInstance, HINSTANCE, LPWSTR, int) {
    g_hInst = hInstance;

    InitPaths();
    CreateDefaultConfigIfMissing();
    InitButtonDefinitions();
    LoadConfig();

    if (!IsAdmin()) {
        DWORD code = 1;
        RunHiddenCommand(L"schtasks /Run /TN \"LegionGoControl\"", code);
        return 0;
    }

    g_mutex = CreateMutexW(nullptr, TRUE, MUTEX_NAME);

    if (g_mutex && GetLastError() == ERROR_ALREADY_EXISTS) {
        SignalExistingInstanceAndExit();
        return 0;
    }

    WNDCLASSW wc = {};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = WINDOW_CLASS_NAME;
    wc.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);

    if (!RegisterClassW(&wc)) {
        return 1;
    }

    g_hIcon = CreateTrayIcon();

    g_hwnd = CreateWindowExW(
        0,
        WINDOW_CLASS_NAME,
        APP_TITLE,
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        300,
        200,
        nullptr,
        nullptr,
        hInstance,
        nullptr
    );

    if (!g_hwnd) {
        return 2;
    }

    ShowWindow(g_hwnd, SW_HIDE);
    UpdateWindow(g_hwnd);

    MSG msg = {};

    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    if (g_hIcon) {
        DestroyIcon(g_hIcon);
        g_hIcon = nullptr;
    }

    if (g_mutex) {
        ReleaseMutex(g_mutex);
        CloseHandle(g_mutex);
        g_mutex = nullptr;
    }

    return static_cast<int>(msg.wParam);
}