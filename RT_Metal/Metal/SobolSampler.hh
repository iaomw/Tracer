#ifndef SobolSampler_h
#define SobolSampler_h

#include "Math.hh"
#include "Random.hh"
#include "sobolmatrices.hh"

namespace pbrt {

template <typename T>
struct Bounds {
    T maxi, mini;
    
    T diagonal() const {
        return maxi - mini;
    }
};

typedef Bounds<float2> Bounds2f;
typedef Bounds<int2> Bounds2i;

typedef Bounds<float3> Bounds3f;
typedef Bounds<int3> Bounds3i;

// SobolSampler Declarations
struct SobolSampler {
    
public:
    pcg32_t rng;
    
private:
    
    uint2 xy;
    
    uint32_t resolution, log2Resolution;
    
    uint64_t mSampleIndex=0;
    uint64_t mSobolIndex=0;
    
    uint64_t mScrable=0;
    uint mDimension=0;
    
public:
    
    float random() {
        return randomF(&rng);
    }
    
    SobolSampler(const thread pcg32_t& rng, uint frame, const thread uint2& xy, uint2 wh): rng(rng), xy(xy), mSampleIndex(frame)
    {
        //samplePerPixel = RoundUpPow2(samplesPerPixel);
        //if (!IsPowerOf2(samplesPerPixel)) { /* Warning */ }
        
        //resolution = RoundUpPow2(max(xy.x, xy.y));
        resolution = RoundUpPow2(max(wh.x, wh.y));
        //log2Resolution = log2((float)resolution);
        log2Resolution = PBRT_Log2Int(resolution);
        
        mSobolIndex = GetIndexForSample(mSampleIndex); //mDimension = 0;
    }
    
    inline float sample1D() {
        return SampleDimension(mSobolIndex, mDimension++);
    }
    
    float2 sample2D() {
        auto a = sample1D();
        auto b = sample1D();
        
        return float2(a, b);
    }
    
    float3 sample3D() {
        auto a = sample1D();
        auto b = sample1D();
        auto c = sample1D();
        
        return float3(a, b, c);
    }
    
    float3 sampleUnit() {
        auto a = sample1D() * 2 * M_PI_F;
        auto z = sample1D() * 2.0 - 1.0;
        auto r = sqrt(max(FLT_EPSILON, 1-z*z));
        
        return { r*cos(a), r*sin(a), z };
    }

    float2 sampleUnitInDisk() {
        float2 p = float2(sample1D(), 1-sample1D());
        //p = 2.0 * p - float2(1,1);
        
        return normalize(p);
    }

    float3 sampleUnitInSphere() {
        
        float3 p = float3(sample1D(), sample1D(), sample1D());
        p = 2.0 * p - float3(1);
        
        return normalize(p);
    }

    float3 randomUnitInHemisphere(const thread float3& normal) {
        float3 direction = sampleUnit();
        if (dot(normal, direction) > 0) {
            return direction;
        } else {
            return -direction;
        }
    }
    
    uint64_t GetIndexForSample(uint64_t sampleIndex) const;
    float SampleDimension(uint64_t index, uint dimension) const;
};

inline uint64_t SobolIntervalToIndex(const uint32_t m,
                                     uint64_t sampleIndex,
                                     const thread uint2& p)
{
    if (m == 0) return 0;
    
    const uint32_t m2 = m << 1;
    uint64_t index = sampleIndex << m2;
    
    uint64_t delta = 0;
    for (int c = 0; sampleIndex; sampleIndex >>= 1, ++c)
        if (sampleIndex & 1)  // Add flipped column m + c + 1.
            delta ^= VdCSobolMatrices[m - 1][c];

    // flipped b
    uint64_t b = (((uint64_t)p.x << m) | (uint32_t)p.y) ^ delta;
    
    for (int c = 0; b; b >>= 1, ++c)
        if (b & 1)  // Add column 2 * m - c.
            index ^= VdCSobolMatricesInv[m - 1][c];

    return index;
}

uint64_t SobolSampler::GetIndexForSample(uint64_t sampleIndex) const {
    return SobolIntervalToIndex(log2Resolution, sampleIndex, xy);
}

inline float SobolSampleFloat(uint64_t _index, int dimension, uint32_t scramble=0) {
    
    uint32_t v = scramble;
    
    for (int i = dimension * SobolMatrixSize; _index != 0; _index >>= 1, i++)
        if (_index & 1) v ^= SobolMatrices32[i];
    
    return min(v * 2.3283064365386963e-10f /* 1/2^32 */, 1.0-FLT_EPSILON);
    //return min(v * 0x1p-32f /* 1/2^32 */, 1.0-FLT_EPSILON);
}

float SobolSampler::SampleDimension(uint64_t index, uint dimension) const {
    if (dimension >= pbrt::NumSobolDimensions) { return 0; }
    
    float s = SobolSampleFloat(index, dimension);
    
    if (dimension <=1 ) {
        s = s * resolution + 0;//+sampleBounds.mini[dimension];
        s = clamp(s-xy[dimension], 0.0, 1.0-FLT_EPSILON);
    }
    
    return s;
}

} // pbrt

#endif /* Sobol_h */
