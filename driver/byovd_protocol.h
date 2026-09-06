/*
 * Shared protocol definition between seraph_drv.sys (kernel) and the
 * usermode BYOVD client (loader + cheat).
 *
 * Cross-process virtual memory via MmCopyVirtualMemory. The driver resolves
 * the target EPROCESS from PID, then copies to/from the caller's buffer.
 */
#pragma once

#include <cstdint>

// The CTL_CODE family (CTL_CODE, FILE_DEVICE_UNKNOWN, METHOD_BUFFERED,
// FILE_ANY_ACCESS) lives in <winioctl.h> for user mode and in <ntddk.h>/<wdm.h>
// for kernel mode. In kernel compiles ntddk.h is included first, so only pull
// the user-mode header when we are NOT in kernel mode.
#ifdef _KERNEL_MODE
#include <ntddk.h>
#else
#include <winioctl.h>
#endif

#define BYOVD_DEVICE_NAME  L"\\\\.\\byovd"

#define IOCTL_BYOVD_READ_PROC  CTL_CODE(FILE_DEVICE_UNKNOWN, 0x900, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_BYOVD_WRITE_PROC CTL_CODE(FILE_DEVICE_UNKNOWN, 0x901, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_BYOVD_READ_DIRECT CTL_CODE(FILE_DEVICE_UNKNOWN, 0x902, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_BYOVD_WRITE_DIRECT CTL_CODE(FILE_DEVICE_UNKNOWN, 0x903, METHOD_BUFFERED, FILE_ANY_ACCESS)

struct BYOVD_READ_PROC_REQUEST {
    uint32_t pid;
    uint32_t pad;
    uint64_t address;
    uint64_t size;
    uint8_t  buffer[];  // output: read data appended inline
};

struct BYOVD_WRITE_PROC_REQUEST {
    uint32_t pid;
    uint32_t pad;
    uint64_t address;
    uint64_t size;
    uint8_t  data[];    // input: data to write appended inline
};

// N.B. When METHOD_BUFFERED, the system buffer (OutputBufferLength) is the
// union of input+output. The driver sizes the buffer accordingly:
//   read:  sizeof(header) + size          (output fills the tail)
//   write: sizeof(header) + size          (input embedded in the tail)
