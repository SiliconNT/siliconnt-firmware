/** @file
  The GIC the EL2 layer emulates, as far as the firmware has to know its shape
  (SiliconNT docs/DESIGN.md; m1n1, src/hv/hv.h and src/hv/hv_vgic.c).

  The Distributor is at PcdGicDistributorBase. The Redistributor of the core
  m1n1 numbers n is two 64 KB frames at
  PcdGicRedistributorsBase + n * EMULATED_GIC_REDISTRIBUTOR_SIZE: the place
  depends on the number of the core, not on how many cores there are.

  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#ifndef EMULATED_GIC_H_
#define EMULATED_GIC_H_

#include <Base.h>
#include <Library/AppleDTLib.h>

#define EMULATED_GIC_DISTRIBUTOR_SIZE    0x10000ULL
#define EMULATED_GIC_REDISTRIBUTOR_SIZE  0x20000ULL

/**
  How many Redistributors the range that holds the ones of these cores has: the
  highest core number plus one. The memory map and the MADT both describe the
  range with this length.
**/
STATIC
inline
UINTN
EmulatedGicRedistributorCount (
  IN CONST APPLE_CPU  *Cpus,
  IN UINTN            CpuCount
  )
{
  UINTN  Count = 0;
  UINTN  Index;

  for (Index = 0; Index < CpuCount; Index++) {
    if (Cpus[Index].Number + 1 > Count) {
      Count = Cpus[Index].Number + 1;
    }
  }

  return Count;
}

#endif // EMULATED_GIC_H_
