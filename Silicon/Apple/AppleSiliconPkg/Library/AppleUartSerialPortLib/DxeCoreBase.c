/** @file
  Where the serial port is, for the DXE core. See AppleUartBase.h.

  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include <PiDxe.h>
#include <Library/HobLib.h>
#include <Library/AppleDTLib.h>

#include "AppleUartBase.h"

STATIC UINT64   mUartBase;

//
// Whether mUartBase holds what the HOB says. Without a serial port the HOB says
// 0, and the core must not walk the HOB list again at every write.
//
STATIC BOOLEAN  mUartBaseRead;

UINT64
AppleUartBase (
  VOID
  )
{
  VOID  *Hob;

  if (!mUartBaseRead) {
    //
    // The HOB library of the DXE core needs no constructor: the entry point of
    // the core stores the HOB list before anything else runs.
    //
    Hob = GetFirstGuidHob (&gAppleSiliconPkgBootInfoHobGuid);
    if (Hob != NULL) {
      mUartBase     = ((APPLE_BOOT_INFO *)GET_GUID_HOB_DATA (Hob))->UartBase;
      mUartBaseRead = TRUE;
    }
  }

  return mUartBase;
}
