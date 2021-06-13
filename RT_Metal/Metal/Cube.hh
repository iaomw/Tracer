#ifndef Cube_h
#define Cube_h

#include "Common.hh"

struct Cube {
    
    float4x4 model_matrix;
    float4x4 normal_matrix;
    float4x4 inverse_matrix;
    
    AABB box;
    uint material;
    AABB boundingBOX;
    
#ifdef __METAL_VERSION__
    
    bool hit_test(const thread Ray& ray, thread float2& range_t, thread HitRecord& hitRecord) constant {
        
        auto origin = inverse_matrix * float4(ray.origin, 1.0);
        auto direction = inverse_matrix * float4(ray.direction, 0.0);
        
        Ray testRay = Ray(origin.xyz, direction.xyz);
        
        if(!box.hit(testRay, range_t, hitRecord)) {return false;}
        
        hitRecord.material = material;
        hitRecord.p = ray.pointAt(hitRecord.t);
        
        hitRecord.checkFace(testRay);
        auto normal = float4(hitRecord.normal(), 0.0);
        hitRecord.n = normalize((normal_matrix * normal).xyz);
        
        range_t.y = hitRecord.t;
        
        return true;
    }
    
    bool hit_medium(thread Ray& ray, thread float2& range_t, thread HitRecord& hitRecord, thread pcg32_t* seed) constant {
        
        auto origin = inverse_matrix * float4(ray.origin, 1.0);
        auto direction = inverse_matrix * float4(ray.direction, 0.0);
        
        Ray testRay = Ray(origin.xyz, direction.xyz);

        HitRecord rec1, rec2;
        
        if (!boundingBOX.hit(testRay, float2(-FLT_MAX, FLT_MAX), rec1))
            return false;

        if (!boundingBOX.hit(testRay, float2(rec1.t+0.0001, FLT_MAX), rec2))
            return false;
        
        rec1.t = max(rec1.t, range_t.x);
        rec2.t = min(rec2.t, range_t.y);

        if (rec1.t >= rec2.t) return false;

        rec1.t = max(rec1.t, 0.0f);
        
        auto neg_inv_density = -1.0f/0.01f;

        const auto ray_length = length(testRay.direction);
        const auto distance_inside = (rec2.t - rec1.t) * ray_length;
        //const auto hit_distance = neg_inv_density * log( randomF(seed) );
        //const auto hit_distance = -100 * log(0.99999 + 0.00002 * randomF(seed));
        const auto hit_distance = neg_inv_density * log(0.99999 + 0.00002 * randomF(seed));
        
        if (hit_distance > distance_inside) {
            return false;
        }

        hitRecord.t = rec1.t + hit_distance / ray_length;
        hitRecord.p = ray.pointAt(hitRecord.t);
        
        hitRecord.n = float3(0,0,0);
        hitRecord.material = material;
        
        range_t.y = hitRecord.t;
        
        return true;
    }
    
#endif

};

#endif /* Cube_h */
