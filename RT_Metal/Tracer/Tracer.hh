#ifndef Tracer_h
#define Tracer_h

#include "Common.hh"

#include <vector>

//#include "Light.hh"

#include "Camera.hh"
#include "Texture.hh"
#include "Material.hh"

#include "AABB.hh"

#include "Cube.hh"
#include "Square.hh"
#include "Sphere.hh"

inline float Radians(float degree) {
    
    return degree * M_PI / 180;
}

float4x4 scale4x4(float sx, float sy, float sz);
float4x4 rotation4x4(float radians, float3 axis);
float4x4 translation4x4(float tx, float ty, float tz);

float4x4 LookAt(const float3 &pos, const float3 &look, const float3 &up);

float4x4 Perspective(float fov, float n, float f);

void prepareCubeList(std::vector<Cube>& list, std::vector<Material>& materials);
void prepareCornellBox(std::vector<Square>& list, std::vector<Material>& materials);
void prepareSphereList(std::vector<Sphere>& list, std::vector<Material>& materials);

void prepareCamera(struct Camera* pointer, float2 viewSize, float2 rotate);

#endif /* Tracer_h */
