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
#include <wbemidl.h>
#include <oleauto.h>

#include <cwctype>
#include <string>
#include <sstream>
#include <vector>

#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "oleaut32.lib")
#pragma comment(lib, "wbemuuid.lib")

static constexpr int TDP_ID_STAPM = 0x0101FF00;
static constexpr int TDP_ID_FAST  = 0x0102FF00;
static constexpr int TDP_ID_SLOW  = 0x0103FF00;

static constexpr int BATTERY_LIMIT_80_ID = 0x03010001;

static constexpr int TDP_MIN_W = 5;
static constexpr int TDP_MAX_W = 35;

static std::wstring g_exePath;
static std::wstring g_baseDir;
static std::wstring g_tdpStatePath;
static std::wstring g_batteryStatePath;
static std::wstring g_logPath;

static IWbemLocator* g_locator = nullptr;
static IWbemServices* g_services = nullptr;
static IWbemClassObject* g_lenovoClass = nullptr;

static std::wstring ToUpper(std::wstring s) {
    for (wchar_t& c : s) {
        c = static_cast<wchar_t>(towupper(c));
    }
    return s;
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

static std::wstring HResultToHex(HRESULT hr) {
    wchar_t buf[64] = {};
    swprintf_s(buf, L"0x%08X", static_cast<unsigned int>(hr));
    return buf;
}

static std::wstring GetDirectoryOfPath(const std::wstring& path) {
    size_t p = path.find_last_of(L"\\/");
    if (p == std::wstring::npos) {
        return L".";
    }
    return path.substr(0, p);
}

static void InitPaths() {
    wchar_t exe[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, exe, MAX_PATH);

    g_exePath = exe;
    g_baseDir = GetDirectoryOfPath(g_exePath);
    g_tdpStatePath = g_baseDir + L"\\LegionGoNativeWmiProbe_state.txt";
    g_batteryStatePath = g_baseDir + L"\\LegionGoBatteryLimitState.txt";
    g_logPath = g_baseDir + L"\\LegionGoNativeWmiProbe.log";
}

static void WriteFileUtf16Replace(const std::wstring& path, const std::wstring& text) {
    HANDLE h = CreateFileW(
        path.c_str(),
        GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        nullptr,
        CREATE_ALWAYS,
        FILE_ATTRIBUTE_NORMAL,
        nullptr
    );

    if (h == INVALID_HANDLE_VALUE) {
        return;
    }

    DWORD written = 0;
    WORD bom = 0xFEFF;

    WriteFile(h, &bom, sizeof(bom), &written, nullptr);
    WriteFile(
        h,
        text.data(),
        static_cast<DWORD>(text.size() * sizeof(wchar_t)),
        &written,
        nullptr
    );

    CloseHandle(h);
}

static void AppendFileUtf16(const std::wstring& path, const std::wstring& text) {
    HANDLE h = CreateFileW(
        path.c_str(),
        FILE_APPEND_DATA,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        nullptr,
        OPEN_ALWAYS,
        FILE_ATTRIBUTE_NORMAL,
        nullptr
    );

    if (h == INVALID_HANDLE_VALUE) {
        return;
    }

    DWORD size = GetFileSize(h, nullptr);
    DWORD written = 0;

    if (size == 0) {
        WORD bom = 0xFEFF;
        WriteFile(h, &bom, sizeof(bom), &written, nullptr);
    }

    WriteFile(
        h,
        text.data(),
        static_cast<DWORD>(text.size() * sizeof(wchar_t)),
        &written,
        nullptr
    );

    CloseHandle(h);
}

static void Log(const std::wstring& s) {
    AppendFileUtf16(g_logPath, L"[" + NowString() + L"] " + s + L"\r\n");
}

static void ConsoleWrite(const std::wstring& s) {
    HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);

    if (!h || h == INVALID_HANDLE_VALUE) {
        return;
    }

    int len = WideCharToMultiByte(CP_UTF8, 0, s.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (len <= 1) {
        return;
    }

    std::string bytes(static_cast<size_t>(len - 1), '\0');

    WideCharToMultiByte(
        CP_UTF8,
        0,
        s.c_str(),
        -1,
        &bytes[0],
        len,
        nullptr,
        nullptr
    );

    DWORD written = 0;
    WriteFile(h, bytes.data(), static_cast<DWORD>(bytes.size()), &written, nullptr);
}

static void FinishProcess(UINT code) {
    Log(L"FinishProcess code=" + std::to_wstring(code));
    TerminateProcess(GetCurrentProcess(), code);
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
            0, 0, 0, 0, 0, 0,
            &adminGroup)) {
        CheckTokenMembership(nullptr, adminGroup, &isAdmin);
        FreeSid(adminGroup);
    }

    return isAdmin == TRUE;
}

static bool VariantToInt(const VARIANT& v, int& out) {
    switch (v.vt) {
        case VT_I1:
            out = v.cVal;
            return true;
        case VT_UI1:
            out = v.bVal;
            return true;
        case VT_I2:
            out = v.iVal;
            return true;
        case VT_UI2:
            out = v.uiVal;
            return true;
        case VT_I4:
            out = v.lVal;
            return true;
        case VT_UI4:
            out = static_cast<int>(v.ulVal);
            return true;
        case VT_INT:
            out = v.intVal;
            return true;
        case VT_UINT:
            out = static_cast<int>(v.uintVal);
            return true;
        case VT_BOOL:
            out = (v.boolVal == VARIANT_TRUE) ? 1 : 0;
            return true;
        case VT_BSTR:
            if (v.bstrVal) {
                out = _wtoi(v.bstrVal);
                return true;
            }
            return false;
        default:
            return false;
    }
}

static bool WmiOpenNoCleanup(std::wstring& error) {
    Log(L"WmiOpenNoCleanup start");

    HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);

    if (FAILED(hr) && hr != RPC_E_CHANGED_MODE) {
        error = L"CoInitializeEx failed " + HResultToHex(hr);
        Log(error);
        return false;
    }

    hr = CoInitializeSecurity(
        nullptr,
        -1,
        nullptr,
        nullptr,
        RPC_C_AUTHN_LEVEL_DEFAULT,
        RPC_C_IMP_LEVEL_IMPERSONATE,
        nullptr,
        EOAC_NONE,
        nullptr
    );

    if (hr == RPC_E_TOO_LATE) {
        hr = S_OK;
    }

    if (FAILED(hr)) {
        error = L"CoInitializeSecurity failed " + HResultToHex(hr);
        Log(error);
        return false;
    }

    hr = CoCreateInstance(
        CLSID_WbemLocator,
        nullptr,
        CLSCTX_INPROC_SERVER,
        IID_IWbemLocator,
        reinterpret_cast<void**>(&g_locator)
    );

    if (FAILED(hr) || !g_locator) {
        error = L"CoCreateInstance IWbemLocator failed " + HResultToHex(hr);
        Log(error);
        return false;
    }

    BSTR ns = SysAllocString(L"ROOT\\WMI");

    hr = g_locator->ConnectServer(
        ns,
        nullptr,
        nullptr,
        nullptr,
        0,
        nullptr,
        nullptr,
        &g_services
    );

    SysFreeString(ns);

    if (FAILED(hr) || !g_services) {
        error = L"ConnectServer ROOT\\WMI failed " + HResultToHex(hr);
        Log(error);
        return false;
    }

    hr = CoSetProxyBlanket(
        g_services,
        RPC_C_AUTHN_WINNT,
        RPC_C_AUTHZ_NONE,
        nullptr,
        RPC_C_AUTHN_LEVEL_CALL,
        RPC_C_IMP_LEVEL_IMPERSONATE,
        nullptr,
        EOAC_NONE
    );

    if (FAILED(hr)) {
        error = L"CoSetProxyBlanket failed " + HResultToHex(hr);
        Log(error);
        return false;
    }

    BSTR className = SysAllocString(L"LENOVO_OTHER_METHOD");

    hr = g_services->GetObject(
        className,
        0,
        nullptr,
        &g_lenovoClass,
        nullptr
    );

    SysFreeString(className);

    if (FAILED(hr) || !g_lenovoClass) {
        error = L"GetObject LENOVO_OTHER_METHOD failed " + HResultToHex(hr);
        Log(error);
        return false;
    }

    Log(L"WmiOpenNoCleanup OK");
    return true;
}

static bool ExecLenovoMethodNoCleanup(
    const wchar_t* methodName,
    const std::vector<std::pair<std::wstring, long>>& args,
    IWbemClassObject** outObj,
    std::wstring& error
) {
    if (outObj) {
        *outObj = nullptr;
    }

    if (!g_services || !g_lenovoClass) {
        error = L"WMI not initialized";
        Log(error);
        return false;
    }

    Log(std::wstring(L"ExecLenovoMethodNoCleanup start: ") + methodName);

    HRESULT hr = S_OK;
    IWbemClassObject* inDef = nullptr;
    IWbemClassObject* inInst = nullptr;

    BSTR method = SysAllocString(methodName);

    hr = g_lenovoClass->GetMethod(method, 0, &inDef, nullptr);

    if (FAILED(hr) || !inDef) {
        SysFreeString(method);
        error = std::wstring(L"GetMethod failed ") + methodName + L" " + HResultToHex(hr);
        Log(error);
        return false;
    }

    hr = inDef->SpawnInstance(0, &inInst);

    if (FAILED(hr) || !inInst) {
        SysFreeString(method);
        error = std::wstring(L"SpawnInstance failed ") + methodName + L" " + HResultToHex(hr);
        Log(error);
        return false;
    }

    for (const auto& kv : args) {
        VARIANT v;
        VariantInit(&v);
        v.vt = VT_I4;
        v.lVal = kv.second;

        hr = inInst->Put(kv.first.c_str(), 0, &v, 0);
        VariantClear(&v);

        if (FAILED(hr)) {
            SysFreeString(method);
            error = L"Input Put failed " + kv.first + L" " + HResultToHex(hr);
            Log(error);
            return false;
        }
    }

    IWbemClassObject* localOut = nullptr;

    BSTR objectPath = SysAllocString(L"LENOVO_OTHER_METHOD.InstanceName=\"ACPI\\\\PNP0C14\\\\GMZN_0\"");

    hr = g_services->ExecMethod(
        objectPath,
        method,
        0,
        nullptr,
        inInst,
        &localOut,
        nullptr
    );

    SysFreeString(objectPath);
    SysFreeString(method);

    if (FAILED(hr)) {
        error = std::wstring(L"ExecMethod failed ") + methodName + L" " + HResultToHex(hr);
        Log(error);
        return false;
    }

    if (outObj) {
        *outObj = localOut;
    }

    Log(std::wstring(L"ExecLenovoMethodNoCleanup OK: ") + methodName);
    return true;
}

static bool GetFeatureValueNoCleanup(int id, int& value, std::wstring& error) {
    value = 0;

    IWbemClassObject* outObj = nullptr;

    if (!ExecLenovoMethodNoCleanup(
            L"GetFeatureValue",
            {
                { L"IDs", static_cast<long>(id) }
            },
            &outObj,
            error)) {
        return false;
    }

    if (!outObj) {
        error = L"GetFeatureValue output object null";
        Log(error);
        return false;
    }

    VARIANT v;
    VariantInit(&v);

    HRESULT hr = outObj->Get(L"value", 0, &v, nullptr, nullptr);

    if (FAILED(hr)) {
        VariantClear(&v);
        hr = outObj->Get(L"Value", 0, &v, nullptr, nullptr);
    }

    bool ok = false;

    if (SUCCEEDED(hr)) {
        ok = VariantToInt(v, value);
    }

    VariantClear(&v);

    if (!ok) {
        error = L"Cannot parse GetFeatureValue output, hr=" + HResultToHex(hr);
        Log(error);
        return false;
    }

    Log(L"GetFeatureValue id=" + std::to_wstring(id) + L" value=" + std::to_wstring(value));
    return true;
}

static bool SetFeatureValueNoCleanup(int id, int value, std::wstring& error) {
    IWbemClassObject* outObj = nullptr;

    bool ok = ExecLenovoMethodNoCleanup(
        L"SetFeatureValue",
        {
            { L"IDs", static_cast<long>(id) },
            { L"value", static_cast<long>(value) }
        },
        &outObj,
        error
    );

    if (!ok) {
        return false;
    }

    Log(L"SetFeatureValue id=" + std::to_wstring(id) + L" value=" + std::to_wstring(value));
    return true;
}

static bool ValidateTdp(int stapm, int fast, int slow, std::wstring& error) {
    if (stapm < TDP_MIN_W || stapm > TDP_MAX_W) {
        error = L"STAPM fuori range 5-35W";
        return false;
    }

    if (fast < TDP_MIN_W || fast > TDP_MAX_W) {
        error = L"FAST fuori range 5-35W";
        return false;
    }

    if (slow < TDP_MIN_W || slow > TDP_MAX_W) {
        error = L"SLOW fuori range 5-35W";
        return false;
    }

    if (stapm > fast) {
        error = L"STAPM non può essere superiore a FAST";
        return false;
    }

    if (stapm > slow) {
        error = L"STAPM non può essere superiore a SLOW";
        return false;
    }

    return true;
}

static void WriteTdpState(bool valid, int stapm, int fast, int slow, const std::wstring& error) {
    std::wstringstream ss;

    ss << L"Timestamp=" << NowString() << L"\r\n";
    ss << L"TdpValid=" << (valid ? L"True" : L"False") << L"\r\n";
    ss << L"TdpStapmW=" << stapm << L"\r\n";
    ss << L"TdpFastW=" << fast << L"\r\n";
    ss << L"TdpSlowW=" << slow << L"\r\n";
    ss << L"Error=" << error << L"\r\n";

    WriteFileUtf16Replace(g_tdpStatePath, ss.str());
}

static void WriteBatteryState(bool valid, bool enabled, const std::wstring& error) {
    std::wstringstream ss;

    ss << L"Timestamp=" << NowString() << L"\r\n";
    ss << L"FeatureIdHex=0x03010001\r\n";

    if (valid) {
        ss << L"BatteryLimit80=" << (enabled ? L"True" : L"False") << L"\r\n";
        ss << L"Error=\r\n";
    } else {
        ss << L"BatteryLimit80=Unknown\r\n";
        ss << L"Error=" << error << L"\r\n";
    }

    WriteFileUtf16Replace(g_batteryStatePath, ss.str());
}

static bool EnsureAdminAndWmi(std::wstring& error) {
    if (!IsAdmin()) {
        error = L"Not admin";
        return false;
    }

    return WmiOpenNoCleanup(error);
}

static int DoStatus() {
    Log(L"DoStatus start");

    std::wstring error;

    if (!EnsureAdminAndWmi(error)) {
        WriteTdpState(false, 0, 0, 0, error);
        ConsoleWrite(L"ERROR: " + error + L"\r\n");
        FinishProcess(10);
    }

    int stapm = 0;
    int fast = 0;
    int slow = 0;

    if (!GetFeatureValueNoCleanup(TDP_ID_STAPM, stapm, error)) {
        WriteTdpState(false, 0, 0, 0, error);
        ConsoleWrite(L"ERROR: " + error + L"\r\n");
        FinishProcess(12);
    }

    if (!GetFeatureValueNoCleanup(TDP_ID_FAST, fast, error)) {
        WriteTdpState(false, 0, 0, 0, error);
        ConsoleWrite(L"ERROR: " + error + L"\r\n");
        FinishProcess(13);
    }

    if (!GetFeatureValueNoCleanup(TDP_ID_SLOW, slow, error)) {
        WriteTdpState(false, 0, 0, 0, error);
        ConsoleWrite(L"ERROR: " + error + L"\r\n");
        FinishProcess(14);
    }

    WriteTdpState(true, stapm, fast, slow, L"");

    std::wstringstream out;
    out << L"OK\r\n";
    out << L"STAPM=" << stapm << L"\r\n";
    out << L"FAST=" << fast << L"\r\n";
    out << L"SLOW=" << slow << L"\r\n";

    ConsoleWrite(out.str());

    FinishProcess(0);
    return 0;
}

static int DoSet(int stapm, int fast, int slow) {
    Log(L"DoSet start " + std::to_wstring(stapm) + L"/" + std::to_wstring(fast) + L"/" + std::to_wstring(slow));

    std::wstring error;

    if (!ValidateTdp(stapm, fast, slow, error)) {
        WriteTdpState(false, stapm, fast, slow, error);
        ConsoleWrite(L"ERROR: " + error + L"\r\n");
        FinishProcess(20);
    }

    if (!EnsureAdminAndWmi(error)) {
        WriteTdpState(false, 0, 0, 0, error);
        ConsoleWrite(L"ERROR: " + error + L"\r\n");
        FinishProcess(21);
    }

    if (!SetFeatureValueNoCleanup(TDP_ID_STAPM, stapm, error)) {
        WriteTdpState(false, stapm, fast, slow, error);
        ConsoleWrite(L"ERROR: " + error + L"\r\n");
        FinishProcess(22);
    }

    if (!SetFeatureValueNoCleanup(TDP_ID_FAST, fast, error)) {
        WriteTdpState(false, stapm, fast, slow, error);
        ConsoleWrite(L"ERROR: " + error + L"\r\n");
        FinishProcess(23);
    }

    if (!SetFeatureValueNoCleanup(TDP_ID_SLOW, slow, error)) {
        WriteTdpState(false, stapm, fast, slow, error);
        ConsoleWrite(L"ERROR: " + error + L"\r\n");
        FinishProcess(24);
    }

    WriteTdpState(true, stapm, fast, slow, L"");

    std::wstringstream out;
    out << L"OK\r\n";
    out << L"SET=" << stapm << L"/" << fast << L"/" << slow << L"\r\n";

    ConsoleWrite(out.str());

    FinishProcess(0);
    return 0;
}

static int DoBatteryStatus() {
    Log(L"DoBatteryStatus start");

    std::wstring error;

    if (!EnsureAdminAndWmi(error)) {
        WriteBatteryState(false, false, error);
        ConsoleWrite(L"ERROR: " + error + L"\r\n");
        FinishProcess(30);
    }

    int value = 0;

    if (!GetFeatureValueNoCleanup(BATTERY_LIMIT_80_ID, value, error)) {
        WriteBatteryState(false, false, error);
        ConsoleWrite(L"ERROR: " + error + L"\r\n");
        FinishProcess(31);
    }

    bool enabled = (value != 0);
    WriteBatteryState(true, enabled, L"");

    ConsoleWrite(std::wstring(L"OK\r\nBATTERY80=") + (enabled ? L"1" : L"0") + L"\r\n");

    FinishProcess(0);
    return 0;
}

static int DoBatterySet(bool enabled) {
    Log(std::wstring(L"DoBatterySet start ") + (enabled ? L"1" : L"0"));

    std::wstring error;

    if (!EnsureAdminAndWmi(error)) {
        WriteBatteryState(false, false, error);
        ConsoleWrite(L"ERROR: " + error + L"\r\n");
        FinishProcess(40);
    }

    if (!SetFeatureValueNoCleanup(BATTERY_LIMIT_80_ID, enabled ? 1 : 0, error)) {
        WriteBatteryState(false, enabled, error);
        ConsoleWrite(L"ERROR: " + error + L"\r\n");
        FinishProcess(41);
    }

    WriteBatteryState(true, enabled, L"");

    ConsoleWrite(std::wstring(L"OK\r\nBATTERY80=") + (enabled ? L"1" : L"0") + L"\r\n");

    FinishProcess(0);
    return 0;
}

static int DoBatteryToggle() {
    Log(L"DoBatteryToggle start");

    std::wstring error;

    if (!EnsureAdminAndWmi(error)) {
        WriteBatteryState(false, false, error);
        ConsoleWrite(L"ERROR: " + error + L"\r\n");
        FinishProcess(50);
    }

    int value = 0;

    if (!GetFeatureValueNoCleanup(BATTERY_LIMIT_80_ID, value, error)) {
        WriteBatteryState(false, false, error);
        ConsoleWrite(L"ERROR: " + error + L"\r\n");
        FinishProcess(51);
    }

    bool target = (value == 0);

    if (!SetFeatureValueNoCleanup(BATTERY_LIMIT_80_ID, target ? 1 : 0, error)) {
        WriteBatteryState(false, target, error);
        ConsoleWrite(L"ERROR: " + error + L"\r\n");
        FinishProcess(52);
    }

    WriteBatteryState(true, target, L"");

    ConsoleWrite(std::wstring(L"OK\r\nBATTERY80=") + (target ? L"1" : L"0") + L"\r\n");

    FinishProcess(0);
    return 0;
}

static int ArgToInt(wchar_t** argv, int argc, int index, int fallback) {
    if (index >= argc || !argv[index]) {
        return fallback;
    }

    return _wtoi(argv[index]);
}

int wmain(int argc, wchar_t** argv) {
    InitPaths();

    DeleteFileW(g_logPath.c_str());

    Log(L"Probe start");

    if (argc < 2) {
        ConsoleWrite(
            L"Uso:\r\n"
            L"  LegionGoNativeWmiProbe.exe status\r\n"
            L"  LegionGoNativeWmiProbe.exe set 12 12 12\r\n"
            L"  LegionGoNativeWmiProbe.exe battery-status\r\n"
            L"  LegionGoNativeWmiProbe.exe battery-set 0|1\r\n"
            L"  LegionGoNativeWmiProbe.exe battery-toggle\r\n"
        );
        FinishProcess(2);
    }

    std::wstring cmd = ToUpper(argv[1]);

    if (cmd == L"STATUS") {
        return DoStatus();
    }

    if (cmd == L"SET") {
        int stapm = ArgToInt(argv, argc, 2, 0);
        int fast = ArgToInt(argv, argc, 3, stapm);
        int slow = ArgToInt(argv, argc, 4, stapm);

        return DoSet(stapm, fast, slow);
    }

    if (cmd == L"BATTERY-STATUS") {
        return DoBatteryStatus();
    }

    if (cmd == L"BATTERY-SET") {
        int enabled = ArgToInt(argv, argc, 2, 0);
        return DoBatterySet(enabled != 0);
    }

    if (cmd == L"BATTERY-TOGGLE") {
        return DoBatteryToggle();
    }

    ConsoleWrite(L"Comando sconosciuto.\r\n");
    FinishProcess(3);
    return 3;
}