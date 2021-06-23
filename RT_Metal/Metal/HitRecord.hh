#ifndef HitRecord_h
#define HitRecord_h

#include "Ray.hh"
#include "Material.hh"

#ifdef __METAL_VERSION__

struct HitRecord {
    float t;
    packed_float3 p;
    
    bool f;
    packed_float3 n;
    
    float2 uv;
    uint material;
    
    float3 sn() {
        return f? n:-n;
    }
    
    void checkFace(const thread Ray& ray) {
        f = (dot(ray.direction, n) < 0);
    }
};

struct ScatRecord {
    float3 attenuation;
    // pdf: PDF
};

bool emit(thread HitRecord& hitRecord, thread float3& color, constant Material* materials);
    
#endif

#endif /* HitRecord_h */
