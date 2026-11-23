/** @file
  The internal disk of an Apple silicon Mac, for the firmware: read-only, the
  first namespace only, polled.

  The disk controller (ANS) is driven by the core of siliconnt-drivers
  (ans/core), the same code the Windows driver runs; this file and AnsEnv.c are
  the firmware's side of it.

  At load    The controller is read from the device tree the way the ACPI
             node is (AppleGetAns(), AppleGetAnsSartVersion(),
             AppleGetAnsPmgr()) and brought up. Without a controller in the
             tree, or when it does not come up, nothing is published and the
             driver unloads; a failed start is not tried again. Otherwise one
             handle gets EFI_BLOCK_IO_PROTOCOL and a device path that is the
             same on every boot, VenHw(<this driver's GUID>)/NVMe(0x1,0-0-0-0-
             0-0-0-0); the partition and FAT drivers take it from there.
  Reads      Split into reads of at most ANS_DISK_MAX_TRANSFER bytes. Each goes
             through DmaMap(MapOperationBusMasterWrite) and DmaUnmap() of
             NonCoherentDmaLib, which invalidates the data cache over the
             memory the disk wrote (DC IVAC) before the data is handed on
             (SiliconNT findings/0063, conclusion 33). The completion is
             polled with ans_isr() and ans_mbox_service().
  Writes     Refused with EFI_WRITE_PROTECTED. The core cannot write either:
             write commands are not on its whitelist.
  ExitBootServices
             The controller is brought down (ans_stop()) for the next owner,
             the Windows driver. Nothing is allocated or freed there: the
             environment keeps the device memory (AnsEnv.h), and waits spin on
             the generic timer.

  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include <Uefi.h>

#include <Guid/EventGroup.h>
#include <Protocol/BlockIo.h>
#include <Protocol/DevicePath.h>

#include <Library/AppleDTLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>
#include <Library/DmaLib.h>
#include <Library/TimerLib.h>
#include <Library/UefiBootServicesTableLib.h>

#include "AnsEnv.h"

//
// The largest read handed to the controller: what the Windows driver takes
// (siliconnt-drivers ans/adapter.h, ANS_MAX_TRANSFER_BYTES; findings/0071
// conclusion 19), so the firmware comes up on every controller the Windows
// driver comes up on. The M4's takes 1 MiB (findings/0072).
//
#define ANS_DISK_MAX_TRANSFER  (63 * SIZE_4KB)

//
// One read at a time, always with the same tag
//
#define ANS_DISK_IO_DEPTH  1
#define ANS_DISK_READ_TAG  0

//
// How long a read may take: what m1n1 gives every NVMe command (NVME_TIMEOUT in
// src/nvme.c), and the core its admin commands
//
#define ANS_DISK_READ_TIMEOUT_US  1000000

//
// How often a read in flight is looked at
//
#define ANS_DISK_POLL_US  10

//
// The first piece of a read must be 4-byte aligned (the PRP rule, ans_core.h);
// DmaMap() of a buffer that is not aligned to a cache line bounces it through an
// aligned one
//
#define ANS_DISK_IO_ALIGN  4

#define ANS_DISK_SIGNATURE  SIGNATURE_32 ('A', 'N', 'S', 'D')

typedef struct {
  UINT32                   Signature;
  struct ans_env           Env;
  struct ans_core          Core;
  EFI_BLOCK_IO_MEDIA       Media;
  EFI_BLOCK_IO_PROTOCOL    BlockIo;
  EFI_HANDLE               Handle;
  EFI_EVENT                ExitBootServicesEvent;

  //
  // The read in flight: set by AnsDiskReadDone()
  //
  BOOLEAN                  ReadDone;
  INT32                    ReadStatus;
} ANS_DISK;

#define ANS_DISK_FROM_BLOCK_IO(a)  CR (a, ANS_DISK, BlockIo, ANS_DISK_SIGNATURE)

#pragma pack(1)
typedef struct {
  VENDOR_DEVICE_PATH            Controller;
  NVME_NAMESPACE_DEVICE_PATH    Namespace;
  EFI_DEVICE_PATH_PROTOCOL      End;
} ANS_DISK_DEVICE_PATH;
#pragma pack()

STATIC ANS_DISK_DEVICE_PATH  mAnsDiskDevicePath = {
  {
    { HARDWARE_DEVICE_PATH, HW_VENDOR_DP, { sizeof (VENDOR_DEVICE_PATH), 0 }
    },
    // The internal disk controller of an Apple silicon Mac, as this driver drives it
    { 0x28206808, 0xfea0, 0x41d8, { 0xb2, 0x9b, 0x5c, 0x34, 0x77, 0x3e, 0xaa, 0x21 }
    }
  },
  {
    { MESSAGING_DEVICE_PATH, MSG_NVME_NAMESPACE_DP, { sizeof (NVME_NAMESPACE_DEVICE_PATH), 0 }
    },
    1,    // the namespace
    0     // its EUI-64: not read
  },
  { END_DEVICE_PATH_TYPE, END_ENTIRE_DEVICE_PATH_SUBTYPE, { sizeof (EFI_DEVICE_PATH_PROTOCOL), 0 }
  }
};

STATIC ANS_DISK  mAnsDisk;

/**
  Called from ans_isr() for the read that completed.
**/
STATIC
void
AnsDiskReadDone (
  void      *ctx,
  uint16_t  tag,
  int       status
  )
{
  ANS_DISK  *Disk;

  Disk = ctx;
  if (tag != ANS_DISK_READ_TAG) {
    DEBUG ((DEBUG_ERROR, "AnsBlockIoDxe: completion for tag %u, which was not sent\n", tag));
    return;
  }

  Disk->ReadDone   = TRUE;
  Disk->ReadStatus = status;
}

/**
  One read handed to the controller: at most Bytes bytes from Lba into Buffer,
  a whole number of blocks and at most ANS_DISK_MAX_TRANSFER. *Done is how many
  bytes were read: Bytes, or fewer when DmaMap() maps only part of the buffer.

  @retval EFI_SUCCESS       *Done bytes were read.
  @retval EFI_DEVICE_ERROR  Nothing was read; the log says why.
**/
STATIC
EFI_STATUS
AnsDiskReadOnce (
  IN  ANS_DISK  *Disk,
  IN  EFI_LBA   Lba,
  IN  UINTN     Bytes,
  OUT VOID      *Buffer,
  OUT UINTN     *Done
  )
{
  EFI_STATUS            Status;
  UINTN                 Mapped;
  EFI_PHYSICAL_ADDRESS  Device;
  VOID                  *Mapping;
  UINT32                Blocks;
  struct ans_segment    Segment;
  int                   Result;
  UINT32                Waited;
  enum ans_state        State;

  *Done = 0;

  Mapped  = Bytes;
  Device  = 0;
  Mapping = NULL;
  Status  = DmaMap (MapOperationBusMasterWrite, Buffer, &Mapped, &Device, &Mapping);
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "AnsBlockIoDxe: DmaMap of 0x%lx bytes at %p failed: %r\n", (UINT64)Bytes, Buffer, Status));
    return EFI_DEVICE_ERROR;
  }

  Blocks = (UINT32)(MIN (Mapped, Bytes) / Disk->Media.BlockSize);
  if (Blocks == 0) {
    DEBUG ((DEBUG_ERROR, "AnsBlockIoDxe: DmaMap maps 0x%lx bytes at %p, less than a block\n", (UINT64)Mapped, Buffer));
    DmaUnmap (Mapping);
    return EFI_DEVICE_ERROR;
  }

  Segment.addr     = Device;
  Segment.len      = Blocks * Disk->Media.BlockSize;
  Disk->ReadDone   = FALSE;
  Disk->ReadStatus = 0;
  Result           = ans_read (&Disk->Core, ANS_DISK_READ_TAG, Lba, Blocks, &Segment, 1);
  if (Result != ANS_OK) {
    DEBUG ((DEBUG_ERROR, "AnsBlockIoDxe: read of %u blocks at %lu refused (%d)\n", Blocks, Lba, Result));
    DmaUnmap (Mapping);
    return EFI_DEVICE_ERROR;
  }

  //
  // Until it completes, the controller stops, or the time is up. A read that
  // does not complete in time is stopped with the controller before its memory
  // is unmapped: the controller must not write into it afterwards.
  //
  for (Waited = 0; ; Waited += ANS_DISK_POLL_US) {
    ans_isr (&Disk->Core, AnsDiskReadDone, Disk);
    ans_mbox_service (&Disk->Core);
    if (Disk->ReadDone) {
      break;
    }

    State = ans_get_state (&Disk->Core);
    if (State != ANS_READY) {
      DEBUG ((DEBUG_ERROR, "AnsBlockIoDxe: the controller stopped during a read (state %u)\n", (UINT32)State));
      break;
    }

    if (Waited >= ANS_DISK_READ_TIMEOUT_US) {
      DEBUG ((DEBUG_ERROR, "AnsBlockIoDxe: read of %u blocks at %lu not done in %u us, stopping the controller\n", Blocks, Lba, Waited));
      ans_halt (&Disk->Core);
      break;
    }

    MicroSecondDelay (ANS_DISK_POLL_US);
  }

  //
  // Here the cache lines over what the disk wrote are invalidated
  //
  Status = DmaUnmap (Mapping);

  if (!Disk->ReadDone) {
    return EFI_DEVICE_ERROR;
  }

  if (Disk->ReadStatus != 0) {
    DEBUG ((DEBUG_ERROR, "AnsBlockIoDxe: read of %u blocks at %lu failed, NVMe status 0x%x\n", Blocks, Lba, (UINT32)Disk->ReadStatus));
    return EFI_DEVICE_ERROR;
  }

  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "AnsBlockIoDxe: DmaUnmap after a read failed: %r\n", Status));
    return EFI_DEVICE_ERROR;
  }

  *Done = Segment.len;
  return EFI_SUCCESS;
}

/**
  BufferSize bytes from Lba on into Buffer, split into reads the controller
  takes. The arguments have been checked.
**/
STATIC
EFI_STATUS
AnsDiskRead (
  IN  ANS_DISK  *Disk,
  IN  EFI_LBA   Lba,
  IN  UINTN     BufferSize,
  OUT UINT8     *Buffer
  )
{
  EFI_STATUS  Status;
  UINTN       MaxBytes;
  UINTN       Done;

  if (ans_get_state (&Disk->Core) != ANS_READY) {
    return EFI_DEVICE_ERROR;
  }

  MaxBytes = ANS_DISK_MAX_TRANSFER / Disk->Media.BlockSize * Disk->Media.BlockSize;
  while (BufferSize > 0) {
    Status = AnsDiskReadOnce (Disk, Lba, MIN (BufferSize, MaxBytes), Buffer, &Done);
    if (EFI_ERROR (Status)) {
      return Status;
    }

    Lba        += Done / Disk->Media.BlockSize;
    Buffer     += Done;
    BufferSize -= Done;
  }

  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
EFIAPI
AnsDiskReset (
  IN EFI_BLOCK_IO_PROTOCOL  *This,
  IN BOOLEAN                ExtendedVerification
  )
{
  return EFI_SUCCESS;
}

/**
  EFI_BLOCK_IO_PROTOCOL.ReadBlocks(): the arguments are checked in the order of
  MdeModulePkg's NvmExpressDxe (NvmeBlockIoReadBlocks()).
**/
STATIC
EFI_STATUS
EFIAPI
AnsDiskReadBlocks (
  IN  EFI_BLOCK_IO_PROTOCOL  *This,
  IN  UINT32                 MediaId,
  IN  EFI_LBA                Lba,
  IN  UINTN                  BufferSize,
  OUT VOID                   *Buffer
  )
{
  ANS_DISK    *Disk;
  UINTN       Blocks;
  EFI_TPL     OldTpl;
  EFI_STATUS  Status;

  Disk = ANS_DISK_FROM_BLOCK_IO (This);

  if (MediaId != Disk->Media.MediaId) {
    return EFI_MEDIA_CHANGED;
  }

  if (Buffer == NULL) {
    return EFI_INVALID_PARAMETER;
  }

  if (BufferSize == 0) {
    return EFI_SUCCESS;
  }

  if ((BufferSize % Disk->Media.BlockSize) != 0) {
    return EFI_BAD_BUFFER_SIZE;
  }

  Blocks = BufferSize / Disk->Media.BlockSize;
  if ((Lba > Disk->Media.LastBlock) || (Blocks - 1 > Disk->Media.LastBlock - Lba)) {
    return EFI_INVALID_PARAMETER;
  }

  if (((UINTN)Buffer & (Disk->Media.IoAlign - 1)) != 0) {
    return EFI_INVALID_PARAMETER;
  }

  //
  // One caller in the core at a time
  //
  OldTpl = gBS->RaiseTPL (TPL_CALLBACK);
  Status = AnsDiskRead (Disk, Lba, BufferSize, Buffer);
  gBS->RestoreTPL (OldTpl);
  return Status;
}

STATIC
EFI_STATUS
EFIAPI
AnsDiskWriteBlocks (
  IN EFI_BLOCK_IO_PROTOCOL  *This,
  IN UINT32                 MediaId,
  IN EFI_LBA                Lba,
  IN UINTN                  BufferSize,
  IN VOID                   *Buffer
  )
{
  return EFI_WRITE_PROTECTED;
}

STATIC
EFI_STATUS
EFIAPI
AnsDiskFlushBlocks (
  IN EFI_BLOCK_IO_PROTOCOL  *This
  )
{
  return EFI_SUCCESS;
}

/**
  ExitBootServices: brings the controller down for the next owner. Allocates and
  frees nothing; the environment keeps the device memory.
**/
STATIC
VOID
EFIAPI
AnsDiskExitBootServices (
  IN EFI_EVENT  Event,
  IN VOID       *Context
  )
{
  ANS_DISK  *Disk;

  Disk              = Context;
  Disk->Env.Exiting = TRUE;
  DEBUG ((DEBUG_INFO, "AnsBlockIoDxe: ExitBootServices, bringing the disk controller down\n"));
  ans_stop (&Disk->Core);
}

/**
  Reads the controller from the device tree and brings it up; fills in the
  media and the protocol.

  @retval EFI_SUCCESS       The controller is up.
  @retval EFI_NOT_FOUND     The device tree has no controller this driver can
                            use; the controller has not been touched.
  @retval EFI_DEVICE_ERROR  The controller did not come up, or has no blocks.
                            It has been brought down again.
**/
STATIC
EFI_STATUS
AnsDiskStart (
  IN     CONST APPLE_BOOT_INFO  *BootInfo,
  IN OUT ANS_DISK               *Disk
  )
{
  RETURN_STATUS          Status;
  APPLE_ANS              Ans;
  UINT32                 SartVersion;
  APPLE_MMIO_RANGE       Pmgr;
  struct ans_config      Config;
  CONST struct ans_info  *Info;
  int                    Result;

  Status = AppleGetAns (BootInfo, &Ans);
  if (RETURN_ERROR (Status)) {
    DEBUG ((DEBUG_INFO, "AnsBlockIoDxe: no internal disk: /arm-io/ans or /arm-io/sart-ans in the device tree cannot be read: %r\n", Status));
    return EFI_NOT_FOUND;
  }

  Status = AppleGetAnsSartVersion (BootInfo, &SartVersion);
  if (RETURN_ERROR (Status)) {
    DEBUG ((DEBUG_WARN, "AnsBlockIoDxe: no internal disk: the version of its SART cannot be read from /arm-io/sart-ans: %r\n", Status));
    return EFI_NOT_FOUND;
  }

  Status = AppleGetAnsPmgr (BootInfo, &Ans, &Pmgr);
  if (RETURN_ERROR (Status)) {
    DEBUG ((DEBUG_WARN, "AnsBlockIoDxe: no internal disk: its register in the power manager cannot be found in the device tree: %r\n", Status));
    return EFI_NOT_FOUND;
  }

  Disk->Env.Region[ANS_RGN_NVME].Base = Ans.Nvme.Base;
  Disk->Env.Region[ANS_RGN_NVME].Size = Ans.Nvme.Size;
  Disk->Env.Region[ANS_RGN_ASC].Base  = Ans.Coprocessor.Base;
  Disk->Env.Region[ANS_RGN_ASC].Size  = Ans.Coprocessor.Size;
  Disk->Env.Region[ANS_RGN_SART].Base = Ans.Sart.Base;
  Disk->Env.Region[ANS_RGN_SART].Size = Ans.Sart.Size;
  Disk->Env.Region[ANS_RGN_PMGR].Base = Pmgr.Base;
  Disk->Env.Region[ANS_RGN_PMGR].Size = Pmgr.Size;

  ZeroMem (&Config, sizeof (Config));
  Config.ioq_registers = Ans.SecureBar;
  Config.sart_version  = SartVersion;
  Config.io_depth      = ANS_DISK_IO_DEPTH;
  Config.max_transfer  = ANS_DISK_MAX_TRANSFER;

  DEBUG ((
    DEBUG_INFO,
    "AnsBlockIoDxe: disk controller: NVMe 0x%lx+0x%lx, coprocessor 0x%lx+0x%lx, SART 0x%lx+0x%lx, power manager 0x%lx+0x%lx, %a, SART version %u\n",
    Ans.Nvme.Base,
    Ans.Nvme.Size,
    Ans.Coprocessor.Base,
    Ans.Coprocessor.Size,
    Ans.Sart.Base,
    Ans.Sart.Size,
    Pmgr.Base,
    Pmgr.Size,
    Ans.SecureBar ? "nvme-secure-bar" : "no nvme-secure-bar",
    SartVersion
    ));

  Result = ans_start (&Disk->Core, &Disk->Env, &Config);
  if (Result != ANS_OK) {
    DEBUG ((DEBUG_ERROR, "AnsBlockIoDxe: no internal disk: the controller did not come up (%d)\n", Result));
    return EFI_DEVICE_ERROR;
  }

  Info = ans_get_info (&Disk->Core);
  if (Info->blocks == 0) {
    DEBUG ((DEBUG_ERROR, "AnsBlockIoDxe: no internal disk: namespace 1 has no blocks\n"));
    ans_stop (&Disk->Core);
    return EFI_DEVICE_ERROR;
  }

  Disk->Media.MediaId                       = 0;
  Disk->Media.RemovableMedia                = FALSE;
  Disk->Media.MediaPresent                  = TRUE;
  Disk->Media.LogicalPartition              = FALSE;
  Disk->Media.ReadOnly                      = TRUE;
  Disk->Media.WriteCaching                  = FALSE;
  Disk->Media.BlockSize                     = Info->block_size;
  Disk->Media.IoAlign                       = ANS_DISK_IO_ALIGN;
  Disk->Media.LastBlock                     = Info->blocks - 1;
  Disk->Media.LowestAlignedLba              = 0;
  Disk->Media.LogicalBlocksPerPhysicalBlock = 1;

  Disk->BlockIo.Revision    = EFI_BLOCK_IO_PROTOCOL_REVISION2;
  Disk->BlockIo.Media       = &Disk->Media;
  Disk->BlockIo.Reset       = AnsDiskReset;
  Disk->BlockIo.ReadBlocks  = AnsDiskReadBlocks;
  Disk->BlockIo.WriteBlocks = AnsDiskWriteBlocks;
  Disk->BlockIo.FlushBlocks = AnsDiskFlushBlocks;

  DEBUG ((DEBUG_INFO, "AnsBlockIoDxe: internal disk: %lu blocks of %u bytes, read-only\n", Info->blocks, Info->block_size));
  return EFI_SUCCESS;
}

/**
  Brings the controller up and publishes the disk: the stop at ExitBootServices
  first, then the protocols. Publishes nothing when any step fails.
**/
STATIC
EFI_STATUS
AnsDiskPublish (
  IN     CONST APPLE_BOOT_INFO  *BootInfo,
  IN OUT ANS_DISK               *Disk
  )
{
  EFI_STATUS  Status;

  ZeroMem (Disk, sizeof (*Disk));
  Disk->Signature = ANS_DISK_SIGNATURE;

  Status = AnsDiskStart (BootInfo, Disk);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Status = gBS->CreateEventEx (
                  EVT_NOTIFY_SIGNAL,
                  TPL_NOTIFY,
                  AnsDiskExitBootServices,
                  Disk,
                  &gEfiEventExitBootServicesGuid,
                  &Disk->ExitBootServicesEvent
                  );
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "AnsBlockIoDxe: cannot create the ExitBootServices event: %r\n", Status));
    ans_stop (&Disk->Core);
    return Status;
  }

  Status = gBS->InstallMultipleProtocolInterfaces (
                  &Disk->Handle,
                  &gEfiDevicePathProtocolGuid,
                  &mAnsDiskDevicePath,
                  &gEfiBlockIoProtocolGuid,
                  &Disk->BlockIo,
                  NULL
                  );
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "AnsBlockIoDxe: cannot install the protocols: %r\n", Status));
    gBS->CloseEvent (Disk->ExitBootServicesEvent);
    ans_stop (&Disk->Core);
    return Status;
  }

  return EFI_SUCCESS;
}

EFI_STATUS
EFIAPI
AnsBlockIoDxeEntryPoint (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  return AnsDiskPublish (AppleGetBootInfo (), &mAnsDisk);
}
