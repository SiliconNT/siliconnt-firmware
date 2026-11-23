/** @file
  ans_env.h of the disk controller's core, for the firmware. See AnsEnv.h.

  Registers    The four ranges AnsBlockIoDxe.c reads from the device tree. An
               access outside its range, or not aligned to its size, is
               refused: logged, the hardware not touched, a read gives all
               ones. MmioRead64() and MmioWrite64() of the ARM IoLib are single
               64-bit accesses.
  Memory       Pages from DmaAllocateAlignedBuffer(): physically contiguous,
               aligned to 16 KiB, mapped write-combining (Normal non-cacheable
               on ARM) and so not cached. The device address is what
               DmaMap(MapOperationBusMasterCommonBuffer) gives for them, not the
               CPU address taken as it is. Filled with zeros by aligned 64-bit
               writes: no CopyMem() or SetMem() on memory the device shares
               (findings/0071 conclusion 6).
  Waiting      MicroSecondDelay() of TimerLib, which spins on the generic
               timer. Usable inside ExitBootServices, unlike gBS->Stall().
  Log          One line to DEBUG_INFO, with "AnsBlockIoDxe: " in front.

  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include "AnsEnv.h"

#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>
#include <Library/DmaLib.h>
#include <Library/IoLib.h>
#include <Library/TimerLib.h>

//
// A line of the core's log: the prefix, the core's format and the newline
//
#define ANS_ENV_LOG_PREFIX       "AnsBlockIoDxe: "
#define ANS_ENV_LOG_FORMAT_SIZE  256

/**
  Whether an access of Width bytes at Offset fits inside the region and is
  aligned to its size. A refused one is logged and counted.
**/
STATIC
BOOLEAN
AccessFits (
  IN struct ans_env    *Env,
  IN enum ans_region   Region,
  IN UINT32            Offset,
  IN UINT32            Width
  )
{
  if (((UINT32)Region < ANS_RGN_COUNT) &&
      (Env->Region[Region].Base != 0) &&
      ((Offset & (Width - 1)) == 0) &&
      (Offset <= Env->Region[Region].Size) &&
      (Width <= Env->Region[Region].Size - Offset))
  {
    return TRUE;
  }

  Env->Refused++;
  ans_env_log (Env, "env: refused %u-byte access to region %u offset 0x%x", Width, (UINT32)Region, Offset);
  return FALSE;
}

uint32_t
ans_env_read32 (
  struct ans_env   *env,
  enum ans_region  rgn,
  uint32_t         offset
  )
{
  if (!AccessFits (env, rgn, offset, sizeof (UINT32))) {
    return MAX_UINT32;
  }

  return MmioRead32 ((UINTN)(env->Region[rgn].Base + offset));
}

void
ans_env_write32 (
  struct ans_env   *env,
  enum ans_region  rgn,
  uint32_t         offset,
  uint32_t         value
  )
{
  if (!AccessFits (env, rgn, offset, sizeof (UINT32))) {
    return;
  }

  MmioWrite32 ((UINTN)(env->Region[rgn].Base + offset), value);
}

uint64_t
ans_env_read64 (
  struct ans_env   *env,
  enum ans_region  rgn,
  uint32_t         offset
  )
{
  if (!AccessFits (env, rgn, offset, sizeof (UINT64))) {
    return MAX_UINT64;
  }

  return MmioRead64 ((UINTN)(env->Region[rgn].Base + offset));
}

void
ans_env_write64 (
  struct ans_env   *env,
  enum ans_region  rgn,
  uint32_t         offset,
  uint64_t         value
  )
{
  if (!AccessFits (env, rgn, offset, sizeof (UINT64))) {
    return;
  }

  MmioWrite64 ((UINTN)(env->Region[rgn].Base + offset), value);
}

bool
ans_env_dma_alloc (
  struct ans_env  *env,
  size_t          size,
  struct ans_dma  *dma
  )
{
  EFI_STATUS            Status;
  UINTN                 Pages;
  UINTN                 Bytes;
  VOID                  *Host;
  EFI_PHYSICAL_ADDRESS  Device;
  VOID                  *Mapping;
  volatile UINT64       *Word;
  UINTN                 Index;

  ZeroMem (dma, sizeof (*dma));

  if (env->Exiting) {
    ans_env_log (env, "env: device memory asked for inside ExitBootServices, refused");
    return false;
  }

  if ((size == 0) || (size > ANS_ENV_DMA_MAX_SIZE)) {
    ans_env_log (env, "env: device memory of 0x%llx bytes asked for, refused", (unsigned long long)size);
    return false;
  }

  Pages  = EFI_SIZE_TO_PAGES (size);
  Status = DmaAllocateAlignedBuffer (EfiBootServicesData, Pages, ANS_ENV_DMA_ALIGNMENT, &Host);
  if (EFI_ERROR (Status)) {
    ans_env_log (env, "env: DmaAllocateAlignedBuffer of %u pages failed", (UINT32)Pages);
    return false;
  }

  Bytes   = EFI_PAGES_TO_SIZE (Pages);
  Device  = 0;
  Mapping = NULL;
  Status  = DmaMap (MapOperationBusMasterCommonBuffer, Host, &Bytes, &Device, &Mapping);
  if (EFI_ERROR (Status) || (Bytes != EFI_PAGES_TO_SIZE (Pages)) || ((Device & (ANS_ENV_DMA_ALIGNMENT - 1)) != 0)) {
    ans_env_log (
      env,
      "env: DmaMap of %u pages gives 0x%llx bytes at device address 0x%llx (status 0x%llx), not used",
      (UINT32)Pages,
      (unsigned long long)Bytes,
      (unsigned long long)Device,
      (unsigned long long)Status
      );
    if (!EFI_ERROR (Status)) {
      DmaUnmap (Mapping);
    }

    DmaFreeBuffer (Pages, Host);
    return false;
  }

  Word = (volatile UINT64 *)Host;
  for (Index = 0; Index < EFI_PAGES_TO_SIZE (Pages) / sizeof (UINT64); Index++) {
    Word[Index] = 0;
  }

  dma->va       = Host;
  dma->dva      = Device;
  dma->size     = size;
  dma->env_priv = Mapping;
  return true;
}

void
ans_env_dma_free (
  struct ans_env  *env,
  struct ans_dma  *dma
  )
{
  EFI_STATUS  Status;
  VOID        *Host;
  VOID        *Mapping;
  UINTN       Pages;

  Host    = dma->va;
  Mapping = dma->env_priv;
  Pages   = EFI_SIZE_TO_PAGES (dma->size);
  ZeroMem (dma, sizeof (*dma));
  if (Host == NULL) {
    return;
  }

  if (env->Exiting) {
    return;
  }

  Status = DmaUnmap (Mapping);
  if (EFI_ERROR (Status)) {
    //
    // Better to keep the pages than to free memory whose mapping is in an
    // unknown state
    //
    ans_env_log (env, "env: DmaUnmap failed (status 0x%llx), the memory is kept", (unsigned long long)Status);
    return;
  }

  Status = DmaFreeBuffer (Pages, Host);
  if (EFI_ERROR (Status)) {
    ans_env_log (env, "env: DmaFreeBuffer failed (status 0x%llx)", (unsigned long long)Status);
  }
}

void
ans_env_udelay (
  struct ans_env  *env,
  uint32_t        usec
  )
{
  MicroSecondDelay (usec);
}

uint32_t
ans_env_wait (
  struct ans_env  *env,
  uint32_t        usec
  )
{
  if (usec == 0) {
    usec = 1;
  }

  MicroSecondDelay (usec);
  return usec;
}

/**
  The core's format for one log line, made into one for the firmware's print
  functions: Prefix in front, a newline at the end, and "%s" (a CHAR16 string
  there) as "%a". The other conversions the core uses (%d, %u, %x, %02x, %08x,
  %llu, %llx, %016llx; ans_env.h) mean the same in both.

  @retval TRUE   Out holds the format.
  @retval FALSE  It does not fit; Out holds an empty string.
**/
STATIC
BOOLEAN
AnsEnvLogFormat (
  IN  CONST CHAR8  *Format,
  OUT CHAR8        *Out,
  IN  UINTN        OutSize
  )
{
  CONST CHAR8  *Prefix;
  UINTN        Used;
  BOOLEAN      InConversion;

  Used = 0;
  for (Prefix = ANS_ENV_LOG_PREFIX; *Prefix != '\0'; Prefix++) {
    if (Used + 1 >= OutSize) {
      goto TooLong;
    }

    Out[Used++] = *Prefix;
  }

  InConversion = FALSE;
  for ( ; *Format != '\0'; Format++) {
    if (Used + 2 >= OutSize) {
      goto TooLong;
    }

    if (!InConversion) {
      InConversion = *Format == '%';
      Out[Used++]  = *Format;
    } else if (((*Format >= '0') && (*Format <= '9')) || (*Format == 'l')) {
      Out[Used++] = *Format;
    } else {
      InConversion = FALSE;
      Out[Used++]  = *Format == 's' ? 'a' : *Format;
    }
  }

  Out[Used++] = '\n';
  Out[Used]   = '\0';
  return TRUE;

TooLong:
  Out[0] = '\0';
  return FALSE;
}

void
ans_env_log (
  struct ans_env  *env,
  const char      *fmt,
  ...
  )
{
  CHAR8    Format[ANS_ENV_LOG_FORMAT_SIZE];
  VA_LIST  Marker;

  if (!AnsEnvLogFormat (fmt, Format, sizeof (Format))) {
    DEBUG ((DEBUG_INFO, ANS_ENV_LOG_PREFIX "a line of the core's log is too long to print\n"));
    return;
  }

  VA_START (Marker, fmt);
  DebugVPrint (DEBUG_INFO, Format, Marker);
  VA_END (Marker);
}
