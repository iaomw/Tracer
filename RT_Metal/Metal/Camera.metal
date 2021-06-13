#include "Camera.hh"

void CoordinateSystem(const thread float3& a, thread float3& b, thread float3& c) {
    
//    if (abs(a.x) > abs(a.y))
//        b = float3(-a.z, 0, a.x) /
//              sqrt(max(FLT_EPSILON, a.x * a.x + a.z * a.z));
//    else
//        b = float3(0, a.z, -a.y) /
//              sqrt(max(FLT_EPSILON, a.y * a.y + a.z * a.z));
    
    if (abs(a.x) > abs(a.y))
        b = float3(-a.z, 0, a.x);
    else
        b = float3(0, a.z, -a.y);
    
    b = normalize(b);
    c = cross(a, b);
}

