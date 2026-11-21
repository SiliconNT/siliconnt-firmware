/**
 * @file MemoryInitPeiLib.c
 * 
 * @author amarioguy (Arminder Singh)
 * 
 * This file implements page table setup, memory HOB setup, and MMU initialization.
 * Adapted from SurfaceDuoPkg/MemoryInitPeiLib.c
 * 
 * @version 1.0
 * @date 2022-07-31
 * 
 * @copyright Copyright (c) amarioguy (Arminder Singh) 2022.
 * 
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 * 
 * SiliconNT: taken from NT-for-ASi/apple_silicon_platforms_mu (861dcc490c56),
 * Silicon/Apple/T602XFamilyPkg/Library/MemoryInitPeiLib. The M2 Pro device
 * ranges were replaced by the M4 MacBook Pro (T8132) ones; the "cpm-impl-reg"
 * reservation loop and its helper were removed.
 *
 **/

#include <PiPei.h>

#include <Library/ArmMmuLib.h>
#include <Library/ArmPlatformLib.h>
#include <Library/DebugLib.h>
#include <Library/HobLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/PcdLib.h>
#include <Library/IoLib.h>

//
// Device ranges of the M4 MacBook Pro as the firmware sees them under the EL2 layer.
//
// MMIO: the 27 "ranges" entries of the Apple device tree node /arm-io span
// 0x2_0000_0000 .. 0x1c_c000_0000 (SiliconNT docs/findings/0017, section 1).
// GIC: emulated by the EL2 layer, not real hardware (SiliconNT docs/DESIGN.md).
//
#define M4_MMIO_BASE              0x200000000ULL
#define M4_MMIO_SIZE              (0x1cc0000000ULL - M4_MMIO_BASE)
#define M4_GIC_REDIST_FRAME_SIZE  0x20000ULL
#define M4_GIC_DIST_SIZE          0x10000ULL

#define MAX_VIRTUAL_MEMORY_MAP_DESCRIPTORS 6

#define DDR_ATTRIBUTES_CACHED           ARM_MEMORY_REGION_ATTRIBUTE_WRITE_BACK
#define DDR_ATTRIBUTES_UNCACHED         ARM_MEMORY_REGION_ATTRIBUTE_UNCACHED_UNBUFFERED

VOID BuildMemoryTypeInformationHob(VOID);

VOID BuildVirtualMemoryMap(OUT ARM_MEMORY_REGION_DESCRIPTOR **VirtualMemoryMap);

STATIC VOID InitMmu(IN ARM_MEMORY_REGION_DESCRIPTOR *MemoryTable)
{
    VOID *MemoryTranslationTableBase;
    UINTN MemoryTranslationTableSize;
    RETURN_STATUS StatusCode;

    DEBUG(
        (DEBUG_INFO, 
        "MemoryInitPeiLib: Enabling MMU, Page Table Base: 0x%p, Page Table Size: 0x%p\n", 
        &MemoryTranslationTableBase, &MemoryTranslationTableSize)
        );
    StatusCode = ArmConfigureMmu(MemoryTable, &MemoryTranslationTableBase, &MemoryTranslationTableSize);
    DEBUG((DEBUG_INFO, "MMU enable successful\n"));

    if(EFI_ERROR(StatusCode))
    {
        DEBUG((DEBUG_ERROR | DEBUG_INFO, "MemoryInitPeiLib: MMU enable failed!! Status: %llx\n", StatusCode));
    }
}


//Borrowed from ArmPlatformPkg
EFI_STATUS EFIAPI MemoryPeim(IN EFI_PHYSICAL_ADDRESS UefiMemoryBase, IN UINT64 UefiMemorySize)
{
  ARM_MEMORY_REGION_DESCRIPTOR  *MemoryTable;
  EFI_RESOURCE_ATTRIBUTE_TYPE   ResourceAttributes;
  UINT64                        ResourceLength;
  EFI_PEI_HOB_POINTERS          NextHob;
  EFI_PHYSICAL_ADDRESS          FdTop;
  EFI_PHYSICAL_ADDRESS          SystemMemoryTop;
  EFI_PHYSICAL_ADDRESS          ResourceTop;
  BOOLEAN                       Found;

  DEBUG((DEBUG_INFO, "%a: Building VirtualMemoryMap\n", __FUNCTION__));
  // build up virtual memory map
  BuildVirtualMemoryMap(&MemoryTable);

  // Ensure PcdSystemMemorySize has been set
  ASSERT (PcdGet64 (PcdSystemMemorySize) != 0);

  //
  // Now, the permanent memory has been installed, we can call AllocatePages()
  //

  DEBUG((DEBUG_INFO, "%a: Building VirtualMemoryMap\n", __FUNCTION__));
  ResourceAttributes = (
                        EFI_RESOURCE_ATTRIBUTE_PRESENT |
                        EFI_RESOURCE_ATTRIBUTE_INITIALIZED |
                        EFI_RESOURCE_ATTRIBUTE_WRITE_COMBINEABLE |
                        EFI_RESOURCE_ATTRIBUTE_WRITE_THROUGH_CACHEABLE |
                        EFI_RESOURCE_ATTRIBUTE_WRITE_BACK_CACHEABLE |
                        EFI_RESOURCE_ATTRIBUTE_TESTED
                        );

  DEBUG((DEBUG_INFO, "%a: Resource Attributes: 0x%lx\n", __FUNCTION__, ResourceAttributes));
  //
  // Check if the resource for the main system memory has been declared
  //
  Found       = FALSE;
  NextHob.Raw = GetHobList ();
  while ((NextHob.Raw = GetNextHob (EFI_HOB_TYPE_RESOURCE_DESCRIPTOR, NextHob.Raw)) != NULL) {
    if ((NextHob.ResourceDescriptor->ResourceType == EFI_RESOURCE_SYSTEM_MEMORY) &&
        (PcdGet64 (PcdSystemMemoryBase) >= NextHob.ResourceDescriptor->PhysicalStart) &&
        (NextHob.ResourceDescriptor->PhysicalStart + NextHob.ResourceDescriptor->ResourceLength <= PcdGet64 (PcdSystemMemoryBase) + PcdGet64 (PcdSystemMemorySize)))
    {
      Found = TRUE;
      break;
    }

    NextHob.Raw = GET_NEXT_HOB (NextHob);
  }

  if (!Found) {
    // Reserved the memory space occupied by the firmware volume
    BuildResourceDescriptorHob (
      EFI_RESOURCE_SYSTEM_MEMORY,
      ResourceAttributes,
      PcdGet64 (PcdSystemMemoryBase),
      PcdGet64 (PcdSystemMemorySize)
      );
  }

  //
  // Reserved the memory space occupied by the firmware volume
  //

  SystemMemoryTop = (EFI_PHYSICAL_ADDRESS)PcdGet64 (PcdSystemMemoryBase) + (EFI_PHYSICAL_ADDRESS)PcdGet64 (PcdSystemMemorySize);
  FdTop           = (EFI_PHYSICAL_ADDRESS)PcdGet64 (PcdFdBaseAddress) + (EFI_PHYSICAL_ADDRESS)PcdGet32 (PcdFdSize);

  // EDK2 does not have the concept of boot firmware copied into DRAM. To avoid the DXE
  // core to overwrite this area we must create a memory allocation HOB for the region,
  // but this only works if we split off the underlying resource descriptor as well.
  if ((PcdGet64 (PcdFdBaseAddress) >= PcdGet64 (PcdSystemMemoryBase)) && (FdTop <= SystemMemoryTop)) {
    Found = FALSE;

    // Search for System Memory Hob that contains the firmware
    NextHob.Raw = GetHobList ();
    while ((NextHob.Raw = GetNextHob (EFI_HOB_TYPE_RESOURCE_DESCRIPTOR, NextHob.Raw)) != NULL) {
      if ((NextHob.ResourceDescriptor->ResourceType == EFI_RESOURCE_SYSTEM_MEMORY) &&
          (PcdGet64 (PcdFdBaseAddress) >= NextHob.ResourceDescriptor->PhysicalStart) &&
          (FdTop <= NextHob.ResourceDescriptor->PhysicalStart + NextHob.ResourceDescriptor->ResourceLength))
      {
        ResourceAttributes = NextHob.ResourceDescriptor->ResourceAttribute;
        ResourceLength     = NextHob.ResourceDescriptor->ResourceLength;
        ResourceTop        = NextHob.ResourceDescriptor->PhysicalStart + ResourceLength;

        if (PcdGet64 (PcdFdBaseAddress) == NextHob.ResourceDescriptor->PhysicalStart) {
          if (SystemMemoryTop != FdTop) {
            // Create the System Memory HOB for the firmware
            BuildResourceDescriptorHob (
              EFI_RESOURCE_SYSTEM_MEMORY,
              ResourceAttributes,
              PcdGet64 (PcdFdBaseAddress),
              PcdGet32 (PcdFdSize)
              );

            // Top of the FD is system memory available for UEFI
            NextHob.ResourceDescriptor->PhysicalStart  += PcdGet32 (PcdFdSize);
            NextHob.ResourceDescriptor->ResourceLength -= PcdGet32 (PcdFdSize);
          }
        } else {
          // Create the System Memory HOB for the firmware
          BuildResourceDescriptorHob (
            EFI_RESOURCE_SYSTEM_MEMORY,
            ResourceAttributes,
            PcdGet64 (PcdFdBaseAddress),
            PcdGet32 (PcdFdSize)
            );

          // Update the HOB
          NextHob.ResourceDescriptor->ResourceLength = PcdGet64 (PcdFdBaseAddress) - NextHob.ResourceDescriptor->PhysicalStart;

          // If there is some memory available on the top of the FD then create a HOB
          if (FdTop < NextHob.ResourceDescriptor->PhysicalStart + ResourceLength) {
            // Create the System Memory HOB for the remaining region (top of the FD)
            BuildResourceDescriptorHob (
              EFI_RESOURCE_SYSTEM_MEMORY,
              ResourceAttributes,
              FdTop,
              ResourceTop - FdTop
              );
          }
        }

        // Mark the memory covering the Firmware Device as runtime services data
        BuildMemoryAllocationHob (
          PcdGet64 (PcdFdBaseAddress),
          PcdGet32 (PcdFdSize),
          EfiRuntimeServicesData
          );

        Found = TRUE;
        break;
      }

      NextHob.Raw = GET_NEXT_HOB (NextHob);
    }

    ASSERT (Found);
  }


  // Build Memory Allocation Hob
  InitMmu (MemoryTable);

  if (FeaturePcdGet (PcdPrePiProduceMemoryTypeInformationHob)) {
    // Optional feature that helps prevent EFI memory map fragmentation.
    BuildMemoryTypeInformationHob ();
  }

  return EFI_SUCCESS;
}

/**
 * BuildVirtualMemoryMap
 * 
 * This will build up the memory map of the platform used to initialize the MMU and page tables.
 * 
 * @param VirtualMemoryMap - A pointer to a pointer to be used for page table setup.
 * 
 * does not return anything as the value will be stored in a pointer accessible by MemoryPeim.
 * 
 */
VOID BuildVirtualMemoryMap(OUT ARM_MEMORY_REGION_DESCRIPTOR **VirtualMemoryMap)
{
  ARM_MEMORY_REGION_ATTRIBUTES CacheAttributes;
  UINTN Index = 0;
  ARM_MEMORY_REGION_DESCRIPTOR *VirtualMemoryTable;

  //ensure we actually have a valid memory map pointer
  ASSERT(VirtualMemoryMap != NULL);

  DEBUG((DEBUG_INFO, "Allocating virtual memory table pages\n"));
  VirtualMemoryTable = (ARM_MEMORY_REGION_DESCRIPTOR *)AllocatePages (EFI_SIZE_TO_PAGES (sizeof (ARM_MEMORY_REGION_DESCRIPTOR) * MAX_VIRTUAL_MEMORY_MAP_DESCRIPTORS));
  if (VirtualMemoryTable == NULL) {
    DEBUG((DEBUG_INFO, "Unexpected failure to allocate VirtualMemoryTable\n"));
    return;
  }

  CacheAttributes = DDR_ATTRIBUTES_CACHED;

  /**
   * NOTE - On Apple silicon platforms, non PCIe MMIO regions *must* use nGnRnE mappings, 
   * while all PCIe regions *must* use nGnRE mappings.
   * by default EDK2 sets up the MMIO as nGnRnE, good for core system devices
   * though we will need to add an attribute for nGnRE mappings at some point.
   * 
   * TODO: add ARM_MEMORY_REGION_ATTRIBUTE_DEVICE_POSTED_WRITE
   **/

  // Apple device MMIO
  VirtualMemoryTable[Index].PhysicalBase = M4_MMIO_BASE;
  VirtualMemoryTable[Index].VirtualBase  = M4_MMIO_BASE;
  VirtualMemoryTable[Index].Length       = M4_MMIO_SIZE;
  VirtualMemoryTable[Index].Attributes   = ARM_MEMORY_REGION_ATTRIBUTE_DEVICE;

  // GIC Distributor (emulated by the EL2 layer)
  VirtualMemoryTable[++Index].PhysicalBase = FixedPcdGet64(PcdGicDistributorBase);
  VirtualMemoryTable[Index].VirtualBase    = FixedPcdGet64(PcdGicDistributorBase);
  VirtualMemoryTable[Index].Length         = M4_GIC_DIST_SIZE;
  VirtualMemoryTable[Index].Attributes     = ARM_MEMORY_REGION_ATTRIBUTE_DEVICE;

  // GIC Redistributors, one frame pair per core (emulated by the EL2 layer)
  VirtualMemoryTable[++Index].PhysicalBase = FixedPcdGet64(PcdGicRedistributorsBase);
  VirtualMemoryTable[Index].VirtualBase    = FixedPcdGet64(PcdGicRedistributorsBase);
  VirtualMemoryTable[Index].Length         = M4_GIC_REDIST_FRAME_SIZE * FixedPcdGet32(PcdCoreCount);
  VirtualMemoryTable[Index].Attributes     = ARM_MEMORY_REGION_ATTRIBUTE_DEVICE;


  //System DRAM
  VirtualMemoryTable[++Index].PhysicalBase = PcdGet64(PcdSystemMemoryBase);
  VirtualMemoryTable[Index].VirtualBase    = PcdGet64(PcdSystemMemoryBase);
  VirtualMemoryTable[Index].Length         = PcdGet64(PcdSystemMemorySize);
  VirtualMemoryTable[Index].Attributes     = CacheAttributes;


  DEBUG ((
    DEBUG_ERROR,
    "%a: Dumping System DRAM Memory Map:\n"
    "\tPhysicalBase: 0x%lX\n"
    "\tVirtualBase: 0x%lX\n"
    "\tLength: 0x%lX\n"
    "\tTop of system RAM: 0x%lX\n",
    __FUNCTION__,
    VirtualMemoryTable[Index].PhysicalBase,
    VirtualMemoryTable[Index].VirtualBase,
    VirtualMemoryTable[Index].Length,
    VirtualMemoryTable[Index].PhysicalBase + VirtualMemoryTable[Index].Length
    ));

  //Framebuffer
  VirtualMemoryTable[++Index].PhysicalBase = PcdGet64(PcdFrameBufferAddress);
  VirtualMemoryTable[Index].VirtualBase    = PcdGet64(PcdFrameBufferAddress);
  VirtualMemoryTable[Index].Length         = PcdGet64(PcdFrameBufferSize);
  VirtualMemoryTable[Index].Attributes     = ARM_MEMORY_REGION_ATTRIBUTE_UNCACHED_UNBUFFERED;

  DEBUG ((
    DEBUG_ERROR,
    "%a: Dumping Framebuffer Memory Map:\n"
    "\tPhysicalBase: 0x%lX\n"
    "\tVirtualBase: 0x%lX\n"
    "\tLength: 0x%lX\n"
    "\tTop of framebuffer RAM: 0x%lX\n",
    __FUNCTION__,
    VirtualMemoryTable[Index].PhysicalBase,
    VirtualMemoryTable[Index].VirtualBase,
    VirtualMemoryTable[Index].Length,
    VirtualMemoryTable[Index].PhysicalBase + VirtualMemoryTable[Index].Length
    ));

  //TODO: add other NC regions here?

  // End of Table
  VirtualMemoryTable[++Index].PhysicalBase  = 0;
  VirtualMemoryTable[Index].VirtualBase     = 0;
  VirtualMemoryTable[Index].Length          = 0;
  VirtualMemoryTable[Index].Attributes      = (ARM_MEMORY_REGION_ATTRIBUTES)0;

  ASSERT((Index + 1) <= MAX_VIRTUAL_MEMORY_MAP_DESCRIPTORS);

  *VirtualMemoryMap = VirtualMemoryTable;
}