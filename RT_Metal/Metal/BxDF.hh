#ifndef BxDF_h
#define BxDF_h

#include "Common.hh"

enum BxDF_Type {
    BSDF_REFLECTION   = 1 << 0,
    BSDF_TRANSMISSION = 1 << 1,
    BSDF_DIFFUSE      = 1 << 2,
    BSDF_GLOSSY       = 1 << 3,
    BSDF_SPECULAR     = 1 << 4,
    BSDF_ALL          = BSDF_DIFFUSE | BSDF_GLOSSY | BSDF_SPECULAR |
                        BSDF_REFLECTION | BSDF_TRANSMISSION,
};

#ifdef __METAL_VERSION__

static float FrDielectric(float cosThetaI, float etaI, float etaT) {
    cosThetaI = clamp(cosThetaI, -1.0, 1.0);
    //<<Potentially swap indices of refraction>>
       bool entering = cosThetaI > 0.f;
       if (!entering) {
           auto tmp = etaI;
           etaI = etaT; etaT = tmp;
           cosThetaI = abs(cosThetaI);
       }

    //<<Compute cosThetaT using Snell’s law>>
    float sinThetaI = sqrt(max(FLT_EPSILON, 1 - cosThetaI * cosThetaI));
    float sinThetaT = etaI / etaT * sinThetaI;
       //<<Handle total internal reflection>>
          if (sinThetaT >= 1)
              return 1;

    float cosThetaT = sqrt(max(FLT_EPSILON, 1 - sinThetaT * sinThetaT));

    float Rparl = ((etaT * cosThetaI) - (etaI * cosThetaT)) /
                  ((etaT * cosThetaI) + (etaI * cosThetaT));
    float Rperp = ((etaI * cosThetaI) - (etaT * cosThetaT)) /
                  ((etaI * cosThetaI) + (etaT * cosThetaT));
    return (Rparl * Rparl + Rperp * Rperp) / 2;
}

static float FrConductor(float cosThetaI, const thread float &etaI,
                         const thread float &etaT, const thread float3 &k) {
    
}

#else

class BxDF {
    const BxDF_Type type;
    
        ~BxDF() { }
       BxDF(BxDF_Type type) : type(type) { }
    
       bool MatchesFlags(BxDF_Type t) const {
           return (type & t) == type;
       }
    
    float3 f(const float3 &wo, const float3 &wi);
    
    float3 Sample_f(const float3 &wo, float3 *wi,
                    const float2 &sample, float *pdf,
                    BxDF_Type *sampledType = nullptr) const;
    
    float Pdf(const float3 &wi, const float3 &wo) const;
};

#endif

#endif /* BxDF_h */
