/** @file
  EFI_CPU2_PROTOCOL for the DXE core of Project Mu.

  At the mu_basecore commit this repository pins, the DXE core no longer signals
  gIdleLoopEventGuid when none of the events of a WaitForEvent() call is ready. It
  calls EFI_CPU2_PROTOCOL.EnableAndForWaitInterrupt() instead, and only if the
  protocol is there (MdeModulePkg/Core/Dxe/Event/Event.c,
  CoreWaitForEventInternal()). Nothing in the pinned mu_silicon_arm_tiano produces
  the protocol, and BaseLib has EnableInterruptsAndSleep() for IA32 and X64 only, so
  without this driver the core never sleeps: it goes round its loop until an event
  is signaled.

  Later versions of mu_basecore remove the protocol again and go back to the idle
  loop event, which ArmPkg/Drivers/CpuDxe answers with CpuSleep(). When the pins
  move there, this driver goes.

  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include <Uefi.h>

#include <Library/ArmLib.h>
#include <Library/CpuLib.h>
#include <Library/DebugLib.h>
#include <Library/UefiBootServicesTableLib.h>

#include <Protocol/Cpu2.h>

/**
  Wait for an interrupt, then enable interrupts.

  The DXE core calls this with interrupts disabled, after it has seen that no event
  is pending. An interrupt that arrives from then on stays pending, and a pending
  interrupt ends WFI whether or not it is masked (Arm DDI 0487, "Wait for
  Interrupt"), so none is slept through. It is taken once interrupts are enabled.

  @param  This  The EFI_CPU2_PROTOCOL instance.

  @retval EFI_SUCCESS  Interrupts are enabled.
**/
STATIC
EFI_STATUS
EFIAPI
Cpu2EnableAndWaitForInterrupt (
  IN EFI_CPU2_PROTOCOL  *This
  )
{
  CpuSleep ();
  ArmEnableInterrupts ();

  return EFI_SUCCESS;
}

STATIC EFI_CPU2_PROTOCOL  mCpu2 = {
  Cpu2EnableAndWaitForInterrupt
};

EFI_STATUS
EFIAPI
Cpu2DxeInitialize (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  EFI_STATUS  Status;
  EFI_HANDLE  Handle;

  Handle = NULL;
  Status = gBS->InstallMultipleProtocolInterfaces (
                  &Handle,
                  &gEfiCpu2ProtocolGuid,
                  &mCpu2,
                  NULL
                  );
  ASSERT_EFI_ERROR (Status);

  return Status;
}
