#include "Scatter.hh"

bool scatter(thread Ray& ray,
             thread HitRecord& hitRecord,
             thread ScatRecord& scatRecord,
             thread pcg32_random_t* seed)
{
    auto normal = hitRecord.normal();
    thread auto& material = hitRecord.material;
    
    switch(material.type) {
            
        case MaterialType::Lambert: {
            
            auto direction = normal + randomUnit(seed);
            
            ray = Ray(hitRecord.p, direction);
            scatRecord.attenuation = material.textureInfo.value(nullptr, hitRecord.uv, hitRecord.p);
            return true;
        }
            
        case MaterialType::Metal: {
            
            auto fuzz = 0.01 * randomInUnitSphereFFF(seed);
            auto reflected = reflect(ray.direction, normal);
            auto direction = reflected + fuzz;
            
            ray = Ray(hitRecord.p, direction);
            scatRecord.attenuation = material.textureInfo.value(nullptr, hitRecord.uv, hitRecord.p);
            return true;
        }
            
        case MaterialType::Dielectric: {
            
            auto theIOR = hitRecord.front? (1.0/material.parameter) : material.parameter;
            
            auto unit_direction = ray.direction;
            auto cos_theta = min(dot(-unit_direction, normal), 1.0);
            auto sin_theta = sqrt(1.0 - cos_theta*cos_theta);
            
            if (theIOR * sin_theta > 1.0 ) {
                auto reflected = reflect(unit_direction, normal);
                
                ray = Ray(hitRecord.p, reflected);
                scatRecord.attenuation = material.textureInfo.value(nullptr, hitRecord.uv, hitRecord.p);
                return true;
            }

            auto reflect_prob = schlick(cos_theta, theIOR);
            if (randomF(seed) < reflect_prob) {
                auto reflected = reflect(unit_direction, normal);
                
                ray = Ray(hitRecord.p, reflected);
                scatRecord.attenuation = material.textureInfo.value(nullptr, hitRecord.uv, hitRecord.p);
                return true;
            }

            auto refracted = refract(unit_direction, normal, theIOR);
            
            ray = Ray(hitRecord.p, refracted);
            scatRecord.attenuation = material.textureInfo.value(nullptr, hitRecord.uv, hitRecord.p);
            return true;
        }
        case MaterialType::Diffuse: {
            return false;
        }
        case MaterialType::Isotropic: {
            
            ray = Ray(hitRecord.p, randomInUnitSphereFFF(seed));
            scatRecord.attenuation = material.textureInfo.value(nullptr, hitRecord.uv, hitRecord.p);
            return true;
        }
            
        case MaterialType::Specular: {
            
            float rayProbability = 1.0f;
            auto throughput = float3(1.0);
            
            auto theIOR = hitRecord.front? (1.0/material.parameter) : material.parameter;
            
            if (!hitRecord.front) {
                throughput *= exp(-hitRecord.material.refractionColor * hitRecord.t);
            }
            
            // apply fresnel
            float specularProb = hitRecord.material.specularProb;
            float refractionProb = hitRecord.material.refractionProb;
            
            if (specularProb > 0.0f) {
                
                specularProb = fresnel(
                        hitRecord.front? 1.0 : hitRecord.material.parameter,
                        !hitRecord.front? 1.0 : hitRecord.material.parameter,
                        normal, ray.direction, hitRecord.material.specularProb, 1.0f);
                        //ray.direction, hitRecord.n, hitRecord.material.specularProb, 1.0f);
                
                refractionProb *= (1.0f - specularProb) / (1.0f - hitRecord.material.specularProb);
            }
            
            auto doSpecular = 0.0f;
            auto doRefraction = 0.0f;
            auto raySelectRoll = randomF(seed);
            
            if (specularProb > 0.0f && raySelectRoll < specularProb)
            {
                doSpecular = 1.0f;
                rayProbability = specularProb;
            }
            else if (refractionProb > 0.0f && raySelectRoll < (specularProb + refractionProb))
            {
                doRefraction = 1.0f;
                rayProbability = refractionProb;
            }
            else
            {
                rayProbability = 1.0f - (specularProb + refractionProb);
            }
                 
            // numerical problems can cause rayProbability to become small enough to cause a divide by zero.
            rayProbability = max(rayProbability, 0.001f);
            
            auto origin = hitRecord.p + normal * 0.01;
            
            if (doRefraction == 1.0) {
                origin = hitRecord.p - normal * 0.01;
            } else {
                origin = hitRecord.p + normal * 0.01;
            }
            
            auto diffuseDir = normal + randomUnit(seed);
            auto specularDir = reflect(ray.direction, normal);
            
            specularDir = normalize(mix(specularDir, diffuseDir, hitRecord.material.specularRoughness * hitRecord.material.specularRoughness));
            
            auto refractionDir = refract(ray.direction, normal, theIOR);
            refractionDir = normalize(mix(refractionDir, normalize(-normal + randomUnit(seed)), hitRecord.material.refractionRoughness *  hitRecord.material.refractionRoughness));
                
            auto direction = mix(diffuseDir, specularDir, doSpecular);
            direction = mix(direction, refractionDir, doRefraction);
            
            ray = Ray(origin, direction);
            scatRecord.attenuation = throughput;
            
            if (doRefraction == 0.0f) {
                
                scatRecord.attenuation *= mix(hitRecord.material.textureInfo.albedo,
                                              hitRecord.material.specularColor,
                                              doSpecular);
            }
            
            scatRecord.attenuation /= rayProbability;
            
            return true;
        }
            
        default: {return false;}
    }
}
