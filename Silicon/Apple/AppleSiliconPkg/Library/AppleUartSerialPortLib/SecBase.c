/** @file
  Where the serial port is, for the entry code. See AppleUartBase.h.

  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include <Library/AppleUartSerialPortLib.h>

#include "AppleUartBase.h"

STATIC UINT64  mUartBase;

VOID
AppleUartSerialPortSetBase (
  IN UINT64  Base
  )
{
  mUartBase = Base;
}

UINT64
AppleUartBase (
  VOID
  )
{
  return mUartBase;
}
