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
    
} SceneComplex;

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
vertexShader(uint vertexID [[vertex_id]],
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

                constant SceneComplex* sceneMeta [[buffer(0)]])

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
    
    auto tex_color = thisTexture.sample(textureSampler, scaled);

    auto this_mip = thisTexture.sample(textureSampler, float2(0.5), thisTexture.get_num_mip_levels());
    auto prev_mip = prevTexture.sample(textureSampler, float2(0.5), prevTexture.get_num_mip_levels());
    
    auto this_luma = dot(this_mip.rgb, float3(0.2126, 0.7152, 0.0722));
    auto prev_luma = dot(prev_mip.rgb, float3(0.2126, 0.7152, 0.0722));
    
    auto frame_count = sceneMeta->frame_count;
    
    auto mix_luma = (this_luma + prev_luma * frame_count) / (1 + frame_count);
    
   // float luminance = dot(mix_mip.rgb, float3(0.2126, 0.7152, 0.0722));
    float mapped = clamp(CETone(mix_luma, 1.0f), 0.0, 0.96);
    float expose = 1.0 - mapped;
    
    tex_color.rgb = ACESTone(tex_color.rgb, expose * 0.5);
    tex_color.rgb = LinearToSRGB(tex_color.rgb);
    
    return tex_color;
}

float3 traceBVH(float depth, thread Ray& ray,
                         
                         thread texture2d<half, access::sample> &ambientHDR,
                         thread texture2d<half, access::sample> &textureTest,
                         
                         constant Sphere* sphere_list,
                         constant Square* square_list,
                         constant Cube* cube_list,
                         
                         constant uint32_t* tirIndex,
                         constant Triangle* tirList,
                         constant BVH* bvh_list,
                
                         constant  Material* materials,
                         
                         thread pcg32_random_t* seed)
{
    HitRecord hitRecord;
    ScatRecord scatRecord;
    
    float3 color = float3(0);
    float3 ratio = float3(1.0);
    
    float2 range_t;

    do {
        
        range_t = float2(0.01, INFINITY);
            
        uint the_index = 0;
        uint tested_index = UINT_MAX;
        
        uint64_t stack_mark = 0;
        uint32_t stack_level = 0;
        
        float ttt = INFINITY;
        
        //if ( bvh_list[the_index].boundingBOX.hit(ray, range_t) ) {
        if ( bvh_list[the_index].boundingBOX.hit_get_t(ray, range_t, ttt) ) {

            do {
                
                uint selected_index = UINT_MAX;
                
                uint left_index = bvh_list[the_index].left;
                uint right_index = bvh_list[the_index].right;
                uint parent_index = bvh_list[the_index].parent;
                
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
//                    bool left_test = bvh_list[left_index].boundingBOX.hit_get_t(ray, range_t, ttt);
//
//                    if (left_test) {
//                        center = (bvh_list[left_index].boundingBOX.maxi + bvh_list[left_index].boundingBOX.mini) / 2;
//                        half_diagonal = (bvh_list[left_index].boundingBOX.maxi - bvh_list[left_index].boundingBOX.mini) / 2;
//
//                        p = ray.pointAt(ttt);
//                        delta = abs(p - center);
//
//                        cheker = 0;
//                        for (int i=0; i<3; i++) {
//                            if (abs(delta[i] - half_diagonal[i]) * 0.1 < 0.2 * 1 ) {
//                                cheker+=1;
//                                if (cheker == 2) { return float3(0, 1, 0);}
//                            }
//                        }
//                    }
//
//                    bool right_test = bvh_list[right_index].boundingBOX.hit_get_t(ray, range_t, ttt);
//
//                    if (right_test) {
//                        center = (bvh_list[right_index].boundingBOX.maxi + bvh_list[right_index].boundingBOX.mini) / 2;
//                        half_diagonal = (bvh_list[right_index].boundingBOX.maxi - bvh_list[right_index].boundingBOX.mini) / 2;
//
//                        p = ray.pointAt(ttt);
//                        delta = abs(p - center);
//
//                        cheker = 0;
//                        for (int i=0; i<3; i++) {
//                            if (abs(delta[i] - half_diagonal[i]) * 0.1 < 0.2 * 1 ) {
//                                cheker+=1;
//                                if (cheker == 2) { return float3(0, 1, 0);}
//                            }
//                        }
//                    }
                }
                
                if (tested_index != left_index && tested_index != right_index) {
                
                    float t_left = INFINITY, t_right = INFINITY;
                    
                    bool left_test = bvh_list[left_index].boundingBOX.hit_get_t(ray, range_t, t_left);
                    bool right_test = bvh_list[right_index].boundingBOX.hit_get_t(ray, range_t, t_right);
                    
                    if (!left_test && !right_test) {
                        
                        tested_index = the_index;
                        the_index = parent_index;
                        
                        stack_mark &= ~(1U << stack_level); // clear the bit
                        stack_level -= 1; // pop stack
                        continue;
                    }
                    
                    selected_index = (t_left < t_right)? left_index:right_index;
                    
                    bool needTestAnother = (left_test) && (right_test);
                    if (needTestAnother) { stack_mark |= 1U << stack_level; }
                } // came to this part firsty
                
                else { // came back from left or right
                    
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
                    
                    auto hitted = bvh_list[selected_index].boundingBOX.hit(ray, range_t);
                    if (!hitted) {
                        tested_index = selected_index;
                        the_index = parent_index;
                        stack_level -= 1;
                        continue;
                    }
                }
                
                auto shapeIndex = bvh_list[selected_index].shapeIndex;
                
                switch(bvh_list[selected_index].shape) {
                        
                    case ShapeType::BVH: { // Should already tested before reaching this step
                        //auto hitted = bvh_list[selected_index].boundingBOX.hit_keep_range(ray, range_t);
                        //if (hitted) {
                            the_index = selected_index;
                            stack_level += 1;
                            continue;
                        //}
                    }
                    case ShapeType::Sphere: {
                        sphere_list[shapeIndex].hit_test(ray, range_t, hitRecord); break;
                    }
                    case ShapeType::Square: {
                        square_list[shapeIndex].hit_test(ray, range_t, hitRecord); break;
                    }
                    case ShapeType::Cube: {
                        cube_list[shapeIndex].hit_test(ray, range_t, hitRecord); break;
                    }
                    case  ShapeType::Triangle: {
                        auto index_r = shapeIndex * 3;
                        auto index_a = tirIndex[index_r];
                        auto index_b = tirIndex[index_r + 1];
                        auto index_c = tirIndex[index_r + 2];
                        
                        Triangle::rayTriangleIntersect(ray, tirList[index_a], tirList[index_b], tirList[index_c], range_t, hitRecord);
                        break;
                    }
                    default: { return float3(0); }
                } // switch
                
                tested_index = selected_index;
                
            } while (tested_index != 0);
        }

        if ( isinf(range_t.y ) ) {
            float3 sphereVector = ray.origin + 1000000 * ray.direction;
            float2 uv = SampleSphericalMap(normalize(sphereVector));
            auto ambient = ambientHDR.sample(textureSampler, uv);
            //return ratio * float3(ambient.rgb);
            color = ratio * float3(ambient.rgb);
            break;
        }
        
        float3 emit_color;
        if ( emit(hitRecord, emit_color, materials) ) {
            //return ratio * emit_color;
            color = ratio * emit_color;
            break;
        }
        
        if ( !scatter(ray, hitRecord, scatRecord, seed, materials) ) {
            //return float3(1, 0, 1);
            color = float3(0);
            break;
        }
        
        ratio *= scatRecord.attenuation;
        
        { // Russian Roulette
            float p = max3(ratio.r, ratio.g, ratio.b);
            if (randomF(seed) > p)
                break;
            // Add the energy we 'lose' by randomly terminating paths
            ratio *= 1.0f / p;
        }
        
    } while( (--depth) > 0 );
    
    return color;
}


float3 traceColor(float depth, thread Ray& ray,
                         
                         thread texture2d<half, access::sample> &ambientHDR,
                         thread texture2d<half, access::sample> &textureTest,
                         
                         constant Sphere* sphere_list,
                         constant Square* square_list,
                         constant Cube* cube_list,
                         
                         constant uint32_t* meshIndex,
                         constant Triangle* mesh,
                         constant BVH* bvh_list,
                  
                         constant  Material* materials,
                         
                         thread pcg32_random_t* seed)
{
    HitRecord hitRecord;
    ScatRecord scatRecord;
    
    float2 range_t;
    float3 ratio = float3(1.0);
    
    float3 color = float3(0);
   
    do {
        
        range_t = float2(0.01, INFINITY);

//        for (int i=0; i<13; i++) {
//            sphere_list[i].hit_test(ray, range_t, hitRecord);
//        }

        for (int i=0; i<6; i++) {
            square_list[i].hit_test(ray, range_t, hitRecord);
        }

        for (int i=0; i<2; i++) {
            cube_list[i].hit_test(ray, range_t, hitRecord);
        }
//
//        for (int i=1; i<2; i++) {
//            cube_list[1].hit_medium(ray, range_t, hitRecord, seed);
//        }
        
        
        //144045
        //for (uint32_t i=0; i<372; i+=3) {
        //for (uint32_t i=0; i<140448; i+=3) {
//        for (uint32_t i=0; i<44045; i+=3) {
//            auto index_a = meshIndex[i];
//            auto index_b = meshIndex[i+1];
//            auto index_c = meshIndex[i+2];
//
//            constant auto& ele_a = mesh[index_a];
//            constant auto& ele_b = mesh[index_b];
//            constant auto& ele_c = mesh[index_c];
//
//            auto done = Triangle::rayTriangleIntersect(ray, ele_a, ele_b, ele_c, range_t, hitRecord);
//
////            if (done) {
////                auto hhhh = textureTest.sample(textureSampler, hitRecord.uv);
////                hitRecord.material.textureInfo.albedo = float3(hhhh.xyz);
////            }
//        }

        if ( isinf(range_t.y ) ) {
            float3 sphereVector = ray.origin + 1000000 * ray.direction;
            float2 uv = SampleSphericalMap(normalize(sphereVector));
            auto ambient = ambientHDR.sample(textureSampler, uv);
            //return ratio * float3(ambient.rgb);
            color = ratio * float3(ambient.rgb);
            break;
        }
        
        float3 emit_color;
        if ( emit(hitRecord, emit_color, materials) ) {
            //return ratio * emit_color;
            color = ratio * emit_color;
            break;
        }
        
        if ( !scatter(ray, hitRecord, scatRecord, seed, materials) ) {
            //return float3(1, 0, 1);
            color = float3(0);
            break;
        }
        
        ratio *= scatRecord.attenuation;
        
        { // Russian Roulette
            float p = max3(ratio.r, ratio.g, ratio.b);
            if (randomF(seed) > p)
                break;
            // Add the energy we 'lose' by randomly terminating paths
            ratio *= 1.0f / p;
        }
        
    } while( (--depth) > 0 );
    
    return color;
}

kernel void
tracerKernel(texture2d<half, access::read>  inTexture  [[texture(0)]],
             texture2d<half, access::write> outTexture [[texture(1)]],
             
             texture2d<uint32_t, access::read>  inRNG  [[texture(2)]],
             texture2d<uint32_t, access::write> outRNG [[texture(3)]],
             
             texture2d<half, access::sample>  textureHDR [[texture(4)]],
             texture2d<half, access::sample> textureTest [[texture(5)]],
             
             uint2 thread_pos  [[thread_position_in_grid]],
             
             constant SceneComplex* sceneMeta [[buffer(0)]],
             constant Camera* camera [[buffer(1)]],

             constant Sphere* sphere_list [[buffer(2)]],
             constant Square* square_list [[buffer(3)]],
             constant Cube* cube_list [[buffer(4)]],
             
             constant uint32_t* meshIndex [[buffer(5)]],
             constant Triangle* meshList [[buffer(6)]],
             constant BVH* bvh_list [[buffer(7)]],
             
             constant Material* materials [[buffer(8)]])
{
    // Check if the pixel is within the bounds of the output texture
    if((thread_pos.x >= outTexture.get_width()) || (thread_pos.y >= outTexture.get_height()))
    {// Return early if the pixel is out of bounds
        return;
    }
    
    uint32_t rr = inRNG.read(thread_pos).r;
    uint32_t gg = inRNG.read(thread_pos).g;
    uint32_t bb = inRNG.read(thread_pos).b;
    uint32_t aa = inRNG.read(thread_pos).a;
    
    uint64_t rng_state = (uint64_t(rr) << 32) | gg;
    uint64_t rng_inc = (uint64_t(bb) << 32) | aa;
    
    pcg32_random_t rng = { rng_inc, rng_state };
    
    auto cached_color = float3(inTexture.read(thread_pos).rgb);
    auto frame_count = sceneMeta->frame_count;
    
    //auto float_time = float(sceneMeta->running_time);
    //auto int_time = uint32_t(1000*sceneMeta->running_time);
    //auto pixelPisition = input.texCoord*float2(sceneMeta->view_size);
    
    auto u = float(thread_pos.x)/outTexture.get_width();
    auto v = float(thread_pos.y)/outTexture.get_height();
    
    auto ray = castRay(camera, u, v, &rng);
    auto color = traceBVH(32, ray,
    //auto color = traceColor(32, ray,
                            textureHDR,
                            textureTest,
                            
                            sphere_list,
                            square_list,
                            cube_list,
                            
                            meshIndex,
                            meshList,
                            bvh_list,
                          
                            materials,
                            
                            &rng);

    float3 result = (cached_color.rgb * frame_count + color) / (frame_count + 1);
    
    auto hhhh = half4(1.0);
    hhhh.rgb = half3(result);
    
    outTexture.write(hhhh, thread_pos);
    
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
