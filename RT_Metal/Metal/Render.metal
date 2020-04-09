
#include <metal_stdlib>
using namespace metal;

#include "Random.hh"
#include "Render.hh"
#include "Tracer.metal"

typedef struct  {
    float2 view_size;
    float running_time;
    uint sample_frame_count;
    
} SceneMeta;

typedef struct {
    float4 position [[position]];
    float2 texCoord;
} RasterizerData;

typedef struct {
    vector_float2 position;
    vector_float2 textureCoordinate;
} VertexWithUV;

static float3 traceColor(Ray ray,
                         float depth,
                         thread float3& background,
                         constant Sphere* sphere_list,
                         constant Square* square_list,
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
        float nearst_t = FLT_MAX;
        
        for (int i=0; i<6; i++) {
            auto square = square_list[i];
            if(square.hit(testRay, range_t, testHitRecord)) {
                if (testHitRecord.t < nearst_t) {
                    nearst_t = testHitRecord.t;
                    hitRecord = testHitRecord;
                }
            }
        }
        
        if (nearst_t == FLT_MAX) {
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

                constant SceneMeta* sceneMeta [[buffer(0)]],
                constant Camera* camera [[buffer(1)]],

                constant Sphere* sphere_list [[buffer(2)]],
                constant Square* square_list [[buffer(3)]],
                constant Cube* cube_list [[buffer(4)]] )

{
    constexpr sampler textureSampler (mag_filter::linear, min_filter::linear);
    input.texCoord.y = 1.0 - input.texCoord.y;
    auto colorSample = theTexture.sample(textureSampler, input.texCoord);
    
    return colorSample;
}

kernel void
tracerKernel(texture2d<half, access::read>  inTexture  [[texture(0)]],
             texture2d<half, access::write> outTexture [[texture(1)]],
             
             uint2 pos_grid  [[thread_position_in_grid]],
             constant SceneMeta* sceneMeta [[buffer(0)]],
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
    
    auto previous_color = float3(inTexture.read(pos_grid).rgb);
    //auto float_time = float(sceneMeta->running_time);
    auto frame_count = float(sceneMeta->sample_frame_count);
    //auto pixelPisition = input.texCoord*float2(sceneMeta->view_size);
    
    auto u = float(pos_grid.x)/outTexture.get_width();
    auto v = float(pos_grid.y)/outTexture.get_height();
    
    auto int_time = uint32_t(sceneMeta->running_time*1000);
    
    pcg32_random_t rng;
    pcg32_srandom_r(&rng, pos_grid.x*int_time, pos_grid.y*int_time);
    
    auto background = float3(0.0, 0.0, 0.0);
    auto result = float3(0.0, 0.0, 0.0);
    
    auto n = 4;
    for (int i=0; i<n; i++) {
        auto ray = castRay(camera, u, v, &rng);
        auto color = traceColor(ray, 50, background, sphere_list, square_list, &rng);
        result.rgb += color;
    }
    
    result.rgb = (previous_color.rgb*frame_count + result.rgb) / (frame_count + n);
    //result.rgb = (previous_color.rgb + result.rgb) / (1 + n);
    
    auto hhhh = half4(1.0);
    hhhh.rgb = half3(result);
    
    outTexture.write(hhhh, pos_grid);
}

