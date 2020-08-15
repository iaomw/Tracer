#ifndef Texture_h
#define Texture_h

#include "Common.hh"

#include "Noise.hh"

enum struct TextureType { Constant, Checker, Noise, Image };

struct TextureInfo {
    
    enum TextureType type;
    uint textureIndex;
    float3 albedo;
    
#ifdef __METAL_VERSION__
    
    float3 value(constant texture2d<half, access::sample> *texture, float2 uv, float3 p) {
        
        switch (type) {
            case TextureType::Constant:
                return albedo;
                
            case TextureType::Checker: {
                auto sines = sin(10 * M_PI_F * uv.x) * cos(M_PI_F * uv.y);
                if (sines < 0)
                    return albedo * 0.5;
                else
                    return albedo;
            }
                
            case TextureType::Image: {
                
                if (nullptr == texture) { return albedo; }
                
                auto sample = texture->sample(textureSampler, uv);
                return float3(sample.rgb);
            }
                
            case TextureType::Noise:
                return float3( noise(p * 0.1) );
                
            default:
                return albedo;
        }
    }
    
#endif
    
};

#endif /* Texture_h */
