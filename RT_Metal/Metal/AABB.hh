#ifndef AABB_h
#define AABB_h

#include "Common.hh"
#include "HitRecord.hh"

struct AABB {
    float3 mini;
    float3 maxi;
    
    AABB() {
        mini = { FLT_MAX, FLT_MAX, FLT_MAX };
        maxi = { -FLT_MAX, -FLT_MAX, -FLT_MAX };
    }
    
    inline float3 diagonal() const {
        return maxi - mini;
    }
    
    float3 centroid() const {
        auto d = diagonal() / 2;
        return mini + d;
    }
    
    float surfaceArea() const {
        auto d = diagonal();
        return 2 * (d.x * d.y + d.x * d.z + d.y * d.z);
    }
    
    float volume() const {
        auto d = diagonal();
        return d.x * d.y * d.z;
    }
    
    float3 relative(float3 p) const {
        auto d = diagonal();
        return (p - mini) / d;
    }
    
    uint maximumExtent() const {
        
        auto d = diagonal();
        
        if (d.x > d.y && d.x > d.z) {
            return 0;
        }
        
        if (d.y > d.z)
            return 1;
        else
            return 2;
    }
    
#ifdef __METAL_VERSION__
    
    bool hit_edge(const thread Ray& ray, const thread float2& range_t, float ttt, float level) constant {
        
        auto center = (maxi + mini) / 2;
        auto half_diagonal = (maxi - mini) / 2;

        auto p = ray.pointAt(ttt);
        auto delta = abs(p - center);

        int cheker = 0;
        for (int i=0; i<3; i++) {
            if (abs(delta[i] - half_diagonal[i]) < 1 / (level + 1) ) {
                cheker+=1;
                if (cheker == 2) { return true; }
            }
        }
        
        return false;
    }
    
    // https://gamedev.stackexchange.com/questions/18436/most-efficient-aabb-vs-ray-collision-algorithms/
    bool hit(const thread Ray& ray, const thread float2& range_t) constant {
        
        auto inverse = 1.0 / ray.direction;

        auto ts = (mini - ray.origin) * inverse;
        auto te = (maxi - ray.origin) * inverse;
        
        auto a = min(ts, te);
        auto b = max(ts, te);
        
        float tmin = max3(a.x, a.y, a.z);
        float tmax = min3(b.x, b.y, b.z);

        tmin = max(tmin, range_t.x);
        tmax = min(tmax, range_t.y);

        return ! (tmax < tmin || tmax < 0);
    }
    
    bool hit_range(const thread Ray& ray, thread float2& range_t) constant {
        
        auto inverse = 1.0 / ray.direction;

        auto ts = (mini - ray.origin) * inverse;
        auto te = (maxi - ray.origin) * inverse;
        
        auto a = min(ts, te);
        auto b = max(ts, te);
        
        float tmin = max3(a.x, a.y, a.z);
        float tmax = min3(b.x, b.y, b.z);

        tmin = max(tmin, range_t.x);
        tmax = min(tmax, range_t.y);

        if (tmax <= tmin || tmax < 0) {return false;}
        
//        range_t[0] = tmin;
//        range_t[1] = tmax;
        
        range_t = float2(tmin, tmax);
        
        return true;
    }
    
    bool hit_get_t(const thread Ray& ray, const thread float2& range_t, thread float& t) constant {
        
        auto inverse = 1.0 / ray.direction;

        auto ts = (mini - ray.origin) * inverse;
        auto te = (maxi - ray.origin) * inverse;
        
        auto a = min(ts, te);
        auto b = max(ts, te);
        
        float tmin = max3(a.x, a.y, a.z);
        float tmax = min3(b.x, b.y, b.z);

        tmin = max(tmin, range_t.x);
        tmax = min(tmax, range_t.y);

        if (tmax < tmin || tmax < 0) { return false; }
        t = (tmin < 0)? tmax : tmin; // maybe internal
        
        return true;
    }
    
    bool hit_get_range(const thread Ray& ray, const thread float2& range_t, thread float& t, thread float2& range_new) constant {
        
        auto inverse = 1.0 / ray.direction;

        auto ts = (mini - ray.origin) * inverse;
        auto te = (maxi - ray.origin) * inverse;
        
        auto a = min(ts, te);
        auto b = max(ts, te);
        
        float tmin = max3(a.x, a.y, a.z);
        float tmax = min3(b.x, b.y, b.z);

        tmin = max(tmin, range_t.x);
        tmax = min(tmax, range_t.y);

        if (tmax < tmin || tmax < 0) { return false; }
        t = (tmin < 0)? tmax : tmin; // maybe internal
        
        if (tmin < 0) {
            range_new = float2(0.001, tmax);
        } else {
            range_new = float2(tmin, tmax);
        }
        
        return true;
    }
    
    bool hit(thread Ray& ray, const thread float2& range_t, thread HitRecord& record) constant {
        
        float tmin = -FLT_MAX;
        float tmax = range_t.y;
        
        uint axisPick = 0;
        
        float3 ddd = ray.origin - mini;
        float3 bbb = ray.origin - maxi;
        
        if (min3(ddd.x, ddd.y, ddd.z) > 0 && max3(bbb.x, bbb.y, bbb.z) < 0) { // internal hit
            
            for (auto i : {0, 1, 2}) {
                
                auto min_bound = (mini[i] - ray.origin[i])/ray.direction[i];
                auto max_bound = (maxi[i] - ray.origin[i])/ray.direction[i];
                
                auto ts = min(max_bound, min_bound);
                auto te = max(max_bound, min_bound);
                
                tmin = max(ts, tmin);
                //tmax = min(te, tmax);
                
                if (te <= tmax) {
                    tmax = te;
                    axisPick = i;
                }
                
                //if (tmax < tmin) { return false; }
                if (tmax <= tmin || tmax < 0) { return false; }
            }
            
            record.t = tmax;
            
            record.n = float3(0);
            record.n[axisPick] = ray.direction[axisPick] > 0 ? 1:-1;
            
            auto hitPoint = ray.pointAt(record.t);
            record.p = hitPoint;
            
            //auto ratio = (hitPoint - mini) / (maxi - mini);
            //ratio[axisPick] = 0;
            
            uint2 axisUV = (uint2(1, 2) + axisPick) % 3;
            record.uv = float2(hitPoint[axisUV.x], hitPoint[axisUV.y]);
            
            return true;
        }
        
        for (auto i : {0, 1, 2}) {
            
            auto min_bound = (mini[i] - ray.origin[i])/ray.direction[i];
            auto max_bound = (maxi[i] - ray.origin[i])/ray.direction[i];
            
            auto ts = min(max_bound, min_bound);
            auto te = max(max_bound, min_bound);
            
            //tmin = max(ts, tmin);
            tmax = min(te, tmax);
            
            if (ts >= tmin) {
                
                tmin = ts;
                axisPick = i;
            }
            
            //if (tmax < tmin) { return false; }
            if (tmax <= tmin || tmax < 0) { return false; }
        }
        
        record.t = tmin; // external
        
        record.n = float3(0);
        record.n[axisPick] = ray.direction[axisPick] > 0 ? -1:1;
        
        auto hitPoint = ray.pointAt(record.t);
        record.p = hitPoint;
        
        //auto ratio = (hitPoint - mini) / (maxi - mini);
        //ratio[axisPick] = 0;
        
        uint2 axisUV = (uint2(1, 2) + axisPick) % 3;
        record.uv = float2(hitPoint[axisUV.x], hitPoint[axisUV.y]);
        
        return true;
    }
    
#else
    
    static AABB make(const float3& a, const float3& b) {
        
        auto mini = float3{ fminf(a.x, b.x),
                            fminf(a.y, b.y),
                            fminf(a.z, b.z) };
        
        auto maxi = float3{ fmaxf(a.x, b.x),
                            fmaxf(a.y, b.y),
                            fmaxf(a.z, b.z) };
        
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
        
        AABB r; r.mini = small; r.maxi = big;

        return r; // AABB::make(small, big);
    }
    
    static AABB make(AABB& box_s, float3 fff) {
        
        auto small = simd_make_float3(fminf(box_s.mini.x, fff.x),
                                      fminf(box_s.mini.y, fff.y),
                                      fminf(box_s.mini.z, fff.z));
        
        auto big = simd_make_float3(fmaxf(box_s.maxi.x, fff.x),
                                    fmaxf(box_s.maxi.y, fff.y),
                                    fmaxf(box_s.maxi.z, fff.z));
        
        AABB r; r.mini = small; r.maxi = big;

        return r; //AABB::make(small, big);
    }
    
#endif
    
};

#endif /* AABB_h */
