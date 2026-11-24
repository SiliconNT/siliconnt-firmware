/** @file
  Installs the ACPI tables SiliconNT presents to Windows: a machine with a
  standard GICv3, the virtual timer on INTID 27, PSCI over HVC.

  GTDT and FADT are the same on every machine (Gtdt.c, Fadt.c). MADT, DBG2 and
  DSDT describe the cores and the serial port of the machine the firmware runs
  on, which it reads from the Apple device tree, and are built here at boot
  (AcpiTableBuilder.c). The OEM revision of every table is the number of the
  chip, also from the device tree.

  There is a DBG2 only when there is a serial port (Dbg2Wanted()). Without one,
  when the device tree has no /arm-io/uart0 as when m1n1 runs the guest without
  a host, the other four tables are the same as with it.

  The DSDT also describes the internal disk controller (AnsNode.c), MTP and SMC
  (CoprocessorNode.c), each when the device tree and the table of interrupt numbers
  hold all its node needs. Otherwise the DSDT goes without that node and the log
  says why; the firmware boots on as before.

  All tables are built before the first is installed: a device tree that
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
#include <Library/PrintLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/AppleDTLib.h>

#include "AcpiTableBuilder.h"
#include "AnsNode.h"
#include "CoprocessorNode.h"
#include "StaticTables.h"

// Aff3, Aff2, Aff1 and Aff0 of MPIDR_EL1, which is what the MADT holds of it
#define MPIDR_AFFINITY_MASK  0xFF00FFFFFFULL

// MADT, GTDT, FADT, DBG2 and DSDT; DBG2 only with a serial port
#define MAX_TABLE_COUNT  5

// The disk controller, MTP and SMC
#define MAX_DEVICE_COUNT  3

// Tens of KiB each, too much for the stack
STATIC COPROCESSOR_NODE  mMtp;
STATIC COPROCESSOR_NODE  mSmc;

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
        "power manager 0x%lx+0x%lx, controller AIC %u is GIC %u, mailbox AIC %u is GIC %u, %a, SART version %u\n",
        DSDT_ANS_HID,
        Ans->Node.Nvme.Base,
        Ans->Node.Nvme.Size,
        Ans->Node.Coprocessor.Base,
        Ans->Node.Coprocessor.Size,
        Ans->Node.Sart.Base,
        Ans->Node.Sart.Size,
        Ans->Node.Pmgr.Base,
        Ans->Node.Pmgr.Size,
        Ans->Device.NvmeAic,
        Ans->Node.NvmeGsiv,
        Ans->Device.MailboxAic,
        Ans->Node.MailboxGsiv,
        Ans->Node.SecureBar ? "nvme-secure-bar" : "no nvme-secure-bar",
        Ans->Node.SartVersion
        ));
      break;
    case AnsNodeNoDevice:
      DEBUG ((DEBUG_INFO, "AcpiPlatformDxe: no disk controller node: /arm-io/ans or /arm-io/sart-ans in the device tree cannot be read: %r\n", Ans->Status));
      break;
    case AnsNodeNoSartVersion:
      DEBUG ((DEBUG_WARN, "AcpiPlatformDxe: no disk controller node: the version of its SART cannot be read from /arm-io/sart-ans: %r\n", Ans->Status));
      break;
    case AnsNodeNoPmgr:
      DEBUG ((DEBUG_WARN, "AcpiPlatformDxe: no disk controller node: its register in the power manager cannot be found in the device tree: %r\n", Ans->Status));
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

/**
  What is in a coprocessor's node, what was left out of it and why
**/
STATIC
VOID
PrintCoprocessorNodeMade (
  IN CONST CHAR8             *Name,
  IN CONST COPROCESSOR_NODE  *Node
  )
{
  CONST APPLE_DEVICE_RANGE  *Range;
  CONST APPLE_DEVICE_LINE   *Line;
  CONST SKIPPED_PROPERTY    *Skipped;
  CHAR8                     Entry[12];
  UINTN                     Index;

  DEBUG ((
    DEBUG_INFO,
    "AcpiPlatformDxe: %a, %a: %u memory ranges, %u interrupts, %u properties\n",
    Name,
    Node->Dsdt.Hid,
    (UINT32)Node->Dsdt.MemoryCount,
    (UINT32)Node->Dsdt.GsivCount,
    (UINT32)Node->Dsdt.PropertyCount
    ));

  for (Index = 0; Index < Node->Device.RangeCount; Index++) {
    Range    = &Node->Device.Ranges[Index];
    Entry[0] = 0;
    if (Range->Entry != APPLE_DEVICE_NO_ENTRY) {
      AsciiSPrint (Entry, sizeof (Entry), "-%u", Range->Entry);
    }

    if (Range->PowerManager) {
      DEBUG ((DEBUG_INFO, "AcpiPlatformDxe: %a without %a%a 0x%lx+0x%lx: the power manager has registers there\n", Name, Range->Block, Entry, Range->Range.Base, Range->Range.Size));
    } else {
      DEBUG ((DEBUG_INFO, "AcpiPlatformDxe: %a memory %a%a 0x%lx+0x%lx\n", Name, Range->Block, Entry, Range->Range.Base, Range->Range.Size));
    }
  }

  if (RETURN_ERROR (Node->Device.SramStatus)) {
    DEBUG ((DEBUG_WARN, "AcpiPlatformDxe: %a without its SRAM: where it is cannot be told from the device tree: %r\n", Name, Node->Device.SramStatus));
  }

  for (Index = 0; Index < Node->Device.LineCount; Index++) {
    Line = &Node->Device.Lines[Index];
    DEBUG ((DEBUG_INFO, "AcpiPlatformDxe: %a interrupt %a-%u AIC %u is GIC %u\n", Name, Line->Block, Line->Entry, Line->Aic, Node->Gsiv[Index]));
  }

  for (Index = 0; Index < MIN (Node->Properties.SkippedCount, DEVICE_MAX_SKIPPED); Index++) {
    Skipped = &Node->Properties.Skipped[Index];
    DEBUG ((
      DEBUG_WARN,
      "AcpiPlatformDxe: %a without \"%a\", %u bytes in the device tree: %a\n",
      Name,
      Skipped->Key,
      (UINT32)Skipped->Length,
      Skipped->Reason == PropertySkippedValue     ? "they are not a value of its type" :
      Skipped->Reason == PropertySkippedReference ? "the node it points to cannot be used" :
                                                    "there is no room for it"
      ));
  }

  if (Node->Properties.SkippedCount > DEVICE_MAX_SKIPPED) {
    DEBUG ((DEBUG_WARN, "AcpiPlatformDxe: %a: %u properties were left out, the first %u of them are listed\n", Name, (UINT32)Node->Properties.SkippedCount, DEVICE_MAX_SKIPPED));
  }
}

/**
  A coprocessor: its node, or why there is none
**/
STATIC
VOID
PrintCoprocessorNode (
  IN CONST CHAR8             *Name,
  IN CONST COPROCESSOR_NODE  *Node
  )
{
  CONST APPLE_DEVICE_LINE  *Line;

  switch (Node->Outcome) {
    case CoprocessorNodeMade:
      PrintCoprocessorNodeMade (Name, Node);
      break;
    case CoprocessorNodeNoDevice:
      if (Node->Device.FailedProperty == NULL) {
        DEBUG ((DEBUG_INFO, "AcpiPlatformDxe: no %a node: %a in the device tree cannot be read: %r\n", Name, Node->Device.FailedNode, Node->Status));
      } else {
        DEBUG ((DEBUG_WARN, "AcpiPlatformDxe: no %a node: \"%a\" of %a in the device tree cannot be used: %r\n", Name, Node->Device.FailedProperty, Node->Device.FailedNode, Node->Status));
      }

      break;
    case CoprocessorNodeNoTable:
      DEBUG ((DEBUG_INFO, "AcpiPlatformDxe: no %a node: no AIC-to-GIC table to use: %r\n", Name, Node->Status));
      break;
    case CoprocessorNodeLineMissing:
      Line = &Node->Device.Lines[Node->MissingLine];
      DEBUG ((DEBUG_WARN, "AcpiPlatformDxe: no %a node: the line %a-%u, AIC %u, is not in the AIC-to-GIC table\n", Name, Line->Block, Line->Entry, Line->Aic));
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
  BOOLEAN                  WithDbg2;
  VOID                     *Madt;
  VOID                     *Dbg2;
  VOID                     *Dsdt;
  VOID                     *Tables[MAX_TABLE_COUNT];
  UINTN                    TableCount;
  UINTN                    Index;
  UINTN                    TableKey;
  UINT64                   BootMpidr;
  ANS_NODE                 Ans;
  ANS_DEVICE               AnsDevice;
  CONST DSDT_DEVICE        *Devices[MAX_DEVICE_COUNT];
  UINTN                    DeviceCount;

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

  WithDbg2 = Dbg2Wanted (BootInfo->UartBase);
  if (WithDbg2) {
    DEBUG ((DEBUG_INFO, "AcpiPlatformDxe: chip 0x%x, %u cores, serial port at 0x%lx\n", ChipId, (UINT32)CpuCount, BootInfo->UartBase));
  } else {
    DEBUG ((DEBUG_INFO, "AcpiPlatformDxe: chip 0x%x, %u cores, no serial port: no DBG2\n", ChipId, (UINT32)CpuCount));
  }

  //
  // The devices, in the order the DSDT has them
  //
  DeviceCount = 0;

  FindAnsNode (BootInfo, &Ans);
  PrintAnsNode (&Ans);
  if (Ans.Outcome == AnsNodeMade) {
    DescribeAns (&Ans.Node, &AnsDevice);
    Devices[DeviceCount++] = &AnsDevice.Dsdt;
  }

  FindMtpNode (BootInfo, &mMtp);
  PrintCoprocessorNode ("MTP0", &mMtp);
  if (mMtp.Outcome == CoprocessorNodeMade) {
    Devices[DeviceCount++] = &mMtp.Dsdt;
  }

  FindSmcNode (BootInfo, &mSmc);
  PrintCoprocessorNode ("SMC0", &mSmc);
  if (mSmc.Outcome == CoprocessorNodeMade) {
    Devices[DeviceCount++] = &mSmc.Dsdt;
  }

  //
  // Build. Tables lists them in the order they are installed in.
  //
  CreatorRevision = FixedPcdGet32 (PcdAcpiDefaultCreatorRevision);

  Madt = AllocatePool (MadtSize (CpuCount));
  Dbg2 = WithDbg2 ? AllocatePool (Dbg2Size ()) : NULL;
  Dsdt = AllocatePool (DsdtSize (Cpus, CpuCount, Devices, DeviceCount));

  TableCount           = 0;
  Tables[TableCount++] = Madt;
  Tables[TableCount++] = CopyStaticTable (&gStaticGtdt.Header, ChipId);
  Tables[TableCount++] = CopyStaticTable (&gStaticFadt.Header, ChipId);
  if (WithDbg2) {
    Tables[TableCount++] = Dbg2;
  }

  Tables[TableCount++] = Dsdt;

  Status = EFI_SUCCESS;
  for (Index = 0; Index < TableCount; Index++) {
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
               Madt
               );
  }

  if (!EFI_ERROR (Status)) {
    if (WithDbg2) {
      Dbg2Build (ChipId, CreatorRevision, BootInfo->UartBase, Dbg2);
    }

    Status = DsdtBuild (ChipId, CreatorRevision, Cpus, CpuCount, Devices, DeviceCount, Dsdt);
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

  for (Index = 0; !EFI_ERROR (Status) && Index < TableCount; Index++) {
    Status = AcpiTable->InstallAcpiTable (
                          AcpiTable,
                          Tables[Index],
                          ((EFI_ACPI_DESCRIPTION_HEADER *)Tables[Index])->Length,
                          &TableKey
                          );
  }

  for (Index = 0; Index < TableCount; Index++) {
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
