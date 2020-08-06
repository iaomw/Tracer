#ifndef Random_h
#define Random_h

#include "Common.hh"

struct pcg_state_setseq_64 {    // Internals are *Private*.
    uint64_t state;             // RNG state.  All values are possible.
    uint64_t inc;               // Controls which RNG sequence (stream) is
                                // selected. Must *always* be odd.
};

typedef struct pcg_state_setseq_64 pcg32_random_t;

// If you *must* statically initialize it, here's one.

#define PCG32_INITIALIZER   { 0x853c49e6748fea9bULL, 0xda3e39cb94b95bdbULL }

// pcg32_srandom(initstate, initseq)
// pcg32_srandom_r(rng, initstate, initseq):
//     Seed the rng.  Specified in two parts, state initializer and a
//     sequence selection constant (a.k.a. stream id)

void pcg32_srandom_r(thread pcg32_random_t* rng, uint64_t initstate, uint64_t initseq);

uint32_t pcg32_random_r(thread pcg32_random_t* rng);

uint32_t pcg32_boundedrand_r(thread pcg32_random_t* rng, uint32_t bound);

float randomF(thread pcg32_random_t* rng);
float randomF(float mini, float maxi, thread pcg32_random_t* rng);

float3 randomUnit(thread pcg32_random_t* rng);
float2 randomInUnitDiskFF(thread pcg32_random_t* rng);
float3 randomInUnitSphereFFF(thread pcg32_random_t* rng);
float3 randomInHemisphere(const thread float3& normal, thread pcg32_random_t* rng);

#endif /* Random_h */
