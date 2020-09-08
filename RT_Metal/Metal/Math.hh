#ifndef Math_h
#define Math_h

#include "Common.hh"

inline float PBRT_Log2(float x) {
    const float invLog2 = 1.442695040888963387004650940071;
    return log(x) * invLog2;
}

inline int PBRT_Log2Int(uint32_t v) {
    return 31 - clz(v);
}

template <typename T>
inline bool IsPowerOf2(T v) {
    return v && !(v & (v - 1));
}

template <class T> inline T RoundUpPow2(T v) {
    v--;

    v |= v >> 1;
    v |= v >> 2;
    v |= v >> 4;
    v |= v >> 8;
    v |= v >> 16;

    return v+1;
}

inline unsigned int RoundUpPow2(unsigned int v) {
    v--;
    
    v |= v >> 1;
    v |= v >> 2;
    v |= v >> 4;
    v |= v >> 8;
    v |= v >> 16;
    
    return v+1;
}

inline int CountTrailingZeros(uint32_t v) {
    return __builtin_ctz(v);
}

#endif /* Math_h */
