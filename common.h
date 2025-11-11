#pragma once

#include <stdint.h>

typedef uint64_t u64;
typedef int64_t i64;
typedef uint32_t u32;
typedef int32_t i32;
typedef uint16_t u16;
typedef int16_t i16;
typedef uint8_t u8;
typedef int8_t i8;

[[nodiscard]] constexpr u32 get_part(u32 in, u32 ileft, u32 iright, u32 oright = 0)
{
    return (in << (31 - ileft)) >> (31 - ileft + iright) << oright;
}

[[nodiscard]] constexpr i32 get_part_s(u32 in, u32 ileft, u32 iright, u32 oright = 0)
{
    return ((i32)(in << (31 - ileft)) >> (31 - ileft + iright)) << oright;
}

[[nodiscard]] constexpr i32 sign_extend(u32 in, u32 left)
{
    return ((i32)(in << (31 - left))) >> (31 - left);
}

[[nodiscard]] constexpr u32 create_mask(u32 left, u32 right)
{
    return get_part(-1, left, right, right);
}

[[nodiscard]] constexpr u32 set_part(u32 d, u32 dleft, u32 dright, u32 s, u32 sright = 0)
{
    u32 mask = create_mask(dleft, dright);
    if (dright > sright) {
        return (d & ~mask) | (mask & (s << (dright - sright)));
    } else {
        return (d & ~mask) | (mask & (s >> (sright - dright)));
    }
}

template <typename T>
[[nodiscard]] constexpr T set_bit(T d, u32 index)
{
    return d | T(1) << index;
}

template <typename T>
[[nodiscard]] constexpr T clear_bit(T d, u32 index)
{
    return d & ~(T(1) << index);
}
