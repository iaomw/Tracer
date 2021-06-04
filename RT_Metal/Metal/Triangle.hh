#ifndef Triangle_h
#define Triangle_h

#include "Common.hh"

#include "Ray.hh"
#include "HitRecord.hh"

constant float kEpsilon = 1e-8;

struct Triangle {
    
    packed_float3 v;
    packed_float3 n;

    packed_float2 uv;

    static bool hitTest(const thread Ray& ray,
                                     
                                     constant Triangle& x_a,
                                     constant Triangle& x_b,
                                     constant Triangle& x_c,
                                     
                                     thread float2& range_t,
                                     
                                     thread HitRecord& hitRecord)
    {
        
        const thread float3 &orig = ray.origin;
        const thread float3 &dir = ray.direction;
        
        thread auto& v0 = x_a.v;
        thread auto& v1 = x_b.v;
        thread auto& v2 = x_c.v;
        
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
           // if (fabs(det) < kEpsilon) return false;
            if (fabs(det) < kEpsilon) return false;
        #endif
        
        float invDet = 1 / det;
     
        float3 tvec = orig - v0;
        float u = dot(tvec, pvec) * invDet;
        if (u < 0 || u > 1) return false;
     
        float3 qvec = cross(tvec, v0v1);
        float v = dot(dir, qvec) * invDet;
        if (v < 0 || (u + v) > 1) return false;
        
        float w = 1.0 - u - v;
        auto t = dot(v0v2, qvec) * invDet;
        
        //t = abs(t);
    
        if (t > range_t.y || t < range_t.x) { return false; }
    
        range_t.y = t;
        
        hitRecord.t = t;
        hitRecord.p = ray.pointAt(t);
    
        hitRecord.uv = u * x_b.uv + v * x_c.uv + w * x_a.uv;
        hitRecord.n = u * x_b.n + v * x_c.n + w * x_a.n;
        //hitRecord.n = normalize(hitRecord.n);
        
        hitRecord.checkFace(ray);
        hitRecord.material = 19;
         
        return true;
    }
};

#endif
