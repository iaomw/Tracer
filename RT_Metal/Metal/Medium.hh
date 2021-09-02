#ifndef Medium_h
#define Medium_h

#ifdef __METAL_VERSION__
    #include "Ray.hh"
    #include "Sampling.hh"
    #include "HitRecord.hh"
#else
    #include "Medium.h"
#endif

class HomogeneousMedium;
class GridDensityMedium;

#ifdef __METAL_VERSION__
struct MediumInteraction {
    float3 p, wo;
    float phaseG;
    //thread HenyeyGreenstein *phase = nullptr;
    thread HomogeneousMedium *medium = nullptr;
    thread GridDensityMedium *density = nullptr;
};
#endif

class HomogeneousMedium {
  public:
    // HomogeneousMedium Public Methods
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
        
        if (sampledMedium) {

            mi->p = ray.pointAt(t);
            mi->wo = -ray.direction;
            //auto hg = HenyeyGreenstein(g);
            mi->phaseG = g;
            mi->medium = this;
        }
        
        // Compute the transmittance and sampling density
        float3 Tr = exp(-sigma_t * min(t, FLT_MAX)); //* ray.d.Length());
        // Return weighting factor for scattering from homogeneous medium
        float3 density = sampledMedium ? (sigma_t * Tr) : Tr;
        
        float pdf = 0.0f; //float tmp = dot( float3(1.0), density );
        for (int i = 0; i < nSamples; ++i) { pdf += density[i]; }
        
        if (0.0f >= pdf) { pdf = 1.0f; }
        else { pdf = pdf / nSamples; }
        
        return sampledMedium ? (Tr * sigma_s / pdf) : (Tr / pdf);
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
    GridDensityInfo(float sigma_a, float sigma_s, float g, int nx, int ny, int nz)
        : sigma_a(sigma_a), sigma_s(sigma_s), g(g), nx(nx), ny(ny), nz(nz)
    {
        sigma_t = (sigma_a + sigma_s);
        
        float maxDensity = 0;
        for (int i = 0; i < (nx * ny * nz); ++i) {
            maxDensity = fmax(maxDensity, test_density[i]);
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
    
    // GridDensityMedium Public Methods
    float Density(const thread float3 &p) const {
        // Compute voxel coordinates and offsets for _p_
        float nx = info->nx, ny = info->ny, nz = info->nz;
        
        float3 pSamples(p.x * nx - .5f, p.y * ny - .5f, p.z * nz - .5f);
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
    
    float D(const thread int3 &p) const {
        
        auto nx = info->nx, ny = info->ny, nz = info->nz;
        uint3 tmp = {nx, ny, nz};
        
        for (int i=0; i<3; i++) {
            if (p[i] < 0 || p[i] >= tmp[i]) {
                return 0;
            }
        }

//        if (min3(p.x, p.y, p.z) < 0) { return 0; }
//
//        auto nx = info->nx, ny = info->ny, nz = info->nz;
//
//        uint3 tmp = {nx, ny, nz};
//        auto delta = (int3)tmp - p;
//
//        auto pick = min3(delta.x, delta.y, delta.z);
//        if (pick < 1) { return 0; }
        
        return density[(p.z * ny + p.y) * nx + p.x];
    }
    
//    float3 Tr(const Ray &ray, Sampler &sampler) const {
//
//        Ray ray = WorldToMedium(
//         Ray(rWorld.o, Normalize(rWorld.d), rWorld.tMax * rWorld.d.Length()));
//        // Compute $[\tmin, \tmax]$ interval of _ray_'s overlap with medium bounds
    //      &  const Bounds3f b(Point3f(0, 0, 0), Point3f(1, 1, 1));
//        float tMin, tMax;
//        if (!b.IntersectP(ray, &tMin, &tMax)) return Spectrum(1.f);
//
//        // Perform ratio tracking to estimate the transmittance value
//        float Tr = 1, t = tMin;
//        while (true) {
//         ++nTrSteps;
//         t -= log(1 - sampler.Get1D()) * invMaxDensity / sigma_t;
//         if (t >= tMax) break;
//         float density = Density(ray(t));
//         Tr *= 1 - std::max((Float)0, density * invMaxDensity);
//         // Added after book publication: when transmittance gets low,
//         // start applying Russian roulette to terminate sampling.
//         const Float rrThreshold = .1;
//         if (Tr < rrThreshold) {
//             Float q = std::max((Float).05, 1 - Tr);
//             if (sampler.Get1D() < q) return 0;
//             Tr /= 1 - q;
//         }
//        }
//        return Spectrum(Tr);
//    }
    
    template <typename XSampler>
    float3 Sample(const thread Ray &ray, const thread HitRecord& hitRecord, thread MediumInteraction *mi, thread XSampler &sampler) {
        //float tMin,
        float tMax = hitRecord._t;
        //if (!b.IntersectP(ray, &tMin, &tMax)) return (1.f);

        // Run delta-tracking iterations to sample a medium interaction
        float t = 0;//tMin;
        
        while (true)
        {
            t -= log(1 - sampler.sample1D()) * info->invMaxDensity / info->sigma_t;
            if (t >= tMax) break;
            
            auto p = ray.pointAt(t);
            
            if (Density(p) * info->invMaxDensity > sampler.sample1D()) {
                
                //auto phase = HenyeyGreenstein(g);
                mi->p = p;
                mi->wo = 0;
                mi->phaseG = info->g;
                mi->density = this;
                
                return info->sigma_s / info->sigma_t;
            }
        }
        
        return (1.f);
    }
#endif
};

#endif /* Medium_h */