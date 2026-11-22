/*
 * Copyright (c) 2015, Linaro Ltd. All rights reserved.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#include <Uefi.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DebugAgentLib.h>
#include <Library/DebugLib.h>
#include <Library/AppleDTLib.h>
#include <Library/AppleUartSerialPortLib.h>

#define ALIGN_UP(x, a)   (((x) + ((a) - 1)) & ~((a) - 1))

UINT32 InitializeUART(VOID);

//
// SiliconNT: the boot arguments and the device tree are used where the loader
// put them; nothing is copied to a fixed address any more.
//
BOOLEAN
EarlySetup (
  IN VOID     *BootArgsAddr,
  OUT UINT64  *SystemMemoryBase,
  OUT UINT64  *SystemMemorySize
  )
{

  struct boot_args *BootArgs = (struct boot_args *)BootArgsAddr;
  APPLE_BOOT_INFO  Info;
  UINT64           UartBase;
  RETURN_STATUS    Status;

  Info.BootArgs       = BootArgs;
  Info.DeviceTree     = (dt_node_t *)((UINTN)BootArgs->devtree - BootArgs->virt_base + BootArgs->phys_base);
  Info.DeviceTreeSize = BootArgs->devtree_size;
  Info.UartBase       = 0;

  //
  // SiliconNT: the address of the serial port comes from the device tree, so
  // nothing can be printed before this point.
  //
  // A corrupt tree stops the entry code here without a word. Going on would take
  // the memory base and size from the build-time defaults, which describe no
  // real machine. So does a tree whose /arm-io/uart0 is there but gives no
  // address that can be used (AppleGetUartBase): that is a fault of the tree.
  //
  // A tree without /arm-io/uart0 is a machine without a serial port: m1n1 hands
  // over such a tree when no host is attached. The library is told no address,
  // so the firmware prints nothing at all, and boots on.
  //
  if(dt_check(Info.DeviceTree, Info.DeviceTreeSize, NULL) != 0) {
    CpuDeadLoop();
  }
  Status = AppleGetUartBase(&Info, &UartBase);
  if(RETURN_ERROR(Status) && Status != RETURN_NOT_FOUND) {
    CpuDeadLoop();
  }

  if(Status == RETURN_SUCCESS) {
    AppleUartSerialPortSetBase(UartBase);
    InitializeUART();
    DEBUG((EFI_D_INFO | EFI_D_LOAD, "Serial port at 0x%llx, from /arm-io/uart0 of the device tree\n", UartBase));
  }

  *SystemMemoryBase = BootArgs->phys_base;
  *SystemMemorySize = BootArgs->mem_size;


  PatchPcdSet64(PcdFrameBufferAddress, BootArgs->video.base);
  PatchPcdSet64(PcdFrameBufferSize, ALIGN_UP(BootArgs->video.stride * BootArgs->video.height, 0x4000)); // for notched/internal displays, the FB size has to be aligned up!

  DEBUG((EFI_D_INFO | EFI_D_LOAD | EFI_D_ERROR, "Framebuffer address loaded to PCDs: 0x%llx (size 0x%llx)\n", PcdGet64(PcdFrameBufferAddress), PcdGet64(PcdFrameBufferSize)));

  return TRUE;
}
