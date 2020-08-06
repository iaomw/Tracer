#ifndef Tracer_h
#define Tracer_h

#include "Common.hh"

#include <vector>

#include "Camera.hh"
#include "Texture.hh"
#include "Material.hh"

#include "AABB.hh"

#include "Cube.hh"
#include "Square.hh"
#include "Sphere.hh"

float4x4 matrix4x4_scale(float sx, float sy, float sz);
float4x4 matrix4x4_rotation(float radians, float3 axis);
float4x4 matrix4x4_translation(float tx, float ty, float tz);

void prepareCubeList(std::vector<Cube>& list);
void prepareCornellBox(std::vector<Square>& list);
void prepareSphereList(std::vector<Sphere>& list);

void prepareCamera(struct Camera* pointer, float2 viewSize, float2 rotate);

#endif /* Tracer_h */
