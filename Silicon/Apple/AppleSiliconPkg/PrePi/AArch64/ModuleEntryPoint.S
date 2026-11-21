#include <Library/PcdLib.h>
#include <AArch64/AsmMacroLib.h>
#include <AArch64.h>
#include <Library/ArmLib.h>

.text
.align 3

GCC_ASM_IMPORT(CEntryPoint)
GCC_ASM_EXPORT(_ModuleEntryPoint)
GCC_ASM_IMPORT (ArmEnableInstructionCache)
GCC_ASM_IMPORT (ArmEnableDataCache)
GCC_ASM_IMPORT (ArmEnableVFP)
GCC_ASM_IMPORT(UARTRelocationDebugMessage)

ASM_FUNC(_ModuleEntryPoint)

  //back up initial start addr and ADT pointer
  mov x27, x0
  mov x26, x1
  mov x25, x0 //boot args addr (SiliconNT: the EL2 layer passes it in x0)

  //
  // SiliconNT: the EL2 layer enters the image with SP_EL1 = 0 (m1n1,
  // src/hv/hv_asm.S, hv_enter_guest), and the next call is into C. Until the
  // real stack is set up below, use the window between the image header and
  // the firmware volume, the same one DiscoverDramFromDt switches to.
  // x1 = load address of the image, left there by the header.
  //
  adr   x8, PcdGet64 (PcdFdBaseAddress)
  adr   x9, PcdGet64 (PcdFvBaseAddress)
  ldr   x8, [x8]
  ldr   x9, [x9]
  sub   x9, x9, x8
  add   x9, x9, x1
  mov   sp, x9

  //
  // SiliconNT: the loop that clears the stack below uses SIMD registers, and
  // nothing has enabled them. CPACR_EL1.FPEN is UNKNOWN after a reset (Arm
  // DDI 0487 M.d, D24.2.35) and the EL2 layer does not write it for the guest.
  // ArmEnableVFP is a leaf routine that changes x0-x2 only; all three were
  // saved above and are restored before DiscoverDramFromDt.
  //
  bl    ASM_PFX(ArmEnableVFP)
  isb

  //
  // SiliconNT: the serial port is not set up here any more. Its address is in
  // the device tree, which EarlySetup (called by DiscoverDramFromDt, after the
  // image has been relocated) reads; it sets the port up and prints the banner.
  //

  //Use ADT to discover usable DRAM base/size
  //restore values
  mov x0, x27
  mov x1, x26
  bl ASM_PFX(DiscoverDramFromDt)
  b _ModuleEntrySetupStack

_ModuleEntrySetupStack:

  // Compute Top of System Memory
  ldr   x1, =PcdGet64 (PcdSystemMemoryBase)
  ldr   x1, [x1]
  ldr   x2, =PcdGet64 (PcdSystemMemorySize)
  ldr   x2, [x2]
  sub   x2, x2, #1
  add   x1, x1, x2      // x1 = SystemMemoryTop = PcdSystemMemoryBase + PcdSystemMemorySize

  //calculate top of FD image
  ldr   x2, =PcdGet64 (PcdFdBaseAddress)
  ldr   x2, [x2]
  MOV64 (x3, FixedPcdGet32 (PcdFdSize) - 1)
  add   x3, x3, x2      // x3 = FdTop = PcdFdBaseAddress + PcdFdSize

  // UEFI Memory Size (stacks are allocated in this region)
  MOV32 (x4, FixedPcdGet32(PcdSystemMemoryUefiRegionSize))

  //
  // Reserve the memory for the UEFI region (contain stacks on its top)
  //

  // Calculate how much space there is between the top of the Firmware and the Top of the System Memory
  subs  x0, x1, x3   // x0 = SystemMemoryTop - FdTop
  b.mi  _SetupStack  // Jump if negative (FdTop > SystemMemoryTop). Case when the PrePi is in XIP memory outside of the DRAM
  cmp   x0, x4
  b.ge  _SetupStack

  // Case the top of stacks is the FdBaseAddress
  mov   x1, x2

_SetupStack:
  // x1 contains the top of the stack (and the UEFI Memory)

  // Because the 'push' instruction is equivalent to 'stmdb' (decrement before), we need to increment
  // one to the top of the stack. We check if incrementing one does not overflow (case of DRAM at the
  // top of the memory space)
  adds  x11, x1, #1
  b.cs  _SetupOverflowStack

_SetupAlignedStack:
  mov   x1, x11
  b     _GetBaseUefiMemory

_SetupOverflowStack:
  // Case memory at the top of the address space. Ensure the top of the stack is EFI_PAGE_SIZE
  // aligned (4KB)
  and   x1, x1, ~EFI_PAGE_MASK

_GetBaseUefiMemory:
  // Calculate the Base of the UEFI Memory
  sub   x11, x1, x4
  // backup EFI memory base pointer
  mov x22, x11

_GetStackBase:
  // Stack for the primary core = SecPhaseStack (base address in x12)
  //TODO - make the PCD value work properly, for now hardcode this value.
  MOV32 (x2, 0x20000)
  sub   x12, x1, x2
  //HACK for now - subtract both stack top and base by 0x40 to mitigate an ASSERT
  //TODO - fix the root cause of the stack base being misaligned by 0x40 bytes
  sub   x1, x1, #0x40
  sub   x12, x12, #0x40

_StackSetup:
  //x1, top of stack
  //x12, stack base

  mov v4.d[0], xzr
  mov v4.d[1], xzr
  mov v5.2d, v4.2d 
  mov v6.2d, v4.2d
  mov v7.2d, v4.2d 
  
_ClearStack: 
  /* Assumes StackBase is 128-bit aligned, StackSize is a multiple of 64B */
  st4     {v4.2d, v5.2d, v6.2d, v7.2d}, [x12], #64  /* Fill every 64 bytes */
  cmp     x12, x2                                   /* Compare Size */ 
  b.lt     _ClearStack 

  //set stack pointer to addr in x1
  add sp, x1, xzr

_EnableCaches:
  bl ASM_PFX(ArmEnableInstructionCache)
  bl ASM_PFX(ArmEnableDataCache)

_PrepareArgumentsAndJump:
  mov x0, x12 //Stack Base
  mov x1, x2  //Stack Size
  mov x2, x25 //boot args addr
  mov x3, x22 //UEFI memory base

  ldr x4, =ASM_PFX(CEntryPoint)
  //null frame pointer such that backtraces terminate here
  mov x29, xzr
  blr x4

// if we get here, something *very bad* has happened. (or we're debugging)
_NeverReturn:
  b _NeverReturn


// VOID
// DiscoverDramFromDt (
//   VOID   *BootArgs,                // passed by the EL2 layer in x0
//   VOID   *ImageBase                // passed by FDF trampoline in x1
//   );
ASM_PFX(DiscoverDramFromDt):
  mov   x28, x30            // preserve LR
  mov   x26, x1             // preserve base of image addr

  //
  // The base of the runtime image has been preserved in x1. Check whether
  // the expected magic number can be found in the header.
  //
  ldr   w8, .LArm64LinuxMagic
  ldr   w9, [x1, #0x38]
  cmp   w8, w9
  bne   .Lout

  //
  //
  // OK, so far so good. We have confirmed that we likely have a DTB and are
  // booting via the arm64 Linux boot protocol. Update the base-of-image PCD
  // to the actual relocated value, and add the shift of PcdFdBaseAddress to
  // PcdFvBaseAddress as well
  //
  adr   x8, PcdGet64 (PcdFdBaseAddress)
  adr   x9, PcdGet64 (PcdFvBaseAddress)
  ldr   x6, [x8]
  ldr   x7, [x9]
  sub   x7, x7, x6
  add   x7, x7, x1
  str   x1, [x8]
  str   x7, [x9]

  //
  // The runtime address may be different from the link time address so fix
  // up the PE/COFF relocations. Since we are calling a C function, use the
  // window at the beginning of the FD image as a temp stack.
  //
  mov   x0, x7
  adr   x1, PeCoffLoaderImageReadFromMemory
  mov   sp, x7
  bl    RelocatePeCoffImage

  //
  // Take the memory base and size and the framebuffer from the boot
  // arguments, and record them in the respective PCDs. Does not return if
  // the device tree the boot arguments point to is corrupt.
  //
  // SiliconNT: the boot arguments and the device tree stay where the loader
  // put them. Main() reserves the two ranges and publishes their addresses
  // in a HOB (PrePi.c, PublishBootInfo).
  //
  mov   x0, x25 //boot args addr
  adr   x1, PcdGet64 (PcdSystemMemoryBase)
  adr   x2, PcdGet64 (PcdSystemMemorySize)

  bl    EarlySetup

.Lout:
  ret    x28

.LArm64LinuxMagic:
  .byte   0x41, 0x52, 0x4d, 0x64
