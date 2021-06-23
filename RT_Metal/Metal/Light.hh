#ifndef Light_h
#define Light_h

#include "Common.hh"
#include "Ray.hh"

#include "BVH.hh"

#include "Sampling.hh"

#include "Spectrum.hh"
#include "HitRecord.hh"


#ifdef __METAL_VERSION__

#endif

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

class VisibilityTester {
    
    Spectrum Tr() { // Scene // Sampler
        return 0;
    }
    
    bool unoccluded() { // Scene
        return false; // does light blocked
    }
    
    float3 p0, p1;
};

// Light Declarations
class Light {
    
public:
    
    const PrimitiveType pType;
    const uint32_t pIndex;
    
    // Light Public Data
    const int flags;
    const int nSamples;

  protected:
    // Light Protected Data
    const float4x4 LightToWorld, WorldToLight;
    

    // Light Interface
    //virtual ~Light();
    
#ifdef __METAL_VERSION__
    
    Spectrum Sample_Li(const thread HitRecord &ref, const thread float2 &u, thread float3 *wi, thread float *pdf, thread VisibilityTester *vis) const {};
    
    Spectrum Power() {}
    
    //void Preprocess(const Scene &scene) {}
    
    Spectrum Le(const thread Ray &r) const;
    
    float Pdf_Li(const thread HitRecord &ref, const thread float3 &wi) const {}
    
    Spectrum Sample_Le(const thread float2 &u1, const thread float2 &u2, float time,
                               thread Ray *ray, thread float3 *nLight, thread float *pdfPos,
                       thread float *pdfDir) const {}
    
    void Pdf_Le(const thread Ray &ray, const thread float3 &nLight, thread float *pdfPos,
                        thread float *pdfDir) const {}
    
#else
    Light(int flags, const float4x4 &LightToWorld, int nSamples = 1);
#endif

};


#ifdef __METAL_VERSION__

class PointLight {
    float3 p;
    float3 I;

    float3 sample_Li(const thread HitRecord &ref, const thread float2 &u, thread float3 &wi, thread float *pdf, thread VisibilityTester *vis) {
        //ProfilePhase _(Prof::LightSample);
        wi = normalize(p - ref.p);
        *pdf = 1.0;
        //*vis = VisibilityTester(ref, Interaction(pLight, ref.time, mediumInterface));
        
        return I / distance(p, ref.p);
    }

    float3 power() {
        return 4 * M_PI_F * I;
    }

    float pdf_Li(const thread HitRecord &ref, const thread float3& ) {
        return 0;
    }

    float3 Sample_Le(const thread float2 &u1, const thread float2 &u2, float time,
                        thread Ray *ray, thread float3 *nLight, thread float *pdfPos,
                        thread float *pdfDir) {
        //ProfilePhase _(Prof::LightSample);
        *ray = Ray(p, UniformSampleSphere(u1));
        *nLight = ray->direction;
        *pdfPos = 1;
        *pdfDir = UniformSpherePdf();
        return I;
    }
    
    void pdf_Le(const thread Ray&, const thread float3 &, thread float *pdfPos, thread float *pdfDir) const {
        //ProfilePhase _(Prof::LightPdf);
        *pdfPos = 0;
        *pdfDir = UniformSpherePdf();
    }
    
};

#endif


#endif /* Light_h */
