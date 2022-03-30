#ifndef Common_h
#define Common_h

    #ifdef __METAL_VERSION__

        #include <metal_stdlib>
        using namespace metal;

        constexpr sampler textureSampler (mag_filter::linear, min_filter::linear, mip_filter::linear);
        
    #else

        #define let __auto_type const
        #define var __auto_type

        #include <MetalKit/MetalKit.h>
        #include <simd/simd.h>

        typedef simd_float4x4 float4x4;
        typedef simd_float3x3 float3x3;
        typedef simd_float2x2 float2x2;
        typedef simd_float4 float4;
        typedef simd_float3 float3;
        typedef simd_float2 float2;
        
        struct MeshElement {
            float vx, vy, vz;
            float nx, ny, nz;
            float2 uv;
        };

        #define identity_4x4 matrix_identity_float4x4

        #if defined __cplusplus

            struct packed_float3 {
                float x=0, y=0, z=0;
                
                packed_float3();
                packed_float3(float3 v) {
                    x = v.x;
                    y = v.y;
                    z = v.z;
                }
                
                float operator[](int i) const {
                   //Assert(i >= 0 && i <= 2);
                   if (i == 0) return x;
                   if (i == 1) return y;
                   return z;
                }
            };
        #endif // __cplusplus
    #endif // __METAL_VERSION__

#endif /* Common_h */
