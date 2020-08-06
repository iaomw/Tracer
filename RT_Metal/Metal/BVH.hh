#ifndef BVH_h
#define BVH_h

#include "AABB.hh"
#include "HitRecord.hh"

struct BVH {
    uint left, right; // index in BVH array
    AABB boundingBOX;
    
#ifdef __METAL_VERSION__

    bool hit(constant BVH* listBVH, thread Ray& r, float2 range_t, thread HitRecord& rec) constant {
        
        if (!boundingBOX.hit(r, range_t))
            return false;
        
        auto leftNode = &listBVH[left];
        auto rightNode = &listBVH[right];

        bool hit_left = leftNode->hit(listBVH, r, range_t, rec);

        if (hit_left) { range_t.y = rec.t; }

        bool hit_right = rightNode->hit(listBVH, r, range_t, rec);

        return hit_left || hit_right;
        
        return true;
    }

    static inline bool box_compare(constant AABB& a, constant AABB& b, int axis) {

        return a.mini[axis] < b.mini[axis];
    }
    
#endif
    
};

//BVH MakeBVH() {
//    return BVH();
//}

#endif /* BVH_h */
