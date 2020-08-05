#ifndef Tracer_h
#define Tracer_h

#include "Common.h"

#include <stdlib.h>
#include <vector>

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

enum struct TextureType { Constant, Checker, Noise, Image };

struct Texture {
    enum TextureType type;
    uint textureIndex;
};

enum struct MaterialType { Lambert, Metal, Dielectric, Diffuse, Isotropic, Specular };

struct Material {
    enum MaterialType type;
    
    float IOR;                 // index of refraction. used by fresnel and refraction.
    float3 albedo;
    
    float specularProb;
    float specularRoughness;
    float3  specularColor;
    
    float refractionProb;
    float refractionRoughness;
    float3  refractionColor;
    
    struct Texture texture;
};

struct AABB {
    float3 mini;
    float3 maxi;
};

struct Square {
    uint8_t axis_i;
    uint8_t axis_j;
    float2 range_i;
    float2 range_j;
    
    uint8_t axis_k;
    float value_k;
    
    float4x4 model_matrix;
    float4x4 normal_matrix;
    float4x4 inverse_matrix;
    
    struct AABB boundingBOX;
    struct Material material;
};

struct Cube {
    float3 a;
    float3 b;
    
    float4x4 model_matrix;
    float4x4 normal_matrix;
    float4x4 inverse_matrix;
    
    struct AABB boundingBOX;
    struct Square rectList[6];
};

struct Sphere {
    float radius;
    float3 center;
    
    float4x4 model_matrix;
    float4x4 normal_matrix;
    float4x4 inverse_matrix;
    
    struct AABB boundingBOX;
    struct Material material;
};

float4x4 matrix4x4_scale(float sx, float sy, float sz);
float4x4 matrix4x4_rotation(float radians, float3 axis);
float4x4 matrix4x4_translation(float tx, float ty, float tz);

void prepareCubeList(std::vector<Cube>& list);
void prepareCornellBox(std::vector<Square>& list);
void prepareSphereList(std::vector<Sphere>& list);

void prepareCamera(struct Camera* pointer, float2 viewSize, float2 rotate);

#endif /* Tracer_h */
