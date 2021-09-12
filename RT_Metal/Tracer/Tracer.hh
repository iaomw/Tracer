#ifndef Tracer_h
#define Tracer_h

#include <vector>
#include <iostream>

#include "Common.hh"

#include "Camera.hh"
#include "Texture.hh"
#include "Material.hh"

#include "BVH.hh"
#include "AABB.hh"

#include "Cube.hh"
#include "Square.hh"
#include "Sphere.hh"

inline float Radians(float degree) {
    return degree * M_PI / 180;
}

//inline float4x4 operator* (const simd_float4x4 l, const simd_float4x4 r){
//    return simd_mul(l, r);
//}

inline float4x4 operator* (const float4x4& l, const float4x4& r){
    return simd_mul(l, r);
}

float4x4 scale4x4(float sx, float sy, float sz);
float4x4 rotation4x4(float radians, float3 axis);
float4x4 translation4x4(float tx, float ty, float tz);

inline float4x4 scale4x4(float3& s);
inline float4x4 translation4x4(float3& t);

float4x4 LookAt(const float3 &pos, const float3 &look, const float3 &up);

float4x4 Perspective(float fov, float n, float f);

void prepareCubeList(std::vector<Cube>& list, std::vector<Material>& materials);
void prepareCornellBox(std::vector<Square>& list, std::vector<Material>& materials);
void prepareSphereList(std::vector<Sphere>& list, std::vector<Material>& materials);

void prepareCamera(struct Camera* pointer, float2 viewSize, float2 rotate);

#endif /* Tracer_h */
