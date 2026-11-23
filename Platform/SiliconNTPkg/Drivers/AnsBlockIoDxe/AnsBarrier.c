/** @file
  The two barriers of ans_env.h, as m1n1 writes them: dma_wmb() is "dmb oshst"
  and dma_rmb() is "dmb oshld" (m1n1 src/utils.h). A file of their own because
  they are the one part of the environment that is ARM code; the host test
  brings its own.

  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include "AnsEnv.h"

void
ans_env_wmb (
  struct ans_env  *env
  )
{
  __asm__ __volatile__ ("dmb oshst" ::: "memory");
}

void
ans_env_rmb (
  struct ans_env  *env
  )
{
  __asm__ __volatile__ ("dmb oshld" ::: "memory");
}
