#ifndef BXDF_h
#define BXDF_h

#include "Common.hh"

enum BXDF_Type {
    BSDF_REFLECTION   = 1 << 0,
    BSDF_TRANSMISSION = 1 << 1,
    BSDF_DIFFUSE      = 1 << 2,
    BSDF_GLOSSY       = 1 << 3,
    BSDF_SPECULAR     = 1 << 4,
    BSDF_ALL          = BSDF_DIFFUSE | BSDF_GLOSSY | BSDF_SPECULAR |
                        BSDF_REFLECTION | BSDF_TRANSMISSION,
};

#ifdef __METAL_VERSION__

float FrDielectric(float cosThetaI, float etaI, float etaT);

#else

class BXDF {
    const BXDF_Type type;
    
        ~BXDF() { }
       BXDF(BXDF_Type type) : type(type) { }
    
       bool MatchesFlags(BXDF_Type t) const {
           return (type & t) == type;
       }
    
    float3 f(const float3 &wo, const float3 &wi);
    
    float3 Sample_f(const float3 &wo, float3 *wi,
                    const float2 &sample, float *pdf,
                    BXDF_Type *sampledType = nullptr) const;
    
    float Pdf(const float3 &wi, const float3 &wo) const;
};

#endif

#endif /* BXDF_h */
