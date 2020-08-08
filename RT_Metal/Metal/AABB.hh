#ifndef AABB_h
#define AABB_h

#include "Common.hh"
#include "HitRecord.hh"

enum class Axis { X=0, Y=1, Z=2 };
//const Axis AxisList[3]{Axis::X, Axis::Y, Axis::Z};

struct AABB {
    float3 mini;
    float3 maxi;
    
#ifdef __METAL_VERSION__
    
    // https://gamedev.stackexchange.com/questions/18436/most-efficient-aabb-vs-ray-collision-algorithms/
    bool hit(thread Ray& ray, const thread float2& range_t) constant {
        
        float tmin = range_t.x, tmax = range_t.y;
            
        for (auto i : {0, 1, 2}) {
            
            auto min_bound = (mini[i] - ray.origin[i])/ray.direction[i];
            auto max_bound = (maxi[i] - ray.origin[i])/ray.direction[i];
            
            auto ts = min(max_bound, min_bound);
            auto te = max(max_bound, min_bound);
            
            tmin = max(ts, tmin);
            tmax = min(te, tmax);
            
            if (tmax <= tmin) { return false; }
        }
        
        return true;
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
            //if (tmax <= tmin || tmax < 0 || tmin < 0) { return false; }
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
    
#endif
    
};

#endif /* AABB_h */
