#ifndef Triangle_h
#define Triangle_h

#include "Common.hh"

#include "Ray.hh"
#include "HitRecord.hh"

constant float kEpsilon = FLT_EPSILON; //1e-8;

struct TriangleVertex {
    
    packed_float3 v;
    packed_float3 n;

    packed_float2 uv;
};

struct Triangle {
    constant TriangleVertex *_a;
    constant TriangleVertex *_b;
    constant TriangleVertex *_c;
    
    Triangle(constant TriangleVertex* tv, thread uint3& abc) {
        _a = &tv[abc.x];
        _b = &tv[abc.y];
        _c = &tv[abc.z];
    }
    
    bool hit_test(const thread Ray& ray,
                        thread float2& range_t,
                        thread HitRecord& hitRecord)
    {
        
        const thread float3 &ori = ray.origin;
        const thread float3 &dir = ray.direction;
        
        constant auto& v0 = _a->v;
        constant auto& v1 = _b->v;
        constant auto& v2 = _c->v;
        
        float3 v0v1 = (v1 - v0);
        float3 v0v2 = (v2 - v0);
        
        float3 pvec = cross(dir, v0v2);
        float det = dot(v0v1, pvec);
        
        #ifdef CULLING
            // if the determinant is negative the triangle is backfacing
            // if the determinant is close to 0, the ray misses the triangle
            if (det < kEpsilon) return false;
        #else
            // ray and triangle are parallel if det is close to 0
            if (fabs(det) < kEpsilon) return false;
        #endif
        
        float invDet = 1 / det;
     
        float3 tvec = ori - v0;
        float u = dot(tvec, pvec) * invDet;
        if (u < 0 || u > 1) return false;
     
        float3 qvec = cross(tvec, v0v1);
        float v = dot(dir, qvec) * invDet;
        if (v < 0 || (u + v) > 1) return false;
        
        float w = 1.0 - u - v;
        auto t = dot(v0v2, qvec) * invDet; //t = abs(t);
    
        if (t > range_t.y || t < range_t.x) { return false; }
    
        range_t.y = t;
        
        hitRecord.t = t;
        hitRecord.p = ray.pointAt(t);
    
        hitRecord.n = u * _b->n + v * _c->n + w * _a->n;
        hitRecord.uv = u * _b->uv + v * _c->uv + w * _a->uv;
        
        hitRecord.checkFace(ray);
        hitRecord.material = 19;
         
        return true;
    }
};

#endif
