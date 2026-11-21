/** @file
  The ACPI tables that are the same on every machine (Gtdt.c, Fadt.c). Their OEM
  revision is 0 here; AcpiPlatformDxe installs copies with the number of the chip
  in that field.

  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#ifndef STATIC_TABLES_H_
#define STATIC_TABLES_H_

#include <IndustryStandard/Acpi64.h>

extern CONST EFI_ACPI_6_4_GENERIC_TIMER_DESCRIPTION_TABLE  gStaticGtdt;
extern CONST EFI_ACPI_6_4_FIXED_ACPI_DESCRIPTION_TABLE     gStaticFadt;

#endif // STATIC_TABLES_H_
