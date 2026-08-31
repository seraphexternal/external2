/*
 * SeraphLoader
 * Loads seraph_drv.sys as a kernel service and exposes the BYOVD:: process
 * memory interface used by the external cheat.
 *
 * Usage: SeraphLoader <path\to\seraph_drv.sys>
 *
 * Runs in normal boot only if the driver is signed (EV/WHQL) or the service
 * is created while signature enforcement is relaxed. Opening the device
 * requires admin; copying via MmCopyVirtualMemory to a target PID uses the
 * same privilege the loader has.
 */
#include <windows.h>
#include <winioctl.h>
#include <winsvc.h>
#include <cstdio>
#include <cstdint>
#include <vector>
#include <string>

#include "../driver/byovd_protocol.h"

namespace {

const wchar_t* kServiceName = L"SeraphIO";
const wchar_t* kDriverPath  = L"System32\\drivers\\seraph_drv.sys";

HANDLE g_dev = INVALID_HANDLE_VALUE;

bool CopyDriverToSystem(const wchar_t* srcPath)
{
    wchar_t sys32[MAX_PATH];
    if (!GetSystemDirectoryW(sys32, MAX_PATH)) return false;
    std::wstring dest = std::wstring(sys32) + L"\\drivers\\seraph_drv.sys";
    // Always overwrite so a freshly signed image always propagates.
    return CopyFileW(srcPath, dest.c_str(), FALSE) != 0;
}

bool InstallAndStartService()
{
    SC_HANDLE scm = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_ALL_ACCESS);
    if (!scm) {
        wprintf(L"OpenSCManager failed: %lu\n", GetLastError());
        return false;
    }

    // Stop + delete any prior instance so we reload a fresh image.
    SC_HANDLE svc = OpenServiceW(scm, kServiceName, SERVICE_ALL_ACCESS);
    if (svc) {
        SERVICE_STATUS ss;
        ControlService(svc, SERVICE_CONTROL_STOP, &ss);
        DeleteService(svc);
        CloseHandle(svc);
    }

    wchar_t sys32[MAX_PATH];
    GetSystemDirectoryW(sys32, MAX_PATH);
    std::wstring fullPath = std::wstring(sys32) + L"\\drivers\\seraph_drv.sys";

    svc = CreateServiceW(
        scm,
        kServiceName,
        kServiceName,
        SERVICE_ALL_ACCESS,
        SERVICE_KERNEL_DRIVER,
        SERVICE_DEMAND_START,
        SERVICE_ERROR_IGNORE,
        fullPath.c_str(),
        nullptr, nullptr, nullptr, nullptr, nullptr);
    if (!svc) {
        wprintf(L"CreateService failed: %lu\n", GetLastError());
        CloseServiceHandle(scm);
        return false;
    }

    if (!StartServiceW(svc, 0, nullptr)) {
        DWORD err = GetLastError();
        wprintf(L"StartService failed: %lu (try test-signing or F8 'disable driver signature enforcement')\n", err);
        DeleteService(svc);
        CloseHandle(svc);
        CloseServiceHandle(scm);
        return false;
    }

    CloseServiceHandle(svc);
    CloseServiceHandle(scm);
    return true;
}

} // namespace

int wmain(int argc, wchar_t** argv)
{
    if (argc < 2) {
        wprintf(L"usage: SeraphLoader <path\\to\\seraph_drv.sys>\n");
        return 1;
    }

    // Copy the driver binary into \System32\drivers so the service can load it.
    if (!CopyDriverToSystem(argv[1])) {
        wprintf(L"copy driver -> drivers dir failed: %lu\n", GetLastError());
        return 1;
    }

    if (!InstallAndStartService()) {
        wprintf(L"service load failed\n");
        return 1;
    }

    g_dev = CreateFileW(BYOVD_DEVICE_NAME, GENERIC_READ | GENERIC_WRITE, 0, nullptr,
                        OPEN_EXISTING, 0, nullptr);
    if (g_dev == INVALID_HANDLE_VALUE) {
        wprintf(L"open \\\\.\\byovd failed: %lu\n", GetLastError());
        return 1;
    }

    wprintf(L"driver loaded; device open\n");
    return 0;
}

namespace BYOVD {

bool Open(const wchar_t* devicePath) {
    g_dev = CreateFileW(devicePath, GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
    return g_dev != INVALID_HANDLE_VALUE;
}

void Close() {
    if (g_dev != INVALID_HANDLE_VALUE) CloseHandle(g_dev);
    g_dev = INVALID_HANDLE_VALUE;
}

bool IsOpen() {
    return g_dev != INVALID_HANDLE_VALUE;
}

bool KernelReadProcessMemory(HANDLE pid, uint64_t addr, void* buffer, size_t size) {
    if (g_dev == INVALID_HANDLE_VALUE) return false;
    std::vector<uint8_t> inout(sizeof(BYOVD_READ_PROC_REQUEST) + size);
    BYOVD_READ_PROC_REQUEST* req = (BYOVD_READ_PROC_REQUEST*)inout.data();
    req->pid = (uint32_t)(ULONG_PTR)pid;
    req->address = addr;
    req->size = size;
    DWORD returned = 0;
    BOOL ok = DeviceIoControl(g_dev, IOCTL_BYOVD_READ_PROC, inout.data(), (DWORD)inout.size(),
                              inout.data(), (DWORD)inout.size(), &returned, nullptr);
    if (!ok) return false;
    // returned == sizeof(header) + data; copy the data tail out.
    size_t dataLen = returned - sizeof(BYOVD_READ_PROC_REQUEST);
    if (dataLen < size) return false;
    memcpy(buffer, inout.data() + sizeof(BYOVD_READ_PROC_REQUEST), size);
    return true;
}

bool KernelWriteProcessMemory(HANDLE pid, uint64_t addr, const void* buffer, size_t size) {
    if (g_dev == INVALID_HANDLE_VALUE) return false;
    std::vector<uint8_t> inout(sizeof(BYOVD_WRITE_PROC_REQUEST) + size);
    BYOVD_WRITE_PROC_REQUEST* req = (BYOVD_WRITE_PROC_REQUEST*)inout.data();
    req->pid = (uint32_t)(ULONG_PTR)pid;
    req->address = addr;
    req->size = size;
    memcpy(req->data, buffer, size);
    DWORD returned = 0;
    return DeviceIoControl(g_dev, IOCTL_BYOVD_WRITE_PROC, inout.data(), (DWORD)inout.size(),
                           inout.data(), (DWORD)inout.size(), &returned, nullptr) != 0;
}

} // namespace BYOVD
