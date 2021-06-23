#ifndef Square_h
#define Square_h

#include "Common.hh"

struct Square {
    
    uint8_t axis_i;
    uint8_t axis_j;
    float2 range_i;
    float2 range_j;
    
    uint8_t axis_k;
    float value_k;
    
    float4x4 model_matrix;
    float4x4 normal_matrix;
    float4x4 inverse_matrix;
    
    uint material;
    AABB boundingBOX;
    
#ifdef __METAL_VERSION__
    
    #include "Sampling.hh"
    
    float area() constant {
        auto i = range_i[1] - range_i[0];
        auto j = range_j[1] - range_j[0];
        
        return i * j;
    }
    
    float pdf() constant { return 1 / area(); }
    
    void sample(const thread float2& u, const thread float3& pos, thread LightSampleRecord& lsr) constant {
        
        lsr.p[axis_k] = value_k;
        
        lsr.p[axis_i] = range_i.x + u[0] * (range_i.y - range_i.x);
        lsr.p[axis_j] = range_j.x + u[1] * (range_j.y - range_j.x);
        
        lsr.n = float3(0);
        lsr.n[axis_k] = 1;
        
        auto w = normalize(pos - lsr.p);
        
        if ( dot(w, lsr.n) < 0 ) {
            lsr.n = -lsr.n;
        }
    }
    
    bool hit_test(const thread Ray& ray, thread float2& range_t, thread HitRecord& hitRecord) constant {
        
        auto t = (value_k-ray.origin[axis_k]) / ray.direction[axis_k];
        if (t<range_t.x || t>range_t.y) { return false; }
        
        auto a = ray.origin[axis_i] + t*ray.direction[axis_i];
        if (a<range_i.x || a>range_i.y) { return false; }

        auto b = ray.origin[axis_j] + t*ray.direction[axis_j];
        if (b<range_j.x || b>range_j.y) { return false; }
        
        hitRecord.uv[0] = (a-range_i.x)/(range_i.y-range_i.x);
        hitRecord.uv[1] = (b-range_j.x)/(range_j.y-range_j.x);
        
        hitRecord.t = t;
        
        hitRecord.n = float3(0);
        hitRecord.n[axis_k] = 1;
        hitRecord.checkFace(ray);
        hitRecord.p = ray.pointAt(t);
        hitRecord.material = material;
        
        range_t.y = hitRecord.t;
        
        return true;
    }
    
#endif
    
};

#endif /* Square_h */
