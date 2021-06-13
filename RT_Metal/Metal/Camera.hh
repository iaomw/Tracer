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

#ifdef __METAL_VERSION__

void CoordinateSystem(const thread float3& a, thread float3& b, thread float3& c);

template <typename XSampler>
Ray castRay(constant Camera* camera, float s, float t, thread XSampler* xsampler)
{
    auto rd = camera->lenRadius * xsampler->sampleUnitInDisk();
    //auto rd = 0.005 * randomInUnitDiskFF(seed);
    auto offset = camera->u*rd.x + camera->v*rd.y;
    auto origin = camera->lookFrom + offset;
    auto sample = camera->cornerLowLeft + camera->horizontal*s + camera->vertical*t;
    Ray ray = Ray(origin, sample - origin);
    return ray;
}

#else

static float4x4 LookAtMatrix(const float3 &pos, const float3 &look, const float3 &up) {
    
    auto zaxis = simd::normalize(look-pos);
    auto xaxis = simd::normalize(simd::cross(up, zaxis));
    auto yaxis = simd::cross(zaxis, xaxis);
    
    return float4x4 {{
        { xaxis.x, yaxis.x, zaxis.x, 0.0 },
        { xaxis.y, yaxis.y, zaxis.y, 0.0 },
        { xaxis.z, yaxis.z, zaxis.z, 0.0 },
        { -simd::dot(xaxis, pos),  -simd::dot(yaxis, pos),  -simd::dot(zaxis, pos),  1 }
    }};
}

#endif

#endif /* Camera_h */
