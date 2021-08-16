#ifndef Medium_h
#define Medium_h

#include "Ray.hh"
#include "HitRecord.hh"
#include "Sampling.hh"

class Medium {
  public:
    //Spectrum Tr(const Ray &ray, Sampler &sampler) const = 0;
    //Spectrum Sample(const Ray &ray, Sampler &sampler, MediumInteraction *mi) const = 0;
};

class HomogeneousMedium;

class MediumInteraction {
public:
    float3 p, wo;
    float phaseG;
    //thread HenyeyGreenstein *phase = nullptr;
    thread HomogeneousMedium *medium = nullptr;
};

class HomogeneousMedium {
  public:
    // HomogeneousMedium Public Methods
    HomogeneousMedium(const thread float3 &sigma_a, const thread float3 &sigma_s, float g)
        : sigma_a(sigma_a), sigma_s(sigma_s), sigma_t(sigma_s + sigma_a), g(g) {}
    
    //Spectrum Tr(const thread Ray &ray, const thread Sampler &sampler) const;
    float3 Tr(const thread Ray &ray, const thread HitRecord &hitRecord) const {
        //return Exp(-sigma_t * std::min(ray.tMax * ray.d.Length(), MaxFloat));
        return exp(-sigma_t * min(hitRecord.t, FLT_MAX));
    }
    
    //Spectrum Sample(const threadRay &ray, Sampler &sampler, MediumInteraction *mi) const;
    template <typename XSampler>
    float3 Sample(const thread Ray &ray, thread HitRecord &hitRecord,
                  thread MediumInteraction *mi, thread XSampler& xsampler) {
        //ProfilePhase _(Prof::MediumSample);
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
    
  private:
    // HomogeneousMedium Private Data
    const float3 sigma_a, sigma_s, sigma_t;
    const float g;
};

#endif /* Medium_h */
