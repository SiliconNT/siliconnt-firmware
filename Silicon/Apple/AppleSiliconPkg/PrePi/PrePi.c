/**
 * PrePi.c
 * 
 * Main implementation of SEC phase
 * 
 * PEI is not necessary in this implementation, so build up HOBs and jump right to DXE.
 * 
 * TODO: Adapt code to dynamically change PcdSystemMemorySize to use the values from FDT instead of
 * hardcoding values.
 * 
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 * 
 */


#include "PrePi.h"
#include <Pi/PiBootMode.h>
#include <Pi/PiHob.h>
#include <PiDxe.h>
#include <PiPei.h>
#include <Library/ArmLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/CacheMaintenanceLib.h>
#include <Library/DebugAgentLib.h>
#include <Library/DebugLib.h>
#include <Library/HobLib.h>
#include <Library/IoLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/PcdLib.h>
#include <Library/PeCoffLib.h>
#include <Library/PeCoffGetEntryPointLib.h>
#include <Library/PerformanceLib.h>
#include <Library/PrePiHobListPointerLib.h>
#include <Library/PrePiLib.h>
#include <Library/SerialPortLib.h>
#include <Library/AppleDTLib.h>

UINT32 InitializeUART(VOID);

VOID EFIAPI ProcessLibraryConstructorList(VOID);

//
// SiliconNT: keep a range the loader filled out of the hands of the DXE core.
// Type says what the operating system may do with it once it has taken over:
// boot services data may be reused, reserved memory must be left alone.
//
STATIC VOID ReserveLoaderRange(IN UINT64 Base, IN UINT64 Size, IN EFI_MEMORY_TYPE Type)
{
    UINT64 Start = Base & ~(UINT64)EFI_PAGE_MASK;
    UINT64 End   = ALIGN_VALUE(Base + Size, (UINT64)EFI_PAGE_SIZE);

    BuildMemoryAllocationHob(Start, End - Start, Type);
}

//
// SiliconNT: the boot disk image, if the loader put one into memory and named it
// in the device tree, also stays where it is. BootRamdiskHelperDxe registers that
// memory as a RAM disk; here it is only kept away from the DXE core's allocator.
//
// The range is reserved memory, not boot services data. Windows goes on using the
// disk after it has taken over: its loader finds the RAM disk node in the boot
// device path and hands the address to the kernel, which maps the memory as a
// disk. ACPI 6.6 section 5.2.26.2 asks for EFI reserved memory for a virtual disk
// range, and RamDiskDxe publishes the disk in the NFIT only when it finds it so.
//
// An entry that cannot be used stops the entry code, like a corrupt device tree.
// Going on would mean either handing the image's memory out as free memory or
// ending up in the UEFI Shell with the reason several hundred lines up the log.
//
STATIC VOID ReserveBootRamdisk(IN CONST APPLE_BOOT_INFO *Info)
{
    UINT64        Base;
    UINT64        Size;
    RETURN_STATUS Status;

    Status = AppleGetBootRamdisk(Info, &Base, &Size);
    if(Status == RETURN_NOT_FOUND) {
        DEBUG((EFI_D_INFO | EFI_D_LOAD, "No boot disk image: /chosen/memory-map has no RAMDisk entry\n"));
        return;
    }
    if(RETURN_ERROR(Status)) {
        DEBUG(
            (EFI_D_INFO | EFI_D_LOAD | EFI_D_ERROR,
            "PrePi: bad RAMDisk entry in /chosen/memory-map: base 0x%llx, size 0x%llx\n",
            Base, Size));
        DEBUG(
            (EFI_D_INFO | EFI_D_LOAD | EFI_D_ERROR,
            "It must be 16 bytes long, page aligned, not empty and inside memory (0x%llx, size 0x%llx); stopping here\n",
            Info->BootArgs->phys_base, Info->BootArgs->mem_size));
        CpuDeadLoop();
    }

    DEBUG((EFI_D_INFO | EFI_D_LOAD, "Boot disk image: 0x%llx (size 0x%llx)\n", Base, Size));
    ReserveLoaderRange(Base, Size, EfiReservedMemoryType);
}

//
// SiliconNT: the table of interrupt numbers m1n1 made at this boot (AppleDTLib.h,
// AppleGetAicToGic). Read here once to say in one line whether there is one, how
// many numbers it holds and its first and last pair; the code that describes
// devices to Windows reads it again. Its memory stays where the loader put it, as
// boot services data like the device tree: nothing needs it once Windows runs.
//
// A table that cannot be used does not stop the entry code. A loader of an older
// build hands over none, and without a table the firmware describes no device
// that needs an interrupt and boots as before. So the line says why, and the
// firmware goes on as if there were no table. The memory is kept all the same
// when the property names a usable range, so that a later reader finds the same
// bytes and comes to the same verdict.
//
STATIC CONST CHAR8 *AicToGicProblem(IN RETURN_STATUS Status)
{
    switch(Status) {
    case RETURN_INVALID_PARAMETER:
        return "the entry must be 16 bytes long, start at a page, give at least 8 bytes and lie inside memory";
    case RETURN_INCOMPATIBLE_VERSION:
        return "the version is not 1";
    case RETURN_BAD_BUFFER_SIZE:
        return "the count is above 988 or the size is not 8 + 4 * count";
    case RETURN_COMPROMISED_DATA:
        return "the AIC numbers are not strictly ascending";
    default:
        return "unknown";
    }
}

STATIC VOID ReserveAicToGic(IN CONST APPLE_BOOT_INFO *Info)
{
    APPLE_AIC_TO_GIC Table;
    RETURN_STATUS    Status;

    Status = AppleGetAicToGic(Info, &Table);
    if(Status == RETURN_NOT_FOUND) {
        DEBUG((EFI_D_INFO | EFI_D_LOAD, "No AIC-to-GIC table: /chosen/memory-map has no AICToGIC entry\n"));
        return;
    }
    if(Status == RETURN_INVALID_PARAMETER) {
        DEBUG(
            (EFI_D_INFO | EFI_D_LOAD | EFI_D_ERROR,
            "AIC-to-GIC table not used, going on without: base 0x%llx, size 0x%llx: %a (memory 0x%llx, size 0x%llx)\n",
            Table.Base, Table.Size, AicToGicProblem(Status), Info->BootArgs->phys_base, Info->BootArgs->mem_size));
        return;
    }

    ReserveLoaderRange(Table.Base, Table.Size, EfiBootServicesData);

    if(RETURN_ERROR(Status)) {
        DEBUG(
            (EFI_D_INFO | EFI_D_LOAD | EFI_D_ERROR,
            "AIC-to-GIC table not used, going on without: base 0x%llx, size 0x%llx, version %u, count %u: %a\n",
            Table.Base, Table.Size, Table.Version, Table.Count, AicToGicProblem(Status)));
        return;
    }
    if(Table.Count == 0) {
        DEBUG((EFI_D_INFO | EFI_D_LOAD, "AIC-to-GIC table: 0x%llx, no numbers\n", Table.Base));
        return;
    }
    DEBUG(
        (EFI_D_INFO | EFI_D_LOAD,
        "AIC-to-GIC table: 0x%llx, %u numbers, AIC %u is GIC %u ... AIC %u is GIC %u\n",
        Table.Base, Table.Count,
        Table.Aic[0], APPLE_AIC_TO_GIC_FIRST_GIC,
        Table.Aic[Table.Count - 1], APPLE_AIC_TO_GIC_FIRST_GIC + Table.Count - 1));
}

//
// SiliconNT: the boot arguments and the Apple device tree stay where the loader
// put them. Tell everything that runs later where they are, and where the serial
// port is (AppleGetBootInfo() in AppleDTLib reads this HOB; so do the serial port
// libraries of the DXE phase).
//
// Called before MemoryPeim(), which reads the device registers and the cores
// from the device tree through this HOB.
//
STATIC VOID PublishBootInfo(IN struct boot_args *BootArgs, OUT APPLE_BOOT_INFO *Info)
{
    Info->BootArgs       = BootArgs;
    Info->DeviceTree     = (dt_node_t *)((UINTN)BootArgs->devtree - BootArgs->virt_base + BootArgs->phys_base);
    Info->DeviceTreeSize = BootArgs->devtree_size;

    // EarlySetup() has read the same tree for the same address, or nothing could be printed
    if(RETURN_ERROR(AppleGetUartBase(Info, &Info->UartBase))) {
        DEBUG((EFI_D_INFO | EFI_D_LOAD | EFI_D_ERROR, "PrePi: /arm-io/uart0 of the device tree cannot be read any more; stopping here\n"));
        CpuDeadLoop();
    }

    DEBUG(
        (EFI_D_INFO | EFI_D_LOAD,
        "Boot arguments: 0x%p, device tree: 0x%p (size 0x%x), serial port: 0x%llx\n",
        Info->BootArgs, Info->DeviceTree, Info->DeviceTreeSize, Info->UartBase));

    BuildGuidDataHob(&gAppleSiliconPkgBootInfoHobGuid, Info, sizeof(*Info));
}

//
// SiliconNT: reserve the boot arguments, the device tree, the table of interrupt
// numbers and the boot disk image.
//
STATIC VOID ReserveLoaderRanges(IN CONST APPLE_BOOT_INFO *Info)
{
    ReserveLoaderRange((UINTN)Info->BootArgs, sizeof(struct boot_args), EfiBootServicesData);
    ReserveLoaderRange((UINTN)Info->DeviceTree, Info->DeviceTreeSize, EfiBootServicesData);
    ReserveAicToGic(Info);
    ReserveBootRamdisk(Info);
}

VOID Main(IN VOID *StackBase, IN UINTN StackSize, IN VOID *BootArgsPtr, IN UINT64 UefiMemoryBase)
{
    EFI_HOB_HANDOFF_INFO_TABLE  *HobList;
    EFI_STATUS Status;
    APPLE_BOOT_INFO BootInfo;

    UINTN UefiMemoryLength = FixedPcdGet32(PcdSystemMemoryUefiRegionSize);

    DEBUG(
      (EFI_D_INFO | EFI_D_LOAD,
       "UEFI Memory Base = 0x%llx, Size = 0x%llx, Stack Base = 0x%p, Stack "
       "Size = 0x%llx\n",
       UefiMemoryBase, UefiMemoryLength, StackBase, StackSize));

    DEBUG((EFI_D_INFO | EFI_D_LOAD, "Setting up DXE Hand-Off Blocks.\n"));

    HobList = HobConstructor(
        (VOID *)UefiMemoryBase,
        UefiMemoryLength,
        (VOID *)UefiMemoryBase,
        StackBase
    );

    PrePeiSetHobList(HobList);

    PublishBootInfo((struct boot_args *)BootArgsPtr, &BootInfo);

    //set up memory HOBs
    DEBUG((EFI_D_INFO | EFI_D_LOAD, "Invalidating data cache.\n"));

    InvalidateDataCacheRange((VOID *)(UINTN)PcdGet64(PcdFdBaseAddress), PcdGet32(PcdFdSize));    

    DEBUG(
        (EFI_D_INFO | EFI_D_LOAD, 
        "Beginning memory hand off block setup.\n"));
    Status = MemoryPeim(UefiMemoryBase, UefiMemoryLength);
    if(EFI_ERROR(Status))
    {
        DEBUG(
            (DEBUG_ERROR | DEBUG_INFO | DEBUG_LOAD,
            "PrePi: Memory setup failed! Status: 0x%llx \n",
            Status)
            );
        DEBUG(
            (DEBUG_ERROR | DEBUG_INFO | DEBUG_LOAD,
            "Looping forever, check serial log for failure point\n")
            );
        DEBUG(
            (DEBUG_ERROR | DEBUG_INFO | DEBUG_LOAD,
            "Issue reboot command through hypervisor shell, JTAG interface, or send VDM command to reboot\n")
            );
        CpuDeadLoop();
    }

    ReserveLoaderRanges(&BootInfo);

    //set up stack and CPU HOBs
    DEBUG((EFI_D_INFO | EFI_D_LOAD, "Building up Stack/CPU HOBs\n"));
    DEBUG((EFI_D_INFO | EFI_D_LOAD, "Stack Base: 0x%llx, Stack Size: 0x%llx\n", (UINT64)StackBase, StackSize));
    BuildStackHob((UINT64)StackBase, StackSize);
    BuildCpuHob(ArmGetPhysicalAddressBits(), PcdGet8(PcdPrePiCpuIoSize));

    DEBUG((EFI_D_INFO | EFI_D_LOAD, "Setting boot mode to default settings...\n"));
    SetBootMode(BOOT_WITH_DEFAULT_SETTINGS);

    DEBUG((EFI_D_INFO | EFI_D_LOAD, "Starting PlatformPeim\n"));
    Status = PlatformPeim();
    ASSERT_EFI_ERROR(Status);

    // SEC phase needs to run library constructors by hand.
    ProcessLibraryConstructorList();

    // Assume the FV that contains the PI (our code) also contains a compressed
    // FV.
    DEBUG((DEBUG_INFO, "Decompressing FV...\n"));
    Status = DecompressFirstFv();
    ASSERT_EFI_ERROR(Status);

    // Load the DXE Core and transfer control to it
    DEBUG(
        (EFI_D_INFO | EFI_D_LOAD,
        "Loading DXE Core now\n")
        );
    Status = LoadDxeCoreFromFv(NULL, 0);
    ASSERT_EFI_ERROR(Status);
    
    //if we reach here, something has *seriously* gone wrong
    CpuDeadLoop();
}

VOID CEntryPoint(IN VOID *StackBase, IN UINTN StackSize, IN VOID *BootArgsPtr, IN UINT64 UefiMemoryBase)
{
    Main(StackBase, StackSize, BootArgsPtr, UefiMemoryBase);
    //DXE Core should not return, if it does, something is *very* wrong
    ASSERT(FALSE);
}


UINT32 InitializeUART(VOID)
{
    SerialPortInitialize();
    DEBUG(
        (EFI_D_INFO | EFI_D_LOAD, 
        "Apple Silicon Project Mu Firmware (arm64/arm64e)\n")
        );
    DEBUG((EFI_D_INFO | EFI_D_LOAD, "If you can see this message, UART works\n"));
    DEBUG((EFI_D_INFO | EFI_D_LOAD, "FD Base Address - 0x%llx\n", PcdGet64(PcdFdBaseAddress)));
    DEBUG((EFI_D_INFO | EFI_D_LOAD, "FV Base Address - 0x%llx\n", PcdGet64(PcdFvBaseAddress)));
    return EFI_SUCCESS;
}

//borrowed from ArmVirtPkg/PrePi/PrePi.c
VOID
RelocatePeCoffImage (
  IN  EFI_PEI_FV_HANDLE         FwVolHeader,
  IN  PE_COFF_LOADER_READ_FILE  ImageRead
  )
{
  EFI_PEI_FILE_HANDLE           FileHandle;
  VOID                          *SectionData;
  PE_COFF_LOADER_IMAGE_CONTEXT  ImageContext;
  EFI_STATUS                    Status;

  FileHandle = NULL;
  Status     = FfsFindNextFile (
                 EFI_FV_FILETYPE_SECURITY_CORE,
                 FwVolHeader,
                 &FileHandle
                 );
  ASSERT_EFI_ERROR (Status);

  Status = FfsFindSectionData (EFI_SECTION_PE32, FileHandle, &SectionData);
  if (EFI_ERROR (Status)) {
    Status = FfsFindSectionData (EFI_SECTION_TE, FileHandle, &SectionData);
  }

  ASSERT_EFI_ERROR (Status);

  ZeroMem (&ImageContext, sizeof ImageContext);

  ImageContext.Handle    = (EFI_HANDLE)SectionData;
  ImageContext.ImageRead = ImageRead;
  PeCoffLoaderGetImageInfo (&ImageContext);

  if (ImageContext.ImageAddress != (UINTN)SectionData) {
    ImageContext.ImageAddress = (UINTN)SectionData;
    PeCoffLoaderRelocateImage (&ImageContext);
  }
}