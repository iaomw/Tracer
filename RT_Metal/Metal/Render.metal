
#include <metal_stdlib>
using namespace metal;

#include "Random.hh"
#include "Render.hh"
#include "Tracer.metal"

typedef enum  {
    VertexInputIndexVertices = 0,
    VertexInputIndexViewSize = 1
} VertexInput;

typedef struct  {
    float2 view_size;
    float running_time;
    uint32_t sample_frame_count;
    
} SceneComplex;

typedef struct {
    float4 position [[position]];
    float2 texCoord;
} RasterizerData;

typedef struct {
    vector_float2 position;
    vector_float2 textureCoordinate;
} VertexWithUV;

static float3 traceColor(thread Ray& ray,
                         float depth,
                         thread float3& background,
                         constant Sphere* sphere_list,
                         constant Square* square_list,
                         constant Cube* cube_list,
                         thread pcg32_random_t* seed)
{
    HitRecord hitRecord;
    ScatterRecord scatterRecord;
    
    float3 color = float3(0.0);
    float3 ratio = float3(1.0);
    float2 range_t = float2(0.000001, FLT_MAX);
    
    Ray testRay = ray;
    bool hasNewRay = false;
    
    do {
        hasNewRay = false;
        HitRecord testHitRecord;
        //float nearst_t = FLT_MAX;
        range_t.y = FLT_MAX;
        
        for (int i=0; i<6; i++) {
            auto square = square_list[i];
            if(square.hit(testRay, range_t, testHitRecord)) {
                if (testHitRecord.t < range_t.y) {
                    range_t.y = testHitRecord.t;
                    hitRecord = testHitRecord;
                }
            }
        }
        
        for (int i=0; i<2; i++) {
            auto cube = cube_list[i];
            if(cube.hit(testRay, range_t, testHitRecord)) {
                if (testHitRecord.t < range_t.y) {
                    range_t.y = testHitRecord.t;
                    hitRecord = testHitRecord;
                }
            }
        }
        
        if (range_t.y == FLT_MAX) {
            color += ratio * background;
            break;
        }
        
        auto emitted = emit(hitRecord);
        color += ratio * emitted;
        
        if(!scatter(testRay, hitRecord, scatterRecord, seed)) {
            break;
        }
        
        ratio = ratio * scatterRecord.attenuation;
        testRay = scatterRecord.specularRay;
        
        hasNewRay = true;
        depth -= 1;
        
    } while(hasNewRay && depth > 0);
    
    return color;
}

vertex RasterizerData
vertexShader(uint vertexID [[vertex_id]],
             constant VertexWithUV *vertexArray [[buffer(VertexInputIndexVertices)]])
{
    RasterizerData out;
    
    float2 world_pos = vertexArray[vertexID].position.xy;
    
    out.position = vector_float4(0, 0, 0, 1);
    out.position.xy = world_pos;
    
    out.texCoord = vertexArray[vertexID].textureCoordinate;
    
    return out;
}

fragment float4
fragmentShader( RasterizerData input [[stage_in]],
               
                texture2d<float> theTexture [[texture(0)]],
               
                float2 point_coord [[point_coord]],

                constant SceneComplex* sceneMeta [[buffer(0)]])

{
    constexpr sampler textureSampler (mag_filter::linear, min_filter::linear);
    
    auto width = sceneMeta->view_size.x;
    auto height = sceneMeta->view_size.y;
    
    input.texCoord.y = 1.0 - input.texCoord.y;
    auto offset = input.texCoord - float2(0.5);
    
    auto ratio = width / height;
    
    if (ratio > 1) {
        offset.x *= ratio;
    } else if (ratio < 1) {
        offset.y /= ratio;
    }
    
    auto scaled = offset + float(0.5);
    
    auto colorSample = theTexture.sample(textureSampler, scaled);
    
    return colorSample;
}

kernel void
tracerKernel(texture2d<half, access::read>  inTexture  [[texture(0)]],
             texture2d<half, access::write> outTexture [[texture(1)]],
             
             texture2d<uint32_t, access::read> inSeedRNG [[texture(2)]],
             texture2d<uint32_t, access::write> outSeedRNG [[texture(3)]],
             
             uint2 pos_grid  [[thread_position_in_grid]],
             constant SceneComplex* sceneMeta [[buffer(0)]],
             constant Camera* camera [[buffer(1)]],

             constant Sphere* sphere_list [[buffer(2)]],
             constant Square* square_list [[buffer(3)]],
             constant Cube* cube_list [[buffer(4)]] )
{
    // Check if the pixel is within the bounds of the output texture
    if((pos_grid.x >= outTexture.get_width()) || (pos_grid.y >= outTexture.get_height()))
    {
        // Return early if the pixel is out of bounds
        return;
    }
    
    uint32_t rr = inSeedRNG.read(pos_grid).r;
    uint32_t gg = inSeedRNG.read(pos_grid).g;
    uint32_t bb = inSeedRNG.read(pos_grid).b;
    uint32_t aa = inSeedRNG.read(pos_grid).a;
    
    uint64_t rng_state = (uint64_t(rr) << 32) | gg;
    uint64_t rng_inc = (uint64_t(bb) << 32) | aa;
    
    auto cached_color = float3(inTexture.read(pos_grid).rgb);
    //auto float_time = float(sceneMeta->running_time);
    //auto int_time = uint32_t(1000*sceneMeta->running_time);
    auto frame_count = sceneMeta->sample_frame_count;
    //auto pixelPisition = input.texCoord*float2(sceneMeta->view_size);
    
    auto u = float(pos_grid.x)/outTexture.get_width();
    auto v = float(pos_grid.y)/outTexture.get_height();
    
    pcg32_random_t rng;
    
    rng.inc = rng_inc;
    rng.state = rng_state;
//        rng = { 0x853c49e6748fea9bULL, 0xda3e39cb94b95bdbULL };
//        pcg32_srandom_r(&rng, pos_grid.x * int_time, pos_grid.y * int_time);
//        pcg32_random_r(&rng);
    
    auto background = float3(0.0);
    auto result = float3(0.0);
    
    auto ray = castRay(camera, u, v, &rng);
    //ray.direction.x *= -1;
    auto color = traceColor(ray, 50,
                            background,
                            sphere_list,
                            square_list,
                            cube_list, &rng);

    result.rgb = (cached_color.rgb * frame_count + color) / (frame_count + 1);
    
    auto hhhh = half4(1.0);
    hhhh.rgb = half3(result);
    //hhhh.rgb = half3(ray.direction * 0.5 + 0.5);
    
    outTexture.write(hhhh, pos_grid);
    
    gg = rng.state;
    rr = rng.state >> 32;
    
    aa = rng.inc;
    bb = rng.inc >> 32;
    
    vec<uint32_t, 4> rng_cache;
    rng_cache.r = rr;
    rng_cache.g = gg;
    rng_cache.b = bb;
    rng_cache.a = aa;
    
    outSeedRNG.write(rng_cache, pos_grid);
}

