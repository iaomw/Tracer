#ifndef Material_h
#define Material_h

#include "Common.hh"
#include "Texture.hh"

#ifdef __METAL_VERSION__

#include "BXDF.hh"
#include "MatteBXDF.hh"
#include "SpecularBXDF.hh"
#include "MicrofacetBXDF.h"

#include "HitRecord.hh"

#endif

enum struct MaterialType { Lambert, OrenNayar, Plastic, Metal, Dielectric, Diffuse, Isotropic, Demofox, PBR, PBRT };

class Material {
public:
    enum MaterialType type;
    
    float parameter;
    //float3 albedo;
    
    float specularProb;
    float specularRoughness;
    float3  specularColor;
    
    float refractionProb;
    float refractionRoughness;
    float3  refractionColor;
    
    TextureInfo textureInfo;
    
#ifdef __METAL_VERSION__
    
    float3 F(const thread float3 &wo, const thread float3 &wi, const thread float2 &uv, thread float &pdf) constant;
    
    template <typename BxType>
    float3 F(BxType bx, const thread float3 &wo, const thread float3 &wi, const thread float2 &uv, thread float &pdf) constant {
        
        auto color = textureInfo.value(nullptr, uv, float3(0));
        
        pdf = bx.PDF(wo, wi);
        return color * bx.F(wo, wi);
    }
    
    float PDF(const thread float3 &wo, const thread float3 &wi) constant;
    
    template <typename BxType>
    float PDF(BxType bx, const thread float3 &wo, const thread float3 &wi) constant {
        
        return bx.PDF(wo, wi);
    }
    
    float3 S_F(const thread float3 &wo, thread float3 &wi, const thread float2 &uv, const thread float2 &uu, thread float &pdf) constant;
    
    template <typename BxType>
    float3 S_F(BxType bx, const thread float3 &wo, thread float3 &wi, const thread float2 &uv, const thread float2 &uu, thread float &pdf) constant {
        
        auto color = textureInfo.value(nullptr, uv, float3(0));
        
        return color * bx.S_F(wo, wi, uu, pdf);
    }
    
#endif
};

#ifdef __METAL_VERSION__

inline float3 Material::F(const thread float3 &wo, const thread float3 &wi, const thread float2 &uv, thread float &pdf) constant {
    
    switch(type) {
        case MaterialType::Lambert: {
            Lambertian bx;
            return F<Lambertian>(bx, wo, wi, uv, pdf);
        }
        case MaterialType::Metal:{
            auto bx = createMetalMaterial();
            return F<MetalMaterial>(bx, wo, wi, uv, pdf);
        }
        default:
            return 0;
    }
    
    return 0;
}

inline float Material::PDF(const thread float3 &wo, const thread float3 &wi) constant {
    switch(type) {
        case MaterialType::Lambert: {
            Lambertian bx;
            return PDF<Lambertian>(bx, wo, wi);
        }
        case MaterialType::Metal: {
            auto bx = createMetalMaterial();
            return PDF<MetalMaterial>(bx, wo, wi);
        }
        default:
            return 0;
    }
    return 0;
}

inline float3 Material::S_F(const thread float3 &wo, thread float3 &wi, const thread float2 &uv, const thread float2 &uu, thread float &pdf) constant {
    
    switch(type) {
        case MaterialType::Lambert: {
            Lambertian bx;
            return S_F<Lambertian>(bx, wo, wi, uv, uu, pdf);
        }
        case MaterialType::Metal: {
            auto bx = createMetalMaterial();
            return S_F<MetalMaterial>(bx, wo, wi, uv, uu, pdf);
        }
        default:
            return 0;
    }
    
    return 0;
}

#endif


float schlick(float cosine, float ref_idx);

float fresnel(float n1, float n2, float3 normal, float3 incident, float f0, float f90);

#endif /* Material_h */
