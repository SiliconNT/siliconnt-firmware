/** @file
  Builds the ACPI tables whose contents depend on the machine: MADT, DBG2 and DSDT.

  Nothing here calls a firmware service: each function turns its arguments into
  the bytes of one table, in a buffer of the caller. Test/AcpiTablesHostTest
  compiles this code for the host.

  Every function takes the two header fields that are not the same on every
  machine and build: the OEM revision, which is the number of the chip, and the
  creator revision. The checksum of a table is left 0: the ACPI table protocol
  fills it in when the table is installed.

  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#ifndef ACPI_TABLE_BUILDER_H_
#define ACPI_TABLE_BUILDER_H_

#include <Base.h>
#include <IndustryStandard/Acpi64.h>
#include <Library/AppleDTLib.h>

/**
  Multiple APIC Description Table: one GICC structure per core, the Distributor,
  one Redistributor discovery range, which reaches from RedistributorsBase to the
  Redistributor of the core with the highest number (EmulatedGic.h).

  The cores are listed in the order of their numbers, starting with the boot core
  and wrapping around: ACPI wants the boot core first. The ACPI processor UID of a
  core is its number.

  @retval RETURN_SUCCESS            Table holds MadtSize (CpuCount) bytes.
  @retval RETURN_INVALID_PARAMETER  There are no cores, more than APPLE_MAX_CPUS,
                                    or not exactly one of them is the boot core.
**/
UINTN
MadtSize (
  IN UINTN  CpuCount
  );

RETURN_STATUS
MadtBuild (
  IN  UINT32                     OemRevision,
  IN  UINT32                     CreatorRevision,
  IN  CONST APPLE_CPU            *Cpus,
  IN  UINTN                      CpuCount,
  IN  UINT64                     DistributorBase,
  IN  UINT64                     RedistributorsBase,
  OUT VOID                       *Table
  );

/**
  Whether the firmware installs a DBG2: only when there is a serial port, at
  UartBase. UartBase is 0 when the device tree has no /arm-io/uart0, as when m1n1
  runs the guest without a host. Then there is no port for the Windows kernel
  debugger; without a DBG2, Windows' loader finds no debug device, loads kd.dll
  instead and touches no device (SiliconNT docs/findings/0068, conclusion 6).
**/
BOOLEAN
Dbg2Wanted (
  IN UINT64  UartBase
  );

/**
  Debug Port Table 2: the serial port at UartBase, the one the EL2 layer emulates,
  for the Windows kernel debugger.
**/
UINTN
Dbg2Size (
  VOID
  );

VOID
Dbg2Build (
  IN  UINT32                     OemRevision,
  IN  UINT32                     CreatorRevision,
  IN  UINT64                     UartBase,
  OUT VOID                       *Table
  );

/**
  The internal disk controller as the DSDT describes it: four register ranges,
  in this order in _CRS, and two GIC interrupt numbers (GSIVs), controller's
  first. Its _DSD holds "nvme-secure-bar" when SecureBar is set, then
  "sart-version" with SartVersion. AnsNode.h says where the values come from.
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

/**
  Differentiated System Description Table: a processor device for each core, in
  the order of Cpus, then the disk controller if Ans is not NULL. The _UID of a
  processor device is the number of the core; its name is "CPU" and one character
  for the number, 0-9 then A-Z. The disk controller is ANS0, _HID DSDT_ANS_HID.

  With Ans NULL the table is the one the firmware built before it knew of the
  disk controller, byte for byte.

  @retval RETURN_SUCCESS            Table holds DsdtSize (Cpus, CpuCount, Ans) bytes.
  @retval RETURN_INVALID_PARAMETER  A core number is 36 or above: there is no
                                    name for it. Or a register range of Ans is
                                    empty or reaches beyond the address space.
**/
UINTN
DsdtSize (
  IN CONST APPLE_CPU  *Cpus,
  IN UINTN            CpuCount,
  IN CONST DSDT_ANS   *Ans      OPTIONAL
  );

RETURN_STATUS
DsdtBuild (
  IN  UINT32                     OemRevision,
  IN  UINT32                     CreatorRevision,
  IN  CONST APPLE_CPU            *Cpus,
  IN  UINTN                      CpuCount,
  IN  CONST DSDT_ANS             *Ans      OPTIONAL,
  OUT VOID                       *Table
  );

#endif // ACPI_TABLE_BUILDER_H_
