/*
 * pongoOS - https://checkra.in
 *
 * Copyright (C) 2019-2024 checkra1n team
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
#ifndef APPLEDTLIB_H
#define APPLEDTLIB_H

#include <stddef.h>
#include <stdint.h>


#define CMDLINE_LENGTH_RV1 256
#define CMDLINE_LENGTH_RV2 608
#define CMDLINE_LENGTH_RV3 1024

struct boot_video {
    UINT64 base;
    UINT64 display;
    UINT64 stride;
    UINT64 width;
    UINT64 height;
    UINT64 depth;
};

struct boot_args {
    UINT16 revision;
    UINT16 version;
    UINT64 virt_base;
    UINT64 phys_base;
    UINT64 mem_size;
    UINT64 top_of_kernel_data;
    struct boot_video video;
    UINT32 machine_type;
    VOID *devtree;
    UINT32 devtree_size;
    union {
        struct {
            char cmdline[CMDLINE_LENGTH_RV1];
            UINT64 boot_flags;
            UINT64 mem_size_actual;
        } rv1;
        struct {
            char cmdline[CMDLINE_LENGTH_RV2];
            UINT64 boot_flags;
            UINT64 mem_size_actual;
        } rv2;
        struct {
            char cmdline[CMDLINE_LENGTH_RV3];
            UINT64 boot_flags;
            UINT64 mem_size_actual;
        } rv3;
    };
};



#define DT_KEY_LEN 0x20

typedef struct
{
    uint32_t nprop;
    uint32_t nchld;
    char prop[];
} dt_node_t;

typedef struct
{
    char key[DT_KEY_LEN];
    uint32_t len;
    char val[];
} dt_prop_t;

int dt_check(void *mem, size_t size, size_t *offp) __asm__("_dt_check$64");
int dt_parse(dt_node_t *node, int depth, size_t *offp, int (*cb_node)(void*, dt_node_t*, int), void *cbn_arg, int (*cb_prop)(void*, dt_node_t*, int, const char*, void*, size_t), void *cbp_arg) __asm__("_dt_parse$64");
dt_node_t* dt_find(dt_node_t *node, const char *name);
void* dt_prop(dt_node_t *node, const char *key, size_t *lenp) __asm__("_dt_prop$64");


struct memmap
{
    uint64_t addr;
    uint64_t size;
};

dt_node_t* dt_node(dt_node_t *node, const char *name);
dt_node_t* dt_node_parent(dt_node_t *node);
dt_node_t* dt_get(const char *name);
void* dt_node_prop(dt_node_t *node, const char *prop, size_t *size);
void* dt_get_prop(const char *device, const char *prop, size_t *size) __asm__("_dt_get_prop$64");
uint32_t dt_node_u32(dt_node_t *node, const char *prop, uint32_t idx);
uint32_t dt_get_u32(const char *device, const char *prop, uint32_t idx);
uint64_t dt_node_u64(dt_node_t *node, const char *prop, uint32_t idx);
uint64_t dt_get_u64(const char *device, const char *prop, uint32_t idx);
int dt_node_reg(dt_node_t *node, uint32_t idx, uint64_t *paddr, uint64_t *psize);

//
// SiliconNT: where the loader left the boot arguments and the Apple device tree,
// and the address of the serial port, which the entry code reads from the device
// tree once (AppleGetUartBase) so that the serial port library of every later
// module finds it without reading the tree itself.
// The entry code (PrePi) publishes this in a HOB, gAppleSiliconPkgBootInfoHobGuid;
// AppleGetBootInfo() returns the copy in the HOB.
//
typedef struct
{
    struct boot_args *BootArgs;
    dt_node_t        *DeviceTree;
    UINT32           DeviceTreeSize;
    UINT64           UartBase;
} APPLE_BOOT_INFO;

APPLE_BOOT_INFO* AppleGetBootInfo(VOID);

//
// SiliconNT: what the firmware needs to know about the machine it runs on. All of
// it is read from the device tree at boot; none of it is a build-time value. The
// functions below read Info->DeviceTree only and work before the HOB exists.
//
// They share these results:
//
// RETURN_SUCCESS            The outputs are valid.
// RETURN_NOT_FOUND          The node or the property is not in the tree.
// RETURN_INVALID_PARAMETER  It is there and cannot be used. Where a function
//                           says nothing more, that is a property of the wrong
//                           length or a number of cells other than 1 or 2.
//

//
// One core, from a child of /cpus that has a "cpu-id" property.
//
typedef enum
{
    AppleCpuEfficiency,     // "cluster-type" is "E"
    AppleCpuPerformance     // "cluster-type" is "P"
} APPLE_CPU_CLASS;

typedef struct
{
    UINT32          Number;     // "cpu-id": the number m1n1 and the EL2 layer know the core by
    UINT64          Mpidr;      // Aff2, Aff1 and Aff0 of the core's MPIDR_EL1
    APPLE_CPU_CLASS Class;
    BOOLEAN         Boot;       // "state" is "running": the core the firmware runs on
} APPLE_CPU;

//
// m1n1 has room for this many cores (MAX_CPUS, src/smp_asm.h) and ignores a core
// with a higher number; the firmware refuses such a tree instead.
//
#define APPLE_MAX_CPUS 32

//
// The cores, in the order of the tree. Cpus has room for APPLE_MAX_CPUS.
//
// MPIDR_EL1 is not in the tree. It is put together from "reg", which holds the
// core in bits 0-7 and the cluster in bits 8-10 (m1n1, src/smp.c, CPU_REG_CORE
// and CPU_REG_CLUSTER), and from the class: Aff0 is the core, Aff1 the cluster,
// Aff2 is 1 on a performance core and 0 on an efficiency core (XNU
// xnu-12377.121.6, osfmk/arm64/proc_reg.h, "Multiprocessor Affinity Register").
// Bits 11-14 of "reg" number the die on a chip made of several; where that
// number shows in MPIDR_EL1 is not known here, so such a tree is refused.
//
// RETURN_INVALID_PARAMETER also when: a core has no "reg", "cluster-type" or
// "state", a class other than "E" and "P", a number of APPLE_MAX_CPUS or above
// or one another core has, a "reg" above 0x7ff; or when not exactly one core
// is running.
// RETURN_NOT_FOUND also when /cpus has no core.
//
RETURN_STATUS AppleGetCpus(IN CONST APPLE_BOOT_INFO *Info, OUT APPLE_CPU *Cpus, OUT UINTN *Count);

//
// The address of the serial port: the first "reg" entry of /arm-io/uart0, taken
// through "ranges" of /arm-io the way m1n1 does it (an address no range covers
// stays as it is). The EL2 layer emulates the port at this address.
//
RETURN_STATUS AppleGetUartBase(IN CONST APPLE_BOOT_INFO *Info, OUT UINT64 *Base);

//
// The device registers: from the lowest address of "ranges" of /arm-io to the
// end of the highest range.
//
// RETURN_INVALID_PARAMETER also when a range wraps around the address space.
// RETURN_NOT_FOUND also when "ranges" is empty.
//
RETURN_STATUS AppleGetMmioSpan(IN CONST APPLE_BOOT_INFO *Info, OUT UINT64 *Base, OUT UINT64 *Size);

//
// The number of the chip, "chip-id" of /chosen: 0x8132 on the M4.
//
RETURN_STATUS AppleGetChipId(IN CONST APPLE_BOOT_INFO *Info, OUT UINT32 *ChipId);

//
// SiliconNT: the boot disk. The loader may put a disk image into memory; it
// then describes it in the "RAMDisk" property of /chosen/memory-map: base and
// size, two 64-bit values, like that node's "DeviceTree" and "BootArgs".
//
// RETURN_SUCCESS            *Base and *Size describe the image.
// RETURN_NOT_FOUND          There is no such property: no boot disk.
// RETURN_INVALID_PARAMETER  The property is there and cannot be used: it is not
//                           16 bytes long, the size is 0, base or size is not a
//                           multiple of the page size, or the range is not
//                           inside the memory the boot arguments describe.
//                           *Base and *Size hold what the property says, or 0
//                           if it is not 16 bytes long.
//
RETURN_STATUS AppleGetBootRamdisk(IN CONST APPLE_BOOT_INFO *Info, OUT UINT64 *Base, OUT UINT64 *Size);

#endif /* APPLEDTLIB_H */
