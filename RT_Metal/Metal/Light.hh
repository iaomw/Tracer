#ifndef Light_h
#define Light_h

#include "Common.hh"

#include "Ray.hh"
#include "Spectrum.hh"
#include "HitRecord.hh"

enum class LightFlags : int {
    DeltaPosition = 1,
    DeltaDirection = 2,
    Area = 4,
    Infinite = 8
};

inline bool IsDeltaLight(int flags) {
    return flags & (int)LightFlags::DeltaPosition ||
           flags & (int)LightFlags::DeltaDirection;
}

// Light Declarations
struct Light {
    
public:
    
    const PrimitiveType pType;
    const uint32_t pIndex;
    
    const int flags;
    
    Light(PrimitiveType pt, uint32_t pi, int f):
        pType(pt), pIndex(pi), flags(f) {}

  protected:
    // Light Protected Data
    float4x4 LightToWorld, WorldToLight;
};


#ifdef __METAL_VERSION__

class PointLight {
    float3 p;
    float3 I;

    float3 Sample_Li(const thread HitRecord &ref, const thread float2 &u, thread float3 &wi, thread float *pdf) {
        //ProfilePhase _(Prof::LightSample);
        
        wi = normalize(p - ref.p); *pdf = 1.0;
        
        auto dist = distance(p, ref.p);
        
        return I / (dist * dist);
    }

    float3 Power() {
        return 4 * M_PI_F * I;
    }

    float PDF_Li(const thread HitRecord &ref, const thread float3& ) {
        return 0;
    }

    float3 Sample_Le(const thread float2 &u1, const thread float2 &u2, float time,
                     thread Ray *ray, thread float3 *nLight, thread float *pdfPos, thread float *pdfDir) {
        //ProfilePhase _(Prof::LightSample);
        *ray = Ray(p, UniformSampleSphere(u1));
        *nLight = ray->direction;
        *pdfPos = 1;
        *pdfDir = UniformSpherePDF();
        return I;
    }
    
    void PDF_Le(const thread Ray&, const thread float3 &, thread float *pdfPos, thread float *pdfDir) const {
        //ProfilePhase _(Prof::LightPdf);
        *pdfPos = 0;
        *pdfDir = UniformSpherePDF();
    }
};

#endif


#endif /* Light_h */
