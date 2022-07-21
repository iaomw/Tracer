#ifndef HitRecord_h
#define HitRecord_h

#include "Ray.hh"
#include "Sampling.hh"

#ifdef __METAL_VERSION__

struct HitRecord {

    float t;
    float3 p;
    
    bool f;
    float3 gn;
    float3 sn;
   
    float2 uv;
    uint material;
    
    float PDF;
    
    Ray _r; float _t;
    float4x4 modelMatrix;
    
    void checkFace(const thread Ray& ray) {
        f = dot(ray.direction, gn) <= 0 ;
        sn = f? gn:-gn;
    }
};

struct BxRecord {
    float3 attenuation;
    float bxPDF = 1.0;
};

class HenyeyGreenstein {
public:
    float g;
    HenyeyGreenstein(float g) : g(g) {}
    
    float p(const thread float3 &wo, const thread float3 &wi) const;
    float Sample_p(const thread float3 &wo, thread float3 &wi, const thread float2 &uu) const;
};

// Media Inline Functions
inline float PhaseHG(float cosTheta, float g) {
    float gg = g * g;
    float denom = 1 + gg + 2 * g * cosTheta;
    return (0.25 / M_PI_F) * (1 - gg) / (denom * sqrt(denom));
}

// HenyeyGreenstein Method Definitions
inline float HenyeyGreenstein::p(const thread float3 &wo, const thread float3 &wi) const {
    //ProfilePhase _(Prof::PhaseFuncEvaluation);
    return PhaseHG(dot(wo, wi), g);
}

inline float HenyeyGreenstein::Sample_p(const thread float3 &wo, thread float3 &wi, const thread float2 &uu) const {
    // Compute $\cos \theta$ for Henyey--Greenstein sample
    float cosTheta;
    if (abs(g) < 1e-3)
        cosTheta = 1 - 2 * uu[0];
    else {
        float gg = g * g;
        float sqrTerm = (1 - gg) / (1 + g - 2 * g * uu[0]);
        cosTheta = -(1 + gg - sqrTerm * sqrTerm) / (2 * g);
    }

    // Compute direction _wi_ for Henyey--Greenstein sample
    float sinTheta = sqrt(max(0.0, 1 - cosTheta * cosTheta));
    float phi = 2 * M_PI_F * uu[1];
    
    float3 v1, v2;
    CoordinateSystem(wo, v1, v2);
    
    wi = SphericalDirection(sinTheta, cosTheta, phi, v1, v2, wo);
    return PhaseHG(cosTheta, g);
}

#endif

#endif /* HitRecord_h */
