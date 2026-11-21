/**
 * Differentiated System Description Table of the M4 MacBook Pro as SiliconNT
 * presents it. It describes the ten processors and nothing else
 * (SiliconNT docs/DESIGN.md, "固件").
 *
 * _UID of each processor equals the ACPI Processor UID in the MADT, which is the
 * m1n1 core number: 0-5 efficiency cores, 6-9 performance cores.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 **/

DefinitionBlock ("Dsdt.aml", "DSDT", 2, "Apple ", "APPLEEFI", 0x8132) {
  Scope (\_SB) {
    Device (CPU0) { Name (_HID, "ACPI0007") Name (_UID, 0) }
    Device (CPU1) { Name (_HID, "ACPI0007") Name (_UID, 1) }
    Device (CPU2) { Name (_HID, "ACPI0007") Name (_UID, 2) }
    Device (CPU3) { Name (_HID, "ACPI0007") Name (_UID, 3) }
    Device (CPU4) { Name (_HID, "ACPI0007") Name (_UID, 4) }
    Device (CPU5) { Name (_HID, "ACPI0007") Name (_UID, 5) }
    Device (CPU6) { Name (_HID, "ACPI0007") Name (_UID, 6) }
    Device (CPU7) { Name (_HID, "ACPI0007") Name (_UID, 7) }
    Device (CPU8) { Name (_HID, "ACPI0007") Name (_UID, 8) }
    Device (CPU9) { Name (_HID, "ACPI0007") Name (_UID, 9) }
  }
}
