/** @file
  Where the serial port is, for the DXE core. See AppleUartBase.h.

  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include <PiDxe.h>
#include <Library/HobLib.h>
#include <Library/AppleDTLib.h>

#include "AppleUartBase.h"

STATIC UINT64  mUartBase;

UINT64
AppleUartBase (
  VOID
  )
{
  VOID  *Hob;

  if (mUartBase == 0) {
    //
    // The HOB library of the DXE core needs no constructor: the entry point of
    // the core stores the HOB list before anything else runs.
    //
    Hob = GetFirstGuidHob (&gAppleSiliconPkgBootInfoHobGuid);
    if (Hob != NULL) {
      mUartBase = ((APPLE_BOOT_INFO *)GET_GUID_HOB_DATA (Hob))->UartBase;
    }
  }

  return mUartBase;
}
