#ifndef BXDF_h
#define BXDF_h

#include "Common.hh"
#include "Sampling.hh"

enum BXDF_Type {
    BSDF_REFLECTION   = 1 << 0,
    BSDF_TRANSMISSION = 1 << 1,
    BSDF_DIFFUSE      = 1 << 2,
    BSDF_GLOSSY       = 1 << 3,
    BSDF_SPECULAR     = 1 << 4,
    BSDF_ALL          = BSDF_DIFFUSE | BSDF_GLOSSY | BSDF_SPECULAR |
                        BSDF_REFLECTION | BSDF_TRANSMISSION,
};

struct BXDF_Data {
    const BXDF_Type type;
    const float3 scale;
    
    BXDF_Data(BXDF_Type type, float3 scale): type(type), scale(scale) {}
};

inline float3 Reflect(const thread float3 &wo, const thread float3 &n) {
    return -wo + 2 * dot(wo, n) * n;
}

inline bool Refract(const thread float3 &wo, const thread float3 &n, float eta, thread float3 &wi) {
    // Compute $\cos \theta_\roman{t}$ using Snell's law
    
    float cosThetaI = wo.z;
    float sin2ThetaI = max(0.0, 1.0 - cosThetaI * cosThetaI);
    float sin2ThetaT = eta * eta * sin2ThetaI;

    // Handle total internal reflection for transmission
    if (sin2ThetaT >= 1) return false;
    
    float cosThetaT = sqrt(1 - sin2ThetaT);
    wi = eta * -wo + (eta * cosThetaI - cosThetaT) * n;
    return true;
}

template <typename BxType>
struct BXDF_Wrapped {
    
    const BXDF_Data data;
    
    const BxType bx;
    
    BXDF_Wrapped(BXDF_Data data, BxType bx): data(data), bx(bx) {}
    
    bool MatchesFlags(BXDF_Type t) const {
       return (data.type & t) == data.type;
    }
    
    float3 F(const thread float3 &wo, const thread float3 &wi) {
        return bx.F(wo, wi);
    }
    
    float PDF(const thread float3 &wo, const thread float3 &wi) const {
        return bx.PDF(wi, wo);
    }
    
    float3 S_F(const thread float3 &wo, thread float3 &wi,
                const thread float2 *sample, thread float &pdf,
                    thread BXDF_Type *sampledType = nullptr)  {
        
        return bx.S_F(wo, wi, sample, pdf, sampledType);
    }
};

float3 FrDielectric(float cosi, float eta);
float3 FrConductor(float cosi, const thread float3 &eta, const thread float3 &k);
//inline float FrComplex(float cosi, float eta);

class FresnelConductor {
  public:
    // FresnelConductor Public Methods
    float3 Evaluate(float cosThetaI) const {
        return FrConductor(abs(cosThetaI), eta, k);
    }
    
    FresnelConductor(const thread float3& eta, const thread float3& k)
        : eta(eta), k(k) {}
    
  //private:
    float3 eta, k;
};

class FresnelDielectric {
  public:
    // FresnelDielectric Public Methods
    float3 Evaluate(float cosThetaI) const {
        return FrDielectric(cosThetaI, eta);
    }
    
    FresnelDielectric(float eta) : eta(eta) {}

  //private:
    float eta;
};

template <typename DistType>
class FresnelBlend {
    
private:
  // FresnelBlend Private Data
  const float3 Rd, Rs;
  thread DistType *dist;
    
public:
    // FresnelBlend Public Methods
    FresnelBlend(const thread float3 &Rd, const thread float3 &Rs, thread DistType* dist);
    
    float3 SchlickFresnel(float cosTheta) const {
        
        return Rs + pow(1 - cosTheta, 5) * (float3(1.) - Rs);
    }

    float3 F(const thread float3 &wo, const thread float3 &wi) const {
        //auto pow5 = [](Float v) { return (v * v) * (v * v) * v; };
        float3 diffuse = (28.f / (23.f * M_PI_F)) * Rd * (1.f - Rs) *
                            (1 - pow(1 - .5f * AbsCosTheta(wi), 5)) *
                            (1 - pow(1 - .5f * AbsCosTheta(wo), 5));
        
        float3 wh = wi + wo;
        
        if (wh.x == 0 && wh.y == 0 && wh.z == 0) return 0;
        
        wh = normalize(wh);
        
        float3 specular = dist->D(wh) /
            (4 * abs(dot(wi, wh)) * max(AbsCosTheta(wi), AbsCosTheta(wo))) * SchlickFresnel(dot(wi, wh));
        return diffuse + specular;
    }

    float PDF(const thread float3 &wo, const thread float3 &wi) const {
        if (wi.z * wo.z <= 0 ) return 0;
        
        float3 wh = normalize(wo + wi);
        float pdf_wh = dist->PDF(wo, wh);
        return .5f * (AbsCosTheta(wi) / M_PI_F + pdf_wh / (4 * dot(wo, wh)));
    }

    float3 S_F(const thread float3 &wo, thread float3 &wi,
               const thread float2 &uu, thread float *pdf) const {
        float2 u = uu;
        if (u[0] < .5) {
            u[0] = min(2 * u[0], 1.0-FLT_EPSILON);
            // Cosine-sample the hemisphere, flipping the direction if necessary
            wi = CosineSampleHemisphere(u);
            if (wo.z < 0) wi.z *= -1;
        } else {
            u[0] = min(2 * (u[0] - .5f), 1.0-FLT_EPSILON);
            // Sample microfacet orientation $\wh$ and reflected direction $\wi$
            float3 wh = dist->Sample_wh(wo, u);
            wi = Reflect(wo, wh);
            if (wo.z * wi.z <= 0) return 0;
        }
        *pdf = PDF(wo, wi);
        return F(wo, wi);
    }
};


#endif /* BXDF_h */
