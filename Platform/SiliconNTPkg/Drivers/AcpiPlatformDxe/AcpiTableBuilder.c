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
    - DSDT: the processors and the devices Windows drivers of SiliconNT drive,
      for now the internal disk controller: docs/DESIGN.md. What the node of
      the disk controller holds and why: docs/findings/0062.

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
//       Device (ANS0) {
//         Name (_HID, "SLNT0001")
//         Name (_UID, 0)
//         Name (_CCA, 0)
//         Name (_CRS, ResourceTemplate () {
//           QWordMemory (ResourceConsumer, PosDecode, MinFixed, MaxFixed, NonCacheable,
//                        ReadWrite, 0, <base>, <base + size - 1>, 0, <size>)     NVMe
//           QWordMemory (...)                                                    coprocessor
//           QWordMemory (...)                                                    SART
//           Interrupt (ResourceConsumer, Level, ActiveHigh, Exclusive) { <GSIV> } controller
//           Interrupt (ResourceConsumer, Level, ActiveHigh, Exclusive) { <GSIV> } mailbox
//         })
//         Name (_DSD, Package () {                       only with "nvme-secure-bar"
//           ToUUID ("daffd814-6eba-4d8c-8a91-bc9bbf4aa301"),
//           Package () { Package (2) { "nvme-secure-bar", 1 } }
//         })
//       }
//     }
//   }
//
// written out by hand (ACPI 6.6, chapter 20, "ACPI Machine Language Specification";
// the resource descriptors: 6.4.3.5.1, QWORD Address Space Descriptor, and
// 6.4.3.6, Extended Interrupt Descriptor; ToUUID: 19.6.144). The _DSD follows
// "Device Properties UUID For _DSD", revision 2.0, 2.1. The opcodes are in
// IndustryStandard/AcpiAml.h, the descriptors in IndustryStandard/Acpi10.h.
//

#define DSDT_REVISION  2    // 64-bit integers

STATIC CONST CHAR8  mProcessorHid[]  = "ACPI0007";
STATIC CONST CHAR8  mCpuNameSuffix[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";

//
// A core number is below this, so a _UID fits the one-byte integer form, and the
// scope with all the devices, the disk controller's included, stays far below the
// 4095 bytes that a PkgLength of two bytes can express.
//
#define CPU_NUMBER_LIMIT  (sizeof (mCpuNameSuffix) - 1)

STATIC CONST CHAR8  mAnsHid[]         = DSDT_ANS_HID;
STATIC CONST CHAR8  mSecureBarKey[]   = "nvme-secure-bar";

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

//
// _CRS: three QWORD memory descriptors, two Extended Interrupt descriptors with
// one interrupt each, the end tag. Its size fits the one-byte integer form.
//
#define ANS_CRS_SIZE  (3 * sizeof (EFI_ACPI_QWORD_ADDRESS_SPACE_DESCRIPTOR) +\
                       2 * sizeof (EFI_ACPI_EXTENDED_INTERRUPT_DESCRIPTOR) +\
                       sizeof (EFI_ACPI_END_TAG_DESCRIPTOR))

STATIC_ASSERT (ANS_CRS_SIZE <= 0xFF, "_CRS of the disk controller: buffer size above one byte");

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

//
// The disk controller
//

// A Buffer or a Package with ContentSize bytes after its PkgLength: opcode, PkgLength, content
STATIC
UINTN
PackageSize (
  IN UINTN  ContentSize
  )
{
  return 1 + PkgLengthSize (ContentSize) + ContentSize;
}

// Name (XXXX, ...) up to the object: NameOp, NameSeg
#define NAME_SIZE  (1 + 4)

// After the PkgLength of the Buffer of _CRS: its size as an integer, the descriptors
#define ANS_CRS_CONTENT_SIZE  (2 + ANS_CRS_SIZE)

// After the PkgLength of Package (2) { "nvme-secure-bar", 1 }: NumElements, the key, One
#define SECURE_BAR_CONTENT_SIZE  (1 + 1 + sizeof (mSecureBarKey) + 1)

// After the PkgLength of the package of properties: NumElements, the one property
#define PROPERTIES_CONTENT_SIZE  (1 + PackageSize (SECURE_BAR_CONTENT_SIZE))

// After the PkgLength of the Buffer of ToUUID: the size 16 as an integer, the 16 bytes
#define UUID_CONTENT_SIZE  (2 + sizeof (mDevicePropertiesUuid))

// After the PkgLength of the Package of _DSD: NumElements, the UUID, the properties
#define ANS_DSD_CONTENT_SIZE  (1 + PackageSize (UUID_CONTENT_SIZE) + PackageSize (PROPERTIES_CONTENT_SIZE))

// What follows the PkgLength of Device (ANS0): its name and its objects
STATIC
UINTN
AnsContentSize (
  IN CONST DSDT_ANS  *Ans
  )
{
  return 4 +
         NAME_SIZE + 1 + sizeof (mAnsHid) +
         NAME_SIZE + IntegerSize (0) +
         NAME_SIZE + IntegerSize (0) +
         NAME_SIZE + PackageSize (ANS_CRS_CONTENT_SIZE) +
         (Ans->SecureBar ? NAME_SIZE + PackageSize (ANS_DSD_CONTENT_SIZE) : 0);
}

STATIC
UINTN
AnsSize (
  IN CONST DSDT_ANS  *Ans
  )
{
  return 2 + PkgLengthSize (AnsContentSize (Ans)) + AnsContentSize (Ans);
}

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
UINT8 *
PutName (
  OUT UINT8        *Cursor,
  IN  CONST CHAR8  *Name
  )
{
  *Cursor++ = AML_NAME_OP;
  CopyMem (Cursor, Name, 4);
  return Cursor + 4;
}

// QWordMemory (ResourceConsumer, PosDecode, MinFixed, MaxFixed, NonCacheable, ReadWrite, ...)
STATIC
UINT8 *
PutQWordMemory (
  OUT UINT8                   *Cursor,
  IN  CONST APPLE_MMIO_RANGE  *Range
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

  CopyMem (Cursor, &Descriptor, sizeof (Descriptor));
  return Cursor + sizeof (Descriptor);
}

// Interrupt (ResourceConsumer, Level, ActiveHigh, Exclusive) { Gsiv }: the flags but the first are 0
STATIC
UINT8 *
PutInterrupt (
  OUT UINT8   *Cursor,
  IN  UINT32  Gsiv
  )
{
  EFI_ACPI_EXTENDED_INTERRUPT_DESCRIPTOR  Descriptor;

  Descriptor.Header.Header.Byte   = ACPI_EXTENDED_INTERRUPT_DESCRIPTOR;
  Descriptor.Header.Length        = sizeof (Descriptor) - sizeof (ACPI_LARGE_RESOURCE_HEADER);
  Descriptor.InterruptVectorFlags = EFI_ACPI_EXTENDED_INTERRUPT_FLAG_PRODUCER_CONSUMER_MASK;
  Descriptor.InterruptTableLength = 1;
  Descriptor.InterruptNumber[0]   = Gsiv;

  CopyMem (Cursor, &Descriptor, sizeof (Descriptor));
  return Cursor + sizeof (Descriptor);
}

STATIC
UINT8 *
PutAns (
  OUT UINT8           *Cursor,
  IN  CONST DSDT_ANS  *Ans
  )
{
  STATIC CONST EFI_ACPI_END_TAG_DESCRIPTOR  EndTag = { ACPI_END_TAG_DESCRIPTOR, 0 };

  *Cursor++ = AML_EXT_OP;
  *Cursor++ = AML_EXT_DEVICE_OP;
  Cursor    = PutPkgLength (Cursor, AnsContentSize (Ans));
  CopyMem (Cursor, "ANS0", 4);
  Cursor += 4;

  Cursor    = PutName (Cursor, "_HID");
  *Cursor++ = AML_STRING_PREFIX;
  CopyMem (Cursor, mAnsHid, sizeof (mAnsHid));
  Cursor += sizeof (mAnsHid);

  Cursor = PutName (Cursor, "_UID");
  Cursor = PutInteger (Cursor, 0);

  Cursor = PutName (Cursor, "_CCA");
  Cursor = PutInteger (Cursor, 0);

  Cursor    = PutName (Cursor, "_CRS");
  *Cursor++ = AML_BUFFER_OP;
  Cursor    = PutPkgLength (Cursor, ANS_CRS_CONTENT_SIZE);
  Cursor    = PutInteger (Cursor, ANS_CRS_SIZE);
  Cursor    = PutQWordMemory (Cursor, &Ans->Nvme);
  Cursor    = PutQWordMemory (Cursor, &Ans->Coprocessor);
  Cursor    = PutQWordMemory (Cursor, &Ans->Sart);
  Cursor    = PutInterrupt (Cursor, Ans->NvmeGsiv);
  Cursor    = PutInterrupt (Cursor, Ans->MailboxGsiv);
  CopyMem (Cursor, &EndTag, sizeof (EndTag));
  Cursor += sizeof (EndTag);

  if (!Ans->SecureBar) {
    return Cursor;
  }

  Cursor    = PutName (Cursor, "_DSD");
  *Cursor++ = AML_PACKAGE_OP;
  Cursor    = PutPkgLength (Cursor, ANS_DSD_CONTENT_SIZE);
  *Cursor++ = 2;

  *Cursor++ = AML_BUFFER_OP;
  Cursor    = PutPkgLength (Cursor, UUID_CONTENT_SIZE);
  Cursor    = PutInteger (Cursor, sizeof (mDevicePropertiesUuid));
  CopyMem (Cursor, mDevicePropertiesUuid, sizeof (mDevicePropertiesUuid));
  Cursor += sizeof (mDevicePropertiesUuid);

  *Cursor++ = AML_PACKAGE_OP;
  Cursor    = PutPkgLength (Cursor, PROPERTIES_CONTENT_SIZE);
  *Cursor++ = 1;
  *Cursor++ = AML_PACKAGE_OP;
  Cursor    = PutPkgLength (Cursor, SECURE_BAR_CONTENT_SIZE);
  *Cursor++ = 2;
  *Cursor++ = AML_STRING_PREFIX;
  CopyMem (Cursor, mSecureBarKey, sizeof (mSecureBarKey));
  Cursor += sizeof (mSecureBarKey);
  return PutInteger (Cursor, 1);
}

//
// The table
//

// What follows the PkgLength of the scope: its name and the devices
STATIC
UINTN
ScopeContentSize (
  IN CONST APPLE_CPU  *Cpus,
  IN UINTN            CpuCount,
  IN CONST DSDT_ANS   *Ans
  )
{
  UINTN  Size = 4;
  UINTN  Index;

  for (Index = 0; Index < CpuCount; Index++) {
    Size += DeviceSize (Cpus[Index].Number);
  }

  if (Ans != NULL) {
    Size += AnsSize (Ans);
  }

  return Size;
}

UINTN
DsdtSize (
  IN CONST APPLE_CPU  *Cpus,
  IN UINTN            CpuCount,
  IN CONST DSDT_ANS   *Ans      OPTIONAL
  )
{
  UINTN  Content = ScopeContentSize (Cpus, CpuCount, Ans);

  return sizeof (EFI_ACPI_DESCRIPTION_HEADER) + 1 + PkgLengthSize (Content) + Content;
}

RETURN_STATUS
DsdtBuild (
  IN  UINT32                     OemRevision,
  IN  UINT32                     CreatorRevision,
  IN  CONST APPLE_CPU            *Cpus,
  IN  UINTN                      CpuCount,
  IN  CONST DSDT_ANS             *Ans      OPTIONAL,
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

  if ((Ans != NULL) &&
      !(DescribableRange (&Ans->Nvme) && DescribableRange (&Ans->Coprocessor) && DescribableRange (&Ans->Sart)))
  {
    return RETURN_INVALID_PARAMETER;
  }

  FillHeader (
    Table,
    EFI_ACPI_6_4_DIFFERENTIATED_SYSTEM_DESCRIPTION_TABLE_SIGNATURE,
    DsdtSize (Cpus, CpuCount, Ans),
    DSDT_REVISION,
    mDsdtOemId,
    OemRevision,
    CreatorRevision
    );

  Cursor    = (UINT8 *)Table + sizeof (EFI_ACPI_DESCRIPTION_HEADER);
  *Cursor++ = AML_SCOPE_OP;
  Cursor    = PutPkgLength (Cursor, ScopeContentSize (Cpus, CpuCount, Ans));
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

  if (Ans != NULL) {
    PutAns (Cursor, Ans);
  }

  return RETURN_SUCCESS;
}
