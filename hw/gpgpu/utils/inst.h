#ifndef HW_GPGPU_INST_H
#define HW_GPGPU_INST_H

#include "qemu/osdep.h"

typedef enum GPGPUInstType {
    TYPE_NONE,
    TYPE_R,
    TYPE_I,
    TYPE_B,
    TYPE_S,
    TYPE_U,
} GPGPUInstType;

typedef struct GPGPUDecode {
    uint32_t pc;     /* 当前指令地址，也就是本次取指的位置 */
    uint32_t snpc;   /* 顺序下一条指令地址，通常为 pc + 4 */
    uint32_t inst;   /* 从 VRAM 中取出的 32 位 RISC-V 机器码 */

    uint32_t rd;     /* 目标寄存器编号，例如 rd=28 表示 x28 */
    uint32_t rs1;    /* 第一个源寄存器编号，例如 rs1=28 表示 x28 */
    uint32_t rs2;    /* 第二个源寄存器编号，例如 rs2=7 表示 x7 */
    int32_t imm;     /* 指令立即数，按指令类型解码并完成符号扩展 */
} GPGPUDecode;

#define BITS(x, hi, lo) (((x) >> (lo)) & ((1u << ((hi) - (lo) + 1)) - 1))
#define GPGPU_EBREAK 0x00100073

static inline int32_t sext(uint32_t val, unsigned bits)
{
    uint32_t sign = 1u << (bits - 1);
    return (int32_t)((val ^ sign) - sign);
}

static inline int32_t imm_i(uint32_t inst)
{
    return sext(BITS(inst, 31, 20), 12);
}

static inline int32_t imm_s(uint32_t inst)
{
    return sext((BITS(inst, 31, 25) << 5) | BITS(inst, 11, 7), 12);
}

static inline int32_t imm_b(uint32_t inst)
{
    return sext((BITS(inst, 31, 31) << 12) |
                (BITS(inst, 7, 7) << 11) |
                (BITS(inst, 30, 25) << 5) |
                (BITS(inst, 11, 8) << 1), 13);
}

static inline int32_t imm_u(uint32_t inst)
{
    return inst & 0xfffff000u;
}

static inline void decode_operand(GPGPUDecode *ctx, GPGPUInstType type)
{
    uint32_t inst = ctx->inst;

    ctx->rd = BITS(inst, 11, 7);
    ctx->rs1 = BITS(inst, 19, 15);
    ctx->rs2 = BITS(inst, 24, 20);

    switch (type) {
    case TYPE_I:
        ctx->imm = imm_i(inst);
        break;
    case TYPE_B:
        ctx->imm = imm_b(inst);
        break;
    case TYPE_S:
        ctx->imm = imm_s(inst);
        break;
    case TYPE_U:
        ctx->imm = imm_u(inst);
        break;
    default:
        ctx->imm = 0;
        break;
    }
}

static inline void pattern_decode(const char *str, uint32_t *key, uint32_t *mask)
{
    int bit = 31;

    *key = 0;
    *mask = 0;

    for (; *str; str++) {
        if (*str == ' ') {
            continue;
        }

        if (*str == '0' || *str == '1') {
            *mask |= 1u << bit;
            if (*str == '1') {
                *key |= 1u << bit;
            }
            bit--;
        } else if (*str == '?') {
            bit--;
        }
    }
}

/*
 * NEMU 风格指令匹配宏。
 * pattern: 32 位指令模板，0/1 表示必须匹配的位，? 表示忽略位。
 * name:    指令名，仅用于让调用处自说明，例如 add/lui/sw。
 * type:    指令格式，用于解码 rd/rs1/rs2/imm。
 * body:    匹配成功后执行的指令语义，通常修改 lane->gpr 或 VRAM。
 *
 * 匹配成功流程: pattern -> key/mask -> decode_operand() -> body -> decode_success。
 */
#define INSTPAT(pattern, name, type, body) do {            \
    uint32_t key, mask;                                    \
    pattern_decode(pattern, &key, &mask);                  \
    if ((ctx->inst & mask) == key) {                       \
        decode_operand(ctx, type);                         \
        uint32_t src1 = lane->gpr[ctx->rs1];               \
        uint32_t src2 = lane->gpr[ctx->rs2];               \
        int32_t imm = ctx->imm;                            \
        (void)src1; (void)src2; (void)imm;                 \
        body;                                              \
        goto decode_success;                               \
    }                                                      \
} while (0)

#endif
