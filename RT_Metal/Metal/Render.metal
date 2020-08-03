#include "Render.hh"
#include "Random.hh"
#include "Tracer.metal"

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

constexpr sampler textureSampler (mag_filter::linear, min_filter::linear, mip_filter::linear);

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

    auto scaled = offset + float2(0.5);
    
    if (scaled.x < 0 || scaled.y < 0 || scaled.x > 1.0 || scaled.y > 1.0) { return float4(0.0); }
    
    auto tex_color = thisTexture.sample(textureSampler, scaled);

    auto this_mip = thisTexture.sample(textureSampler, float2(0.5), thisTexture.get_num_mip_levels());
    auto prev_mip = prevTexture.sample(textureSampler, float2(0.5), prevTexture.get_num_mip_levels());
    
    auto mix_mip = (this_mip.rgb + 0 * prev_mip.rgb);
    
    float luminance = dot(mix_mip, float3(0.2126, 0.7152, 0.0722));
    float mapped = clamp(CETone(luminance, 1.0f), 0.0, 0.96);
    float expose = 1.0 - mapped;
    
    tex_color.rgb = ACESTone(tex_color.rgb, expose);
    tex_color.rgb = LinearToSRGB(tex_color.rgb);
    
    return tex_color;
}

static float3 traceColor(float depth,
                         thread Ray& ray,
                         
                         texture2d<half, access::sample> ambientHDR,
                         
                         constant Sphere* sphere_list,
                         constant Square* square_list,
                         constant Cube* cube_list,
                         thread pcg32_random_t* seed)
{
    HitRecord hitRecord;
    ScatRecord scatRecord;
    
    float3 color = float3(0.0);
    float3 ratio = float3(1.0);
    float2 range_t = float2(0.01, FLT_MAX);
    
    Ray test_ray = ray;
    bool hitted = false;
    bool has_ray = false;
    
    do {
        hitted = false;
        has_ray = false;
        
        HitRecord hit_re;
        range_t = float2(0.01, FLT_MAX);
        
        for (int i=0; i<13; i++) {
            auto sphere = &sphere_list[i];
            if(sphere->hit_test(test_ray, range_t, hit_re)) {
                if (hit_re.t < range_t.y) {
                    range_t.y = hit_re.t;
                    hitRecord = hit_re;
                    hitted = true;
                }
            }
        }

        for (int i=0; i<6; i++) {
            auto square = &square_list[i];
            if(square->hit_test(test_ray, range_t, hit_re)) {
                if (hit_re.t < range_t.y) {
                    range_t.y = hit_re.t;
                    hitRecord = hit_re;
                    hitted = true;
                }
            }
        }

        for (int i=0; i<1; i++) {
            auto cube = &cube_list[i];
            if(cube->hit_test(test_ray, range_t, hit_re)) {
                if (hit_re.t < range_t.y) {
                    range_t.y = hit_re.t;
                    hitRecord = hit_re;
                    hitted = true;
                }
            }
        }
        
        if (!hitted) {
            float3 sphereVector = test_ray.origin + 1000000 * test_ray.direction;
            float2 uv = SampleSphericalMap(normalize(sphereVector) * float3(1, -1, -1));
            auto ambient = ambientHDR.sample(textureSampler, uv);
            color = ratio * float3(ambient.rgb);
            return color; //break;
        }
        
        float3 emit_color;
        auto emitted = emit_test(hitRecord, emit_color);
        if (emitted) {
            color = ratio * emit_color;
            return color;
        }
        
        has_ray = scatter(test_ray, hitRecord, scatRecord, seed);
        
        if(!has_ray) {
            return float3(0, 0, 0); //break;
        }
        
        ratio = ratio * scatRecord.attenuation;
        test_ray = scatRecord.specular;
        
        { // Russian Roulette
            float p = max(ratio.r, max(ratio.g, ratio.b));
            if (randomF(seed) > p)
                break;
            // Add the energy we 'lose' by randomly terminating paths
            ratio *= 1.0f / p;
        }
        
        depth -= 1;
        
    } while(has_ray && depth > 0);
    
    return color;
}

kernel void
tracerKernel(texture2d<half, access::read>  inTexture  [[texture(0)]],
             texture2d<half, access::write> outTexture [[texture(1)]],
             
             texture2d<uint32_t, access::read>  inRNG  [[texture(2)]],
             texture2d<uint32_t, access::write> outRNG [[texture(3)]],
             
             texture2d<half, access::sample> textureHDR [[texture(4)]],
             
             uint2 thread_pos  [[thread_position_in_grid]],
             constant SceneComplex* sceneMeta [[buffer(0)]],
             constant Camera* camera [[buffer(1)]],

             constant Sphere* sphere_list [[buffer(2)]],
             constant Square* square_list [[buffer(3)]],
             constant Cube* cube_list [[buffer(4)]] )
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

    auto result = float3(0.0);
    
    auto ray = castRay(camera, u, v, &rng);
    auto color = traceColor(32, ray,
                            textureHDR,
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
    
    outRNG.write(rng_cache, thread_pos);
}
