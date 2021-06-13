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

struct BXDF_Data {
    const BXDF_Type type;
    const float3 scale;
    
    BXDF_Data(BXDF_Type type, float3 scale): type(type), scale(scale) {}
};

enum struct TransportMode {
    Radiance, Importance
};

// BSDF Inline Functions
inline float CosTheta(const thread float3 &w) { return w.z; }
inline float Cos2Theta(const thread thread float3 &w) { return w.z * w.z; }
inline float AbsCosTheta(const thread float3 &w) { return abs(w.z); }
inline float Sin2Theta(const thread float3 &w) { return max(0.0, 1.0 - Cos2Theta(w)); }

inline float SinTheta(const thread float3 &w) { return sqrt(Sin2Theta(w)); }

//inline float TanTheta(const thread float3 &w) { return SinTheta(w) / CosTheta(w); }

//inline float Tan2Theta(const thread float3 &w) { return Sin2Theta(w) / Cos2Theta(w); }

inline float TanTheta(const thread float3& vec)
{
    float temp = 1 - vec.z * vec.z;
    if (temp <= 0.0f || vec.z == 0.0f)
        return 0.0f;
    return sqrt(temp) / vec.z;
}

inline float Tan2Theta(const thread float3& vec)
{
    float zz = vec.z * vec.z;
    float temp = 1 - zz;
    if (temp <= 0.0f)
        return 0.0f;
    return temp / zz;
}

inline float CosPhi(const thread float3 &w) {
    float sinTheta = SinTheta(w);
    return (sinTheta == 0) ? 1 : clamp(w.x / sinTheta, -1.0, 1.0);
}

inline float SinPhi(const thread float3 &w) {
    float sinTheta = SinTheta(w);
    return (sinTheta == 0) ? 0 : clamp(w.y / sinTheta, -1.0, 1.0);
}

inline float Cos2Phi(const thread float3 &w) {
    auto r = CosPhi(w);
    return r * r;
}

inline float Sin2Phi(const thread float3 &w) {
    auto r = SinPhi(w);
    return r * r;
}

inline float Sqr(float v) {return v * v; }

inline float CosDPhi(thread float3 &wa, thread float3 &wb) {
    return clamp((wa.x * wb.x + wa.y * wb.y) /
                    sqrt((wa.x * wa.x + wa.y * wa.y) *
                         (wb.x * wb.x + wb.y * wb.y)), -1.0, 1.0);
}

template <typename BxType>
struct BXDF_Wrapped {
    
    const BXDF_Data data;
    
    const BxType bx;
    
    BXDF_Wrapped(BXDF_Data data, BxType bx): data(data), bx(bx) {}
    
    bool MatchesFlags(BXDF_Type t) const {
       return (data.type & t) == data.type;
    }
    
    float3 f(const thread float3 &wo, const thread float3 &wi) {
        return bx.f(wo, wi);
    }
    
    float3 sample_f(const thread float3 &wo, thread float3 &wi,
                    const thread float2 *sample, thread float &pdf,
                    thread BXDF_Type *sampledType = nullptr)  {
        
        return bx.sample_f(wo, wi, sample, pdf, sampledType);
    }
    
    float PDF(const thread float3 &wo, const thread float3 &wi) const {
        return bx.PDF(wi, wo);
    }
    
    float3 rho(const thread float3 &wo, int nSamples, const thread float2 *samples) const;
    float3 rho(int nSamples, const thread float2 *samples1, const thread float2 *samples2) const;
};

template <typename TypeDist, typename TypeFr>
struct ConductorBXDF {
    TypeDist dist;
    TypeFr fr;
    
    ConductorBXDF(TypeDist dist, TypeFr fr): dist(dist), fr(fr) {}
    
    float3 sample_f(const thread float3 &wo, thread float3& wi,
                                            const thread float2* u, thread float& pdf,
                                            thread BXDF_Type *sampledType) const {
        
        //if (!(sampleFlags & BxDFReflTransFlags::Reflection)) return 0;
        
        if (dist.EffectivelySmooth()) {
            wi = float3(-wo.x, -wo.y, wo.z); pdf = 1;
            auto F = fr.Evaluate(abs(wi.z)) / abs(wi);
            return F;
        }
        
        if (wo.z == 0) return 0;
        
        float3 wh = dist.Sample_wm(wo, *u);
        
        if (dot(wo, wh) < 0) return 0;   // Should be rare
        wi = reflect(-wo, wh);
        
        if (wi.z == 0) return 0;
        
        if ( wo.z * wi.z <= 0 || dot(wo, wh) <=0 ) return 0;

        // Compute PDF of _wi_ for microfacet reflection
        pdf = dist.PDF(wo, wh) / (4 * dot(wo, wh));
        
        // Evaluate Fresnel factor _F_ for conductor BRDF
        float frCosTheta_i = abs(dot(wi, wh));
        auto F = fr.Evaluate(frCosTheta_i);

        return dist.D(wh) * dist.G(wo, wi) * F / (4 * wi.z * wo.z);
    }
    
    float3 f(const thread float3 &wo, const thread float3 &wi) const {
    
        if (wo.z * wi.z <= 0) return 0;
        if (dist.EffectivelySmooth()) return 0;
        
        float cosThetaO = AbsCosTheta(wo), cosThetaI = AbsCosTheta(wi);
        float3 wh = wi + wo;
        //<<Handle degenerate cases for microfacet reflection>>
           if (cosThetaI == 0 || cosThetaO == 0) return float3(0.);
           if (wh.x == 0 && wh.y == 0 && wh.z == 0) return float3(0.);

        wh = normalize(wh);
        float3 F = fr.Evaluate(dot(wi, wh));
        
        return dist.D(wh) * dist.G(wo, wi) * F / (4 * cosThetaI * cosThetaO);
    }
    
    float PDF(const thread float3 &wo, const thread float3 &wi) const {
        
        //if (!(sampleFlags & BxDFReflTransFlags::Reflection)) return 0;
        
        if (wo.z * wi.z <= 0) return 0;
        if (dist.EffectivelySmooth()) return 0;
        
        float3 wh = normalize(wo + wi);
        
        if (length(wh) <=0 || dot(wo, wh) <= 0) return 0;
        
        return dist.PDF(wo, wh) / (4 * dot(wo, wh));
    }
};

struct TrowbridgeReitzDistribution {
    float alpha_x, alpha_y;
    
    TrowbridgeReitzDistribution(){}
    
    static float RoughnessToAlpha(float roughness) {
        
        return sqrt(roughness);
        
//        roughness = max(roughness, (float)1e-3);
//        float x = log(roughness);
//        return 1.62142f + 0.819955f * x + 0.1734f * x * x +
//               0.0171201f * x * x * x + 0.000640711f * x * x * x * x;
    }
    
    TrowbridgeReitzDistribution(float alphax, float alphay)
    {
        this->alpha_x = max(0.001, alphax);
        this->alpha_y = max(0.001, alphay);
    }
    
    bool EffectivelySmooth() const { return min(alpha_x, alpha_y) < 1e-3f; }
    
    float D(const thread float3& wm) const {
        
        float tan2Theta = Tan2Theta(wm);
        if (isinf(tan2Theta)) return 0.;
        const float cos4Theta = Cos2Theta(wm) * Cos2Theta(wm);
        
        if (cos4Theta < 1e-16f)
            return 0;
        
        float e = (Cos2Phi(wm) / Sqr(alpha_x)  + Sin2Phi(wm) / Sqr(alpha_y)) * tan2Theta;
    
        return 1 / (M_PI_F * alpha_x * alpha_y * cos4Theta * (1 + e) * (1 + e));
    }
    
    float D(const thread float3 &w, const thread float3 &wm) const {
        return D(wm) * G1(w) * max(0.0, dot(w, wm)) / AbsCosTheta(w);
    }
     
    float G1(const thread float3 &w) const { return 1 / (1 + Lambda(w)); }
    
    float G(const thread float3 &wo, const thread float3 &wi) const {
        return 1 / (1 + Lambda(wo) + Lambda(wi));
    }
        
    float Lambda(const thread float3 &w) const {
        float tan2Theta = Tan2Theta(w);
        if (isinf(tan2Theta))
            return 0.;
        // Compute _alpha2_ for direction _w_
        float alpha2 = Sqr(CosPhi(w) * alpha_x) + Sqr(SinPhi(w) * alpha_y);

        return (-1 + Sqr(1 + alpha2 * tan2Theta)) / 2;
    }

    float3 Sample_wm(float2 u) const {
        return SampleTrowbridgeReitz(alpha_x, alpha_y, u);
    }
    
    float3 Sample_wm(const thread float3& wo, const thread float2& u) const {
        bool flip = wo.z < 0;
        float3 wm =
            SampleTrowbridgeReitzVisibleArea(flip ? -wo : wo, alpha_x, alpha_y, u);
        return flip ? -wm : wm;
    }
    
    float PDF(float3 wo, float3 wm) const {
        return D(wm) * G1(wo) * abs(dot(wo, wm)) / AbsCosTheta(wo);
    }
    
    void Regularize() {
        if (alpha_x < 0.3f)
            alpha_x = clamp(2 * alpha_x, 0.1f, 0.3f);
        if (alpha_y < 0.3f)
            alpha_y = clamp(2 * alpha_y, 0.1f, 0.3f);
    }
    
    inline float3 SphericalDirection(float sinTheta, float cosTheta, float phi) const {
        
        //DCHECK(sinTheta >= -1.0001 && sinTheta <= 1.0001);
        //DCHECK(cosTheta >= -1.0001 && cosTheta <= 1.0001);
        
        return float3(clamp(sinTheta, -1.0, 1.0) * cos(phi),
                        clamp(sinTheta, -1.0, 1.0) * sin(phi),
                            clamp(cosTheta, -1.0, 1.0));
    }
    
    inline float3 SampleTrowbridgeReitz(float alpha_x, float alpha_y, float2 u) const {
        float cosTheta, phi;
        if (alpha_x == alpha_y) {
            // Sample $\cos \theta$ for isotropic Trowbridge--Reitz distribution
            float tanTheta2 = alpha_x * alpha_x * u[0] / (1 - u[0]);
            cosTheta = 1 / sqrt(1 + tanTheta2);
            phi = 2 * M_PI_F * u[1];

        } else {
            // Sample $\cos \theta$ for anisotropic Trowbridge--Reitz distribution
            phi = atan(alpha_y / alpha_x * tan(2 * M_PI_F * u[1] + .5f * M_PI_F));
            if (u[1] > .5f)
                phi += M_PI_F;
            float sinPhi = sin(phi), cosPhi = cos(phi);
            float alpha2 = 1 / (Sqr(cosPhi / alpha_x) + Sqr(sinPhi / alpha_y));
            float tanTheta2 = alpha2 * u[0] / (1 - u[0]);
            cosTheta = 1 / sqrt(1 + tanTheta2);
        }
        float sinTheta = sqrt(max(FLT_EPSILON, 1 - Sqr(cosTheta)));
        return SphericalDirection(sinTheta, cosTheta, phi);
    }
    
    // Via Eric Heitz's jcgt sample code...
    inline float3 SampleTrowbridgeReitzVisibleArea(float3 w, float alpha_x, float alpha_y, float2 u) const {
        // Transform _w_ to hemispherical configuration for visible area sampling
        float3 wh = normalize(float3(alpha_x * w.x, alpha_y * w.y, w.z));

        // Find orthonormal basis for visible area microfacet sampling
        float3 T1 =
            (wh.z < 0.99999f) ? normalize(cross(float3(0, 0, 1), wh)) : float3(1, 0, 0);
        float3 T2 = cross(wh, T1);

        // Sample parameterization of projected microfacet area
        float r = sqrt(u[0]);
        float phi = 2 * M_PI_F * u[1];
        float t1 = r * cos(phi), t2 = r * sin(phi);
        float s = 0.5f * (1 + wh.z);
        t2 = (1 - s) * sqrt(1 - t1 * t1) + s * t2;

        // Reproject to hemisphere and transform normal to ellipsoid configuration
        float3 nh =
            t1 * T1 + t2 * T2 + sqrt(max(0.0, 1.0 - t1 * t1 - t2 * t2)) * wh;
        //CHECK_RARE(1e-5f, nh.z == 0);
        return normalize(float3(alpha_x * nh.x, alpha_y * nh.y, max(1e-6f, nh.z)));
    }
};

template <typename FrType>
struct SpecularReflection {
    FrType fr;
    
    BXDF_Type bxType = BXDF_Type(BSDF_REFLECTION | BSDF_SPECULAR);
    
    SpecularReflection(const thread FrType& fr): fr(fr) {}
    
    float3 f(const thread float3 &wo, const thread float3 &wi) { return float3(0); }
    
    float pdf(const thread float3 &wo, const thread float3 &wi) { return 0; }
    
    float3 sample_f(const thread float3 &wo, thread float3 &wi,
                    const thread float2 *sample, thread float &pdf,
                    thread BXDF_Type *sampledType = nullptr) const {
        
        wi = float3(-wo.x, -wo.y, wo.z); pdf = 1;
        
        return fr.Evaluate( wi.z) / abs(wi.z);
    }
};

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

template <typename FrType>
struct SpecularTransmission {
    FrType fr;
    TransportMode mode = TransportMode::Radiance;
    
    BXDF_Type bxType = BXDF_Type(BSDF_TRANSMISSION | BSDF_SPECULAR);
    
    SpecularTransmission(const thread FrType& fr): fr(fr) {}
    
    float3 f(const thread float3 &wo, const thread float3 &wi) { return float3(0); }
    
    float pdf(const thread float3 &wo, const thread float3 &wi) { return 0; }
    
    float3 sample_f(const thread float3 &wo, thread float3 &wi,
                    const thread float2 *sample, thread float &pdf,
                    thread BXDF_Type *sampledType = nullptr) const {
        
        //<<Figure out which  is incident and which is transmitted>>
        bool entering = wo.z > 0;
        float etaI = entering ? fr.etaI : fr.etaT;
        float etaT = entering ? fr.etaT : fr.etaI;
        
        auto n = float3(0, 0, 1) * wo.z / abs(wo.z); // forward normal
        
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

struct OrenNayar {
    float A, B;
    
    BXDF_Type bxType = BXDF_Type(BSDF_TRANSMISSION | BSDF_DIFFUSE);
    
    OrenNayar(float sigma) {
        //Radians(sigma);
        auto sigma2 = sigma * sigma;
        A = 1.f - (sigma2 / (2.f * (sigma2 + 0.33f)));
        B = 0.45f * sigma2 / (sigma2 + 0.09f);
    }
    
    float3 f(const thread float3 &wo, const thread float3 &wi) {
        
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
};

float FrDielectric(float cosi, float eta);
float FrConductor(float cosi, float eta, const float k);
//inline float FrComplex(float cosi, float eta);

class FresnelConductor {
  public:
    // FresnelConductor Public Methods
    float3 Evaluate(float cosThetaI) const {
        auto eta = etaI / etaT; // need test later
        return FrConductor(abs(cosThetaI), eta, k);
    }
    
    FresnelConductor(const float etaI, const float etaT, const float k)
        : etaI(etaI), etaT(etaT), k(k) {}
    
  //private:
    float etaI, etaT, k;
};

class FresnelDielectric {
  public:
    // FresnelDielectric Public Methods
    float3 Evaluate(float cosThetaI) const {
        return FrDielectric(cosThetaI, etaI/etaT);
    }
    
    FresnelDielectric(float etaI, float etaT) : etaI(etaI), etaT(etaT) {}

  //private:
    float etaI, etaT;
};


#ifdef __METAL_VERSION__

static float2 ImportanceSampleGGX_VisibleNormal_Unit(float thetaI, float u1, float u2)
{
    float2 Slope;

    // Special case (normal incidence)
    if (thetaI < 1e-4f)
    {
        float SinPhi, CosPhi;
        float R = sqrt(max(u1 / ((1 - u1) + 1e-6f), 0.0f));
        
        auto Phi = 2 * M_PI_F * u2;
        SinPhi = sin(Phi);
        CosPhi = cos(Phi);
        
        return float2(R * CosPhi, R * SinPhi);
    }

    // Precomputations
    float TanThetaI = tan(thetaI); float a = 1 / TanThetaI;
    float G1 = 2.0f / (1.0f + sqrt(max(1.0f + 1.0f / (a*a), 0.0f)));

    // Simulate X component
    float A = 2.0f * u1 / G1 - 1.0f;
    if (abs(A) == 1)
        A -= (A >= 0.0f ? 1.0f : -1.0f) * 1e-4f;

    float Temp = 1.0f / (A*A - 1.0f);
    float B = TanThetaI;
    float D = sqrt(max(B*B*Temp*Temp - (A*A - B*B) * Temp, 0.0f));
    float Slope_x_1 = B * Temp - D;
    float Slope_x_2 = B * Temp + D;
    Slope.x = (A < 0.0f || Slope_x_2 > 1.0f / TanThetaI) ? Slope_x_1 : Slope_x_2;

    // Simulate Y component
    float S;
    if (u2 > 0.5f)
    {
        S = 1.0f;
        u2 = 2.0f * (u2 - 0.5f);
    }
    else
    {
        S = -1.0f;
        u2 = 2.0f * (0.5f - u2);
    }

    // Improved fit
    float z =
        (u2 * (u2 * (u2 * (-0.365728915865723) + 0.790235037209296) -
        0.424965825137544) + 0.000152998850436920) /
        (u2 * (u2 * (u2 * (u2 * 0.169507819808272 - 0.397203533833404) -
        0.232500544458471) + 1) - 0.539825872510702);

    Slope.y = S * z * sqrt(1.0f + Slope.x * Slope.x);

    return Slope;
}

static float GGX_D(const thread float3& wh, float alpha)
{
    if (wh.z <= 0.0f) return 0.0f;

    const float tan2Theta = Tan2Theta(wh), cosTheta2 = wh.z * wh.z;

    const float root = alpha / (cosTheta2 * (alpha * alpha + tan2Theta));

    return float(1.0/M_PI_F) * (root * root);
}

static float SmithG(const thread float3& v, const thread float3& wh, float alpha)
{
    const float tanTheta = abs(TanTheta(v));

    if (tanTheta == 0.0f)
        return 1.0f;

    if (dot(v, wh) * v.z <= 0)
        return 0.0f;

    const float root = alpha * tanTheta;
    return 2.0f / (1.0f + sqrt(1.0f + root*root));
}

static float GGX_G(const thread float3& wo, const thread float3& wi, const thread float3& wh, float alpha)
{
    return SmithG(wo, wh, alpha) * SmithG(wi, wh, alpha);
}

static float GGX_Pdf_VisibleNormal(const thread float3& wi, const thread float3& H, float Alpha)
{
    float D = GGX_D(H, Alpha);

    return SmithG(wi, H, Alpha) * abs(dot(wi, H)) * D / (abs(wi.z) + 1e-4f);
}

static float3 GGX_SampleVisibleNormal(const thread float3& _wi, float u1, float u2, thread float* pPdf, float rough)
{
    // Stretch wi
    float3 wi = normalize( { rough * _wi.x, rough * _wi.y, _wi.z } );

    // Get polar coordinates
    float Theta = 0, Phi = 0;
    if (wi.z < float(0.99999f))
    {
        Theta = acos(wi.z);
        Phi = atan2(wi.y, wi.x);
    }
    float SinPhi = sin(Phi);
    float CosPhi = cos(Phi);

    // Simulate P22_{wi}(Slope.x, Slope.y, 1, 1)
    float2 Slope = ImportanceSampleGGX_VisibleNormal_Unit(Theta, u1, u2);

    // Step 3: rotate
    Slope = float2(
        CosPhi * Slope.x - SinPhi * Slope.y,
        SinPhi * Slope.x + CosPhi * Slope.y);

    // Unstretch
    Slope *= rough;

    // Compute normal
    float Normalization = 1.0 / sqrt(Slope.x*Slope.x + Slope.y*Slope.y + 1.0);

    float3 H = float3 ( -Slope.x * Normalization, -Slope.y * Normalization, Normalization );

    //*pPdf = GGX_Pdf_VisibleNormal(_wi, H, roughness);

    return H;
}

#else

#endif

#endif /* BXDF_h */
