#pragma once

#define GETBIT(num, bit) (((num) >> (bit)) & 1)

template<class T>
T rotate_right(T num, int size, int shift) {
    shift %= size;
    if (shift < 0) shift += size;
    T all_up = (T(1) << size) - 1;
    num &= all_up;
    T u = num >> shift;
    T v = (num << (size - shift)) & all_up;
    return u | v;
}
