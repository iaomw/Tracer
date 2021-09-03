#ifndef SpecularBXDF_h
#define SpecularBXDF_h

#include "BXDF.hh"

template <typename FrType>
struct SpecularReflection {
    FrType fr;
    
    BXDF_Type bxType = BXDF_Type(BSDF_REFLECTION | BSDF_SPECULAR);
    
    SpecularReflection(const thread FrType& fr): fr(fr) {}
    
    float3 F(const thread float3 &wo, const thread float3 &wi) { return float3(0); }
    
    float PDF(const thread float3 &wo, const thread float3 &wi) { return 0; }
    
    float3 S_F(const thread float3 &wo, thread float3 &wi,
                const thread float2 *sample, thread float &pdf,
                    thread BXDF_Type *sampledType = nullptr) const {
        
        wi = float3(-wo.x, -wo.y, wo.z); pdf = 1;
        
        return fr.Evaluate(wi.z) / abs(wi.z);
    }
};

template <typename FrType>
struct SpecularTransmission {
    FrType fr;
    TransportMode mode = TransportMode::Radiance;
    
    BXDF_Type bxType = BXDF_Type(BSDF_TRANSMISSION | BSDF_SPECULAR);
    
    SpecularTransmission(const thread FrType& fr): fr(fr) {}
    
    float3 f(const thread float3 &wo, const thread float3 &wi) { return 0; }
    
    float pdf(const thread float3 &wo, const thread float3 &wi) { return 0; }
    
    float3 sample_f(const thread float3 &wo, thread float3 &wi,
                    const thread float2 *sample, thread float &pdf,
                    thread BXDF_Type *sampledType = nullptr) const {
        
        //<<Figure out which  is incident and which is transmitted>>
        bool entering = wo.z > 0;

        //if (!entering) { eta = 1.0/ eta; }
        
        float etaI = entering ? 1.0 : fr.eta;
        float etaT = entering ? fr.eta : 1.0;
        
        auto n = float3(0, 0, 1);// * wo.z / abs(wo.z); // forward normal
        
        //<<Compute ray direction for specular transmission>>
       if (!Refract(wo, n, etaI / etaT, wi))
           return 0;

        pdf = 1;
        
        float3 ft = float3(1.0) - fr.Evaluate(wi.z);
        //<<Account for non-symmetry with transmission to different medium>>
        if (mode == TransportMode::Radiance)
            ft *= (etaI * etaI) / (etaT * etaT);

        return ft / abs(wi.z);
    }
};


#endif /* SpecularBXDF_h */
