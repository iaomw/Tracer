#include "Random.hh"
#include <metal_relational>

static void CoordinateSystem(const thread float3& a, thread float3& b, thread float3& c) {
    
    if (abs(a.x) > abs(a.y))
        b = float3(-a.z, 0, a.x) /
              sqrt(a.x * a.x + a.z * a.z);
    else
        b = float3(0, a.z, -a.y) /
              sqrt(a.y * a.y + a.z * a.z);
    c = cross(a, b);
}

inline bool isInvalid(const thread float3& vector) {
    auto status = isnan(vector) || isinf(vector);
    return status.x || status.y || status.z;
}

struct Ray {
    float3 origin;
    float3 direction;
    //Float time;
    //const Medium *medium;
    
    Ray(float3 o, float3 d) {
        origin = o;
        direction = normalize(d);
    }
    
    float3 pointAt(float t) {
        return origin + direction * t;
    }
    
    bool isInvalid() {
        return ::isInvalid(origin) || ::isInvalid(direction);
    }
};

struct RayDifferential {
    Ray ray;
    bool hasDifferentials;
    float3 rxOrigin, ryOrigin;
    float3 rxDirection, ryDirection;
    
    void ScaleDifferentials(float s) {
        rxOrigin = ray.origin + (rxOrigin - ray.origin) * s;
        ryOrigin = ray.origin + (ryOrigin - ray.origin) * s;
        rxDirection = ray.direction + (rxDirection - ray.direction) * s;
        ryDirection = ray.direction + (ryDirection - ray.direction) * s;
    }
};

struct Camera {
    
    float3 lookFrom, lookAt, viewUp;
    
    float vfov;
    float aspect, aperture;
    float lenRadius, focus_dist;
    
    float3 u, v, w;
    
    float3 vertical;
    float3 horizontal;
    float3 cornerLowLeft;
};

static Ray castRay(constant Camera* camera, float s, float t, thread pcg32_random_t* seed) {
    //auto rd = camera->lenRadius * randomInUnitDiskFF(seed);
    auto rd = 0.005 * randomInUnitDiskFF(seed);
    auto offset = camera->u*rd.x + camera->v*rd.y;
    auto origin = camera->lookFrom + offset;
    auto sample = camera->cornerLowLeft + camera->horizontal*s + camera->vertical*t;
    Ray ray = Ray(origin, sample - origin);
    return ray;
}

enum struct TextureType { Constant, Checker, Noise, Image };
struct Texture {
    enum TextureType type;
    uint textureIndex;
    
    float3 value(texture2d<half, access::sample> outTexture,
                 float2 uv, float3 p) {
        return float3(0);
    }
};

enum struct MaterialType { Lambert, Metal, Dielectric, Diffuse, Isotropic, Specular};

struct Material {
    enum MaterialType type;
    
    float IOR;                 // index of refraction. used by fresnel and refraction.
    float3 albedo;
    
    float specularProb;
    float specularRoughness;
    float3  specularColor;
    
    float refractionProb;
    float refractionRoughness;
    float3  refractionColor;
    
    struct Texture texture;
};;

static float schlick(float cosine, float ref_idx) {
    auto r0 = (1-ref_idx) / (1+ref_idx);
    r0 = r0*r0;
    return r0 + (1-r0)*pow((1 - cosine),5);
}

static float FresnelReflectAmount(float n1, float n2, float3 normal, float3 incident, float f0, float f90)
{
        // Schlick aproximation
        float r0 = (n1-n2) / (n1+n2);
        r0 *= r0;
        float cosX = -dot(normal, incident);
        if (n1 > n2)
        {
            float n = n1/n2;
            float sinT2 = n*n*(1.0-cosX*cosX);
            // Total internal reflection
            if (sinT2 > 1.0)
                return f90;
            cosX = sqrt(1.0-sinT2);
        }
        float x = 1.0-cosX;
        float ret = r0+(1.0-r0)*x*x*x*x*x;
 
        // adjust reflect multiplier for object reflectivity
        return mix(f0, f90, ret);
}

struct HitRecord {
    float t;
    float3 p;
    
    bool front;
    float3 n;
    float2 uv;
        
    Material material;
    
    float3 normal() {
        return front? n:-n;
    }
    
    void checkFace(thread Ray& ray) {
        front = (dot(ray.direction, n) < 0);
    }
};

struct ScatterRecord {
    Ray specular = {float3(0), float3(0)};
    float prob;
    float3 attenuation;
    // pdf: PDF
};

enum class Axis { X=0, Y=1, Z=2 };
//const Axis AxisList[3]{Axis::X, Axis::Y, Axis::Z};

struct AABB {
    float3 mini;
    float3 maxi;
    
    bool hit(thread Ray& ray, float2 range_t) {
        
        for (auto i : {0, 1, 2}) {
            auto min_bound = (mini[i] - ray.origin[i])/ray.direction[i];
            auto max_bound = (maxi[i] - ray.origin[i])/ray.direction[i];
            
            auto ts = min_bound;
            auto te = max_bound;
            
            if (ray.direction[i] < 0.0) {
                ts = max_bound;
                te = min_bound;
            }
            
            auto tmin = metal::max(ts, range_t.x);
            auto tmax = metal::min(te, range_t.y);
            
            if (tmax <= tmin) { return false; }
        }
        
        return true;
    }
};

struct Square {
    
    uint8_t axis_i;
    uint8_t axis_j;
    float2 rang_i;
    float2 rang_j;
    
    uint8_t axis_k;
    float value_k;
    
    float4x4 model_matrix;
    float4x4 normal_matrix;
    float4x4 inverse_matrix;
    
    AABB boundingBOX;
    Material material;
    
    bool hit_test(thread Ray& ray, thread float2& range_t, thread HitRecord& hitRecord) {
        
        //if (!boundingBOX.hit(ray, range_t)) {return false;}
        
        auto t = (value_k-ray.origin[axis_k]) / ray.direction[axis_k];
        
        if (t<range_t.x || t>range_t.y) {return false;}
        
        auto a = ray.origin[axis_i] + t*ray.direction[axis_i];
        auto b = ray.origin[axis_j] + t*ray.direction[axis_j];
        
        if (a<rang_i.x || a>rang_i.y || b<rang_j.x || b>rang_j.y) {return false;}
        
        hitRecord.uv[0] = (a-rang_i.x)/(rang_i.y-rang_i.x);
        hitRecord.uv[1] = (b-rang_j.x)/(rang_j.y-rang_j.x);
        
        hitRecord.t = t;
        auto normal = float3(0); normal[axis_k]=1;
        hitRecord.n = normal;
        hitRecord.checkFace(ray);
        hitRecord.p = ray.pointAt(t);
        hitRecord.material = material;
        
        return true;
    }
};

struct Cube {
    float3 a;
    float3 b;
    
    float4x4 model_matrix;
    float4x4 normal_matrix;
    float4x4 inverse_matrix;
    
    AABB boundingBOX;
    
    Square rectList[6];
    
    bool hit_test(thread Ray& ray, thread float2& range_t, thread HitRecord& hitRecord) {
        
        auto origin = inverse_matrix * float4(ray.origin, 1.0);
        auto direction = inverse_matrix * float4(ray.direction, 0.0);
        Ray transformedRay = Ray(origin.xyz, direction.xyz);
        
        if(!boundingBOX.hit(transformedRay, range_t)) {return false;}
    
        auto nearest = range_t.y;
        HitRecord testHitResult;
        
        for (auto rect : rectList) {
            if (!rect.hit_test(transformedRay, range_t, testHitResult)) {continue;}
            if (testHitResult.t >= nearest) {continue;}
            hitRecord = testHitResult;
            nearest = testHitResult.t;
        }
        
        if (nearest >= range_t.y) {
            return false;
        }
        
        hitRecord.checkFace(transformedRay);
        hitRecord.p = ray.pointAt(hitRecord.t);
        //hitRecord.p = (model_matrix * float4(hitRecord.p, 1.0)).xyz;
        //hitRecord.p = (float4(hitRecord.p, 1.0) * model_matrix).xyz;
        hitRecord.n = normalize((normal_matrix * float4(hitRecord.normal(), 0.0)).xyz);
        //hitRecord.n = normalize((float4(hitRecord.normal(), 0.0) * normal_matrix).xyz);
        
        return true;
    }
};

static void sphereUV(thread float3& p, thread float2& uv) {
    auto phi = atan2(p.z, p.x);
    auto theta = asin(p.y);
    uv[0] = 1-(phi + M_PI_F) / (2*M_PI_F);
    uv[1] = (theta + M_PI_2_F) / M_PI_F;
}

struct Sphere {
    float radius;
    float3 center;
    
    float4x4 model_matrix;
    float4x4 normal_matrix;
    float4x4 inverse_matrix;
    
    AABB boundingBOX;
    Material material;
    
    bool hit_test(thread Ray& ray, float2 rang_t, thread HitRecord& hitRecord) {
        
        if(!boundingBOX.hit(ray, rang_t)) { return false; }
        
        float3 oc = ray.origin - center;
    
        auto a = length_squared(ray.direction);
        auto half_b = dot(oc, ray.direction);
        auto c = length_squared(oc) - radius*radius;

        auto discriminant = half_b*half_b - a*c;
        if (discriminant <= 0) { return false; }
        
        auto t_min = rang_t.x;
        auto t_max = rang_t.y;

        auto root = sqrt(discriminant);

        auto temp = (-half_b - root)/a;
        if (temp < t_max && temp > t_min) {
            hitRecord.t = temp;
            hitRecord.p = ray.pointAt(hitRecord.t);
            hitRecord.n = (hitRecord.p-center)/radius;
            hitRecord.checkFace(ray);
            sphereUV(hitRecord.n, hitRecord.uv);
            hitRecord.material = material;
            return true;
        }

        temp = (-half_b + root)/a;
        if (temp < t_max && temp > t_min) {
            hitRecord.t = temp;
            hitRecord.p = ray.pointAt(hitRecord.t);
            hitRecord.n = (hitRecord.p-center)/radius;
            hitRecord.checkFace(ray);
            sphereUV(hitRecord.n, hitRecord.uv);
            hitRecord.material = material;
            return true;
        }
        
        return false;
    }
};

static bool emit_test(thread HitRecord& hitRecord, thread float3& color) {
    
    switch(hitRecord.material.type) {
        case MaterialType::Diffuse: {
            color = hitRecord.material.albedo;
            return true;
        }
        case MaterialType::Metal:
        case MaterialType::Lambert:
        case MaterialType::Isotropic:
        case MaterialType::Dielectric:
        default:{
            return false;
        }
    }
}
    
static bool scatter(thread Ray& ray,
                    thread HitRecord& hitRecord,
                    thread ScatterRecord& scatterRecord,
                    thread pcg32_random_t* seed)
{
    auto material = hitRecord.material;
    auto normal = hitRecord.normal();
    
    switch(material.type) {
            
        case MaterialType::Lambert: {
            
            auto direction = normal + randomUnit(seed);
            //normal + randomInHemisphere(normal, seed);
            auto scattered = Ray(hitRecord.p, direction);
            //auto attenuation = material.texture.value(hitRecord.uv, hitRecord.p);
            scatterRecord.specular = scattered;
            scatterRecord.attenuation = material.albedo;//attenuation;
            return true;
        }
        case MaterialType::Metal: {
            auto fuzz = 0.01 * randomInUnitSphereFFF(seed);
            auto reflected = reflect(ray.direction, normal);
            auto scattered = Ray(hitRecord.p, reflected + fuzz);
            auto attenuation = material.albedo;
            scatterRecord.specular = scattered;
            scatterRecord.attenuation = attenuation;
            return true;//(dot(scattered.direction, hitRecord.normal()) > 0);
        }
        case MaterialType::Dielectric: {
            
            auto attenuation = material.albedo;
            auto theIOR = hitRecord.front? (1.0/material.IOR) : material.IOR;
            
            auto unit_direction = ray.direction;
            auto cos_theta = min(dot(-unit_direction, normal), 1.0);
            auto sin_theta = sqrt(1.0 - cos_theta*cos_theta);
            
            if (theIOR * sin_theta > 1.0 ) {
                auto reflected = reflect(unit_direction, normal);
                auto scattered = Ray(hitRecord.p, reflected);
                scatterRecord.specular = scattered;
                scatterRecord.attenuation = attenuation;
                return true;
            }

            auto reflect_prob = schlick(cos_theta, theIOR);
            if (randomF(seed) < reflect_prob) {
                auto reflected = reflect(unit_direction, normal);
                auto scattered = Ray(hitRecord.p, reflected);
                scatterRecord.specular = scattered;
                scatterRecord.attenuation = attenuation;
                return true;
            }

            auto refracted = refract(unit_direction, normal, theIOR);
            auto scattered = Ray(hitRecord.p, refracted);
            scatterRecord.specular = scattered;
            scatterRecord.attenuation = attenuation;
            return true;
        }
        case MaterialType::Diffuse: {
            return false;
        }
        case MaterialType::Isotropic: {

            auto scattered = Ray(hitRecord.p, randomInUnitSphereFFF(seed));
            //auto attenuation = material.texture.value(hitRecord.uv, hitRecord.p);
            scatterRecord.specular = scattered;
            scatterRecord.attenuation = hitRecord.material.albedo; //attenuation;
            return true;
        }
            
        case MaterialType::Specular: {
            
            //normal = hitRecord.n;
            float rayProbability = 1.0f;
            auto throughput = float3(1.0);
            
            auto theIOR = hitRecord.front? (1.0/material.IOR) : material.IOR;
            
            if (!hitRecord.front) {
                throughput *= exp(-hitRecord.material.refractionColor * hitRecord.t);
            }
            
            // apply fresnel
            float specularProb = hitRecord.material.specularProb;
            float refractionProb = hitRecord.material.refractionProb;
            
            if (specularProb > 0.0f) {
                
                specularProb = FresnelReflectAmount(
                        hitRecord.front? 1.0 : hitRecord.material.IOR,
                        !hitRecord.front? 1.0 : hitRecord.material.IOR,
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
            
            auto scattered = Ray(origin, direction);
            scatterRecord.specular = scattered;
            scatterRecord.attenuation = throughput;
            scatterRecord.prob = rayProbability;
            
            if (doRefraction == 0.0f) {
                scatterRecord.attenuation *= mix(hitRecord.material.albedo,
                                                 hitRecord.material.specularColor,
                                                 doSpecular);
            }
            
            scatterRecord.attenuation /= rayProbability;
            
            return true;
        }
            
        default: {return false;}
    }
}
