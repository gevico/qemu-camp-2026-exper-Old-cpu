#ifndef HW_GPGPU_UTILS_H
#define HW_GPGPU_UTILS_H

#include <stdint.h>
#include <string.h>

#include "gpgpu_core.h"

/*
 * fpr[] 中保存的是寄存器原始 bit，不是 C 语言的 float 对象。
 * 通过 memcpy 做 bit 级转换，可以避免严格别名规则带来的未定义行为。
 */
static inline uint32_t gpgpu_f32_to_bits(float v)
{
    uint32_t bits;

    memcpy(&bits, &v, sizeof(bits));
    return bits;
}

static inline float gpgpu_bits_to_f32(uint32_t bits)
{
    float v;

    memcpy(&v, &bits, sizeof(v));
    return v;
}

/* 从模拟 lane 的浮点寄存器中读写 FP32 数值。 */
static inline float gpgpu_fpr_read_f32(const GPGPULane *lane, uint32_t reg)
{
    return gpgpu_bits_to_f32(lane->fpr[reg]);
}

static inline void gpgpu_fpr_write_f32(GPGPULane *lane, uint32_t reg, float v)
{
    lane->fpr[reg] = gpgpu_f32_to_bits(v);
}

static inline float gpgpu_scale_pow2(float v, int32_t exp)
{
    while (exp > 0) {
        v *= 2.0f;
        exp--;
    }

    while (exp < 0) {
        v *= 0.5f;
        exp++;
    }

    return v;
}

/*
 * BF16 保留 FP32 的符号位和 8 位指数，只把尾数压缩到 7 位。
 * FP32 -> BF16 时使用 round-to-nearest-even，减少简单截断带来的误差。
 */
static inline uint16_t gpgpu_f32_to_bf16(float v)
{
    uint32_t bits = gpgpu_f32_to_bits(v);
    uint32_t lsb = (bits >> 16) & 1u;
    uint32_t bias = 0x7fffu + lsb;

    return (uint16_t)((bits + bias) >> 16);
}

static inline float gpgpu_bf16_to_f32(uint16_t v)
{
    return gpgpu_bits_to_f32((uint32_t)v << 16);
}

/*
 * E4M3: sign(1) + exponent(4, bias=7) + mantissa(3)。
 * 这个自定义格式没有 Inf，溢出和 Inf 都饱和到 +/-448。
 */
static inline uint8_t gpgpu_f32_to_e4m3(float v)
{
    uint32_t bits = gpgpu_f32_to_bits(v);
    uint32_t sign = bits >> 31;
    uint32_t exp = (bits >> 23) & 0xffu;
    uint32_t frac = bits & 0x7fffffu;
    int32_t e4;
    uint32_t mant;

    if (exp == 0 && frac == 0) {
        return sign << 7;
    }

    if (exp == 0xff) {
        return (sign << 7) | 0x7eu;
    }

    e4 = (int32_t)exp - 127 + 7;
    if (e4 <= 0) {
        return sign << 7;
    }

    if (e4 > 15) {
        return (sign << 7) | 0x7eu;
    }

    mant = (frac + (1u << 19)) >> 20;
    if (mant == 8) {
        mant = 0;
        e4++;
    }

    if (e4 > 15 || (e4 == 15 && mant > 6)) {
        return (sign << 7) | 0x7eu;
    }

    return (sign << 7) | ((uint8_t)e4 << 3) | (uint8_t)mant;
}

static inline float gpgpu_e4m3_to_f32(uint8_t v)
{
    uint32_t sign = v >> 7;
    uint32_t exp = (v >> 3) & 0xfu;
    uint32_t mant = v & 0x7u;
    float f;

    if (exp == 0 && mant == 0) {
        return sign ? -0.0f : 0.0f;
    }

    if (exp == 0) {
        f = (float)mant / 512.0f;
    } else {
        f = 1.0f + (float)mant / 8.0f;
        f = gpgpu_scale_pow2(f, (int32_t)exp - 7);
    }

    return sign ? -f : f;
}

/*
 * E5M2: sign(1) + exponent(5, bias=15) + mantissa(2)。
 * 该格式保留 Inf/NaN 编码；有限值溢出时饱和到最大有限值。
 */
static inline uint8_t gpgpu_f32_to_e5m2(float v)
{
    uint32_t bits = gpgpu_f32_to_bits(v);
    uint32_t sign = bits >> 31;
    uint32_t exp = (bits >> 23) & 0xffu;
    uint32_t frac = bits & 0x7fffffu;
    int32_t e5;
    uint32_t mant;

    if (exp == 0 && frac == 0) {
        return sign << 7;
    }

    if (exp == 0xff) {
        return (sign << 7) | (frac ? 0x7fu : 0x7cu);
    }

    e5 = (int32_t)exp - 127 + 15;
    if (e5 <= 0) {
        return sign << 7;
    }

    if (e5 >= 31) {
        return (sign << 7) | 0x7bu;
    }

    mant = (frac + (1u << 20)) >> 21;
    if (mant == 4) {
        mant = 0;
        e5++;
    }

    if (e5 >= 31) {
        return (sign << 7) | 0x7bu;
    }

    return (sign << 7) | ((uint8_t)e5 << 2) | (uint8_t)mant;
}

static inline float gpgpu_e5m2_to_f32(uint8_t v)
{
    uint32_t sign = v >> 7;
    uint32_t exp = (v >> 2) & 0x1fu;
    uint32_t mant = v & 0x3u;
    float f;

    if (exp == 0 && mant == 0) {
        return sign ? -0.0f : 0.0f;
    }

    if (exp == 0x1f) {
        return gpgpu_bits_to_f32(sign ? 0xff800000u : 0x7f800000u);
    }

    if (exp == 0) {
        f = (float)mant / 65536.0f;
    } else {
        f = 1.0f + (float)mant / 4.0f;
        f = gpgpu_scale_pow2(f, (int32_t)exp - 15);
    }

    return sign ? -f : f;
}

/*
 * E2M1: sign(1) + exponent(2, bias=1) + mantissa(1)。
 * 这个自定义测试格式没有 Inf/NaN，溢出时饱和到 +/-6。
 */
static inline uint8_t gpgpu_f32_to_e2m1(float v)
{
    uint32_t bits = gpgpu_f32_to_bits(v);
    uint32_t sign = bits >> 31;
    uint32_t exp = (bits >> 23) & 0xffu;
    uint32_t frac = bits & 0x7fffffu;
    int32_t e2;
    uint32_t mant;

    if (exp == 0 && frac == 0) {
        return sign << 3;
    }

    if (exp == 0xff) {
        return (sign << 3) | 0x7u;
    }

    e2 = (int32_t)exp - 127 + 1;
    if (e2 <= 0) {
        return sign << 3;
    }

    if (e2 > 3) {
        return (sign << 3) | 0x7u;
    }

    mant = (frac + (1u << 21)) >> 22;
    if (mant == 2) {
        mant = 0;
        e2++;
    }

    if (e2 > 3) {
        return (sign << 3) | 0x7u;
    }

    return (sign << 3) | ((uint8_t)e2 << 1) | (uint8_t)mant;
}

static inline float gpgpu_e2m1_to_f32(uint8_t v)
{
    uint32_t sign = v >> 3;
    uint32_t exp = (v >> 1) & 0x3u;
    uint32_t mant = v & 0x1u;
    float f;

    if (exp == 0 && mant == 0) {
        return sign ? -0.0f : 0.0f;
    }

    if (exp == 0) {
        f = (float)mant * 0.5f;
    } else {
        f = 1.0f + (float)mant * 0.5f;
        f = gpgpu_scale_pow2(f, (int32_t)exp - 1);
    }

    return sign ? -f : f;
}

#endif /* HW_GPGPU_UTILS_H */
