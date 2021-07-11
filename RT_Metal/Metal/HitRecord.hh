#ifndef HitRecord_h
#define HitRecord_h

#include "Ray.hh"

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

struct BxRecord {
    float3 attenuation;
    float bxPDF = 1.0;
};
    
#endif

#endif /* HitRecord_h */
