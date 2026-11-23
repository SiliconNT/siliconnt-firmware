/** @file
  Host test of Drivers/SimpleFbDxe: how it lays out the framebuffer the boot
  arguments describe, and what QueryMode() hands out.

  The driver takes the bytes per line from the stride of the boot arguments and
  falls back to width * 4 only when the stride is 0. A stride it cannot describe
  as whole pixels per line, or one shorter than the width, is refused. Drawing
  goes through the real FrameBufferBltLib of MdeModulePkg into a buffer of this
  test, so a pixel is checked where it lands, and the bytes between the end of a
  line's pixels and the next line are checked to stay untouched. QueryMode()
  must hand out all of the mode information, the pixel masks too.

  Run it with ./run.sh; it needs the host compiler only.

  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include <Uefi.h>
#include <Library/AppleDTLib.h>
#include <Protocol/DevicePath.h>
#include <Protocol/GraphicsOutput.h>

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

//
// What SimpleFbDxe.c and FrameBufferBltLib.c take from the firmware's libraries
//
EFI_GUID gEfiGraphicsOutputProtocolGuid = EFI_GRAPHICS_OUTPUT_PROTOCOL_GUID;
EFI_GUID gEfiDevicePathProtocolGuid     = EFI_DEVICE_PATH_PROTOCOL_GUID;

VOID *EFIAPI CopyMem(OUT VOID *Destination, IN CONST VOID *Source, IN UINTN Length)
{
    return memmove(Destination, Source, Length);
}

VOID *EFIAPI ZeroMem(OUT VOID *Buffer, IN UINTN Length)
{
    return memset(Buffer, 0, Length);
}

VOID *EFIAPI SetMem(OUT VOID *Buffer, IN UINTN Length, IN UINT8 Value)
{
    return memset(Buffer, Value, Length);
}

VOID *EFIAPI SetMem32(OUT VOID *Buffer, IN UINTN Length, IN UINT32 Value)
{
    for(UINTN Index = 0; Index < Length / sizeof(Value); Index++) {
        memcpy((UINT8 *)Buffer + Index * sizeof(Value), &Value, sizeof(Value));
    }
    return Buffer;
}

VOID *EFIAPI SetMem64(OUT VOID *Buffer, IN UINTN Length, IN UINT64 Value)
{
    for(UINTN Index = 0; Index < Length / sizeof(Value); Index++) {
        memcpy((UINT8 *)Buffer + Index * sizeof(Value), &Value, sizeof(Value));
    }
    return Buffer;
}

INTN EFIAPI HighBitSet32(IN UINT32 Operand)
{
    return (Operand == 0) ? -1 : 31 - __builtin_clz(Operand);
}

// Pool memory comes back filled with 0xCC, as uninitialised memory might be
VOID *EFIAPI AllocatePool(IN UINTN AllocationSize)
{
    VOID *Buffer = malloc(AllocationSize);

    if(Buffer != NULL) {
        memset(Buffer, 0xCC, AllocationSize);
    }
    return Buffer;
}

STATIC EFI_STATUS EFIAPI BsAllocatePool(IN EFI_MEMORY_TYPE PoolType, IN UINTN Size, OUT VOID **Buffer)
{
    *Buffer = AllocatePool(Size);
    return (*Buffer == NULL) ? EFI_OUT_OF_RESOURCES : EFI_SUCCESS;
}

STATIC EFI_TPL EFIAPI BsRaiseTpl(IN EFI_TPL NewTpl)
{
    return TPL_APPLICATION;
}

STATIC VOID EFIAPI BsRestoreTpl(IN EFI_TPL OldTpl)
{
}

// The graphics output protocol the driver installs, NULL until it installs one
STATIC EFI_GRAPHICS_OUTPUT_PROTOCOL *mInstalledGop;

STATIC EFI_STATUS EFIAPI BsInstallMultipleProtocolInterfaces(IN OUT EFI_HANDLE *Handle, ...)
{
    va_list   Args;
    EFI_GUID *Guid;

    va_start(Args, Handle);
    while((Guid = va_arg(Args, EFI_GUID *)) != NULL) {
        VOID *Interface = va_arg(Args, VOID *);
        if(memcmp(Guid, &gEfiGraphicsOutputProtocolGuid, sizeof(*Guid)) == 0) {
            mInstalledGop = Interface;
        }
    }
    va_end(Args);
    return EFI_SUCCESS;
}

STATIC EFI_BOOT_SERVICES mBootServices = {
    .AllocatePool                     = BsAllocatePool,
    .RaiseTPL                         = BsRaiseTpl,
    .RestoreTPL                       = BsRestoreTpl,
    .InstallMultipleProtocolInterfaces = BsInstallMultipleProtocolInterfaces,
};
EFI_BOOT_SERVICES *gBS = &mBootServices;

// The boot arguments m1n1 hands over, only the framebuffer filled in
STATIC struct boot_args mBootArgs;
STATIC APPLE_BOOT_INFO  mBootInfo = { .BootArgs = &mBootArgs };

APPLE_BOOT_INFO *AppleGetBootInfo(VOID)
{
    return &mBootInfo;
}

//
// The driver and the drawing library. Included, not compiled on their own: the
// firmware build declares the GUIDs above in a generated header.
//
#include "../../Drivers/SimpleFbDxe/SimpleFbDxe.c"
#include "../../../../../MU_BASECORE/MdeModulePkg/Library/FrameBufferBltLib/FrameBufferBltLib.c"

//
// Checks
//
STATIC UINTN  mChecks;
STATIC UINTN  mFailures;

STATIC BOOLEAN Expect(IN BOOLEAN Ok, IN CONST CHAR8 *What)
{
    mChecks++;
    printf("%s  %s\n", Ok ? "ok  " : "FAIL", What);
    if(!Ok) {
        mFailures++;
    }
    return Ok;
}

STATIC int Verdict(VOID)
{
    if(mFailures != 0) {
        printf("%u of %u check(s) failed\n", (unsigned)mFailures, (unsigned)mChecks);
        return 1;
    }
    printf("all %u checks passed\n", (unsigned)mChecks);
    return 0;
}

//
// The framebuffer: Stride * Height bytes, filled with a byte no drawing in this
// test writes
//
#define UNTOUCHED  0x5A

STATIC UINT8 *mFb;

STATIC VOID Boot(IN UINT32 Width, IN UINT32 Height, IN UINT64 Stride, IN UINT64 BufferSize)
{
    free(mFb);
    mFb = malloc(BufferSize);
    memset(mFb, UNTOUCHED, BufferSize);
    memset(&mBootArgs, 0, sizeof(mBootArgs));
    mBootArgs.video.base   = (UINT64)(UINTN)mFb;
    mBootArgs.video.width  = Width;
    mBootArgs.video.height = Height;
    mBootArgs.video.stride = Stride;
    mBootArgs.video.depth  = 30;
    mInstalledGop          = NULL;
}

// Every pixel of the screen is Pixel, every byte past the pixels of a line untouched
STATIC BOOLEAN ScreenIs(IN UINT32 Width, IN UINT32 Height, IN UINT64 LineLength, IN UINT32 Pixel)
{
    for(UINT32 Y = 0; Y < Height; Y++) {
        CONST UINT8 *Line = mFb + Y * LineLength;
        for(UINT32 X = 0; X < Width; X++) {
            UINT32 Value;
            memcpy(&Value, Line + X * 4, 4);
            if(Value != Pixel) {
                return FALSE;
            }
        }
        for(UINT64 Byte = (UINT64)Width * 4; Byte < LineLength; Byte++) {
            if(Line[Byte] != UNTOUCHED) {
                return FALSE;
            }
        }
    }
    return TRUE;
}

STATIC UINT32 PixelAt(IN UINT64 LineLength, IN UINT32 X, IN UINT32 Y)
{
    UINT32 Value;

    memcpy(&Value, mFb + Y * LineLength + X * 4, 4);
    return Value;
}

//
// One framebuffer the driver accepts: the mode it reports, a fill of the whole
// screen, one pixel put into the last line, and the mode QueryMode() hands out
//
STATIC VOID CheckScreen(IN CONST CHAR8 *Case, IN UINT32 Width, IN UINT32 Height, IN UINT64 Stride, IN UINT64 LineLength)
{
    CHAR8                                 What[256];
    EFI_GRAPHICS_OUTPUT_PROTOCOL         *Gop;
    EFI_GRAPHICS_OUTPUT_MODE_INFORMATION *Info;
    UINTN                                 SizeOfInfo;
    EFI_GRAPHICS_OUTPUT_BLT_PIXEL         Grey  = { .Blue = 0x40, .Green = 0x80, .Red = 0xC0 };
    EFI_GRAPHICS_OUTPUT_BLT_PIXEL         White = { .Blue = 0xFF, .Green = 0xFF, .Red = 0xFF };

    Boot(Width, Height, Stride, LineLength * Height);

    snprintf(What, sizeof(What), "%s: the driver starts and installs the protocol", Case);
    if(!Expect(SimpleFbDxeInitialize(NULL, NULL) == EFI_SUCCESS && mInstalledGop != NULL, What)) {
        return;
    }
    Gop = mInstalledGop;

    snprintf(What, sizeof(What), "%s: %u x %u, %llu pixels per line, %llu bytes of framebuffer at the base",
             Case, (unsigned)Width, (unsigned)Height, (unsigned long long)(LineLength / 4),
             (unsigned long long)(LineLength * Height));
    Expect(Gop->Mode->Info->HorizontalResolution == Width && Gop->Mode->Info->VerticalResolution == Height &&
           Gop->Mode->Info->PixelsPerScanLine == LineLength / 4 && Gop->Mode->FrameBufferSize == LineLength * Height &&
           Gop->Mode->FrameBufferBase == (EFI_PHYSICAL_ADDRESS)(UINTN)mFb &&
           Gop->Mode->Info->PixelFormat == PixelBlueGreenRedReserved8BitPerColor, What);

    snprintf(What, sizeof(What), "%s: a fill reaches every pixel and no byte past the pixels of a line", Case);
    Expect(Gop->Blt(Gop, &Grey, EfiBltVideoFill, 0, 0, 0, 0, Width, Height, 0) == EFI_SUCCESS &&
           ScreenIs(Width, Height, LineLength, 0x00C08040), What);

    snprintf(What, sizeof(What), "%s: the last pixel of the last line lands at base + (height - 1) * stride + (width - 1) * 4", Case);
    Expect(Gop->Blt(Gop, &White, EfiBltBufferToVideo, 0, 0, Width - 1, Height - 1, 1, 1, 0) == EFI_SUCCESS &&
           PixelAt(LineLength, Width - 1, Height - 1) == 0x00FFFFFF && PixelAt(LineLength, Width - 2, Height - 1) == 0x00C08040,
           What);

    //
    // Pixel masks of a bit-mask format, which this driver does not report itself:
    // QueryMode() has to copy them along with the rest
    //
    Gop->Mode->Info->PixelInformation.RedMask      = 0x3FF00000;
    Gop->Mode->Info->PixelInformation.GreenMask    = 0x000FFC00;
    Gop->Mode->Info->PixelInformation.BlueMask     = 0x000003FF;
    Gop->Mode->Info->PixelInformation.ReservedMask = 0xC0000000;
    snprintf(What, sizeof(What), "%s: QueryMode() hands out all of the mode information, the pixel masks too", Case);
    Expect(Gop->QueryMode(Gop, 0, &SizeOfInfo, &Info) == EFI_SUCCESS && SizeOfInfo == sizeof(*Info) &&
           memcmp(Info, Gop->Mode->Info, sizeof(*Info)) == 0, What);
}

STATIC VOID CheckRefused(IN CONST CHAR8 *Case, IN UINT32 Width, IN UINT32 Height, IN UINT64 Stride)
{
    CHAR8 What[256];

    Boot(Width, Height, Stride, (UINT64)Width * 4 * Height);
    snprintf(What, sizeof(What), "%s: the driver refuses it and installs nothing", Case);
    Expect(SimpleFbDxeInitialize(NULL, NULL) == EFI_DEVICE_ERROR && mInstalledGop == NULL, What);
}

int main(void)
{
    // The M4 MacBook Pro: what iBoot hands over (R9a), stride = width * 4
    CheckScreen("stride = width * 4 (M4 MacBook Pro, 3024 x 1964, stride 0x2f40)", 3024, 1964, 0x2f40, 0x2f40);
    // Lines padded past the width: a line starts every stride bytes, not every width * 4
    CheckScreen("stride > width * 4 (1000 x 8, stride 4352)", 1000, 8, 4352, 4352);
    // No stride: width * 4, as before the driver read the stride
    CheckScreen("stride 0 (640 x 8)", 640, 8, 0, 640 * 4);

    CheckRefused("stride < width * 4 (100 x 8, stride 396)", 100, 8, 396);
    CheckRefused("stride not whole pixels (100 x 8, stride 402)", 100, 8, 402);

    return Verdict();
}
