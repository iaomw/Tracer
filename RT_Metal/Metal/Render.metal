#include "Render.hh"
#include "Camera.hh"

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
    vector_float2 coordinate;
} VertexWithUV;

inline float2 SampleSphericalMap(float3 v)
{
    float2 uv = float2(atan2(v.z, v.x), asin(v.y));
    float2 invAtan = float2(0.1591, 0.3183);
    uv *= invAtan; uv += 0.5;
    return uv;
}

inline float3 LessThan(float3 f, float value)
{
    return float3(
        (f.x < value) ? 1.0f : 0.0f,
        (f.y < value) ? 1.0f : 0.0f,
        (f.z < value) ? 1.0f : 0.0f);
}
 
inline float3 LinearToSRGB(float3 rgb)
{
    rgb = clamp(rgb, 0.0f, 1.0f);
    return mix(
        pow(rgb, float3(1.0f / 2.4f)) * 1.055f - 0.055f,
        rgb * 12.92f,
        LessThan(rgb, 0.0031308f)
    );
}
 
inline float3 SRGBToLinear(float3 rgb)
{
    rgb = clamp(rgb, 0.0f, 1.0f);
    return mix(
        pow(((rgb + 0.055f) / 1.055f), float3(2.4f)),
        rgb / 12.92f,
        LessThan(rgb, 0.04045f)
    );
}

inline float3 ACESTone(float3 color, float adapted_lum)
{
    const float A = 2.51f;
    const float B = 0.03f;
    const float C = 2.43f;
    const float D = 0.59f;
    const float E = 0.14f;

    color *= adapted_lum;
    return (color * (A * color + B)) / (color * (C * color + D) + E);
}

inline float3 CETone(float3 color, float adapted_lum)
{
    return 1 - exp(-adapted_lum * color);
}

inline float CETone(float color, float adapted_lum)
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
    
    out.texCoord = vertexArray[vertexID].coordinate;
    
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
Spectrum traceBVH(float depth, thread Ray& ray, thread XSampler& xsampler,
                
                    constant PackageEnv& packageEnv,
                    constant PackagePBR& packagePBR,

                    constant Primitive&  primitives)
{
    HitRecord hitRecord;
    BxRecord scatRecord;
    
    Spectrum ratio = Spectrum(1.0);
    Spectrum color = Spectrum(0.0);
    
    Scene scene { primitives };
    
//    bool edge_hitted = false;
//    if ( edge_hitted ) { return float3(10); }
    
    bool hitted = scene.hit(ray, hitRecord, FLT_MAX);
    
    do { // each ray
        
        if ( !hitted ) {
            float3 sphereVector = ray.direction; //ray.origin + 65536 * ray.direction;
            float2 uv = SampleSphericalMap(normalize(sphereVector));
            auto ambient = packageEnv.texHDR.sample(textureSampler, uv);
            color += ratio * ambient.rgb; break;
        }
        
        if ( packageEnv.materials[hitRecord.material].type == MaterialType::Diffuse ) {
            auto le = packageEnv.materials[hitRecord.material].textureInfo.albedo;
            auto w = dot(-ray.direction, -hitRecord.gn);
            return ratio * le * abs(w);
        }
        
        MediumInteraction mi;
        
        if (ray.medium == MediumType::Homogeneous) {
            
            auto homo = HomogeneousMedium(0.02, 0.08, 0.5);
            ratio *= homo.Sample(ray, hitRecord, &mi, xsampler);
        }
        else if (ray.medium == MediumType::GridDensity) {
            
            GridDensityMedium dMedium { packageEnv.densityInfo, packageEnv.densityArray };
            ratio *= dMedium.Sample(hitRecord._r, hitRecord, &mi, xsampler);
        }
        
        bool need_test = false;
        bool need_bsdf = false;
        
        if (mi.homogen != nullptr || mi.density != nullptr) {

            float3 wi, wo = -ray.direction;
            HenyeyGreenstein(mi.phaseG).Sample_p(wo, wi, xsampler.sample2D());
            
            ray.update(mi.p, wi);
            ray.medium = packageEnv.materials[hitRecord.material].medium;
            
            need_test = true;
            
        } else {
            
            if (packageEnv.materials[hitRecord.material].type == MaterialType::_NIL_) {
                
                if (dot(ray.direction, hitRecord.gn) < 0) { // enter
                    //ray = Ray(hitRecord.p - 0.01*hitRecord.gn, ray.direction);
                    ray = Ray(offset_ray(hitRecord.p, -hitRecord.gn), ray.direction);
                    ray.medium = packageEnv.materials[hitRecord.material].medium;
                }
                else { // depart
                    //ray = Ray(hitRecord.p + 0.01*hitRecord.gn, ray.direction);
                    ray = Ray(offset_ray(hitRecord.p, hitRecord.gn), ray.direction);
                    ray.medium = MediumType::_NIL_;
                }

                need_test = true;
            } //Volume
            else {
                
                need_test = false;
                need_bsdf = true;
            }
        }
        
        if (need_test) {
            hitted = scene.hit(ray, hitRecord, FLT_MAX);
        }
        
        if (!need_bsdf) { continue; }
        
        LightSampleRecord lsr;
        float2 uu = xsampler.sample2D();
        
        const auto $origin = hitRecord.p;
        auto _origin = offset_ray(hitRecord.p, hitRecord.sn);
        //auto _origin = hitRecord.p + hitRecord.sn / 4096;
        
        if (xsampler.random() < 0.5) {
            primitives.squareList[5].sample(uu, _origin, lsr);
        } else {
            primitives.squareList[6].sample(uu, _origin, lsr);
        }

        auto _dir = lsr.p - _origin;
        auto _nor = normalize(_dir);
        
        float3 nx, ny;
        CoordinateSystem(hitRecord.sn, nx, ny);
        float3x3 stw = { nx, ny, hitRecord.sn };
        float3x3 wts = transpose(stw);
        
        const auto _tr = 1.0;
        const auto _dis = length(_dir);
        const auto _ray = Ray(_origin, _nor); HitRecord shr;
        const auto blocked = scene.hit(_ray, shr, _dis, true);

        if( !blocked ) { // Light Sampling

            auto wo = wts * (-ray.direction);
            auto wi = wts * (_ray.direction); float bxPDF;

            float3 weight = packageEnv.materials[hitRecord.material].F(wo, wi, hitRecord.uv, bxPDF, uu);

            auto cosOnLight = abs( dot(lsr.n, -_nor) );
            
            auto Li = packageEnv.materials[lsr.material].textureInfo.albedo;
            weight *= Li * cosOnLight;

            auto dist2 = _dis * _dis; //distance_squared(lsr.p, _origin);
            auto liPDF = dist2 * lsr.areaPDF / cosOnLight;

            weight *= PowerHeuristic(1, liPDF, 1, bxPDF);
            color += _tr * ratio * weight / liPDF;
        }

        // BXDF Sampling
        float3 wi; float bxPDF;
        float3 wo = wts * (-ray.direction);
        
        //uu = xsampler.sample2D();
        
        scatRecord.attenuation = packageEnv.materials[hitRecord.material].S_F(wo, wi, hitRecord.uv, uu, bxPDF);
        scatRecord.bxPDF = bxPDF;
        
        if (bxPDF <= 0) {break;}
        
        if (wi.z < 0) { // Transmission
            
            wi = stw * wi;
            ray.update(offset_ray($origin - 2 * hitRecord.sn * SquarePadding, -hitRecord.sn), wi);
            //ray.update(_origin - hitRecord.sn * 0.02, wi);
            
            if (dot(wi, hitRecord.gn) < 0) { // enter
                //if (packageEnv.materials[hitRecord.material].medium != MediumType::_NIL_) {
                    ray.medium = packageEnv.materials[hitRecord.material].medium;
                //} else { ray.medium = MediumType::_NIL_; }
            } else { // depart
                ray.medium = MediumType::_NIL_;
            }
            
        } else { // do not change medium
            ray.update(_origin, stw * wi);
        }
        
        //break;
        
        ratio *= scatRecord.attenuation / scatRecord.bxPDF;
        
        { // Russian Roulette
            float3 xyz; RGBToXYZ(ratio, xyz);
            float p = xyz.y;   //max3(ratio);
            if (xsampler.random() > p) break;
            // Add the energy we 'lose'
            ratio *= 1.0f / p;
        }
        
        hitted = scene.hit(ray, hitRecord, FLT_MAX);
        
        if (hitted && packageEnv.materials[hitRecord.material].type == MaterialType::Diffuse) {
                
            auto Li = packageEnv.materials[hitRecord.material].textureInfo.albedo;
            auto cosOnLight = dot(-ray.direction, hitRecord.sn);
            
            auto weight = scatRecord.attenuation * Li * cosOnLight;
            
            auto dist2 = distance_squared(hitRecord.p, ray.origin);
            auto lightPDF =  hitRecord.PDF * dist2 / cosOnLight;
            
            weight *= PowerHeuristic(1, scatRecord.bxPDF, 1, lightPDF);
            color += ratio * weight / scatRecord.bxPDF;
            
            break;
        }
        
    } while( (--depth) > 0 );
    
    return color;
}

kernel void
tracerKernel(texture2d<float, access::read>       inTexture [[texture(0)]],
             texture2d<float, access::write>     outTexture [[texture(1)]],
             
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
    //auto ss = pbrt::SobolSampler(rng, frame, thread_pos, vsize);
    
    color = traceBVH(10, ray, rs,
                        packageEnv,
                        packagePBR[1],
                        primitives);
    
    auto bad = isinf(color) || isnan(color);
    
    if( bad[0] || bad[1] || bad[2]) {
        color = float3(0);
    }
    
    float3 cached = float3( inTexture.read( thread_pos ).rgb );
    float3 result = (cached.rgb * frame + color) / (frame + 1);
    
    outTexture.write(float4(result, 1.0), thread_pos);
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
