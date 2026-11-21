# SiliconNT firmware

UEFI firmware and ACPI tables for the M4 MacBook Pro (Apple T8132), part of
SiliconNT: booting unmodified ARM64 Windows on that machine.

The firmware runs in EL1 on top of the SiliconNT EL2 layer (a fork of m1n1). The EL2
layer makes the machine look like one with a standard GICv3, so this firmware is
ordinary Project Mu with a thin platform layer. It never touches Apple's interrupt
controller. What it expects from the EL2 layer:

| What | Value |
|---|---|
| GIC Distributor | `0x50_0000_0000` |
| GIC Redistributors | `0x51_0000_0000`, `0x20000` per core |
| Timer | the virtual timer, INTID 27 |
| PSCI | `hvc #0` (only used by `ResetSystem`) |
| WFI | trapped by the EL2 layer |
| Serial port | the one m1n1 emulates, at `0x3_ad20_0000` |
| Entry | start of the image, `x0` = address of the Apple boot arguments |

The design, and the evidence behind each of these, is in the SiliconNT repository
(`docs/DESIGN.md`, `docs/findings/`).

## Layout

| Path | What |
|---|---|
| `MU_BASECORE`, `Common/TIANO`, `Silicon/ARM/TIANO` | Microsoft's Project Mu repositories, unmodified submodules |
| `Silicon/Apple/AppleSiliconPkg` | Code common to Apple silicon machines, taken from NTASP (see its `README.md`) |
| `Platform/SiliconNTPkg` | The M4 MacBook Pro: build files, memory map, ACPI tables |

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

## State

It builds. It has not been run, on the machine or anywhere else.

## Licence

New files are BSD-2-Clause-Patent, like Project Mu. Files taken from other projects
keep their own notices; see `Silicon/Apple/AppleSiliconPkg/README.md`.
