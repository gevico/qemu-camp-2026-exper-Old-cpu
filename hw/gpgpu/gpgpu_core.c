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

#include "inst.h"
#include "utils.h"

static int gpgpu_core_exec_lane(GPGPUState *s, GPGPULane *lane,
                                uint32_t max_cycles);


/* NEMU 风格的取指、译码、执行解释器。 */
static int gpgpu_decode_exec(GPGPUState *s, GPGPULane *lane,
                             GPGPUDecode *ctx)
{
#define R(i) lane->gpr[(i)]
#define G(x) lane->gpr[ctx->x]



    INSTPAT("??????? ????? ????? ??? ????? 01101 11",
            lui, TYPE_U, {
        G(rd) = imm;
    });

    INSTPAT("??????? ????? ????? 000 ????? 00100 11",
            addi, TYPE_I, {
        G(rd) = src1 + imm;
    });

    INSTPAT("0000000 ????? ????? 001 ????? 00100 11",
            slli, TYPE_I, {
        G(rd) = src1 << (imm & 0x1f);
    });

    INSTPAT("??????? ????? ????? 111 ????? 00100 11",
            andi, TYPE_I, {
        G(rd) = src1 & imm;
    });

    INSTPAT("0000000 ????? ????? 000 ????? 01100 11",
            add, TYPE_R, {
        G(rd) = src1 + src2;
    });

    INSTPAT("0100000 ????? ????? 000 ????? 01100 11",
            sub, TYPE_R, {
        G(rd) = (int32_t)src1 - (int32_t)src2;
    });

    INSTPAT("??????? ????? ????? 010 ????? 01000 11",
            sw, TYPE_S, {
        uint32_t addr = src1 + imm;
        if (addr + 4 > s->vram_size) {
            return -1;
        }
        stl_le_p(s->vram_ptr + addr, src2);
    });

    INSTPAT("???????????? ????? 010 ????? 11100 11",
            csrrs, TYPE_I, {
        uint32_t csr = ctx->inst >> 20;

        if (csr != CSR_MHARTID) {
            return -1;
        }

        G(rd) = lane->mhartid;
    });

    INSTPAT("0000000 00001 00000 000 00000 11100 11",
            ebreak, TYPE_NONE, {
        ctx->snpc = ctx->pc + 4;
        lane->pc = ctx->snpc;
        R(0) = 0;
        return 1;
    });

    /* 测试 kernel 用到的 RV32F 浮点指令子集。 */
    /* x[rs1] 的有符号整数值转换成 FP32，写入 f[rd]。 */
    INSTPAT("1101000 00000 ????? ??? ????? 10100 11",
            fcvt_s_w, TYPE_R, {
        gpgpu_fpr_write_f32(lane, ctx->rd, (float)(int32_t)src1);
    });

    INSTPAT("0001000 ????? ????? ??? ????? 10100 11",
            fmul_s, TYPE_R, {
        float a = gpgpu_fpr_read_f32(lane, ctx->rs1);
        float b = gpgpu_fpr_read_f32(lane, ctx->rs2);

        gpgpu_fpr_write_f32(lane, ctx->rd, a * b);
    });

    INSTPAT("0000000 ????? ????? ??? ????? 10100 11",
            fadd_s, TYPE_R, {
        float a = gpgpu_fpr_read_f32(lane, ctx->rs1);
        float b = gpgpu_fpr_read_f32(lane, ctx->rs2);

        gpgpu_fpr_write_f32(lane, ctx->rd, a + b);
    });

    /* f[rs1] 的 FP32 值按 C 转换规则截断为有符号整数，写入 x[rd]。 */
    INSTPAT("1100000 00000 ????? ??? ????? 10100 11",
            fcvt_w_s, TYPE_R, {
        float v = gpgpu_fpr_read_f32(lane, ctx->rs1);

        G(rd) = (int32_t)v;
    });

    /* fmv.w.x 不做数值转换，只把 x[rs1] 的 32 位原始 bit 搬到 f[rd]。 */
    INSTPAT("1111000 00000 ????? 000 ????? 10100 11",
            fmv_w_x, TYPE_R, {
        lane->fpr[ctx->rd] = src1;
    });

    /* 自定义低精度浮点转换指令子集：BF16、E4M3、E5M2、E2M1。 */
    /* FP32 -> BF16：低 16 位保存 BF16 编码。 */
    INSTPAT("0100010 00001 ????? ??? ????? 10100 11",
            fcvt_bf16_s, TYPE_R, {
        float v = gpgpu_fpr_read_f32(lane, ctx->rs1);

        lane->fpr[ctx->rd] = gpgpu_f32_to_bf16(v);
    });

    /* BF16 -> FP32：从 f[rs1] 的低 16 位取 BF16 编码并扩展回 FP32。 */
    INSTPAT("0100010 00000 ????? ??? ????? 10100 11",
            fcvt_s_bf16, TYPE_R, {
        uint16_t v = lane->fpr[ctx->rs1] & 0xffffu;

        gpgpu_fpr_write_f32(lane, ctx->rd, gpgpu_bf16_to_f32(v));
    });

    /* FP32 -> E4M3：低 8 位保存 E4M3 编码，溢出时饱和。 */
    INSTPAT("0100100 00001 ????? ??? ????? 10100 11",
            fcvt_e4m3_s, TYPE_R, {
        float v = gpgpu_fpr_read_f32(lane, ctx->rs1);

        lane->fpr[ctx->rd] = gpgpu_f32_to_e4m3(v);
    });

    /* E4M3 -> FP32：从低 8 位取 E4M3 编码并扩展回 FP32。 */
    INSTPAT("0100100 00000 ????? ??? ????? 10100 11",
            fcvt_s_e4m3, TYPE_R, {
        uint8_t v = lane->fpr[ctx->rs1] & 0xffu;

        gpgpu_fpr_write_f32(lane, ctx->rd, gpgpu_e4m3_to_f32(v));
    });

    /* FP32 -> E5M2：低 8 位保存 E5M2 编码，支持 Inf/NaN 编码。 */
    INSTPAT("0100100 00011 ????? ??? ????? 10100 11",
            fcvt_e5m2_s, TYPE_R, {
        float v = gpgpu_fpr_read_f32(lane, ctx->rs1);

        lane->fpr[ctx->rd] = gpgpu_f32_to_e5m2(v);
    });

    /* E5M2 -> FP32：从低 8 位取 E5M2 编码并扩展回 FP32。 */
    INSTPAT("0100100 00010 ????? ??? ????? 10100 11",
            fcvt_s_e5m2, TYPE_R, {
        uint8_t v = lane->fpr[ctx->rs1] & 0xffu;

        gpgpu_fpr_write_f32(lane, ctx->rd, gpgpu_e5m2_to_f32(v));
    });

    /* FP32 -> E2M1：低 4 位保存 E2M1 编码，溢出时饱和到 +/-6。 */
    INSTPAT("0100110 00001 ????? ??? ????? 10100 11",
            fcvt_e2m1_s, TYPE_R, {
        float v = gpgpu_fpr_read_f32(lane, ctx->rs1);

        lane->fpr[ctx->rd] = gpgpu_f32_to_e2m1(v);
    });

    /* E2M1 -> FP32：从低 4 位取 E2M1 编码并扩展回 FP32。 */
    INSTPAT("0100110 00000 ????? ??? ????? 10100 11",
            fcvt_s_e2m1, TYPE_R, {
        uint8_t v = lane->fpr[ctx->rs1] & 0xfu;

        gpgpu_fpr_write_f32(lane, ctx->rd, gpgpu_e2m1_to_f32(v));
    });

    return -1;

    decode_success:
        lane->pc = ctx->snpc;
        R(0) = 0;
        return 0;

#undef G
#undef R
}

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
