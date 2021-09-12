#ifndef Medium_h
#define Medium_h

#ifdef __METAL_VERSION__
    #include "Ray.hh"
    #include "Sampling.hh"
    #include "HitRecord.hh"
#endif

class HomogeneousMedium;
class GridDensityMedium;

#ifdef __METAL_VERSION__
struct MediumInteraction {
    
    float3 p; float phaseG;
    
    //thread void* test = nullptr;
    //thread HenyeyGreenstein *phase = nullptr;
    thread HomogeneousMedium *homogen = nullptr;
    thread GridDensityMedium *density = nullptr;
};
#endif

class HomogeneousMedium {
  public:
#ifdef __METAL_VERSION__
    HomogeneousMedium(const thread float3 &sigma_a, const thread float3 &sigma_s, float g)
        : sigma_a(sigma_a), sigma_s(sigma_s), sigma_t(sigma_s + sigma_a), g(g) {}
    
    float3 Tr(const thread Ray &ray, const thread HitRecord &hitRecord) const {
        //return Exp(-sigma_t * std::min(ray.tMax * ray.d.Length(), MaxFloat));
        return exp(-sigma_t * min(hitRecord.t, FLT_MAX));
    }
    
    template <typename XSampler>
    float3 Sample(const thread Ray &ray, thread HitRecord &hitRecord,
                  thread MediumInteraction *mi, thread XSampler& xsampler) {
        
        auto nSamples = 3;
        // Sample a channel and distance along the ray
        int channel = min((int)(xsampler.sample1D() * nSamples), nSamples - 1);
        
        float dist = -log(1 - xsampler.sample1D()) / sigma_t[channel];
        
        float t = min(dist, hitRecord.t);
        bool sampledMedium = t < hitRecord.t;
        
        float3 Tr = exp(-sigma_t * min(t, FLT_MAX));
        
        float3 density = Tr;
        float3 result = Tr;
        
        if (sampledMedium) {

            mi->p = ray.pointAt(t);
            
            mi->phaseG = g;
            mi->homogen = this;
            
            density *= sigma_t;
            result *= sigma_s;
        }
        
//        float pdf = 0; int i = 0;
//        while(i < nSamples) {
//            pdf += density[i]; ++i;
//        }
        float pdf = dot(float3(1.0), density);
        
        if (0.0f >= pdf) { pdf = 1.0f; }
        else { pdf = pdf / nSamples; }
        
        return result / pdf;
    }
#else
    HomogeneousMedium(const float3 &sigma_a, const float3 &sigma_s, float g)
        : sigma_a(sigma_a), sigma_s(sigma_s), sigma_t(sigma_s + sigma_a), g(g) {}
#endif
    
  private:
    const float3 sigma_a, sigma_s, sigma_t;
    const float g;
};

struct GridDensityInfo {
    float sigma_a, sigma_s;
    float sigma_t; float g;
    
    float invMaxDensity;
    
    uint nx, ny, nz;
    
#ifdef __METAL_VERSION__
    GridDensityInfo() {}
#else
    GridDensityInfo(float sigma_a, float sigma_s, float g,
                    int nx, int ny, int nz, float* _density)
        : sigma_a(sigma_a), sigma_s(sigma_s), g(g), nx(nx), ny(ny), nz(nz)
    {
        sigma_t = (sigma_a + sigma_s);
        
        float maxDensity = 0;
        for (int i = 0; i < (nx * ny * nz); ++i) {
            maxDensity = fmax(maxDensity, _density[i]);
        }
        invMaxDensity = 1 / maxDensity;
    }
#endif
    
};

class GridDensityMedium {
#ifdef __METAL_VERSION__
  public:
    constant GridDensityInfo* info;
    constant float* density;
    
    float D(const thread int3 &p) const {
        
        int nx = info->nx, ny = info->ny, nz = info->nz;
        int3 tmp = int3(nx, ny, nz);
        
        if (any(p<0) || any(p>=tmp)) { return 0; }
        return density[(p.z * ny + p.y) * nx + p.x];
    }
    
    // GridDensityMedium Public Methods
    float Density(const thread float3 &p) const {
        // Compute voxel coordinates and offsets for _p_
        float nx = info->nx, ny = info->ny, nz = info->nz;
        float3 pSamples = p * float3(nx, ny, nz) - 0.5;
        
        int3 pi = (int3) floor(pSamples);
        float3 d = pSamples - (float3)pi;

        // Trilinearly interpolate density values to compute local density
        float d00 = Lerp(d.x, D(pi), D(pi + int3(1, 0, 0)));
        float d10 = Lerp(d.x, D(pi + int3(0, 1, 0)), D(pi + int3(1, 1, 0)));
        float d01 = Lerp(d.x, D(pi + int3(0, 0, 1)), D(pi + int3(1, 0, 1)));
        float d11 = Lerp(d.x, D(pi + int3(0, 1, 1)), D(pi + int3(1, 1, 1)));
        float d0 = Lerp(d.y, d00, d10);
        float d1 = Lerp(d.y, d01, d11);
        return Lerp(d.z, d0, d1);
    }
    
    template <typename XSampler>
    float3 Tr(const thread Ray& ray, const thread HitRecord& hitRecord, thread XSampler &sampler) const {
        
        float tMax = hitRecord._t;
        float t = 0, Tr = 1;
        
        while (true) {
            t -= log(1 - sampler.sample1D()) * info->invMaxDensity / info->sigma_t;
            
            if (t >= tMax) break;
            auto p = ray.pointAt(t);
            
            float density = Density(p);
            Tr *= 1 - max(0.0, density * info->invMaxDensity);
            // Added after book publication: when transmittance gets low,
            // start applying Russian roulette to terminate sampling.
            const float rrThreshold = .1;
            if (Tr < rrThreshold) {
                float q = max(.05, 1 - Tr);
                if (sampler.sample1D() < q) return 0;
                Tr /= 1 - q;
            }
        }
        return Tr;
    }
    
    template <typename XSampler>
    float3 Sample(const thread Ray &ray, const thread HitRecord& hitRecord, thread MediumInteraction *mi, thread XSampler &sampler) {
        
        float tMax = hitRecord._t;
        float t = 0;
        
        while (true)
        {
            t -= log(1 - sampler.sample1D()) * info->invMaxDensity / info->sigma_t;
            
            if (t >= tMax) break;
            auto p = ray.pointAt(t);
            
            if (Density(p) * info->invMaxDensity > sampler.sample1D()) {
                
                mi->p = (hitRecord.modelMatrix * float4(p, 1.0)).xyz;
                //hitRecord.p - hitRecord.w * hitRecord.t *(1.0-t/tMax);
                mi->phaseG = info->g;
                mi->density = this;
                
                return info->sigma_s / info->sigma_t;
            }
        }
        
        return 1.0;
    }
#endif
};

#endif /* Medium_h */
