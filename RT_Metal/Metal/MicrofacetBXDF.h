#ifndef MicrofacetBXDF_h
#define MicrofacetBXDF_h

#include "BXDF.hh"
#include "Math.hh"

template <typename DistType, typename FrType>
class MicrofacetReflection {
    
private:
  // MicrofacetReflection Private Data
    const float3 R;
    const FrType fresnel;
    const DistType distribution;
    
public:
    // MicrofacetReflection Public Methods
    MicrofacetReflection(const thread float3 &R, thread FrType &fresnel, thread DistType &distribution)
        : //BxDF(BxDFType(BSDF_REFLECTION | BSDF_GLOSSY)),
        R(R), fresnel(fresnel), distribution(distribution) {}
    
    float3 F(const thread float3 &wo, const thread float3 &wi, const thread float2& uu) const {
        
        float cosThetaO = AbsCosTheta(wo), cosThetaI = AbsCosTheta(wi);
        // Handle degenerate cases for microfacet reflection
        if (cosThetaI == 0 || cosThetaO == 0) return 0;
        
        float3 wh = wi + wo;
        if (wh.x == 0 && wh.y == 0 && wh.z == 0) return 0;
        wh = normalize(wh);
        // For the Fresnel call, make sure that wh is in the same hemisphere
        // as the surface normal, so that TIR is handled correctly.
        float3 F = fresnel.Evaluate(dot(wi, Faceforward(wh, float3(0,0,1))));
        
        return R * distribution.D(wh) * distribution.G(wo, wi) * F /
               (4 * cosThetaI * cosThetaO);
    }
    
    float PDF(const thread float3 &wo, const thread float3 &wi, const thread float2& uu) const {
        if (wo.z * wi.z <= 0) return 0;
        
        float3 wh = normalize(wo + wi);
        return distribution.PDF(wo, wh) / (4 * dot(wo, wh));
    }
    
    float3 S_F(const thread float3 &wo, thread float3 &wi,
               const thread float2 &uu, thread float &pdf) const {
        // Sample microfacet orientation $\wh$ and reflected direction $\wi$
        if (wo.z == 0) return 0.;
    
        float3 wh = distribution.sample_wh(wo, uu);
    
        if (dot(wo, wh) <= 0) return 0.;   // Should be rare
        wi = Reflect(wo, wh);
        //wi = {-wo.x, -wo.y, wo.z };
        if (wo.z * wi.z <= 0) return 0;

        // Compute PDF of _wi_ for microfacet reflection
        pdf = distribution.PDF(wo, wh) / (4 * dot(wo, wh));
        return F(wo, wi, uu);
    }
};

template <typename DistType, typename FrType>
class MicrofacetTransmission {
  private:
    // Private Data
    const float3 T;
    const DistType dist;
    const float etaA, etaB;
    const TransportMode mode;
    const FresnelDielectric fresnel;
    
  public:
    // MicrofacetTransmission Public Methods
    MicrofacetTransmission(const float3 T, thread DistType &dist,
                           float etaA, float etaB, TransportMode mode)
        : //BxDF(BxDFType(BSDF_TRANSMISSION | BSDF_GLOSSY)),
          T(T), dist(dist),
          etaA(etaA), etaB(etaB),
          fresnel(etaA), mode(mode) {}
    
    float3 F(const thread float3 &wo, const thread float3 &wi, const thread float2& uu) const {
        if (wo.z * wi.z > 0) return 0;  // transmission only

        float cosThetaO = CosTheta(wo);
        float cosThetaI = CosTheta(wi);
        if (cosThetaI == 0 || cosThetaO == 0) return 0;

        // Compute $\wh$ from $\wo$ and $\wi$ for microfacet transmission
        float eta = CosTheta(wo) > 0 ? (etaB / etaA) : (etaA / etaB);
        float3 wh = normalize(wo + wi * eta);
        if (wh.z < 0) wh = -wh;

        // Same side?
        if (dot(wo, wh) * dot(wi, wh) > 0) return 0;

        float3 F = fresnel.Evaluate(dot(wo, wh));

        float sqrtDenom = dot(wo, wh) + eta * dot(wi, wh);
        float factor = (mode == TransportMode::Radiance) ? (1 / eta) : 1;

        return (1.0 - F) * T * abs(dist.D(wh) * dist.G(wo, wi) * eta * eta *
                        abs(dot(wi, wh)) * abs(dot(wo, wh)) * factor * factor /
                        (cosThetaI * cosThetaO * sqrtDenom * sqrtDenom));
    }
    
    float PDF(const thread float3 &wo, const thread float3 &wi, const thread float2& uu) const {
        if (wo.z * wi.z > 0) return 0;
        // Compute $\wh$ from $\wo$ and $\wi$ for microfacet transmission
        float eta = CosTheta(wo) > 0 ? (etaB / etaA) : (etaA / etaB);
        float3 wh = normalize(wo + wi * eta);

        if (dot(wo, wh) * dot(wi, wh) > 0) return 0;

        // Compute change of variables _dwh\_dwi_ for microfacet transmission
        float sqrtDenom = dot(wo, wh) + eta * dot(wi, wh);
        float dwh_dwi = abs((eta * eta * dot(wi, wh)) / (sqrtDenom * sqrtDenom));
        return dist.PDF(wo, wh) * dwh_dwi;
    }
    
    float3 S_F(const thread float3 &wo, thread float3 &wi,
               const thread float2 &uu, thread float &pdf) const {
        
        if (wo.z == 0) return 0;
        
        float3 wh = dist.sample_wh(wo, uu);
        if (dot(wo, wh) < 0) return 0;  // Should be rare

        float eta = CosTheta(wo) > 0 ? (etaA / etaB) : (etaB / etaA);
        if (!Refract(wo, wh, eta, wi)) return 0;
        pdf = PDF(wo, wi, uu);
        return F(wo, wi, uu);
    }
};

struct Beckmann {
    
  private:
    // Private Data
    const float alphax, alphay;
    
    // BeckmannDistribution Private Methods
    float Lambda(const thread float3 &w) const {
        float absTanTheta = abs(TanTheta(w));
        if (isinf(absTanTheta)) return 0.;
        
        // Compute _alpha_ for direction _w_
        float alpha = sqrt(Cos2Phi(w) * alphax * alphax + Sin2Phi(w) * alphay * alphay);
        float a = 1 / (alpha * absTanTheta);
        if (a >= 1.6f) return 0;
        return (1 - 1.259f * a + 0.396f * a * a) / (3.535f * a + 2.181f * a * a);
    }
  
public:
    // BeckmannDistribution Public Methods
    static float RoughnessToAlpha(float roughness) {
        roughness = max(roughness, (float)1e-3);
        float x = log(roughness);
        return 1.62142f + 0.819955f * x + 0.1734f * x * x +
               0.0171201f * x * x * x + 0.000640711f * x * x * x * x;
    }
    
    Beckmann(float alphax, float alphay) : alphax(max(0.001, alphax)), alphay(max(0.001, alphay)) {}
    
    float D(const thread float3 &wh) const {
        float tan2Theta = Tan2Theta(wh);
        if (isinf(tan2Theta)) return 0.;
        
        float cos4Theta = Cos2Theta(wh) * Cos2Theta(wh);
        
        return exp(-tan2Theta * (Cos2Phi(wh) / (alphax * alphax) + Sin2Phi(wh) / (alphay * alphay))) /
               (M_PI_F * alphax * alphay * cos4Theta);
    }
    
    float G1(const thread float3 &w) const { return 1 / (1 + Lambda(w)); }
    
    float G(const thread float3 &wo, const thread float3 &wi) const {
        return 1 / (1 + Lambda(wo) + Lambda(wi));
    }
    
    float PDF(const thread float3 &wo, const thread float3& wh) const {
        return D(wh) * G1(wo) * abs(dot(wo, wh)) / AbsCosTheta(wo);
    }
    
    float3 sample_wh(const thread float3 &wo, const thread float2 &u) const {
        
        float3 wh; bool flip = wo.z < 0;
        wh = BeckmannSample(flip ? -wo : wo, alphax, alphay, u[0], u[1]);
        if (flip) wh = -wh;
        return wh;
    }
    
    float3 BeckmannSample(const thread float3 &wi, float alpha_x, float alpha_y,
                                   float U1, float U2) const {
        // 1. stretch wi
        float3 wiStretched = normalize(float3(alpha_x * wi.x, alpha_y * wi.y, wi.z));

        // 2. simulate P22_{wi}(x_slope, y_slope, 1, 1)
        float slope_x, slope_y;
        BeckmannSample11(CosTheta(wiStretched), U1, U2, &slope_x, &slope_y);

        // 3. rotate
        float tmp = CosPhi(wiStretched) * slope_x - SinPhi(wiStretched) * slope_y;
        slope_y = SinPhi(wiStretched) * slope_x + CosPhi(wiStretched) * slope_y;
        slope_x = tmp;

        // 4. unstretch
        slope_x = alpha_x * slope_x;
        slope_y = alpha_y * slope_y;

        // 5. compute normal
        return normalize(float3(-slope_x, -slope_y, 1.f));
    }
    
    // Microfacet Utility Functions
    void BeckmannSample11(float cosThetaI, float U1, float U2, thread float *slope_x, thread float *slope_y) const {
        /* Special case (normal incidence) */
        if (cosThetaI > .9999) {
            float r = sqrt(-log(1.0f - U1));
            float sinPhi = sin(2 * M_PI_F * U2);
            float cosPhi = cos(2 * M_PI_F * U2);
            *slope_x = r * cosPhi;
            *slope_y = r * sinPhi;
            return;
        }

        /* The original inversion routine from the paper contained
           discontinuities, which causes issues for QMC integration
           and techniques like Kelemen-style MLT. The following code
           performs a numerical inversion with better behavior */
        float sinThetaI = sqrt(max(0.0, 1.0 - cosThetaI * cosThetaI));
        float tanThetaI = sinThetaI / cosThetaI;
        float cotThetaI = 1 / tanThetaI;

        /* Search interval -- everything is parameterized
           in the Erf() domain */
        float a = -1, c = Erf(cotThetaI);
        float sample_x = max(U1, (float)1e-6f);

        /* Start with a good initial guess */
        // Float b = (1-sample_x) * a + sample_x * c;

        /* We can do better (inverse of an approximation computed in
         * Mathematica) */
        float thetaI = acos(cosThetaI);
        float fit = 1 + thetaI * (-0.876f + thetaI * (0.4265f - 0.0594f * thetaI));
        float b = c - (1 + c) * pow(1 - sample_x, fit);

        /* Normalization factor for the CDF */
        const float SQRT_PI_INV = 1.f / sqrt(M_PI_F);
        float normalization = 1 / (1 + c + SQRT_PI_INV * tanThetaI * exp(-cotThetaI * cotThetaI));

        int it = 0;
        while (++it < 10) {
            /* Bisection criterion -- the oddly-looking
               Boolean expression are intentional to check
               for NaNs at little additional cost */
            if (!(b >= a && b <= c)) b = 0.5f * (a + c);

            /* Evaluate the CDF and its derivative
               (i.e. the density function) */
            float invErf = ErfInv(b);
            float value = normalization * (1 + b + SQRT_PI_INV * tanThetaI * exp(-invErf * invErf)) - sample_x;
            float derivative = normalization * (1 - invErf * tanThetaI);

            if (abs(value) < 1e-5f) break;

            /* Update bisection intervals */
            if (value > 0)
                c = b;
            else
                a = b;

            b -= value / derivative;
        }

        /* Now convert back into a slope value */
        *slope_x = ErfInv(b);

        /* Simulate Y component */
        *slope_y = ErfInv(2.0f * max(U2, (float)1e-6f) - 1.0f);

//        CHECK(!std::isinf(*slope_x));
//        CHECK(!std::isnan(*slope_x));
//        CHECK(!std::isinf(*slope_y));
//        CHECK(!std::isnan(*slope_y));
    }

};

struct TrowbridgeReitz {
    float alpha_x, alpha_y;
    
    TrowbridgeReitz(){}
    
    static float RoughnessToAlpha(float roughness) {
        return sqrt(roughness);
        
//        roughness = max(roughness, (float)1e-3);
//        float x = log(roughness);
//        return 1.62142f + 0.819955f * x + 0.1734f * x * x +
//        0.0171201f * x * x * x + 0.000640711f * x * x * x * x;
    }
    
    TrowbridgeReitz(float alphax, float alphay) {
        this->alpha_x = max(0.001, alphax);
        this->alpha_y = max(0.001, alphay);
    }
    
    bool EffectivelySmooth() const { return max(alpha_x, alpha_y) < 1e-3f; }
    
    float D(const thread float3& wh) const {
        
        float tan2Theta = Tan2Theta(wh);
        if (isinf(tan2Theta)) return 0.;
        
        const float cos4Theta = Cos2Theta(wh) * Cos2Theta(wh);
        if (cos4Theta < 1e-16f) return 0;
        
        float e = (Cos2Phi(wh) / Sqr(alpha_x)  + Sin2Phi(wh) / Sqr(alpha_y)) * tan2Theta;
        
        return 1 / (M_PI_F * alpha_x * alpha_y * Sqr(1 + e) * cos4Theta);
    }
    
    float D(const thread float3 &w, const thread float3 &wh) const {
        return D(wh) * G1(w) * abs(dot(w, wh) / CosTheta(w));
    }
     
    float G1(const thread float3 &w) const { return 1 / (1 + Lambda(w)); }
    
    float G(const thread float3 &wo, const thread float3 &wi) const {
        return 1 / (1 + Lambda(wo) + Lambda(wi));
    }
        
    float Lambda(const thread float3 &w) const {
        float tan2Theta = Tan2Theta(w);
        if (isinf(tan2Theta)) return 0.;
        // Compute _alpha2_ for direction _w_
        float alpha2 = Sqr(CosPhi(w) * alpha_x) + Sqr(SinPhi(w) * alpha_y);

        return 0.5 * (sqrt(1 + alpha2 * tan2Theta) - 1);
    }
    
    float3 sample_wh(const thread float3 &wo, const thread float2& u) const {
        float3 wh;
        
        bool flip = wo.z < 0;
        wh = TrowbridgeReitzSample(flip ? -wo : wo, alpha_x, alpha_y, u[0], u[1]);
        if (flip) wh = -wh;
        
        return wh;
    }
    
    float PDF(const thread float3 &wo, const thread float3& wh) const {
        return D(wh) * G1(wo) * abs(dot(wo, wh) / CosTheta(wo));
    }
    
    void Regularize() {
        if (alpha_x < 0.3f)
            alpha_x = clamp(2 * alpha_x, 0.1f, 0.3f);
        if (alpha_y < 0.3f)
            alpha_y = clamp(2 * alpha_y, 0.1f, 0.3f);
    }
    
    static float3 TrowbridgeReitzSample(const thread float3 &wi,
                                 float alpha_x, float alpha_y, float U1, float U2) {
        // 1. stretch wi
        float3 wiStretched = normalize(float3(alpha_x * wi.x, alpha_y * wi.y, wi.z));

        // 2. simulate P22_{wi}(x_slope, y_slope, 1, 1)
        float slope_x, slope_y;
        TrowbridgeReitzSample11(CosTheta(wiStretched), U1, U2, &slope_x, &slope_y);

        // 3. rotate
        float tmp = CosPhi(wiStretched) * slope_x - SinPhi(wiStretched) * slope_y;
        slope_y = SinPhi(wiStretched) * slope_x + CosPhi(wiStretched) * slope_y;
        slope_x = tmp;

        // 4. unstretch
        slope_x = alpha_x * slope_x;
        slope_y = alpha_y * slope_y;

        // 5. compute normal
        return normalize(float3(-slope_x, -slope_y, 1.));
    }
    
    static void TrowbridgeReitzSample11(float cosTheta, float U1, float U2,
                                 thread float *slope_x, thread float *slope_y) {
        // special case (normal incidence)
        if (cosTheta > .9999) {
            float r = sqrt(U1 / (1 - U1));
            float phi = 6.28318530718 * U2;
            *slope_x = r * cos(phi);
            *slope_y = r * sin(phi);
            return;
        }

        float sinTheta = sqrt(max(0.0, 1.0 - cosTheta * cosTheta));
        float tanTheta = sinTheta / cosTheta;
        float a = 1 / tanTheta;
        float G1 = 2 / (1 + sqrt(1.f + 1.f / (a * a)));

        // sample slope_x
        float A = 2 * U1 / G1 - 1;
        float tmp = 1.f / (A * A - 1.f);
        if (tmp > 1e10) tmp = 1e10;
        
        float B = tanTheta;
        float D = sqrt(max(float(B * B * tmp * tmp - (A * A - B * B) * tmp), 0.0));
        
        float slope_x_1 = B * tmp - D;
        float slope_x_2 = B * tmp + D;
        
        *slope_x = (A < 0 || slope_x_2 > 1.f / tanTheta) ? slope_x_1 : slope_x_2;

        // sample slope_y
        float S;
        if (U2 > 0.5f) {
            S = 1.f;
            U2 = 2.f * (U2 - .5f);
        } else {
            S = -1.f;
            U2 = 2.f * (.5f - U2);
        }
        float z =
            (U2 * (U2 * (U2 * 0.27385f - 0.73369f) + 0.46341f)) /
            (U2 * (U2 * (U2 * 0.093073f + 0.309420f) - 1.000000f) + 0.597999f);
        *slope_y = S * z * sqrt(1.f + *slope_x * *slope_x);

//        CHECK(!std::isinf(*slope_y));
//        CHECK(!std::isnan(*slope_y));
    }
};

typedef MicrofacetReflection <TrowbridgeReitz, FresnelConductor> MetalMaterial;

inline MetalMaterial createMetalMaterial() {
    
    float2 uv_rough = {0.01, 0.02};
    
    //uv_rough[0] = TrowbridgeReitz::RoughnessToAlpha(0.1);
    //uv_rough[1] = TrowbridgeReitz::RoughnessToAlpha(0.2);
    
    float3 eta = { 0.18, 0.15, 0.81 };
    //float3 eta = float3(0.9);
    float3 k = 1;
    
    auto fr = FresnelConductor(eta, k);
    auto dist = TrowbridgeReitz(uv_rough[0], uv_rough[1]);

    auto mm = MetalMaterial(1.0, fr, dist);
    
    return mm;
}

typedef MicrofacetReflection <Beckmann, FresnelDielectric> PlasticX;

struct PlasticMaterial {
    
    float3 ks = 0.2;
    float3 kd {0.35, 0.12, 0.48};
    
    Lambertian matte;
    MicrofacetReflection <Beckmann, FresnelDielectric> micro;
    
    PlasticMaterial(thread PlasticX &_micro): micro(_micro) {}
    
    float3 F(const thread float3 &wo, const thread float3 &wi, const thread float2& uu)  {
        
        if (uu[0] < 0.5) {
            
            float2 _uu = uu; _uu[0] *= 2;
            return kd * matte.F(wo, wi, _uu);
            
        } else {
            
            float2 _uu = uu; _uu[0] = _uu[0] * 2 - 1;
            return ks * micro.F(wo, wi, uu);
        }
    }
    
    float PDF(const thread float3 &wo, const thread float3 &wi, const thread float2& uu)  {
        
        if (uu[0] < 0.5) {
            
            float2 _uu = uu; _uu[0] *= 2;
            return matte.PDF(wo, wi, uu);
            
        } else {
            
            float2 _uu = uu; _uu[0] = _uu[0] * 2 - 1;
            return micro.PDF(wo, wi, uu);
        }
    }
    
    float3 S_F(const thread float3 &wo, thread float3 &wi, const thread float2 &uu, thread float &pdf)  {
        
        auto _uu_ = uu;
        
        if (_uu_[0] < 0.5) {
            _uu_[0] *= 2;
            return kd * matte.S_F(wo, wi, _uu_, pdf);
            
        } else {
            
            _uu_[0] -= 0.5; _uu_[0] *= 2.0;
            
            return ks * micro.S_F(wo, wi, _uu_, pdf);
        }
    }
};

inline PlasticMaterial createPlasticMaterial() {
    
    float2 uv_rough = {0.01, 0.1};
    
    //uv_rough[0] = TrowbridgeReitz::RoughnessToAlpha(0.1);
    //uv_rough[1] = TrowbridgeReitz::RoughnessToAlpha(0.2);
    
    //float3 eta = { 0.18, 0.15, 0.81 };
    float eta = float(1.5);
    
    auto fr = FresnelDielectric(eta);
    auto dist = Beckmann(uv_rough[0], uv_rough[1]);

    auto px = PlasticX(1.0, fr, dist);
    
    return PlasticMaterial(px);
}

struct GlassMaterial {
    float3 kr = 0.98, kt = 0.98;
    MicrofacetReflection<Beckmann, FresnelDielectric> _mr;
    MicrofacetTransmission<Beckmann, FresnelDielectric> _mt;
    
    float ratio = 0.25;
    
    GlassMaterial(thread FresnelDielectric &fr, thread Beckmann &dist):
        _mr( kr, fr, dist ),
        _mt( kt, dist, 1.0, fr.eta, TransportMode::Importance) {}
    
    float3 F(const thread float3 &wo, const thread float3 &wi, const thread float2 &uu)  {
        
        if (uu[0] < ratio) {
            float2 _uu = uu; _uu[0] = uu[0] / ratio;
            return _mr.F(wo, wi, _uu);
        } else {
            float2 _uu = uu; _uu[0] = (uu[0] - ratio) / (1.0 - ratio);
            return _mt.F(wo, wi, _uu);
        }
    }
    
    float PDF(const thread float3 &wo, const thread float3 &wi, const thread float2 &uu)  {
        if (uu[0] < ratio) {
            float2 _uu = uu; _uu[0] = uu[0] / ratio;
            return ratio * _mr.PDF(wo, wi, _uu);
        } else {
            float2 _uu = uu; _uu[0] = (uu[0] - ratio) / (1.0 - ratio);
            return (1-ratio) * _mt.PDF(wo, wi, _uu);
        }
    }
    
    float3 S_F(const thread float3 &wo, thread float3 &wi, const thread float2 &uu, thread float &pdf)  {
        
        if (uu[0] < ratio) {
            float2 _uu = uu; _uu[0] = uu[0] / ratio;
            return _mr.S_F(wo, wi, _uu, pdf);
        } else {
            float2 _uu = uu; _uu[0] = (uu[0] - ratio) / (1.0 - ratio);
            return _mt.S_F(wo, wi, _uu, pdf);
        }
    }
};

inline GlassMaterial createGlass() {
    
    float eta = 1.5;
    float2 rough {0.01, 0.01};
 
    auto fr = FresnelDielectric(eta);
    auto dist = Beckmann(rough[0], rough[1]);

    auto mm = GlassMaterial(fr, dist);
    return mm;
}

#endif /* MicrofacetBXDF_h */
