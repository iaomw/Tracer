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
    
    float3 value(constant texture2d<float> *texture, float2 uv, float3 p) constant {
        
        switch (type) {
            case TextureType::Constant:
                return albedo;
                
            case TextureType::Checker: {
                
//                auto sample = texture->sample(textureSampler, uv);
//                auto result = dot(sample.rgb, {0.299, 0.587, 0.114});
//                return float3(result);
                
                auto sines = sin( 8 * M_PI_F * uv.x) * cos(M_PI_F/2 + 4 * M_PI_F * uv.y);
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
