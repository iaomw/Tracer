#ifndef HitRecord_h
#define HitRecord_h

#include "Ray.hh"
#include "Material.hh"

#ifdef __METAL_VERSION__

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
    
    void checkFace(const thread Ray& ray) {
        front = (dot(ray.direction, n) < 0);
    }
};

struct ScatRecord {
    float3 attenuation;
    // pdf: PDF
};

bool emit(thread HitRecord& hitRecord, thread float3& color);

constant float kEpsilon = 1e-8;

struct MeshEle {
    
    packed_float3 v;
    packed_float3 n;

    packed_float2 uv;

    static bool rayTriangleIntersect(const thread Ray& ray,
                                     
                                     constant MeshEle& x_a,
                                     constant MeshEle& x_b,
                                     constant MeshEle& x_c,
                                     
                                     thread float2& range_t,
                                     
                                     thread HitRecord& hitRecord)
    {
        
        const thread float3 &orig = ray.origin;
        const thread float3 &dir = ray.direction;
        
        float3 offset = float3(0);//float3(500, 200, 200);
        float3 scale = float3(8); float3(20, 20, 20);
        
        auto v0 = x_a.v * scale + offset;
        auto v1 = x_b.v * scale + offset;
        auto v2 = x_c.v * scale + offset;
        
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
    
        if (t >= range_t.y || t < FLT_MIN) { return false; }
    
        range_t.y = t;
        
        hitRecord.t = t;
        hitRecord.p = ray.pointAt(t);
    
        hitRecord.uv = u * x_b.uv + v * x_c.uv + w * x_a.uv;
        hitRecord.n = u * x_b.n + v * x_c.n + w * x_a.n;
        
        //hitRecord.n = normalize( x_a.n + x_b.n + x_c.n );
        hitRecord.checkFace(ray);
        
        Material material;
        //material.type= MaterialType::Metal;
        material.type= MaterialType::Lambert;
        material.textureInfo.type = TextureType::Constant;
        material.textureInfo.albedo = float3(1.0);
        
        hitRecord.material = material;
         
        return true;
    }
    
};
    
#endif

#endif /* HitRecord_h */
