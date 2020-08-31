#ifndef Camera_h
#define Camera_h

#include "Ray.hh"
#include "Random.hh"
#include "Common.hh"

struct Camera {
    
    float3 lookFrom, lookAt, viewUp;
    
    float vfov;
    float aspect, aperture;
    float lenRadius, focus_dist;
    
    float3 u, v, w;
    
    float3 vertical;
    float3 horizontal;
    float3 cornerLowLeft;
};

void CoordinateSystem(const thread float3& a, thread float3& b, thread float3& c);

#ifdef __METAL_VERSION__
Ray castRay(constant Camera* camera, float s, float t, thread pcg32_t* seed);
#endif

#endif /* Camera_h */
