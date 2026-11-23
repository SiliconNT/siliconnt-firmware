/** @file
  The environment the disk controller's core runs in inside the firmware:
  ans_env.h of siliconnt-drivers (ans/core) implemented with the firmware's
  libraries. AnsEnv.c says how each part is done; AnsBarrier.c holds the two
  barriers.

  The firmware has one thread and polls instead of taking interrupts: the core
  is only entered from AnsBlockIoDxe.c, one call at a time, so the environment
  needs no lock.

  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#ifndef ANS_FIRMWARE_ENV_H_
#define ANS_FIRMWARE_ENV_H_

#include <Uefi.h>

#include "siliconnt-drivers/ans/core/ans_core.h"

//
// What ans_env_dma_alloc() hands out starts at a device address aligned to this
// (ans_env.h)
//
#define ANS_ENV_DMA_ALIGNMENT  SIZE_16KB

//
// The largest block of device memory handed out. The core asks for queues and
// PRP lists of a few pages each.
//
#define ANS_ENV_DMA_MAX_SIZE  SIZE_16MB

typedef struct {
  UINT64    Base;     // CPU address; 0: the region is not there
  UINT64    Size;     // bytes
} ANS_ENV_REGION;

struct ans_env {
  ANS_ENV_REGION    Region[ANS_RGN_COUNT];

  //
  // Set from ExitBootServices on: the memory map is final and nothing may be
  // allocated or freed any more. Device memory the core gives back is left as
  // it is; it is boot services data, which the operating system takes over.
  //
  BOOLEAN           Exiting;

  UINT32            Refused;  // register accesses refused
};

#endif // ANS_FIRMWARE_ENV_H_
