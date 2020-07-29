#include "Random.hh"

//static pcg32_random_t pcg32_global = PCG32_INITIALIZER;

void pcg32_srandom_r(thread pcg32_random_t* rng, uint64_t initstate, uint64_t initseq)
{
    rng->state = 0U;
    rng->inc = (initseq << 1u) | 1u;
    pcg32_random_r(rng);
    rng->state += initstate;
    pcg32_random_r(rng);
}

//void pcg32_srandom(uint64_t seed, uint64_t seq)
//{
//    pcg32_srandom_r(&pcg32_global, seed, seq);
//}

uint32_t pcg32_random_r(thread pcg32_random_t* rng)
{
    uint64_t oldstate = rng->state;
    rng->state = oldstate * 6364136223846793005ULL + rng->inc;
    uint32_t xorshifted = ((oldstate >> 18u) ^ oldstate) >> 27u;
    uint32_t rot = oldstate >> 59u;
    return (xorshifted >> rot) | (xorshifted << ((-rot) & 31));
}

float randomF(thread pcg32_random_t* rng)
{
    //return pcg32_random_r(rng)/float(UINT_MAX);
    auto i = pcg32_random_r(rng);
    return ldexp(float(i), -32);
}

float randomF(float mini, float maxi, thread pcg32_random_t* rng) {
    return mini + (maxi-mini)*randomF(rng);
}

float3 randomUnit(thread pcg32_random_t* rng) {
    auto z = randomF(rng) * 2.0 - 1.0;
    auto a = randomF(rng) * 2*M_PI_F;
    auto r = sqrt(1-z*z);
    return float3(r*cos(a), r*sin(a), z);
}

float2 randomInUnitDiskFF(thread pcg32_random_t* rng) {
    float2 p;
    do {
        p = 2.0 * float2(randomF(rng), randomF(rng)) - float2(1,1);
    } while (dot(p, p)>=1.0);
    return normalize(p);
}

float3 randomInUnitSphereFFF(thread pcg32_random_t* rng) {
    float3 p;
    do {
        auto x = randomF(rng);
        auto y = randomF(rng);
        auto z = randomF(rng);
        p = 2.0 * float3(x, y, z) - float3(1);
    } while (dot(p, p)>=1.0);
    return normalize(p);
}

float3 randomInHemisphere(const thread float3& normal, thread pcg32_random_t* rng) {
    float3 direction = randomInUnitSphereFFF(rng);
    if (dot(normal, direction) > 0) {
        return direction;
    } else {
        return -direction;
    }
}
