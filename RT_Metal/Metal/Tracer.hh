#ifndef Tracer_h
#define Tracer_h

#include <Foundation/Foundation.h>
#include <simd/simd.h>
#include "Common.h"

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

enum struct MaterialType { Lambert, Metal, Dielectric, Diffuse, Isotropic };
struct Material {
    enum MaterialType type;
    
    float3 albedo;
    float refractive;
    struct Texture texture;
};

struct AABB {
    float3 mini;
    float3 maxi;
};

struct Square {
    uint8_t axis_i;
    uint8_t axis_j;
    float2 rang_i;
    float2 rang_j;
    
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

void prepareCubeList(struct Cube* pointer);
void prepareCornellBox(struct Square* pointer);
void prepareCamera(struct Camera* pointer, float2 viewSize);

#endif /* Tracer_h */
