#ifndef Ray_h
#define Ray_h

#include "Common.hh"

enum struct MediumType { Nill, Homogeneous, GridDensity };

#ifdef __METAL_VERSION__

struct Ray {
    float3 origin;
    float3 direction;
    //Float time;
    float eta = 1.0;
    
    MediumType medium = MediumType::Nill;
    
    Ray(): origin(0), direction(0) {}
    
    Ray(float3 o, float3 d): origin(o) {
        direction = normalize(d);
    }
    
    float3 pointAt(float t) const {
        return origin + direction * t;
    }
};

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
