#ifndef RandomSampler_h
#define RandomSampler_h

#include "Random.hh"

struct RandomSampler {
    thread pcg32_t* rng;
    
    float random() {
        return randomF(rng);
    }
    
    inline float sample1D() {
        return randomF(rng);
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
        float2 p;
        do {
            p = 2.0 * float2(sample1D(), sample1D()) - float2(1,1);
        } while (dot(p, p)>=1.0);
        return normalize(p);
    }

    float3 sampleUnitInSphere() {
        float3 p;
        do {
            auto x = sample1D();
            auto y = sample1D();
            auto z = sample1D();
            p = 2.0 * float3(x, y, z) - float3(1);
        } while (dot(p, p)>=1.0);
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

};

#endif /* RandomSampler_h */
