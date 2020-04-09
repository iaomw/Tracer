
#ifndef Random_h
#define Random_h

#include <metal_stdlib>

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

//void pcg32_srandom(uint64_t initstate, uint64_t initseq);
void pcg32_srandom_r(thread pcg32_random_t* rng, uint64_t initstate,
                     uint64_t initseq);

// pcg32_random()
// pcg32_random_r(rng)
//     Generate a uniformly distributed 32-bit random number

//uint32_t pcg32_random(void);
uint32_t pcg32_random_r(thread pcg32_random_t* rng);

// pcg32_boundedrand(bound):
// pcg32_boundedrand_r(rng, bound):
//     Generate a uniformly distributed number, r, where 0 <= r < bound

//uint32_t pcg32_boundedrand(uint32_t bound);
uint32_t pcg32_boundedrand_r(thread pcg32_random_t* rng, uint32_t bound);


float randomF(thread pcg32_random_t* rng);
float randomF(float mini, float maxi, thread pcg32_random_t* rng);

float2 randomInUnitDiskFF(thread pcg32_random_t* rng);
float3 randomInUnitSphereFFF(thread pcg32_random_t* rng);

#endif /* Random_h */
