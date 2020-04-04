
#include <metal_stdlib>
using namespace metal;

static float random1(float x)
{
    float y = fract(sin(x)*100000.0);
    return y;
}

static float2 random2(float2 st){
    st = float2( dot(st,float2(127.1,311.7)),
              dot(st,float2(269.5,183.3)) );
    return -1.0 + 2.0*fract(sin(st)*43758.5453123);
}

static float randFromF2(float2 co)
{
    float a = 12.9898;
    float b = 78.233;
    float c = 43758.5453;
    float dt= dot(co.xy ,float2(a,b));
    
    float sn= fmod(dt, 3.14);
    return fract(sin(sn) * c);
}

static uint32_t RNG(thread uint32_t& state)
{
    uint32_t x = state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 15;
    state = x;
    return x;
}

static float RandomRNG(thread uint32_t& state)
{
    return (RNG(state) & 0xFFFFFF) / 16777216.0f;
}

static float randomF() {
    return float(0);
}

static float randomF(float mini, float maxi) {
    return mini + (maxi-mini)*randomF();
}

static float3 randomUnitFFF() {
    auto a = randomF(0, 2*M_PI_F);
    auto z = randomF(-1, 1);
    auto r = sqrt(1 - z*z);
    return float3(r*cos(a), r*sin(a), z);
}

static float2 randomInUnitDiskFF() {
    float2 p;
    do {
        p = 2.0 * float2(randomF(), randomF()) - float2(1);
    } while (dot(p, p) >= 1.0);
    return p;
}

static float3 randomInUnitSphereFFF() {
    float3 p;
    do {
        auto x = randomF();
        auto y = randomF();
        auto z = randomF();
        p = 2.0 * float3(x, y, z) - float3(1);
    } while (dot(p, p)>=1.0);
    return p;
}

static float schlick(float cosine, float ref_idx) {
    auto r0 = (1-ref_idx) / (1+ref_idx);
    r0 = r0*r0;
    return r0 + (1-r0)*pow((1 - cosine),5);
}
