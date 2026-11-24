/** @file
  Whether the DSDT describes MTP, the coprocessor of the keyboard and the trackpad,
  and SMC, the system management controller, and with what: from the device tree
  (AppleGetMtp(), AppleGetSmc(), DeviceProperties.h) and the table of interrupt
  numbers m1n1 hands over (AppleGetAicToGic(), AppleFindGicInterrupt()).

  A node describes the whole device, not what some driver uses of it: all register
  ranges, the SRAM and all interrupt lines that belong to it alone, and every
  property of the tree that describes hardware. "memory-names" and "interrupt-names"
  in its _DSD name what is in _CRS, in the same order: a driver looks for a name,
  not for a position. What is in and what is not, and why: SiliconNT
  docs/findings/0095.

  The node is made only when every register range and every line can be described.
  Otherwise there is no node, the outcome says why, and the rest of the DSDT is as
  without it; one of the two nodes does not depend on the other. A range the power
  manager shares, an SRAM that cannot be found and a property whose value does not
  fit are left out of the node and do not prevent it.

  Nothing here calls a firmware service: AcpiPlatformDxe.c prints the outcome, and
  Test/AcpiTablesHostTest compiles this code for the host.

  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#ifndef COPROCESSOR_NODE_H_
#define COPROCESSOR_NODE_H_

#include "AcpiTableBuilder.h"
#include "DeviceProperties.h"

//
// The hardware IDs. SLNT is SiliconNT's own prefix and is not registered with the
// UEFI Forum; SiliconNT docs/findings/0062.
//
#define DSDT_MTP_HID  "SLNT0002"
#define DSDT_SMC_HID  "SLNT0003"

typedef enum {
  CoprocessorNodeMade,          // Dsdt is the node
  CoprocessorNodeNoDevice,      // AppleGetMtp() or AppleGetSmc() failed with Status; Device says at what
  CoprocessorNodeNoTable,       // AppleGetAicToGic() failed with Status
  CoprocessorNodeLineMissing    // the table does not hold Device.Lines[MissingLine]
} COPROCESSOR_NODE_OUTCOME;

//
// Dsdt points into the rest and into the tree, so the structure must not be copied
//
typedef struct {
  COPROCESSOR_NODE_OUTCOME    Outcome;
  RETURN_STATUS               Status;       // of the step that failed
  APPLE_DEVICE                Device;       // what the tree says
  UINTN                       MissingLine;
  APPLE_MMIO_RANGE            Memory[APPLE_DEVICE_MAX_RANGES];
  UINT32                      Gsiv[APPLE_DEVICE_MAX_LINES];   // of Device.Lines
  DEVICE_PROPERTIES           Properties;
  DSDT_DEVICE                 Dsdt;         // all 0 unless CoprocessorNodeMade
} COPROCESSOR_NODE;

// MTP0, SLNT0002
VOID
FindMtpNode (
  IN  CONST APPLE_BOOT_INFO  *Info,
  OUT COPROCESSOR_NODE       *Node
  );

// SMC0, SLNT0003
VOID
FindSmcNode (
  IN  CONST APPLE_BOOT_INFO  *Info,
  OUT COPROCESSOR_NODE       *Node
  );

#endif // COPROCESSOR_NODE_H_
