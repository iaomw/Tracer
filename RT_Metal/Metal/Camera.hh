#ifndef Camera_h
#define Camera_h

#include "Ray.hh"
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

struct Transform {
    float4x4 m, w;
};

#ifdef __METAL_VERSION__

void CoordinateSystem(const thread float3& a, thread float3& b, thread float3& c);

template <typename XSampler>
Ray castRay(constant Camera* camera, float s, float t, thread XSampler* xsampler)
{
    auto rd = camera->lenRadius * xsampler->sampleUnitInDisk();
    auto offset = camera->u*rd.x + camera->v*rd.y;
    auto origin = camera->lookFrom + offset ;
    
    //thread uint *bits = reinterpret_cast<thread uint*>(&s);
    
    auto sample = camera->cornerLowLeft + camera->horizontal*s + camera->vertical*t;
    //Ray ray = Ray(float3(s * 1920, t * 1080, -1000), float3(FLT_MIN, FLT_MIN, 1));
    return Ray(origin, sample - origin);
}

#else

namespace pbrt {



}


#endif

#endif /* Camera_h */
