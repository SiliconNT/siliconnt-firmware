/** @file
  Where the serial port is, for DXE drivers and UEFI applications. See
  AppleUartBase.h.

  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include <PiDxe.h>
#include <Guid/HobList.h>
#include <Library/BaseMemoryLib.h>
#include <Library/HobLib.h>
#include <Library/AppleDTLib.h>

#include "AppleUartBase.h"

STATIC UINT64  mUartBase;

UINT64
AppleUartBase (
  VOID
  )
{
  return mUartBase;
}

/**
  Takes the address of the port from the boot information HOB.

  The HOB list is found in the configuration tables of the system table and
  walked here, with the macros of HobLib.h only. HobLib itself is not used: its
  constructor depends on DebugLib, which depends on this library, so it may not
  have run yet.

  @retval EFI_SUCCESS  Always: without the HOB the library writes nothing, which
                       must not keep the module from starting.
**/
EFI_STATUS
EFIAPI
AppleUartSerialPortLibConstructor (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  EFI_PEI_HOB_POINTERS  Hob;
  UINTN                 Index;

  Hob.Raw = NULL;
  for (Index = 0; Index < SystemTable->NumberOfTableEntries; Index++) {
    if (CompareGuid (&SystemTable->ConfigurationTable[Index].VendorGuid, &gEfiHobListGuid)) {
      Hob.Raw = SystemTable->ConfigurationTable[Index].VendorTable;
      break;
    }
  }

  if (Hob.Raw == NULL) {
    return EFI_SUCCESS;
  }

  for ( ; !END_OF_HOB_LIST (Hob); Hob.Raw = GET_NEXT_HOB (Hob)) {
    if ((GET_HOB_TYPE (Hob) == EFI_HOB_TYPE_GUID_EXTENSION) &&
        CompareGuid (&Hob.Guid->Name, &gAppleSiliconPkgBootInfoHobGuid))
    {
      mUartBase = ((APPLE_BOOT_INFO *)GET_GUID_HOB_DATA (Hob.Guid))->UartBase;
      break;
    }
  }

  return EFI_SUCCESS;
}
