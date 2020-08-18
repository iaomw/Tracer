#ifndef BVH_h
#define BVH_h

#include "AABB.hh"
#include "HitRecord.hh"

#ifdef __METAL_VERSION__

#else

inline double random_double() {
    // Returns a random real in [0,1).
    return rand() / (RAND_MAX + 1.0);
}

inline double random_double(double min, double max) {
    // Returns a random real in [min,max).
    return min + (max-min)*random_double();
}

inline int random_int(int min, int max) {
    // Returns a random integer in [min,max].
    return static_cast<int>(random_double(min, max+1));
}

#endif

enum struct BVHStatus {
    TracingLeft, TracingRight
};

enum struct ShapeType {
    Sphere, Square, Cube, Triangle, BVH, UNKNOW
};

struct BVH {
    uint parent=0, left=0, right=0; // index in BVH array
    
    ShapeType shape = ShapeType::UNKNOW;
    uint shapeIndex = 0;
    
    AABB boundingBOX;
    
#ifdef __METAL_VERSION__

    bool hit(constant BVH* listBVH, thread Ray& ray, float2 range_t, thread HitRecord& rec) constant {
        
        range_t.x = -FLT_MAX;
        range_t.y = +FLT_MAX;
        
        if (!boundingBOX.hit(ray, range_t))
            return false;
        
        bool hit_left;
        bool hit_right;
        
        uint index_pick = 0;
        uint index_left = left;
        uint index_right = right;
        
        do {
            auto pickNode = &listBVH[index_pick];
            
            index_pick = 0;
            index_left = pickNode->left;
            index_right = pickNode->left;
            
            hit_left = false;
            hit_right = false;
            
            if (index_left != 0) {
                auto leftNode = &listBVH[index_left];
                hit_left = leftNode->boundingBOX.hit(ray, range_t, rec);
                
                if (hit_left) {
                    range_t.y = rec.t;
                    index_pick = index_left;
                }
            }
            
            if (index_right != 0) {
                auto rightNode = &listBVH[index_right];
                hit_right = rightNode->boundingBOX.hit(ray, range_t, rec);
                
                if (hit_right) {
                    range_t.y = rec.t;
                    index_pick = index_right;
                }
            }
            
        } while(index_pick != 0);
        
        if (hit_left || hit_right ) {
            // hit test on shape index_pick
        }
        
        return false;
    }
    
#else
    
    static bool box_compare(const AABB& a, const AABB& b, int axis) {

        return a.mini[axis] < b.mini[axis];
    }
    
    static void work(std::vector<BVH>& bvh_list) {
        
        auto old_size = (uint32_t)bvh_list.size();
        
        std::vector<uint> index_list;
        
        for (int i=0; i<old_size; i++) {
            index_list.push_back(i);
        }
        
        BVH::make(bvh_list, index_list, 0, (uint32_t)index_list.size());
        
        auto root = bvh_list.back();
        bvh_list.pop_back();
        root.parent = 0;
        
        bvh_list.insert(bvh_list.begin(), root);
        
        for (int i=old_size; i<bvh_list.size(); i++) {
            if( bvh_list[i].parent == bvh_list.size()) {
                bvh_list[i].parent = 0;
            }
        }
    }
    
    static uint make(std::vector<BVH>& bvh_list,
                     std::vector<uint>& index_list,
                     
                     uint start, uint end) {
        
        uint axis = random_int(0, 2);
        
        auto comparator = [&](const uint a, const uint b) -> bool {
            
            auto& node_a = bvh_list[a];
            auto& node_b = bvh_list[b];
            
            return box_compare(node_a.boundingBOX, node_b.boundingBOX, axis);
        };
        
        uint left; uint right;
        uint span = end - start;
        
        if (1 == span) {
            
            left = right = 0;
            return index_list[start];
        }
        
        if (2 == span) {
            
            let index_a = index_list[start];
            let index_b = index_list[start+1];
            
            if (comparator(index_a, index_a)) {
                left = index_a;
                right = index_b;
            } else {
                left = index_b;
                right = index_a;
            }
            
        } else {
            
            std::sort(index_list.begin()+start, index_list.begin()+end, comparator);
            
            auto mid = start + span / 2;
            left = BVH::make(bvh_list, index_list, start, mid);
            right = BVH::make(bvh_list, index_list, mid, end);
        }
        
        BVH newBVH;
        
        newBVH.left = left + 1;
        newBVH.right = right + 1;
        
        bvh_list[left].parent = (uint32_t)bvh_list.size()+1;
        bvh_list[right].parent = (uint32_t)bvh_list.size()+1;
        
        newBVH.shape = ShapeType::BVH;
        
        auto& leftBOX = bvh_list[left].boundingBOX;
        auto& rightBOX = bvh_list[right].boundingBOX;
        
        newBVH.boundingBOX = AABB::make(leftBOX, rightBOX);
        
        bvh_list.emplace_back(newBVH);
        
        return (uint32_t)bvh_list.size() - 1;
    }
    
    static inline void build(const AABB& box, const float4x4& model_matrix,
                             ShapeType shapeType, uint shapeIndex,
                             std::vector<BVH>& bvh_list)
    {
        
        float3 ele[] { box.mini, box.maxi };
        
        float3 newMINI = float3(FLT_MAX);
        float3 newMAXI = float3(-FLT_MAX);
        
        for (int i = 0; i < 2; i++) {
            for (int j = 0; j < 2; j++) {
                for (int k = 0; k < 2; k++) {
                    
                    auto x = ele[i][0];
                    auto y = ele[j][1];
                    auto z = ele[k][2];
                    
                    auto pick_point = simd_make_float4(x, y, z, 1);
                    
                    auto tester = simd_mul(model_matrix, pick_point);

                    for (int c = 0; c < 3; c++) {
                        newMINI[c] = fmin(newMINI[c], tester[c]);
                        newMAXI[c] = fmax(newMAXI[c], tester[c]);
                    }
                } // k
            } // j
        } // i
        
        BVH newBVH;
        newBVH.left = 0;
        newBVH.right = 0;
        
        newBVH.shape = shapeType;
        newBVH.shapeIndex = shapeIndex;
        
        AABB newBOX;
        
        newBOX.mini = newMINI;
        newBOX.maxi = newMAXI;
        
        newBVH.boundingBOX = newBOX;
        
        bvh_list.emplace_back(newBVH);
    }
    
#endif
    
};

#endif /* BVH_h */
