#include <metal_stdlib>
#include "Random.hh"

using namespace metal;

struct Ray {
    float3 origin;
    float3 direction;
    
    float3 pointAt(float t) {
        return origin + direction * t;
    }
};

static Ray MakeRay(float3 origin, float3 direction) {
    Ray r;
    r.origin = origin;
    r.direction = direction;
    return r;
}

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
    auto rd = camera->lenRadius*randomInUnitDiskFF(seed);
    auto offset = camera->u*rd.x + camera->v*rd.y;
    auto origin = camera->lookFrom + offset;
    auto sample = camera->cornerLowLeft + camera->horizontal*s + camera->vertical*t;
    auto direction = simd::normalize(sample-origin);
    Ray ray = MakeRay(origin, direction);
    return ray;
}

enum struct TextureType { Constant, Checker, Noise, Image };
struct Texture {
    enum TextureType type;
    
    float3 value(float2 uv, float3 p) {
        return float3(0);
    }
};

enum struct MaterialType { Lambert, Metal, Dielectric, Diffuse, Isotropic };
struct Material {
    enum MaterialType type;
    
    float3 albedo;
    float refractive;
    Texture texture;
};

static float schlick(float cosine, float ref_idx) {
    auto r0 = (1-ref_idx) / (1+ref_idx);
    r0 = r0*r0;
    return r0 + (1-r0)*pow((1 - cosine),5);
}

struct HitRecord {
    float t;
    float3 p;
    float3 n;
    
    float2 uv;
    bool front;
    
    Material material;
    
    void checkFace(Ray ray) {
        if( dot(ray.direction, n) < 0 ){
            front = false;
        } else {
            front = true;
        }
    }
};

struct ScatterRecord {
    Ray specularRay;
    float3 attenuation;
    // pdf: PDF
};

enum class Axis { X=0, Y=1, Z=2 };
//const Axis AxisList[3]{Axis::X, Axis::Y, Axis::Z};

struct AABB {
    float3 mini;
    float3 maxi;
    
    bool hit(Ray ray, float2 range_t) {
        
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
            
            return tmax > tmin;
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
    
    float3x3 matrix;
    AABB boundingBOX;
    Material material;
    
    bool hit(thread Ray& ray, float2 rang_t, thread HitRecord& hitRecord) {
        
        auto t = (value_k-ray.origin[axis_k]) / ray.direction[axis_k];
        
        if (t<rang_t.x || t>rang_t.y) {return false;}
        
        auto a = ray.origin[axis_i] + t*ray.direction[axis_i];
        auto b = ray.origin[axis_j] + t*ray.direction[axis_j];
        
        if (a<rang_i.x || a>rang_i.y || b<rang_j.x || b>rang_j.y) {return false;}
        auto uv = float2(0);
        uv[0] = (a-rang_i.x)/(rang_i.y-rang_i.x);
        uv[1] = (b-rang_j.x)/(rang_j.y-rang_j.x);
        hitRecord.uv = uv;
        hitRecord.t = t;
        auto normal = float3(0); normal[axis_k]=1;
        hitRecord.n = normal;
        hitRecord.material = material;
        hitRecord.p = ray.pointAt(t);
        
        hitRecord.front = (dot(ray.direction, normal)<0);
        return true;
    }
};



struct Cube {
    float3 a;
    float3 b;
    
    float3x3 matrix;
    AABB boundingBOX;
    
    struct Square rectList[6];
    
    bool hit(Ray ray, float2 rang_t, thread HitRecord& hitRecord) {
        auto nearest = FLT_MAX;
        HitRecord hitResult;
        for (auto rect : rectList) {
            if (!rect.hit(ray, rang_t, hitResult)) {continue;}
            if (hitRecord.t >= nearest) {continue;}
            hitRecord = hitResult;
            nearest = hitResult.t;
        }
        return nearest < FLT_MAX;
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
    
    float3x3 matrix;
    AABB boundingBOX;
    Material material;
    
    bool hit(Ray ray, float2 rang_t, thread HitRecord& hitRecord) {
        float3 oc = ray.origin - center;
    
        auto a = length_squared(ray.direction);
        auto half_b = dot(oc, ray.direction);
        auto c = length_squared(oc) - radius*radius;

        auto discriminant = half_b*half_b - a*c;
        auto t_min = rang_t.x;
        auto t_max = rang_t.y;
        if (discriminant > 0) {
            auto root = sqrt(discriminant);

            auto temp = (-half_b - root)/a;
            if (temp < t_max && temp > t_min) {
                hitRecord.t = temp;
                hitRecord.p = ray.pointAt(hitRecord.t);
                hitRecord.n = (hitRecord.p-center)/radius;
                hitRecord.checkFace(ray);
                //hitRecord.set_face_normal(ray, outward_normal);
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
                //hitRecord.set_face_normal(ray, outward_normal);
                sphereUV(hitRecord.n, hitRecord.uv);
                hitRecord.material = material;
                return true;
            }
        }

        return false;
    }
};

static float3 emit(thread HitRecord& hitRecord) {
    
    switch(hitRecord.material.type) {
        case MaterialType::Lambert: {
            return float3(0);
        }
        case MaterialType::Metal: {}
        case MaterialType::Dielectric: {}
        case MaterialType::Diffuse: {
            return hitRecord.material.albedo;
        }
        case MaterialType::Isotropic: {}
        default:{
            return float3(0);
        }
    }
    return float3(0);
}
    
static bool scatter(thread Ray& ray,
                    thread HitRecord& hitRecord,
                    thread ScatterRecord& scatterRecord,
                    thread pcg32_random_t* seed)
{
    auto material = hitRecord.material;
    switch(material.type) {
        case MaterialType::Lambert: {
            
            float3 normal;
            if (hitRecord.front) {
                normal = hitRecord.n;
            } else {
                normal = -hitRecord.n;
            }
            
            auto scatter_direction = normal + randomInUnitSphereFFF(seed);
            auto scattered = MakeRay(hitRecord.p, scatter_direction);
            //auto attenuation = material.texture.value(hitRecord.uv, hitRecord.p);
            scatterRecord.specularRay = scattered;
            scatterRecord.attenuation = material.albedo;//attenuation;
            return true;
        }
        case MaterialType::Metal: {
            auto fuzz = 0.4;
            auto reflected = reflect(normalize(ray.direction), hitRecord.n);
            auto scattered = MakeRay(hitRecord.p, reflected + fuzz*randomInUnitSphereFFF(seed));
            auto attenuation = material.albedo;
            scatterRecord.specularRay = scattered;
            scatterRecord.attenuation = attenuation;
            return (dot(scattered.direction, hitRecord.n) > 0);
        }
        case MaterialType::Dielectric: {
            auto attenuation = float3(1);
            auto etai_over_etat = hitRecord.front? (1.0/material.refractive):material.refractive;
            
            auto unit_direction = normalize(ray.direction);
            auto cos_theta = min(dot(-unit_direction, hitRecord.n), 1.0);
            auto sin_theta = sqrt(1.0 - cos_theta*cos_theta);
            
            if (etai_over_etat * sin_theta > 1.0 ) {
                auto reflected = reflect(unit_direction, hitRecord.n);
                auto scattered = MakeRay(hitRecord.p, reflected);
                scatterRecord.specularRay = scattered;
                scatterRecord.attenuation = attenuation;
                return true;
            }

            auto reflect_prob = schlick(cos_theta, etai_over_etat);
            if (randomF(seed) < reflect_prob) {
                auto reflected = reflect(unit_direction, hitRecord.n);
                auto scattered = MakeRay(hitRecord.p, reflected);
                scatterRecord.specularRay = scattered;
                scatterRecord.attenuation = attenuation;
                return true;
            }

            auto refracted = refract(unit_direction, hitRecord.n, etai_over_etat);
            auto scattered = MakeRay(hitRecord.p, refracted);
            scatterRecord.specularRay = scattered;
            scatterRecord.attenuation = attenuation;
            return true;
            
        }
        case MaterialType::Diffuse: {
            return false;
        }
        case MaterialType::Isotropic: {
            auto scattered = MakeRay(hitRecord.p, randomInUnitSphereFFF(seed));
            auto attenuation = material.texture.value(hitRecord.uv, hitRecord.p);
            scatterRecord.specularRay = scattered;
            scatterRecord.attenuation = attenuation;
            return true;
        }
        default: {}
    }
    return false;
}
