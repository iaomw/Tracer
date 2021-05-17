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
float FrConductor(float cosi, const float eta, const float k);

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

inline float TanTheta(const thread float3& vec)
{
    float temp = 1 - vec.z * vec.z;
    if (temp <= 0.0f)
        return 0.0f;
    return sqrt(temp) / vec.z;
}

inline float TanTheta2(const thread float3& vec)
{
    float temp = 1 - vec.z * vec.z;
    if (temp <= 0.0f)
        return 0.0f;
    return temp / (vec.z * vec.z);
}

static float GGX_D(const thread float3& wh, float alpha)
{
    if (wh.z <= 0.0f) return 0.0f;

    const float tanTheta2 = TanTheta2(wh), cosTheta2 = wh.z * wh.z;

    const float root = alpha / (cosTheta2 * (alpha * alpha + tanTheta2));

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
