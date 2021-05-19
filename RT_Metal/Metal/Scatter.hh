#ifndef Scatter_h
#define Scatter_h

#include "Ray.hh"
#include "Camera.hh"
#include "Texture.hh"
#include "Material.hh"
#include "AABB.hh"

#include "BVH.hh"
#include "Cube.hh"
#include "Square.hh"
#include "Sphere.hh"

#include "BXDF.hh"

#include "HitRecord.hh"
#include "SobolSampler.hh"

// Normal Distribution Function
float DX(const thread float3& h, float a2) {
    //  GGX / Trowbridge-Reitz
    float NoH = h.z;
    
    float d = (NoH * a2 - NoH) * NoH + 1;
    //return a2 / (d * d * M_PI_F);
    return a2 / max(FLT_EPSILON, d * d * M_PI_F);
}

// Geometry Function
float GX(const thread float3& wo, const thread float3& wi, float a2) {
    // Schlick, remap roughness and k
    float k = a2 / 2;
    
    auto oz = wo.z; //max(0.0, wo.z);
    auto iz = wi.z; //max(0.0, wi.z);
    
    float G1_wo = oz / (oz * (1 - k) + k);
    float G1_wi = iz / (iz * (1 - k) + k);
    return G1_wo * G1_wi;
}

// Fresnel
float3 FX(const thread float3& wo, const thread float3& albedo, float metallic) {
    // Schlick’s approximation
    
    auto F0 = mix(0.04f, albedo, metallic);
    //return F0 + (1.0-F0) * pow(1.0-HoWi, 5.0);
    
    auto ex = (-5.55473f * wo.z - 6.98316f) * wo.z;
    return F0 + (1.0 - F0) * pow(2.0f, ex);
}

float DisneyDiffuse(const thread float3& wo, const thread float3& wi, const float IDotH, const float roughness)
{
//    if (mMetallic == 1.0f) return 0.0f;

    float oneMinusCosL = 1.0f - wi.z;
    float oneMinusCosLSqr = oneMinusCosL * oneMinusCosL;
    float oneMinusCosV = 1.0f - wo.z;
    float oneMinusCosVSqr = oneMinusCosV * oneMinusCosV;
    float F_D90 = 0.5f + 2.0f * IDotH * IDotH * roughness;

    return float(1.0/M_PI_F) * (1.0f + (F_D90 - 1.0f) * oneMinusCosLSqr * oneMinusCosLSqr * oneMinusCosL) *
        (1.0f + (F_D90 - 1.0f) * oneMinusCosVSqr * oneMinusCosVSqr * oneMinusCosV);
}

template <typename XSampler>
bool scatter(thread Ray& ray,
             thread XSampler& xsampler,
             thread HitRecord& hitRecord,
             thread ScatRecord& scatRecord,
             
             constant Material* materials,
             
             constant PackPBR& packPBR)
{
    auto normal = hitRecord.normal();
    auto materialID = hitRecord.material;
    constant auto& material = materials[materialID];
    
    switch(material.type) {
            
        case MaterialType::Diffuse: { return false; }
            
        case MaterialType::Lambert: {

            float3 nx, ny;
            CoordinateSystem(normal, nx, ny);
            float3x3 stw = { nx, ny, normal };

            auto r0 = xsampler.sample1D();
            auto r1 = xsampler.sample1D();

            float sinTheta = sqrt( max(r0, kEpsilon) );
            float cosTheta = sqrt( max(1-r0, kEpsilon) );

            float phi = 2.0 * M_PI_F * r1;

            float x = sinTheta * cos(phi);
            float y = sinTheta * sin(phi);
            float z = cosTheta;

            float3 wi = { x, y, z };
            auto direction = stw * wi;
            
            ray = Ray(hitRecord.p, direction);
            scatRecord.attenuation = material.textureInfo.value(nullptr, hitRecord.uv, hitRecord.p);
            //scatRecord.attenuation /= M_PI_F;
            return true;
        }
//
//        case MaterialType::Metal: {
//
//            auto fuzz = 0.01 * xsampler.sampleUnitInSphere();
//            auto reflected = reflect(ray.direction, normal);
//            auto direction = reflected + fuzz;
//
//            ray = Ray(hitRecord.p, direction);
//            scatRecord.attenuation = material.textureInfo.value(nullptr, hitRecord.uv, hitRecord.p);
//            return true;
//        }
//
//        case MaterialType::Dielectric: {
//
//            auto theIOR = hitRecord.f? (1.0/material.parameter) : material.parameter;
//
//            auto unit_direction = ray.direction;
//            auto cos_theta = min(dot(-unit_direction, normal), 1.0);
//            auto sin_theta = sqrt(1.0 - cos_theta*cos_theta);
//
//            if (theIOR * sin_theta > 1.0 ) {
//                auto reflected = reflect(unit_direction, normal);
//
//                ray = Ray(hitRecord.p, reflected);
//                scatRecord.attenuation = material.textureInfo.value(nullptr, hitRecord.uv, hitRecord.p);
//                return true;
//            }
//
//            auto reflect_prob = schlick(cos_theta, theIOR);
//            if (xsampler.random() < reflect_prob) {
//                auto reflected = reflect(unit_direction, normal);
//
//                ray = Ray(hitRecord.p, reflected);
//                scatRecord.attenuation = material.textureInfo.value(nullptr, hitRecord.uv, hitRecord.p);
//                return true;
//            }
//
//            auto refracted = refract(unit_direction, normal, theIOR);
//
//            ray = Ray(hitRecord.p, refracted);
//            scatRecord.attenuation = material.textureInfo.value(nullptr, hitRecord.uv, hitRecord.p);
//            return true;
//        }
//
//        case MaterialType::Isotropic: {
//
//            ray = Ray(hitRecord.p, xsampler.sampleUnitInSphere());
//            scatRecord.attenuation = material.textureInfo.value(nullptr, hitRecord.uv, hitRecord.p);
//            return true;
//        }
            
        case MaterialType::PBR: {
            
            //float ao = 1.0; //texAO.sample(textureSampler, hitRecord.uv, 0).r;
            
            float3 albedo = packPBR.texAlbedo.sample(textureSampler, hitRecord.uv, 0).rgb;
            albedo = pow(albedo, 2.2); //albedo = float3(1.0);
            
            float metallic = packPBR.texMetallic.sample(textureSampler, hitRecord.uv, 0).r;
            float3 tNormal = packPBR.texNormal.sample(textureSampler, hitRecord.uv, 0).xyz;
            tNormal = normalize(tNormal * 2.0 - 1.0);
            
            float rough = packPBR.texRoughness.sample(textureSampler, hitRecord.uv, 0).r;
            auto a1 = max(abs(rough), 0.001);
            auto a2 = a1;//a1 * a1;
            
            float3 nx, ny;
            CoordinateSystem(normal, nx, ny);
            float3x3 stw = { nx, ny, normal };
            
            normal = stw * tNormal;
            CoordinateSystem(normal, nx, ny);
            stw = { nx, ny, normal };
            
            float3 nextDir, microNormal;
            float3 fixedDir = transpose(stw) * (-ray.direction);
        
            auto pSpecular = 1 / (2 - metallic);
            
            auto r0 = xsampler.sample1D();
            auto r1 = xsampler.sample1D();
            
            r0 = max(r0, FLT_EPSILON);
            auto rr0 = max(1-r0, FLT_EPSILON);
            
            if (xsampler.random() > pSpecular) {
                
                float sinTheta = sqrt(r0);
                float cosTheta = sqrt(rr0);
                
                float phi = 2.0 * M_PI_F * r1;
                
                float x = sinTheta * cos(phi);
                float y = sinTheta * sin(phi);
                float z = cosTheta;
                
                nextDir = { x, y, z };
                microNormal = normalize(fixedDir + nextDir); // not specular
                
                ray = Ray(hitRecord.p, stw * nextDir);
                scatRecord.attenuation = albedo / (1.0-pSpecular);
                //auto pdf = nextDir.z;// / M_PI_F;
                //scatRecord.attenuation /= pdf;
                
                return true;
                
            } else {
                
                float pdf;
                microNormal = GGX_SampleVisibleNormal(fixedDir, r0, r1, &pdf, a2);
                
//                auto tmp = rr0 / ((a2 - 1) * r0 + 1);
//                auto cosTheta = sqrt(tmp);
//                auto sinTheta = sqrt(max(FLT_EPSILON, 1.0-tmp));
//                //auto theta = acos(sqrt(rr0/((a2-1)*r0+1)));
//                auto phi = 2 * M_PI_F * r1;
//
//                auto x = sinTheta * cos(phi);
//                auto y = sinTheta * sin(phi);
//                auto z = cosTheta;
//
//                microNormal = normalize( {x, y, z} );
                nextDir = reflect(-fixedDir, microNormal);
            }
            
            if (nextDir.z <= 0 || fixedDir.z <=0) { return false; }
        
            auto D = DX(microNormal, a2);
            auto G = GX(fixedDir, nextDir, a2);
            auto F = FX(fixedDir, albedo, metallic);
            
            float deno = max(FLT_MIN, fixedDir.z * nextDir.z * 4);

            auto specular = D * G * F / deno;
            auto diffuse = albedo / M_PI_F;
            
            auto kS = F;
            auto kD = (1.0 - metallic) * (1.0 - kS);

            ray = Ray(hitRecord.p, stw * nextDir);
            scatRecord.attenuation = kD * diffuse + specular;
            
            //auto diffusePD = wi.z / M_PI_F;
            //auto specularPD = D * microNormal.z
            // 1 / (dot(nextDir, microNormal) * 4);
            
            scatRecord.attenuation /= pSpecular;
            //scatRecord.attenuation *= nextDir.z;
            
            return true;
        }
            
        case MaterialType::Specular: {
            
            float rayProbability = 1.0f;
            auto throughput = float3(1.0);
            
            auto theIOR = hitRecord.f? (1.0/material.parameter) : material.parameter;
            
            if (!hitRecord.f) {
                throughput *= exp(-material.refractionColor * hitRecord.t);
            }
            
            // apply fresnel
            float specularProb = material.specularProb;
            float refractionProb = material.refractionProb;
            
            if (specularProb > 0.0f) {
                
                specularProb = fresnel(
                        hitRecord.f? 1.0 : material.parameter,
                        !hitRecord.f? 1.0 : material.parameter,
                        normal, ray.direction, material.specularProb, 1.0f);
                        //ray.direction, hitRecord.n, hitRecord.material.specularProb, 1.0f);
                
                refractionProb *= (1.0f - specularProb) / (1.0f - material.specularProb);
            }
            
            auto doSpecular = 0.0f;
            auto doRefraction = 0.0f;
            auto raySelectRoll = xsampler.random();
            
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
            
            auto diffuseDir = normal + xsampler.sampleUnit();
            auto specularDir = reflect(ray.direction, normal);
            
            specularDir = normalize(mix(specularDir, diffuseDir, pow(material.specularRoughness, 2)));
            
            auto refractionDir = refract(ray.direction, normal, theIOR);
                refractionDir = normalize(mix(refractionDir, normalize(-normal + xsampler.sampleUnit()), pow(material.refractionRoughness, 2)));
                
            auto direction = mix(diffuseDir, specularDir, doSpecular);
            direction = mix(direction, refractionDir, doRefraction);
            
            ray = Ray(origin, direction);
            scatRecord.attenuation = throughput;
            
            if (doRefraction == 0.0f) {
                
                scatRecord.attenuation *= mix(material.textureInfo.albedo,
                                              material.specularColor,
                                              doSpecular);
            }
            
            scatRecord.attenuation /= rayProbability;
            
            return true;
        }
            
        default: {return false;}
    }
}


#endif /* MetalObject_h */
