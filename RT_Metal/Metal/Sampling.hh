#ifndef Sampling_h
#define Sampling_h

#ifdef __METAL_VERSION__

struct LightSampleRecord {
    float3 p;
    float3 n;
    float areaPDF;
};

template <typename XSampler>
inline float2 RejectionSampleDisk(const thread XSampler &rng) {
    float2 p;
    do {
        p.x = 1 - 2 * rng.random();
        p.y = 1 - 2 * rng.random();
    } while (p.x * p.x + p.y * p.y > 1);
    return p;
}

inline float3 UniformSampleHemisphere(const thread float2 &u) {
    float z = u[0];
    float r = sqrt(max(0.0, 1.0 - z * z));
    float phi = 2 * M_PI_F * u[1];
    return float3(r * cos(phi), r * sin(phi), z);
}

inline float UniformHemispherePDF() { return 0.5 / M_PI_F; }

inline float3 UniformSampleSphere(const thread float2 &u) {
    float z = 1 - 2 * u[0];
    float r = sqrt(max(0.0, 1.0 - z * z));
    float phi = 2 * M_PI_F * u[1];
    return float3(r * cos(phi), r * sin(phi), z);
}

inline float UniformSpherePDF() { return 0.25 / M_PI_F; }

inline float2 UniformSampleDisk(const thread float2 &u) {
    float r = sqrt(u[0]);
    float theta = 2 * M_PI_F * u[1];
    return float2(r * cos(theta), r * sin(theta));
}

inline float2 ConcentricSampleDisk(const thread float2 &u) {
    // Map uniform random numbers to $[-1,1]^2$
    float2 uOffset = 2.f * u - float2(1, 1);

    // Handle degeneracy at the origin
    if (uOffset.x == 0 && uOffset.y == 0) return float2(0, 0);
    
    auto PiOver2 = M_PI_F/2.0;
    auto PiOver4 = M_PI_F/4.0;

    // Apply concentric mapping to point
    float theta, r;
    if (abs(uOffset.x) > abs(uOffset.y)) {
        r = uOffset.x;
        theta = PiOver4 * (uOffset.y / uOffset.x);
    } else {
        r = uOffset.y;
        theta = PiOver2 - PiOver4 * (uOffset.x / uOffset.y);
    }
    return r * float2(cos(theta), sin(theta));
}

inline float UniformConePDF(float cosThetaMax) {
    return 1 / (2 * M_PI_F * (1 - cosThetaMax));
}

inline float3 UniformSampleCone(const thread float2 &u, float cosThetaMax) {
    float cosTheta = (1.0 - u[0]) + u[0] * cosThetaMax;
    float sinTheta = sqrt(1.0 - cosTheta * cosTheta);
    float phi = u[1] * 2 * M_PI_F;
    return float3(cos(phi) * sinTheta, sin(phi) * sinTheta, cosTheta);
}

inline float3 UniformSampleCone(const thread float2 &u, float cosThetaMax,
                           const thread float3 &x, const thread float3 &y, const thread float3 &z) {
    float cosTheta = mix(u[0], cosThetaMax, 1.f);
    float sinTheta = sqrt(1.0 - cosTheta * cosTheta);
    float phi = u[1] * 2 * M_PI_F;
    return cos(phi) * sinTheta * x + sin(phi) * sinTheta * y + cosTheta * z;
}

inline float2 UniformSampleTriangle(const thread float2 &u) {
    float su0 = sqrt(u[0]);
    return float2(1 - su0, u[1] * su0);
}

inline float3 CosineSampleHemisphere(const thread float2 &u) {
    float2 d = ConcentricSampleDisk(u);
    float z = sqrt(max(0.0, 1.0 - d.x * d.x - d.y * d.y));
    return float3(d.x, d.y, z);
}

inline float CosineHemispherePDF(float cosTheta) { return cosTheta / M_PI_F; }

inline float BalanceHeuristic(int nf, float fPdf, int ng, float gPdf) {
    return (nf * fPdf) / (nf * fPdf + ng * gPdf);
}

inline float PowerHeuristic(int nf, float fPdf, int ng, float gPdf) {
    float f = nf * fPdf, g = ng * gPdf;
    return (f * f) / (f * f + g * g);
}

enum struct TransportMode { Radiance, Importance };

inline float Radians(float deg) { return (M_PI_F / 180) * deg; }
inline float Degrees(float rad) { return (180 / M_PI_F) * rad; }

// BSDF Inline Functions
inline float CosTheta(const thread float3 &w) { return w.z; }
inline float Cos2Theta(const thread thread float3 &w) { return w.z * w.z; }
inline float AbsCosTheta(const thread float3 &w) { return abs(w.z); }
inline float Sin2Theta(const thread float3 &w) { return max(0.0, 1.0 - Cos2Theta(w)); }

inline float SinTheta(const thread float3 &w) { return sqrt(Sin2Theta(w)); }

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
    if (temp <= 0.0f || zz == 0.0f)
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

inline float3 Faceforward(const thread float3 &n, const thread float3 &v) {
    return (dot(n, v) < 0.f) ? -n : n;
}

#endif

#endif /* Sampling_h */
