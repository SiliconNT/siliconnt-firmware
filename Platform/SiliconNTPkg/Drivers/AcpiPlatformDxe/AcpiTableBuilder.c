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
    - DSDT: the processors and the devices Windows drivers drive:
      docs/DESIGN.md. What the nodes of the devices hold and why: AnsNode.h,
      CoprocessorNode.h.

  SPDX-License-Identifier: BSD-2-Clause-Patent OR MIT
**/

#include "AcpiTableBuilder.h"

#include <Acpi/AcpiHeader.h>
#include <EmulatedGic.h>
#include <IndustryStandard/DebugPort2Table.h>
#include <Library/AcpiLib.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>

#include "AmlWriter.h"

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

BOOLEAN
Dbg2Wanted (
  IN UINT64  UartBase
  )
{
  return UartBase != 0;
}

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
//       Device (<name>) {                         for each device
//         Name (_HID, "<hardware ID>")
//         Name (_UID, 0)
//         Name (_CCA, 0)
//         Name (_CRS, ResourceTemplate () {
//           QWordMemory (ResourceConsumer, PosDecode, MinFixed, MaxFixed, NonCacheable,
//                        ReadWrite, 0, <base>, <base + size - 1>, 0, <size>)
//           ...
//           Interrupt (ResourceConsumer, Level, ActiveHigh, Exclusive) { <GSIV> }
//           ...
//         })
//         Name (_DSD, Package () {
//           ToUUID ("daffd814-6eba-4d8c-8a91-bc9bbf4aa301"),
//           Package () {
//             Package (2) { "<key>", <integer, string or package> }
//             ...
//           }
//         })
//       }
//     }
//   }
//
// written out by hand (ACPI 6.6, chapter 20, "ACPI Machine Language Specification";
// the resource descriptors: 6.4.3.5.1, QWORD Address Space Descriptor, and
// 6.4.3.6, Extended Interrupt Descriptor; ToUUID: 19.6.144). The _DSD follows
// "Device Properties UUID For _DSD", revision 2.0, 2.1. The descriptors are in
// IndustryStandard/Acpi10.h.
//
// A package has at most 255 elements: more would need VarPackageOp, and Windows
// stops reading the table where it meets one (SiliconNT docs/findings/0095,
// conclusion 11).
//

#define DSDT_REVISION  2    // 64-bit integers

STATIC CONST CHAR8  mCpuNameSuffix[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";

// A core number is below this: there is a character of the name for it
#define CPU_NUMBER_LIMIT  (sizeof (mCpuNameSuffix) - 1)

//
// The Device Properties UUID, daffd814-6eba-4d8c-8a91-bc9bbf4aa301, in the byte
// order of ToUUID: the first three fields little-endian, the last two as written
//
STATIC CONST UINT8  mDevicePropertiesUuid[16] = {
  0x14, 0xd8, 0xff, 0xda, 0xba, 0x6e, 0x8c, 0x4d, 0x8a, 0x91, 0xbc, 0x9b, 0xbf, 0x4a, 0xa3, 0x01
};

//
// General flags of an address space descriptor (ACPI 6.6, table 6.45): MaxFixed,
// MinFixed, PosDecode. Bit 0, which the table calls ignored, is what the ASL
// compiler sets for ResourceConsumer.
//
#define ADDRESS_FLAG_CONSUMER   BIT0
#define ADDRESS_FLAG_MIN_FIXED  BIT2
#define ADDRESS_FLAG_MAX_FIXED  BIT3

// A range a QWORD descriptor can hold: not empty, its last byte inside the address space
STATIC
BOOLEAN
DescribableRange (
  IN CONST APPLE_MMIO_RANGE  *Range
  )
{
  return Range->Size != 0 && Range->Size - 1 <= MAX_UINT64 - Range->Base;
}

STATIC
BOOLEAN
DescribableDevice (
  IN CONST DSDT_DEVICE  *Device
  )
{
  UINTN  Index;

  for (Index = 0; Index < Device->MemoryCount; Index++) {
    if (!DescribableRange (&Device->Memory[Index])) {
      return FALSE;
    }
  }

  for (Index = 0; Index < Device->PropertyCount; Index++) {
    if (Device->Properties[Index].StringCount + Device->Properties[Index].WordCount > DSDT_MAX_ELEMENTS) {
      return FALSE;
    }
  }

  return Device->PropertyCount <= DSDT_MAX_ELEMENTS;
}

// Device (XXXX) up to what is in it
STATIC
UINTN
BeginDevice (
  IN OUT AML_WRITER   *Writer,
  IN     CONST CHAR8  *Name
  )
{
  UINTN  Start;

  AmlPutByte (Writer, AML_EXT_OP);
  AmlPutByte (Writer, AML_EXT_DEVICE_OP);
  Start = AmlBegin (Writer);
  AmlPutNameSeg (Writer, Name);
  return Start;
}

// Package () { up to the first element
STATIC
UINTN
BeginPackage (
  IN OUT AML_WRITER  *Writer,
  IN     UINTN       Elements
  )
{
  UINTN  Start;

  AmlPutByte (Writer, AML_PACKAGE_OP);
  Start = AmlBegin (Writer);
  AmlPutByte (Writer, (UINT8)Elements);
  return Start;
}

// QWordMemory (ResourceConsumer, PosDecode, MinFixed, MaxFixed, NonCacheable, ReadWrite, ...)
STATIC
VOID
PutQWordMemory (
  IN OUT AML_WRITER              *Writer,
  IN     CONST APPLE_MMIO_RANGE  *Range
  )
{
  EFI_ACPI_QWORD_ADDRESS_SPACE_DESCRIPTOR  Descriptor;

  ZeroMem (&Descriptor, sizeof (Descriptor));
  Descriptor.Header.Header.Byte    = ACPI_QWORD_ADDRESS_SPACE_DESCRIPTOR;
  Descriptor.Header.Length         = sizeof (Descriptor) - sizeof (ACPI_LARGE_RESOURCE_HEADER);
  Descriptor.ResType               = ACPI_ADDRESS_SPACE_TYPE_MEM;
  Descriptor.GenFlag               = ADDRESS_FLAG_CONSUMER | ADDRESS_FLAG_MIN_FIXED | ADDRESS_FLAG_MAX_FIXED;
  Descriptor.SpecificFlag          = EFI_ACPI_MEMORY_RESOURCE_SPECIFIC_FLAG_NON_CACHEABLE | EFI_ACPI_MEMORY_RESOURCE_SPECIFIC_FLAG_READ_WRITE;
  Descriptor.AddrSpaceGranularity  = 0;
  Descriptor.AddrRangeMin          = Range->Base;
  Descriptor.AddrRangeMax          = Range->Base + (Range->Size - 1);
  Descriptor.AddrTranslationOffset = 0;
  Descriptor.AddrLen               = Range->Size;

  AmlPutBytes (Writer, &Descriptor, sizeof (Descriptor));
}

// Interrupt (ResourceConsumer, Level, ActiveHigh, Exclusive) { Gsiv }: the flags but the first are 0
STATIC
VOID
PutInterrupt (
  IN OUT AML_WRITER  *Writer,
  IN     UINT32      Gsiv
  )
{
  EFI_ACPI_EXTENDED_INTERRUPT_DESCRIPTOR  Descriptor;

  Descriptor.Header.Header.Byte   = ACPI_EXTENDED_INTERRUPT_DESCRIPTOR;
  Descriptor.Header.Length        = sizeof (Descriptor) - sizeof (ACPI_LARGE_RESOURCE_HEADER);
  Descriptor.InterruptVectorFlags = EFI_ACPI_EXTENDED_INTERRUPT_FLAG_PRODUCER_CONSUMER_MASK;
  Descriptor.InterruptTableLength = 1;
  Descriptor.InterruptNumber[0]   = Gsiv;

  AmlPutBytes (Writer, &Descriptor, sizeof (Descriptor));
}

// ResourceTemplate () { ... }: a Buffer, its size as an integer, the descriptors
STATIC
VOID
PutResources (
  IN OUT AML_WRITER         *Writer,
  IN     CONST DSDT_DEVICE  *Device
  )
{
  STATIC CONST EFI_ACPI_END_TAG_DESCRIPTOR  EndTag = { ACPI_END_TAG_DESCRIPTOR, 0 };
  UINTN                                     Start;
  UINTN                                     Index;

  AmlPutByte (Writer, AML_BUFFER_OP);
  Start = AmlBegin (Writer);
  AmlPutInteger (
    Writer,
    (UINT32)(Device->MemoryCount * sizeof (EFI_ACPI_QWORD_ADDRESS_SPACE_DESCRIPTOR) +
             Device->GsivCount * sizeof (EFI_ACPI_EXTENDED_INTERRUPT_DESCRIPTOR) +
             sizeof (EndTag))
    );

  for (Index = 0; Index < Device->MemoryCount; Index++) {
    PutQWordMemory (Writer, &Device->Memory[Index]);
  }

  for (Index = 0; Index < Device->GsivCount; Index++) {
    PutInterrupt (Writer, Device->Gsiv[Index]);
  }

  AmlPutBytes (Writer, &EndTag, sizeof (EndTag));
  AmlEnd (Writer, Start);
}

// Package (2) { "key", value }
STATIC
VOID
PutProperty (
  IN OUT AML_WRITER           *Writer,
  IN     CONST DSDT_PROPERTY  *Property
  )
{
  UINTN        Start = BeginPackage (Writer, 2);
  UINTN        ValueStart;
  CONST CHAR8  *String = Property->Strings;
  UINT32       Word;
  UINTN        Index;

  AmlPutString (Writer, Property->Key);

  switch (Property->Kind) {
    case DsdtInteger:
      AmlPutInteger (Writer, Property->Integer);
      break;
    case DsdtString:
      AmlPutString (Writer, Property->Strings);
      break;
    case DsdtPackage:
      ValueStart = BeginPackage (Writer, Property->StringCount + Property->WordCount);
      for (Index = 0; Index < Property->StringCount; Index++) {
        AmlPutString (Writer, String);
        String += AsciiStrLen (String) + 1;
      }

      for (Index = 0; Index < Property->WordCount; Index++) {
        CopyMem (&Word, Property->Words + Index * sizeof (Word), sizeof (Word));
        AmlPutInteger (Writer, Word);
      }

      AmlEnd (Writer, ValueStart);
      break;
  }

  AmlEnd (Writer, Start);
}

// Package () { ToUUID (...), Package () { the properties } }
STATIC
VOID
PutProperties (
  IN OUT AML_WRITER         *Writer,
  IN     CONST DSDT_DEVICE  *Device
  )
{
  UINTN  Start = BeginPackage (Writer, 2);
  UINTN  InnerStart;
  UINTN  Index;

  AmlPutByte (Writer, AML_BUFFER_OP);
  InnerStart = AmlBegin (Writer);
  AmlPutInteger (Writer, sizeof (mDevicePropertiesUuid));
  AmlPutBytes (Writer, mDevicePropertiesUuid, sizeof (mDevicePropertiesUuid));
  AmlEnd (Writer, InnerStart);

  InnerStart = BeginPackage (Writer, Device->PropertyCount);
  for (Index = 0; Index < Device->PropertyCount; Index++) {
    PutProperty (Writer, &Device->Properties[Index]);
  }

  AmlEnd (Writer, InnerStart);
  AmlEnd (Writer, Start);
}

STATIC
VOID
PutDevice (
  IN OUT AML_WRITER         *Writer,
  IN     CONST DSDT_DEVICE  *Device
  )
{
  UINTN  Start = BeginDevice (Writer, Device->Name);

  AmlPutName (Writer, "_HID");
  AmlPutString (Writer, Device->Hid);
  AmlPutName (Writer, "_UID");
  AmlPutInteger (Writer, 0);
  AmlPutName (Writer, "_CCA");
  AmlPutInteger (Writer, 0);
  AmlPutName (Writer, "_CRS");
  PutResources (Writer, Device);
  AmlPutName (Writer, "_DSD");
  PutProperties (Writer, Device);
  AmlEnd (Writer, Start);
}

STATIC
VOID
PutProcessor (
  IN OUT AML_WRITER  *Writer,
  IN     UINT32      Number
  )
{
  CHAR8  Name[4] = { 'C', 'P', 'U', mCpuNameSuffix[Number] };
  UINTN  Start   = BeginDevice (Writer, Name);

  AmlPutName (Writer, "_HID");
  AmlPutString (Writer, "ACPI0007");
  AmlPutName (Writer, "_UID");
  AmlPutInteger (Writer, Number);
  AmlEnd (Writer, Start);
}

// Everything after the table header. The core numbers are below CPU_NUMBER_LIMIT.
STATIC
VOID
PutDsdtBody (
  IN OUT AML_WRITER         *Writer,
  IN     CONST APPLE_CPU    *Cpus,
  IN     UINTN              CpuCount,
  IN     CONST DSDT_DEVICE  *CONST *Devices,
  IN     UINTN              DeviceCount
  )
{
  UINTN  Start;
  UINTN  Index;

  AmlPutByte (Writer, AML_SCOPE_OP);
  Start = AmlBegin (Writer);
  AmlPutNameSeg (Writer, "_SB_");

  for (Index = 0; Index < CpuCount; Index++) {
    PutProcessor (Writer, Cpus[Index].Number);
  }

  for (Index = 0; Index < DeviceCount; Index++) {
    PutDevice (Writer, Devices[Index]);
  }

  AmlEnd (Writer, Start);
}

STATIC
BOOLEAN
DescribableMachine (
  IN CONST APPLE_CPU    *Cpus,
  IN UINTN              CpuCount,
  IN CONST DSDT_DEVICE  *CONST *Devices,
  IN UINTN              DeviceCount
  )
{
  UINTN  Index;

  for (Index = 0; Index < CpuCount; Index++) {
    if (Cpus[Index].Number >= CPU_NUMBER_LIMIT) {
      return FALSE;
    }
  }

  for (Index = 0; Index < DeviceCount; Index++) {
    if (!DescribableDevice (Devices[Index])) {
      return FALSE;
    }
  }

  return TRUE;
}

UINTN
DsdtSize (
  IN CONST APPLE_CPU    *Cpus,
  IN UINTN              CpuCount,
  IN CONST DSDT_DEVICE  *CONST *Devices,
  IN UINTN              DeviceCount
  )
{
  AML_WRITER  Writer = { NULL, 0 };

  if (!DescribableMachine (Cpus, CpuCount, Devices, DeviceCount)) {
    return 0;
  }

  PutDsdtBody (&Writer, Cpus, CpuCount, Devices, DeviceCount);
  return sizeof (EFI_ACPI_DESCRIPTION_HEADER) + Writer.Used;
}

RETURN_STATUS
DsdtBuild (
  IN  UINT32             OemRevision,
  IN  UINT32             CreatorRevision,
  IN  CONST APPLE_CPU    *Cpus,
  IN  UINTN              CpuCount,
  IN  CONST DSDT_DEVICE  *CONST *Devices,
  IN  UINTN              DeviceCount,
  OUT VOID               *Table
  )
{
  AML_WRITER  Writer = { (UINT8 *)Table + sizeof (EFI_ACPI_DESCRIPTION_HEADER), 0 };

  if (!DescribableMachine (Cpus, CpuCount, Devices, DeviceCount)) {
    return RETURN_INVALID_PARAMETER;
  }

  PutDsdtBody (&Writer, Cpus, CpuCount, Devices, DeviceCount);
  FillHeader (
    Table,
    EFI_ACPI_6_4_DIFFERENTIATED_SYSTEM_DESCRIPTION_TABLE_SIGNATURE,
    sizeof (EFI_ACPI_DESCRIPTION_HEADER) + Writer.Used,
    DSDT_REVISION,
    mDsdtOemId,
    OemRevision,
    CreatorRevision
    );
  return RETURN_SUCCESS;
}
