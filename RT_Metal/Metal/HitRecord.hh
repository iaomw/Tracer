#ifndef HitRecord_h
#define HitRecord_h

#include "Ray.hh"
#include "Material.hh"

#ifdef __METAL_VERSION__

struct HitRecord {

    float t;
    float3 p;

    bool f;
    float3 n;
    float3 sn;
   
    float2 uv;
    uint material;
    
    float PDF;
    
    void checkFace(const thread Ray& ray) {
        f = dot(ray.direction, n) < 0;
        sn = f? n:-n;
    }
};

struct ScatRecord {
    float3 attenuation;
    float bxPDF = 1.0;
};

bool emit(thread HitRecord& hitRecord, thread float3& color, constant Material* materials);
    
#endif

#endif /* HitRecord_h */
