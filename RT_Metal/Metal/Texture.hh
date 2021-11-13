#ifndef Texture_h
#define Texture_h

#include "Noise.hh"

enum struct TextureType { Constant, Checker, Noise, Image };

class TextureInfo {
public:
    enum TextureType type;
    uint textureIndex;
    
    float3 albedo;
    
#ifdef __METAL_VERSION__
    
    float3 value(constant texture2d<float> *texture, float2 uv, float3 p) constant {
        
        switch (type) {
            case TextureType::Constant:
                return albedo;
                
            case TextureType::Checker: {
                
                auto sines = sin( 8 * M_PI_F * uv.x) * cos(M_PI_F/2 + 4 * M_PI_F * uv.y);
                return albedo * (0.5 * step(0, sines) + 0.5);
            }
                
            case TextureType::Image: {
                
                if (nullptr == texture) { return albedo; }
                
                auto sample = texture->sample(textureSampler, uv);
                return float3(sample.rgb);
            }
                
            case TextureType::Noise:
                return float3( noise(p * 0.1) );
                
            default:
                return 1.0;
        }
    }
    
#endif
    
};

#endif /* Texture_h */
