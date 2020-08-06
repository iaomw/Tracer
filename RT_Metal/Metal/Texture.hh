#ifndef Texture_h
#define Texture_h

#include "Common.hh"

enum struct TextureType { Constant, Checker, Noise, Image };
struct Texture {
    enum TextureType type;
    uint textureIndex;
    float3 albedo;
    
#ifdef __METAL_VERSION__
    
    float3 value(constant texture2d<half, access::sample> *texture, float2 uv, float3 p) {
        
        switch (type) {
            case TextureType::Constant:
                return albedo;
                
            case TextureType::Checker: {
                auto sines = sin(100 * uv.x) * cos(10 * uv.y);
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
                
            default:
                return albedo;
        }
    }
    
#endif
    
};

#endif /* Texture_h */
