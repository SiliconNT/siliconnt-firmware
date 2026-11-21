/** @file
  Installs the ACPI tables SiliconNT presents to Windows: a machine with a
  standard GICv3, the virtual timer on INTID 27, PSCI over HVC.

  GTDT and FADT are the same on every machine (Gtdt.c, Fadt.c). MADT, DBG2 and
  DSDT describe the cores and the serial port of the machine the firmware runs
  on, which it reads from the Apple device tree, and are built here at boot
  (AcpiTableBuilder.c). The OEM revision of every table is the number of the
  chip, also from the device tree.

  The DSDT also describes the internal disk controller when the device tree and
  the table of interrupt numbers hold all a node needs (AnsNode.c). Otherwise the
  DSDT goes without it and the log says why; the firmware boots on as before.

  All five tables are built before the first is installed: a device tree that
  cannot be used leaves the machine without ACPI tables and with the reason in
  the log, not with half of them.

  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include <PiDxe.h>

#include <Protocol/AcpiTable.h>

#include <Library/ArmLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/PcdLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/AppleDTLib.h>

#include "AcpiTableBuilder.h"
#include "AnsNode.h"
#include "StaticTables.h"

// Aff3, Aff2, Aff1 and Aff0 of MPIDR_EL1, which is what the MADT holds of it
#define MPIDR_AFFINITY_MASK  0xFF00FFFFFFULL

#define TABLE_COUNT  5

/**
  A copy of a table that is the same on every machine, with the number of the
  chip as its OEM revision.
**/
STATIC
VOID *
CopyStaticTable (
  IN CONST EFI_ACPI_DESCRIPTION_HEADER  *Table,
  IN UINT32                             ChipId
  )
{
  EFI_ACPI_DESCRIPTION_HEADER  *Copy;

  Copy = AllocateCopyPool (Table->Length, Table);
  if (Copy != NULL) {
    Copy->OemRevision = ChipId;
  }

  return Copy;
}

/**
  One line about the disk controller: the node, or why there is none.
**/
STATIC
VOID
PrintAnsNode (
  IN CONST ANS_NODE  *Ans
  )
{
  switch (Ans->Outcome) {
    case AnsNodeMade:
      DEBUG ((
        DEBUG_INFO,
        "AcpiPlatformDxe: disk controller ANS0, %a: NVMe 0x%lx+0x%lx, coprocessor 0x%lx+0x%lx, SART 0x%lx+0x%lx, "
        "controller AIC %u is GIC %u, mailbox AIC %u is GIC %u, %a\n",
        DSDT_ANS_HID,
        Ans->Node.Nvme.Base,
        Ans->Node.Nvme.Size,
        Ans->Node.Coprocessor.Base,
        Ans->Node.Coprocessor.Size,
        Ans->Node.Sart.Base,
        Ans->Node.Sart.Size,
        Ans->Device.NvmeAic,
        Ans->Node.NvmeGsiv,
        Ans->Device.MailboxAic,
        Ans->Node.MailboxGsiv,
        Ans->Node.SecureBar ? "nvme-secure-bar" : "no nvme-secure-bar"
        ));
      break;
    case AnsNodeNoDevice:
      DEBUG ((DEBUG_INFO, "AcpiPlatformDxe: no disk controller node: /arm-io/ans or /arm-io/sart-ans in the device tree cannot be read: %r\n", Ans->Status));
      break;
    case AnsNodeNoTable:
      DEBUG ((DEBUG_INFO, "AcpiPlatformDxe: no disk controller node: no AIC-to-GIC table to use: %r\n", Ans->Status));
      break;
    case AnsNodeNvmeLineMissing:
      DEBUG ((DEBUG_WARN, "AcpiPlatformDxe: no disk controller node: the controller's line, AIC %u, is not in the AIC-to-GIC table\n", Ans->Device.NvmeAic));
      break;
    case AnsNodeMailboxLineMissing:
      DEBUG ((DEBUG_WARN, "AcpiPlatformDxe: no disk controller node: the mailbox line, AIC %u, is not in the AIC-to-GIC table\n", Ans->Device.MailboxAic));
      break;
  }
}

EFI_STATUS
EFIAPI
AcpiPlatformEntryPoint (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  EFI_STATUS               Status;
  EFI_ACPI_TABLE_PROTOCOL  *AcpiTable;
  APPLE_BOOT_INFO          *BootInfo;
  APPLE_CPU                Cpus[APPLE_MAX_CPUS];
  UINTN                    CpuCount;
  UINT32                   ChipId;
  UINT32                   CreatorRevision;
  VOID                     *Tables[TABLE_COUNT];
  UINTN                    Index;
  UINTN                    TableKey;
  UINT64                   BootMpidr;
  ANS_NODE                 Ans;
  CONST DSDT_ANS           *AnsInDsdt;

  //
  // What the device tree says about the machine
  //
  BootInfo = AppleGetBootInfo ();

  Status = AppleGetChipId (BootInfo, &ChipId);
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "AcpiPlatformDxe: cannot read \"chip-id\" of /chosen in the device tree: %r. No ACPI tables.\n", Status));
    return EFI_DEVICE_ERROR;
  }

  Status = AppleGetCpus (BootInfo, Cpus, &CpuCount);
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "AcpiPlatformDxe: cannot read the cores from /cpus in the device tree: %r. No ACPI tables.\n", Status));
    return EFI_DEVICE_ERROR;
  }

  //
  // MPIDR_EL1 of a core is not in the device tree; AppleGetCpus() puts it
  // together from what is. The one core that can be asked is the one this code
  // runs on. If the two disagree, the rule is wrong for this machine and so is
  // every MPIDR of the MADT: Windows would not find its cores.
  //
  BootMpidr = ArmReadMpidr () & MPIDR_AFFINITY_MASK;
  for (Index = 0; Index < CpuCount; Index++) {
    DEBUG ((
      DEBUG_INFO,
      "AcpiPlatformDxe: core %u, MPIDR 0x%lx, %a core%a\n",
      Cpus[Index].Number,
      Cpus[Index].Mpidr,
      Cpus[Index].Class == AppleCpuPerformance ? "performance" : "efficiency",
      Cpus[Index].Boot ? ", boot core" : ""
      ));
    if (Cpus[Index].Boot && (Cpus[Index].Mpidr != BootMpidr)) {
      DEBUG ((
        DEBUG_ERROR,
        "AcpiPlatformDxe: the device tree gives MPIDR 0x%lx for the boot core %u, the core itself says 0x%lx. No ACPI tables.\n",
        Cpus[Index].Mpidr,
        Cpus[Index].Number,
        BootMpidr
        ));
      return EFI_DEVICE_ERROR;
    }
  }

  DEBUG ((DEBUG_INFO, "AcpiPlatformDxe: chip 0x%x, %u cores, serial port at 0x%lx\n", ChipId, (UINT32)CpuCount, BootInfo->UartBase));

  FindAnsNode (BootInfo, &Ans);
  PrintAnsNode (&Ans);
  AnsInDsdt = Ans.Outcome == AnsNodeMade ? &Ans.Node : NULL;

  //
  // Build
  //
  CreatorRevision = FixedPcdGet32 (PcdAcpiDefaultCreatorRevision);

  Tables[0] = AllocatePool (MadtSize (CpuCount));
  Tables[1] = CopyStaticTable (&gStaticGtdt.Header, ChipId);
  Tables[2] = CopyStaticTable (&gStaticFadt.Header, ChipId);
  Tables[3] = AllocatePool (Dbg2Size ());
  Tables[4] = AllocatePool (DsdtSize (Cpus, CpuCount, AnsInDsdt));

  Status = EFI_SUCCESS;
  for (Index = 0; Index < TABLE_COUNT; Index++) {
    if (Tables[Index] == NULL) {
      Status = EFI_OUT_OF_RESOURCES;
    }
  }

  if (!EFI_ERROR (Status)) {
    Status = MadtBuild (
               ChipId,
               CreatorRevision,
               Cpus,
               CpuCount,
               FixedPcdGet64 (PcdGicDistributorBase),
               FixedPcdGet64 (PcdGicRedistributorsBase),
               Tables[0]
               );
  }

  if (!EFI_ERROR (Status)) {
    Dbg2Build (ChipId, CreatorRevision, BootInfo->UartBase, Tables[3]);
    Status = DsdtBuild (ChipId, CreatorRevision, Cpus, CpuCount, AnsInDsdt, Tables[4]);
  }

  //
  // Install. Tables other modules create later (the NFIT of the RAM disk driver)
  // take their OEM revision from this PCD.
  //
  if (!EFI_ERROR (Status)) {
    Status = PcdSet32S (PcdAcpiDefaultOemRevision, ChipId);
  }

  if (!EFI_ERROR (Status)) {
    Status = gBS->LocateProtocol (&gEfiAcpiTableProtocolGuid, NULL, (VOID **)&AcpiTable);
  }

  for (Index = 0; !EFI_ERROR (Status) && Index < TABLE_COUNT; Index++) {
    Status = AcpiTable->InstallAcpiTable (
                          AcpiTable,
                          Tables[Index],
                          ((EFI_ACPI_DESCRIPTION_HEADER *)Tables[Index])->Length,
                          &TableKey
                          );
  }

  for (Index = 0; Index < TABLE_COUNT; Index++) {
    if (Tables[Index] != NULL) {
      FreePool (Tables[Index]);
    }
  }

  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "AcpiPlatformDxe: cannot build or install the ACPI tables: %r\n", Status));
    return Status;
  }

  //
  // The driver does not require to be kept loaded.
  //
  return EFI_REQUEST_UNLOAD_IMAGE;
}
