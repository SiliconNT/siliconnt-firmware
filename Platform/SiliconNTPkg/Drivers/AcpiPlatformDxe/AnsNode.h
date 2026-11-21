/** @file
  Whether the DSDT describes the internal disk controller, and with what: from the
  device tree (AppleGetAns()) and the table of interrupt numbers m1n1 hands over
  (AppleGetAicToGic(), AppleFindGicInterrupt()).

  The node is made only when all of it is there. Without the device, without a
  usable table, or with a line the table does not hold, there is no node and the
  DSDT is the one without it; the outcome says which.

  Nothing here calls a firmware service: AcpiPlatformDxe.c prints the outcome, and
  Test/AcpiTablesHostTest compiles this code for the host.

  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#ifndef ANS_NODE_H_
#define ANS_NODE_H_

#include "AcpiTableBuilder.h"

typedef enum {
  AnsNodeMade,              // Node is the node
  AnsNodeNoDevice,          // AppleGetAns() failed with Status
  AnsNodeNoTable,           // AppleGetAicToGic() failed with Status
  AnsNodeNvmeLineMissing,   // the table does not hold Device.NvmeAic
  AnsNodeMailboxLineMissing // the table does not hold Device.MailboxAic
} ANS_NODE_OUTCOME;

typedef struct {
  ANS_NODE_OUTCOME    Outcome;
  RETURN_STATUS       Status;   // of AppleGetAns() or AppleGetAicToGic()
  APPLE_ANS           Device;   // what the tree says: all 0 with AnsNodeNoDevice
  DSDT_ANS            Node;     // all 0 unless AnsNodeMade
} ANS_NODE;

VOID
FindAnsNode (
  IN  CONST APPLE_BOOT_INFO  *Info,
  OUT ANS_NODE               *Ans
  );

#endif // ANS_NODE_H_
