#ifndef Material_h
#define Material_h

#include "Common.hh"
#include "Texture.hh"

enum struct MaterialType { Lambert, Metal, Dielectric, Diffuse, Isotropic, Specular, PBR, PBRT };

struct Material {
    enum MaterialType type;
    
    float parameter;
    //float3 albedo;
    
    float specularProb;
    float specularRoughness;
    float3  specularColor;
    
    float refractionProb;
    float refractionRoughness;
    float3  refractionColor;
    
    struct TextureInfo textureInfo;
    
//    float albedo;
//    float metallic;
//    float roughness;
};

float schlick(float cosine, float ref_idx);

float fresnel(float n1, float n2, float3 normal, float3 incident, float f0, float f90);

#endif /* Material_h */
