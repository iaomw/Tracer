#ifndef MatteBXDF_h
#define MatteBXDF_h

#include "BXDF.hh"

struct Lambertian {
    
    float F(const thread float3& wo, const thread float3& wi) {
        return wi.z / M_PI_F;
    }
    
    float PDF(const thread float3& wo, const thread float3& wi) {
        return wo.z * wi.z > 0 ? abs(wi.z) / M_PI_F : 0;
    }
    
    float S_F(const thread float3& wo, thread float3& wi, const thread float2& uu, thread float& pdf) {
        wi = CosineSampleHemisphere(uu);
        pdf = PDF(wo, wi);
        
        return wi.z / M_PI_F;
    }
    
    static void test() {}
};

struct OrenNayar {
    
    BXDF_Type bxType = BXDF_Type(BSDF_TRANSMISSION | BSDF_DIFFUSE);
    
    float A, B;
    
    OrenNayar(float sigma) {
        sigma = Radians(sigma);
        auto sigma2 = sigma * sigma;
        A = 1.f - (sigma2 / (2.f * (sigma2 + 0.33f)));
        B = 0.45f * sigma2 / (sigma2 + 0.09f);
    }
    
    float F(const thread float3 &wo, const thread float3 &wi) {
        
        float sinThetaI = SinTheta(wi);
        float sinThetaO = SinTheta(wo);
        // Compute cosine term of Oren-Nayar model
        float maxCos = 0;
        if (sinThetaI > 1e-4 && sinThetaO > 1e-4) {
            float sinPhiI = SinPhi(wi), cosPhiI = CosPhi(wi);
            float sinPhiO = SinPhi(wo), cosPhiO = CosPhi(wo);
            float dCos = cosPhiI * cosPhiO + sinPhiI * sinPhiO;
            maxCos = max(0.0, dCos);
        }

        // Compute sine and tangent terms of Oren-Nayar model
        float sinAlpha, tanBeta;
        if (AbsCosTheta(wi) > AbsCosTheta(wo)) {
            sinAlpha = sinThetaO;
            tanBeta = sinThetaI / AbsCosTheta(wi);
        } else {
            sinAlpha = sinThetaI;
            tanBeta = sinThetaO / AbsCosTheta(wo);
        }
        
        return (1.0 / M_PI_F) * (A + B * maxCos * sinAlpha * tanBeta);
    }
    
    float PDF(const thread float3& wo, const thread float3& wi) {
        return wo.z * wi.z > 0 ? abs(wi.z) / M_PI_F : 0;
    }
    
    float S_F(const thread float3& wo, thread float3& wi, const thread float2& uu, thread float& pdf) {
        wi = CosineSampleHemisphere(uu);
        pdf = PDF(wo, wi);
        
        return F(wo, wi);
    }
};

#endif /* MatteBXDF_h */
