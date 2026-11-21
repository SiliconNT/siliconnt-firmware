/**
 * Copyright (c) 2023, amarioguy (AppleWOA authors).
 *
 * Module Name:
 *     Fadt.c
 *
 * Abstract:
 *     Fixed ACPI Description Table as SiliconNT presents it. It is the same on
 *     every machine; AcpiPlatformDxe installs a copy with the OEM revision set to
 *     the number of the chip.
 *
 * License:
 *     SPDX-License-Identifier: BSD-2-Clause-Patent OR MIT
 *
 * SiliconNT: derived from NT-for-ASi/apple_silicon_platforms_mu (861dcc490c56),
 * Platform/MacBookProEarly2023Pkg/AcpiTables/FADT.aslc.
 *
 * What SiliconNT relies on (docs/DESIGN.md, "提供 PSCI"; docs/findings/0006):
 *   - major version 6, so that Windows looks at the ARM boot flags;
 *   - PSCI_COMPLIANT and PSCI_USE_HVC both set: Windows calls PSCI with hvc #0;
 *   - Hypervisor Vendor Identity all zero: Windows concludes there is no hypervisor.
 * The fixed feature flags are the ones the NTASP table had; they have not been
 * checked against what Windows needs.
 */

#include <Library/AcpiLib.h>
#include <IndustryStandard/Acpi64.h>
#include <Acpi/AcpiHeader.h>

#include "StaticTables.h"

CONST EFI_ACPI_6_4_FIXED_ACPI_DESCRIPTION_TABLE  gStaticFadt = {
  __ACPI_HEADER (
    EFI_ACPI_6_4_FIXED_ACPI_DESCRIPTION_TABLE_SIGNATURE,
    EFI_ACPI_6_4_FIXED_ACPI_DESCRIPTION_TABLE,
    EFI_ACPI_6_4_FIXED_ACPI_DESCRIPTION_TABLE_REVISION
    ),
  0,                              // UINT32  FirmwareCtrl
  0,                              // UINT32  Dsdt
  EFI_ACPI_RESERVED_BYTE,         // UINT8   Reserved0
  EFI_ACPI_6_4_PM_PROFILE_MOBILE, // UINT8   PreferredPmProfile
  0,                              // UINT16  SciInt
  0,                              // UINT32  SmiCmd
  0,                              // UINT8   AcpiEnable
  0,                              // UINT8   AcpiDisable
  0,                              // UINT8   S4BiosReq
  0,                              // UINT8   PstateCnt
  0,                              // UINT32  Pm1aEvtBlk
  0,                              // UINT32  Pm1bEvtBlk
  0,                              // UINT32  Pm1aCntBlk
  0,                              // UINT32  Pm1bCntBlk
  0,                              // UINT32  Pm2CntBlk
  0,                              // UINT32  PmTmrBlk
  0,                              // UINT32  Gpe0Blk
  0,                              // UINT32  Gpe1Blk
  0,                              // UINT8   Pm1EvtLen
  0,                              // UINT8   Pm1CntLen
  0,                              // UINT8   Pm2CntLen
  0,                              // UINT8   PmTmrLen
  0,                              // UINT8   Gpe0BlkLen
  0,                              // UINT8   Gpe1BlkLen
  0,                              // UINT8   Gpe1Base
  0,                              // UINT8   CstCnt
  0,                              // UINT16  PLvl2Lat
  0,                              // UINT16  PLvl3Lat
  0,                              // UINT16  FlushSize
  0,                              // UINT16  FlushStride
  0,                              // UINT8   DutyOffset
  0,                              // UINT8   DutyWidth
  0,                              // UINT8   DayAlrm
  0,                              // UINT8   MonAlrm
  0,                              // UINT8   Century
  0,                              // UINT16  IaPcBootArch
  0,                              // UINT8   Reserved1
  (EFI_ACPI_6_4_HW_REDUCED_ACPI | EFI_ACPI_6_4_LOW_POWER_S0_IDLE_CAPABLE | EFI_ACPI_6_4_PWR_BUTTON), // UINT32  Flags
  NULL_GAS,                       // ResetReg
  0,                              // UINT8   ResetValue
  (EFI_ACPI_6_4_ARM_PSCI_COMPLIANT | EFI_ACPI_6_4_ARM_PSCI_USE_HVC), // UINT16  ArmBootArch
  EFI_ACPI_6_4_FIXED_ACPI_DESCRIPTION_TABLE_MINOR_REVISION, // UINT8   MinorVersion
  0,                              // UINT64  XFirmwareCtrl
  0,                              // UINT64  XDsdt
  NULL_GAS,                       // XPm1aEvtBlk
  NULL_GAS,                       // XPm1bEvtBlk
  NULL_GAS,                       // XPm1aCntBlk
  NULL_GAS,                       // XPm1bCntBlk
  NULL_GAS,                       // XPm2CntBlk
  NULL_GAS,                       // XPmTmrBlk
  NULL_GAS,                       // XGpe0Blk
  NULL_GAS,                       // XGpe1Blk
  NULL_GAS,                       // SleepControlReg
  NULL_GAS,                       // SleepStatusReg
  0                               // UINT64  HypervisorVendorIdentity
};
