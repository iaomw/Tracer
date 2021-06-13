#include "Render.hh"
#include "Random.hh"
#include "Scatter.hh"

typedef enum  {
    VertexInputIndexVertices = 0,
    VertexInputIndexViewSize = 1
} VertexInput;

typedef struct  {
    float2 tex_size;
    float2 view_size;
    float running_time;
    uint32_t frame_count;
    
} Complex;

typedef struct {
    float4 position [[position]];
    float2 texCoord;
} RasterizerData;

typedef struct {
    vector_float2 position;
    vector_float2 textureCoordinate;
} VertexWithUV;

float2 SampleSphericalMap(float3 v)
{
    float2 uv = float2(atan2(v.z, v.x), asin(v.y));
    float2 invAtan = float2(0.1591, 0.3183);
    uv *= invAtan;
    uv += 0.5;
    return uv;
}

float3 LessThan(float3 f, float value)
{
    return float3(
        (f.x < value) ? 1.0f : 0.0f,
        (f.y < value) ? 1.0f : 0.0f,
        (f.z < value) ? 1.0f : 0.0f);
}
 
float3 LinearToSRGB(float3 rgb)
{
    rgb = clamp(rgb, 0.0f, 1.0f);
    return mix(
        pow(rgb, float3(1.0f / 2.4f)) * 1.055f - 0.055f,
        rgb * 12.92f,
        LessThan(rgb, 0.0031308f)
    );
}
 
float3 SRGBToLinear(float3 rgb)
{
    rgb = clamp(rgb, 0.0f, 1.0f);
    return mix(
        pow(((rgb + 0.055f) / 1.055f), float3(2.4f)),
        rgb / 12.92f,
        LessThan(rgb, 0.04045f)
    );
}

float3 ACESTone(float3 color, float adapted_lum)
{
    const float A = 2.51f;
    const float B = 0.03f;
    const float C = 2.43f;
    const float D = 0.59f;
    const float E = 0.14f;

    color *= adapted_lum;
    return (color * (A * color + B)) / (color * (C * color + D) + E);
}

float3 CETone(float3 color, float adapted_lum)
{
    return 1 - exp(-adapted_lum * color);
}

float CETone(float color, float adapted_lum)
{
    return 1 - exp(-adapted_lum * color);
}

vertex RasterizerData
vertexShader(uint     vertexID                  [[vertex_id]],
             constant VertexWithUV *vertexArray [[buffer(VertexInputIndexVertices)]])
{
    RasterizerData out;
    
    float2 world_pos = vertexArray[vertexID].position.xy;
    
    out.position.xy = world_pos;
    out.position.zw = float2(0, 1);
    
    out.texCoord = vertexArray[vertexID].textureCoordinate;
    
    return out;
}

fragment float4
fragmentShader( RasterizerData input [[stage_in]],
                float2 point_coord [[point_coord]],
               
                texture2d<float> thisTexture [[texture(0)]],
                texture2d<float> prevTexture [[texture(1)]],

                constant Complex* sceneMeta [[buffer(0)]])

{
    auto tex_w = sceneMeta->tex_size.x;
    auto tex_h = sceneMeta->tex_size.y;
    
    auto pix_w = sceneMeta->view_size.x;
    auto pix_h = sceneMeta->view_size.y;
    
    auto tex_ratio = tex_w / tex_h;
    auto pix_ratio = pix_w / pix_h;
    
    input.texCoord = 1.0 - input.texCoord;
    
    auto offset = input.texCoord - float2(0.5);
    auto r_ratio = pix_ratio / tex_ratio;
    offset.x *= r_ratio;

    auto scaled = float2(-1, 1) * offset + float2(0.5);
    
    if (scaled.x < 0 || scaled.y < 0 || scaled.x > 1.0 || scaled.y > 1.0) { return float4(0.0); }
    
    auto tex_level = (level) thisTexture.get_num_mip_levels();
    auto this_mip = thisTexture.sample(textureSampler, float2(0.5), tex_level);
    
    auto this_luma = dot(this_mip.rgb, float3(0.2126, 0.7152, 0.0722));
    
    auto tex_color = thisTexture.sample(textureSampler, scaled);
    float mapped = clamp(CETone(this_luma, 1.0f), 0.0, 0.98);
    float expose = 1.0 - mapped;
    
    tex_color.rgb = ACESTone(tex_color.rgb, expose);
    //tex_color.rgb = LinearToSRGB(tex_color.rgb);
    
    return tex_color;
}

template <typename XSampler>
float3 traceBVH(float depth, thread Ray& ray, thread XSampler& xsampler,
                
                constant PackageEnv& packageEnv,
                constant PackagePBR& packagePBR,
                
                constant Primitive&  primitives)
{
    HitRecord hitRecord;
    ScatRecord scatRecord;
    
    float3 color = float3(0.0);
    float3 ratio = float3(1.0);
    
    float2 range_t;

    do { // each ray
            
        uint the_index = 0;
        uint tested_index = UINT_MAX;
        
        uint32_t stack_mark = 0;
        uint32_t stack_level = 0;
        
        range_t = float2(0.001, INFINITY);
        
        if ( primitives.bvhList[the_index].boundingBOX.hit(ray, range_t) ) {

            do { // travel in bvh
                
                uint selected_index = UINT_MAX;
                
                uint left_index = primitives.bvhList[the_index].left;
                uint right_index = primitives.bvhList[the_index].right;
                uint parent_index = primitives.bvhList[the_index].parent;
                
                
                {
//                    auto center = (bvh_list[the_index].boundingBOX.maxi + bvh_list[the_index].boundingBOX.mini) / 2;
//                    auto half_diagonal = (bvh_list[the_index].boundingBOX.maxi - bvh_list[the_index].boundingBOX.mini) / 2;
//
//                    auto p = ray.pointAt(ttt);
//                    auto delta = abs(p - center);
//
//                    int cheker = 0;
//                    for (int i=0; i<3; i++) {
//                        if (abs(delta[i] - half_diagonal[i]) * 0.1 < 0.2 * 1 ) {
//                            cheker+=1;
//                            if (cheker == 2) { return float3(0, 1, 0);}
//                        }
//                    }
//
                }
                
                if (tested_index != left_index && tested_index != right_index) {
                    
                    float t_left = FLT_MAX, t_right = FLT_MAX;
                    
                    bool left_test = primitives.bvhList[left_index].boundingBOX.hit_get_t(ray, range_t, t_left);
                    bool right_test = primitives.bvhList[right_index].boundingBOX.hit_get_t(ray, range_t, t_right);
                    
                    if (!left_test && !right_test) {
                        
                        tested_index = the_index;
                        the_index = parent_index;
                        stack_level -= 1; // pop stack
                        
                        continue;
                    }
                    
                    selected_index = (t_left < t_right)? left_index : right_index;
                    
                    bool needTestAnother = (left_test) && (right_test);
                    if (needTestAnother) { stack_mark |= 1U << stack_level; }
                    
                } // came from parent
                
                else { // came back child
                    
                    uint needCheckChild = (stack_mark >> stack_level) & 1U;
                    // don't need check child in case of go back;
                    stack_mark &= ~(1U << stack_level);
                    
                    if (0 == needCheckChild) { // go up
                        
                        tested_index = the_index;
                        the_index = parent_index;
                        stack_level -= 1; // pop stack
                        
                        continue;
                    }
                    
                    if (tested_index == left_index) {
                        selected_index = right_index;
                    } else {
                        selected_index = left_index;
                    }
                }
                
                auto pIndex = primitives.bvhList[selected_index].pIndex;
                
                switch(primitives.bvhList[selected_index].pType) {
                        
                    case PrimitiveType::BVH: { // Should already tested before reaching this step
                         
                        the_index = selected_index;
                        stack_level += 1;
                        continue;
                    }
                    case PrimitiveType::Sphere: {
                        primitives.sphereList[pIndex].hit_test(ray, range_t, hitRecord); break;
                    }
                    case PrimitiveType::Square: {
                        primitives.squareList[pIndex].hit_test(ray, range_t, hitRecord); break;
                    }
                    case PrimitiveType::Cube: {
                        primitives.cubeList[pIndex].hit_test(ray, range_t, hitRecord); break;
                    }
                    case  PrimitiveType::Triangle: {
                        auto index_r = pIndex * 3;
                        auto index_a = primitives.idxList[index_r];
                        auto index_b = primitives.idxList[index_r + 1];
                        auto index_c = primitives.idxList[index_r + 2];
                        
                        uint3 abc {index_a, index_b, index_c};
                        auto tri = Triangle(primitives.triList, abc);
                        tri.hit_test(ray, range_t, hitRecord); break;
                    }
                    default: { return float3(0); }
                } // switch
                
                tested_index = selected_index;
                
            } while (tested_index != 0);
        }

        if ( isinf(range_t.y ) ) {
            float3 sphereVector = ray.origin + 1000000 * ray.direction;
            float2 uv = SampleSphericalMap(normalize(sphereVector));
            auto ambient = packageEnv.texHDR.sample(textureSampler, uv);
            return ratio * ambient.rgb;
        }
        
        float3 emit_color;
        if ( emit(hitRecord, emit_color, packageEnv.materials) ) {
            return ratio * emit_color;
        }
        
        if ( !scatter(ray, xsampler, hitRecord, scatRecord, packageEnv.materials, packagePBR) ) {
            //return float3(1, 0, 1);
            return float3(0);
        }
        
        ratio *= scatRecord.attenuation;
        
        { // Russian Roulette
            float p = max3(ratio.r, ratio.g, ratio.b);
            //thread auto& rng = xsampler.rng;
            if (xsampler.random() > p)
                break;
            // Add the energy we 'lose' by randomly terminating paths
            ratio *= 1.0f / p;
        }
        
    } while( (--depth) > 0 );
    
    return color;
}

kernel void
tracerKernel(texture2d<half, access::read>       inTexture [[texture(0)]],
             texture2d<half, access::write>     outTexture [[texture(1)]],
             
             texture2d<uint32_t, access::read>       inRNG [[texture(2)]],
             texture2d<uint32_t, access::write>     outRNG [[texture(3)]],
             
             uint2 thread_pos   [[thread_position_in_grid]],
             
             constant Camera*          camera [[buffer(0)]],
             constant Complex*        complex [[buffer(1)]],
             
             constant Primitive&   primitives [[buffer(7)]],
             constant PackageEnv&  packageEnv [[buffer(8)]],
             constant PackagePBR*  packagePBR [[buffer(9)]])
{
    
    uint32_t rr = inRNG.read(thread_pos).r;
    uint32_t gg = inRNG.read(thread_pos).g;
    uint32_t bb = inRNG.read(thread_pos).b;
    uint32_t aa = inRNG.read(thread_pos).a;
    
    uint64_t rng_state = (uint64_t(rr) << 32) | gg;
    uint64_t rng_inc = (uint64_t(bb) << 32) | aa;
    
    pcg32_t rng = { rng_inc, rng_state };
    
    auto frame = complex->frame_count;
        
    auto u = float(thread_pos.x)/outTexture.get_width();
    auto v = float(thread_pos.y)/outTexture.get_height();
    
    float3 color; RandomSampler rs { &rng };
    auto ray = castRay(camera, u, v, &rs);
    
    //uint2 vsize = { inTexture.get_width(), inTexture.get_height()};
    //auto ss = pbrt::SobolSampler(rng, frame_count, thread_pos, vsize);
    
    color = traceBVH(32, ray, rs,
                        packageEnv,
                        packagePBR[1],
                        primitives);
    
    float3 cached_color = float3( inTexture.read( thread_pos ).rgb );
    
    float3 result = (cached_color.rgb * frame + color) / (frame + 1);
    
    outTexture.write(half4(half3(result), 1.0), thread_pos);
    //outTexture.write(half4(xxx), thread_pos);

    gg = rng.state;
    rr = rng.state >> 32;
    
    aa = rng.inc;
    bb = rng.inc >> 32;
    
    vec<uint32_t, 4> rng_cache;
    rng_cache.r = rr;
    rng_cache.g = gg;
    rng_cache.b = bb;
    rng_cache.a = aa;
    
    outRNG.write(rng_cache, thread_pos);
}
