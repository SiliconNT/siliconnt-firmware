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
  A property of a device: Package (2) { Key, value } in its _DSD. Which fields hold
  the value depends on Kind:
    DsdtInteger  Integer.
    DsdtString   Strings is the string.
    DsdtPackage  A package of StringCount strings and after them WordCount integers.
                 The strings are at Strings one after the other, each ended by a
                 zero, the integers at Words, 32 bits each, little-endian, not
                 necessarily aligned.
  The strings, Key too, are of the characters 0x01-0x7F.
**/
typedef enum {
  DsdtInteger,
  DsdtString,
  DsdtPackage
} DSDT_VALUE_KIND;

typedef struct {
  CONST CHAR8        *Key;
  DSDT_VALUE_KIND    Kind;
  UINT32             Integer;
  CONST CHAR8        *Strings;
  UINTN              StringCount;
  CONST UINT8        *Words;
  UINTN              WordCount;
} DSDT_PROPERTY;

// The most elements of a package, and so the most properties of a device
#define DSDT_MAX_ELEMENTS  255

/**
  A device the DSDT describes: Name, four characters; its hardware ID; _UID 0; _CCA
  0, its DMA is not coherent; its register ranges and after them its GIC interrupt
  numbers (GSIVs) in _CRS, in this order; its properties in _DSD, in this order.
**/
typedef struct {
  CONST CHAR8               *Name;
  CONST CHAR8               *Hid;
  CONST APPLE_MMIO_RANGE    *Memory;
  UINTN                     MemoryCount;
  CONST UINT32              *Gsiv;
  UINTN                     GsivCount;
  CONST DSDT_PROPERTY       *Properties;
  UINTN                     PropertyCount;
} DSDT_DEVICE;

/**
  Differentiated System Description Table: a processor device for each core, in
  the order of Cpus, then the devices, in the order of Devices. The _UID of a
  processor device is the number of the core; its name is "CPU" and one character
  for the number, 0-9 then A-Z.

  @retval RETURN_SUCCESS            Table holds DsdtSize () bytes.
  @retval RETURN_INVALID_PARAMETER  A core number is 36 or above: there is no
                                    name for it. Or a register range of a device is
                                    empty or reaches beyond the address space, or a
                                    device has more than DSDT_MAX_ELEMENTS
                                    properties, or one of them more than that many
                                    elements. DsdtSize () is 0 then.
**/
UINTN
DsdtSize (
  IN CONST APPLE_CPU    *Cpus,
  IN UINTN              CpuCount,
  IN CONST DSDT_DEVICE  *CONST *Devices,
  IN UINTN              DeviceCount
  );

RETURN_STATUS
DsdtBuild (
  IN  UINT32             OemRevision,
  IN  UINT32             CreatorRevision,
  IN  CONST APPLE_CPU    *Cpus,
  IN  UINTN              CpuCount,
  IN  CONST DSDT_DEVICE  *CONST *Devices,
  IN  UINTN              DeviceCount,
  OUT VOID               *Table
  );

#endif // ACPI_TABLE_BUILDER_H_
