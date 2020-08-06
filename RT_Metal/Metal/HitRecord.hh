#ifndef HitRecord_h
#define HitRecord_h

#include "Ray.hh"
#include "Material.hh"

struct HitRecord {
    float t;
    float3 p;
    
    bool front;
    float3 n;
    float2 uv;
        
    Material material;
    
    float3 normal() {
        return front? n:-n;
    }
    
#ifdef __METAL_VERSION__
    void checkFace(thread Ray& ray) {
        front = (dot(ray.direction, n) < 0);
    }
#endif
    
};

struct ScatRecord {
    Ray specular = {float3(0), float3(0)};
    float prob;
    float3 attenuation;
    // pdf: PDF
};

bool emit(thread HitRecord& hitRecord, thread float3& color);

#endif /* HitRecord_h */
