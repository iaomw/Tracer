#ifndef Cube_h
#define Cube_h

#include "Common.hh"

struct Cube {
    
    float4x4 model_matrix;
    float4x4 normal_matrix;
    float4x4 inverse_matrix;
    
    AABB box;
    uint material;
    
#ifdef __METAL_VERSION__
    
    bool hit_test(const thread Ray& ray, thread float2& range_t, thread HitRecord& hitRecord) constant {
        
        auto origin = inverse_matrix * float4(ray.origin, 1.0);
        auto direction = inverse_matrix * float4(ray.direction, 0.0);
        
        HitRecord _record;
        auto _ray = Ray(origin.xyz, direction.xyz);
        if(!box.hit(_ray, range_t, _record)) { return false; }
        
        _record._p = _record.p;
        auto p = float4(_record.p, 1.0);
        _record.p = (model_matrix * p).xyz;
        
        _record._r = _ray;
        _record._t = _record.t;
        
        _record.t = distance(ray.origin, _record.p);
        
        if (_record.t < range_t.y) {
            range_t.y = _record.t;
        } else {
            return false;
        }
        
        _record.material = material;
        _record.modelMatrix = model_matrix;
        
        auto normal = float4(_record.n, 0.0);
        _record.n = normalize((normal_matrix * normal).xyz);
        _record.checkFace(ray);
        
        hitRecord = _record;
        return true;
    }
    
#endif

};

#endif /* Cube_h */
