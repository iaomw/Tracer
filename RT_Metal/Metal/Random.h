
#ifndef Random_h
#define Random_h

static float random1(float x);

static float2 random2(float2 st);

static float randFromF2(float2 co);

static uint32_t RNG(thread uint32_t& state);

static float RandomRNG(thread uint32_t& state);
static float randomF();

static float randomF(float mini, float maxi);

static float3 randomUnitFFF();

static float2 randomInUnitDiskFF();

static float3 randomInUnitSphereFFF();

static float schlick(float cosine, float ref_idx);

#endif /* Random_h */
