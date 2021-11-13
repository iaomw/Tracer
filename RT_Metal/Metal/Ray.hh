#ifndef Ray_h
#define Ray_h

#include "Common.hh"

enum struct MediumType { _NIL_, Homogeneous, GridDensity };

#ifdef __METAL_VERSION__

struct Ray {
    float3 origin;
    float3 direction;
    
    //float time;
    float eta = 1.0;
    
    MediumType medium = MediumType::_NIL_;
    
    Ray(): origin(0), direction(0) {}
    
    Ray(float3 o, float3 d): origin(o) {
        direction = normalize(d);
    }
    
    void update(float3 o, float3 d) {
        origin = o;
        direction = normalize(d);
    }
    
    float3 pointAt(float t) const {
        return origin + direction * t;
    }
};

#include "Math.hh"

inline float3 OffsetRayOrigin(const thread float3 &p, const thread float3 &pError,
                               const thread float3 &n, const thread float3 &w) {
    float d = dot(abs(n), pError);
    float3 offset = d * float3(n);
    
    if (dot(w, n) < 0) offset = -offset;
    
    float3 po = p + offset;
    // Round offset point _po_ away from _p_
    for (int i = 0; i < 3; ++i) {
        if (offset[i] > 0)
            po[i] = NextFloatUp(po[i]);
        else if (offset[i] < 0)
            po[i] = NextFloatDown(po[i]);
    }
    return po;
}

#endif

//struct RayDifferential {
//    Ray ray;
//    bool hasDifferentials;
//    float3 rxOrigin, ryOrigin;
//    float3 rxDirection, ryDirection;
//
//    void ScaleDifferentials(float s) {
//        rxOrigin = ray.origin + (rxOrigin - ray.origin) * s;
//        ryOrigin = ray.origin + (ryOrigin - ray.origin) * s;
//        rxDirection = ray.direction + (rxDirection - ray.direction) * s;
//        ryDirection = ray.direction + (ryDirection - ray.direction) * s;
//    }
//};

#endif /* Ray_h */
