#ifndef Math_h
#define Math_h

#include "Common.hh"

// Global Inline Functions
inline int32_t FloatToInt(float f) {
    return as_type<int32_t>(f);
}

inline float IntToFloat(int32_t i) {
    return as_type<float>(i);
}

inline uint32_t FloatToBits(float f) {
    uint32_t ui = as_type<uint32_t>(f);
    return ui;
}

inline float BitsToFloat(uint32_t ui) {
    float f = as_type<float>(ui);
    return f;
}

inline float NextFloatUp(float v) {
    // Handle infinity and negative zero for _NextFloatUp()_
    if (isinf(v) && v > 0.) return v;
    if (v == -0.f) v = 0.f;

    // Advance _v_ to next higher float
    uint32_t ui = FloatToBits(v);
    if (v >= 0)
        ++ui;
    else
        --ui;
    return BitsToFloat(ui);
}

inline float NextFloatDown(float v) {
    // Handle infinity and positive zero for _NextFloatDown()_
    if (isinf(v) && v < 0.) return v;
    if (v == 0.f) v = -0.f;
    uint32_t ui = FloatToBits(v);
    if (v > 0)
        --ui;
    else
        ++ui;
    return BitsToFloat(ui);
}

#define MachineEpsilon FLT_EPSILON * 0.5

inline constexpr float gamma(int n) {
    return (n * MachineEpsilon) / (1 - n * MachineEpsilon);
}

constexpr float origin()      { return 1.0f / 32.0f; }
constexpr float float_scale() { return 1.0f / 65536.0f; }
constexpr float int_scale()   { return 256.0f; }

// Normal points outward for rays exiting the surface, else is flipped.
inline float3 offset_ray(const float3 p, const float3 n)
{
  int3 of_i(int_scale() * n.x, int_scale() * n.y, int_scale() * n.z);

  float3 p_i(
             IntToFloat(FloatToInt(p.x) + ((p.x < 0) ? -of_i.x : of_i.x)),
             IntToFloat(FloatToInt(p.y) + ((p.y < 0) ? -of_i.y : of_i.y)),
             IntToFloat(FloatToInt(p.z) + ((p.z < 0) ? -of_i.z : of_i.z)));
    
  return float3(abs(p.x) < origin() ? p.x+float_scale()*n.x : p_i.x,
                abs(p.y) < origin() ? p.y+float_scale()*n.y : p_i.y,
                abs(p.z) < origin() ? p.z+float_scale()*n.z : p_i.z);
}

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

inline float ErfInv(float x) {
    float w, p;
    x = clamp(x, -.99999f, .99999f);
    w = -log((1 - x) * (1 + x));
    if (w < 5) {
        w = w - 2.5f;
        p = 2.81022636e-08f;
        p = 3.43273939e-07f + p * w;
        p = -3.5233877e-06f + p * w;
        p = -4.39150654e-06f + p * w;
        p = 0.00021858087f + p * w;
        p = -0.00125372503f + p * w;
        p = -0.00417768164f + p * w;
        p = 0.246640727f + p * w;
        p = 1.50140941f + p * w;
    } else {
        w = sqrt(w) - 3;
        p = -0.000200214257f;
        p = 0.000100950558f + p * w;
        p = 0.00134934322f + p * w;
        p = -0.00367342844f + p * w;
        p = 0.00573950773f + p * w;
        p = -0.0076224613f + p * w;
        p = 0.00943887047f + p * w;
        p = 1.00167406f + p * w;
        p = 2.83297682f + p * w;
    }
    return p * x;
}

inline float Erf(float x) {
    // constants
    float a1 = 0.254829592f;
    float a2 = -0.284496736f;
    float a3 = 1.421413741f;
    float a4 = -1.453152027f;
    float a5 = 1.061405429f;
    float p = 0.3275911f;

    // Save the sign of x
    int sign = 1;
    if (x < 0) sign = -1;
    x = abs(x);

    // A&S formula 7.1.26
    float t = 1 / (1 + p * x);
    float y = 1 - (((((a5 * t + a4) * t) + a3) * t + a2) * t + a1) * t * exp(-x * x);

    return sign * y;
}

#endif /* Math_h */
