#ifndef Camera_h
#define Camera_h

#include "Ray.hh"
#include "AABB.hh"
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

struct Complex {
    float2 tex_size;
    float2 view_size;
    float running_time;
    uint32_t frame_count;
    
    AABB   photonBox;
    float3 photonBoxSize;
    
    float photonInitialRadius;
    
    float photonHashScale;
    float photonHashNumber;
    
    uint32_t photonSum = 0;
};

#ifdef __METAL_VERSION__

template <typename XSampler>
Ray castRay(constant Camera* camera, float s, float t, thread XSampler* xsampler)
{
    auto rd = camera->lenRadius * xsampler->sampleUnitInDisk();
    auto offset = camera->u*rd.x + camera->v*rd.y;
    auto origin = camera->lookFrom + offset;
    
    auto sample = camera->cornerLowLeft + camera->horizontal*s + camera->vertical*t;
    //Ray ray = Ray(float3(s * 1920, t * 1080, -1000), float3(FLT_MIN, FLT_MIN, 1));
    return Ray(origin, sample - origin);
}

#endif

#endif /* Camera_h */
