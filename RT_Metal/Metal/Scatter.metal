#include "Scatter.hh"

// Normal Distribution Function
float DX(const thread float3& h, float a2) {
    //  GGX / Trowbridge-Reitz
    
    float NoH = abs(h.z);
    
    float d = (NoH * a2 - NoH) * NoH + 1;
    return a2 / (d * d * M_PI_F);
}

// Geometry Function
float GX(const thread float3& wo, const thread float3& wi, float a2) {
    // Schlick, remap roughness and k
    float k = a2 / 2;
    
    auto oz = max(0.0, wo.z);
    auto iz = max(0.0, wi.z);
    
    float G1_wo = oz / (oz * (1 - k) + k);
    float G1_wi = iz / (iz * (1 - k) + k);
    return G1_wo * G1_wi;
}

float SmithGGXMaskingShadowing(float3 wi, float3 wo, float a2)
{
    float dotNL = max(0.0, wi.z);
    float dotNV = max(0.0, wo.z);

    float denomA = dotNV * sqrt(a2 + (1.0f - a2) * dotNL * dotNL);
    float denomB = dotNL * sqrt(a2 + (1.0f - a2) * dotNV * dotNV);

    return 2.0f * dotNL * dotNV / (denomA + denomB);
}

// Fresnel
float3 FX(const thread float3& wi, const thread float3& wh, const thread float3 albedo, float metallic) {
    // Schlick’s approximation
    // use a Spherical Gaussian approximation to replace the power.
    // slightly more efficient to calculate and the difference is imperceptible
    
    float HoWi = abs(dot(wh, wi));
    
    float3 F0 = mix(float3(0.04f), albedo, metallic);
    return F0 + (1.0 - F0) * pow(2.0f, (-5.55473f * HoWi - 6.98316f) * HoWi);
}

// 概率密度函数 probability density function
float PDF(const thread float3& wh, const float roughness) {
    return DX(wh, roughness) * abs(wh.z);
}

bool scatter(thread Ray& ray,
             thread pcg32_t* seed,
             thread HitRecord& hitRecord,
             thread ScatRecord& scatRecord,
             
             constant Material* materials,
             
             thread texture2d<float, access::sample> &texAO,
             thread texture2d<float, access::sample> &texAlbedo,
             thread texture2d<float, access::sample> &texMetallic,
             thread texture2d<float, access::sample> &texNormal,
             thread texture2d<float, access::sample> &texRoughness )
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

            auto r0 = randomF(seed);
            auto r1 = randomF(seed);

            float sinTheta = sqrt( max(r0, kEpsilon) );
            float cosTheta = sqrt( max(1-r0, kEpsilon) );

            float phi = 2.0 * M_PI_F * r1;

            float x = sinTheta * cos(phi);
            float y = sinTheta * sin(phi);
            float z = cosTheta;

            float3 wi = { x, y, z };
            auto direction = stw * wi;
            
            //auto direction = normal + randomUnit(seed);
            
            ray = Ray(hitRecord.p, direction);
            scatRecord.attenuation = material.textureInfo.value(nullptr, hitRecord.uv, hitRecord.p);
            //scatRecord.attenuation /= M_PI_F;
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
            
            auto theIOR = hitRecord.f? (1.0/material.parameter) : material.parameter;
            
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
            
        case MaterialType::Isotropic: {
            
            ray = Ray(hitRecord.p, randomInUnitSphereFFF(seed));
            scatRecord.attenuation = material.textureInfo.value(nullptr, hitRecord.uv, hitRecord.p);
            return true;
        }
            
        case MaterialType::PBR: {
            
            float ao = 1.0; //texAO.sample(textureSampler, hitRecord.uv, 0).r;
            
            float3 albedo = texAlbedo.sample(textureSampler, hitRecord.uv, 0).rgb;
            albedo = pow(albedo, 2.2); //albedo = float3(1.0);
            
            float metallic = texMetallic.sample(textureSampler, hitRecord.uv, 0).r;
            float3 tNormal = texNormal.sample(textureSampler, hitRecord.uv, 0).xyz;
            tNormal = normalize(tNormal * 2.0 - 1.0);
            
            float roughness = texRoughness.sample(textureSampler, hitRecord.uv, 0).r;
            roughness = max(roughness, 0.001);
            
            float3 nx, ny;
            CoordinateSystem(normal, nx, ny);
            float3x3 stw = { nx, ny, normal };
            
            normal = stw * tNormal;
            CoordinateSystem(normal, nx, ny);
            stw = { nx, ny, normal };
            
            float3 wi, wh;
            float3 wo = transpose(stw) * (-ray.direction);
        
            auto pSpecular = 1.0 / (2.0 - metallic);
            
            auto r0 = randomF(seed);
            auto r1 = randomF(seed);
            
            r0 = max(r0, FLT_EPSILON);
            auto rr0 = max(1-r0, FLT_EPSILON);
            
            auto a1 = roughness;
            auto a2 = a1 * a1;
            
            if (randomF(seed) > 1) {
                
                float sinTheta = sqrt(r0);
                float cosTheta = sqrt(rr0);
                
                float phi = 2.0 * M_PI_F * r1;
                
                float x = sinTheta * cos(phi);
                float y = sinTheta * sin(phi);
                float z = cosTheta;
                
                wi = { x, y, z };
                wh = normalize(wo + wi); // not specular
                
            } else {
                
                auto theta = acos(sqrt(rr0/((a2-1)*r0+1)));
                auto phi = 2 * M_PI_F * r1;
                
                auto x = sin(theta) * cos(phi);
                auto y = sin(theta) * sin(phi);
                auto z = cos(theta);
                
                wh = normalize( {x, y, z} );
                wi = reflect(-wo, wh);
            }
            
            if (wi.z <= 0 || wo.z <=0) { return false; }
        
            auto D = DX(wh, a2);
            auto G = GX(wo, wi, a2);
            auto F = FX(wi, wh, albedo, metallic);
            
            //auto G1 = SmithGGXMasking(wi, wo, a2);
            //auto G2 = SmithGGXMaskingShadowing(wi, wo, a2);
            
            float denominator = max(FLT_MIN, 4.0f * wo.z * wi.z);

            auto specular = D * G * F / denominator;
            auto diffuse = albedo / M_PI_F;
            
            auto kS = F;
            auto kD = (1.0 - metallic) * (1.0 - kS);

            ray = Ray(hitRecord.p, stw * wi);
            scatRecord.attenuation = (kD * diffuse + specular) * ao;
            
            //scatRecord.attenuation = G;
            //auto pdf = PDF(wh, roughness);
            //scatRecord.attenuation /= abs(pdf);
            
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
            
            specularDir = normalize(mix(specularDir, diffuseDir, pow(material.specularRoughness, 2)));
            
            auto refractionDir = refract(ray.direction, normal, theIOR);
            refractionDir = normalize(mix(refractionDir, normalize(-normal + randomUnit(seed)), pow(material.refractionRoughness, 2)));
                
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
