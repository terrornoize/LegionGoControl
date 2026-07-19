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
#include <sddl.h>

#include <array>
#include <cerrno>
#include <climits>
#include <cstring>
#include <cwctype>
#include <limits>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "oleaut32.lib")
#pragma comment(lib, "wbemuuid.lib")

static constexpr int TDP_ID_STAPM = 0x0101FF00;
static constexpr int TDP_ID_FAST  = 0x0102FF00;
static constexpr int TDP_ID_SLOW  = 0x0103FF00;
static constexpr int BATTERY_LIMIT_80_ID = 0x03010001;
static constexpr int FAN_RPM_ID = 0x04030001;
static constexpr int FAN_FULL_SPEED_ID = 0x04020000;
static constexpr int CPU_TEMPERATURE_ID = 0x05040000;
static constexpr int TDP_MIN_W = 5;
static constexpr int TDP_MAX_W = 35;

static constexpr const wchar_t* TDP_MUTEX_NAME =
    L"Global\\LegionGoNativeWmiProbe_TdpTransaction_v1";
static constexpr const wchar_t* BATTERY_MUTEX_NAME =
    L"Global\\LegionGoNativeWmiProbe_BatteryTransaction_v1";
static constexpr const wchar_t* FAN_MUTEX_NAME =
    L"Global\\LegionGoNativeWmiProbe_FanTransaction_v1";
static constexpr std::array<int, 10> FAN_TEMPERATURES{10,20,30,40,50,60,70,80,90,100};

static std::wstring g_baseDir;
static std::wstring g_tdpStatePath;
static std::wstring g_batteryStatePath;
static std::wstring g_fanStatePath;
static std::wstring g_fanBackupPath;
static std::wstring g_logPath;

static std::wstring ToUpper(std::wstring s) {
    for (wchar_t& c : s) {
        c = static_cast<wchar_t>(towupper(c));
    }
    return s;
}

static std::wstring NowString() {
    SYSTEMTIME st = {};
    GetLocalTime(&st);
    wchar_t buffer[64] = {};
    swprintf_s(
        buffer,
        L"%04u-%02u-%02u %02u:%02u:%02u.%03u",
        static_cast<unsigned int>(st.wYear),
        static_cast<unsigned int>(st.wMonth),
        static_cast<unsigned int>(st.wDay),
        static_cast<unsigned int>(st.wHour),
        static_cast<unsigned int>(st.wMinute),
        static_cast<unsigned int>(st.wSecond),
        static_cast<unsigned int>(st.wMilliseconds));
    return buffer;
}

static std::wstring HResultToHex(HRESULT hr) {
    wchar_t buffer[64] = {};
    swprintf_s(buffer, L"0x%08X", static_cast<unsigned int>(hr));
    return buffer;
}

static std::wstring Win32ErrorToString(DWORD error) {
    wchar_t buffer[64] = {};
    swprintf_s(buffer, L"%lu", static_cast<unsigned long>(error));
    return buffer;
}

static std::wstring GetDirectoryOfPath(const std::wstring& path) {
    const size_t pos = path.find_last_of(L"\\/");
    return pos == std::wstring::npos ? L"." : path.substr(0, pos);
}

static void InitPaths() {
    std::vector<wchar_t> path(32768, L'\0');
    const DWORD length = GetModuleFileNameW(
        nullptr, path.data(), static_cast<DWORD>(path.size()));
    const std::wstring exePath =
        (length != 0 && length < path.size()) ? std::wstring(path.data(), length) : L".";
    g_baseDir = GetDirectoryOfPath(exePath);
    g_tdpStatePath = g_baseDir + L"\\LegionGoNativeWmiProbe_state.txt";
    g_batteryStatePath = g_baseDir + L"\\LegionGoBatteryLimitState.txt";
    g_fanStatePath = g_baseDir + L"\\LegionGoFanState.txt";
    g_fanBackupPath = g_baseDir + L"\\LegionGoFanBackup.txt";
    g_logPath = g_baseDir + L"\\LegionGoNativeWmiProbe.log";
}

static bool WriteAll(HANDLE file, const void* data, size_t byteCount) {
    const BYTE* current = static_cast<const BYTE*>(data);
    while (byteCount != 0) {
        const DWORD chunk = static_cast<DWORD>(
            (byteCount > static_cast<size_t>(MAXDWORD)) ? MAXDWORD : byteCount);
        DWORD written = 0;
        if (!WriteFile(file, current, chunk, &written, nullptr) || written == 0) {
            return false;
        }
        current += written;
        byteCount -= written;
    }
    return true;
}

static bool WriteFileUtf16Replace(const std::wstring& path, const std::wstring& text) {
    // Publish state atomically: readers see either the previous complete file or the new one.
    const std::wstring temporaryPath =
        path + L"." + std::to_wstring(GetCurrentProcessId()) + L".tmp";
    HANDLE file = CreateFileW(
        temporaryPath.c_str(),
        GENERIC_WRITE,
        0,
        nullptr,
        CREATE_ALWAYS,
        FILE_ATTRIBUTE_NORMAL,
        nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return false;
    }

    const WORD bom = 0xFEFF;
    const bool written = WriteAll(file, &bom, sizeof(bom)) &&
        WriteAll(file, text.data(), text.size() * sizeof(wchar_t)) &&
        FlushFileBuffers(file) != FALSE;
    CloseHandle(file);
    if (!written) {
        return false;
    }

    return MoveFileExW(
        temporaryPath.c_str(),
        path.c_str(),
        MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != FALSE;
}

static bool ReadFileUtf16(const std::wstring& path, std::wstring& text) {
    text.clear();
    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                              nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER size{};
    if (!GetFileSizeEx(file, &size) || size.QuadPart < 2 || size.QuadPart > 1024 * 1024) {
        CloseHandle(file); return false;
    }
    std::vector<BYTE> bytes(static_cast<size_t>(size.QuadPart));
    DWORD read = 0;
    const bool ok = ReadFile(file, bytes.data(), static_cast<DWORD>(bytes.size()), &read, nullptr) != FALSE;
    CloseHandle(file);
    if (!ok || read < 2) return false;
    const size_t offset = bytes[0] == 0xFF && bytes[1] == 0xFE ? 2 : 0;
    text.assign(reinterpret_cast<const wchar_t*>(bytes.data() + offset), (read - offset) / sizeof(wchar_t));
    return true;
}

static std::wstring StateField(const std::wstring& text, const std::wstring& key) {
    std::wstringstream stream(text); std::wstring line; const std::wstring prefix = key + L"=";
    while (std::getline(stream, line)) {
        if (!line.empty() && line.back() == L'\r') line.pop_back();
        if (line.compare(0, prefix.size(), prefix) == 0) return line.substr(prefix.size());
    }
    return L"";
}

static void AppendFileUtf16(const std::wstring& path, const std::wstring& text) {
    HANDLE file = CreateFileW(
        path.c_str(),
        FILE_APPEND_DATA | SYNCHRONIZE,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr,
        OPEN_ALWAYS,
        FILE_ATTRIBUTE_NORMAL,
        nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return;
    }

    OVERLAPPED overlapped = {};
    const bool locked = LockFileEx(
        file, LOCKFILE_EXCLUSIVE_LOCK, 0, MAXDWORD, MAXDWORD, &overlapped) != FALSE;

    LARGE_INTEGER size = {};
    if (GetFileSizeEx(file, &size) && size.QuadPart == 0) {
        const WORD bom = 0xFEFF;
        (void)WriteAll(file, &bom, sizeof(bom));
    }
    (void)WriteAll(file, text.data(), text.size() * sizeof(wchar_t));

    if (locked) {
        (void)UnlockFileEx(file, 0, MAXDWORD, MAXDWORD, &overlapped);
    }
    CloseHandle(file);
}

static void Log(const std::wstring& message) {
    AppendFileUtf16(g_logPath, L"[" + NowString() + L"] " + message + L"\r\n");
}

static void ConsoleWrite(const std::wstring& text) {
    HANDLE output = GetStdHandle(STD_OUTPUT_HANDLE);
    if (output == nullptr || output == INVALID_HANDLE_VALUE || text.empty()) {
        return;
    }

    if (text.size() > static_cast<size_t>(INT_MAX)) {
        return;
    }
    const int sourceLength = static_cast<int>(text.size());
    const int byteCount = WideCharToMultiByte(
        CP_UTF8, 0, text.data(), sourceLength, nullptr, 0, nullptr, nullptr);
    if (byteCount <= 0) {
        return;
    }

    std::string bytes(static_cast<size_t>(byteCount), '\0');
    const int converted = WideCharToMultiByte(
        CP_UTF8, 0, text.data(), sourceLength, bytes.data(), byteCount, nullptr, nullptr);
    if (converted != byteCount) {
        return;
    }
    (void)WriteAll(output, bytes.data(), bytes.size());
}

static int ReturnCode(int code) {
    Log(L"Exit code=" + std::to_wstring(code));
    return code;
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
        (void)CheckTokenMembership(nullptr, adminGroup, &isAdmin);
        FreeSid(adminGroup);
    }
    return isAdmin == TRUE;
}

template <typename T>
class ComPtr final {
public:
    ComPtr() noexcept = default;
    ~ComPtr() { Reset(); }
    ComPtr(const ComPtr&) = delete;
    ComPtr& operator=(const ComPtr&) = delete;

    ComPtr(ComPtr&& other) noexcept : pointer_(other.Detach()) {}
    ComPtr& operator=(ComPtr&& other) noexcept {
        if (this != &other) {
            Reset(other.Detach());
        }
        return *this;
    }

    T* Get() const noexcept { return pointer_; }
    T* operator->() const noexcept { return pointer_; }
    explicit operator bool() const noexcept { return pointer_ != nullptr; }
    T** Put() noexcept {
        Reset();
        return &pointer_;
    }
    T* Detach() noexcept {
        T* result = pointer_;
        pointer_ = nullptr;
        return result;
    }
    void Reset(T* value = nullptr) noexcept {
        if (pointer_ != nullptr) {
            pointer_->Release();
        }
        pointer_ = value;
    }

private:
    T* pointer_ = nullptr;
};

class UniqueBstr final {
public:
    explicit UniqueBstr(const wchar_t* text) : value_(SysAllocString(text)) {}
    ~UniqueBstr() { SysFreeString(value_); }
    UniqueBstr(const UniqueBstr&) = delete;
    UniqueBstr& operator=(const UniqueBstr&) = delete;
    BSTR Get() const noexcept { return value_; }
    explicit operator bool() const noexcept { return value_ != nullptr; }

private:
    BSTR value_ = nullptr;
};

class ScopedVariant final {
public:
    ScopedVariant() { VariantInit(&value_); }
    ~ScopedVariant() { VariantClear(&value_); }
    ScopedVariant(const ScopedVariant&) = delete;
    ScopedVariant& operator=(const ScopedVariant&) = delete;
    VARIANT* Get() noexcept { return &value_; }
    const VARIANT& Value() const noexcept { return value_; }

private:
    VARIANT value_;
};

class ComRuntime final {
public:
    ~ComRuntime() {
        if (mustUninitialize_) {
            CoUninitialize();
        }
    }
    ComRuntime(const ComRuntime&) = delete;
    ComRuntime& operator=(const ComRuntime&) = delete;
    ComRuntime() = default;

    bool Initialize(std::wstring& error) {
        HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        if (SUCCEEDED(hr)) {
            mustUninitialize_ = true;
        } else if (hr != RPC_E_CHANGED_MODE) {
            error = L"CoInitializeEx failed " + HResultToHex(hr);
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
            nullptr);
        if (hr == RPC_E_TOO_LATE) {
            hr = S_OK;
        }
        if (FAILED(hr)) {
            error = L"CoInitializeSecurity failed " + HResultToHex(hr);
            return false;
        }
        return true;
    }

private:
    bool mustUninitialize_ = false;
};

static bool ParseIntStrict(const wchar_t* text, int& value) {
    if (text == nullptr || *text == L'\0') {
        return false;
    }
    const wchar_t* p = text;
    if (*p == L'+' || *p == L'-') {
        ++p;
    }
    if (*p == L'\0') {
        return false;
    }
    for (; *p != L'\0'; ++p) {
        if (*p < L'0' || *p > L'9') {
            return false;
        }
    }

    errno = 0;
    wchar_t* end = nullptr;
    const long long parsed = wcstoll(text, &end, 10);
    if (errno == ERANGE || end == text || *end != L'\0' ||
        parsed < std::numeric_limits<int>::min() ||
        parsed > std::numeric_limits<int>::max()) {
        return false;
    }
    value = static_cast<int>(parsed);
    return true;
}

static bool VariantToInt(const VARIANT& variant, int& value) {
    switch (variant.vt) {
        case VT_I1:
            value = variant.cVal;
            return true;
        case VT_UI1:
            value = variant.bVal;
            return true;
        case VT_I2:
            value = variant.iVal;
            return true;
        case VT_UI2:
            value = variant.uiVal;
            return true;
        case VT_I4:
            value = variant.lVal;
            return true;
        case VT_UI4:
            if (variant.ulVal > static_cast<ULONG>(INT_MAX)) {
                return false;
            }
            value = static_cast<int>(variant.ulVal);
            return true;
        case VT_INT:
            value = variant.intVal;
            return true;
        case VT_UINT:
            if (variant.uintVal > static_cast<UINT>(INT_MAX)) {
                return false;
            }
            value = static_cast<int>(variant.uintVal);
            return true;
        case VT_I8:
            if (variant.llVal < INT_MIN || variant.llVal > INT_MAX) {
                return false;
            }
            value = static_cast<int>(variant.llVal);
            return true;
        case VT_UI8:
            if (variant.ullVal > static_cast<ULONGLONG>(INT_MAX)) {
                return false;
            }
            value = static_cast<int>(variant.ullVal);
            return true;
        case VT_BOOL:
            if (variant.boolVal == VARIANT_FALSE) {
                value = 0;
                return true;
            }
            if (variant.boolVal == VARIANT_TRUE) {
                value = 1;
                return true;
            }
            return false;
        case VT_BSTR:
            return variant.bstrVal != nullptr && ParseIntStrict(variant.bstrVal, value);
        default:
            return false;
    }
}

class WmiSession final {
public:
    WmiSession() = default;
    WmiSession(const WmiSession&) = delete;
    WmiSession& operator=(const WmiSession&) = delete;

    bool Open(std::wstring& error) {
        if (!runtime_.Initialize(error)) {
            Log(error);
            return false;
        }

        HRESULT hr = CoCreateInstance(
            CLSID_WbemLocator,
            nullptr,
            CLSCTX_INPROC_SERVER,
            IID_IWbemLocator,
            reinterpret_cast<void**>(locator_.Put()));
        if (FAILED(hr) || !locator_) {
            error = L"CoCreateInstance IWbemLocator failed " + HResultToHex(hr);
            Log(error);
            return false;
        }

        UniqueBstr nameSpace(L"ROOT\\WMI");
        if (!nameSpace) {
            error = L"SysAllocString ROOT\\WMI failed";
            Log(error);
            return false;
        }
        hr = locator_->ConnectServer(
            nameSpace.Get(), nullptr, nullptr, nullptr, 0, nullptr, nullptr, services_.Put());
        if (FAILED(hr) || !services_) {
            error = L"ConnectServer ROOT\\WMI failed " + HResultToHex(hr);
            Log(error);
            return false;
        }

        hr = CoSetProxyBlanket(
            services_.Get(),
            RPC_C_AUTHN_WINNT,
            RPC_C_AUTHZ_NONE,
            nullptr,
            RPC_C_AUTHN_LEVEL_CALL,
            RPC_C_IMP_LEVEL_IMPERSONATE,
            nullptr,
            EOAC_NONE);
        if (FAILED(hr)) {
            error = L"CoSetProxyBlanket failed " + HResultToHex(hr);
            Log(error);
            return false;
        }

        UniqueBstr className(L"LENOVO_OTHER_METHOD");
        if (!className) {
            error = L"SysAllocString LENOVO_OTHER_METHOD failed";
            Log(error);
            return false;
        }
        hr = services_->GetObject(className.Get(), 0, nullptr, lenovoClass_.Put(), nullptr);
        if (FAILED(hr) || !lenovoClass_) {
            error = L"GetObject LENOVO_OTHER_METHOD failed " + HResultToHex(hr);
            Log(error);
            return false;
        }

        Log(L"WMI open OK");
        return true;
    }

    bool GetFeatureValue(int id, int& value, std::wstring& error) {
        ComPtr<IWbemClassObject> output;
        if (!ExecLenovoMethod(
                L"GetFeatureValue", {{L"IDs", static_cast<long>(id)}}, output, error)) {
            return false;
        }
        if (!output) {
            error = L"GetFeatureValue returned no output object";
            Log(error);
            return false;
        }
        if (!CheckReturnValue(output.Get(), L"GetFeatureValue", error)) {
            return false;
        }

        ScopedVariant result;
        HRESULT hr = output->Get(L"value", 0, result.Get(), nullptr, nullptr);
        if (FAILED(hr)) {
            VariantClear(result.Get());
            VariantInit(result.Get());
            hr = output->Get(L"Value", 0, result.Get(), nullptr, nullptr);
        }
        if (FAILED(hr) || !VariantToInt(result.Value(), value)) {
            error = L"Cannot parse GetFeatureValue output, hr=" + HResultToHex(hr);
            Log(error);
            return false;
        }

        Log(L"GetFeatureValue id=" + std::to_wstring(id) +
            L" value=" + std::to_wstring(value));
        return true;
    }

    bool SetFeatureValue(int id, int value, std::wstring& error) {
        ComPtr<IWbemClassObject> output;
        if (!ExecLenovoMethod(
                L"SetFeatureValue",
                {{L"IDs", static_cast<long>(id)}, {L"value", static_cast<long>(value)}},
                output,
                error)) {
            return false;
        }
        if (!CheckReturnValue(output.Get(), L"SetFeatureValue", error)) {
            return false;
        }
        Log(L"SetFeatureValue id=" + std::to_wstring(id) +
            L" value=" + std::to_wstring(value));
        return true;
    }

    bool GetFanCurve(std::array<int, 10>& temperatures,
                     std::array<int, 10>& speeds,
                     std::wstring& error) {
        ComPtr<IWbemClassObject> fanClass;
        std::wstring objectPath;
        if (!GetMethodTarget(L"LENOVO_FAN_METHOD", fanClass, objectPath, error)) return false;
        UniqueBstr methodName(L"Fan_Get_Table");
        ComPtr<IWbemClassObject> definition;
        HRESULT hr = fanClass->GetMethod(methodName.Get(), 0, definition.Put(), nullptr);
        if (FAILED(hr) || !definition) { error = L"Fan_Get_Table GetMethod failed " + HResultToHex(hr); return false; }
        ComPtr<IWbemClassObject> input;
        hr = definition->SpawnInstance(0, input.Put());
        if (FAILED(hr) || !input) { error = L"Fan_Get_Table SpawnInstance failed " + HResultToHex(hr); return false; }
        ScopedVariant fanId; fanId.Get()->vt = VT_UI1; fanId.Get()->bVal = 1;
        hr = input->Put(L"FanID", 0, fanId.Get(), CIM_UINT8);
        if (FAILED(hr)) { error = L"FanID Put failed " + HResultToHex(hr); return false; }
        ScopedVariant sensorId; sensorId.Get()->vt = VT_UI1; sensorId.Get()->bVal = 0;
        hr = input->Put(L"SensorID", 0, sensorId.Get(), CIM_UINT8);
        if (FAILED(hr)) { error = L"SensorID Put failed " + HResultToHex(hr); return false; }
        ComPtr<IWbemClassObject> output;
        UniqueBstr path(objectPath.c_str());
        hr = services_->ExecMethod(path.Get(), methodName.Get(), 0, nullptr, input.Get(), output.Put(), nullptr);
        if (FAILED(hr) || !output) { error = L"Fan_Get_Table ExecMethod failed " + HResultToHex(hr); return false; }
        ScopedVariant fanValues, sensorValues;
        hr = output->Get(L"FanTable", 0, fanValues.Get(), nullptr, nullptr);
        if (FAILED(hr) || !VariantArrayToTenInts(fanValues.Value(), speeds)) {
            error = L"Invalid FanTable output " + HResultToHex(hr); return false;
        }
        hr = output->Get(L"SensorTable", 0, sensorValues.Get(), nullptr, nullptr);
        if (FAILED(hr) || !VariantArrayToTenInts(sensorValues.Value(), temperatures)) {
            error = L"Invalid SensorTable output " + HResultToHex(hr); return false;
        }
        return true;
    }

    bool SetFanCurve(const std::array<int, 10>& speeds, std::wstring& error) {
        ComPtr<IWbemClassObject> fanClass;
        std::wstring objectPath;
        if (!GetMethodTarget(L"LENOVO_FAN_METHOD", fanClass, objectPath, error)) return false;
        UniqueBstr methodName(L"Fan_Set_Table");
        ComPtr<IWbemClassObject> definition;
        HRESULT hr = fanClass->GetMethod(methodName.Get(), 0, definition.Put(), nullptr);
        if (FAILED(hr) || !definition) { error = L"Fan_Set_Table GetMethod failed " + HResultToHex(hr); return false; }
        ComPtr<IWbemClassObject> input;
        hr = definition->SpawnInstance(0, input.Put());
        if (FAILED(hr) || !input) { error = L"Fan_Set_Table SpawnInstance failed " + HResultToHex(hr); return false; }
        std::array<BYTE, 64> bytes{};
        bytes[0] = 1; bytes[1] = 0;
        for (size_t index = 0; index < speeds.size(); ++index) {
            const unsigned value = static_cast<unsigned>(speeds[index]);
            bytes[6 + index * 2] = static_cast<BYTE>(value & 0xFF);
            bytes[7 + index * 2] = static_cast<BYTE>((value >> 8) & 0xFF);
        }
        ScopedVariant table;
        table.Get()->vt = VT_ARRAY | VT_UI1;
        table.Get()->parray = SafeArrayCreateVector(VT_UI1, 0, static_cast<ULONG>(bytes.size()));
        if (!table.Get()->parray) { error = L"Cannot allocate FanTable SAFEARRAY"; return false; }
        void* raw = nullptr;
        hr = SafeArrayAccessData(table.Get()->parray, &raw);
        if (FAILED(hr) || !raw) { error = L"Cannot access FanTable SAFEARRAY " + HResultToHex(hr); return false; }
        memcpy(raw, bytes.data(), bytes.size());
        SafeArrayUnaccessData(table.Get()->parray);
        hr = input->Put(L"FanTable", 0, table.Get(), static_cast<CIMTYPE>(CIM_FLAG_ARRAY | CIM_UINT8));
        if (FAILED(hr)) { error = L"FanTable Put failed " + HResultToHex(hr); return false; }
        ComPtr<IWbemClassObject> output;
        UniqueBstr path(objectPath.c_str());
        hr = services_->ExecMethod(path.Get(), methodName.Get(), 0, nullptr, input.Get(), output.Put(), nullptr);
        if (FAILED(hr)) { error = L"Fan_Set_Table ExecMethod failed " + HResultToHex(hr); return false; }
        return CheckReturnValue(output.Get(), L"Fan_Set_Table", error);
    }

private:
    bool GetMethodTarget(const wchar_t* className,
                         ComPtr<IWbemClassObject>& classObject,
                         std::wstring& objectPath,
                         std::wstring& error) {
        UniqueBstr language(L"WQL");
        const std::wstring queryText = std::wstring(L"SELECT * FROM ") + className + L" WHERE Active=True";
        UniqueBstr query(queryText.c_str());
        ComPtr<IEnumWbemClassObject> enumerator;
        HRESULT hr = services_->ExecQuery(language.Get(), query.Get(),
            WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY, nullptr, enumerator.Put());
        if (FAILED(hr) || !enumerator) { error = L"Fan class query failed " + HResultToHex(hr); return false; }
        ComPtr<IWbemClassObject> instance;
        ULONG returned = 0;
        hr = enumerator->Next(5000, 1, instance.Put(), &returned);
        if (FAILED(hr) || returned != 1 || !instance) { error = L"Active fan WMI instance not found"; return false; }
        ScopedVariant path;
        hr = instance->Get(L"__PATH", 0, path.Get(), nullptr, nullptr);
        if (FAILED(hr) || path.Value().vt != VT_BSTR || !path.Value().bstrVal) {
            error = L"Fan WMI instance path unavailable"; return false;
        }
        objectPath = path.Value().bstrVal;
        UniqueBstr name(className);
        hr = services_->GetObject(name.Get(), 0, nullptr, classObject.Put(), nullptr);
        if (FAILED(hr) || !classObject) { error = L"Fan class GetObject failed " + HResultToHex(hr); return false; }
        return true;
    }

    static bool VariantArrayToTenInts(const VARIANT& value, std::array<int, 10>& result) {
        if (!(value.vt & VT_ARRAY) || !value.parray || SafeArrayGetDim(value.parray) != 1) return false;
        LONG lower = 0, upper = -1;
        if (FAILED(SafeArrayGetLBound(value.parray, 1, &lower)) ||
            FAILED(SafeArrayGetUBound(value.parray, 1, &upper)) || upper - lower + 1 != 10) return false;
        const VARTYPE type = static_cast<VARTYPE>(value.vt & VT_TYPEMASK);
        for (LONG index = lower; index <= upper; ++index) {
            int parsed = 0;
            HRESULT hr = E_FAIL;
            if (type == VT_UI4) { ULONG item = 0; hr = SafeArrayGetElement(value.parray, &index, &item); if (item <= INT_MAX) parsed = static_cast<int>(item); else return false; }
            else if (type == VT_I4) { LONG item = 0; hr = SafeArrayGetElement(value.parray, &index, &item); parsed = static_cast<int>(item); }
            else if (type == VT_UI2) { USHORT item = 0; hr = SafeArrayGetElement(value.parray, &index, &item); parsed = static_cast<int>(item); }
            else return false;
            if (FAILED(hr)) return false;
            result[static_cast<size_t>(index - lower)] = parsed;
        }
        return true;
    }
    bool ExecLenovoMethod(
        const wchar_t* methodName,
        const std::vector<std::pair<std::wstring, long>>& arguments,
        ComPtr<IWbemClassObject>& output,
        std::wstring& error) {
        if (!services_ || !lenovoClass_) {
            error = L"WMI not initialized";
            return false;
        }

        UniqueBstr method(methodName);
        if (!method) {
            error = std::wstring(L"SysAllocString failed for ") + methodName;
            return false;
        }

        ComPtr<IWbemClassObject> inputDefinition;
        HRESULT hr = lenovoClass_->GetMethod(method.Get(), 0, inputDefinition.Put(), nullptr);
        if (FAILED(hr) || !inputDefinition) {
            error = std::wstring(L"GetMethod failed ") + methodName + L" " + HResultToHex(hr);
            Log(error);
            return false;
        }

        ComPtr<IWbemClassObject> input;
        hr = inputDefinition->SpawnInstance(0, input.Put());
        if (FAILED(hr) || !input) {
            error = std::wstring(L"SpawnInstance failed ") + methodName + L" " + HResultToHex(hr);
            Log(error);
            return false;
        }

        for (const auto& argument : arguments) {
            ScopedVariant variant;
            variant.Get()->vt = VT_I4;
            variant.Get()->lVal = argument.second;
            hr = input->Put(argument.first.c_str(), 0, variant.Get(), 0);
            if (FAILED(hr)) {
                error = L"Input Put failed " + argument.first + L" " + HResultToHex(hr);
                Log(error);
                return false;
            }
        }

        UniqueBstr objectPath(
            L"LENOVO_OTHER_METHOD.InstanceName=\"ACPI\\\\PNP0C14\\\\GMZN_0\"");
        if (!objectPath) {
            error = L"SysAllocString Lenovo object path failed";
            return false;
        }

        ComPtr<IWbemClassObject> localOutput;
        hr = services_->ExecMethod(
            objectPath.Get(), method.Get(), 0, nullptr, input.Get(), localOutput.Put(), nullptr);
        if (FAILED(hr)) {
            error = std::wstring(L"ExecMethod failed ") + methodName + L" " + HResultToHex(hr);
            Log(error);
            return false;
        }
        output = std::move(localOutput);
        return true;
    }

    static bool CheckReturnValue(
        IWbemClassObject* output,
        const wchar_t* methodName,
        std::wstring& error) {
        if (output == nullptr) {
            return true;
        }
        ScopedVariant returnValue;
        const HRESULT hr = output->Get(L"ReturnValue", 0, returnValue.Get(), nullptr, nullptr);
        if (hr == WBEM_E_NOT_FOUND) {
            return true;
        }
        if (FAILED(hr)) {
            error = std::wstring(L"Cannot read optional ReturnValue for ") + methodName +
                L" " + HResultToHex(hr);
            Log(error);
            return false;
        }

        int value = 0;
        if (!VariantToInt(returnValue.Value(), value)) {
            error = std::wstring(L"Invalid ReturnValue for ") + methodName;
            Log(error);
            return false;
        }
        if (value != 0) {
            error = std::wstring(L"Provider ReturnValue for ") + methodName +
                L" was " + std::to_wstring(value);
            Log(error);
            return false;
        }
        return true;
    }

    ComRuntime runtime_;
    ComPtr<IWbemLocator> locator_;
    ComPtr<IWbemServices> services_;
    ComPtr<IWbemClassObject> lenovoClass_;
};

class NamedMutexLock final {
public:
    NamedMutexLock() = default;
    ~NamedMutexLock() {
        if (acquired_) {
            (void)ReleaseMutex(handle_);
        }
        if (handle_ != nullptr) {
            CloseHandle(handle_);
        }
    }
    NamedMutexLock(const NamedMutexLock&) = delete;
    NamedMutexLock& operator=(const NamedMutexLock&) = delete;

    bool Acquire(const wchar_t* name, std::wstring& error) {
        PSECURITY_DESCRIPTOR descriptor = nullptr;
        SECURITY_ATTRIBUTES attributes = {};
        attributes.nLength = sizeof(attributes);
        attributes.bInheritHandle = FALSE;

        // Every elevated local probe is in BUILTIN\\Administrators; deny unprivileged squatting/DoS.
        const wchar_t* sddl = L"D:P(A;;GA;;;SY)(A;;GA;;;BA)";
        if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(
                sddl, SDDL_REVISION_1, &descriptor, nullptr)) {
            error = L"Cannot build transaction mutex security, Win32=" +
                Win32ErrorToString(GetLastError());
            Log(error);
            return false;
        }
        attributes.lpSecurityDescriptor = descriptor;

        handle_ = CreateMutexW(&attributes, FALSE, name);
        DWORD createError = GetLastError();
        if (descriptor != nullptr) {
            LocalFree(descriptor);
        }

        if (handle_ == nullptr && createError == ERROR_ACCESS_DENIED) {
            handle_ = OpenMutexW(SYNCHRONIZE | MUTEX_MODIFY_STATE, FALSE, name);
            createError = GetLastError();
        }
        if (handle_ == nullptr) {
            error = L"Cannot create/open transaction mutex, Win32=" +
                Win32ErrorToString(createError);
            Log(error);
            return false;
        }

        const DWORD waitResult = WaitForSingleObject(handle_, 60000);
        if (waitResult == WAIT_TIMEOUT) {
            error = L"Timed out waiting for transaction mutex";
            Log(error);
            return false;
        }
        if (waitResult != WAIT_OBJECT_0 && waitResult != WAIT_ABANDONED) {
            error = L"Cannot acquire transaction mutex, Win32=" +
                Win32ErrorToString(GetLastError());
            Log(error);
            return false;
        }
        acquired_ = true;
        if (waitResult == WAIT_ABANDONED) {
            Log(L"Acquired abandoned transaction mutex");
        }
        return true;
    }

private:
    HANDLE handle_ = nullptr;
    bool acquired_ = false;
};

struct TdpValues {
    int stapm = 0;
    int fast = 0;
    int slow = 0;
};

static bool ValidateTdp(const TdpValues& values, std::wstring& error) {
    if (values.stapm < TDP_MIN_W || values.stapm > TDP_MAX_W) {
        error = L"STAPM fuori range 5-35W";
        return false;
    }
    if (values.fast < TDP_MIN_W || values.fast > TDP_MAX_W) {
        error = L"FAST fuori range 5-35W";
        return false;
    }
    if (values.slow < TDP_MIN_W || values.slow > TDP_MAX_W) {
        error = L"SLOW fuori range 5-35W";
        return false;
    }
    if (values.stapm > values.fast) {
        error = L"STAPM non può essere superiore a FAST";
        return false;
    }
    if (values.stapm > values.slow) {
        error = L"STAPM non può essere superiore a SLOW";
        return false;
    }
    if (values.slow > values.fast) {
        error = L"SLOW non può essere superiore a FAST (ordine richiesto: STAPM <= SLOW <= FAST)";
        return false;
    }
    return true;
}

static bool SameTdp(const TdpValues& left, const TdpValues& right) {
    return left.stapm == right.stapm && left.fast == right.fast && left.slow == right.slow;
}

static std::wstring FormatTdp(const TdpValues& values) {
    return std::to_wstring(values.stapm) + L"/" +
        std::to_wstring(values.fast) + L"/" + std::to_wstring(values.slow);
}

static bool WriteTdpState(
    bool effectiveKnown,
    const TdpValues& effective,
    const std::wstring& error) {
    std::wstring validationError;
    const bool valid = effectiveKnown && ValidateTdp(effective, validationError);
    std::wstringstream state;
    state << L"Timestamp=" << NowString() << L"\r\n";
    state << L"EffectiveKnown=" << (effectiveKnown ? L"True" : L"False") << L"\r\n";
    state << L"TdpValid=" << (valid ? L"True" : L"False") << L"\r\n";
    if (effectiveKnown) {
        state << L"TdpStapmW=" << effective.stapm << L"\r\n";
        state << L"TdpFastW=" << effective.fast << L"\r\n";
        state << L"TdpSlowW=" << effective.slow << L"\r\n";
        state << L"EffectiveStapmW=" << effective.stapm << L"\r\n";
        state << L"EffectiveFastW=" << effective.fast << L"\r\n";
        state << L"EffectiveSlowW=" << effective.slow << L"\r\n";
    } else {
        state << L"TdpStapmW=Unknown\r\nTdpFastW=Unknown\r\nTdpSlowW=Unknown\r\n";
        state << L"EffectiveStapmW=Unknown\r\nEffectiveFastW=Unknown\r\nEffectiveSlowW=Unknown\r\n";
    }
    state << L"Error=" << error << L"\r\n";
    if (!WriteFileUtf16Replace(g_tdpStatePath, state.str())) {
        Log(L"Failed to write TDP state file");
        return false;
    }
    return true;
}

static bool WriteBatteryState(bool effectiveKnown, int effective, const std::wstring& error) {
    std::wstringstream state;
    state << L"Timestamp=" << NowString() << L"\r\n";
    state << L"FeatureIdHex=0x03010001\r\n";
    state << L"EffectiveKnown=" << (effectiveKnown ? L"True" : L"False") << L"\r\n";
    if (effectiveKnown) {
        state << L"BatteryLimit80=" << (effective == 1 ? L"True" : L"False") << L"\r\n";
        state << L"EffectiveValue=" << effective << L"\r\n";
    } else {
        state << L"BatteryLimit80=Unknown\r\nEffectiveValue=Unknown\r\n";
    }
    state << L"Error=" << error << L"\r\n";
    if (!WriteFileUtf16Replace(g_batteryStatePath, state.str())) {
        Log(L"Failed to write battery state file");
        return false;
    }
    return true;
}

struct FanValues {
    std::array<int, 10> temperatures{};
    std::array<int, 10> speeds{};
    int rpm = 0;
    int temperatureC = 0;
    int fullSpeed = 0;
};

static bool ValidateFanSpeeds(const std::array<int, 10>& speeds, std::wstring& error) {
    for (size_t index = 0; index < speeds.size(); ++index) {
        if (speeds[index] < 20 || speeds[index] > 100) {
            error = L"Fan duty must be between 20 and 100 percent"; return false;
        }
        if (index != 0 && speeds[index] < speeds[index - 1]) {
            error = L"Fan duty must not decrease as temperature rises"; return false;
        }
    }
    if (speeds[7] < 60 || speeds[8] < 80 || speeds[9] < 85) {
        error = L"Unsafe high-temperature fan curve"; return false;
    }
    return true;
}

static bool SameFanCurve(const std::array<int, 10>& left, const std::array<int, 10>& right) {
    return left == right;
}

static bool ReadFanExact(WmiSession& wmi, FanValues& values, std::wstring& error) {
    FanValues result;
    if (!wmi.GetFanCurve(result.temperatures, result.speeds, error)) return false;
    if (result.temperatures != FAN_TEMPERATURES) {
        error = L"Unexpected firmware fan temperature table"; return false;
    }
    if (!ValidateFanSpeeds(result.speeds, error)) return false;
    if (!wmi.GetFeatureValue(FAN_RPM_ID, result.rpm, error) || result.rpm < 0 || result.rpm > 20000) {
        error = L"Invalid fan RPM telemetry: " + error; return false;
    }
    if (!wmi.GetFeatureValue(CPU_TEMPERATURE_ID, result.temperatureC, error) ||
        result.temperatureC < 0 || result.temperatureC > 120) {
        error = L"Invalid CPU temperature telemetry: " + error; return false;
    }
    if (!wmi.GetFeatureValue(FAN_FULL_SPEED_ID, result.fullSpeed, error) ||
        (result.fullSpeed != 0 && result.fullSpeed != 1)) {
        error = L"Invalid full-speed state: " + error; return false;
    }
    values = result; return true;
}

static bool WriteFanBackup(bool active, const std::array<int, 10>& speeds) {
    std::wstringstream state;
    state << L"Active=" << (active ? L"True" : L"False") << L"\r\n";
    for (size_t index = 0; index < speeds.size(); ++index)
        state << L"Speed" << index << L"=" << speeds[index] << L"\r\n";
    return WriteFileUtf16Replace(g_fanBackupPath, state.str());
}

static bool ReadFanBackup(std::array<int, 10>& speeds, bool& active) {
    std::wstring text;
    if (!ReadFileUtf16(g_fanBackupPath, text)) { active = false; return false; }
    active = ToUpper(StateField(text, L"Active")) == L"TRUE";
    for (size_t index = 0; index < speeds.size(); ++index) {
        const std::wstring value = StateField(text, L"Speed" + std::to_wstring(index));
        if (!ParseIntStrict(value.c_str(), speeds[index])) return false;
    }
    std::wstring error; return ValidateFanSpeeds(speeds, error);
}

static bool WriteFanState(bool known, const FanValues& values, const std::wstring& error) {
    std::array<int, 10> backup{}; bool backupActive = false;
    (void)ReadFanBackup(backup, backupActive);
    std::wstringstream state;
    state << L"Timestamp=" << NowString() << L"\r\nKnown=" << (known ? L"True" : L"False") << L"\r\n";
    state << L"BackupActive=" << (backupActive ? L"True" : L"False") << L"\r\n";
    if (known) {
        state << L"Rpm=" << values.rpm << L"\r\nTemperatureC=" << values.temperatureC
              << L"\r\nFullSpeed=" << values.fullSpeed << L"\r\n";
        for (size_t index = 0; index < values.speeds.size(); ++index)
            state << L"Temp" << index << L"=" << values.temperatures[index] << L"\r\nSpeed" << index << L"=" << values.speeds[index] << L"\r\n";
    }
    state << L"Error=" << error << L"\r\n";
    return WriteFileUtf16Replace(g_fanStatePath, state.str());
}

static bool ApplyFanVerified(WmiSession& wmi, const std::array<int, 10>& target, std::wstring& error) {
    if (!wmi.SetFanCurve(target, error)) return false;
    std::array<int, 10> temperatures{}, actual{};
    if (!wmi.GetFanCurve(temperatures, actual, error)) return false;
    if (temperatures != FAN_TEMPERATURES || !SameFanCurve(actual, target)) {
        error = L"Fan curve read-back mismatch"; return false;
    }
    return true;
}

static bool OpenWmiAsAdmin(WmiSession& wmi, std::wstring& error) {
    if (!IsAdmin()) {
        error = L"Not admin";
        Log(error);
        return false;
    }
    return wmi.Open(error);
}

static bool ReadTdpTriple(WmiSession& wmi, TdpValues& values, std::wstring& error) {
    TdpValues result;
    if (!wmi.GetFeatureValue(TDP_ID_STAPM, result.stapm, error) ||
        !wmi.GetFeatureValue(TDP_ID_FAST, result.fast, error) ||
        !wmi.GetFeatureValue(TDP_ID_SLOW, result.slow, error)) {
        return false;
    }
    values = result;
    return true;
}

static bool SetAndVerifyTdpTriple(
    WmiSession& wmi,
    int id,
    int target,
    const wchar_t* label,
    TdpValues& expected,
    std::wstring& error) {
    if (!wmi.SetFeatureValue(id, target, error)) {
        error = std::wstring(label) + L" write failed: " + error;
        return false;
    }

    if (id == TDP_ID_STAPM) {
        expected.stapm = target;
    } else if (id == TDP_ID_FAST) {
        expected.fast = target;
    } else {
        expected.slow = target;
    }

    TdpValues actual;
    if (!ReadTdpTriple(wmi, actual, error)) {
        error = std::wstring(label) + L" triple verification read failed: " + error;
        return false;
    }
    if (!SameTdp(actual, expected)) {
        error = std::wstring(label) + L" changed an unexpected TDP value: expected " +
            FormatTdp(expected) + L", read " + FormatTdp(actual);
        Log(error);
        return false;
    }
    return true;
}

static bool ApplyTdpOrdered(
    WmiSession& wmi,
    const TdpValues& before,
    const TdpValues& target,
    std::wstring& error) {
    TdpValues expected = before;

    // Raise both ceilings first so a subsequent STAPM increase is always valid.
    if (target.fast > before.fast &&
        !SetAndVerifyTdpTriple(
            wmi, TDP_ID_FAST, target.fast, L"FAST raise", expected, error)) {
        return false;
    }
    if (target.slow > before.slow &&
        !SetAndVerifyTdpTriple(
            wmi, TDP_ID_SLOW, target.slow, L"SLOW raise", expected, error)) {
        return false;
    }
    if (target.stapm != before.stapm &&
        !SetAndVerifyTdpTriple(
            wmi, TDP_ID_STAPM, target.stapm, L"STAPM", expected, error)) {
        return false;
    }
    // Lower the middle SLOW limit before FAST so the conventional hierarchy
    // STAPM <= SLOW <= FAST remains valid throughout the transaction.
    if (target.slow < before.slow &&
        !SetAndVerifyTdpTriple(
            wmi, TDP_ID_SLOW, target.slow, L"SLOW lower", expected, error)) {
        return false;
    }
    if (target.fast < before.fast &&
        !SetAndVerifyTdpTriple(
            wmi, TDP_ID_FAST, target.fast, L"FAST lower", expected, error)) {
        return false;
    }

    if (!SameTdp(expected, target)) {
        error = L"Internal TDP transition did not reach its target";
        Log(error);
        return false;
    }
    return true;
}

static bool RollbackTdp(
    WmiSession& wmi,
    const TdpValues& snapshot,
    std::wstring& error) {
    TdpValues current;
    if (!ReadTdpTriple(wmi, current, error)) {
        error = L"Rollback snapshot read failed: " + error;
        return false;
    }
    std::wstring validationError;
    if (!ValidateTdp(current, validationError)) {
        error = L"Rollback cannot safely continue from an invalid effective triple: " + validationError;
        return false;
    }
    if (!ApplyTdpOrdered(wmi, current, snapshot, error)) {
        error = L"Rollback ordered transition failed: " + error;
        return false;
    }
    TdpValues verified;
    if (!ReadTdpTriple(wmi, verified, error) || !SameTdp(verified, snapshot)) {
        error = L"Rollback final verification failed" + (error.empty() ? L"" : L": " + error);
        return false;
    }
    return true;
}

static bool ReadBatteryExact(WmiSession& wmi, int& value, std::wstring& error) {
    if (!wmi.GetFeatureValue(BATTERY_LIMIT_80_ID, value, error)) {
        return false;
    }
    if (value != 0 && value != 1) {
        error = L"Invalid battery firmware value " + std::to_wstring(value) + L" (expected 0 or 1)";
        Log(error);
        return false;
    }
    return true;
}

static void ReportBatteryFailureState(WmiSession& wmi, const std::wstring& operationError) {
    int effective = 0;
    std::wstring readError;
    if (ReadBatteryExact(wmi, effective, readError)) {
        WriteBatteryState(true, effective, operationError);
    } else {
        WriteBatteryState(false, 0, operationError + L"; effective read failed: " + readError);
    }
}

static int DoStatus() {
    Log(L"DoStatus start");
    std::wstring error;
    NamedMutexLock lock;
    if (!lock.Acquire(TDP_MUTEX_NAME, error)) {
        WriteTdpState(false, {}, error);
        ConsoleWrite(L"ERROR: " + error + L"\r\n");
        return ReturnCode(10);
    }

    WmiSession wmi;
    if (!OpenWmiAsAdmin(wmi, error)) {
        WriteTdpState(false, {}, error);
        ConsoleWrite(L"ERROR: " + error + L"\r\n");
        return ReturnCode(11);
    }

    TdpValues values;
    if (!ReadTdpTriple(wmi, values, error)) {
        WriteTdpState(false, {}, error);
        ConsoleWrite(L"ERROR: " + error + L"\r\n");
        return ReturnCode(12);
    }
    std::wstring validationError;
    if (!ValidateTdp(values, validationError)) {
        error = L"Firmware returned incoherent TDP triple: " + validationError;
        WriteTdpState(true, values, error);
        ConsoleWrite(L"ERROR: " + error + L"\r\n");
        return ReturnCode(13);
    }

    if (!WriteTdpState(true, values, L"")) {
        ConsoleWrite(L"ERROR: failed to publish TDP state\r\n");
        return ReturnCode(14);
    }
    std::wstringstream output;
    output << L"OK\r\nSTAPM=" << values.stapm << L"\r\nFAST=" << values.fast
           << L"\r\nSLOW=" << values.slow << L"\r\n";
    ConsoleWrite(output.str());
    return ReturnCode(0);
}

static int DoSet(const TdpValues& target) {
    Log(L"DoSet start " + std::to_wstring(target.stapm) + L"/" +
        std::to_wstring(target.fast) + L"/" + std::to_wstring(target.slow));
    std::wstring error;
    NamedMutexLock lock;
    if (!lock.Acquire(TDP_MUTEX_NAME, error)) {
        WriteTdpState(false, {}, error);
        ConsoleWrite(L"ERROR: " + error + L"\r\n");
        return ReturnCode(21);
    }
    if (!ValidateTdp(target, error)) {
        WriteTdpState(false, {}, error);
        ConsoleWrite(L"ERROR: " + error + L"\r\n");
        return ReturnCode(20);
    }

    WmiSession wmi;
    if (!OpenWmiAsAdmin(wmi, error)) {
        WriteTdpState(false, {}, error);
        ConsoleWrite(L"ERROR: " + error + L"\r\n");
        return ReturnCode(22);
    }

    TdpValues snapshot;
    if (!ReadTdpTriple(wmi, snapshot, error)) {
        WriteTdpState(false, {}, L"Snapshot failed: " + error);
        ConsoleWrite(L"ERROR: snapshot failed: " + error + L"\r\n");
        return ReturnCode(23);
    }
    std::wstring snapshotError;
    if (!ValidateTdp(snapshot, snapshotError)) {
        error = L"Snapshot is not a valid TDP triple: " + snapshotError;
        WriteTdpState(true, snapshot, error);
        ConsoleWrite(L"ERROR: " + error + L"\r\n");
        return ReturnCode(24);
    }

    std::wstring applyError;
    if (!ApplyTdpOrdered(wmi, snapshot, target, applyError)) {
        Log(L"TDP apply failed; starting rollback: " + applyError);
        std::wstring rollbackError;
        if (RollbackTdp(wmi, snapshot, rollbackError)) {
            error = applyError + L"; rollback verified";
            WriteTdpState(true, snapshot, error);
            ConsoleWrite(L"ERROR: " + error + L"\r\n");
            return ReturnCode(25);
        }

        TdpValues effective;
        std::wstring effectiveError;
        const bool known = ReadTdpTriple(wmi, effective, effectiveError);
        error = applyError + L"; rollback failed: " + rollbackError;
        if (!known) {
            error += L"; effective read failed: " + effectiveError;
        }
        WriteTdpState(known, effective, error);
        ConsoleWrite(L"ERROR: " + error + L"\r\n");
        return ReturnCode(26);
    }

    if (!WriteTdpState(true, target, L"")) {
        ConsoleWrite(L"ERROR: TDP was set and verified, but state publication failed\r\n");
        return ReturnCode(27);
    }
    std::wstringstream output;
    output << L"OK\r\nSET=" << target.stapm << L"/" << target.fast << L"/"
           << target.slow << L"\r\n";
    ConsoleWrite(output.str());
    return ReturnCode(0);
}

static int DoBatteryStatus() {
    Log(L"DoBatteryStatus start");
    std::wstring error;
    NamedMutexLock lock;
    if (!lock.Acquire(BATTERY_MUTEX_NAME, error)) {
        WriteBatteryState(false, 0, error);
        ConsoleWrite(L"ERROR: " + error + L"\r\n");
        return ReturnCode(30);
    }

    WmiSession wmi;
    if (!OpenWmiAsAdmin(wmi, error)) {
        WriteBatteryState(false, 0, error);
        ConsoleWrite(L"ERROR: " + error + L"\r\n");
        return ReturnCode(31);
    }
    int value = 0;
    if (!ReadBatteryExact(wmi, value, error)) {
        WriteBatteryState(false, 0, error);
        ConsoleWrite(L"ERROR: " + error + L"\r\n");
        return ReturnCode(32);
    }

    if (!WriteBatteryState(true, value, L"")) {
        ConsoleWrite(L"ERROR: failed to publish battery state\r\n");
        return ReturnCode(33);
    }
    ConsoleWrite(L"OK\r\nBATTERY80=" + std::to_wstring(value) + L"\r\n");
    return ReturnCode(0);
}

static int DoBatterySet(int target) {
    Log(L"DoBatterySet start " + std::to_wstring(target));
    std::wstring error;
    NamedMutexLock lock;
    if (!lock.Acquire(BATTERY_MUTEX_NAME, error)) {
        WriteBatteryState(false, 0, error);
        ConsoleWrite(L"ERROR: " + error + L"\r\n");
        return ReturnCode(40);
    }

    WmiSession wmi;
    if (!OpenWmiAsAdmin(wmi, error)) {
        WriteBatteryState(false, 0, error);
        ConsoleWrite(L"ERROR: " + error + L"\r\n");
        return ReturnCode(41);
    }
    if (!wmi.SetFeatureValue(BATTERY_LIMIT_80_ID, target, error)) {
        ReportBatteryFailureState(wmi, error);
        ConsoleWrite(L"ERROR: " + error + L"\r\n");
        return ReturnCode(42);
    }

    int actual = 0;
    if (!ReadBatteryExact(wmi, actual, error)) {
        WriteBatteryState(false, 0, error);
        ConsoleWrite(L"ERROR: final battery read failed: " + error + L"\r\n");
        return ReturnCode(43);
    }
    if (actual != target) {
        error = L"Battery final verification mismatch: requested " + std::to_wstring(target) +
            L", read " + std::to_wstring(actual);
        WriteBatteryState(true, actual, error);
        ConsoleWrite(L"ERROR: " + error + L"\r\n");
        return ReturnCode(44);
    }

    if (!WriteBatteryState(true, actual, L"")) {
        ConsoleWrite(L"ERROR: battery was set and verified, but state publication failed\r\n");
        return ReturnCode(45);
    }
    ConsoleWrite(L"OK\r\nBATTERY80=" + std::to_wstring(actual) + L"\r\n");
    return ReturnCode(0);
}

static int DoBatteryToggle() {
    Log(L"DoBatteryToggle start");
    std::wstring error;
    NamedMutexLock lock;
    if (!lock.Acquire(BATTERY_MUTEX_NAME, error)) {
        WriteBatteryState(false, 0, error);
        ConsoleWrite(L"ERROR: " + error + L"\r\n");
        return ReturnCode(50);
    }

    WmiSession wmi;
    if (!OpenWmiAsAdmin(wmi, error)) {
        WriteBatteryState(false, 0, error);
        ConsoleWrite(L"ERROR: " + error + L"\r\n");
        return ReturnCode(51);
    }

    int before = 0;
    if (!ReadBatteryExact(wmi, before, error)) {
        WriteBatteryState(false, 0, error);
        ConsoleWrite(L"ERROR: " + error + L"\r\n");
        return ReturnCode(52);
    }
    const int target = before == 0 ? 1 : 0;
    if (!wmi.SetFeatureValue(BATTERY_LIMIT_80_ID, target, error)) {
        ReportBatteryFailureState(wmi, error);
        ConsoleWrite(L"ERROR: " + error + L"\r\n");
        return ReturnCode(53);
    }

    int actual = 0;
    if (!ReadBatteryExact(wmi, actual, error)) {
        WriteBatteryState(false, 0, error);
        ConsoleWrite(L"ERROR: final battery read failed: " + error + L"\r\n");
        return ReturnCode(54);
    }
    if (actual != target) {
        error = L"Battery toggle verification mismatch: requested " + std::to_wstring(target) +
            L", read " + std::to_wstring(actual);
        WriteBatteryState(true, actual, error);
        ConsoleWrite(L"ERROR: " + error + L"\r\n");
        return ReturnCode(55);
    }

    if (!WriteBatteryState(true, actual, L"")) {
        ConsoleWrite(L"ERROR: battery was toggled and verified, but state publication failed\r\n");
        return ReturnCode(56);
    }
    ConsoleWrite(L"OK\r\nBATTERY80=" + std::to_wstring(actual) + L"\r\n");
    return ReturnCode(0);
}

static int DoFanStatus() {
    std::wstring error; NamedMutexLock lock;
    if (!lock.Acquire(FAN_MUTEX_NAME, error)) { WriteFanState(false, {}, error); return ReturnCode(60); }
    WmiSession wmi;
    if (!OpenWmiAsAdmin(wmi, error)) { WriteFanState(false, {}, error); return ReturnCode(61); }
    FanValues values;
    if (!ReadFanExact(wmi, values, error)) { WriteFanState(false, {}, error); ConsoleWrite(L"ERROR: " + error + L"\r\n"); return ReturnCode(62); }
    if (!WriteFanState(true, values, L"")) return ReturnCode(63);
    ConsoleWrite(L"OK\r\nRPM=" + std::to_wstring(values.rpm) + L"\r\nTEMP=" + std::to_wstring(values.temperatureC) + L"\r\n");
    return ReturnCode(0);
}

static int DoFanSet(const std::array<int, 10>& target) {
    std::wstring error;
    if (!ValidateFanSpeeds(target, error)) { ConsoleWrite(L"ERROR: " + error + L"\r\n"); return ReturnCode(70); }
    NamedMutexLock lock;
    if (!lock.Acquire(FAN_MUTEX_NAME, error)) return ReturnCode(71);
    WmiSession wmi;
    if (!OpenWmiAsAdmin(wmi, error)) return ReturnCode(72);
    FanValues snapshot;
    if (!ReadFanExact(wmi, snapshot, error)) { WriteFanState(false, {}, error); return ReturnCode(73); }
    if (snapshot.fullSpeed != 0) {
        error = L"Full-speed override is active; refusing curve write"; WriteFanState(true, snapshot, error); return ReturnCode(74);
    }
    std::array<int, 10> backup{}; bool backupActive = false;
    const bool haveBackup = ReadFanBackup(backup, backupActive);
    const bool createdBackup = !haveBackup || !backupActive;
    if (createdBackup && !WriteFanBackup(true, snapshot.speeds)) return ReturnCode(75);
    if (!ApplyFanVerified(wmi, target, error)) {
        std::wstring rollbackError;
        const bool rolledBack = ApplyFanVerified(wmi, snapshot.speeds, rollbackError);
        FanValues effective; std::wstring readError;
        const bool known = ReadFanExact(wmi, effective, readError);
        error += rolledBack ? L"; rollback verified" : L"; rollback failed: " + rollbackError;
        if (rolledBack && createdBackup && !WriteFanBackup(false, snapshot.speeds))
            error += L"; backup marker cleanup failed";
        WriteFanState(known, effective, error);
        return ReturnCode(rolledBack ? 76 : 77);
    }
    FanValues effective;
    if (!ReadFanExact(wmi, effective, error)) { WriteFanState(false, {}, error); return ReturnCode(78); }
    if (!WriteFanState(true, effective, L"")) return ReturnCode(79);
    return ReturnCode(0);
}

static int DoFanRestore() {
    std::wstring error; NamedMutexLock lock;
    if (!lock.Acquire(FAN_MUTEX_NAME, error)) return ReturnCode(80);
    std::array<int, 10> backup{}; bool active = false;
    const bool haveActiveBackup = ReadFanBackup(backup, active) && active;
    WmiSession wmi;
    if (!OpenWmiAsAdmin(wmi, error)) return ReturnCode(81);
    if (!haveActiveBackup) {
        FanValues unchanged;
        if (!ReadFanExact(wmi, unchanged, error)) { WriteFanState(false, {}, error); return ReturnCode(84); }
        if (!WriteFanState(true, unchanged, L"")) return ReturnCode(85);
        return ReturnCode(0);
    }
    if (!ApplyFanVerified(wmi, backup, error)) {
        const std::wstring firstError = error;
        if (!ApplyFanVerified(wmi, backup, error)) {
            WriteFanState(false, {}, L"Restore failed twice: " + firstError + L"; " + error);
            return ReturnCode(82);
        }
    }
    if (!WriteFanBackup(false, backup)) return ReturnCode(83);
    FanValues effective;
    if (!ReadFanExact(wmi, effective, error)) { WriteFanState(false, {}, error); return ReturnCode(84); }
    if (!WriteFanState(true, effective, L"")) return ReturnCode(85);
    return ReturnCode(0);
}

static int UsageError(const std::wstring& detail) {
    if (!detail.empty()) {
        ConsoleWrite(L"ERROR: " + detail + L"\r\n");
    }
    ConsoleWrite(
        L"Uso:\r\n"
        L"  LegionGoNativeWmiProbe.exe status\r\n"
        L"  LegionGoNativeWmiProbe.exe set <stapm> <fast> <slow>\r\n"
        L"  LegionGoNativeWmiProbe.exe battery-status\r\n"
        L"  LegionGoNativeWmiProbe.exe battery-set 0|1\r\n"
        L"  LegionGoNativeWmiProbe.exe battery-toggle\r\n"
        L"  LegionGoNativeWmiProbe.exe fan-status\r\n"
        L"  LegionGoNativeWmiProbe.exe fan-set <10 duty percentages>\r\n"
        L"  LegionGoNativeWmiProbe.exe fan-restore\r\n");
    return ReturnCode(2);
}

int wmain(int argc, wchar_t** argv) {
    InitPaths();
    Log(L"Probe start");

    if (argc < 2 || argv[1] == nullptr) {
        return UsageError(L"Missing command");
    }

    const std::wstring command = ToUpper(argv[1]);
    if (command == L"STATUS") {
        return argc == 2 ? DoStatus() : UsageError(L"status takes no arguments");
    }
    if (command == L"SET") {
        if (argc != 5) {
            return UsageError(L"set requires exactly three integer arguments");
        }
        TdpValues target;
        if (!ParseIntStrict(argv[2], target.stapm) ||
            !ParseIntStrict(argv[3], target.fast) ||
            !ParseIntStrict(argv[4], target.slow)) {
            return UsageError(L"set arguments must be complete decimal integers");
        }
        return DoSet(target);
    }
    if (command == L"BATTERY-STATUS") {
        return argc == 2 ? DoBatteryStatus() :
            UsageError(L"battery-status takes no arguments");
    }
    if (command == L"BATTERY-SET") {
        if (argc != 3 || argv[2] == nullptr ||
            (std::wstring(argv[2]) != L"0" && std::wstring(argv[2]) != L"1")) {
            return UsageError(L"battery-set requires exactly literal 0 or 1");
        }
        return DoBatterySet(argv[2][0] == L'1' ? 1 : 0);
    }
    if (command == L"BATTERY-TOGGLE") {
        return argc == 2 ? DoBatteryToggle() :
            UsageError(L"battery-toggle takes no arguments");
    }
    if (command == L"FAN-STATUS") {
        return argc == 2 ? DoFanStatus() : UsageError(L"fan-status takes no arguments");
    }
    if (command == L"FAN-SET") {
        if (argc != 12) return UsageError(L"fan-set requires exactly ten percentages");
        std::array<int, 10> speeds{};
        for (size_t index = 0; index < speeds.size(); ++index)
            if (!ParseIntStrict(argv[index + 2], speeds[index])) return UsageError(L"fan-set values must be integers");
        return DoFanSet(speeds);
    }
    if (command == L"FAN-RESTORE") {
        return argc == 2 ? DoFanRestore() : UsageError(L"fan-restore takes no arguments");
    }

    ConsoleWrite(L"Comando sconosciuto.\r\n");
    return ReturnCode(3);
}
