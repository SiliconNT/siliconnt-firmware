# AppleSiliconPkg

Everything in this directory was taken from
[NT-for-ASi/apple_silicon_platforms_mu](https://github.com/NT-for-ASi/apple_silicon_platforms_mu)
at commit `861dcc490c564bd362197fdd7c30fcf4f7cafd15`, directory
`Silicon/Apple/AppleSiliconPkg`. `LICENSE` is that repository's licence file. Each
file keeps the notice it had; some had none. The one exception is `Test/`, which is
new here: `Test/AppleDTLibHostTest/run.sh` compiles the device tree reader for the
host and checks it against device trees it builds: `AppleGetBootRamdisk()` in
`BootRamdiskTest.c`, what the firmware reads about the machine in
`MachineInfoTest.c`. `HostTest.h` holds what the tests share, the tree of the M4
MacBook Pro among it.

That repository is not an upstream of this one: updates are not followed.

| Here | What | Changed |
|---|---|---|
| `PrePi/` | Entry from m1n1, hand-off blocks, jump to the DXE core | The boot arguments address is taken from `x0` instead of `x4`. The entry code sets a stack before its first call into C (the EL2 layer enters with `SP_EL1` = 0). It also enables FP/SIMD access (`ArmEnableVFP`) before the stack clearing loop that uses SIMD registers: nothing guarantees `CPACR_EL1.FPEN` on entry. The boot arguments and the device tree are no longer copied to two fixed addresses: they stay where the loader put them, `Main()` reserves both ranges and publishes their addresses in a HOB (`gAppleSiliconPkgBootInfoHobGuid`). A corrupt device tree stops the entry code instead of letting it go on with build-time memory defaults. `Main()` also reserves the boot disk image when the device tree names one (`AppleGetBootRamdisk()`), and stops on an entry it cannot use. The non-relocating entry file was not taken. `LzmaDecompressLib` removed from the module's library list (the platform build links the LZMA library explicitly) The serial port is no longer set up first thing: its address is in the device tree, so `EarlySetup()` reads it (`AppleGetUartBase()`), tells the serial port library and prints the banner; a tree that is corrupt or does not name the port stops the entry code without output. The HOB is built before `MemoryPeim()`, which reads the device tree through it, and carries the address of the serial port for the modules that run later |
| `Library/AppleDTLib/` | Apple device tree reader (from pongoOS) | `dt_get()` and `dt_node_parent()` take the device tree address from that HOB instead of a fixed PCD; `AppleGetBootInfo()` added. `AppleGetBootRamdisk()` added: it reads the `RAMDisk` entry of `/chosen/memory-map` (base and size of the boot disk image the loader left in memory) and checks it against the memory the boot arguments describe. What the firmware has to know about the machine is read here, from the tree it is given and nothing else: `AppleGetCpus()` (number, MPIDR, class and boot core of every core of `/cpus`), `AppleGetUartBase()` (`/arm-io/uart0`), `AppleGetMmioSpan()` (`ranges` of `/arm-io`), `AppleGetChipId()` (`chip-id` of `/chosen`) |
| `Library/AppleUartSerialPortLib/` | Samsung-style serial port | `SerialPortWrite()` returns the number of bytes written; it returned 0, which `SerialDxe` takes for a timeout. The library only writes: `SerialPortPoll()` always answers "no data" and `SerialPortRead()` reads nothing, neither touches the port. The receive direction of this port belongs to the Windows kernel debugger. The address of the port is read from the device tree at boot. The library has three flavours that differ in how they learn it: the entry code is told (`AppleUartSerialPortLibSec.inf`), the DXE core looks it up in the boot information HOB (`AppleUartSerialPortLibDxeCore.inf`), every later module takes it from that HOB in a constructor (`AppleUartSerialPortLibDxe.inf`). While a module does not know the address it writes nothing |
| `Library/PlatformPeiLib/` | Publishes the firmware volume | — |
| `Drivers/SimpleFbDxe/` | Framebuffer left by iBoot and m1n1 | The boot arguments address comes from `AppleGetBootInfo()` instead of a fixed PCD. Line length and pixel layout follow the stride and the depth of the boot arguments: depth 30 is described as a 10-bit-per-channel bit mask (from OakRoller, pull request #1) |
| `Drivers/BootRamdiskHelperDxe/` | Registers the boot disk image as a RAM disk | The image is no longer a file embedded in the firmware volume and copied to the heap when `PcdInitializeRamdisk` is set. The driver takes base and size from the device tree (`AppleGetBootRamdisk()`) and registers that memory where it is. Without a `RAMDisk` entry it prints one line and registers nothing. Library, protocol and GUID lists cut down to what is used |
| `Drivers/SmbiosInfoDxe/` | SMBIOS tables | The Mu version library and two unused package references removed, with the two calls that wrote the version and date into string literals. The processor record (type 4) carries the number of cores the device tree lists |
| `AppleSiliconFdfRules.fdf.inc` | Image build rules | — |
| `AppleSiliconPkg.dec` | Package declaration | Cut down to what the files above use. `PcdAdtPointer`, `PcdBootArgsPointer` and `PcdSecPhaseStackBase` removed, the HOB GUID added. `PcdInitializeRamdisk` and `gAppleSiliconPkgEmbeddedRamdiskGuid` removed with the embedded ramdisk. It declares no PCD for the address of the serial port or for the number of cores: both are read from the device tree at boot |

Line endings were converted to LF.

Also derived from the same commit, outside this directory:

| File | From |
|---|---|
| `Platform/SiliconNTPkg/Library/MemoryInitPeiLib/` | `Silicon/Apple/T602XFamilyPkg/Library/MemoryInitPeiLib/` |
| `Platform/SiliconNTPkg/Drivers/AcpiPlatformDxe/AcpiTableBuilder.c` (the layout of the MADT), `Gtdt.c` | `Silicon/Apple/T602XFamilyPkg/AcpiTables/` |
| `Platform/SiliconNTPkg/Drivers/AcpiPlatformDxe/Fadt.c`, `AcpiTableBuilder.c` (the layout of the DBG2) | `Platform/MacBookProEarly2023Pkg/AcpiTables/` |
| `Platform/SiliconNTPkg/Include/Acpi/AcpiHeader.h` | `Silicon/Apple/AppleSiliconPkg/Include/Acpi/AcpiHeader.h` |
| `Platform/SiliconNTPkg/SiliconNT.dsc`, `.fdf`, `PlatformBuild.py` | `Silicon/Apple/AppleSiliconPkg/AppleSiliconPkg.dsc.inc`, `Platform/MacBookProEarly2023Pkg/` |
