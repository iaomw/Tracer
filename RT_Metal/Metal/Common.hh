#ifndef Common_h
#define Common_h

    #ifdef __METAL_VERSION__

        #include <metal_stdlib>
        using namespace metal;

        constexpr sampler textureSampler (mag_filter::linear, min_filter::linear, mip_filter::linear);

        #define metal_constant constant

    #else

        #define let __auto_type const
        #define var __auto_type

        #define metal_constant
        #define thread
        
        #include <MetalKit/MetalKit.h>
        #include <simd/simd.h>

        typedef simd_float4x4 float4x4;
        typedef simd_float3x3 float3x3;
        typedef simd_float4 float4;
        typedef simd_float3 float3;
        typedef simd_float2 float2;
        
        struct VertexStrut {
            float vx, vy, vz;
            float nx, ny, nz;
            float2 uv;
        };

    #endif

#endif /* Common_h */
