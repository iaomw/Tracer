#include "Camera.hh"

void CoordinateSystem(const thread float3& a, thread float3& b, thread float3& c) {
    
    if (abs(a.x) > abs(a.y))
        b = float3(-a.z, 0, a.x) /
              sqrt(a.x * a.x + a.z * a.z);
    else
        b = float3(0, a.z, -a.y) /
              sqrt(a.y * a.y + a.z * a.z);
    c = cross(a, b);
}

Ray castRay(constant Camera* camera, float s, float t, thread pcg32_t* seed) {
    auto rd = camera->lenRadius * randomInUnitDiskFF(seed);
    //auto rd = 0.005 * randomInUnitDiskFF(seed);
    auto offset = camera->u*rd.x + camera->v*rd.y;
    auto origin = camera->lookFrom + offset;
    auto sample = camera->cornerLowLeft + camera->horizontal*s + camera->vertical*t;
    Ray ray = Ray(origin, sample - origin);
    return ray;
}
