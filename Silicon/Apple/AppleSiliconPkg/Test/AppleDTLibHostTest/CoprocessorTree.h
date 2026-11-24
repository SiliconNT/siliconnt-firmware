/** @file
  The coprocessors of the M4 MacBook Pro as its Apple device tree has them: MTP
  (the keyboard and the trackpad) with its DART and its DockChannel, SMC, and what
  /chosen says about memory. The nodes and their properties, in the order of the
  tree, are those of the tree iBoot handed over in SiliconNT's run R46 (SiliconNT
  docs/findings/0095); addresses in "reg" are addresses on the bus. Two values are
  made up: "kblang-calibration" and "uuid" belong to one machine or one build of
  Apple's firmware, not to the model.

  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#ifndef COPROCESSOR_TREE_H_
#define COPROCESSOR_TREE_H_

#include "HostTest.h"

// /arm-io/smc
STATIC CONST TEST_PROPERTY mTreeSmc[] = {
    TEST_TEXT ("compatible", "iop,ascwrap-v6"),
    TEST_EMPTY ("clock-ids"),
    TEST_WORDS ("interrupt-parent", 0xD5),
    TEST_WORDS ("reg",
        0x8C600000, 1, 0x88000, 0,
        0x8C050000, 1, 0x4000, 0,
        0x8C810000, 1, 0x100, 0),
    TEST_WORDS ("interrupts", 0x232, 0x231, 0x234, 0x233),
    TEST_EMPTY ("clock-gates"),
    TEST_WORDS ("AAPL,phandle", 0x10B),
    TEST_WORDS ("iop-version", 1),
    TEST_TEXT ("device_type", "smc"),
    TEST_WORDS ("aot-power", 1),
    TEST_TEXT ("role", "SMC"),
    TEST_EMPTY ("power-gates"),
    TEST_WORDS ("timers-reg-index", 2),
};

// /arm-io/smc/iop-smc-nub
STATIC CONST TEST_PROPERTY mTreeIopSmcNub[] = {
    TEST_WORDS ("user-power-managed", 1),
    TEST_EMPTY ("quiesced"),
    TEST_TEXT ("compatible", "iop-nub,rtbuddy-v2"),
    TEST_WORDS ("coredump-enable", 0x40),
    TEST_WORDS ("AAPL,phandle", 0x10C),
    TEST_WORDS ("region-base", 0x8DE00000, 3),
    TEST_TEXT ("firmware-name", "t8132smc"),
    TEST_WORDS ("no-shutdown", 1),
    TEST_WORDS ("region-size", 0x100000, 0),
    TEST_EMPTY ("continuous-time"),
    TEST_EMPTY ("cold-boot-after-hibernate"),
    TEST_EMPTY ("no-hibernate-sleep"),
    TEST_EMPTY ("watchdog-enable"),
    TEST_EMPTY ("coredump-active-only"),
    TEST_WORDS ("pre-loaded", 1),
    TEST_TEXT ("uuid", "00000000-1111-2222-3333-444444444444"),
    TEST_WORDS ("running", 1),
};

// /arm-io/smc/iop-smc-nub/smc-pmu
STATIC CONST TEST_PROPERTY mTreeSmcPmu[] = {
    TEST_WORDS ("#address-cells", 0),
    TEST_TEXT ("event_name-bit12", "ipd"),
    TEST_WORDS ("AAPL,phandle", 0x10D),
    TEST_TEXT ("event_name-bit10", "codec"),
    TEST_TEXT ("event_name-bit48", "rtc"),
    TEST_TEXT ("event_name-bit44", "USB2_wake"),
    TEST_WORDS ("has_lid_open_sensor", 1),
    TEST_TEXT ("event_name-bit42", "USB-C_plug"),
    TEST_TEXT ("event_name-bit8", "lid"),
    TEST_TEXT ("event_name-bit0", "pwrbtn"),
    TEST_TEXT ("event_name-bit40", "acattach"),
    TEST_TEXT ("event_name-bit13", "wifibt"),
    TEST_TEXT ("compatible", "smc-pmu"),
    TEST_EMPTY ("interrupt-controller"),
    TEST_TEXT ("event_name-bit9", "sdconn"),
    TEST_WORDS ("#interrupt-cells", 1),
    TEST_TEXT ("event_name-bit11", "charger"),
    TEST_TEXT ("event_name-bit45", "aupo"),
    TEST_TEXT ("event_name-bit43", "USB2_plug"),
    TEST_TEXT ("device_type", "interrupt-controller"),
    TEST_TEXT ("event_name-bit41", "acdetach"),
    TEST_WORDS ("function-pmu_button", 0x1A9, 0x42757444),
};

// /arm-io/smc/iop-smc-nub/smc-charger-util-0
STATIC CONST TEST_PROPERTY mTreeSmcChargerUtil0[] = {
    TEST_TEXT ("device_type", "smc-charger-util"),
    TEST_TEXT ("compatible", "smc-charger-util"),
    TEST_WORDS ("dock-num", 1),
    TEST_WORDS ("port-number", 1),
    TEST_WORDS ("port-type", 2),
    TEST_WORDS ("AAPL,phandle", 0x10E),
};

// /arm-io/smc/iop-smc-nub/smc-charger-util-1
STATIC CONST TEST_PROPERTY mTreeSmcChargerUtil1[] = {
    TEST_TEXT ("device_type", "smc-charger-util"),
    TEST_TEXT ("compatible", "smc-charger-util"),
    TEST_WORDS ("dock-num", 2),
    TEST_WORDS ("port-number", 2),
    TEST_WORDS ("port-type", 2),
    TEST_WORDS ("AAPL,phandle", 0x10F),
};

// /arm-io/smc/iop-smc-nub/smc-charger-util-3
STATIC CONST TEST_PROPERTY mTreeSmcChargerUtil3[] = {
    TEST_TEXT ("device_type", "smc-charger-util"),
    TEST_TEXT ("compatible", "smc-charger-util"),
    TEST_WORDS ("dock-num", 3),
    TEST_WORDS ("port-number", 4),
    TEST_WORDS ("port-type", 2),
    TEST_WORDS ("AAPL,phandle", 0x110),
};

// /arm-io/smc/iop-smc-nub/smc-charger-util
STATIC CONST TEST_PROPERTY mTreeSmcChargerUtil[] = {
    TEST_WORDS ("magsafe-auth", 1),
    TEST_TEXT ("device_type", "smc-charger-util"),
    TEST_WORDS ("port-number", 1),
    TEST_WORDS ("dock-num", 4),
    TEST_WORDS ("port-type", 0x11),
    TEST_WORDS ("AAPL,phandle", 0x111),
};

// /arm-io/dockchannel-mtp
STATIC CONST TEST_PROPERTY mTreeDockchannelMtp[] = {
    TEST_WORDS ("interrupt-parent", 0xD5),
    TEST_WORDS ("dock-wstat-mask", 0xFFF),
    TEST_TEXT ("compatible", "dockchannel,t8002"),
    TEST_WORDS ("channel-number", 0),
    TEST_WORDS ("interrupts", 0x445),
    TEST_TEXT ("channel-name", "channel0"),
    TEST_WORDS ("reg",
        0x94B00000, 1, 0x1000, 0,
        0x94B14000, 1, 0x1000, 0,
        0x94B30000, 1, 0x1000, 0,
        0x94B34000, 1, 0x1000, 0,
        0x94B28000, 1, 0x1000, 0,
        0x94B2C000, 1, 0x1000, 0),
    TEST_WORDS ("AAPL,phandle", 0x11A),
    TEST_TEXT ("device_type", "dockchannel-mtp"),
    TEST_WORDS ("fifo-size", 0x800),
};

// /arm-io/dockchannel-mtp/mtp-transport
STATIC CONST TEST_PROPERTY mTreeMtpTransport[] = {
    TEST_TEXT ("hibernator-target", "AppleHIDTransportDeviceFIFO"),
    TEST_TEXT ("hid-merge-personality", "MTP_SYS"),
    TEST_TEXT ("compatible", "aht-hibernator"),
    TEST_TEXT ("role", "MTP"),
    TEST_EMPTY ("aud-early-boot-critical"),
    TEST_WORDS ("image-tag", 0x6D747066),
    TEST_WORDS ("iommu-parent", 0x124),
    TEST_TEXT ("hid-transport-mux", "mtp-aop-mux"),
    TEST_TEXT ("device_type", "mtp-transport"),
    TEST_WORDS ("AAPL,phandle", 0x11B),
};

// /arm-io/dockchannel-mtp/mtp-transport/multi-touch
STATIC CONST TEST_PROPERTY mTreeMultiTouch[] = {
    TEST_TEXT ("bootloader-type", "CBOR"),
    TEST_TEXT ("hid-merge-personality", "C1FD0,5"),
    TEST_WORDS ("function-afe-reset", 0x10D, 0x704B5734, 0x67503163, 0x10000),
    TEST_EMPTY ("aud-early-boot-critical"),
    TEST_WORDS ("image-tag", 0x6D746677),
    TEST_TEXT ("device_type", "multi-touch"),
    TEST_WORDS ("AAPL,phandle", 0x11C),
};

// /arm-io/dockchannel-mtp/mtp-transport/keyboard
STATIC CONST TEST_PROPERTY mTreeKeyboard[] = {
    TEST_WORDS ("kblang-calibration", 0x11220133, 0x4455, 0, 0),
    TEST_TEXT ("device-name", "HID_DEVICE_NAME"),
    TEST_WORDS ("AAPL,phandle", 0x11D),
    TEST_TEXT ("device_type", "keyboard"),
};

// /arm-io/dockchannel-mtp/mtp-transport/stm
STATIC CONST TEST_PROPERTY mTreeStm[] = {
    TEST_TEXT ("bootloader-type", "HIDDevice"),
    TEST_TEXT ("hid-merge-personality", "IPD"),
    TEST_WORDS ("AAPL,phandle", 0x11E),
    TEST_WORDS ("function-stm-reset", 0x10D, 0x704B5734, 0x67503164, 0x10000),
    TEST_EMPTY ("aud-early-boot-critical"),
    TEST_WORDS ("image-tag", 0x69706466),
    TEST_TEXT ("device-name", "HID_DEVICE_NAME"),
    TEST_TEXT ("device_type", "stm"),
};

// /arm-io/dockchannel-mtp/mtp-transport/actuator
STATIC CONST TEST_PROPERTY mTreeActuator[] = {
    TEST_TEXT ("device-name", "HID_DEVICE_NAME"),
    TEST_WORDS ("AAPL,phandle", 0x11F),
    TEST_TEXT ("device_type", "actuator"),
};

// /arm-io/dockchannel-mtp/mtp-transport/tp-accel
STATIC CONST TEST_PROPERTY mTreeTpAccel[] = {
    TEST_TEXT ("device-name", "HID_DEVICE_NAME"),
    TEST_WORDS ("AAPL,phandle", 0x120),
    TEST_TEXT ("device_type", "tp-accel"),
};

// /arm-io/mtp
STATIC CONST TEST_PROPERTY mTreeMtp[] = {
    TEST_TEXT ("compatible", "iop,ascwrap-v6"),
    TEST_WORDS ("iommu-parent", 0x124),
    TEST_WORDS ("interrupt-parent", 0xD5),
    TEST_WORDS ("interrupts", 0x45A, 0x459, 0x45C, 0x45B),
    TEST_EMPTY ("clock-gates"),
    TEST_EMPTY ("clock-ids"),
    TEST_WORDS ("reg", 0x94600000, 1, 0x88000, 0, 0x94050000, 1, 0x4000, 0),
    TEST_WORDS ("AAPL,phandle", 0x121),
    TEST_WORDS ("iop-version", 1),
    TEST_TEXT ("device_type", "mtp"),
    TEST_TEXT ("role", "MTP"),
    TEST_EMPTY ("power-gates"),
    TEST_TEXT ("segment-names", "__TEXT;__DATA;__OS_LOG"),
    TEST_WORDS ("segment-ranges",
        0x94C00000, 3, 0x1000000, 0, 0x94C00000, 3, 0x5F000, 3,
        0x94C5F000, 3, 0x105F000, 0, 0x94C5F000, 3, 0x6C000, 2,
        0x4D58000, 0x100, 0x10CB000, 0, 0x4D58000, 0x100, 0x3000, 0xA),
};

// /arm-io/mtp/iop-mtp-nub
STATIC CONST TEST_PROPERTY mTreeIopMtpNub[] = {
    TEST_WORDS ("AAPL,phandle", 0x122),
    TEST_TEXT ("compatible", "iop-nub,rtbuddy-v2"),
    TEST_EMPTY ("sleep-on-hibernate"),
    TEST_EMPTY ("watchdog-enable"),
    TEST_WORDS ("coredump-enable", 0x40),
    TEST_WORDS ("no-shutdown", 1),
    TEST_EMPTY ("no-firmware-service"),
    TEST_EMPTY ("cold-boot-after-hibernate"),
    TEST_TEXT ("uuid", "00000000-1111-2222-3333-444444444444"),
    TEST_TEXT ("segment-names", "__TEXT;__DATA;__OS_LOG"),
    TEST_WORDS ("segment-ranges",
        0x94C00000, 3, 0x1000000, 0, 0x94C00000, 3, 0x5F000, 3,
        0x94C5F000, 3, 0x105F000, 0, 0x94C5F000, 3, 0x6C000, 2,
        0x4D58000, 0x100, 0x10CB000, 0, 0x4D58000, 0x100, 0x3000, 0xA),
    TEST_WORDS ("pre-loaded", 1),
};

// /arm-io/dart-mtp
STATIC CONST TEST_PROPERTY mTreeDartMtp[] = {
    TEST_WORDS ("flush-by-dva", 0),
    TEST_TEXT ("compatible", "dart,t8110"),
    TEST_WORDS ("page-size", 0x4000),
    TEST_WORDS ("interrupt-parent", 0xD5),
    TEST_WORDS ("interrupts", 0x443),
    TEST_WORDS ("reg",
        0x94800000, 1, 0xC000, 0,
        0x94810000, 1, 0x4000, 0,
        0x882A4000, 1, 0x4000, 0),
    TEST_EMPTY ("no-sleep"),
    TEST_WORDS ("sid-count", 0x10),
    TEST_WORDS ("dapf-instance-0",
        0x8828C000, 3, 0x8828C06F, 3, 1, 0, 0, 0, 0, 0, 0, 0, 0x10300,
        0x8829C800, 3, 0x8829C9A7, 3, 1, 0, 0, 0, 0, 0, 0, 0, 0x10300,
        0x882D0018, 3, 0x882D0113, 3, 1, 0, 0, 0, 0, 0, 0, 0, 0x10300,
        0x882BC020, 3, 0x882BC023, 3, 1, 0, 0, 0, 0, 0, 0, 0, 0x10300,
        0x88240324, 3, 0x88240327, 3, 1, 0, 0, 0, 0, 0, 0, 0, 0x10300,
        0x9060C110, 3, 0x9060C81F, 3, 1, 0, 0, 0, 0, 0, 0, 0, 0x10300,
        0x90668000, 3, 0x90668003, 3, 1, 0, 0, 0, 0, 0, 0, 0, 0x10300,
        0x90EEC000, 3, 0x90EEE000, 3, 1, 0, 0, 0, 0, 0, 0, 0, 0x10300,
        0x881F006C, 3, 0x881F007B, 3, 1, 0, 0, 0, 0, 0, 0, 0, 0x10300),
    TEST_WORDS ("error-reflector", 0x7F000000, 2),
    TEST_TEXT ("device_type", "dart"),
    TEST_WORDS ("remap", 1, 2, 3),
    TEST_WORDS ("sid", 0),
    TEST_WORDS ("dart-options", 0x25),
    TEST_WORDS ("vm-base", 0x4000, 0x100),
    TEST_WORDS ("instance",
        0x44415254, 0, 0x54524144, 0, 0x44415046, 0, 0x46504144, 0,
        0x47415046, 0, 0x505F5350, 0x544F52),
    TEST_WORDS ("AAPL,phandle", 0x123),
    TEST_WORDS ("vm-size", 0xFFFF0000, 0),
    TEST_WORDS ("dart-tunables-instance-0",
        0x20C, 4, 0x80000121, 0, 0x80000121, 0, 0x220, 4,
        0xF0F0F, 0, 0xF0F0F, 0, 0x224, 4, 0xFFFFFF, 0,
        0x80808, 0, 0x300, 4, 0x1F31, 0, 1, 0,
        0x308, 4, 0x3FFFFFFC, 0, 0x10000000, 0, 0x310, 4,
        0x3FFFFFFC, 0, 0x3FFFFFFC, 0),
    TEST_WORDS ("clock-protection-slice-index", 0xE),
    TEST_WORDS ("dart-id", 0xC),
};

// /arm-io/dart-mtp/mapper-mtp
STATIC CONST TEST_PROPERTY mTreeMapperMtp[] = {
    TEST_WORDS ("AAPL,phandle", 0x124),
    TEST_WORDS ("reg", 0),
    TEST_TEXT ("compatible", "iommu-mapper"),
    TEST_TEXT ("device_type", "dart-mapper"),
};

STATIC CONST TEST_NODE mCoprocessorsOfM4MacBookPro[] = {
    TEST_NODE_OF (0, "smc", mTreeSmc),
    TEST_NODE_OF (1, "iop-smc-nub", mTreeIopSmcNub),
    TEST_NODE_OF (2, "smc-pmu", mTreeSmcPmu),
    TEST_NODE_OF (2, "smc-charger-util-0", mTreeSmcChargerUtil0),
    TEST_NODE_OF (2, "smc-charger-util-1", mTreeSmcChargerUtil1),
    TEST_NODE_OF (2, "smc-charger-util-3", mTreeSmcChargerUtil3),
    TEST_NODE_OF (2, "smc-charger-util", mTreeSmcChargerUtil),
    TEST_NODE_OF (0, "dockchannel-mtp", mTreeDockchannelMtp),
    TEST_NODE_OF (1, "mtp-transport", mTreeMtpTransport),
    TEST_NODE_OF (2, "multi-touch", mTreeMultiTouch),
    TEST_NODE_OF (2, "keyboard", mTreeKeyboard),
    TEST_NODE_OF (2, "stm", mTreeStm),
    TEST_NODE_OF (2, "actuator", mTreeActuator),
    TEST_NODE_OF (2, "tp-accel", mTreeTpAccel),
    TEST_NODE_OF (0, "mtp", mTreeMtp),
    TEST_NODE_OF (1, "iop-mtp-nub", mTreeIopMtpNub),
    TEST_NODE_OF (0, "dart-mtp", mTreeDartMtp),
    TEST_NODE_OF (1, "mapper-mtp", mTreeMapperMtp),
};

//
// /chosen: where the DRAM is, and the memory map of the coprocessors. Of the 79
// regions of that map these are here: one in DRAM that holds most of it (24), the
// SRAM of AOP (31), of MTP (91) and of PMP (32).
//
STATIC CONST TEST_PROPERTY mChosenOfM4MacBookPro[] = {
    TEST_WORDS ("dram-base", 0, 0x100),
    TEST_WORDS ("dram-size", 0, 4),
};

STATIC CONST TEST_PROPERTY mTreeCarveoutMemoryMap[] = {
    TEST_WORDS ("AAPL,phandle", 9),
    TEST_WORDS ("region-id-24", 0x02E70000, 0x100, 0xD87A0000, 3),
    TEST_TEXT ("region-name-id-24", ""),
    TEST_WORDS ("region-id-31", 0x90C00000, 3, 0x2F0000, 0),
    TEST_WORDS ("region-id-91", 0x94C00000, 3, 0x140000, 0),
    TEST_WORDS ("region-id-32", 0x80500000, 3, 0xC0000, 0),
};

STATIC CONST TEST_NODE mChosenNodesOfM4MacBookPro[] = {
    TEST_NODE_OF (0, "carveout-memory-map", mTreeCarveoutMemoryMap),
};

//
// The M4 MacBook Pro as m1n1 hands it to the firmware: mM4MacBookProWithAns and the
// coprocessors
//
STATIC CONST TEST_MACHINE mM4MacBookProWhole = {
    0x8132,
    mCpusOfM4MacBookPro, ARRAY_SIZE(mCpusOfM4MacBookPro),
    mRangesOfM4MacBookPro, ARRAY_SIZE(mRangesOfM4MacBookPro),
    { 0x1ad200000ULL, 0x4000 },
    &mAnsOfM4MacBookPro,
    mAicToGicOfM4MacBookPro, ARRAY_SIZE(mAicToGicOfM4MacBookPro),
    TestUartNamed,
    &mPmgrOfM4MacBookPro, NULL,
    mCoprocessorsOfM4MacBookPro, ARRAY_SIZE(mCoprocessorsOfM4MacBookPro),
    mChosenOfM4MacBookPro, ARRAY_SIZE(mChosenOfM4MacBookPro),
    mChosenNodesOfM4MacBookPro, ARRAY_SIZE(mChosenNodesOfM4MacBookPro),
    NULL, 0
};

#endif // COPROCESSOR_TREE_H_
