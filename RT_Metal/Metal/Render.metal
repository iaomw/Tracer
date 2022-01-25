#include "Render.hh"
#include "Camera.hh"

typedef enum  {
    VertexInputIndexVertices = 0,
    VertexInputIndexViewSize = 1
} VertexInput;

struct RasterizerData {
    float4 position [[position]];
    float2 texCoord;
};

struct VertexWithUV {
    float2 position;
    float2 coordinate;
};

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
Spectrum traceVolume(float depth, thread Ray& ray, thread XSampler& xsampler,
                
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
            ray.update(offset_ray($origin, -hitRecord.sn), wi);
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

template <typename XSampler>
Spectrum traceMIS(float depth, thread Ray& ray, thread XSampler& xsampler,
                
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
            float3 sphereVector = ray.direction;
            float2 uv = SampleSphericalMap(normalize(sphereVector));
            auto ambient = packageEnv.texHDR.sample(textureSampler, uv);
            color += ratio * ambient.rgb; break;
        }
        
        if ( packageEnv.materials[hitRecord.material].type == MaterialType::Diffuse ) {
            auto le = packageEnv.materials[hitRecord.material].textureInfo.albedo;
            auto w = dot(-ray.direction, -hitRecord.gn);
            return ratio * le * abs(w);
        }
        
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
            ray.update(offset_ray($origin, -hitRecord.sn), wi);
            //ray.update(_origin - hitRecord.sn * 0.02, wi);
        } else { // do not change medium
            ray.update(_origin, stw * wi);
        }
        
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

template <typename XSampler>
Spectrum tracePath(float depth, thread Ray& ray, thread XSampler& xsampler,
                
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
            float3 sphereVector = ray.direction;
            float2 uv = SampleSphericalMap(normalize(sphereVector));
            auto ambient = packageEnv.texHDR.sample(textureSampler, uv);
            color += ratio * ambient.rgb; break;
        }
        
        if ( packageEnv.materials[hitRecord.material].type == MaterialType::Diffuse ) {
            auto le = packageEnv.materials[hitRecord.material].textureInfo.albedo;
            auto w = dot(-ray.direction, -hitRecord.gn);
            return ratio * le * abs(w);
        }
        
        float2 uu = xsampler.sample2D();
        
        const auto $origin = hitRecord.p;
        auto _origin = offset_ray(hitRecord.p, hitRecord.sn);
        //auto _origin = hitRecord.p + hitRecord.sn / 4096;
        
        float3 nx, ny;
        CoordinateSystem(hitRecord.sn, nx, ny);
        float3x3 stw = { nx, ny, hitRecord.sn };
        float3x3 wts = transpose(stw);

        // BXDF Sampling
        float3 wi; float bxPDF;
        float3 wo = wts * (-ray.direction);
        
        //uu = xsampler.sample2D();
        
        scatRecord.attenuation = packageEnv.materials[hitRecord.material].S_F(wo, wi, hitRecord.uv, uu, bxPDF);
        scatRecord.bxPDF = bxPDF;
        if (bxPDF <= 0) {break;}
        
        if (wi.z < 0) { // Transmission
            
            wi = stw * wi;
            ray.update(offset_ray($origin, -hitRecord.sn), wi);
            //ray.update(_origin - hitRecord.sn * 0.02, wi);
        } else { // do not change medium
            ray.update(_origin, stw * wi);
        }
        
        ratio *= scatRecord.attenuation / max(FLT_EPSILON, scatRecord.bxPDF);
        
        { // Russian Roulette
            float3 xyz; RGBToXYZ(ratio, xyz);
            float p = xyz.y;   //max3(ratio);
            if (xsampler.random() > p) break;
            // Add the energy we 'lose'
            ratio *= 1.0f / p;
        }
        
        hitted = scene.hit(ray, hitRecord, FLT_MAX);
        
    } while( (--depth) > 0 );
    
    return color;
}


kernel void
kernelPathTracing(texture2d<float, access::read>       inTexture [[texture(0)]],
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
    
    color = tracePath(8, ray, rs,
                        packageEnv,
                        packagePBR[1],
                        primitives);
    
    auto bad = isinf(color) || isnan(color);
    if( any(bad) ) { color = float3(0); }
    
    float3 cached = float3( inTexture.read( thread_pos ).rgb );
    float3 result = (cached.rgb * frame + color) / (frame + 1);
    
    outTexture.write(float4(result, 1.0), thread_pos);

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


template <typename XSampler>
bool traceCameraRecord(float depth, thread Ray& ray, thread XSampler& xsampler,
             
                       thread CameraRecord& cr, thread float3& directLightToCamera,
                
                       constant PackageEnv& packageEnv,
                       constant PackagePBR& packagePBR,
                       constant Primitive&  primitives)
{
    
    HitRecord hitRecord;
    BxRecord scatRecord;
    
    Spectrum ratio = Spectrum(1.0);
    
    Scene scene { primitives };
    
    bool hitted = scene.hit(ray, hitRecord, FLT_MAX);
    
    if ( !hitted ) {
        float3 sphereVector = ray.direction;
        float2 uv = SampleSphericalMap(normalize(sphereVector));
        auto ambient = packageEnv.texHDR.sample(textureSampler, uv);
        directLightToCamera = ratio * ambient.rgb;
        return false;
    }
    
    if ( packageEnv.materials[hitRecord.material].type == MaterialType::Diffuse ) {
        auto le = packageEnv.materials[hitRecord.material].textureInfo.albedo;
        auto w = dot(-ray.direction, -hitRecord.gn);
        directLightToCamera = ratio * le * abs(w);
        return false;
    }
    
    do { // each ray
        
        if (!packageEnv.materials[hitRecord.material].specular) {
            
            cr.ratio = ratio;
            cr.position = hitRecord.p;
            cr.direction = ray.direction;
            
            return true;
        }
        
        float2 uu = xsampler.sample2D();
        
        const auto $origin = hitRecord.p;
        auto _origin = offset_ray(hitRecord.p, hitRecord.sn);
        
        float3 nx, ny;
        CoordinateSystem(hitRecord.sn, nx, ny);
        float3x3 stw = { nx, ny, hitRecord.sn };
        float3x3 wts = transpose(stw);

        // BXDF Sampling
        float3 wi; float bxPDF;
        float3 wo = wts * (-ray.direction);
        
        //uu = xsampler.sample2D();
        
        scatRecord.attenuation = packageEnv.materials[hitRecord.material].S_F(wo, wi, hitRecord.uv, uu, bxPDF);
        scatRecord.bxPDF = bxPDF;
        if (bxPDF <= 0) {break;}
        
        if (wi.z < 0) { // Transmission
            
            wi = stw * wi;
            ray.update(offset_ray($origin, -hitRecord.sn), wi);
            //ray.update(_origin - hitRecord.sn * 0.02, wi);
        } else { // do not change medium
            ray.update(_origin, stw * wi);
        }
        
        ratio *= scatRecord.attenuation / max(FLT_EPSILON, scatRecord.bxPDF);
        
        { // Russian Roulette
            float3 xyz; RGBToXYZ(ratio, xyz);
            float p = xyz.y;   //max3(ratio);
            if (xsampler.random() > p) break;
            // Add the energy we 'lose'
            ratio *= 1.0f / p;
        }
        
        hitted = scene.hit(ray, hitRecord, FLT_MAX);
        
    } while( (--depth) > 0 && hitted );
    
    return false;
}

constant bool deviceSupportsNonuniformThreadgroups [[ function_constant(0) ]];

kernel void
kernelCameraRecording(texture2d<float, access::read>       inTexture [[texture(0)]],
                      texture2d<float, access::write>     outTexture [[texture(1)]],
             
                      texture2d<uint32_t, access::read>       inRNG [[texture(2)]],
                      texture2d<uint32_t, access::write>     outRNG [[texture(3)]],
             
                      uint2 thread_pos       [[thread_position_in_grid]],
             
                      constant Camera*              camera [[buffer(0)]],
                      constant Complex*            complex [[buffer(1)]],
                      
                      device CameraRecord*    cameraRecord [[buffer(2)]],
                      device AABB*              cameraAABB [[buffer(3)]],
             
                      constant Primitive&   primitives [[buffer(7)]],
                      constant PackageEnv&  packageEnv [[buffer(8)]],
                      constant PackagePBR*  packagePBR [[buffer(9)]])
{
    #ifndef DEVICE_SUPPORTS_NON_UNIFORM_TREADGROUPS
    if (thread_pos.x >= inRNG.get_width() || thread_pos.y >= inRNG.get_height()) {
        return;
    }
    #endif
    
    if (!deviceSupportsNonuniformThreadgroups) {
        if (thread_pos.x >= inRNG.get_width() || thread_pos.y >= inRNG.get_height()) {
            return;
        }
    }
    
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
    
    CameraRecord cr; float3 directLightToCamera;
    
    bool hasCameraRecord = traceCameraRecord(8, ray, rs,
                                             cr, directLightToCamera,
                                             packageEnv,
                                             packagePBR[1],
                                             primitives);
    
//    auto bad = isinf(color) || isnan(color);
//    if( any(bad) ) { color = float3(0); }
    auto idx = thread_pos.x + thread_pos.y * outTexture.get_width();
    
    if (hasCameraRecord) {
        
        cameraRecord[idx] = cr;
        cameraAABB[idx] = {cr.position, cr.position};
        
    } else {
        
        cameraAABB[idx] = {FLT_MAX, -FLT_MAX};
        
        color = directLightToCamera;
        
        float3 cached = float3( inTexture.read( thread_pos ).rgb );
        float3 result = (cached.rgb * frame + color) / (frame + 1);
        
        outTexture.write(float4(result, 1.0), thread_pos);
    }
    
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
};

kernel void
kernelCameraReducing(constant Camera*           camera [[buffer(0)]],
                     constant Complex*         complex [[buffer(1)]],
                     
                     constant uint*              bound [[buffer(2)]],
                     
                     device AABB*               inAABB [[buffer(3)]],
                     device AABB*              outAABB [[buffer(4)]],
                     
                     //uint2 thread_pos   [[thread_position_in_grid]]
                     uint thread_idx       [[ thread_position_in_grid ]],
                     uint group_idx   [[ threadgroup_position_in_grid ]],
                     uint local_idx [[ thread_position_in_threadgroup ]],
                     
                     uint grid_size               [[ threads_per_grid ]],
                     uint group_size       [[ threads_per_threadgroup ]] )
{
    threadgroup AABB shared_memory[256];
    
    if ( (thread_idx + grid_size) < bound[0] ) {
        
        thread auto& a = inAABB[thread_idx];
        thread auto& b = inAABB[thread_idx + grid_size];

        shared_memory[local_idx].mini = min(a.mini, b.mini);
        shared_memory[local_idx].maxi = max(a.maxi, b.maxi);
        
    } else {
        shared_memory[local_idx] = inAABB[thread_idx];
    }
    
    threadgroup_barrier(mem_flags::mem_none);
    // reduction in shared memory
    for (uint stride = group_size / 2; stride > 0; stride >>= 1) {
        
        if (local_idx < stride) {
            
            threadgroup auto& a = shared_memory[local_idx];
            threadgroup auto& b = shared_memory[local_idx + stride];
            
            shared_memory[local_idx].mini = min(a.mini, b.mini);
            shared_memory[local_idx].maxi = max(a.maxi, b.maxi);
        }
        threadgroup_barrier(mem_flags::mem_none);
    }
    
    if (0 == local_idx) {
        outAABB[group_idx] = shared_memory[0];
    }
}

inline pcg32_t toRNG(thread vec<uint32_t, 4> &rng_cache) {
    //return as_type<pcg32_t>(rng_cache);
    
    uint32_t rr = rng_cache.r;
    uint32_t gg = rng_cache.g;
    uint32_t bb = rng_cache.b;
    uint32_t aa = rng_cache.a;
    
    uint64_t rng_state = (uint64_t(rr) << 32) | gg;
    uint64_t rng_inc = (uint64_t(bb) << 32) | aa;
    return pcg32_t { rng_inc, rng_state };
}

inline vec<uint32_t, 4> exRNG(thread pcg32_t &rng) {
    //return as_type<vec<uint32_t, 4>>(rng);
    
    vec<uint32_t, 4> rng_cache;
    
    rng_cache.r = rng.state >> 32;
    rng_cache.g = rng.state;
    rng_cache.b = rng.inc >> 32;
    rng_cache.a = rng.inc;
    
    return rng_cache;
}

template <typename XSampler>
void tracePhotonRecord(thread Ray& ray, thread XSampler& xsampler,
             
                       thread PhotonRecord& pr,
                
                       constant PackageEnv& packageEnv,
                       constant PackagePBR& packagePBR,
                       constant Primitive&  primitives)
{
    
    HitRecord hitRecord;
    BxRecord scatRecord;
    
    Spectrum ratio = Spectrum(1.0);
    
    Scene scene { primitives };
    bool hitted = scene.hit(ray, hitRecord, FLT_MAX);
    
    constant auto& material = packageEnv.materials[hitRecord.material];
    
    if ( !hitted || material.type == MaterialType::Diffuse ) {
        pr.reset();
        return;
    }
    
        float2 uu = xsampler.sample2D();
        
        float3 nx, ny;
        CoordinateSystem(hitRecord.sn, nx, ny);
        float3x3 stw = { nx, ny, hitRecord.sn };
        float3x3 wts = transpose(stw);

        // BXDF Sampling
        float3 wi; float bxPDF;
        float3 wo = wts * (-ray.direction);
        
        //uu = xsampler.sample2D();
        scatRecord.attenuation = material.S_F(wo, wi, hitRecord.uv, uu, bxPDF);
        scatRecord.bxPDF = bxPDF;
    
        if (bxPDF <= 0) {
            pr.reset();
            return;
        }
        
        wi = stw * wi;
        
        ratio *= scatRecord.attenuation / max(FLT_EPSILON, scatRecord.bxPDF);
        
        { // Russian Roulette
            float3 xyz; RGBToXYZ(ratio, xyz);
            float p = xyz.y;   //max3(ratio);
            if (xsampler.random() > p) {
                pr.reset();
                return;
            }
            // Add the energy we 'lose'
            ratio *= 1.0f / p;
        }
        
        pr.position = offset_ray(hitRecord.p, copysign(hitRecord.sn, wi.z));
        pr.normal = hitRecord.sn;
        pr.direction = wi;
        pr.power = ratio;

        pr.step += 1;
        
        pr.valid = !material.specular;
}
  
kernel void
kernelPhotonRecording(texture2d<uint32_t, access::read>       inRNG [[texture(2)]],
                      texture2d<uint32_t, access::write>     outRNG [[texture(3)]],
             
                      uint2 thread_pos         [[thread_position_in_grid]],
                      uint2 group_size         [[threads_per_threadgroup]],
                      uint2 grid_size                 [[threads_per_grid]],
             
                      constant Camera*              camera [[buffer(0)]],
                      constant Complex*            complex [[buffer(1)]],
                      
                      device PhotonRecord*     photonRecord [[buffer(2)]],
             
                      constant Primitive&   primitives [[buffer(7)]],
                      constant PackageEnv&  packageEnv [[buffer(8)]],
                      constant PackagePBR*  packagePBR [[buffer(9)]])
{
    #ifndef DEVICE_SUPPORTS_NON_UNIFORM_TREADGROUPS
    if (thread_pos.x >= inRNG.get_width() || thread_pos.y >= inRNG.get_height()) {
        return;
    }
    #endif
    
    uint idx = thread_pos.y * grid_size.x + thread_pos.x;
    auto photon_cache = photonRecord[idx];
    
    auto frame = complex->frame_count;
    
    auto rng_cache = inRNG.read(thread_pos);
    pcg32_t rng = toRNG(rng_cache);
    RandomSampler rs { &rng };
    
    Ray ray;
    
    bool check = frame == 0 || !photon_cache.valid || photon_cache.step > 8;
    
    if (check) { // ray from light source
        
        photon_cache.step = 0;
        
        LightSampleRecord lsr;
        float2 uu = rs.sample2D();
        auto _origin = float3(0.0);
        
        if (rs.random() < 0.5) {
            primitives.squareList[5].sample(uu, _origin, lsr);
        } else {
            primitives.squareList[6].sample(uu, _origin, lsr);
        }
        
        float3 nx, ny;
        CoordinateSystem(lsr.n, nx, ny);
        float3x3 stw = { nx, ny, lsr.n };
        //float3x3 wts = transpose(stw);
        ray = Ray(lsr.p, stw * UniformSampleHemisphere(uu));
    }
    else { // ray from previous photon position
        //thread auto& photon = photonRecord[idx];
        ray = Ray(photon_cache.position, photon_cache.direction);
    }
    
    tracePhotonRecord(ray, rs, photon_cache,
                      packageEnv,
                      packagePBR[1],
                      primitives);
    
    photonRecord[idx] = photon_cache;
    outRNG.write(exRNG(rng), thread_pos);
};

kernel void
kernelPhotonHashing(constant Complex*              complex [[buffer(1)]],
                    constant PhotonRecord*    photonRecord [[buffer(2)]],
                    
                    device float4*            photonHashed [[buffer(3)]],
                    device float*             photonRadius [[buffer(4)]],
                      
                    uint2 thread_pos         [[thread_position_in_grid]],
                    uint2 group_size         [[threads_per_threadgroup]],
                    uint2 grid_size                 [[threads_per_grid]] )
{
    size_t thread_idx = thread_pos.y * grid_size.x + thread_pos.x;
    float3 position = photonRecord[thread_idx].position;
    
    float3 HashIndex = floor((position - complex->photonBox.mini) * complex->photonHashScale);
    
    auto tmp = hash( HashIndex, complex->photonHashScale, grid_size[0]*grid_size[1] );
    
    float4 tttt = { grid_size[0]/1.0f, grid_size[1]/1.0f, 1.0f/grid_size[0], 1.0f/grid_size[1] };
    
    float2 relative_pos = (float2)thread_pos / (float2)grid_size;
    photonHashed[thread_idx] = float4(relative_pos.xy, convert1Dto2D(tmp, tttt));
    
    photonRadius[thread_idx] = complex->photonInitialRadius;
}

kernel void
kernelPhotonParams(device Complex*              complex [[buffer(0)]],
                   device AABB*                 _bounds [[buffer(1)]])
{
    device auto* x = complex;
    
    x->photonBox = _bounds[0];
    x->photonBoxSize = x->photonBox.maxi - x->photonBox.mini;
    
    x->photonInitialRadius = dot(x->photonBoxSize, float3(1.0/3.0));
    x->photonInitialRadius *= 3.0 / 1024;
    
    x->photonBox.mini -= float3(x->photonInitialRadius);
    x->photonBox.maxi += float3(x->photonInitialRadius);
    
    x->photonHashScale = 1.0f / (x->photonInitialRadius * 1.5);
}

kernel void
kernelPhotonRadius(constant Complex*              complex [[buffer(0)]],
                   device float*                  _radius [[buffer(1)]],
                   
                   //uint thread_pos          [[thread_position_in_grid]])
                   uint2 thread_pos         [[thread_position_in_grid]],
                   uint2 group_size         [[threads_per_threadgroup]],
                   uint2 grid_size                 [[threads_per_grid]])
{
    size_t thread_idx = thread_pos.y * grid_size.x + thread_pos.x;
    _radius[thread_idx] = complex->photonInitialRadius;
}

typedef struct
{
    vector_float2 position;
    vector_float4 color;
} AAPLSimpleVertex;

// Vertex shader outputs and fragment shader inputs for simple pipeline
struct SimplePipelineRasterizerData
{
    float4 position [[position]];
    float4 PhotonIndex;
};

// Vertex shader which passes position and color through to rasterizer.
vertex SimplePipelineRasterizerData
simpleVertexShader(const uint vertexID [[ vertex_id ]],
                   constant float4 *vertices [[ buffer(0) ]])
{
    SimplePipelineRasterizerData out;
    
    auto element = vertices[vertexID];
    auto PhotonListIndex = element.zw;
    
    out.position = float4(PhotonListIndex * 2.0 * (1.0/512) - 1.0, 0.5, 1.0);
    out.PhotonIndex = element; //float4(1.0, 1.0, 1.0, 1.0);

    return out;
}

struct SimplePiplelineFragment {
    float4 PhotonIndex [[color(0)]];
    half count [[color(1)]];
};

// Fragment shader that just outputs color passed from rasterizer.
fragment SimplePiplelineFragment
simpleFragmentShader(SimplePipelineRasterizerData in [[stage_in]])
{
    SimplePiplelineFragment result;
    
    result.PhotonIndex = in.PhotonIndex;
    result.count = 1;
    
    return result;
}
