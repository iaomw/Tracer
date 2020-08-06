#ifndef Ray_h
#define Ray_h

#include "Common.hh"

struct Ray {
    float3 origin;
    float3 direction;
    //Float time;
    //const Medium *medium;
#ifdef __METAL_VERSION__
    Ray(float3 o, float3 d) {
        origin = o;
        direction = metal::normalize(d);
    }
#endif
    
    float3 pointAt(float t) {
        return origin + direction * t;
    }
};

struct RayDifferential {
    Ray ray;
    bool hasDifferentials;
    float3 rxOrigin, ryOrigin;
    float3 rxDirection, ryDirection;
    
    void ScaleDifferentials(float s) {
        rxOrigin = ray.origin + (rxOrigin - ray.origin) * s;
        ryOrigin = ray.origin + (ryOrigin - ray.origin) * s;
        rxDirection = ray.direction + (rxDirection - ray.direction) * s;
        ryDirection = ray.direction + (ryDirection - ray.direction) * s;
    }
};

#endif /* Ray_h */
