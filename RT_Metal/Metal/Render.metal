#include "Render.hh"
#include "Random.hh"
#include "Tracer.metal"

typedef enum  {
    VertexInputIndexVertices = 0,
    VertexInputIndexViewSize = 1
} VertexInput;

typedef struct  {
    
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

static float3 traceColor(float depth,
                         thread Ray& ray,
                         thread float3& ambient,
                         constant Sphere* sphere_list,
                         constant Square* square_list,
                         constant Cube* cube_list,
                         thread pcg32_random_t* seed)
{
    HitRecord hitRecord;
    ScatterRecord scatterRecord;
    
    float3 color = float3(0.0);
    float3 ratio = float3(1.0);
    float2 range_t = float2(0.01, FLT_MAX);
    
    Ray test_ray = ray;
    bool has_ray = false;
    
    do {
        has_ray = false;
        HitRecord hit_re;
        range_t = float2(0.01, FLT_MAX);
        
        bool hitted = false;
        
        for (int i=0; i<1; i++) {
            auto sphere = sphere_list[i];
            if(sphere.hit_test(test_ray, range_t, hit_re)) {
                if (hit_re.t < range_t.y) {
                    range_t.y = hit_re.t;
                    hitRecord = hit_re;
                    hitted = true;
                }
            }
        }
        
        for (int i=0; i<6; i++) {
            auto square = square_list[i];
            if(square.hit_test(test_ray, range_t, hit_re)) {
                if (hit_re.t < range_t.y) {
                    range_t.y = hit_re.t;
                    hitRecord = hit_re;
                    hitted = true;
                }
            }
        }
        
        for (int i=0; i<2; i++) {
            auto cube = cube_list[i];
            if(cube.hit_test(test_ray, range_t, hit_re)) {
                if (hit_re.t < range_t.y) {
                    range_t.y = hit_re.t;
                    hitRecord = hit_re;
                    hitted = true;
                }
            }
        }
        
        if (!hitted) {
            color += ratio * ambient;
            break;
        }
        
        float3 emit_color;
        auto emitted = emit_test(hitRecord, emit_color);
        if (emitted) {
            color += ratio * emit_color;
            return color;
        }
        
        has_ray = scatter(test_ray, hitRecord, scatterRecord, seed);
        
        if(!has_ray) { break; }
        
        ratio = ratio * scatterRecord.attenuation;
        test_ray = scatterRecord.specular;
        
        depth -= 1;
        
    } while(has_ray && depth > 0);
    
    return color;
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
             
             uint2 thread_pos  [[thread_position_in_grid]],
             constant SceneComplex* sceneMeta [[buffer(0)]],
             constant Camera* camera [[buffer(1)]],

             constant Sphere* sphere_list [[buffer(2)]],
             constant Square* square_list [[buffer(3)]],
             constant Cube* cube_list [[buffer(4)]] )
{
    // Check if the pixel is within the bounds of the output texture
    if((thread_pos.x >= outTexture.get_width()) || (thread_pos.y >= outTexture.get_height()))
    {
        // Return early if the pixel is out of bounds
        return;
    }
    
    uint32_t rr = inSeedRNG.read(thread_pos).r;
    uint32_t gg = inSeedRNG.read(thread_pos).g;
    uint32_t bb = inSeedRNG.read(thread_pos).b;
    uint32_t aa = inSeedRNG.read(thread_pos).a;
    
    uint64_t rng_state = (uint64_t(rr) << 32) | gg;
    uint64_t rng_inc = (uint64_t(bb) << 32) | aa;
    
    auto cached_color = float3(inTexture.read(thread_pos).rgb);
    auto frame_count = sceneMeta->frame_count;
    
    //auto float_time = float(sceneMeta->running_time);
    //auto int_time = uint32_t(1000*sceneMeta->running_time);
    //auto pixelPisition = input.texCoord*float2(sceneMeta->view_size);
    
    auto u = float(thread_pos.x)/outTexture.get_width();
    auto v = float(thread_pos.y)/outTexture.get_height();
    
    pcg32_random_t rng;
    
    rng.inc = rng_inc;
    rng.state = rng_state;
    
    auto ambient = float3(0.0);
    auto result = float3(0.0);
    
    auto ray = castRay(camera, u, v, &rng);
    auto color = traceColor(64, ray,
                            ambient,
                            sphere_list,
                            square_list,
                            cube_list, &rng);

    result.rgb = (cached_color.rgb * frame_count + color) / (frame_count + 1);
    
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
    
    outSeedRNG.write(rng_cache, thread_pos);
}

