#include "LegionGoPresentTrace.h"

#include <evntrace.h>
#include <evntcons.h>

#include <atomic>
#include <cstdint>
#include <iterator>
#include <map>
#include <mutex>
#include <thread>
#include <utility>
#include <vector>

#pragma comment(lib, "advapi32.lib")

namespace LegionGoPresentTrace {
namespace {

constexpr wchar_t kSessionName[] = L"LegionGoControlNativePresentTrace";
// Public provider GUIDs and event IDs from the Microsoft DXGI/D3D9 manifests.
constexpr GUID kDxgiProvider{0xCA11C036,0x0102,0x4A2D,{0xA6,0xAD,0xF0,0x3C,0xFE,0xD5,0xD3,0xC9}};
constexpr GUID kD3d9Provider{0x783ACA0A,0x790E,0x4D7F,{0x84,0x51,0xAA,0x85,0x05,0x11,0xC6,0xB9}};
constexpr USHORT kDxgiPresentStart = 0x002a;
constexpr USHORT kD3d9PresentStart = 0x0001;
constexpr ULONGLONG kPresentKeyword = 0x8000000000000002ULL;

struct SingleEventIdFilter {
    BOOLEAN filterIn = TRUE;
    UCHAR reserved = 0;
    USHORT count = 1;
    USHORT eventId = 0;
};

ULONG EnableOnlyEvent(TRACEHANDLE session, const GUID& provider, USHORT eventId) {
    SingleEventIdFilter filter{};
    filter.eventId = eventId;
    EVENT_FILTER_DESCRIPTOR descriptor{};
    descriptor.Ptr = reinterpret_cast<ULONGLONG>(&filter);
    descriptor.Size = sizeof(filter);
    descriptor.Type = EVENT_FILTER_TYPE_EVENT_ID;
    ENABLE_TRACE_PARAMETERS parameters{};
    parameters.Version = ENABLE_TRACE_PARAMETERS_VERSION_2;
    parameters.EnableFilterDesc = &descriptor;
    parameters.FilterDescCount = 1;
    return EnableTraceEx2(session, &provider, EVENT_CONTROL_CODE_ENABLE_PROVIDER,
                          TRACE_LEVEL_VERBOSE, kPresentKeyword, 0, 0, &parameters);
}

std::vector<unsigned char> PropertiesBuffer() {
    std::vector<unsigned char> bytes(sizeof(EVENT_TRACE_PROPERTIES) + sizeof(kSessionName));
    auto* properties = reinterpret_cast<EVENT_TRACE_PROPERTIES*>(bytes.data());
    ZeroMemory(properties, sizeof(*properties));
    properties->Wnode.BufferSize = static_cast<ULONG>(bytes.size());
    properties->Wnode.Flags = WNODE_FLAG_TRACED_GUID;
    properties->Wnode.ClientContext = 1; // QPC timestamps
    properties->LogFileMode = EVENT_TRACE_REAL_TIME_MODE;
    properties->LoggerNameOffset = sizeof(EVENT_TRACE_PROPERTIES);
    return bytes;
}

} // namespace

struct Collector::Impl {
    TRACEHANDLE session = 0;
    TRACEHANDLE trace = INVALID_PROCESSTRACE_HANDLE;
    EVENT_TRACE_LOGFILEW log{};
    std::thread thread;
    std::atomic_bool running{false};
    LARGE_INTEGER frequency{};
    std::mutex mutex;
    std::vector<Frame> pending;
    std::map<DWORD, LONGLONG> lastQpc;

    static VOID WINAPI OnEvent(EVENT_RECORD* event) {
        if (event == nullptr || event->UserContext == nullptr) return;
        auto* self = static_cast<Impl*>(event->UserContext);
        const bool dxgi = IsEqualGUID(event->EventHeader.ProviderId, kDxgiProvider) &&
                          event->EventHeader.EventDescriptor.Id == kDxgiPresentStart;
        const bool d3d9 = IsEqualGUID(event->EventHeader.ProviderId, kD3d9Provider) &&
                          event->EventHeader.EventDescriptor.Id == kD3d9PresentStart;
        if (!dxgi && !d3d9) return;
        const DWORD pid = event->EventHeader.ProcessId;
        const LONGLONG qpc = event->EventHeader.TimeStamp.QuadPart;
        if (pid == 0 || qpc <= 0 || self->frequency.QuadPart <= 0) return;
        std::lock_guard<std::mutex> lock(self->mutex);
        const auto previous = self->lastQpc.find(pid);
        const LONGLONG previousQpc = previous == self->lastQpc.end() ? 0 : previous->second;
        self->lastQpc[pid] = qpc;
        if (self->lastQpc.size() > 2048U) self->lastQpc.erase(self->lastQpc.begin());
        if (previousQpc == 0 || qpc <= previousQpc) return;
        const double milliseconds = 1000.0 * static_cast<double>(qpc - previousQpc) /
                                    static_cast<double>(self->frequency.QuadPart);
        if (milliseconds > 0.05 && milliseconds < 10000.0) {
            self->pending.push_back({pid, milliseconds});
            if (self->pending.size() > 16384U) self->pending.erase(self->pending.begin(), self->pending.begin() + 8192);
        }
    }

    void StopStaleSession() {
        auto bytes = PropertiesBuffer();
        ControlTraceW(0, kSessionName, reinterpret_cast<EVENT_TRACE_PROPERTIES*>(bytes.data()), EVENT_TRACE_CONTROL_STOP);
    }
};

Collector::Collector() : impl_(new Impl) {
    // Cleanup is unconditional so an orphaned session from a crashed process
    // cannot remain enabled when FPS capture starts disabled on the next run.
    impl_->StopStaleSession();
}
Collector::~Collector() { Stop(); delete impl_; impl_ = nullptr; }

bool Collector::Start() {
    if (impl_ == nullptr || impl_->running.load()) return impl_ != nullptr;
    // ProcessTrace can return independently (for example if an administrator
    // stops the session). A completed std::thread must be joined before reuse.
    if (impl_->thread.joinable()) Stop();
    impl_->StopStaleSession();
    auto bytes = PropertiesBuffer();
    ULONG status = StartTraceW(&impl_->session, kSessionName,
                               reinterpret_cast<EVENT_TRACE_PROPERTIES*>(bytes.data()));
    if (status != ERROR_SUCCESS) { impl_->session = 0; return false; }
    // Filter in the providers, not merely in our callback. Without this ETW
    // would deliver every analytic DXGI/D3D9 event and could disturb games.
    status = EnableOnlyEvent(impl_->session, kDxgiProvider, kDxgiPresentStart);
    if (status == ERROR_SUCCESS) status = EnableOnlyEvent(impl_->session, kD3d9Provider, kD3d9PresentStart);
    if (status != ERROR_SUCCESS) { Stop(); return false; }
    QueryPerformanceFrequency(&impl_->frequency);
    {
        std::lock_guard<std::mutex> lock(impl_->mutex);
        impl_->pending.reserve(4096U);
    }
    impl_->log = {};
    impl_->log.LoggerName = const_cast<LPWSTR>(kSessionName);
    impl_->log.ProcessTraceMode = PROCESS_TRACE_MODE_EVENT_RECORD | PROCESS_TRACE_MODE_REAL_TIME;
    impl_->log.EventRecordCallback = &Impl::OnEvent;
    impl_->log.Context = impl_;
    impl_->trace = OpenTraceW(&impl_->log);
    if (impl_->trace == INVALID_PROCESSTRACE_HANDLE) { Stop(); return false; }
    impl_->running.store(true);
    const TRACEHANDLE openedTrace = impl_->trace;
    try {
        impl_->thread = std::thread([this, openedTrace] {
            SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);
            TRACEHANDLE handle = openedTrace;
            ProcessTrace(&handle, 1, nullptr, nullptr);
            impl_->running.store(false);
        });
    } catch (...) { Stop(); return false; }
    return true;
}

void Collector::Stop() {
    if (impl_ == nullptr) return;
    if (impl_->session != 0) {
        auto bytes = PropertiesBuffer();
        ControlTraceW(impl_->session, kSessionName,
                      reinterpret_cast<EVENT_TRACE_PROPERTIES*>(bytes.data()), EVENT_TRACE_CONTROL_STOP);
        impl_->session = 0;
    }
    if (impl_->trace != INVALID_PROCESSTRACE_HANDLE) {
        CloseTrace(impl_->trace);
        impl_->trace = INVALID_PROCESSTRACE_HANDLE;
    }
    if (impl_->thread.joinable()) impl_->thread.join();
    impl_->log = {};
    impl_->running.store(false);
    std::lock_guard<std::mutex> lock(impl_->mutex);
    impl_->pending.clear(); impl_->lastQpc.clear();
}

bool Collector::Running() const { return impl_ != nullptr && impl_->running.load(); }

std::vector<Frame> Collector::Drain() {
    std::vector<Frame> result;
    if (impl_ == nullptr) return result;
    std::lock_guard<std::mutex> lock(impl_->mutex);
    result.assign(std::make_move_iterator(impl_->pending.begin()),
                  std::make_move_iterator(impl_->pending.end()));
    impl_->pending.clear(); // Retain callback-side capacity; allocations stay off the ETW path.
    return result;
}

} // namespace LegionGoPresentTrace
