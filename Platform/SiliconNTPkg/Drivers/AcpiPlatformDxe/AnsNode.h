/** @file
  Whether the DSDT describes the internal disk controller, and with what: from the
  device tree (AppleGetAns(), AppleGetAnsSartVersion(), AppleGetAnsPmgr()) and the
  table of interrupt numbers m1n1 hands over (AppleGetAicToGic(),
  AppleFindGicInterrupt()).

  The node is made only when all of it is there. Without the device, without the
  version of its SART, without its register in the power manager, without a
  usable table, or with a line the table does not hold, there is no node and the
  DSDT is the one without it; the outcome says which.

  What the node holds and why: SiliconNT docs/findings/0062.

  Nothing here calls a firmware service: AcpiPlatformDxe.c prints the outcome, and
  Test/AcpiTablesHostTest compiles this code for the host.

  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#ifndef ANS_NODE_H_
#define ANS_NODE_H_

#include "AcpiTableBuilder.h"

/**
  The internal disk controller as the DSDT describes it: four register ranges,
  in this order in _CRS, and two GIC interrupt numbers (GSIVs), controller's
  first. Its _DSD holds "nvme-secure-bar" when SecureBar is set, then
  "sart-version" with SartVersion.
**/
typedef struct {
  APPLE_MMIO_RANGE    Nvme;
  APPLE_MMIO_RANGE    Coprocessor;
  APPLE_MMIO_RANGE    Sart;
  APPLE_MMIO_RANGE    Pmgr;           // the controller's register in the power manager
  UINT32              NvmeGsiv;
  UINT32              MailboxGsiv;
  BOOLEAN             SecureBar;
  UINT32              SartVersion;
} DSDT_ANS;

//
// The hardware ID of the disk controller. SLNT is SiliconNT's own prefix and is
// not registered with the UEFI Forum; SiliconNT docs/findings/0062.
//
#define DSDT_ANS_HID  "SLNT0001"

typedef enum {
  AnsNodeMade,              // Node is the node
  AnsNodeNoDevice,          // AppleGetAns() failed with Status
  AnsNodeNoSartVersion,     // AppleGetAnsSartVersion() failed with Status
  AnsNodeNoPmgr,            // AppleGetAnsPmgr() failed with Status
  AnsNodeNoTable,           // AppleGetAicToGic() failed with Status
  AnsNodeNvmeLineMissing,   // the table does not hold Device.NvmeAic
  AnsNodeMailboxLineMissing // the table does not hold Device.MailboxAic
} ANS_NODE_OUTCOME;

typedef struct {
  ANS_NODE_OUTCOME    Outcome;
  RETURN_STATUS       Status;   // of the step that failed
  APPLE_ANS           Device;   // what the tree says: all 0 with AnsNodeNoDevice
  DSDT_ANS            Node;     // all 0 unless AnsNodeMade
} ANS_NODE;

VOID
FindAnsNode (
  IN  CONST APPLE_BOOT_INFO  *Info,
  OUT ANS_NODE               *Ans
  );

//
// The node as the DSDT takes it. Dsdt points into the rest, so the structure must
// not be copied.
//
typedef struct {
  APPLE_MMIO_RANGE    Memory[4];
  UINT32              Gsiv[2];
  DSDT_PROPERTY       Properties[2];
  DSDT_DEVICE         Dsdt;           // ANS0
} ANS_DEVICE;

VOID
DescribeAns (
  IN  CONST DSDT_ANS  *Ans,
  OUT ANS_DEVICE      *Device
  );

#endif // ANS_NODE_H_
