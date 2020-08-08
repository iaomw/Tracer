#ifndef BVH_h
#define BVH_h

#include "AABB.hh"
#include "HitRecord.hh"

struct BVH {
    uint left, right; // index in BVH array
    
    uint shapeID;
    uint shapeIndex;
    
    AABB boundingBOX;
    
#ifdef __METAL_VERSION__

    bool hit(constant BVH* listBVH, thread Ray& ray, float2 range_t, thread HitRecord& rec) constant {
        
        range_t.x = -FLT_MAX;
        range_t.y = +FLT_MAX;
        
        if (!boundingBOX.hit(ray, range_t))
            return false;
        
        bool hit_left = false;
        bool hit_right = false;
        
        uint index_pick = 0;
        uint index_left = left;
        uint index_right = right;
        
        do {
            auto pickNode = &listBVH[index_pick];
            
            index_left = pickNode->left;
            index_right = pickNode->left;
            
            if (index_left != 0) {
                
                auto leftNode = &listBVH[index_left];
                hit_left = leftNode->boundingBOX.hit(ray, range_t, rec);
            }
            
            if (hit_left) {
                range_t.y = rec.t;
                index_pick = index_left;
            }
            
            if (index_right != 0) {
                auto rightNode = &listBVH[index_right];
                hit_right = rightNode->boundingBOX.hit(ray, range_t, rec);
            }
            
            if (hit_right) {
                range_t.y = rec.t;
                index_pick = index_right;
            }
            
        } while(index_pick != 0);
        
        return hit_left || hit_right;
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
