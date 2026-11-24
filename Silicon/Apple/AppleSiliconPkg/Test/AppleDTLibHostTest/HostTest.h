/** @file
  What the host tests of the device tree reader share: stand-ins for the
  firmware's libraries, Library/AppleDTLib itself, a builder of Apple device
  trees, and the tree of one real machine.

  A test includes this file once. The tests of other packages that start from a
  device tree include it too.

  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#ifndef APPLE_DT_HOST_TEST_H_
#define APPLE_DT_HOST_TEST_H_

#include <Base.h>
#include <PiPei.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/HobLib.h>
#include <Library/AppleDTLib.h>

#include <stdio.h>
#include <string.h>

//
// What AppleDTLib.c takes from the firmware's libraries
//
EFI_GUID  gAppleSiliconPkgBootInfoHobGuid;

VOID *EFIAPI GetFirstGuidHob(IN CONST EFI_GUID *Guid)
{
    return NULL;
}

INTN EFIAPI AsciiStrCmp(IN CONST CHAR8 *First, IN CONST CHAR8 *Second)
{
    return strcmp(First, Second);
}

INTN EFIAPI AsciiStrnCmp(IN CONST CHAR8 *First, IN CONST CHAR8 *Second, IN UINTN Length)
{
    return strncmp(First, Second, Length);
}

UINTN EFIAPI AsciiStrnLenS(IN CONST CHAR8 *String, IN UINTN MaxSize)
{
    return strnlen(String, MaxSize);
}

CHAR8 *EFIAPI AsciiStrStr(IN CONST CHAR8 *String, IN CONST CHAR8 *SearchString)
{
    return (CHAR8 *)strstr(String, SearchString);
}

UINTN EFIAPI AsciiStrLen(IN CONST CHAR8 *String)
{
    return strlen(String);
}

VOID *EFIAPI CopyMem(OUT VOID *Destination, IN CONST VOID *Source, IN UINTN Length)
{
    return memmove(Destination, Source, Length);
}

VOID *EFIAPI ZeroMem(OUT VOID *Buffer, IN UINTN Length)
{
    return memset(Buffer, 0, Length);
}

//
// The library itself. Included, not compiled on its own: the firmware build
// declares the GUID above in a generated header.
//
#include "../../Library/AppleDTLib/AppleDTLib.c"

//
// Device tree builder. A node is two counts, then its properties (32-byte
// name, 32-bit length, value padded to 4 bytes), then its children.
//
STATIC UINT8  mTree[131072];
STATIC UINTN  mUsed;

STATIC VOID Node(IN UINT32 Properties, IN UINT32 Children)
{
    memcpy(mTree + mUsed, &Properties, 4);
    memcpy(mTree + mUsed + 4, &Children, 4);
    mUsed += 8;
}

STATIC VOID Prop(IN CONST CHAR8 *Key, IN CONST VOID *Value, IN UINT32 Length)
{
    memset(mTree + mUsed, 0, DT_KEY_LEN + 4 + ((Length + 3) & ~3U));
    strcpy((CHAR8 *)mTree + mUsed, Key);
    memcpy(mTree + mUsed + DT_KEY_LEN, &Length, 4);
    memcpy(mTree + mUsed + DT_KEY_LEN + 4, Value, Length);
    mUsed += DT_KEY_LEN + 4 + ((Length + 3) & ~3U);
}

STATIC VOID Text(IN CONST CHAR8 *Key, IN CONST CHAR8 *Value)
{
    Prop(Key, Value, (UINT32)strlen(Value) + 1);
}

STATIC VOID Name(IN CONST CHAR8 *Value)
{
    Text("name", Value);
}

STATIC VOID Number(IN CONST CHAR8 *Key, IN UINT32 Value)
{
    Prop(Key, &Value, sizeof(Value));
}

//
// Guest memory for what a loader puts next to the tree: one page, which holds the
// table of interrupt numbers when a machine has one (BuildMachine()).
//
STATIC UINT8 mGuestMemory[EFI_PAGE_SIZE] __attribute__((aligned(EFI_PAGE_SIZE)));

//
// The tree a test has built, as the functions under test take it. The boot
// arguments describe mGuestMemory as the memory.
//
STATIC CONST APPLE_BOOT_INFO *BuiltTree(VOID)
{
    STATIC struct boot_args mBootArgs;
    STATIC APPLE_BOOT_INFO  mInfo;

    mBootArgs.phys_base  = (UINT64)(UINTN)mGuestMemory;
    mBootArgs.mem_size   = sizeof(mGuestMemory);
    mInfo.BootArgs       = &mBootArgs;
    mInfo.DeviceTree     = (dt_node_t *)mTree;
    mInfo.DeviceTreeSize = (UINT32)mUsed;
    mInfo.UartBase       = 0;
    return &mInfo;
}

//
// Checks
//
STATIC UINTN  mChecks;
STATIC UINTN  mFailures;

STATIC BOOLEAN Expect(IN BOOLEAN Ok, IN CONST CHAR8 *What)
{
    mChecks++;
    printf("%s  %s\n", Ok ? "ok  " : "FAIL", What);
    if(!Ok) {
        mFailures++;
    }
    return Ok;
}

STATIC int Verdict(VOID)
{
    if(mFailures != 0) {
        printf("%u of %u check(s) failed\n", (unsigned)mFailures, (unsigned)mChecks);
        return 1;
    }
    printf("all %u checks passed\n", (unsigned)mChecks);
    return 0;
}

//
// A machine, reduced to what the firmware reads from its tree:
//
// /device-tree              #address-cells 2, #size-cells 2
//   /ans                    with a disk controller: a node of that name that
//                           must not be taken for /arm-io/ans
//   /chosen                 chip-id
//     /memory-map           with a table of interrupt numbers: AICToGIC
//   /cpus                   #address-cells 1, #size-cells 0
//     /cpu<n> ...           cluster-type, cpu-id, state, device_type, reg
//     /not-a-core           a child without "cpu-id", which must not be taken
//   /arm-io                 #address-cells 2, #size-cells 2, ranges
//     /uart1                a "reg" that must not be taken
//     /uart0                reg; with its name cleared, or left out (Uart)
//     /ans                  with a disk controller: nvme-interrupt-idx,
//                           interrupt-parent, interrupts, name, reg,
//                           nvme-secure-bar, clock-gates
//       /iop-ans-nub        a "reg" that must not be taken
//     /sart-ans             reg, sart-version, compatible
//     /pmgr                 with a power manager: compatible, reg, ps-regs or
//                           ps-groups, devices
//     /pmgr-child           the same, when the machine has one
//     ...                   the nodes of ArmIoNodes, as they are (CoprocessorTree.h)
// and in /chosen the properties of ChosenProperties and the nodes of ChosenNodes.
//
typedef struct
{
    UINT32      Number;         // "cpu-id"
    UINT32      Reg;
    CONST CHAR8 *Class;         // "cluster-type"; NULL leaves the property out
    CONST CHAR8 *State;         // NULL leaves the property out
} TEST_CPU;

//
// The disk controller. Omit leaves out what its bits name.
//
#define OMIT_ANS                BIT0    // /arm-io/ans
#define OMIT_ANS_REG            BIT1
#define OMIT_ANS_INTERRUPTS     BIT2
#define OMIT_ANS_INTERRUPT_IDX  BIT3
#define OMIT_SART               BIT4    // /arm-io/sart-ans
#define OMIT_SART_REG           BIT5
#define OMIT_SART_VERSION       BIT6
#define OMIT_ANS_CLOCK_GATES    BIT7

typedef struct
{
    CONST UINT64 (*Reg)[2];         // address on the bus, size
    UINT32       RegLength;         // in bytes, as are the other lengths
    CONST UINT32 *Interrupts;
    UINT32       InterruptsLength;
    UINT32       NvmeInterruptIdx;
    BOOLEAN      SecureBar;         // "nvme-secure-bar", empty, is there
    CONST UINT64 (*SartReg)[2];
    UINT32       SartRegLength;
    UINT32       Omit;
    CONST VOID   *SartVersion;      // "sart-version" of sart-ans
    UINT32       SartVersionLength;
    CONST CHAR8  *SartCompatible;   // "compatible" of sart-ans: strings, each ended by a zero; NULL leaves it out
    UINT32       SartCompatibleLength;
    CONST UINT32 *ClockGates;       // "clock-gates" of ans
    UINT32       ClockGatesLength;
} TEST_ANS;

//
// A power manager node. Omit leaves out what its bits name.
//
#define OMIT_PMGR_REG           BIT0
#define OMIT_PMGR_GROUPS        BIT1
#define OMIT_PMGR_DEVICES       BIT2

typedef struct
{
    CONST CHAR8  *Compatible;       // strings, each ended by a zero
    UINT32       CompatibleLength;
    CONST UINT64 (*Reg)[2];         // address on the bus, size
    UINT32       RegLength;
    CONST UINT32 *Groups;           // "ps-regs", or "ps-groups" with ByGroup
    UINT32       GroupsLength;
    BOOLEAN      ByGroup;
    CONST UINT8  *Devices;          // "devices", 48 bytes an entry
    UINT32       DevicesLength;
    UINT32       Omit;
} TEST_PMGR;

//
// How the tree holds /arm-io/uart0
//
typedef enum
{
    TestUartNamed,          // as iBoot hands it over
    TestUartNameCleared,    // as m1n1 hands it over when no host is attached: the
                            // value of "name" all zero, its length kept
                            // (SiliconNT docs/findings/0068, conclusion 7)
    TestUartLeftOut         // not in the tree at all
} TEST_UART;

//
// A node and its properties but the name, as a table. A list of these is a list of
// trees: each node is followed by its children, whose Depth is one more.
//
typedef struct
{
    CONST CHAR8 *Key;
    CONST VOID  *Value;
    UINT32      Length;
} TEST_PROPERTY;

typedef struct
{
    UINT32              Depth;
    CONST CHAR8         *Name;
    CONST TEST_PROPERTY *Properties;
    UINTN               PropertyCount;
} TEST_NODE;

#define TEST_WORDS(Key, ...)  { Key, (CONST UINT32[]){ __VA_ARGS__ }, sizeof((CONST UINT32[]){ __VA_ARGS__ }) }
#define TEST_TEXT(Key, Text)  { Key, Text, sizeof(Text) }
#define TEST_EMPTY(Key)       { Key, "", 0 }
#define TEST_NODE_OF(Depth, Name, Properties)  { Depth, Name, Properties, ARRAY_SIZE(Properties) }

//
// A change to these nodes: to the one called Node, or to /chosen with Node NULL.
// With Key NULL the node and all below it is left out. Else the property is left
// out (Value NULL), or it is there with this value, whether the table has it or not.
//
typedef struct
{
    CONST CHAR8 *Node;
    CONST CHAR8 *Key;
    CONST VOID  *Value;
    UINT32      Length;
} TEST_EDIT;

#define EDIT_NO_NODE(Node)            { Node, NULL, NULL, 0 }
#define EDIT_NO_PROPERTY(Node, Key)   { Node, Key, NULL, 0 }
#define EDIT_WORDS(Node, Key, ...)    { Node, Key, (CONST UINT32[]){ __VA_ARGS__ }, sizeof((CONST UINT32[]){ __VA_ARGS__ }) }
#define EDIT_BYTES(Node, Key, Bytes)  { Node, Key, Bytes, sizeof(Bytes) - 1 }   // a string literal, without its last zero

typedef struct
{
    UINT32         ChipId;
    CONST TEST_CPU *Cpus;
    UINTN          CpuCount;
    CONST UINT64   (*Ranges)[3];    // address on the bus, address for the CPU, size
    UINTN          RangeCount;
    UINT64         UartReg[2];      // address on the bus, size
    CONST TEST_ANS *Ans;            // NULL: no disk controller
    CONST UINT32   *AicToGic;       // the numbers of the table; NULL: no /chosen/memory-map
    UINT32         AicToGicCount;
    TEST_UART      Uart;
    CONST TEST_PMGR *Pmgr;          // /arm-io/pmgr; NULL: none
    CONST TEST_PMGR *PmgrChild;     // /arm-io/pmgr-child; NULL: none
    CONST TEST_NODE *ArmIoNodes;    // more children of /arm-io, and what is below them
    UINTN           ArmIoNodeCount;
    CONST TEST_PROPERTY *ChosenProperties;
    UINTN           ChosenPropertyCount;
    CONST TEST_NODE *ChosenNodes;
    UINTN           ChosenNodeCount;
    CONST TEST_EDIT *Edits;
    UINTN           EditCount;
} TEST_MACHINE;

//
// The disk controller's nodes under /arm-io: ans with its child, sart-ans
//
STATIC VOID BuildAns(IN CONST TEST_ANS *Ans)
{
    STATIC CONST UINT64 NubReg[2]       = { 0x281700000ULL, 0x4000 };
    STATIC CONST UINT32 AicPhandle      = 0xd5;

    if((Ans->Omit & OMIT_ANS) == 0) {
        Node(3 + ((Ans->Omit & OMIT_ANS_INTERRUPT_IDX) == 0 ? 1 : 0) + ((Ans->Omit & OMIT_ANS_INTERRUPTS) == 0 ? 1 : 0) +
             ((Ans->Omit & OMIT_ANS_REG) == 0 ? 1 : 0) + (Ans->SecureBar ? 1 : 0) + ((Ans->Omit & OMIT_ANS_CLOCK_GATES) == 0 ? 1 : 0), 1);
        if((Ans->Omit & OMIT_ANS_CLOCK_GATES) == 0) {
            Prop("clock-gates", Ans->ClockGatesLength != 0 ? (CONST VOID *)Ans->ClockGates : "", Ans->ClockGatesLength);
        }
        if((Ans->Omit & OMIT_ANS_INTERRUPT_IDX) == 0) {
            Number("nvme-interrupt-idx", Ans->NvmeInterruptIdx);
        }
        Number("interrupt-parent", AicPhandle);
        if((Ans->Omit & OMIT_ANS_INTERRUPTS) == 0) {
            Prop("interrupts", Ans->InterruptsLength != 0 ? (CONST VOID *)Ans->Interrupts : "", Ans->InterruptsLength);
        }
        Name("ans");
        if((Ans->Omit & OMIT_ANS_REG) == 0) {
            Prop("reg", Ans->RegLength != 0 ? (CONST VOID *)Ans->Reg : "", Ans->RegLength);
        }
        if(Ans->SecureBar) {
            Prop("nvme-secure-bar", "", 0);
        }
        Text("compatible", "iop,ascwrap-v6");
          Node(2, 0);
          Name("iop-ans-nub");
          Prop("reg", NubReg, sizeof(NubReg));
    }
    if((Ans->Omit & OMIT_SART) == 0) {
        Node(1 + ((Ans->Omit & OMIT_SART_REG) == 0 ? 1 : 0) + ((Ans->Omit & OMIT_SART_VERSION) == 0 ? 1 : 0) +
             (Ans->SartCompatible != NULL ? 1 : 0), 0);
        Name("sart-ans");
        if(Ans->SartCompatible != NULL) {
            Prop("compatible", Ans->SartCompatible, Ans->SartCompatibleLength);
        }
        if((Ans->Omit & OMIT_SART_REG) == 0) {
            Prop("reg", Ans->SartRegLength != 0 ? (CONST VOID *)Ans->SartReg : "", Ans->SartRegLength);
        }
        if((Ans->Omit & OMIT_SART_VERSION) == 0) {
            Prop("sart-version", Ans->SartVersionLength != 0 ? Ans->SartVersion : "", Ans->SartVersionLength);
        }
    }
}

//
// A power manager node under /arm-io
//
STATIC VOID BuildPmgr(IN CONST TEST_PMGR *Pmgr, IN CONST CHAR8 *NodeName)
{
    Node(2 + ((Pmgr->Omit & OMIT_PMGR_REG) == 0 ? 1 : 0) + ((Pmgr->Omit & OMIT_PMGR_GROUPS) == 0 ? 1 : 0) +
         ((Pmgr->Omit & OMIT_PMGR_DEVICES) == 0 ? 1 : 0), 0);
    Name(NodeName);
    Prop("compatible", Pmgr->Compatible, Pmgr->CompatibleLength);
    if((Pmgr->Omit & OMIT_PMGR_GROUPS) == 0) {
        Prop(Pmgr->ByGroup ? "ps-groups" : "ps-regs", Pmgr->GroupsLength != 0 ? (CONST VOID *)Pmgr->Groups : "", Pmgr->GroupsLength);
    }
    if((Pmgr->Omit & OMIT_PMGR_REG) == 0) {
        Prop("reg", Pmgr->RegLength != 0 ? (CONST VOID *)Pmgr->Reg : "", Pmgr->RegLength);
    }
    if((Pmgr->Omit & OMIT_PMGR_DEVICES) == 0) {
        Prop("devices", Pmgr->DevicesLength != 0 ? (CONST VOID *)Pmgr->Devices : "", Pmgr->DevicesLength);
    }
}

//
// The edit of a property, or of the node itself with Key NULL; NULL when there is none
//
STATIC CONST TEST_EDIT *FindEdit(IN CONST TEST_MACHINE *Machine, IN CONST CHAR8 *NodeName, IN CONST CHAR8 *Key)
{
    UINTN Index;

    for(Index = 0; Index < Machine->EditCount; Index++) {
        CONST TEST_EDIT *Edit = &Machine->Edits[Index];

        if((NodeName == NULL) != (Edit->Node == NULL) || (NodeName != NULL && strcmp(NodeName, Edit->Node) != 0)) {
            continue;
        }
        if(Key == NULL ? Edit->Key == NULL : Edit->Key != NULL && strcmp(Key, Edit->Key) == 0) {
            return Edit;
        }
    }
    return NULL;
}

//
// The properties of a node after the edits: how many, or (Emit) into the tree
//
STATIC UINT32 EditedProperties(IN CONST TEST_MACHINE *Machine, IN CONST CHAR8 *NodeName, IN CONST TEST_PROPERTY *Properties, IN UINTN Count, IN BOOLEAN Emit)
{
    UINT32          Total = 0;
    UINTN           Index;
    UINTN           Other;
    CONST TEST_EDIT *Edit;

    for(Index = 0; Index < Count; Index++) {
        Edit = FindEdit(Machine, NodeName, Properties[Index].Key);
        if(Edit != NULL && Edit->Value == NULL) {
            continue;
        }
        Total++;
        if(Emit) {
            Prop(Properties[Index].Key, Edit != NULL ? Edit->Value : Properties[Index].Value, Edit != NULL ? Edit->Length : Properties[Index].Length);
        }
    }
    for(Index = 0; Index < Machine->EditCount; Index++) {
        Edit = &Machine->Edits[Index];
        if(Edit->Value == NULL || FindEdit(Machine, NodeName, Edit->Key) != Edit) {
            continue;
        }
        for(Other = 0; Other < Count && strcmp(Properties[Other].Key, Edit->Key) != 0; Other++) {
        }
        if(Other == Count) {
            Total++;
            if(Emit) {
                Prop(Edit->Key, Edit->Value, Edit->Length);
            }
        }
    }
    return Total;
}

// Where the node at Index of Nodes and all below it ends
STATIC UINTN EndOfNode(IN CONST TEST_NODE *Nodes, IN UINTN Count, IN UINTN Index)
{
    UINTN End = Index + 1;

    while(End < Count && Nodes[End].Depth > Nodes[Index].Depth) {
        End++;
    }
    return End;
}

// The nodes of [First, End) that are children of the same parent, after the edits
STATIC UINT32 EditedNodeCount(IN CONST TEST_MACHINE *Machine, IN CONST TEST_NODE *Nodes, IN UINTN First, IN UINTN End)
{
    UINT32 Total = 0;

    for(; First < End; First = EndOfNode(Nodes, End, First)) {
        Total += FindEdit(Machine, Nodes[First].Name, NULL) == NULL ? 1 : 0;
    }
    return Total;
}

STATIC VOID BuildNodes(IN CONST TEST_MACHINE *Machine, IN CONST TEST_NODE *Nodes, IN UINTN First, IN UINTN End)
{
    UINTN Next;

    for(; First < End; First = Next) {
        CONST TEST_NODE *This = &Nodes[First];

        Next = EndOfNode(Nodes, End, First);
        if(FindEdit(Machine, This->Name, NULL) != NULL) {
            continue;
        }
        Node(1 + EditedProperties(Machine, This->Name, This->Properties, This->PropertyCount, FALSE), EditedNodeCount(Machine, Nodes, First + 1, Next));
        EditedProperties(Machine, This->Name, This->Properties, This->PropertyCount, TRUE);
        Name(This->Name);
        BuildNodes(Machine, Nodes, First + 1, Next);
    }
}

STATIC UINT32 AnsNodeCount(IN CONST TEST_ANS *Ans)
{
    return Ans == NULL ? 0 : ((Ans->Omit & OMIT_ANS) == 0 ? 1 : 0) + ((Ans->Omit & OMIT_SART) == 0 ? 1 : 0);
}

STATIC VOID BuildMachine(IN CONST TEST_MACHINE *Machine)
{
    STATIC CONST UINT64 OtherUart[2] = { 0x1ad204000ULL, 0x4000 };
    STATIC CONST UINT64 OtherAns[2]  = { 0x2c5000000ULL, 0x60000 };
    STATIC CONST UINT32 OtherIrqs[5] = { 1, 2, 3, 4, 5 };
    STATIC CONST CHAR8  ClearedName[sizeof("uart0")] = { 0 };
    UINTN               Index;
    CHAR8               CpuName[16];
    UINT32              *Table = (UINT32 *)mGuestMemory;
    UINT64              Entry[2];

    mUsed = 0;
    Node(3, 3 + (Machine->Ans != NULL ? 1 : 0));
    Name("device-tree");
    Number("#address-cells", 2);
    Number("#size-cells", 2);

      if(Machine->Ans != NULL) {
          Node(4, 0);
          Name("ans");
          Prop("reg", OtherAns, sizeof(OtherAns));
          Prop("interrupts", OtherIrqs, sizeof(OtherIrqs));
          Prop("nvme-secure-bar", "", 0);
      }

      Node(2 + EditedProperties(Machine, NULL, Machine->ChosenProperties, Machine->ChosenPropertyCount, FALSE),
           (Machine->AicToGic != NULL ? 1 : 0) + EditedNodeCount(Machine, Machine->ChosenNodes, 0, Machine->ChosenNodeCount));
      Name("chosen");
      Number("chip-id", Machine->ChipId);
      EditedProperties(Machine, NULL, Machine->ChosenProperties, Machine->ChosenPropertyCount, TRUE);
      if(Machine->AicToGic != NULL) {
          // The table at the start of guest memory, as the loader writes it
          Table[0] = APPLE_AIC_TO_GIC_VERSION;
          Table[1] = Machine->AicToGicCount;
          memcpy(&Table[2], Machine->AicToGic, Machine->AicToGicCount * sizeof(UINT32));
          Entry[0] = (UINT64)(UINTN)mGuestMemory;
          Entry[1] = 8 + 4 * (UINT64)Machine->AicToGicCount;
          Node(2, 0);
          Name("memory-map");
          Prop("AICToGIC", Entry, sizeof(Entry));
      }
      BuildNodes(Machine, Machine->ChosenNodes, 0, Machine->ChosenNodeCount);

      Node(3, (UINT32)Machine->CpuCount + 1);
      Number("#address-cells", 1);
      Number("#size-cells", 0);
      Name("cpus");
      for(Index = 0; Index < Machine->CpuCount; Index++) {
          CONST TEST_CPU *Cpu = &Machine->Cpus[Index];

          Node(4 + (Cpu->Class != NULL ? 1 : 0) + (Cpu->State != NULL ? 1 : 0), 0);
          if(Cpu->Class != NULL) {
              Text("cluster-type", Cpu->Class);
          }
          Number("cpu-id", Cpu->Number);
          snprintf(CpuName, sizeof(CpuName), "cpu%u", (unsigned)Cpu->Number);
          Name(CpuName);
          if(Cpu->State != NULL) {
              Text("state", Cpu->State);
          }
          Text("device_type", "cpu");
          Number("reg", Cpu->Reg);
      }
        Node(2, 0);
        Name("not-a-core");
        Number("reg", 0x7ff);

      Node(4, 1 + (Machine->Uart != TestUartLeftOut ? 1 : 0) + (Machine->Pmgr != NULL ? 1 : 0) + (Machine->PmgrChild != NULL ? 1 : 0) +
              AnsNodeCount(Machine->Ans) + EditedNodeCount(Machine, Machine->ArmIoNodes, 0, Machine->ArmIoNodeCount));
      Number("#address-cells", 2);
      Name("arm-io");
      Prop("ranges", Machine->RangeCount != 0 ? (CONST VOID *)Machine->Ranges : "", (UINT32)(Machine->RangeCount * sizeof(Machine->Ranges[0])));
      Number("#size-cells", 2);
        Node(2, 0);
        Name("uart1");
        Prop("reg", OtherUart, sizeof(OtherUart));
        if(Machine->Uart != TestUartLeftOut) {
            Node(2, 0);
            Prop("reg", Machine->UartReg, sizeof(Machine->UartReg));
            if(Machine->Uart == TestUartNameCleared) {
                Prop("name", ClearedName, sizeof(ClearedName));
            } else {
                Name("uart0");
            }
        }
        if(Machine->Pmgr != NULL) {
            BuildPmgr(Machine->Pmgr, "pmgr");
        }
        if(Machine->Ans != NULL) {
            BuildAns(Machine->Ans);
        }
        if(Machine->PmgrChild != NULL) {
            BuildPmgr(Machine->PmgrChild, "pmgr-child");
        }
        BuildNodes(Machine, Machine->ArmIoNodes, 0, Machine->ArmIoNodeCount);
}

//
// The M4 MacBook Pro (Apple T8132), from its device tree as macOS shows it
// (SiliconNT docs/lab/2026-11-10-q3-macos-probes/ioreg-devicetree.log): ten
// cores, 0-5 efficiency cores, 6-9 performance cores, started on core 6; the 27
// "ranges" entries of /arm-io in the order of the tree; "reg" of uart0.
//
STATIC CONST TEST_CPU mCpusOfM4MacBookPro[] = {
    { 0, 0x000, "E", "waiting" },
    { 1, 0x001, "E", "waiting" },
    { 2, 0x002, "E", "waiting" },
    { 3, 0x003, "E", "waiting" },
    { 4, 0x004, "E", "waiting" },
    { 5, 0x005, "E", "waiting" },
    { 6, 0x100, "P", "running" },
    { 7, 0x101, "P", "waiting" },
    { 8, 0x102, "P", "waiting" },
    { 9, 0x103, "P", "waiting" },
};

STATIC CONST UINT64 mRangesOfM4MacBookPro[][3] = {
    { 0x0000000000ULL, 0x0200000000ULL, 0x3a0000000ULL },
    { 0x0400000000ULL, 0x0400000000ULL, 0x004074000ULL },
    { 0x0408000000ULL, 0x0408000000ULL, 0x004074000ULL },
    { 0x0410000000ULL, 0x0410000000ULL, 0x004074000ULL },
    { 0x0418000000ULL, 0x0418000000ULL, 0x004074000ULL },
    { 0x0800000000ULL, 0x0800000000ULL, 0x200000000ULL },
    { 0x0c00000000ULL, 0x0c00000000ULL, 0x200000000ULL },
    { 0x1000000000ULL, 0x1000000000ULL, 0x200000000ULL },
    { 0x1400000000ULL, 0x1400000000ULL, 0x200000000ULL },
    { 0x0a00000000ULL, 0x0a00000000ULL, 0x080000000ULL },
    { 0x0e00000000ULL, 0x0e00000000ULL, 0x080000000ULL },
    { 0x1200000000ULL, 0x1200000000ULL, 0x080000000ULL },
    { 0x1600000000ULL, 0x1600000000ULL, 0x080000000ULL },
    { 0x1c50000000ULL, 0x1c50000000ULL, 0x010000000ULL },
    { 0x1c60000000ULL, 0x1c60000000ULL, 0x010000000ULL },
    { 0x1c70000000ULL, 0x1c70000000ULL, 0x010000000ULL },
    { 0x1c80000000ULL, 0x1c80000000ULL, 0x010000000ULL },
    { 0x0404000000ULL, 0x0404000000ULL, 0x000074000ULL },
    { 0x040c000000ULL, 0x040c000000ULL, 0x000074000ULL },
    { 0x0414000000ULL, 0x0414000000ULL, 0x000074000ULL },
    { 0x041c000000ULL, 0x041c000000ULL, 0x000074000ULL },
    { 0x0490000000ULL, 0x0490000000ULL, 0x007060000ULL },
    { 0x1cb0000000ULL, 0x1cb0000000ULL, 0x010000000ULL },
    { 0x0bc0000000ULL, 0x0bc0000000ULL, 0x020000000ULL },
    { 0x0b80000000ULL, 0x0b80000000ULL, 0x040000000ULL },
    { 0x0ac0000000ULL, 0x0ac0000000ULL, 0x020000000ULL },
    { 0x0a80000000ULL, 0x0a80000000ULL, 0x040000000ULL },
};

STATIC CONST TEST_MACHINE mM4MacBookPro = {
    0x8132,
    mCpusOfM4MacBookPro, ARRAY_SIZE(mCpusOfM4MacBookPro),
    mRangesOfM4MacBookPro, ARRAY_SIZE(mRangesOfM4MacBookPro),
    { 0x1ad200000ULL, 0x4000 }
};

//
// The same machine with core 6 alone in the tree. m1n1's hypervisor tooling makes
// such a tree when it starts a guest on one core: it removes the cores that are
// not running (proxyclient/m1n1/hv/__init__.py, "if not self.smp").
//
STATIC CONST TEST_MACHINE mM4MacBookProOneCore = {
    0x8132,
    &mCpusOfM4MacBookPro[6], 1,
    mRangesOfM4MacBookPro, ARRAY_SIZE(mRangesOfM4MacBookPro),
    { 0x1ad200000ULL, 0x4000 }
};

//
// The disk controller of the M4 MacBook Pro, from the same device tree: "reg" of
// /arm-io/ans (13 entries) and of /arm-io/sart-ans (3), as addresses on the bus;
// "interrupts", AIC 1004, 1003, 1006, 1005 (the mailbox) and 1018; the
// controller's line is number 4; "nvme-secure-bar" is there; "clock-gates" is
// empty (line 5871 of the log). sart-ans is "sart,coastguard" with
// "sart-version" 3 (lines 6069 and 6074).
//
STATIC CONST UINT64 mAnsRegOfM4MacBookPro[][2] = {
    { 0x281600000ULL, 0x88000 }, { 0x281050000ULL, 0x4000 }, { 0x0ULL, 0x0 }, { 0x285cc0000ULL, 0x60000 },
    { 0x283000000ULL, 0x1000000 }, { 0x285b90000ULL, 0xc000 }, { 0x285d47c00ULL, 0x4000 }, { 0x285d60000ULL, 0x8000 },
    { 0x285d70000ULL, 0x8000 }, { 0x2c5cc0000ULL, 0x10000 }, { 0x285c00000ULL, 0x4000 }, { 0x285c20000ULL, 0x4000 },
    { 0x285b00000ULL, 0x30000 },
};

STATIC CONST UINT64 mSartRegOfM4MacBookPro[][2] = {
    { 0x285c50000ULL, 0xc000 }, { 0x285d44000ULL, 0x4000 }, { 0x285cc0000ULL, 0x4000 },
};

STATIC CONST UINT32 mAnsInterruptsOfM4MacBookPro[] = { 1004, 1003, 1006, 1005, 1018 };

STATIC CONST UINT32 mSartVersionOfM4MacBookPro = 3;

STATIC CONST TEST_ANS mAnsOfM4MacBookPro = {
    mAnsRegOfM4MacBookPro, sizeof(mAnsRegOfM4MacBookPro),
    mAnsInterruptsOfM4MacBookPro, sizeof(mAnsInterruptsOfM4MacBookPro),
    4, TRUE,
    mSartRegOfM4MacBookPro, sizeof(mSartRegOfM4MacBookPro),
    0,
    &mSartVersionOfM4MacBookPro, sizeof(mSartVersionOfM4MacBookPro),
    "sart,coastguard", sizeof("sart,coastguard"),
    NULL, 0
};

//
// The power manager of the M4 MacBook Pro, from the same device tree, the node
// pmgr@80700000 that starts at line 5298 of the log: "compatible" "pmgr1,t8132"
// (line 5340); the first two of its 49 "reg" entries (line 5313), the ones
// "ps-regs" names, as addresses on the bus; all 16 entries of "ps-regs" (line
// 5311); five of the 393 entries of "devices" (line 5360), byte for byte: 0
// (ECPU0), 146 (ISP_SYS), 147 (ANS: flags 0x20, addr_offset 7, psreg_idx 5), 148
// (APCIE_ST) and 325 (ANS-V, flags 0x10, virtual). The controller's register is
// "reg" entry 0, 0x1_8070_0000 on the bus, 0x3_8070_0000 for the CPU, + 0x500 +
// 7 * 8 = 0x3_8070_0538.
//
STATIC CONST UINT64 mPmgrRegOfM4MacBookPro[][2] = {
    { 0x180700000ULL, 0xcc000 }, { 0x188280000ULL, 0x80000 },
};

STATIC CONST UINT32 mPmgrPsRegsOfM4MacBookPro[] = {
    0x0, 0x0, 0xfff,
    0x0, 0x100, 0x3887ff9e,
    0x0, 0x200, 0x7fffe760,
    0x0, 0x300, 0xffffc003,
    0x0, 0x400, 0x7dffffff,
    0x0, 0x500, 0x1f7c0,
    0x0, 0xc00, 0x1,
    0x0, 0x4000, 0x0,
    0x0, 0x8000, 0x1f,
    0x0, 0xc000, 0xf,
    0x0, 0x10000, 0x1,
    0x1, 0x0, 0x3ff77801,
    0x1, 0x4000, 0x0,
    0x1, 0x4100, 0x0,
    0x1, 0x8000, 0x0,
    0x1, 0xc000, 0x0,
};

#define PMGR_DEVICE_INDEX_ANS    2      // in mPmgrDevicesOfM4MacBookPro
#define PMGR_DEVICE_INDEX_ANS_V  4

STATIC CONST UINT8 mPmgrDevicesOfM4MacBookPro[5 * 48] = {
    // ECPU0
    0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x05, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x45, 0x43, 0x50, 0x55, 0x30, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    // ISP_SYS
    0x02, 0x00, 0x00, 0x07, 0x86, 0x00, 0x00, 0x00, 0x00, 0x00, 0x06, 0x05, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x93, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x49, 0x53, 0x50, 0x5f, 0x53, 0x59, 0x53, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    // ANS
    0x20, 0x00, 0x00, 0x00, 0x86, 0x00, 0x00, 0x00, 0x41, 0x02, 0x07, 0x05, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x94, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x41, 0x4e, 0x53, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    // APCIE_ST
    0x00, 0x00, 0x00, 0x00, 0x94, 0x00, 0x00, 0x00, 0x00, 0x00, 0x08, 0x05, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x95, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x41, 0x50, 0x43, 0x49, 0x45, 0x5f, 0x53, 0x54, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    // ANS-V
    0x10, 0x00, 0x00, 0x00, 0x94, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x7b, 0x01, 0x00, 0x00, 0x00, 0x00,
    0x41, 0x4e, 0x53, 0x2d, 0x56, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};

STATIC CONST TEST_PMGR mPmgrOfM4MacBookPro = {
    "pmgr1,t8132", sizeof("pmgr1,t8132"),
    mPmgrRegOfM4MacBookPro, sizeof(mPmgrRegOfM4MacBookPro),
    mPmgrPsRegsOfM4MacBookPro, sizeof(mPmgrPsRegsOfM4MacBookPro), FALSE,
    mPmgrDevicesOfM4MacBookPro, sizeof(mPmgrDevicesOfM4MacBookPro),
    0
};

//
// The table of interrupt numbers m1n1 makes from that tree (src/hv/hv_layout.c,
// hv_aicmap() of SiliconNT/m1n1 622c9313): every value of "interrupts" that goes
// to the AIC, sorted, each once. 397 numbers, 0 the first and 1505 the last, as
// the firmware printed in run R33. AIC 1006 is GIC 257, AIC 1018 is GIC 258.
//
STATIC CONST UINT32 mAicToGicOfM4MacBookPro[] = {
    0, 22, 23, 77, 88, 91, 94, 97, 100, 103, 116, 119, 122, 125, 133, 134,
    135, 136, 137, 138, 139, 140, 141, 142, 143, 144, 145, 215, 256, 257, 266, 268,
    277, 301, 303, 304, 305, 306, 307, 308, 309, 316, 320, 322, 324, 327, 330, 333,
    336, 339, 342, 345, 352, 369, 370, 371, 372, 382, 387, 389, 390, 391, 392, 393,
    396, 397, 398, 399, 400, 401, 402, 403, 404, 405, 407, 423, 424, 425, 426, 427,
    428, 429, 433, 434, 435, 436, 457, 461, 465, 475, 478, 496, 499, 500, 501, 502,
    503, 504, 505, 507, 513, 518, 520, 522, 552, 553, 554, 555, 556, 557, 558, 561,
    562, 563, 564, 566, 567, 568, 569, 570, 571, 572, 573, 574, 575, 576, 577, 578,
    579, 580, 581, 582, 583, 584, 585, 586, 587, 588, 589, 590, 591, 592, 593, 594,
    595, 596, 597, 604, 612, 629, 630, 642, 653, 654, 655, 656, 667, 669, 670, 671,
    672, 676, 677, 678, 681, 688, 731, 732, 733, 734, 739, 741, 743, 744, 748, 749,
    750, 760, 803, 804, 805, 806, 813, 815, 817, 818, 822, 823, 824, 834, 877, 878,
    879, 880, 881, 892, 893, 894, 895, 932, 945, 948, 949, 955, 963, 964, 965, 966,
    967, 970, 975, 976, 977, 978, 990, 992, 993, 994, 995, 996, 997, 999, 1003, 1004,
    1005, 1006, 1018, 1019, 1020, 1022, 1023, 1040, 1041, 1042, 1046, 1056, 1058, 1060, 1061, 1062,
    1063, 1064, 1065, 1066, 1071, 1072, 1074, 1078, 1079, 1080, 1081, 1083, 1091, 1093, 1113, 1114,
    1115, 1116, 1146, 1147, 1184, 1231, 1232, 1233, 1240, 1249, 1250, 1251, 1267, 1268, 1269, 1270,
    1271, 1272, 1273, 1279, 1280, 1281, 1282, 1283, 1287, 1288, 1289, 1290, 1291, 1292, 1293, 1294,
    1295, 1296, 1297, 1298, 1299, 1300, 1301, 1302, 1303, 1304, 1305, 1306, 1307, 1308, 1309, 1310,
    1311, 1312, 1314, 1316, 1317, 1318, 1319, 1329, 1330, 1331, 1332, 1333, 1334, 1335, 1341, 1342,
    1343, 1344, 1345, 1349, 1350, 1351, 1352, 1353, 1354, 1355, 1356, 1357, 1358, 1359, 1360, 1361,
    1362, 1363, 1364, 1365, 1366, 1367, 1368, 1369, 1370, 1371, 1372, 1373, 1374, 1376, 1378, 1379,
    1380, 1381, 1453, 1454, 1455, 1456, 1457, 1458, 1459, 1465, 1466, 1467, 1468, 1469, 1473, 1474,
    1475, 1476, 1477, 1478, 1479, 1480, 1481, 1482, 1483, 1484, 1485, 1486, 1487, 1488, 1489, 1490,
    1491, 1492, 1493, 1494, 1495, 1496, 1497, 1498, 1500, 1502, 1503, 1504, 1505,
};

//
// The M4 MacBook Pro as m1n1 hands it to the firmware: its tree with the disk
// controller and the power manager, and the table of interrupt numbers
//
STATIC CONST TEST_MACHINE mM4MacBookProWithAns = {
    0x8132,
    mCpusOfM4MacBookPro, ARRAY_SIZE(mCpusOfM4MacBookPro),
    mRangesOfM4MacBookPro, ARRAY_SIZE(mRangesOfM4MacBookPro),
    { 0x1ad200000ULL, 0x4000 },
    &mAnsOfM4MacBookPro,
    mAicToGicOfM4MacBookPro, ARRAY_SIZE(mAicToGicOfM4MacBookPro),
    TestUartNamed,
    &mPmgrOfM4MacBookPro, NULL
};

#endif // APPLE_DT_HOST_TEST_H_
