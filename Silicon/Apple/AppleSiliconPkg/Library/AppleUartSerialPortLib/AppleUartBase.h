/** @file
  Where the serial port is.

  The address is read from the device tree once, by the entry code (PrePi), which
  hands it on in the boot information HOB (APPLE_BOOT_INFO, AppleDTLib.h). How a
  module gets at it depends on what the module is, so the library has three
  flavours, one file each:

    SecBase.c      the entry code itself: it tells the library the address
                   (AppleUartSerialPortSetBase) as soon as it has read it; there
                   are no HOBs yet at that point.
    DxeCoreBase.c  the DXE core: it prints before it runs library constructors,
                   and it is handed the HOB list at its entry; the address is
                   looked up in the HOB the first time it is needed.
    DxeBase.c      every later module: a constructor takes the address from the
                   HOB list in the system table. It does not use HobLib, whose
                   own constructor may run later than this one.

  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#ifndef APPLE_UART_BASE_H_
#define APPLE_UART_BASE_H_

#include <Base.h>

/**
  The address of the serial port, or 0 while this module does not know it and
  when there is none.
**/
UINT64
AppleUartBase (
  VOID
  );

#endif // APPLE_UART_BASE_H_
