# SiliconNT firmware

UEFI firmware and ACPI tables for Apple silicon Macs, part of SiliconNT: booting
unmodified ARM64 Windows on those machines. One image serves every machine the
project supports; how far each has been verified is in the SiliconNT repository
(`docs/ROADMAP.md`).

The firmware runs in EL1 on top of the SiliconNT EL2 layer (a fork of m1n1). The EL2
layer makes the machine look like one with a standard GICv3, so this firmware is
ordinary Project Mu with a thin platform layer. It never touches Apple's interrupt
controller. What it expects from the EL2 layer:

| What | Value |
|---|---|
| GIC Distributor | `0x50_0000_0000` |
| GIC Redistributors | `0x51_0000_0000`; the one of core number n at `+ n × 0x20000` |
| Timer | the virtual timer, INTID 27 |
| PSCI | `hvc #0` (only used by `ResetSystem`) |
| WFI | trapped by the EL2 layer |
| Serial port | the one m1n1 emulates, at the address of `/arm-io/uart0` in the device tree. A tree without that node (m1n1 without a host) means no serial port: the firmware writes to none and installs no DBG2 |
| Entry | start of the image, `x0` = address of the Apple boot arguments |

What differs between machines is read from the Apple device tree at boot, not built in:

| What | From |
|---|---|
| The cores: number, MPIDR, efficiency or performance, which one is the boot core | `/cpus` |
| Address of the serial port | `/arm-io/uart0` |
| Range of the device registers | `ranges` of `/arm-io` |
| Number of the chip (the OEM revision of the ACPI tables) | `chip-id` of `/chosen` |
| Memory, framebuffer | the boot arguments |

MADT, DSDT and, with a serial port, DBG2 are built from these at boot; GTDT and FADT are the same everywhere.

The design, and the evidence behind each of these, is in the SiliconNT repository
(`docs/DESIGN.md`, `docs/findings/`).

## Layout

| Path | What |
|---|---|
| `MU_BASECORE`, `Common/TIANO`, `Silicon/ARM/TIANO` | Microsoft's Project Mu repositories, unmodified submodules |
| `Silicon/Apple/AppleSiliconPkg` | Code common to Apple silicon machines, taken from NTASP (see its `README.md`) |
| `Platform/SiliconNTPkg` | The platform: build files, memory map, ACPI tables |

## Building

Only tried on Linux (Ubuntu 26.04 under WSL2) with clang 21.

```
sudo apt install clang lld llvm uuid-dev acpica-tools gcc-aarch64-linux-gnu
python3 -m venv .venv
.venv/bin/pip install -r pip-requirements.txt
./build.sh setup     # submodules
./build.sh update    # downloads Mu's prebuilt build tools
./build.sh           # builds
```

The image is `Build/SiliconNT-AARCH64/DEBUG_CLANGPDB/FV/SILICONNT_EFI.fd`.

## Tests

Two test programs run on the build host; they need `cc`, the second one also `iasl`
(`acpica-tools`).

```
Silicon/Apple/AppleSiliconPkg/Test/AppleDTLibHostTest/run.sh   # the device tree reader
Platform/SiliconNTPkg/Test/AcpiTablesHostTest/run.sh           # MADT, DBG2, DSDT from a device tree
```

Each prints one line per check and ends with `all N checks passed`; the second then
prints `all N DSDTs match the ASL compiler`.

## State

The firmware with static tables for the M4 MacBook Pro (commit `816ea14`) boots that
machine to Windows Setup. Reading the machine from the device tree came after it and
has been built and tested on the host only; it has not been run on a machine.

## Licence

New files are BSD-2-Clause-Patent, like Project Mu. Files taken from other projects
keep their own notices; see `Silicon/Apple/AppleSiliconPkg/README.md`.
