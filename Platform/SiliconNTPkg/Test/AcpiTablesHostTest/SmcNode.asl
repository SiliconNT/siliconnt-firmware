// The node of SMC the M4 MacBook Pro must get (SiliconNT docs/findings/0095,
// conclusion 3). One line for each thing in it: the test takes lines out, or changes
// them, for trees that differ.
    Device (SMC0)
    {
        Name (_HID, "SLNT0003")
        Name (_UID, 0)
        Name (_CCA, 0)
        Name (_CRS, ResourceTemplate ()
        {
            QWordMemory (ResourceConsumer, PosDecode, MinFixed, MaxFixed, NonCacheable, ReadWrite, 0x0, 0x38C600000, 0x38C687FFF, 0x0, 0x88000)   // memory smc-0
            QWordMemory (ResourceConsumer, PosDecode, MinFixed, MaxFixed, NonCacheable, ReadWrite, 0x0, 0x38C050000, 0x38C053FFF, 0x0, 0x4000)   // memory smc-1
            QWordMemory (ResourceConsumer, PosDecode, MinFixed, MaxFixed, NonCacheable, ReadWrite, 0x0, 0x38C810000, 0x38C8100FF, 0x0, 0x100)   // memory smc-2
            QWordMemory (ResourceConsumer, PosDecode, MinFixed, MaxFixed, NonCacheable, ReadWrite, 0x0, 0x38DE00000, 0x38DEFFFFF, 0x0, 0x100000)   // memory smc-region
            Interrupt (ResourceConsumer, Level, ActiveHigh, Exclusive) { 144 }   // line smc-0
            Interrupt (ResourceConsumer, Level, ActiveHigh, Exclusive) { 143 }   // line smc-1
            Interrupt (ResourceConsumer, Level, ActiveHigh, Exclusive) { 146 }   // line smc-2
            Interrupt (ResourceConsumer, Level, ActiveHigh, Exclusive) { 145 }   // line smc-3
        })
        Name (_DSD, Package ()
        {
            ToUUID ("daffd814-6eba-4d8c-8a91-bc9bbf4aa301"),
            Package ()
            {
                Package (2) { "memory-names", Package () { "smc-0", "smc-1", "smc-2", "smc-region" } },
                Package (2) { "interrupt-names", Package () { "smc-0", "smc-1", "smc-2", "smc-3" } },
                Package (2) { "smc/compatible", Package () { "iop,ascwrap-v6" } },
                Package (2) { "smc/iop-version", 1 },
                Package (2) { "smc/aot-power", 1 },
                Package (2) { "smc/timers-reg-index", 2 },
                Package (2) { "iop-smc-nub/compatible", Package () { "iop-nub,rtbuddy-v2" } },
                Package (2) { "iop-smc-nub/pre-loaded", 1 },
                Package (2) { "iop-smc-nub/running", 1 },
                Package (2) { "iop-smc-nub/no-shutdown", 1 },
                Package (2) { "iop-smc-nub/user-power-managed", 1 },
                Package (2) { "iop-smc-nub/coredump-enable", 0x40 },
                Package (2) { "iop-smc-nub/coredump-active-only", 1 },
                Package (2) { "iop-smc-nub/quiesced", 1 },
                Package (2) { "iop-smc-nub/continuous-time", 1 },
                Package (2) { "iop-smc-nub/watchdog-enable", 1 },
                Package (2) { "iop-smc-nub/no-hibernate-sleep", 1 },
                Package (2) { "iop-smc-nub/cold-boot-after-hibernate", 1 },
                Package (2) { "smc-pmu/has_lid_open_sensor", 1 },
                Package (2) { "smc-pmu/event_name-bit0", "pwrbtn" },
                Package (2) { "smc-pmu/event_name-bit8", "lid" },
                Package (2) { "smc-pmu/event_name-bit9", "sdconn" },
                Package (2) { "smc-pmu/event_name-bit10", "codec" },
                Package (2) { "smc-pmu/event_name-bit11", "charger" },
                Package (2) { "smc-pmu/event_name-bit12", "ipd" },
                Package (2) { "smc-pmu/event_name-bit13", "wifibt" },
                Package (2) { "smc-pmu/event_name-bit40", "acattach" },
                Package (2) { "smc-pmu/event_name-bit41", "acdetach" },
                Package (2) { "smc-pmu/event_name-bit42", "USB-C_plug" },
                Package (2) { "smc-pmu/event_name-bit43", "USB2_plug" },
                Package (2) { "smc-pmu/event_name-bit44", "USB2_wake" },
                Package (2) { "smc-pmu/event_name-bit45", "aupo" },
                Package (2) { "smc-pmu/event_name-bit48", "rtc" },
                Package (2) { "smc-charger-util-0/dock-num", 1 },
                Package (2) { "smc-charger-util-0/port-number", 1 },
                Package (2) { "smc-charger-util-0/port-type", 2 },
                Package (2) { "smc-charger-util-1/dock-num", 2 },
                Package (2) { "smc-charger-util-1/port-number", 2 },
                Package (2) { "smc-charger-util-1/port-type", 2 },
                Package (2) { "smc-charger-util-3/dock-num", 3 },
                Package (2) { "smc-charger-util-3/port-number", 4 },
                Package (2) { "smc-charger-util-3/port-type", 2 },
                Package (2) { "smc-charger-util/dock-num", 4 },
                Package (2) { "smc-charger-util/port-number", 1 },
                Package (2) { "smc-charger-util/port-type", 0x11 },
                Package (2) { "smc-charger-util/magsafe-auth", 1 },
            }
        })
    }
