/** @file
  Whether the DSDT describes the internal disk controller. See AnsNode.h.

  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include "AnsNode.h"

#include <Library/BaseMemoryLib.h>

VOID
FindAnsNode (
  IN  CONST APPLE_BOOT_INFO  *Info,
  OUT ANS_NODE               *Ans
  )
{
  APPLE_AIC_TO_GIC  Table;
  UINT32            NvmeGsiv;
  UINT32            MailboxGsiv;

  ZeroMem (Ans, sizeof (*Ans));

  Ans->Status = AppleGetAns (Info, &Ans->Device);
  if (RETURN_ERROR (Ans->Status)) {
    Ans->Outcome = AnsNodeNoDevice;
    return;
  }

  Ans->Status = AppleGetAicToGic (Info, &Table);
  if (RETURN_ERROR (Ans->Status)) {
    Ans->Outcome = AnsNodeNoTable;
    return;
  }

  if (RETURN_ERROR (AppleFindGicInterrupt (&Table, Ans->Device.NvmeAic, &NvmeGsiv))) {
    Ans->Outcome = AnsNodeNvmeLineMissing;
    return;
  }

  if (RETURN_ERROR (AppleFindGicInterrupt (&Table, Ans->Device.MailboxAic, &MailboxGsiv))) {
    Ans->Outcome = AnsNodeMailboxLineMissing;
    return;
  }

  Ans->Outcome          = AnsNodeMade;
  Ans->Node.Nvme        = Ans->Device.Nvme;
  Ans->Node.Coprocessor = Ans->Device.Coprocessor;
  Ans->Node.Sart        = Ans->Device.Sart;
  Ans->Node.NvmeGsiv    = NvmeGsiv;
  Ans->Node.MailboxGsiv = MailboxGsiv;
  Ans->Node.SecureBar   = Ans->Device.SecureBar;
}
