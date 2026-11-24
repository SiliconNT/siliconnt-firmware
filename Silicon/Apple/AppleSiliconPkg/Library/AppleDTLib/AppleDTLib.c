/*
 * pongoOS - https://checkra.in
 *
 * Copyright (C) 2019-2023 checkra1n team
 *
 * This file is part of pongoOS.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 */

#include <Base.h>
#include <PiPei.h>
#include <Library/ArmLib.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/PcdLib.h>
#include <Library/DebugLib.h>
#include <Library/HobLib.h>
#include <Library/AppleDTLib.h>


#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>



EFI_STATUS EFIAPI AppleDTLibInitialize(VOID) {
    return EFI_SUCCESS;
}

static void get_cells(uint64_t *dst, const uint32_t **src, int cells)
{
    *dst = 0;
    for (int i = 0; i < cells; i++)
        *dst |= ((uint64_t) * ((*src)++)) << (32 * i);
}

int dt_check(void *mem, size_t size, size_t *offp)
{
    if(size < sizeof(dt_node_t)) return -1;
    dt_node_t *node = mem;
    size_t off = sizeof(dt_node_t);
    for(size_t i = 0, max = node->nprop; i < max; ++i)
    {
        if(size < off + sizeof(dt_prop_t)) return -1;
        dt_prop_t *prop = (dt_prop_t*)((uintptr_t)mem + off);
        size_t l = prop->len & 0xffffff;
        off += sizeof(dt_prop_t) + ((l + 0x3) & ~0x3);
        if(size < off) return -1;
    }
    for(size_t i = 0, max = node->nchld; i < max; ++i)
    {
        size_t add = 0;
        int r = dt_check((void*)((uintptr_t)mem + off), size - off, &add);
        if(r != 0) return r;
        off += add;
    }
    if(offp) *offp = off;
    return 0;
}

int dt_parse(dt_node_t *node, int depth, size_t *offp, int (*cb_node)(void*, dt_node_t*, int), void *cbn_arg, int (*cb_prop)(void*, dt_node_t*, int, const char*, void*, size_t), void *cbp_arg)
{
    if(cb_node)
    {
        int r = cb_node(cbn_arg, node, depth);
        if(r != 0) return r;
    }
    if(depth >= 0 || cb_prop)
    {
        size_t off = sizeof(dt_node_t);
        for(size_t i = 0, max = node->nprop; i < max; ++i)
        {
            dt_prop_t *prop = (dt_prop_t*)((uintptr_t)node + off);
            size_t l = prop->len & 0xffffff;
            off += sizeof(dt_prop_t) + ((l + 0x3) & ~0x3);
            if(cb_prop)
            {
                int r = cb_prop(cbp_arg, node, depth, prop->key, prop->val, l);
                if(r != 0) return r;
            }
        }
        if(depth >= 0)
        {
            for(size_t i = 0, max = node->nchld; i < max; ++i)
            {
                size_t add = 0;
                int r = dt_parse((dt_node_t*)((uintptr_t)node + off), depth + 1, &add, cb_node, cbn_arg, cb_prop, cbp_arg);
                if(r != 0) return r;
                off += add;
            }
            if(offp) *offp = off;
        }
    }
    return 0;
}

typedef struct
{
    const char *name;
    dt_node_t *node;
    int matchdepth;
} dt_find_cb_t;

static int dt_find_cb(void *a, dt_node_t *node, int depth, const char *key, void *val, size_t len)
{
    dt_find_cb_t *arg = a;
    if(AsciiStrCmp(key, "name") != 0)
    {
        return 0;
    }
    const char *name = arg->name;
    if(name[0] == '/') // Absolute path
    {
        // Don't require "/device-tree" prefix for everything.
        if(depth == 0)
        {
            return 0;
        }
        // If we're in the subtree of a node we didn't match against, then ignore everything.
        if(depth > arg->matchdepth)
        {
            return 0;
        }
        // If this condition is ever true, then we traversed back out of an entry
        // that we matched against, without finding a matching child node.
        if(depth < arg->matchdepth)
        {
            return -1;
        }
        ++name;
        const char *end = AsciiStrStr(name, "/");
        if(end) // Handle non-leaf segment
        {
            size_t size = end - name;
            if(AsciiStrnCmp(name, val, size) == 0 && size + 1 == len && ((const char*)val)[size] == '\0')
            {
                arg->name = end;
                arg->matchdepth = depth + 1;
            }
            return 0;
        }
        // Leaf segment can fall through
    }
    // Simple name
    if(AsciiStrnCmp(name, val, len) == 0 && AsciiStrLen(name) + 1 == len)
    {
        arg->node = node;
        return 1;
    }
    return 0;
}

dt_node_t* dt_find(dt_node_t *node, const char *name)
{
    dt_find_cb_t arg = { name, NULL, 1 };
    dt_parse(node, 0, NULL, NULL, NULL, &dt_find_cb, &arg);
    return arg.node;
}

typedef struct
{
    const char *key;
    void *val;
    size_t len;
} dt_prop_cb_t;

static int dt_prop_cb(void *a, dt_node_t *node, int depth, const char *key, void *val, size_t len)
{
    dt_prop_cb_t *arg = a;
    if(AsciiStrnCmp(arg->key, key, DT_KEY_LEN) == 0)
    {
        arg->val = val;
        arg->len = len;
        return 1;
    }
    return 0;
}

void* dt_prop(dt_node_t *node, const char *key, size_t *lenp)
{
    dt_prop_cb_t arg = { key, NULL, 0 };
    dt_parse(node, -1, NULL, NULL, NULL, &dt_prop_cb, &arg);
    if(arg.val && lenp) *lenp = arg.len;
    return arg.val;
}


typedef struct
{
    const char *name;
    const char *prop;
    size_t size;
} dt_arg_t;


// ========== Legacy/Compat ==========

int dt_check_32(void *mem, uint32_t size, uint32_t *offp) __asm__("_dt_check$32");
int dt_parse_32(dt_node_t *node, int depth, uint32_t *offp, int (*cb_node)(void*, dt_node_t*, int), void *cbn_arg, int (*cb_prop)(void*, dt_node_t*, int, const char*, void*, uint32_t), void *cbp_arg) __asm__("_dt_parse$32");
void* dt_prop_32(dt_node_t *node, const char *key, uint32_t *lenp) __asm__("_dt_prop$32");

int dt_check_32(void *mem, uint32_t size, uint32_t *offp)
{
    size_t off = 0;
    int r = dt_check(mem, size, &off);
    if(offp) *offp = (uint32_t)off;
    return r;
}

typedef struct
{
    int (*cb)(void *a, dt_node_t *node, int depth, const char *key, void *val, uint32_t len);
    void *arg;
} dt_parse_32_cbp_t;

static int dt_parse_32_cbp(void *a, dt_node_t *node, int depth, const char *key, void *val, size_t len)
{
    dt_parse_32_cbp_t *args = a;
    return args->cb(args->arg, node, depth, key, val, (uint32_t)len);
}

int dt_parse_32(dt_node_t *node, int depth, uint32_t *offp, int (*cb_node)(void*, dt_node_t*, int), void *cbn_arg, int (*cb_prop)(void*, dt_node_t*, int, const char*, void*, uint32_t), void *cbp_arg)
{
    dt_parse_32_cbp_t cbp_arg_32 =
    {
        .cb  = cb_prop,
        .arg = cbp_arg,
    };
    int (*cb_prop_32)(void*, dt_node_t*, int, const char*, void*, size_t) = dt_parse_32_cbp;
    cbp_arg = &cbp_arg_32;
    if(!cb_prop)
    {
        cb_prop_32 = NULL;
        cbp_arg = NULL;
    }
    size_t off = 0;
    int r = dt_parse(node, depth, &off, cb_node, cbn_arg, cb_prop_32, cbp_arg);
    if(offp) *offp = (uint32_t)off;
    return r;
}

void* dt_prop_32(dt_node_t *node, const char *key, uint32_t *lenp)
{
    size_t len = 0;
    void *val = dt_prop(node, key, &len);
    if(lenp) *lenp = (uint32_t)len;
    return val;
}



dt_node_t* dt_node(dt_node_t *node, const char *name)
{
    dt_node_t *dev = dt_find(node, name);
    if(!dev)
    {
        DEBUG((DEBUG_INFO, "Missing DeviceTree node: %a\n", name));
    }
    return dev;
}

typedef struct
{
    dt_node_t *path[8];
    dt_node_t *target;
    dt_node_t *parent;
} dt_node_parent_cb_t;

static int dt_node_parent_cb(void *a, dt_node_t *node, int depth)
{
    dt_node_parent_cb_t *arg = a;
    if(node == arg->target)
    {
        if(depth < 1)
        {
            DEBUG((DEBUG_INFO, "DeviceTree parent depth underflow: %d\n", depth));
        }
        arg->parent = arg->path[depth - 1];
        return 1;
    }
    if(depth < 0 || depth >= 8)
    {
        DEBUG((DEBUG_INFO, "DeviceTree parent depth out of bunds: %d\n", depth));
    }
    arg->path[depth] = node;
    return 0;
}

dt_node_t* dt_node_parent(dt_node_t *node)
{
    // Parsing the tree again just to find the parent node is really ugly and inefficient, but for now we're stuck with this.
    // Ideally we'd parse the DeviceTree entirely into heap memory, so we can:
    // a) traverse it faster and in either direction
    // b) overwrite/relocate the original DeviceTree in memory (e.g. for loading a bigger kernel)
    // The problem is that we currently allow clients to directly modify DeviceTree velues and we will need that
    // in one form or another regardless, so we'd have to think about how to design such a writeback.
    dt_node_parent_cb_t arg = { .target = node };
    dt_parse(AppleGetBootInfo()->DeviceTree, 0, NULL, &dt_node_parent_cb, &arg, NULL, NULL);
    return arg.parent;
}

dt_node_t* dt_get(const char *name)
{
    return dt_node(AppleGetBootInfo()->DeviceTree, name);
}

// SiliconNT: the addresses come from the HOB the entry code built, not from fixed PCDs
APPLE_BOOT_INFO* AppleGetBootInfo(VOID)
{
    VOID *Hob = GetFirstGuidHob(&gAppleSiliconPkgBootInfoHobGuid);

    ASSERT(Hob != NULL);
    return (APPLE_BOOT_INFO *)GET_GUID_HOB_DATA(Hob);
}

// SiliconNT: a range the loader names in /chosen/memory-map, base and size.
// RETURN_NOT_FOUND without the node or the property, RETURN_INVALID_PARAMETER
// when the property is not 16 bytes long. *Entry is 0 unless RETURN_SUCCESS.
STATIC RETURN_STATUS ReadMemoryMapEntry(IN CONST APPLE_BOOT_INFO *Info, IN CONST CHAR8 *Key, OUT struct memmap *Entry)
{
    dt_node_t *MemoryMap;
    VOID      *Value;
    size_t    Length = 0;

    Entry->addr = 0;
    Entry->size = 0;

    MemoryMap = dt_find(Info->DeviceTree, "/chosen/memory-map");
    if(MemoryMap == NULL)
    {
        return RETURN_NOT_FOUND;
    }
    Value = dt_prop(MemoryMap, Key, &Length);
    if(Value == NULL)
    {
        return RETURN_NOT_FOUND;
    }
    if(Length != sizeof(*Entry))
    {
        return RETURN_INVALID_PARAMETER;
    }

    // Property values are aligned to 4 bytes only
    CopyMem(Entry, Value, sizeof(*Entry));
    return RETURN_SUCCESS;
}

// SiliconNT: whether a range lies inside the memory the boot arguments describe.
// Written so that neither side can wrap around.
STATIC BOOLEAN InsideMemory(IN CONST APPLE_BOOT_INFO *Info, IN UINT64 Base, IN UINT64 Size)
{
    UINT64 MemoryBase = Info->BootArgs->phys_base;
    UINT64 MemorySize = Info->BootArgs->mem_size;

    return Base >= MemoryBase && Base - MemoryBase < MemorySize && Size <= MemorySize - (Base - MemoryBase);
}

// SiliconNT: the boot disk image the loader describes in the device tree; see AppleDTLib.h
RETURN_STATUS AppleGetBootRamdisk(IN CONST APPLE_BOOT_INFO *Info, OUT UINT64 *Base, OUT UINT64 *Size)
{
    struct memmap Entry;
    RETURN_STATUS Status;

    Status = ReadMemoryMapEntry(Info, "RAMDisk", &Entry);
    *Base = Entry.addr;
    *Size = Entry.size;
    if(RETURN_ERROR(Status))
    {
        return Status;
    }

    if(Entry.size == 0 || (Entry.addr & EFI_PAGE_MASK) != 0 || (Entry.size & EFI_PAGE_MASK) != 0)
    {
        return RETURN_INVALID_PARAMETER;
    }
    if(!InsideMemory(Info, Entry.addr, Entry.size))
    {
        return RETURN_INVALID_PARAMETER;
    }

    return RETURN_SUCCESS;
}

// SiliconNT: the table of interrupt numbers m1n1 made at this boot; see AppleDTLib.h
RETURN_STATUS AppleGetAicToGic(IN CONST APPLE_BOOT_INFO *Info, OUT APPLE_AIC_TO_GIC *Table)
{
    struct memmap Entry;
    RETURN_STATUS Status;
    CONST UINT32  *Words;
    UINT32        Index;

    ZeroMem(Table, sizeof(*Table));

    Status = ReadMemoryMapEntry(Info, "AICToGIC", &Entry);
    Table->Base = Entry.addr;
    Table->Size = Entry.size;
    if(RETURN_ERROR(Status))
    {
        return Status;
    }

    // The header must be there before it can be read
    if((Entry.addr & EFI_PAGE_MASK) != 0 || Entry.size < 2 * sizeof(UINT32) || !InsideMemory(Info, Entry.addr, Entry.size))
    {
        return RETURN_INVALID_PARAMETER;
    }

    Words          = (CONST UINT32 *)(UINTN)Entry.addr;
    Table->Version = Words[0];
    Table->Count   = Words[1];
    if(Table->Version != APPLE_AIC_TO_GIC_VERSION)
    {
        return RETURN_INCOMPATIBLE_VERSION;
    }
    if(Table->Count > APPLE_AIC_TO_GIC_MAX_COUNT || Entry.size != 2 * sizeof(UINT32) + (UINT64)Table->Count * sizeof(UINT32))
    {
        return RETURN_BAD_BUFFER_SIZE;
    }
    for(Index = 1; Index < Table->Count; Index++)
    {
        if(Words[2 + Index] <= Words[2 + Index - 1])
        {
            return RETURN_COMPROMISED_DATA;
        }
    }

    Table->Aic = &Words[2];
    return RETURN_SUCCESS;
}

// SiliconNT: binary search, the numbers being strictly ascending; see AppleDTLib.h
RETURN_STATUS AppleFindGicInterrupt(IN CONST APPLE_AIC_TO_GIC *Table, IN UINT32 Aic, OUT UINT32 *Gic)
{
    UINT32 Low  = 0;
    UINT32 High = Table->Count;     // the number, if there, is at an index in [Low, High)
    UINT32 Middle;

    *Gic = 0;
    if(Table->Aic == NULL)
    {
        return RETURN_NOT_FOUND;
    }
    while(Low < High)
    {
        Middle = Low + (High - Low) / 2;
        if(Table->Aic[Middle] == Aic)
        {
            *Gic = APPLE_AIC_TO_GIC_FIRST_GIC + Middle;
            return RETURN_SUCCESS;
        }
        if(Table->Aic[Middle] < Aic)
        {
            Low = Middle + 1;
        }
        else
        {
            High = Middle;
        }
    }
    return RETURN_NOT_FOUND;
}

// ========== SiliconNT: what the firmware reads about the machine ==========
//
// See AppleDTLib.h. Everything below reads the tree it is given and nothing
// else: no HOB, no global.
//

// Property values are aligned to 4 bytes only, hence the copy
STATIC RETURN_STATUS ReadU32(IN dt_node_t *Node, IN CONST CHAR8 *Key, OUT UINT32 *Value)
{
    size_t Length = 0;
    VOID   *Data  = dt_prop(Node, Key, &Length);

    if(Data == NULL)
    {
        return RETURN_NOT_FOUND;
    }
    if(Length != sizeof(*Value))
    {
        return RETURN_INVALID_PARAMETER;
    }
    CopyMem(Value, Data, sizeof(*Value));
    return RETURN_SUCCESS;
}

// A string property that says Text. Bytes after the terminator do not count.
STATIC BOOLEAN PropertyIs(IN dt_node_t *Node, IN CONST CHAR8 *Key, IN CONST CHAR8 *Text)
{
    size_t Length     = 0;
    VOID   *Data      = dt_prop(Node, Key, &Length);
    UINTN  TextLength = AsciiStrLen(Text);

    return Data != NULL && Length > TextLength && AsciiStrnCmp(Data, Text, TextLength + 1) == 0;
}

// "compatible" holds Text as one of its strings, each ended by a zero
STATIC BOOLEAN Compatible(IN dt_node_t *Node, IN CONST CHAR8 *Text)
{
    size_t      Length     = 0;
    CONST CHAR8 *Data      = dt_prop(Node, "compatible", &Length);
    UINTN       TextLength = AsciiStrLen(Text);
    UINTN       Start;

    if(Data == NULL)
    {
        return FALSE;
    }
    for(Start = 0; Start < Length; Start += AsciiStrnLenS(Data + Start, Length - Start) + 1)
    {
        if(Length - Start > TextLength && AsciiStrnCmp(Data + Start, Text, TextLength + 1) == 0)
        {
            return TRUE;
        }
    }
    return FALSE;
}

// One or two little-endian 32-bit cells
STATIC UINT64 ReadCells(IN OUT CONST UINT8 **Cursor, IN UINT32 Cells)
{
    UINT64 Value = 0;

    CopyMem(&Value, *Cursor, Cells * sizeof(UINT32));
    *Cursor += Cells * sizeof(UINT32);
    return Value;
}

STATIC RETURN_STATUS ReadCellCount(IN dt_node_t *Node, IN CONST CHAR8 *Key, OUT UINT32 *Cells)
{
    RETURN_STATUS Status = ReadU32(Node, Key, Cells);

    if(!RETURN_ERROR(Status) && (*Cells < 1 || *Cells > 2))
    {
        return RETURN_INVALID_PARAMETER;
    }
    return Status;
}

//
// /arm-io: the bus the device registers are on. An entry of "ranges" is an
// address on the bus, the address of the same place as the CPU sees it, and a
// length.
//
typedef struct
{
    dt_node_t   *Node;
    CONST UINT8 *Ranges;
    UINTN       RangeCount;
    UINT32      BusCells;       // "#address-cells" of /arm-io
    UINT32      CpuCells;       // "#address-cells" of the root
    UINT32      SizeCells;      // "#size-cells" of /arm-io
} ARM_IO;

STATIC RETURN_STATUS GetArmIo(IN CONST APPLE_BOOT_INFO *Info, OUT ARM_IO *Io)
{
    RETURN_STATUS Status;
    size_t        Length = 0;
    UINTN         EntrySize;

    Io->Node = dt_find(Info->DeviceTree, "/arm-io");
    if(Io->Node == NULL)
    {
        return RETURN_NOT_FOUND;
    }
    Status = ReadCellCount(Io->Node, "#address-cells", &Io->BusCells);
    if(RETURN_ERROR(Status))
    {
        return Status;
    }
    Status = ReadCellCount(Io->Node, "#size-cells", &Io->SizeCells);
    if(RETURN_ERROR(Status))
    {
        return Status;
    }
    Status = ReadCellCount(Info->DeviceTree, "#address-cells", &Io->CpuCells);
    if(RETURN_ERROR(Status))
    {
        return Status;
    }
    Io->Ranges = dt_prop(Io->Node, "ranges", &Length);
    if(Io->Ranges == NULL)
    {
        return RETURN_NOT_FOUND;
    }
    EntrySize = (Io->BusCells + Io->CpuCells + Io->SizeCells) * sizeof(UINT32);
    if(Length % EntrySize != 0)
    {
        return RETURN_INVALID_PARAMETER;
    }
    Io->RangeCount = Length / EntrySize;
    return RETURN_SUCCESS;
}

STATIC VOID ReadRange(IN CONST ARM_IO *Io, IN UINTN Index, OUT UINT64 *BusAddress, OUT UINT64 *CpuAddress, OUT UINT64 *Size)
{
    CONST UINT8 *Cursor = Io->Ranges + Index * (Io->BusCells + Io->CpuCells + Io->SizeCells) * sizeof(UINT32);

    *BusAddress = ReadCells(&Cursor, Io->BusCells);
    *CpuAddress = ReadCells(&Cursor, Io->CpuCells);
    *Size       = ReadCells(&Cursor, Io->SizeCells);
}

//
// Entry number Entry of "reg" of a child of /arm-io, its address taken through "ranges"
// the way m1n1 does it: into the first range that holds all of the entry; an
// address no range holds stays as it is. RETURN_NOT_FOUND without "reg",
// RETURN_INVALID_PARAMETER when "reg" is too short to hold that entry. *Base and
// *Size are 0 unless RETURN_SUCCESS.
//
STATIC RETURN_STATUS ReadArmIoReg(IN CONST ARM_IO *Io, IN dt_node_t *Node, IN UINTN Entry, OUT UINT64 *Base, OUT UINT64 *Size)
{
    CONST UINT8 *Reg;
    size_t      Length = 0;
    UINTN       EntrySize = (Io->BusCells + Io->SizeCells) * sizeof(UINT32);
    UINT64      Address;
    UINT64      BusAddress;
    UINT64      CpuAddress;
    UINT64      RangeSize;
    UINTN       Index;

    *Base = 0;
    *Size = 0;

    Reg = dt_prop(Node, "reg", &Length);
    if(Reg == NULL)
    {
        return RETURN_NOT_FOUND;
    }
    if(Length < (Entry + 1) * EntrySize)
    {
        return RETURN_INVALID_PARAMETER;
    }
    Reg    += Entry * EntrySize;
    Address = ReadCells(&Reg, Io->BusCells);
    *Size   = ReadCells(&Reg, Io->SizeCells);

    for(Index = 0; Index < Io->RangeCount; Index++)
    {
        ReadRange(Io, Index, &BusAddress, &CpuAddress, &RangeSize);
        // The registers lie inside this range; written so that nothing can wrap around
        if(Address >= BusAddress && Address - BusAddress <= RangeSize && *Size <= RangeSize - (Address - BusAddress))
        {
            Address = Address - BusAddress + CpuAddress;
            break;
        }
    }

    *Base = Address;
    return RETURN_SUCCESS;
}

RETURN_STATUS AppleGetUartBase(IN CONST APPLE_BOOT_INFO *Info, OUT UINT64 *Base)
{
    ARM_IO        Io;
    RETURN_STATUS Status;
    dt_node_t     *Uart;
    UINT64        Size;

    *Base = 0;

    Uart = dt_find(Info->DeviceTree, "/arm-io/uart0");
    if(Uart == NULL)
    {
        return RETURN_NOT_FOUND;
    }
    Status = GetArmIo(Info, &Io);
    if(!RETURN_ERROR(Status))
    {
        Status = ReadArmIoReg(&Io, Uart, 0, Base, &Size);
    }
    // The node is there, so whatever keeps its address from being known is a fault of the tree
    if(RETURN_ERROR(Status) || *Base == 0)
    {
        *Base = 0;
        return RETURN_INVALID_PARAMETER;
    }
    return RETURN_SUCCESS;
}

// A range ACPI can describe: not empty, its last byte inside the address space
STATIC BOOLEAN UsableRange(IN CONST APPLE_MMIO_RANGE *Range)
{
    return Range->Size != 0 && Range->Size - 1 <= MAX_UINT64 - Range->Base;
}

RETURN_STATUS AppleGetAns(IN CONST APPLE_BOOT_INFO *Info, OUT APPLE_ANS *Ans)
{
    ARM_IO        Io;
    RETURN_STATUS Status;
    dt_node_t     *Node;
    dt_node_t     *Sart;
    APPLE_ANS     Found;
    UINT64        Unused;
    CONST UINT8   *Interrupts;
    size_t        Length = 0;
    UINT32        NvmeIndex;

    ZeroMem(Ans, sizeof(*Ans));
    ZeroMem(&Found, sizeof(Found));

    Status = GetArmIo(Info, &Io);
    if(RETURN_ERROR(Status))
    {
        return Status;
    }
    Node = dt_find(Info->DeviceTree, "/arm-io/ans");
    Sart = dt_find(Info->DeviceTree, "/arm-io/sart-ans");
    if(Node == NULL || Sart == NULL)
    {
        return RETURN_NOT_FOUND;
    }

    //
    // Registers. The NVMe window: start of entry 9 or 3, length of entry 3.
    //
    Found.SecureBar = dt_prop(Node, "nvme-secure-bar", NULL) != NULL;
    Status = ReadArmIoReg(&Io, Node, Found.SecureBar ? 9 : 3, &Found.Nvme.Base, &Unused);
    if(!RETURN_ERROR(Status))
    {
        Status = ReadArmIoReg(&Io, Node, 3, &Unused, &Found.Nvme.Size);
    }
    if(!RETURN_ERROR(Status))
    {
        Status = ReadArmIoReg(&Io, Node, 0, &Found.Coprocessor.Base, &Found.Coprocessor.Size);
    }
    if(!RETURN_ERROR(Status))
    {
        Status = ReadArmIoReg(&Io, Sart, 0, &Found.Sart.Base, &Found.Sart.Size);
    }
    if(RETURN_ERROR(Status))
    {
        return Status;
    }
    if(!UsableRange(&Found.Nvme) || !UsableRange(&Found.Coprocessor) || !UsableRange(&Found.Sart))
    {
        return RETURN_INVALID_PARAMETER;
    }

    //
    // Interrupts: the mailbox lines first, the controller's after them
    //
    Status = ReadU32(Node, "nvme-interrupt-idx", &NvmeIndex);
    if(RETURN_ERROR(Status))
    {
        return Status;
    }
    Interrupts = dt_prop(Node, "interrupts", &Length);
    if(Interrupts == NULL)
    {
        return RETURN_NOT_FOUND;
    }
    if(Length % sizeof(UINT32) != 0 || NvmeIndex < APPLE_ANS_MAILBOX_LINES || NvmeIndex >= Length / sizeof(UINT32))
    {
        return RETURN_INVALID_PARAMETER;
    }
    // Property values are aligned to 4 bytes only, hence the copies
    CopyMem(&Found.NvmeAic, Interrupts + NvmeIndex * sizeof(UINT32), sizeof(UINT32));
    CopyMem(&Found.MailboxAic, Interrupts + APPLE_ANS_MAILBOX_RECEIVE * sizeof(UINT32), sizeof(UINT32));

    *Ans = Found;
    return RETURN_SUCCESS;
}

RETURN_STATUS AppleGetAnsSartVersion(IN CONST APPLE_BOOT_INFO *Info, OUT UINT32 *Version)
{
    dt_node_t     *Sart = dt_find(Info->DeviceTree, "/arm-io/sart-ans");
    RETURN_STATUS Status;

    *Version = 0;
    if(Sart == NULL)
    {
        return RETURN_NOT_FOUND;
    }
    Status = ReadU32(Sart, "sart-version", Version);
    // The first SART has no "sart-version": m1n1 v1.9.9, src/sart.c, sart_init()
    if(Status == RETURN_NOT_FOUND && Compatible(Sart, "sart,t8015"))
    {
        return RETURN_SUCCESS;
    }
    if(RETURN_ERROR(Status))
    {
        *Version = 0;
    }
    return Status;
}

//
// The power manager, for AppleGetAnsPmgr(). The layout of its device table, the
// way from an entry to the entry's register and the die of the disk controller
// are written after m1n1 v1.9.9 (https://github.com/AsahiLinux/m1n1, commit
// 809541515659): src/pmgr.c (struct pmgr_device, pmgr_get_psreg(),
// pmgr_device_get_addr(), pmgr_reset(), pmgr_adt_path_offset_trace(),
// pmgr_init()), src/pmgr.h (PMGR_DIE_OFFSET, PMGR_DIE_ID), src/nvme.c
// (nvme_init(): the die; the shutdown: "ANS", then "ANS2").
//
// Those files carry this notice:
//   SPDX-License-Identifier: MIT
//   Copyright The Asahi Linux Contributors
// under the MIT licence, whose text is the permission notice at the top of this
// file. Only the address is worked out here: the firmware does not read or
// write the register.
//
#define PMGR_DEVICE_SIZE              48
#define PMGR_DEVICE_FLAGS             0     // byte
#define PMGR_DEVICE_ADDR_OFFSET       10    // byte: where in the group, in units of 8 bytes
#define PMGR_DEVICE_PSREG_IDX         11    // byte: the group, an entry of "ps-regs"
#define PMGR_DEVICE_GROUP_AND_OFFSET  16    // 32 bits: with "ps-groups", the group in bits 24-31, where in it in bits 0-23
#define PMGR_DEVICE_NAME              32
#define PMGR_DEVICE_NAME_SIZE         16
#define PMGR_FLAG_VIRTUAL             0x10
#define PMGR_GROUP_SIZE               12    // an entry of "ps-regs" or "ps-groups": "reg" entry, offset, a third value
#define PMGR_DIE_OFFSET               0x2000000000ULL
#define PMGR_DIE_ID_SHIFT             28    // in a value of "clock-gates"
#define PMGR_REGISTER_SIZE            4

STATIC dt_node_t *FindPmgr(IN CONST APPLE_BOOT_INFO *Info)
{
    dt_node_t *Node = dt_find(Info->DeviceTree, "/arm-io/pmgr");

    if(Node != NULL && Compatible(Node, "pmgr2,arch"))
    {
        Node = dt_find(Info->DeviceTree, "/arm-io/pmgr-child");
    }
    return Node;
}

// The first entry named Name that is not virtual; NULL when there is none
STATIC CONST UINT8 *FindPmgrDevice(IN CONST UINT8 *Devices, IN UINTN Count, IN CONST CHAR8 *Name)
{
    CONST UINT8 *Device;
    UINTN       Index;

    for(Index = 0; Index < Count; Index++)
    {
        Device = Devices + Index * PMGR_DEVICE_SIZE;
        if((Device[PMGR_DEVICE_FLAGS] & PMGR_FLAG_VIRTUAL) == 0 &&
           AsciiStrnCmp((CONST CHAR8 *)Device + PMGR_DEVICE_NAME, Name, PMGR_DEVICE_NAME_SIZE) == 0)
        {
            return Device;
        }
    }
    return NULL;
}

// The die the disk controller is on: from "clock-gates" of /arm-io/ans, else from where its NVMe registers are
STATIC RETURN_STATUS ReadAnsDie(IN CONST APPLE_BOOT_INFO *Info, IN CONST APPLE_ANS *Ans, OUT UINT32 *Die)
{
    dt_node_t   *Node = dt_find(Info->DeviceTree, "/arm-io/ans");
    CONST UINT8 *Gates;
    size_t      Length = 0;
    UINT32      First;

    *Die = 0;
    if(Node == NULL)
    {
        return RETURN_NOT_FOUND;
    }
    Gates = dt_prop(Node, "clock-gates", &Length);
    if(Gates == NULL || Length == 0)
    {
        *Die = (UINT32)((Ans->Nvme.Base >> 37) & 3);
        return RETURN_SUCCESS;
    }
    if(Length % sizeof(UINT32) != 0)
    {
        return RETURN_INVALID_PARAMETER;
    }
    CopyMem(&First, Gates, sizeof(First));
    *Die = First >> PMGR_DIE_ID_SHIFT;
    return RETURN_SUCCESS;
}

RETURN_STATUS AppleGetAnsPmgr(IN CONST APPLE_BOOT_INFO *Info, IN CONST APPLE_ANS *Ans, OUT APPLE_MMIO_RANGE *Pmgr)
{
    ARM_IO           Io;
    RETURN_STATUS    Status;
    dt_node_t        *Node;
    CONST UINT8      *Groups;
    CONST UINT8      *Devices;
    CONST UINT8      *Device;
    CONST UINT8      *Other;
    size_t           GroupsLength  = 0;
    size_t           DevicesLength = 0;
    BOOLEAN          ByGroup;
    UINT32           Value;
    UINT32           Group;
    UINT32           InGroup;
    UINT32           GroupEntry[2];     // an entry of "reg", an offset into it
    UINT64           RegBase;
    UINT64           RegSize;
    UINT64           Offset;
    UINT64           DieOffset;
    UINT32           Die;
    APPLE_MMIO_RANGE Found;

    ZeroMem(Pmgr, sizeof(*Pmgr));

    Status = GetArmIo(Info, &Io);
    if(RETURN_ERROR(Status))
    {
        return Status;
    }
    Node = FindPmgr(Info);
    if(Node == NULL)
    {
        return RETURN_NOT_FOUND;
    }
    Groups  = dt_prop(Node, "ps-regs", &GroupsLength);
    ByGroup = Groups == NULL || GroupsLength == 0;
    if(ByGroup)
    {
        Groups = dt_prop(Node, "ps-groups", &GroupsLength);
    }
    Devices = dt_prop(Node, "devices", &DevicesLength);
    if(Groups == NULL || GroupsLength == 0 || Devices == NULL || DevicesLength == 0)
    {
        return RETURN_NOT_FOUND;
    }
    if(GroupsLength % PMGR_GROUP_SIZE != 0 || DevicesLength % PMGR_DEVICE_SIZE != 0)
    {
        return RETURN_INVALID_PARAMETER;
    }

    //
    // The controller's entry: some machines call it ANS, some ANS2 (m1n1)
    //
    Device = FindPmgrDevice(Devices, DevicesLength / PMGR_DEVICE_SIZE, "ANS");
    Other  = FindPmgrDevice(Devices, DevicesLength / PMGR_DEVICE_SIZE, "ANS2");
    if(Device != NULL && Other != NULL)
    {
        return RETURN_UNSUPPORTED;
    }
    if(Device == NULL)
    {
        Device = Other;
    }
    if(Device == NULL)
    {
        return RETURN_NOT_FOUND;
    }

    //
    // Its group and where in the group its register is
    //
    if(ByGroup)
    {
        CopyMem(&Value, Device + PMGR_DEVICE_GROUP_AND_OFFSET, sizeof(Value));
        Group   = Value >> 24;
        InGroup = Value & 0xFFFFFF;
    }
    else
    {
        Group   = Device[PMGR_DEVICE_PSREG_IDX];
        InGroup = (UINT32)Device[PMGR_DEVICE_ADDR_OFFSET] << 3;
    }
    if(Group >= GroupsLength / PMGR_GROUP_SIZE)
    {
        return RETURN_INVALID_PARAMETER;
    }
    CopyMem(GroupEntry, Groups + Group * PMGR_GROUP_SIZE, sizeof(GroupEntry));
    Status = ReadArmIoReg(&Io, Node, GroupEntry[0], &RegBase, &RegSize);
    if(RETURN_ERROR(Status))
    {
        return Status;
    }

    //
    // The register: inside its "reg" entry on die 0, nothing wraps around
    //
    Offset = (UINT64)GroupEntry[1] + InGroup;
    if(Offset > RegSize || RegSize - Offset < PMGR_REGISTER_SIZE || RegBase > MAX_UINT64 - Offset)
    {
        return RETURN_INVALID_PARAMETER;
    }
    Status = ReadAnsDie(Info, Ans, &Die);
    if(RETURN_ERROR(Status))
    {
        return Status;
    }
    DieOffset = Die * PMGR_DIE_OFFSET;
    if(RegBase + Offset > MAX_UINT64 - DieOffset)
    {
        return RETURN_INVALID_PARAMETER;
    }
    Found.Base = RegBase + Offset + DieOffset;
    Found.Size = PMGR_REGISTER_SIZE;
    if(!UsableRange(&Found))
    {
        return RETURN_INVALID_PARAMETER;
    }

    *Pmgr = Found;
    return RETURN_SUCCESS;
}

//
// ========== SiliconNT: whole coprocessors ==========
//
// See AppleGetMtp() and AppleGetSmc() in AppleDTLib.h.
//

#define SEGMENT_RANGE_SIZE  32      // an entry of "segment-ranges": the physical address first, 64 bits

// A node that belongs to the device, and the name of its resources
typedef struct
{
    CONST CHAR8 *Path;
    CONST CHAR8 *Block;
} DEVICE_BLOCK;

STATIC RETURN_STATUS ReadU64(IN dt_node_t *Node, IN CONST CHAR8 *Key, OUT UINT64 *Value)
{
    size_t Length = 0;
    VOID   *Data  = dt_prop(Node, Key, &Length);

    if(Data == NULL)
    {
        return RETURN_NOT_FOUND;
    }
    if(Length != sizeof(*Value))
    {
        return RETURN_INVALID_PARAMETER;
    }
    CopyMem(Value, Data, sizeof(*Value));
    return RETURN_SUCCESS;
}

// Two ranges have a byte in common. A range that is empty or wraps around has none.
STATIC BOOLEAN Overlap(IN CONST APPLE_MMIO_RANGE *A, IN CONST APPLE_MMIO_RANGE *B)
{
    return UsableRange(A) && UsableRange(B) && A->Base <= B->Base + (B->Size - 1) && B->Base <= A->Base + (A->Size - 1);
}

// How many entries of "reg" a child of /arm-io has; what is left after the last whole one does not count
STATIC UINTN ArmIoRegCount(IN CONST ARM_IO *Io, IN dt_node_t *Node)
{
    size_t Length = 0;

    return dt_prop(Node, "reg", &Length) == NULL ? 0 : Length / ((Io->BusCells + Io->SizeCells) * sizeof(UINT32));
}

// A range the power manager has registers in. The nodes: see FindPmgr().
STATIC BOOLEAN InPowerManager(IN CONST APPLE_BOOT_INFO *Info, IN CONST ARM_IO *Io, IN CONST APPLE_MMIO_RANGE *Range)
{
    STATIC CONST CHAR8 *CONST Paths[] = { "/arm-io/pmgr", "/arm-io/pmgr-child" };
    dt_node_t                 *Node;
    APPLE_MMIO_RANGE          Entry;
    UINTN                     Path;
    UINTN                     Index;

    for(Path = 0; Path < ARRAY_SIZE(Paths); Path++)
    {
        Node = dt_find(Info->DeviceTree, Paths[Path]);
        if(Node == NULL)
        {
            continue;
        }
        for(Index = 0; Index < ArmIoRegCount(Io, Node); Index++)
        {
            if(!RETURN_ERROR(ReadArmIoReg(Io, Node, Index, &Entry.Base, &Entry.Size)) && Overlap(&Entry, Range))
            {
                return TRUE;
            }
        }
    }
    return FALSE;
}

STATIC RETURN_STATUS AddRange(IN CONST APPLE_BOOT_INFO *Info, IN CONST ARM_IO *Io, IN OUT APPLE_DEVICE *Device, IN CONST CHAR8 *Block, IN UINT32 Entry, IN CONST APPLE_MMIO_RANGE *Range)
{
    APPLE_DEVICE_RANGE *Added = &Device->Ranges[Device->RangeCount];

    if(!UsableRange(Range))
    {
        return RETURN_INVALID_PARAMETER;
    }
    if(Device->RangeCount == APPLE_DEVICE_MAX_RANGES)
    {
        return RETURN_UNSUPPORTED;
    }
    Added->Block        = Block;
    Added->Entry        = Entry;
    Added->Range        = *Range;
    Added->PowerManager = InPowerManager(Info, Io, Range);
    Device->RangeCount++;
    return RETURN_SUCCESS;
}

STATIC RETURN_STATUS AddRegisters(IN CONST APPLE_BOOT_INFO *Info, IN CONST ARM_IO *Io, IN CONST DEVICE_BLOCK *Block, IN dt_node_t *Node, IN OUT APPLE_DEVICE *Device)
{
    APPLE_MMIO_RANGE Range;
    RETURN_STATUS    Status;
    size_t           Length = 0;
    UINTN            Count  = ArmIoRegCount(Io, Node);
    UINT32           Index;

    if(dt_prop(Node, "reg", &Length) == NULL)
    {
        return RETURN_NOT_FOUND;
    }
    if(Count == 0 || Length != Count * (Io->BusCells + Io->SizeCells) * sizeof(UINT32))
    {
        return RETURN_INVALID_PARAMETER;
    }
    for(Index = 0; Index < Count; Index++)
    {
        Status = ReadArmIoReg(Io, Node, Index, &Range.Base, &Range.Size);
        if(!RETURN_ERROR(Status))
        {
            Status = AddRange(Info, Io, Device, Block->Block, Index, &Range);
        }
        if(RETURN_ERROR(Status))
        {
            return Status;
        }
    }
    return RETURN_SUCCESS;
}

STATIC RETURN_STATUS AddLines(IN CONST DEVICE_BLOCK *Block, IN dt_node_t *Node, IN OUT APPLE_DEVICE *Device)
{
    CONST UINT8       *Values;
    size_t            Length = 0;
    UINT32            Index;
    APPLE_DEVICE_LINE *Added;

    Values = dt_prop(Node, "interrupts", &Length);
    if(Values == NULL)
    {
        return RETURN_NOT_FOUND;
    }
    if(Length == 0 || Length % sizeof(UINT32) != 0)
    {
        return RETURN_INVALID_PARAMETER;
    }
    if(Length / sizeof(UINT32) > APPLE_DEVICE_MAX_LINES - Device->LineCount)
    {
        return RETURN_UNSUPPORTED;
    }
    for(Index = 0; Index < Length / sizeof(UINT32); Index++)
    {
        Added        = &Device->Lines[Device->LineCount++];
        Added->Block = Block->Block;
        Added->Entry = Index;
        CopyMem(&Added->Aic, Values + Index * sizeof(UINT32), sizeof(UINT32));
    }
    return RETURN_SUCCESS;
}

// Everything but the SRAM. *Io is valid with RETURN_SUCCESS only.
STATIC RETURN_STATUS ReadDevice(IN CONST APPLE_BOOT_INFO *Info, IN CONST DEVICE_BLOCK *Blocks, IN UINTN BlockCount, OUT ARM_IO *Io, OUT APPLE_DEVICE *Device)
{
    RETURN_STATUS Status;
    dt_node_t     *Node;
    UINTN         Index;
    CONST CHAR8   *FailedNode;
    CONST CHAR8   *FailedProperty = NULL;

    ZeroMem(Device, sizeof(*Device));

    FailedNode = "/arm-io";
    Status     = GetArmIo(Info, Io);

    for(Index = 0; Index < BlockCount && !RETURN_ERROR(Status); Index++)
    {
        FailedNode = Blocks[Index].Path;
        Status     = dt_find(Info->DeviceTree, Blocks[Index].Path) != NULL ? RETURN_SUCCESS : RETURN_NOT_FOUND;
    }
    for(Index = 0; Index < BlockCount && !RETURN_ERROR(Status); Index++)
    {
        FailedNode     = Blocks[Index].Path;
        Node           = dt_find(Info->DeviceTree, Blocks[Index].Path);
        FailedProperty = "reg";
        Status         = AddRegisters(Info, Io, &Blocks[Index], Node, Device);
        if(!RETURN_ERROR(Status))
        {
            FailedProperty = "interrupts";
            Status         = AddLines(&Blocks[Index], Node, Device);
        }
    }

    if(RETURN_ERROR(Status))
    {
        ZeroMem(Device, sizeof(*Device));
        Device->FailedNode     = FailedNode;
        Device->FailedProperty = FailedProperty;
    }
    return Status;
}

// Looks for the first region that holds Address and does not overlap Dram
typedef struct
{
    UINT64                 Address;
    CONST APPLE_MMIO_RANGE *Dram;
    APPLE_MMIO_RANGE       *Region;
} CARVEOUT_WALK;

// Called for the properties of /chosen/carveout-memory-map
static int CarveoutWalkProperty(void *a, dt_node_t *Node, int Depth, const char *Key, void *Value, size_t Length)
{
    CARVEOUT_WALK    *Walk = a;
    APPLE_MMIO_RANGE Region;

    if(AsciiStrnCmp(Key, "region-id-", sizeof("region-id-") - 1) != 0 || Length != 2 * sizeof(UINT64))
    {
        return 0;
    }
    CopyMem(&Region.Base, Value, sizeof(UINT64));
    CopyMem(&Region.Size, (UINT8 *)Value + sizeof(UINT64), sizeof(UINT64));
    if(!UsableRange(&Region) || Walk->Address < Region.Base || Walk->Address - Region.Base >= Region.Size || Overlap(&Region, Walk->Dram))
    {
        return 0;
    }
    *Walk->Region = Region;
    return 1;
}

STATIC RETURN_STATUS FindMtpSram(IN CONST APPLE_BOOT_INFO *Info, OUT APPLE_MMIO_RANGE *Sram)
{
    dt_node_t        *Chosen   = dt_find(Info->DeviceTree, "/chosen");
    dt_node_t        *Carveout = dt_find(Info->DeviceTree, "/chosen/carveout-memory-map");
    CONST UINT8      *Segments;
    size_t           Length = 0;
    APPLE_MMIO_RANGE Dram;
    RETURN_STATUS    Status;
    CARVEOUT_WALK    Walk = { 0, &Dram, Sram };

    Segments = dt_prop(dt_find(Info->DeviceTree, "/arm-io/mtp"), "segment-ranges", &Length);
    if(Segments == NULL || Carveout == NULL)
    {
        return RETURN_NOT_FOUND;
    }
    if(Length == 0 || Length % SEGMENT_RANGE_SIZE != 0)
    {
        return RETURN_INVALID_PARAMETER;
    }
    CopyMem(&Walk.Address, Segments, sizeof(Walk.Address));

    Status = ReadU64(Chosen, "dram-base", &Dram.Base);
    if(!RETURN_ERROR(Status))
    {
        Status = ReadU64(Chosen, "dram-size", &Dram.Size);
    }
    if(RETURN_ERROR(Status))
    {
        return Status;
    }
    return dt_parse(Carveout, -1, NULL, NULL, NULL, &CarveoutWalkProperty, &Walk) == 1 ? RETURN_SUCCESS : RETURN_NOT_FOUND;
}

RETURN_STATUS AppleGetMtp(IN CONST APPLE_BOOT_INFO *Info, OUT APPLE_DEVICE *Device)
{
    STATIC CONST DEVICE_BLOCK Blocks[] = {
        { "/arm-io/mtp", "mtp" },
        { "/arm-io/dart-mtp", "dart-mtp" },
        { "/arm-io/dockchannel-mtp", "dockchannel-mtp" },
    };
    ARM_IO                    Io;
    APPLE_MMIO_RANGE          Sram;
    RETURN_STATUS             Status;

    Status = ReadDevice(Info, Blocks, ARRAY_SIZE(Blocks), &Io, Device);
    if(RETURN_ERROR(Status))
    {
        return Status;
    }
    Device->SramStatus = FindMtpSram(Info, &Sram);
    if(!RETURN_ERROR(Device->SramStatus))
    {
        Device->SramStatus = AddRange(Info, &Io, Device, "mtp-sram", APPLE_DEVICE_NO_ENTRY, &Sram);
    }
    return RETURN_SUCCESS;
}

STATIC RETURN_STATUS FindSmcRegion(IN CONST APPLE_BOOT_INFO *Info, OUT APPLE_MMIO_RANGE *Region)
{
    dt_node_t     *Nub = dt_find(Info->DeviceTree, "/arm-io/smc/iop-smc-nub");
    RETURN_STATUS Status;

    if(Nub == NULL)
    {
        return RETURN_NOT_FOUND;
    }
    Status = ReadU64(Nub, "region-base", &Region->Base);
    if(!RETURN_ERROR(Status))
    {
        Status = ReadU64(Nub, "region-size", &Region->Size);
    }
    return Status;
}

RETURN_STATUS AppleGetSmc(IN CONST APPLE_BOOT_INFO *Info, OUT APPLE_DEVICE *Device)
{
    STATIC CONST DEVICE_BLOCK Blocks[] = {
        { "/arm-io/smc", "smc" },
    };
    ARM_IO                    Io;
    APPLE_MMIO_RANGE          Region;
    RETURN_STATUS             Status;

    Status = ReadDevice(Info, Blocks, ARRAY_SIZE(Blocks), &Io, Device);
    if(RETURN_ERROR(Status))
    {
        return Status;
    }
    Device->SramStatus = FindSmcRegion(Info, &Region);
    if(!RETURN_ERROR(Device->SramStatus))
    {
        Device->SramStatus = AddRange(Info, &Io, Device, "smc-region", APPLE_DEVICE_NO_ENTRY, &Region);
    }
    return RETURN_SUCCESS;
}

RETURN_STATUS AppleGetMmioSpan(IN CONST APPLE_BOOT_INFO *Info, OUT UINT64 *Base, OUT UINT64 *Size)
{
    ARM_IO        Io;
    RETURN_STATUS Status;
    UINT64        Lowest  = MAX_UINT64;
    UINT64        Highest = 0;
    UINT64        BusAddress;
    UINT64        CpuAddress;
    UINT64        RangeSize;
    UINTN         Index;

    *Base = 0;
    *Size = 0;

    Status = GetArmIo(Info, &Io);
    if(RETURN_ERROR(Status))
    {
        return Status;
    }
    for(Index = 0; Index < Io.RangeCount; Index++)
    {
        ReadRange(&Io, Index, &BusAddress, &CpuAddress, &RangeSize);
        if(RangeSize == 0)
        {
            continue;
        }
        if(RangeSize > MAX_UINT64 - CpuAddress)
        {
            return RETURN_INVALID_PARAMETER;
        }
        if(CpuAddress < Lowest)
        {
            Lowest = CpuAddress;
        }
        if(CpuAddress + RangeSize > Highest)
        {
            Highest = CpuAddress + RangeSize;
        }
    }
    if(Highest == 0)
    {
        return RETURN_NOT_FOUND;
    }

    *Base = Lowest;
    *Size = Highest - Lowest;
    return RETURN_SUCCESS;
}

RETURN_STATUS AppleGetChipId(IN CONST APPLE_BOOT_INFO *Info, OUT UINT32 *ChipId)
{
    dt_node_t *Chosen = dt_find(Info->DeviceTree, "/chosen");

    *ChipId = 0;
    if(Chosen == NULL)
    {
        return RETURN_NOT_FOUND;
    }
    return ReadU32(Chosen, "chip-id", ChipId);
}

typedef struct
{
    APPLE_CPU     *Cpus;
    UINTN         Count;
    RETURN_STATUS Status;
} CPU_WALK;

// Called for /cpus (depth 0), its children (1) and whatever is below them
static int CpuWalkNode(void *a, dt_node_t *Node, int Depth)
{
    CPU_WALK  *Walk = a;
    APPLE_CPU Cpu;
    UINT32    Reg;
    UINTN     Index;

    if(Depth != 1)
    {
        return 0;
    }
    Walk->Status = ReadU32(Node, "cpu-id", &Cpu.Number);
    if(Walk->Status == RETURN_NOT_FOUND)
    {
        // Not a core
        Walk->Status = RETURN_SUCCESS;
        return 0;
    }
    if(RETURN_ERROR(Walk->Status))
    {
        DEBUG((DEBUG_ERROR, "AppleGetCpus: a \"cpu-id\" that is not 4 bytes long\n"));
        return 1;
    }

    Walk->Status = RETURN_INVALID_PARAMETER;
    if(Cpu.Number >= APPLE_MAX_CPUS)
    {
        DEBUG((DEBUG_ERROR, "AppleGetCpus: core %u: the highest number m1n1 takes is %u\n", Cpu.Number, APPLE_MAX_CPUS - 1));
        return 1;
    }
    for(Index = 0; Index < Walk->Count; Index++)
    {
        if(Walk->Cpus[Index].Number == Cpu.Number)
        {
            DEBUG((DEBUG_ERROR, "AppleGetCpus: two cores are number %u\n", Cpu.Number));
            return 1;
        }
    }
    if(PropertyIs(Node, "cluster-type", "E"))
    {
        Cpu.Class = AppleCpuEfficiency;
    }
    else if(PropertyIs(Node, "cluster-type", "P"))
    {
        Cpu.Class = AppleCpuPerformance;
    }
    else
    {
        DEBUG((DEBUG_ERROR, "AppleGetCpus: core %u: \"cluster-type\" is neither \"E\" nor \"P\"\n", Cpu.Number));
        return 1;
    }
    if(RETURN_ERROR(ReadU32(Node, "reg", &Reg)) || Reg > 0x7ff)
    {
        DEBUG((DEBUG_ERROR, "AppleGetCpus: core %u: no \"reg\" of 4 bytes, or one with a die number\n", Cpu.Number));
        return 1;
    }
    if(dt_prop(Node, "state", NULL) == NULL)
    {
        DEBUG((DEBUG_ERROR, "AppleGetCpus: core %u: no \"state\"\n", Cpu.Number));
        return 1;
    }
    Cpu.Mpidr = (Cpu.Class == AppleCpuPerformance ? BIT16 : 0) | Reg;
    Cpu.Boot  = PropertyIs(Node, "state", "running");

    Walk->Cpus[Walk->Count++] = Cpu;
    Walk->Status              = RETURN_SUCCESS;
    return 0;
}

RETURN_STATUS AppleGetCpus(IN CONST APPLE_BOOT_INFO *Info, OUT APPLE_CPU *Cpus, OUT UINTN *Count)
{
    CPU_WALK  Walk = { Cpus, 0, RETURN_SUCCESS };
    dt_node_t *Node;
    UINTN     Index;
    UINTN     Running = 0;

    *Count = 0;

    Node = dt_find(Info->DeviceTree, "/cpus");
    if(Node == NULL)
    {
        return RETURN_NOT_FOUND;
    }
    dt_parse(Node, 0, NULL, &CpuWalkNode, &Walk, NULL, NULL);
    if(RETURN_ERROR(Walk.Status))
    {
        return Walk.Status;
    }
    if(Walk.Count == 0)
    {
        return RETURN_NOT_FOUND;
    }
    for(Index = 0; Index < Walk.Count; Index++)
    {
        Running += Cpus[Index].Boot ? 1 : 0;
    }
    if(Running != 1)
    {
        DEBUG((DEBUG_ERROR, "AppleGetCpus: %u cores are running, not one\n", (UINT32)Running));
        return RETURN_INVALID_PARAMETER;
    }

    *Count = Walk.Count;
    return RETURN_SUCCESS;
}

void* dt_node_prop(dt_node_t *node, const char *prop, size_t *size)
{
    void *val = dt_prop(node, prop, size);
    if(!val)
    {
        DEBUG((DEBUG_INFO, "Missing DeviceTree prop: %a\n", prop));
    }
    return val;
}

void* dt_get_prop(const char *device, const char *prop, size_t *size)
{
    return dt_node_prop(dt_get(device), prop, size);
}

uint32_t dt_node_u32(dt_node_t *node, const char *prop, uint32_t idx)
{
    size_t len = 0;
    uint32_t *val = dt_node_prop(node, prop, &len);
    if(len < (idx + 1) * sizeof(*val))
    {
        DEBUG((DEBUG_INFO, "DeviceTree u32 out of bounds: %a[%u]\n", prop, idx));
        ASSERT(FALSE);
    }
    return val[idx];
}

uint32_t dt_get_u32(const char *device, const char *prop, uint32_t idx)
{
    return dt_node_u32(dt_get(device), prop, idx);
}

uint64_t dt_node_u64(dt_node_t *node, const char *prop, uint32_t idx)
{
    size_t len = 0;
    uint64_t *val = dt_node_prop(node, prop, &len);
    if(len < (idx + 1) * sizeof(*val))
    {
        DEBUG((DEBUG_INFO, "DeviceTree u64 out of bounds: %a[%u]\n", prop, idx));
        ASSERT(FALSE);
    }
    return val[idx];
}

uint64_t dt_get_u64(const char *device, const char *prop, uint32_t idx)
{
    return dt_node_u64(dt_get(device), prop, idx);
}

void* dt_get_prop_32(const char *device, const char *prop, uint32_t *size) __asm__("_dt_get_prop$32");
void* dt_get_prop_32(const char *device, const char *prop, uint32_t *size)
{
    size_t len = 0;
    void *val = dt_get_prop(device, prop, &len);
    if(size) *size = (uint32_t)len;
    return val;
}

//borrowed from m1n1
int dt_node_reg(dt_node_t *node, uint32_t idx, uint64_t *paddr, uint64_t *psize)
{
    dt_node_t *parent = dt_node_parent(node);
    dt_node_t *cur = node;

    uint32_t a_cells = dt_node_u32(parent, "#address-cells", 0);
    uint32_t s_cells = dt_node_u32(parent, "#size-cells", 0);

    if (a_cells < 1 || a_cells > 2 || s_cells > 2)
    {
        DEBUG((DEBUG_ERROR, "bad n-cells\n"));
        return 1;
    }

    size_t reg_len = 0;
    const uint32_t *reg = dt_node_prop(node, "reg", &reg_len);

    if (!reg || !reg_len)
    {
        DEBUG((DEBUG_ERROR, "reg not found or empty\n"));
        return 1;
    }

    if (reg_len < (idx + 1) * (a_cells + s_cells) * 4)
    {
        DEBUG((DEBUG_ERROR, "bad reg property length %d\n", reg_len));
        return 1;
    }

    reg += idx * (a_cells + s_cells);

    uint64_t addr, size = 0;
    get_cells(&addr, &reg, a_cells);
    get_cells(&size, &reg, s_cells);

    while (parent)
    {
        cur = parent;
        parent = dt_node_parent(cur);

        size_t ranges_len;
        const uint32_t *ranges = dt_node_prop(cur, "ranges", &ranges_len);
        if (!ranges)
            break;

        uint32_t pa_cells = dt_node_u32(parent, "#address-cells", 0);

        if (pa_cells < 1 || pa_cells > 2 || s_cells > 2)
        {
            DEBUG((DEBUG_ERROR, "bad reg property length %d\n", reg_len));
            return 1;
        }

        int range_cnt = ranges_len / (4 * (pa_cells + a_cells + s_cells));

        while (range_cnt--)
        {
            uint64_t c_addr, p_addr, c_size;
            get_cells(&c_addr, &ranges, a_cells);
            get_cells(&p_addr, &ranges, pa_cells);
            get_cells(&c_size, &ranges, s_cells);

            //dprintf(" ranges %lx %lx %lx\n", c_addr, p_addr, c_size);

            if (addr >= c_addr && (addr + size) <= (c_addr + c_size)) {
                //dprintf(" translate %lx", addr);
                addr = addr - c_addr + p_addr;
                //dprintf(" -> %lx\n", addr);
                break;
            }
        }

        s_cells = dt_node_u32(parent, "#size-cells", 0);

        a_cells = pa_cells;
    }


    if (paddr)
        *paddr = addr;
    if (psize)
        *psize = size;

    return 0;
}