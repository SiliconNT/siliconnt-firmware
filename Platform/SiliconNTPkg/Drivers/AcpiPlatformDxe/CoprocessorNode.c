/** @file
  Whether the DSDT describes MTP and SMC, and with what. See CoprocessorNode.h.

  The tables below are the list of SiliconNT docs/findings/0095, "附表": the
  properties that describe hardware, in the order they have in the _DSD.

  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include "CoprocessorNode.h"

#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>

#define NODE(Path, Rows)  { Path, NULL, Rows, ARRAY_SIZE (Rows) }

//
// MTP
//
STATIC CONST PROPERTY_ROW  mMtpRows[] = {
  { "compatible",   PropertyStrings },
  { "iop-version",  PropertyInteger },
  { "iommu-parent", PropertyStream  },
  { "clock-gates",  PropertyList    },
  { "clock-ids",    PropertyList    },
  { "power-gates",  PropertyList    },
};

STATIC CONST PROPERTY_ROW  mMtpNubRows[] = {
  { "compatible",                PropertyStrings },
  { "pre-loaded",                PropertyInteger },
  { "no-shutdown",               PropertyInteger },
  { "coredump-enable",           PropertyInteger },
  { "watchdog-enable",           PropertyPresent },
  { "no-firmware-service",       PropertyPresent },
  { "sleep-on-hibernate",        PropertyPresent },
  { "cold-boot-after-hibernate", PropertyPresent },
};

STATIC CONST PROPERTY_ROW  mDartRows[] = {
  { "compatible",                   PropertyStrings     },
  { "instance",                     PropertyWords       },
  { "page-size",                    PropertyInteger     },
  { "sid-count",                    PropertyInteger     },
  { "sid",                          PropertyWords       },
  { "remap",                        PropertyWords       },
  { "vm-base",                      PropertyWords       },
  { "vm-size",                      PropertyWords       },
  { "dart-options",                 PropertyInteger     },
  { "dart-id",                      PropertyInteger     },
  { "flush-by-dva",                 PropertyInteger     },
  { "error-reflector",              PropertyWords       },
  { "clock-protection-slice-index", PropertyWords       },
  { "no-sleep",                     PropertyPresent     },
  { "dart-tunables-instance-",      PropertyWords, TRUE },
  { "dapf-instance-",               PropertyWords, TRUE },
};

STATIC CONST PROPERTY_ROW  mDockChannelRows[] = {
  { "compatible",      PropertyStrings },
  { "channel-number",  PropertyInteger },
  { "channel-name",    PropertyString  },
  { "fifo-size",       PropertyInteger },
  { "dock-wstat-mask", PropertyInteger },
};

STATIC CONST PROPERTY_ROW  mTransportRows[] = {
  { "interfaces",            PropertyChildren },
  { "iommu-parent",          PropertyStream   },
  { "hid-transport-mux",     PropertyString   },
  { "hid-merge-personality", PropertyString   },
  { "image-tag",             PropertyInteger  },
};

STATIC CONST PROPERTY_ROW  mMultiTouchRows[] = {
  { "bootloader-type",       PropertyString   },
  { "image-tag",             PropertyInteger  },
  { "hid-merge-personality", PropertyString   },
  { "function-afe-reset",    PropertyFunction },
};

STATIC CONST PROPERTY_ROW  mKeyboardRows[] = {
  { "kblang-calibration", PropertyWords },
};

STATIC CONST PROPERTY_ROW  mStmRows[] = {
  { "bootloader-type",       PropertyString   },
  { "image-tag",             PropertyInteger  },
  { "hid-merge-personality", PropertyString   },
  { "function-stm-reset",    PropertyFunction },
};

STATIC CONST PROPERTY_NODE  mMtpProperties[] = {
  NODE ("/arm-io/mtp",                                         mMtpRows),
  NODE ("/arm-io/mtp/iop-mtp-nub",                             mMtpNubRows),
  NODE ("/arm-io/dart-mtp",                                    mDartRows),
  NODE ("/arm-io/dockchannel-mtp",                             mDockChannelRows),
  NODE ("/arm-io/dockchannel-mtp/mtp-transport",               mTransportRows),
  NODE ("/arm-io/dockchannel-mtp/mtp-transport/multi-touch",   mMultiTouchRows),
  NODE ("/arm-io/dockchannel-mtp/mtp-transport/keyboard",      mKeyboardRows),
  NODE ("/arm-io/dockchannel-mtp/mtp-transport/stm",           mStmRows),
};

//
// SMC
//
STATIC CONST PROPERTY_ROW  mSmcRows[] = {
  { "compatible",       PropertyStrings },
  { "iop-version",      PropertyInteger },
  { "aot-power",        PropertyInteger },
  { "timers-reg-index", PropertyInteger },
  { "clock-gates",      PropertyList    },
  { "clock-ids",        PropertyList    },
  { "power-gates",      PropertyList    },
};

STATIC CONST PROPERTY_ROW  mSmcNubRows[] = {
  { "compatible",                PropertyStrings },
  { "pre-loaded",                PropertyInteger },
  { "running",                   PropertyInteger },
  { "no-shutdown",               PropertyInteger },
  { "user-power-managed",        PropertyInteger },
  { "coredump-enable",           PropertyInteger },
  { "coredump-active-only",      PropertyPresent },
  { "quiesced",                  PropertyPresent },
  { "continuous-time",           PropertyPresent },
  { "watchdog-enable",           PropertyPresent },
  { "no-hibernate-sleep",        PropertyPresent },
  { "cold-boot-after-hibernate", PropertyPresent },
};

STATIC CONST PROPERTY_ROW  mSmcPmuRows[] = {
  { "has_lid_open_sensor", PropertyInteger      },
  { "event_name-bit",      PropertyString, TRUE },
};

STATIC CONST PROPERTY_ROW  mChargerRows[] = {
  { "dock-num",     PropertyInteger },
  { "port-number",  PropertyInteger },
  { "port-type",    PropertyInteger },
  { "magsafe-auth", PropertyInteger },
};

STATIC CONST PROPERTY_NODE  mSmcProperties[] = {
  NODE ("/arm-io/smc",                     mSmcRows),
  NODE ("/arm-io/smc/iop-smc-nub",         mSmcNubRows),
  NODE ("/arm-io/smc/iop-smc-nub/smc-pmu", mSmcPmuRows),
  { "/arm-io/smc/iop-smc-nub", "smc-charger-util", mChargerRows, ARRAY_SIZE (mChargerRows) },
};

//
// _CRS and the names of what is in it
//
STATIC
VOID
TakeResources (
  IN OUT COPROCESSOR_NODE  *Node
  )
{
  DEVICE_PROPERTIES  *Properties = &Node->Properties;
  CONST CHAR8        *Names;
  UINTN              Count;
  UINTN              Index;
  BOOLEAN            Fit;

  Names = Properties->Text + Properties->TextUsed;
  Count = 0;
  Fit   = TRUE;
  for (Index = 0; Index < Node->Device.RangeCount; Index++) {
    if (Node->Device.Ranges[Index].PowerManager) {
      continue;
    }

    Node->Memory[Count++] = Node->Device.Ranges[Index].Range;
    Fit                   = Fit && AddName (Properties, Node->Device.Ranges[Index].Block, Node->Device.Ranges[Index].Entry);
  }

  Node->Dsdt.Memory      = Node->Memory;
  Node->Dsdt.MemoryCount = Count;
  Fit                    = Fit && AddNames (Properties, "memory-names", Names, Count);

  Names = Properties->Text + Properties->TextUsed;
  for (Index = 0; Index < Node->Device.LineCount; Index++) {
    Fit = Fit && AddName (Properties, Node->Device.Lines[Index].Block, Node->Device.Lines[Index].Entry);
  }

  Node->Dsdt.Gsiv      = Node->Gsiv;
  Node->Dsdt.GsivCount = Node->Device.LineCount;
  Fit                  = Fit && AddNames (Properties, "interrupt-names", Names, Node->Device.LineCount);

  //
  // The names are the first in Text, and there are at most APPLE_DEVICE_MAX_RANGES
  // and APPLE_DEVICE_MAX_LINES of them, all shorter than 32 characters
  //
  ASSERT (Fit);
}

STATIC
VOID
FindNode (
  IN  CONST APPLE_BOOT_INFO  *Info,
  IN  RETURN_STATUS          (*GetDevice)(IN CONST APPLE_BOOT_INFO *, OUT APPLE_DEVICE *),
  IN  CONST CHAR8            *Name,
  IN  CONST CHAR8            *Hid,
  IN  CONST PROPERTY_NODE    *Properties,
  IN  UINTN                  PropertyNodeCount,
  OUT COPROCESSOR_NODE       *Node
  )
{
  APPLE_AIC_TO_GIC  Table;
  UINTN             Index;

  ZeroMem (Node, sizeof (*Node));

  Node->Status = GetDevice (Info, &Node->Device);
  if (RETURN_ERROR (Node->Status)) {
    Node->Outcome = CoprocessorNodeNoDevice;
    return;
  }

  Node->Status = AppleGetAicToGic (Info, &Table);
  if (RETURN_ERROR (Node->Status)) {
    Node->Outcome = CoprocessorNodeNoTable;
    return;
  }

  for (Index = 0; Index < Node->Device.LineCount; Index++) {
    if (RETURN_ERROR (AppleFindGicInterrupt (&Table, Node->Device.Lines[Index].Aic, &Node->Gsiv[Index]))) {
      Node->Outcome     = CoprocessorNodeLineMissing;
      Node->MissingLine = Index;
      return;
    }
  }

  Node->Outcome   = CoprocessorNodeMade;
  Node->Dsdt.Name = Name;
  Node->Dsdt.Hid  = Hid;
  TakeResources (Node);
  CopyProperties (Info->DeviceTree, Properties, PropertyNodeCount, &Node->Properties);
  Node->Dsdt.Properties    = Node->Properties.Properties;
  Node->Dsdt.PropertyCount = Node->Properties.Count;
}

VOID
FindMtpNode (
  IN  CONST APPLE_BOOT_INFO  *Info,
  OUT COPROCESSOR_NODE       *Node
  )
{
  FindNode (Info, AppleGetMtp, "MTP0", DSDT_MTP_HID, mMtpProperties, ARRAY_SIZE (mMtpProperties), Node);
}

VOID
FindSmcNode (
  IN  CONST APPLE_BOOT_INFO  *Info,
  OUT COPROCESSOR_NODE       *Node
  )
{
  FindNode (Info, AppleGetSmc, "SMC0", DSDT_SMC_HID, mSmcProperties, ARRAY_SIZE (mSmcProperties), Node);
}
