/*
 * QEMU GPGPU - RISC-V SIMT Core Implementation
 *
 * Copyright (c) 2024-2025
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "qemu/bswap.h"
#include "qemu/log.h"
#include "gpgpu.h"
#include "gpgpu_core.h"
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "./utils/inst.h"
#include "./utils/utils.h"

static int gpgpu_core_exec_lane(GPGPUState *s, GPGPULane *lane,
                                uint32_t max_cycles);


/* GPGPU kernel 调度路径：kernel -> block -> warp -> lane。 */

/* 在解释执行前初始化一个 warp 以及其中活跃的 lane。 */
void gpgpu_core_init_warp(GPGPUWarp *warp, uint32_t pc,
                          uint32_t thread_id_base, const uint32_t block_id[3],
                          uint32_t num_threads,
                          uint32_t warp_id, uint32_t block_id_linear)
{

    memset(warp, 0, sizeof(*warp));

    warp->thread_id_base = thread_id_base;
    warp->warp_id = warp_id;
    
    warp->block_id[0] = block_id[0];
    warp->block_id[1] = block_id[1];
    warp->block_id[2] = block_id[2];

    num_threads = MIN(num_threads, GPGPU_WARP_SIZE);

    for(uint32_t lane = 0; lane < num_threads; lane++){
        GPGPULane *l = &warp->lanes[lane];

        l->pc = pc;
        l->mhartid = MHARTID_ENCODE(block_id_linear, warp_id, lane);
        l->active = true;

        warp->active_mask |= 1u << lane;
    }

}

/* 使用软件解释器依次执行 warp 中的活跃 lane。 */
int gpgpu_core_exec_warp(GPGPUState *s, GPGPUWarp *warp, uint32_t max_cycles)
{
    s->simt.thread_mask = warp->active_mask;

    for (uint32_t lane = 0; lane < GPGPU_WARP_SIZE; lane++) {
        if (!(warp->active_mask & (1u << lane))) {
            continue;
        }

        s->simt.thread_id[0] = warp->thread_id_base + lane;
        s->simt.thread_id[1] = 0;
        s->simt.thread_id[2] = 0;
        s->simt.block_id[0] = warp->block_id[0];
        s->simt.block_id[1] = warp->block_id[1];
        s->simt.block_id[2] = warp->block_id[2];
        s->simt.warp_id = warp->warp_id;
        s->simt.lane_id = lane;

        warp->lanes[lane].gpr[10] = (uint32_t)s->kernel.kernel_args;

        if (gpgpu_core_exec_lane(s, &warp->lanes[lane], max_cycles) < 0) {
            return -1;
        }
    }

    return 0;
}

/* 按寄存器中配置的 grid/block 形状分发并执行 kernel。 */
int gpgpu_core_exec_kernel(GPGPUState *s)
{
    uint32_t grid_x = s->kernel.grid_dim[0];
    uint32_t grid_y = s->kernel.grid_dim[1];
    uint32_t grid_z = s->kernel.grid_dim[2];

    uint32_t block_x = s->kernel.block_dim[0];
    uint32_t block_y = s->kernel.block_dim[1];
    uint32_t block_z = s->kernel.block_dim[2];

    uint64_t total_blocks;
    uint64_t threads_per_block;

    if(!s->vram_ptr){
        return -1;
    }

    if (!grid_x || !grid_y || !grid_z ||
        !block_x || !block_y || !block_z) {
        return -1;
    }

    total_blocks = (uint64_t)grid_x * grid_y * grid_z;
    threads_per_block = (uint64_t)block_x * block_y * block_z;

    for (uint64_t block = 0; block < total_blocks; block++) {
            uint32_t block_id[3] = {
                block % grid_x,
                (block / grid_x) % grid_y,
                block / ((uint64_t)grid_x * grid_y),
            };

            for (uint64_t thread_base = 0;
                thread_base < threads_per_block;
                thread_base += GPGPU_WARP_SIZE) {
                GPGPUWarp warp;
                uint32_t remaining = threads_per_block - thread_base;
                uint32_t active_threads = MIN(remaining, GPGPU_WARP_SIZE);

                gpgpu_core_init_warp(&warp,
                                    s->kernel.kernel_addr,
                                    thread_base,
                                    block_id,
                                    active_threads,
                                    thread_base / GPGPU_WARP_SIZE,
                                    block);

                if (gpgpu_core_exec_warp(s, &warp, 100000) < 0) {
                    return -1;
                }
            }
        }
    return 0;
}

static int gpgpu_core_exec_lane(GPGPUState *s, GPGPULane *lane,
                                uint32_t max_cycles)
{
    for (uint32_t i = 0; i < max_cycles; i++) {
        GPGPUDecode ctx;
        int ret;

        if (lane->pc + 4 > s->vram_size) {
            return -1;
        }

        ctx.pc = lane->pc;
        ctx.snpc = lane->pc + 4;
        ctx.inst = ldl_le_p(s->vram_ptr + lane->pc);

        ret = gpgpu_decode_exec(s, lane, &ctx);
        if (ret > 0) {
            return 0;
        }
        if (ret < 0) {
            return -1;
        }
    }

    return -1;
}
