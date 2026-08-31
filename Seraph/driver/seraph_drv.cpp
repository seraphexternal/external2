/*
 * seraph_drv.sys
 * External-cheat kernel memory primitive.
 *
 * A minimal WDM driver that exposes cross-process virtual memory read/write
 * to the usermode loader/cheat via buffered IOCTLs. Uses MmCopyVirtualMemory
 * to cross the process boundary at ring 0, so the cheat never touches
 * NtReadVirtualMemory/NtWriteVirtualMemory (no usermode hook surface).
 *
 * MmCopyVirtualMemory / PsLookupProcessByProcessId are exported by ntoskrnl
 * but not always declared by the 26100 headers, so we declare them manually
 * (well-known clean-room driver pattern).
 *
 * Build: kernel mode, /D _AMD64_, DriverEntry entry, no CRT, link ntoskrnl.
 */
#include <ntddk.h>
#include "byovd_protocol.h"

#define DEVICE_NAME  L"\\Device\\byovd"
#define SYMLINK_NAME L"\\DosDevices\\byovd"

// Cap individual read/write size to bound pool usage.
#define MAX_RW_SIZE (16 * 1024 * 1024) // 16 MB

typedef struct _BYOVD_DEVICE_EXTENSION {
    PDEVICE_OBJECT DeviceObject;
} BYOVD_DEVICE_EXTENSION;

// --- ntoskrnl exports declared manually -----------------------------------
extern "C" NTKERNELAPI NTSTATUS NTAPI
PsLookupProcessByProcessId(HANDLE ProcessId, PEPROCESS* Process);

extern "C" NTKERNELAPI NTSTATUS NTAPI
MmCopyVirtualMemory(
    PEPROCESS SourceProcess, PVOID SourceAddress,
    PEPROCESS TargetProcess, PVOID TargetAddress,
    SIZE_T BufferSize, MODE PreviousMode,
    PSIZE_T NumberOfBytesTransferred);

// ---------------------------------------------------------------------------
// Cross-process copy helpers
// ---------------------------------------------------------------------------

// Resolve EPROCESS from a PID and copy target VA into the current process.
static NTSTATUS ReadFromProcess(ULONG pid, PVOID srcVa, PVOID dstBuf, SIZE_T size)
{
    PEPROCESS target = nullptr;
    NTSTATUS status = PsLookupProcessByProcessId((HANDLE)(ULONG_PTR)pid, &target);
    if (!NT_SUCCESS(status) || !target) {
        return STATUS_INVALID_PARAMETER;
    }

    SIZE_T copied = 0;
    status = MmCopyVirtualMemory(
        target, srcVa,
        PsGetCurrentProcess(), dstBuf,
        size, UserMode, &copied);

    ObDereferenceObject(target);
    return status;
}

// Write our buffer into the target process VA.
static NTSTATUS WriteToProcess(ULONG pid, PVOID srcBuf, PVOID dstVa, SIZE_T size)
{
    PEPROCESS target = nullptr;
    NTSTATUS status = PsLookupProcessByProcessId((HANDLE)(ULONG_PTR)pid, &target);
    if (!NT_SUCCESS(status) || !target) {
        return STATUS_INVALID_PARAMETER;
    }

    SIZE_T copied = 0;
    status = MmCopyVirtualMemory(
        PsGetCurrentProcess(), srcBuf,
        target, dstVa,
        size, UserMode, &copied);

    ObDereferenceObject(target);
    return status;
}

// ---------------------------------------------------------------------------
// IOCTL dispatch
// ---------------------------------------------------------------------------

static NTSTATUS HandleReadProc(PIRP irp, void* sysBuf, size_t inLen, size_t outLen)
{
    if (inLen < sizeof(BYOVD_READ_PROC_REQUEST)) return STATUS_BUFFER_TOO_SMALL;

    BYOVD_READ_PROC_REQUEST* req = (BYOVD_READ_PROC_REQUEST*)sysBuf;
    size_t available = outLen - sizeof(BYOVD_READ_PROC_REQUEST);
    if (req->size > available) return STATUS_BUFFER_TOO_SMALL;
    if (req->size > MAX_RW_SIZE) return STATUS_INVALID_BUFFER_SIZE;

    NTSTATUS status = ReadFromProcess(
        req->pid,
        (PVOID)req->address,   // source: target process VA
        (PVOID)(req + 1),      // dest:   our system buffer tail
        req->size);

    irp->IoStatus.Information = sizeof(BYOVD_READ_PROC_REQUEST) + req->size;
    return status;
}

static NTSTATUS HandleWriteProc(PIRP irp, void* sysBuf, size_t inLen, size_t outLen)
{
    if (inLen < sizeof(BYOVD_WRITE_PROC_REQUEST)) return STATUS_BUFFER_TOO_SMALL;

    BYOVD_WRITE_PROC_REQUEST* req = (BYOVD_WRITE_PROC_REQUEST*)sysBuf;
    if (req->size > inLen - sizeof(BYOVD_WRITE_PROC_REQUEST)) return STATUS_BUFFER_TOO_SMALL;
    if (req->size > MAX_RW_SIZE) return STATUS_INVALID_BUFFER_SIZE;

    NTSTATUS status = WriteToProcess(
        req->pid,
        (PVOID)(req + 1),      // source: our system buffer tail
        (PVOID)req->address,   // dest:   target process VA
        req->size);

    irp->IoStatus.Information = 0;
    return status;
}

// ---------------------------------------------------------------------------
// Dispatch table
// ---------------------------------------------------------------------------

static NTSTATUS ByovdCreateClose(PDEVICE_OBJECT, PIRP irp)
{
    irp->IoStatus.Status = STATUS_SUCCESS;
    irp->IoStatus.Information = 0;
    IoCompleteRequest(irp, IO_NO_INCREMENT);
    return STATUS_SUCCESS;
}

static NTSTATUS ByovdDeviceControl(PDEVICE_OBJECT, PIRP irp)
{
    IO_STACK_LOCATION* stack = IoGetCurrentIrpStackLocation(irp);
    ULONG ctl = stack->Parameters.DeviceIoControl.IoControlCode;
    ULONG inLen = stack->Parameters.DeviceIoControl.InputBufferLength;
    ULONG outLen = stack->Parameters.DeviceIoControl.OutputBufferLength;
    void* sysBuf = irp->AssociatedIrp.SystemBuffer;

    NTSTATUS status;
    switch (ctl) {
        case IOCTL_BYOVD_READ_PROC:
            status = HandleReadProc(irp, sysBuf, inLen, outLen);
            break;
        case IOCTL_BYOVD_WRITE_PROC:
            status = HandleWriteProc(irp, sysBuf, inLen, outLen);
            break;
        default:
            status = STATUS_INVALID_DEVICE_REQUEST;
            irp->IoStatus.Information = 0;
            break;
    }

    irp->IoStatus.Status = status;
    IoCompleteRequest(irp, IO_NO_INCREMENT);
    return status;
}

// ---------------------------------------------------------------------------
// Driver entry
// ---------------------------------------------------------------------------

static void ByovdUnload(PDRIVER_OBJECT drv)
{
    UNICODE_STRING sym = RTL_CONSTANT_STRING(SYMLINK_NAME);
    IoDeleteSymbolicLink(&sym);
    if (drv->DeviceObject)
        IoDeleteDevice(drv->DeviceObject);
}

extern "C" NTSTATUS DriverEntry(PDRIVER_OBJECT drv, PUNICODE_STRING)
{
    drv->DriverUnload = ByovdUnload;
    drv->MajorFunction[IRP_MJ_CREATE] = ByovdCreateClose;
    drv->MajorFunction[IRP_MJ_CLOSE] = ByovdCreateClose;
    drv->MajorFunction[IRP_MJ_DEVICE_CONTROL] = ByovdDeviceControl;

    UNICODE_STRING dev = RTL_CONSTANT_STRING(DEVICE_NAME);
    UNICODE_STRING sym = RTL_CONSTANT_STRING(SYMLINK_NAME);

    size_t ext = sizeof(BYOVD_DEVICE_EXTENSION);
    NTSTATUS status = IoCreateDevice(drv, ext, &dev, FILE_DEVICE_UNKNOWN, 0, FALSE, &drv->DeviceObject);
    if (!NT_SUCCESS(status)) return status;

    drv->DeviceObject->Flags &= ~DO_DEVICE_INITIALIZING;

    status = IoCreateSymbolicLink(&sym, &dev);
    if (!NT_SUCCESS(status)) {
        IoDeleteDevice(drv->DeviceObject);
        return status;
    }
    return STATUS_SUCCESS;
}