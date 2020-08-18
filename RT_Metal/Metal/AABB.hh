#ifndef AABB_h
#define AABB_h

#include "Common.hh"
#include "HitRecord.hh"

struct AABB {
    float3 mini;
    float3 maxi;
    
#ifdef __METAL_VERSION__
    
    // https://gamedev.stackexchange.com/questions/18436/most-efficient-aabb-vs-ray-collision-algorithms/
    bool hit(thread Ray& ray, const thread float2& range_t) constant {
        
        auto inverse = 1.0 / ray.direction;

        auto ts = (mini - ray.origin) * inverse;
        auto te = (maxi - ray.origin) * inverse;

        float tmin = max3( min(ts[0], te[0]), min(ts[1], te[1]), min(ts[2], te[2]));
        float tmax = min3( max(ts[0], te[0]), max(ts[1], te[1]), max(ts[2], te[2]));
        
        tmin = max(tmin, range_t.x);
        tmax = min(tmax, range_t.y);

        return ! (tmax < tmin || tmax < 0);
        
//        float tmin = range_t.x, tmax = range_t.y;
//
//        for (auto i : {0, 1, 2}) {
//
//            auto min_bound = (mini[i] - ray.origin[i])/ray.direction[i];
//            auto max_bound = (maxi[i] - ray.origin[i])/ray.direction[i];
//
//            auto ts = min(max_bound, min_bound);
//            auto te = max(max_bound, min_bound);
//
//            tmin = max(ts, tmin);
//            tmax = min(te, tmax);
//
//            if (tmax < tmin || tmax < 0) { return false; }
//        }
//
//        return true;
    }
    
    bool hit_get_t(const thread Ray& ray, const thread float2& range_t, thread float& t) constant {
        
        auto inverse = 1.0 / ray.direction;

        auto ts = (mini - ray.origin) * inverse;
        auto te = (maxi - ray.origin) * inverse;

        float tmin = max3( min(ts[0], te[0]), min(ts[1], te[1]), min(ts[2], te[2]));
        float tmax = min3( max(ts[0], te[0]), max(ts[1], te[1]), max(ts[2], te[2]));
        
        tmin = max(tmin, range_t.x);
        tmax = min(tmax, range_t.y);

        if (tmax < tmin || tmax < 0) { return false; }
        t = (tmin < 0)? tmax : tmin;
        return true;
        
//        float tmin = range_t.x, tmax = range_t.y;
//
//        for (auto i : {0, 1, 2}) {
//
//            auto min_bound = (mini[i] - ray.origin[i])/ray.direction[i];
//            auto max_bound = (maxi[i] - ray.origin[i])/ray.direction[i];
//
//            auto ts = min(max_bound, min_bound);
//            auto te = max(max_bound, min_bound);
//
//            tmin = max(ts, tmin);
//            tmax = min(te, tmax);
//
//            if (tmax <= tmin || tmax < 0) { return false; }
//        }
//
//        range_t.x = tmin;
//        range_t.y = tmax;
//
//        t = (tmin < 0)? tmax : tmin;
//
//        return true;
    }
    
    bool hit(thread Ray& ray, const thread float2& range_t, thread HitRecord& record) constant {
        
        float tmin = range_t.x;
        float tmax = range_t.y;
        
        uint axisPick = 0;
        float bound_min = 0.0;
        float bound_max = 0.0;
        
        for (auto i : {0, 1, 2}) {
            
            auto min_bound = (mini[i] - ray.origin[i])/ray.direction[i];
            auto max_bound = (maxi[i] - ray.origin[i])/ray.direction[i];
            
            auto ts = min(max_bound, min_bound);
            auto te = max(max_bound, min_bound);
            
            //tmin = max(ts, tmin);
            tmax = min(te, tmax);
            
            if (ts > tmin) {
                
                tmin = ts;
                axisPick = i;
                bound_min = min_bound;
                bound_max = max_bound;
            }
            
            //if (tmax < tmin) { return false; }
            if (tmax <= tmin || tmax < 0) { return false; }
        }
        
        record.t = tmin < 0 ? tmax : tmin; // internal or external
        record.n = float3(0); record.n[axisPick] = bound_min < bound_max ? -1:1;
        
        auto hitPoint = ray.pointAt(record.t);
        
        auto ratio = (hitPoint - mini) / (maxi - mini);
        ratio[axisPick] = 0;
        
        uint2 axisUV = (uint2(1, 2) + axisPick) % 3;
        record.uv = float2(ratio[axisUV.x], ratio[axisUV.y]);
        
        return true;
    }
    
#else
    
    static AABB make(float3& a, float3& b) {
        
        auto mini = simd_make_float3(fminf(a.x, b.x),
                                      fminf(a.y, b.y),
                                      fminf(a.z, b.z));
        
        auto maxi = simd_make_float3(fmaxf(a.x, b.x),
                                     fmaxf(a.y, b.y),
                                     fmaxf(a.z, b.z));
        
        AABB r; r.mini = mini; r.maxi = maxi;
        
        return r;
    }

    static AABB make(AABB& box_s, AABB& box_e) {
        
        auto small = simd_make_float3(fminf(box_s.mini.x, box_e.mini.x),
                                      fminf(box_s.mini.y, box_e.mini.y),
                                      fminf(box_s.mini.z, box_e.mini.z));
        
        auto big = simd_make_float3(fmaxf(box_s.maxi.x, box_e.maxi.x),
                                    fmaxf(box_s.maxi.y, box_e.maxi.y),
                                    fmaxf(box_s.maxi.z, box_e.maxi.z));

        return AABB::make(small, big);
    }
    
#endif
    
};

#endif /* AABB_h */
