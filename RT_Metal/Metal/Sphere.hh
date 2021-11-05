#ifndef Sphere_h
#define Sphere_h

#include "Common.hh"

struct Sphere {
    float radius;
    float3 center;
    
    float4x4 model_matrix;
    float4x4 normal_matrix;
    float4x4 inverse_matrix;
    
    uint material;
    AABB boundingBOX;
    
#ifdef __METAL_VERSION__
    
    void sphereUV(thread float3& p, thread float2& uv) constant {
        auto phi = atan2(p.z, p.x);
        auto theta = asin(p.y);
        uv[0] = 1-(phi + M_PI_F) / (2*M_PI_F);
        uv[1] = (theta + M_PI_2_F) / M_PI_F;
    }
    
    void sphereUV(thread packed_float3& p, thread float2& uv) constant {
        auto phi = atan2(p.z, p.x);
        auto theta = asin(p.y);
        uv[0] = 1-(phi + M_PI_F) / (2*M_PI_F);
        uv[1] = (theta + M_PI_2_F) / M_PI_F;
    }
    
    bool hit_test(const thread Ray& ray, thread float2& range_t, thread HitRecord& hitRecord) constant {
        
        float3 oc = ray.origin - center;
    
        auto a = length_squared(ray.direction);
        auto half_b = dot(oc, ray.direction);
        auto c = length_squared(oc) - radius*radius;

        auto discriminant = half_b*half_b - a*c;
        if (discriminant <= 0) { return false; }
        
        auto t_min = range_t.x;
        auto t_max = range_t.y;

        auto root = sqrt(discriminant);

        auto temp = (-half_b - root)/a;
        if (temp < t_max && temp > t_min) {
            hitRecord.t = temp;
            hitRecord.p = ray.pointAt(hitRecord.t);
            hitRecord.gn = (hitRecord.p-center)/radius;
            hitRecord.checkFace(ray);
            sphereUV(hitRecord.gn, hitRecord.uv);
            hitRecord.material = material;
            
            range_t.y = hitRecord.t;
            
            return true;
        }

        temp = (-half_b + root)/a;
        if (temp < t_max && temp > t_min) {
            hitRecord.t = temp;
            hitRecord.p = ray.pointAt(hitRecord.t);
            hitRecord.gn = (hitRecord.p-center)/radius;
            hitRecord.checkFace(ray);
            sphereUV(hitRecord.gn, hitRecord.uv);
            hitRecord.material = material;
            
            range_t.y = hitRecord.t;
            
            return true;
        }
        
        return false;
    }
    
#endif
    
};

#endif /* Sphere_h */
