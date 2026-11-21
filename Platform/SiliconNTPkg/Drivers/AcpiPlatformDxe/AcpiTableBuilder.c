/** @file
  Builds the ACPI tables whose contents depend on the machine: MADT, DBG2 and DSDT.
  See AcpiTableBuilder.h.

  Copyright (c) 2023, amarioguy (AppleWOA authors).

  SiliconNT: the layouts of the MADT and of the DBG2 are the ones of the static
  tables this file replaces, which derived from NT-for-ASi/apple_silicon_platforms_mu
  (861dcc490c56), Silicon/Apple/T602XFamilyPkg/AcpiTables/MADT_Static.aslc and
  Platform/MacBookProEarly2023Pkg/AcpiTables/DBG2.aslc.

  What is in them and why (SiliconNT docs):
    - MADT: Parked Address 0, GICD version 3, no ITS, the Redistributors as one
      discovery range: docs/DESIGN.md. The boot core comes first (ACPI 6.6, GICC
      structure).
    - DBG2: every field follows the table in docs/findings/0010: one device, port
      type serial, subtype 0x000A (Samsung style, the port m1n1 emulates), one
      memory address register (32 bits wide, 32-bit access), address length
      0x1000, a non-empty zero-terminated namespace string. The DSDT does not
      describe this port, so the string is ".".
    - DSDT: the processors and nothing else: docs/DESIGN.md.

  SPDX-License-Identifier: BSD-2-Clause-Patent OR MIT
**/

#include "AcpiTableBuilder.h"

#include <Acpi/AcpiHeader.h>
#include <EmulatedGic.h>
#include <IndustryStandard/DebugPort2Table.h>
#include <Library/AcpiLib.h>
#include <Library/BaseMemoryLib.h>

//
// The OEM ID of the tables is the one the static tables this file replaces had.
// The ASL compiler built the DSDT, and its OEM ID has a space in the sixth place.
//
STATIC CONST UINT8  mOemId[6]     = EFI_ACPI_OEM_ID;
STATIC CONST UINT8  mDsdtOemId[6] = { 'A', 'p', 'p', 'l', 'e', ' ' };

STATIC
VOID
FillHeader (
  OUT EFI_ACPI_DESCRIPTION_HEADER  *Header,
  IN  UINT32                       Signature,
  IN  UINTN                        Length,
  IN  UINT8                        Revision,
  IN  CONST UINT8                  *OemId,
  IN  UINT32                       OemRevision,
  IN  UINT32                       CreatorRevision
  )
{
  Header->Signature = Signature;
  Header->Length    = (UINT32)Length;
  Header->Revision  = Revision;
  Header->Checksum  = 0;
  CopyMem (Header->OemId, OemId, sizeof (Header->OemId));
  Header->OemTableId      = EFI_ACPI_OEM_TABLE_ID;
  Header->OemRevision     = OemRevision;
  Header->CreatorId       = EFI_ACPI_CREATOR_ID;
  Header->CreatorRevision = CreatorRevision;
}

//
// MADT
//

UINTN
MadtSize (
  IN UINTN  CpuCount
  )
{
  return sizeof (EFI_ACPI_6_4_MULTIPLE_APIC_DESCRIPTION_TABLE_HEADER) +
         CpuCount * sizeof (EFI_ACPI_6_4_GIC_STRUCTURE) +
         sizeof (EFI_ACPI_6_4_GIC_DISTRIBUTOR_STRUCTURE) +
         sizeof (EFI_ACPI_6_4_GICR_STRUCTURE);
}

RETURN_STATUS
MadtBuild (
  IN  UINT32                     OemRevision,
  IN  UINT32                     CreatorRevision,
  IN  CONST APPLE_CPU            *Cpus,
  IN  UINTN                      CpuCount,
  IN  UINT64                     DistributorBase,
  IN  UINT64                     RedistributorsBase,
  OUT VOID                       *Table
  )
{
  EFI_ACPI_6_4_MULTIPLE_APIC_DESCRIPTION_TABLE_HEADER  *Header;
  EFI_ACPI_6_4_GIC_STRUCTURE                           *Gicc;
  EFI_ACPI_6_4_GIC_DISTRIBUTOR_STRUCTURE               *Gicd;
  EFI_ACPI_6_4_GICR_STRUCTURE                          *Gicr;
  UINTN                                                Order[APPLE_MAX_CPUS];
  UINTN                                                BootCores = 0;
  UINT32                                               BootNumber = 0;
  UINTN                                                Index;
  UINTN                                                Other;
  UINTN                                                Swap;

  if ((CpuCount == 0) || (CpuCount > APPLE_MAX_CPUS)) {
    return RETURN_INVALID_PARAMETER;
  }

  for (Index = 0; Index < CpuCount; Index++) {
    Order[Index] = Index;
    if (Cpus[Index].Boot) {
      BootCores++;
      BootNumber = Cpus[Index].Number;
    }
  }

  if (BootCores != 1) {
    return RETURN_INVALID_PARAMETER;
  }

  //
  // Sort by "how far above the boot core is the number", in unsigned arithmetic:
  // the boot core is 0 away, the numbers below it come after the numbers above it.
  //
  for (Index = 0; Index + 1 < CpuCount; Index++) {
    for (Other = Index + 1; Other < CpuCount; Other++) {
      if ((UINT32)(Cpus[Order[Other]].Number - BootNumber) < (UINT32)(Cpus[Order[Index]].Number - BootNumber)) {
        Swap         = Order[Index];
        Order[Index] = Order[Other];
        Order[Other] = Swap;
      }
    }
  }

  ZeroMem (Table, MadtSize (CpuCount));

  Header = Table;
  FillHeader (
    &Header->Header,
    EFI_ACPI_6_4_MULTIPLE_APIC_DESCRIPTION_TABLE_SIGNATURE,
    MadtSize (CpuCount),
    EFI_ACPI_6_4_MULTIPLE_APIC_DESCRIPTION_TABLE_REVISION,
    mOemId,
    OemRevision,
    CreatorRevision
    );
  Header->LocalApicAddress = 0;
  Header->Flags            = 0;

  Gicc = (VOID *)(Header + 1);
  for (Index = 0; Index < CpuCount; Index++) {
    CONST APPLE_CPU             *Cpu = &Cpus[Order[Index]];
    EFI_ACPI_6_4_GIC_STRUCTURE  Entry = EFI_ACPI_6_3_GICC_STRUCTURE_INIT (
                                          Cpu->Number,
                                          Cpu->Number,
                                          Cpu->Mpidr,
                                          EFI_ACPI_6_4_GIC_ENABLED,
                                          0 /* PmuIrq */,
                                          0 /* GicBase */,
                                          0 /* GicVBase */,
                                          0 /* GicHBase */,
                                          0 /* GsivId */,
                                          0 /* GicRBase */,
                                          Cpu->Class == AppleCpuPerformance ? 1 : 0 /* Efficiency */,
                                          0 /* SpeOvflIrq */
                                          );

    CopyMem (&Gicc[Index], &Entry, sizeof (Entry));
  }

  Gicd = (VOID *)&Gicc[CpuCount];
  {
    EFI_ACPI_6_4_GIC_DISTRIBUTOR_STRUCTURE  Entry = EFI_ACPI_6_0_GIC_DISTRIBUTOR_INIT (0, DistributorBase, 0, 3);

    CopyMem (Gicd, &Entry, sizeof (Entry));
  }

  Gicr                            = (VOID *)(Gicd + 1);
  Gicr->Type                      = EFI_ACPI_6_4_GICR;
  Gicr->Length                    = sizeof (EFI_ACPI_6_4_GICR_STRUCTURE);
  Gicr->Reserved                  = EFI_ACPI_RESERVED_WORD;
  Gicr->DiscoveryRangeBaseAddress = RedistributorsBase;
  Gicr->DiscoveryRangeLength      = (UINT32)(EMULATED_GIC_REDISTRIBUTOR_SIZE * EmulatedGicRedistributorCount (Cpus, CpuCount));

  return RETURN_SUCCESS;
}

//
// DBG2
//

#define DBG2_NAMESPACESTRING_FIELD_SIZE  2
#define DBG2_UART_ADDRESS_LENGTH         0x1000
#define DBG2_SUBTYPE_SAMSUNG             0x000A

#pragma pack (1)

typedef struct {
  EFI_ACPI_DBG2_DEBUG_DEVICE_INFORMATION_STRUCT    Dbg2Device;
  EFI_ACPI_6_4_GENERIC_ADDRESS_STRUCTURE           BaseAddressRegister;
  UINT32                                           AddressSize;
  UINT8                                            NameSpaceString[DBG2_NAMESPACESTRING_FIELD_SIZE];
} DBG2_DEBUG_DEVICE_INFORMATION;

typedef struct {
  EFI_ACPI_DEBUG_PORT_2_DESCRIPTION_TABLE    Description;
  DBG2_DEBUG_DEVICE_INFORMATION              Dbg2DeviceInfo;
} DBG2_TABLE;

#pragma pack ()

UINTN
Dbg2Size (
  VOID
  )
{
  return sizeof (DBG2_TABLE);
}

VOID
Dbg2Build (
  IN  UINT32                     OemRevision,
  IN  UINT32                     CreatorRevision,
  IN  UINT64                     UartBase,
  OUT VOID                       *Table
  )
{
  DBG2_TABLE                                     *Dbg2 = Table;
  EFI_ACPI_DBG2_DEBUG_DEVICE_INFORMATION_STRUCT  *Device;
  EFI_ACPI_6_4_GENERIC_ADDRESS_STRUCTURE         Register = ARM_GAS32 (UartBase);

  ZeroMem (Dbg2, sizeof (*Dbg2));

  FillHeader (
    &Dbg2->Description.Header,
    EFI_ACPI_6_4_DEBUG_PORT_2_TABLE_SIGNATURE,
    sizeof (*Dbg2),
    EFI_ACPI_DBG2_DEBUG_DEVICE_INFORMATION_STRUCT_REVISION,
    mOemId,
    OemRevision,
    CreatorRevision
    );
  Dbg2->Description.OffsetDbgDeviceInfo = OFFSET_OF (DBG2_TABLE, Dbg2DeviceInfo);
  Dbg2->Description.NumberDbgDeviceInfo = 1;

  Device                                  = &Dbg2->Dbg2DeviceInfo.Dbg2Device;
  Device->Revision                        = EFI_ACPI_DBG2_DEBUG_DEVICE_INFORMATION_STRUCT_REVISION;
  Device->Length                          = sizeof (DBG2_DEBUG_DEVICE_INFORMATION);
  Device->NumberofGenericAddressRegisters = 1;
  Device->NameSpaceStringLength           = DBG2_NAMESPACESTRING_FIELD_SIZE;
  Device->NameSpaceStringOffset           = OFFSET_OF (DBG2_DEBUG_DEVICE_INFORMATION, NameSpaceString);
  Device->OemDataLength                   = 0;
  Device->OemDataOffset                   = 0;
  Device->PortType                        = EFI_ACPI_DBG2_PORT_TYPE_SERIAL;
  Device->PortSubtype                     = DBG2_SUBTYPE_SAMSUNG;
  Device->BaseAddressRegisterOffset       = OFFSET_OF (DBG2_DEBUG_DEVICE_INFORMATION, BaseAddressRegister);
  Device->AddressSizeOffset               = OFFSET_OF (DBG2_DEBUG_DEVICE_INFORMATION, AddressSize);

  CopyMem (&Dbg2->Dbg2DeviceInfo.BaseAddressRegister, &Register, sizeof (Register));
  Dbg2->Dbg2DeviceInfo.AddressSize        = DBG2_UART_ADDRESS_LENGTH;
  Dbg2->Dbg2DeviceInfo.NameSpaceString[0] = '.';
  Dbg2->Dbg2DeviceInfo.NameSpaceString[1] = '\0';
}

//
// DSDT. What the ASL compiler makes of
//
//   DefinitionBlock ("", "DSDT", 2, ...) {
//     Scope (\_SB) {
//       Device (CPU0) { Name (_HID, "ACPI0007") Name (_UID, 0) }
//       ...
//     }
//   }
//
// written out by hand (ACPI 6.6, chapter 20, "ACPI Machine Language Specification").
// The opcodes are in IndustryStandard/AcpiAml.h.
//

#define DSDT_REVISION  2    // 64-bit integers

STATIC CONST CHAR8  mProcessorHid[]  = "ACPI0007";
STATIC CONST CHAR8  mCpuNameSuffix[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";

//
// A core number is below this, so a _UID fits the one-byte integer form, and the
// scope with all the devices stays far below the 4095 bytes that a PkgLength of
// two bytes can express.
//
#define CPU_NUMBER_LIMIT  (sizeof (mCpuNameSuffix) - 1)

//
// A PkgLength counts its own bytes (20.2.4). Returns how many those are for a
// package with ContentSize bytes after them: one up to 63 in all, else two.
//
STATIC
UINTN
PkgLengthSize (
  IN UINTN  ContentSize
  )
{
  return ContentSize + 1 <= 0x3F ? 1 : 2;
}

STATIC
UINT8 *
PutPkgLength (
  OUT UINT8  *Cursor,
  IN  UINTN  ContentSize
  )
{
  UINTN  Bytes  = PkgLengthSize (ContentSize);
  UINTN  Length = ContentSize + Bytes;

  if (Bytes == 1) {
    *Cursor++ = (UINT8)Length;
    return Cursor;
  }

  //
  // Lead byte: the number of bytes that follow in bits 6-7, the low 4 bits of the
  // length in bits 0-3. The byte that follows holds the next 8 bits.
  //
  *Cursor++ = (UINT8)((1 << 6) | (Length & 0x0F));
  *Cursor++ = (UINT8)(Length >> 4);
  return Cursor;
}

// An integer below 256 the way the ASL compiler encodes it: the shortest form
STATIC
UINTN
IntegerSize (
  IN UINT32  Value
  )
{
  return Value <= 1 ? 1 : 2;
}

STATIC
UINT8 *
PutInteger (
  OUT UINT8   *Cursor,
  IN  UINT32  Value
  )
{
  if (Value <= 1) {
    *Cursor++ = Value == 0 ? AML_ZERO_OP : AML_ONE_OP;
    return Cursor;
  }

  *Cursor++ = AML_BYTE_PREFIX;
  *Cursor++ = (UINT8)Value;
  return Cursor;
}

//
// What follows the PkgLength of one processor device: its name,
// Name (_HID, "ACPI0007"), Name (_UID, Number)
//
STATIC
UINTN
DeviceContentSize (
  IN UINT32  Number
  )
{
  return 4 + (1 + 4 + 1 + sizeof (mProcessorHid)) + (1 + 4 + IntegerSize (Number));
}

// The whole device: the two opcode bytes, the PkgLength, the content
STATIC
UINTN
DeviceSize (
  IN UINT32  Number
  )
{
  return 2 + PkgLengthSize (DeviceContentSize (Number)) + DeviceContentSize (Number);
}

// What follows the PkgLength of the scope: its name and the devices
STATIC
UINTN
ScopeContentSize (
  IN CONST APPLE_CPU  *Cpus,
  IN UINTN            CpuCount
  )
{
  UINTN  Size = 4;
  UINTN  Index;

  for (Index = 0; Index < CpuCount; Index++) {
    Size += DeviceSize (Cpus[Index].Number);
  }

  return Size;
}

UINTN
DsdtSize (
  IN CONST APPLE_CPU  *Cpus,
  IN UINTN            CpuCount
  )
{
  UINTN  Content = ScopeContentSize (Cpus, CpuCount);

  return sizeof (EFI_ACPI_DESCRIPTION_HEADER) + 1 + PkgLengthSize (Content) + Content;
}

RETURN_STATUS
DsdtBuild (
  IN  UINT32                     OemRevision,
  IN  UINT32                     CreatorRevision,
  IN  CONST APPLE_CPU            *Cpus,
  IN  UINTN                      CpuCount,
  OUT VOID                       *Table
  )
{
  UINT8  *Cursor;
  UINTN  Index;

  for (Index = 0; Index < CpuCount; Index++) {
    if (Cpus[Index].Number >= CPU_NUMBER_LIMIT) {
      return RETURN_INVALID_PARAMETER;
    }
  }

  FillHeader (
    Table,
    EFI_ACPI_6_4_DIFFERENTIATED_SYSTEM_DESCRIPTION_TABLE_SIGNATURE,
    DsdtSize (Cpus, CpuCount),
    DSDT_REVISION,
    mDsdtOemId,
    OemRevision,
    CreatorRevision
    );

  Cursor    = (UINT8 *)Table + sizeof (EFI_ACPI_DESCRIPTION_HEADER);
  *Cursor++ = AML_SCOPE_OP;
  Cursor    = PutPkgLength (Cursor, ScopeContentSize (Cpus, CpuCount));
  CopyMem (Cursor, "_SB_", 4);
  Cursor += 4;

  for (Index = 0; Index < CpuCount; Index++) {
    UINT32  Number = Cpus[Index].Number;

    *Cursor++ = AML_EXT_OP;
    *Cursor++ = AML_EXT_DEVICE_OP;
    Cursor    = PutPkgLength (Cursor, DeviceContentSize (Number));
    CopyMem (Cursor, "CPU", 3);
    Cursor[3] = mCpuNameSuffix[Number];
    Cursor   += 4;

    *Cursor++ = AML_NAME_OP;
    CopyMem (Cursor, "_HID", 4);
    Cursor   += 4;
    *Cursor++ = AML_STRING_PREFIX;
    CopyMem (Cursor, mProcessorHid, sizeof (mProcessorHid));
    Cursor += sizeof (mProcessorHid);

    *Cursor++ = AML_NAME_OP;
    CopyMem (Cursor, "_UID", 4);
    Cursor += 4;
    Cursor  = PutInteger (Cursor, Number);
  }

  return RETURN_SUCCESS;
}
