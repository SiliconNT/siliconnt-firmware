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
  Differentiated System Description Table: a processor device for each core and
  nothing else, in the order of Cpus. The _UID of a device is the number of the
  core; its name is "CPU" and one character for the number, 0-9 then A-Z.

  @retval RETURN_SUCCESS            Table holds DsdtSize (Cpus, CpuCount) bytes.
  @retval RETURN_INVALID_PARAMETER  A core number is 36 or above: there is no
                                    name for it.
**/
UINTN
DsdtSize (
  IN CONST APPLE_CPU  *Cpus,
  IN UINTN            CpuCount
  );

RETURN_STATUS
DsdtBuild (
  IN  UINT32                     OemRevision,
  IN  UINT32                     CreatorRevision,
  IN  CONST APPLE_CPU            *Cpus,
  IN  UINTN                      CpuCount,
  OUT VOID                       *Table
  );

#endif // ACPI_TABLE_BUILDER_H_
