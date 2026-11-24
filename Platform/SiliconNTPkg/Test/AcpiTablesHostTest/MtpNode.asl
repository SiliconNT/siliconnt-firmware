// The node of MTP the M4 MacBook Pro must get (SiliconNT docs/findings/0095,
// conclusion 2; but "kblang-calibration", which is made up here). One line for each
// thing in it: the test takes lines out, or changes them, for trees that differ.
    Device (MTP0)
    {
        Name (_HID, "SLNT0002")
        Name (_UID, 0)
        Name (_CCA, 0)
        Name (_CRS, ResourceTemplate ()
        {
            QWordMemory (ResourceConsumer, PosDecode, MinFixed, MaxFixed, NonCacheable, ReadWrite, 0x0, 0x394600000, 0x394687FFF, 0x0, 0x88000)   // memory mtp-0
            QWordMemory (ResourceConsumer, PosDecode, MinFixed, MaxFixed, NonCacheable, ReadWrite, 0x0, 0x394050000, 0x394053FFF, 0x0, 0x4000)   // memory mtp-1
            QWordMemory (ResourceConsumer, PosDecode, MinFixed, MaxFixed, NonCacheable, ReadWrite, 0x0, 0x394800000, 0x39480BFFF, 0x0, 0xC000)   // memory dart-mtp-0
            QWordMemory (ResourceConsumer, PosDecode, MinFixed, MaxFixed, NonCacheable, ReadWrite, 0x0, 0x394810000, 0x394813FFF, 0x0, 0x4000)   // memory dart-mtp-1
            QWordMemory (ResourceConsumer, PosDecode, MinFixed, MaxFixed, NonCacheable, ReadWrite, 0x0, 0x394B00000, 0x394B00FFF, 0x0, 0x1000)   // memory dockchannel-mtp-0
            QWordMemory (ResourceConsumer, PosDecode, MinFixed, MaxFixed, NonCacheable, ReadWrite, 0x0, 0x394B14000, 0x394B14FFF, 0x0, 0x1000)   // memory dockchannel-mtp-1
            QWordMemory (ResourceConsumer, PosDecode, MinFixed, MaxFixed, NonCacheable, ReadWrite, 0x0, 0x394B30000, 0x394B30FFF, 0x0, 0x1000)   // memory dockchannel-mtp-2
            QWordMemory (ResourceConsumer, PosDecode, MinFixed, MaxFixed, NonCacheable, ReadWrite, 0x0, 0x394B34000, 0x394B34FFF, 0x0, 0x1000)   // memory dockchannel-mtp-3
            QWordMemory (ResourceConsumer, PosDecode, MinFixed, MaxFixed, NonCacheable, ReadWrite, 0x0, 0x394B28000, 0x394B28FFF, 0x0, 0x1000)   // memory dockchannel-mtp-4
            QWordMemory (ResourceConsumer, PosDecode, MinFixed, MaxFixed, NonCacheable, ReadWrite, 0x0, 0x394B2C000, 0x394B2CFFF, 0x0, 0x1000)   // memory dockchannel-mtp-5
            QWordMemory (ResourceConsumer, PosDecode, MinFixed, MaxFixed, NonCacheable, ReadWrite, 0x0, 0x394C00000, 0x394D3FFFF, 0x0, 0x140000)   // memory mtp-sram
            Interrupt (ResourceConsumer, Level, ActiveHigh, Exclusive) { 287 }   // line mtp-0
            Interrupt (ResourceConsumer, Level, ActiveHigh, Exclusive) { 286 }   // line mtp-1
            Interrupt (ResourceConsumer, Level, ActiveHigh, Exclusive) { 289 }   // line mtp-2
            Interrupt (ResourceConsumer, Level, ActiveHigh, Exclusive) { 288 }   // line mtp-3
            Interrupt (ResourceConsumer, Level, ActiveHigh, Exclusive) { 284 }   // line dart-mtp-0
            Interrupt (ResourceConsumer, Level, ActiveHigh, Exclusive) { 285 }   // line dockchannel-mtp-0
        })
        Name (_DSD, Package ()
        {
            ToUUID ("daffd814-6eba-4d8c-8a91-bc9bbf4aa301"),
            Package ()
            {
                Package (2) { "memory-names", Package () { "mtp-0", "mtp-1", "dart-mtp-0", "dart-mtp-1", "dockchannel-mtp-0", "dockchannel-mtp-1", "dockchannel-mtp-2", "dockchannel-mtp-3", "dockchannel-mtp-4", "dockchannel-mtp-5", "mtp-sram" } },
                Package (2) { "interrupt-names", Package () { "mtp-0", "mtp-1", "mtp-2", "mtp-3", "dart-mtp-0", "dockchannel-mtp-0" } },
                Package (2) { "mtp/compatible", Package () { "iop,ascwrap-v6" } },
                Package (2) { "mtp/iop-version", 1 },
                Package (2) { "mtp/iommu-stream", 0 },
                Package (2) { "iop-mtp-nub/compatible", Package () { "iop-nub,rtbuddy-v2" } },
                Package (2) { "iop-mtp-nub/pre-loaded", 1 },
                Package (2) { "iop-mtp-nub/no-shutdown", 1 },
                Package (2) { "iop-mtp-nub/coredump-enable", 0x40 },
                Package (2) { "iop-mtp-nub/watchdog-enable", 1 },
                Package (2) { "iop-mtp-nub/no-firmware-service", 1 },
                Package (2) { "iop-mtp-nub/sleep-on-hibernate", 1 },
                Package (2) { "iop-mtp-nub/cold-boot-after-hibernate", 1 },
                Package (2) { "dart-mtp/compatible", Package () { "dart,t8110" } },
                Package (2) { "dart-mtp/instance", Package () { 0x44415254, 0, 0x54524144, 0, 0x44415046, 0, 0x46504144, 0, 0x47415046, 0, 0x505F5350, 0x544F52 } },
                Package (2) { "dart-mtp/page-size", 0x4000 },
                Package (2) { "dart-mtp/sid-count", 0x10 },
                Package (2) { "dart-mtp/sid", Package () { 0 } },
                Package (2) { "dart-mtp/remap", Package () { 1, 2, 3 } },
                Package (2) { "dart-mtp/vm-base", Package () { 0x4000, 0x100 } },
                Package (2) { "dart-mtp/vm-size", Package () { 0xFFFF0000, 0 } },
                Package (2) { "dart-mtp/dart-options", 0x25 },
                Package (2) { "dart-mtp/dart-id", 0xC },
                Package (2) { "dart-mtp/flush-by-dva", 0 },
                Package (2) { "dart-mtp/error-reflector", Package () { 0x7F000000, 2 } },
                Package (2) { "dart-mtp/clock-protection-slice-index", Package () { 0xE } },
                Package (2) { "dart-mtp/no-sleep", 1 },
                Package (2) { "dart-mtp/dart-tunables-instance-0", Package () { 0x20C, 4, 0x80000121, 0, 0x80000121, 0, 0x220, 4, 0xF0F0F, 0, 0xF0F0F, 0, 0x224, 4, 0xFFFFFF, 0, 0x80808, 0, 0x300, 4, 0x1F31, 0, 1, 0, 0x308, 4, 0x3FFFFFFC, 0, 0x10000000, 0, 0x310, 4, 0x3FFFFFFC, 0, 0x3FFFFFFC, 0 } },
                Package (2) { "dart-mtp/dapf-instance-0", Package () { 0x8828C000, 3, 0x8828C06F, 3, 1, 0, 0, 0, 0, 0, 0, 0, 0x10300, 0x8829C800, 3, 0x8829C9A7, 3, 1, 0, 0, 0, 0, 0, 0, 0, 0x10300, 0x882D0018, 3, 0x882D0113, 3, 1, 0, 0, 0, 0, 0, 0, 0, 0x10300, 0x882BC020, 3, 0x882BC023, 3, 1, 0, 0, 0, 0, 0, 0, 0, 0x10300, 0x88240324, 3, 0x88240327, 3, 1, 0, 0, 0, 0, 0, 0, 0, 0x10300, 0x9060C110, 3, 0x9060C81F, 3, 1, 0, 0, 0, 0, 0, 0, 0, 0x10300, 0x90668000, 3, 0x90668003, 3, 1, 0, 0, 0, 0, 0, 0, 0, 0x10300, 0x90EEC000, 3, 0x90EEE000, 3, 1, 0, 0, 0, 0, 0, 0, 0, 0x10300, 0x881F006C, 3, 0x881F007B, 3, 1, 0, 0, 0, 0, 0, 0, 0, 0x10300 } },
                Package (2) { "dockchannel-mtp/compatible", Package () { "dockchannel,t8002" } },
                Package (2) { "dockchannel-mtp/channel-number", 0 },
                Package (2) { "dockchannel-mtp/channel-name", "channel0" },
                Package (2) { "dockchannel-mtp/fifo-size", 0x800 },
                Package (2) { "dockchannel-mtp/dock-wstat-mask", 0xFFF },
                Package (2) { "mtp-transport/interfaces", Package () { "multi-touch", "keyboard", "stm", "actuator", "tp-accel" } },
                Package (2) { "mtp-transport/iommu-stream", 0 },
                Package (2) { "mtp-transport/hid-transport-mux", "mtp-aop-mux" },
                Package (2) { "mtp-transport/hid-merge-personality", "MTP_SYS" },
                Package (2) { "mtp-transport/image-tag", 0x6D747066 },
                Package (2) { "multi-touch/bootloader-type", "CBOR" },
                Package (2) { "multi-touch/image-tag", 0x6D746677 },
                Package (2) { "multi-touch/hid-merge-personality", "C1FD0,5" },
                Package (2) { "multi-touch/function-afe-reset", Package () { "smc-pmu", 0x704B5734, 0x67503163, 0x10000 } },
                Package (2) { "keyboard/kblang-calibration", Package () { 0x11220133, 0x4455, 0, 0 } },
                Package (2) { "stm/bootloader-type", "HIDDevice" },
                Package (2) { "stm/image-tag", 0x69706466 },
                Package (2) { "stm/hid-merge-personality", "IPD" },
                Package (2) { "stm/function-stm-reset", Package () { "smc-pmu", 0x704B5734, 0x67503164, 0x10000 } },
            }
        })
    }
