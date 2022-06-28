#include "Photon.hh"

template <typename XSampler>
bool traceCameraRecord(float depth, thread Ray& ray, thread XSampler& xsampler,
             
                       device CameraRecord& cr, thread float3& directLightToCamera,
                
                       constant PackageEnv& packageEnv,
                       constant PackagePBR& packagePBR,
                       constant Primitive&  primitives)
{
    HitRecord hitRecord;
    BxRecord scatRecord;
    
    Scene scene { primitives };
    
    Spectrum ratio = Spectrum(1.0);
    
    bool hitted = scene.hit(ray, hitRecord, FLT_MAX);
    
    if ( !hitted ) {
        float3 sphereVector = ray.direction;
        float2 uv = SampleSphericalMap(normalize(sphereVector));
        auto ambient = packageEnv.texHDR.sample(textureSampler, uv);
        directLightToCamera = ratio * ambient.rgb;
        return false;
    } // miss hit
    
    if ( packageEnv.materials[hitRecord.material].type == MaterialType::Diffuse ) {
        auto le = packageEnv.materials[hitRecord.material].textureInfo.albedo;
        auto w = dot(-ray.direction, -hitRecord.gn);
        directLightToCamera = ratio * le * abs(w);
        return false;
    } // light source
    
    do { // each ray
        
        if ( packageEnv.materials[hitRecord.material].specular == false ) {
            
            cr.valid = true;
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

//constant bool deviceSupportsNonuniformThreadgroups [[ function_constant(0) ]];

kernel void
kernelCameraRecording(texture2d<float, access::read>       inTexture [[texture(0)]],
                      texture2d<float, access::write>     outTexture [[texture(1)]],
             
                      texture2d<uint32_t, access::read>        inRNG [[texture(2)]],
                      texture2d<uint32_t, access::write>      outRNG [[texture(3)]],
             
                      uint2 thread_pos                  [[thread_position_in_grid]],
                      uint2 group_size                  [[threads_per_threadgroup]],
                      uint2 grid_size                   [[threads_per_grid]],
             
                      constant Camera*              camera [[buffer(0)]],
                      constant Complex*            complex [[buffer(1)]],
                      
                      device AABB*              cameraAABB [[buffer(2)]],
                      device CameraRecord*    cameraRecord [[buffer(3)]],
             
                      constant Primitive&       primitives [[buffer(7)]],
                      constant PackageEnv&      packageEnv [[buffer(8)]],
                      constant PackagePBR*      packagePBR [[buffer(9)]])
{
    #ifndef DEVICE_SUPPORTS_NON_UNIFORM_TREADGROUPS
    if (thread_pos.x >= inRNG.get_width() || thread_pos.y >= inRNG.get_height()) {
        return;
    }
    #endif
    
//    if (!deviceSupportsNonuniformThreadgroups) {
//        if (thread_pos.x >= inRNG.get_width() || thread_pos.y >= inRNG.get_height()) {
//            return;
//        }
//    }
    
    auto rng_cache = inRNG.read(thread_pos);
    pcg32_t rng = toRNG(rng_cache);
    
    auto frame = complex->frame_count;
        
    auto u = float(thread_pos.x)/outTexture.get_width();
    auto v = float(thread_pos.y)/outTexture.get_height();
    
    auto idx = thread_pos.x + thread_pos.y * outTexture.get_width();
    
    RandomSampler rs { &rng };
    auto ray = castRay(camera, u, v, &rs);
    
    device auto& cr = cameraRecord[idx];
    
    float3 directLightToCamera;
    
    bool hasCameraRecord = traceCameraRecord(8, ray, rs,
                                             cr, directLightToCamera,
                                             packageEnv,
                                             packagePBR[1],
                                             primitives);
    if (hasCameraRecord) {
        
        //cameraRecord[idx] = cr;
        cameraAABB[idx] = {cr.position, cr.position};
        
    } else {
        cr.valid = false;
        cameraAABB[idx] = {FLT_MAX, -FLT_MAX};
        cameraRecord[idx].flux = directLightToCamera;
        
        //float3 cached = float3( inTexture.read( thread_pos ).rgb );
        //float3 result = (cached.rgb * frame + color) / (frame + 1);
    }
    
    rng_cache = exRNG(rng);
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
        pr.flux *= ratio;
        pr.step += 1;
        
        pr.active = !material.specular;
}
  
kernel void
kernelPhotonRecording(texture2d<uint32_t, access::read>       inRNG [[texture(0)]],
                      texture2d<uint32_t, access::write>     outRNG [[texture(1)]],
             
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
    
    bool check = (frame == 0) || (!photon_cache.active) || (photon_cache.step > 8);
    
    if (check) { // ray from light source
        
        photon_cache.reset();
        
        LightSampleRecord lsr;
        float2 uu = rs.sample2D();
        auto _origin = float3(450, 250, 250); //float3(0.0);
        
        if (rs.random() < 0.5) {
            primitives.squareList[5].sample(uu, _origin, lsr);
        } else {
            primitives.squareList[6].sample(uu, _origin, lsr);
        }
        
        photon_cache.flux = packageEnv.materials[lsr.material].textureInfo.albedo * 100000;
        
        float3 nx, ny;
        CoordinateSystem(lsr.n, nx, ny);
        float3x3 stw = { nx, ny, lsr.n };
        //float3x3 wts = transpose(stw);
        uu = rs.sample2D(); // reuse previous uu is bad
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
kernelPhotonParams(device Complex*              complex [[buffer(0)]],
                   device AABB*                 _bounds [[buffer(1)]])
{
    device auto* x = complex;
    
    x->photonBox = _bounds[0];
    x->photonBoxSize = x->photonBox.maxi - x->photonBox.mini;
    
    x->photonInitialRadius = dot(x->photonBoxSize, float3(1.0/3.0));
    x->photonInitialRadius *= 3.0 / 4096;
    
    x->photonBox.mini -= float3(x->photonInitialRadius);
    x->photonBox.maxi += float3(x->photonInitialRadius);
    
    x->photonHashScale = 1.0f / (x->photonInitialRadius * 1.5);
}

kernel void
kernelPhotonRadius(constant Complex*              complex [[buffer(0)]],
                   device CameraRecord*      cameraRecord [[buffer(1)]],
                   
                   uint2 thread_pos         [[thread_position_in_grid]],
                   uint2 group_size         [[threads_per_threadgroup]],
                   uint2 grid_size                 [[threads_per_grid]])
{
    size_t thread_idx = thread_pos.y * grid_size.x + thread_pos.x;
    cameraRecord[thread_idx].radius = complex->photonInitialRadius;
}

kernel void
kernelPhotonHashing(constant Complex*              complex [[buffer(1)]],
                    constant PhotonRecord*    photonRecord [[buffer(2)]],
                    
                    device float4*            photonHashed [[buffer(3)]],
                    
                    uint2 thread_pos         [[thread_position_in_grid]],
                    uint2 group_size         [[threads_per_threadgroup]],
                    uint2 grid_size                 [[threads_per_grid]] )
{
    size_t thread_idx = thread_pos.y * grid_size.x + thread_pos.x;
    float3 position = photonRecord[thread_idx].position;
    
    float HashScale = complex->photonHashScale;
    
    float3 HashIndex = floor((position - complex->photonBox.mini) * HashScale);
    
    auto hashed = hash( HashIndex, HashScale, 512 * 512);
    float2 pos2DHashedGrid = convert1Dto2D(hashed, 512);
    //float2 pos2D = as_type<float2>(thread_pos);
    float2 pos2D = (float2)(thread_pos);
    
    photonHashed[thread_idx] = float4(pos2D, pos2DHashedGrid);
}

// Vertex shader outputs and fragment shader inputs for simple pipeline
struct PhotonMarkVSData
{
    float4 position [[position]];
    float4 PhotonMark;
};

// Vertex shader which passes position and color through to rasterizer.
vertex PhotonMarkVSData
PhotonMarkVS(const uint vertexID [[ vertex_id ]],
             constant float4* vertices [[ buffer(0) ]],
             constant PhotonRecord* photonRecord [[buffer(1)]])
{
    PhotonMarkVSData out;
    
    auto element = vertices[vertexID]; //
    auto photonPosition2DHashedGrid = element.zw;
    
    auto z = photonRecord[vertexID].active ? 0.5 : -9999999999.0; // visible or not
    
    photonPosition2DHashedGrid.y = 512 - photonPosition2DHashedGrid.y;
    photonPosition2DHashedGrid = photonPosition2DHashedGrid * 2.0/512.0 - 1.0;
    
    out.position = float4(photonPosition2DHashedGrid, z, 1.0);
    //out.position.xyz = float3(0.5);
    out.PhotonMark = element - float4(0, 0, 1, 1);

    return out;
}

struct PhotonMarkFSData {
    float4 PhotonMark [[color(0)]];
    float count [[color(1)]];
};

// Fragment shader that just outputs color passed from rasterizer.
fragment PhotonMarkFSData
PhotonMarkFS(PhotonMarkVSData in [[stage_in]])
{
    PhotonMarkFSData result;
    
    result.PhotonMark = in.PhotonMark;
    result.count = 1; // count in grid
    
    return result;
}

kernel void
kernelPhotonSumming(device Complex*                 _complex         [[buffer(0)]],
                    texture2d<float, access::read>  _countHashGrid  [[texture(0)]],
                    
                    uint thread_idx                  [[ thread_position_in_grid ]],
                    uint group_idx              [[ threadgroup_position_in_grid ]],
                    uint local_idx            [[ thread_position_in_threadgroup ]],
                      
                    uint grid_size                          [[ threads_per_grid ]],
                    uint group_size                  [[ threads_per_threadgroup ]] )
{
    threadgroup float shared_memory[256] = { 0.0 };
    shared_memory[local_idx] = 0;
    
    for (uint i = 0; i < 512; i+=2) {
        //float count = _countHashGrid.read(uint2(local_idx, i)).x;
        float count = _countHashGrid.read(uint2(i, local_idx)).x;
        shared_memory[local_idx] += count;
        //sum += count;
        count = _countHashGrid.read(uint2(i+1, local_idx)).x;
        shared_memory[local_idx] += count;
        //sum += count;
    }
    
    //shared_memory[local_idx] = sum;
    
    threadgroup_barrier(mem_flags::mem_threadgroup);
    
    if (0 == local_idx) {
        for (uint i = 1; i < 256; i++) {
            shared_memory[0] += shared_memory[i];
        }
        _complex->photonSum += shared_memory[0];
        _complex->frame_count += 1;
    }
}

kernel void
kernelPhotonRefine(constant Complex*                _complex       [[buffer(0)]],
                   device   CameraRecord*           _cameraRecords [[buffer(1)]],
                   constant PhotonRecord*           _photonRecords [[buffer(2)]],
                   
                   texture2d<float, access::read>  _marksHashGrid [[texture(0)]],
                   texture2d<float, access::read>  _countHashGrid [[texture(1)]],
                   
                   texture2d<float, access::read>      inTexture [[texture(2)]],
                   texture2d<float, access::write>     outTexture [[texture(3)]],
                   
                   uint2 thread_pos                  [[thread_position_in_grid]],
                   uint2 group_size                  [[threads_per_threadgroup]],
                   uint2 grid_size                   [[threads_per_grid]])
{
    size_t thread_idx = thread_pos.y * grid_size.x + thread_pos.x;
    
    if (!_cameraRecords[thread_idx].valid) {
        
        auto color = _cameraRecords[thread_idx].flux;
        auto cache = inTexture.read(thread_pos).xyz;
        auto frame = _complex->frame_count - 1u;
        
        auto result = (cache*frame + color) / (frame + 1);
        outTexture.write(float4(result, 1.0), thread_pos);
        return;
    }

    float3 QueryPosition = _cameraRecords[thread_idx].position;
    float3 QueryDirection = _cameraRecords[thread_idx].direction;

    float3 QueryFlux = _cameraRecords[thread_idx].flux;
    float QueryRadius = _cameraRecords[thread_idx].radius;
    
    float3 QueryReflectance = _cameraRecords[thread_idx].ratio;
    uint QueryPhotonCount = _cameraRecords[thread_idx].photonCount;
    
    float3 BBoxMin = _complex->photonBox.mini;
    float HashScale = _complex->photonHashScale;

    float3 RangeMin = abs(QueryPosition - float3(QueryRadius) - BBoxMin) * HashScale;
    float3 RangeMax = abs(QueryPosition + float3(QueryRadius) - BBoxMin) * HashScale;
     
    float3 _Flux = 0; uint _PhotonCount = 0;
    
for (int iz = int(RangeMin.z); iz <= int(RangeMax.z); iz++)
{
    for (int iy = int(RangeMin.y); iy <= int(RangeMax.y); iy++)
    {
        for (int ix = int(RangeMin.x); ix <= int(RangeMax.x); ix++)
        {
            
float3 hashIndex = float3(ix, iy, iz);
float hashed = hash(hashIndex, HashScale, 512 * 512);
float2 HashedPhotonIndex2D = convert1Dto2D(hashed, 512);
            HashedPhotonIndex2D -= 1.0;
float2 PhotonIndex2D = _marksHashGrid.read((uint2)(HashedPhotonIndex2D)).xy;
uint PhotonIndex1D = (uint)PhotonIndex2D.y * 512 + (uint)(PhotonIndex2D.x);
            
            if (PhotonIndex1D == 0){
                continue;
            }
// accumulate photon
float3 PhotonFlux = _photonRecords[PhotonIndex1D].flux;
float3 PhotonPosition = _photonRecords[PhotonIndex1D].position;
float3 PhotonDirection = _photonRecords[PhotonIndex1D].direction;
            
            if (!_photonRecords[PhotonIndex1D].active) {
                continue;
            }

// make sure that the photon is actually in the given grid cell
float3 _RangeMin = hashIndex / HashScale + BBoxMin;
float3 _RangeMax = (hashIndex + float3(1.0)) / HashScale + BBoxMin;
            
if ((_RangeMin.x < PhotonPosition.x) && (PhotonPosition.x < _RangeMax.x)
    && (_RangeMin.y < PhotonPosition.y) && (PhotonPosition.y < _RangeMax.y)
    && (_RangeMin.z < PhotonPosition.z) && (PhotonPosition.z < _RangeMax.z))
{
    float d = distance(PhotonPosition, QueryPosition);

    if ((d < QueryRadius) && (-dot(QueryDirection, PhotonDirection) > 0.001))
    {
        float Correction = _countHashGrid.read((uint2)HashedPhotonIndex2D).x;
        
        _Flux += PhotonFlux * Correction;
        _PhotonCount += Correction;
    }
}
//outTexture.write(float4(_Flux , 1.0), thread_pos);
//return;
        }
    }
}
    // BRDF (Lambertian)
    _Flux = _Flux * (QueryReflectance / 3.141592);
    //if (FullSpectrum) Flux = Spectrum2RGB(Wavelength) * Flux.r;

    // progressive refinement
    const float alpha = 0.8;
    float g = min((QueryPhotonCount + _PhotonCount * alpha ) / (QueryPhotonCount + _PhotonCount), 1.0);
    QueryRadius = QueryRadius * sqrt(g);
    
    QueryPhotonCount = QueryPhotonCount + _PhotonCount * alpha;
    QueryFlux = (QueryFlux + _Flux) * g;
    
    _cameraRecords[thread_idx].flux = QueryFlux;
    _cameraRecords[thread_idx].radius = QueryRadius;
    _cameraRecords[thread_idx].photonCount = QueryPhotonCount;
    
    auto TotalPhotonNum = _complex->photonSum; //512 * 512 * (_complex->frame_count + 1);
    auto maxPhotonNum = 512 * 512 * (_complex->frame_count + 1);
    
    //if (TotalPhotonNum > maxPhotonNum) {
        //outTexture.write(float4(99999, TotalPhotonNum, maxPhotonNum, 1.0), thread_pos); return;
    //}
    
    float3 color = float3(QueryFlux / (QueryRadius * QueryRadius * 3.141592 * TotalPhotonNum));
    //color = CETone(color, 1.0);
    
    auto cache = inTexture.read(thread_pos).xyz;
    auto frame = _complex->frame_count - 1u;
    
    float3 result = (cache*frame + color) / (frame+1);
    outTexture.write(float4(result, 1.0), thread_pos);
}
