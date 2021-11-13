#ifndef BVH_h
#define BVH_h

#include "AABB.hh"

#ifdef __cplusplus
    //#include <iostream>
#endif

enum struct PrimitiveType {
    Sphere, Square, Cube, Triangle, BVH, UNKNOW
};

struct BucketInfo {
    uint count = 0;
    AABB bbox;
};

struct BVH {
    uint parent=0, left=0, right=0; // index in BVH array
    uint axis = 0;
    
    PrimitiveType pType = PrimitiveType::UNKNOW;
    uint pIndex = 0;
    
    AABB bBOX;
    
#ifdef __METAL_VERSION__
#else
    
#include <iostream>
    
    static inline bool box_compare(const AABB& a, const AABB& b, int axis) {

        return a.centroid()[axis] < b.centroid()[axis];
    }
    
    static uint make(std::vector<BVH>&  bvh_list,
                     std::vector<uint>& index_list,
                     
                     uint start, uint end, uint depth,
                     
                     dispatch_queue_t cqueue, dispatch_semaphore_t semaphore)
    {
        auto comparator = [&](const uint a, const uint b, uint axis) -> bool {
            
            auto& node_a = bvh_list[a];
            auto& node_b = bvh_list[b];
            
            return box_compare(node_a.bBOX, node_b.bBOX, axis);
        };
        
        uint span = end - start;
        
        if (1 == span) {
            return index_list[start];
        }
        
        uint dim=0;
        
        __block uint left, right;
        
        if (2 == span) {
            
            let index_a = index_list[start];
            let index_b = index_list[start+1];
            
            let centroid_a = bvh_list[index_a].bBOX.centroid();
            let centroid_b = bvh_list[index_b].bBOX.centroid();
            
            let cbox = AABB::make(centroid_a, centroid_b);
            dim = cbox.maximumExtent();
            
            if (comparator(index_a, index_b, dim)) {
                left = index_a;
                right = index_b;
            } else {
                left = index_b;
                right = index_a;
            }
            
        } else {
            
            AABB cbox;
            
            for (int i=start; i<end; i++) {
                 
                auto& bBOX = bvh_list[index_list[i]].bBOX;
                cbox = AABB::make(cbox, bBOX.centroid());
            }
            
            dim = cbox.maximumExtent();
            
            const uint nBuckets = 10;
            BucketInfo buckets[nBuckets];
            
            auto prepareBucket = [&](const size_t i) -> void {
                
                auto& primi = bvh_list[index_list[i]];
                auto centroid = primi.bBOX.centroid();
                
                uint b = nBuckets * cbox.relative(centroid)[dim];
                b = std::min(b, nBuckets-1);
                
                buckets[b].bbox = AABB::make(buckets[b].bbox, primi.bBOX);
                buckets[b].count++;
            };
            
            for (int i=start; i<end; ++i) {
                prepareBucket(i);
            }
            
            float cost[nBuckets-1];
            
            auto prepareCost = [&](const size_t i) -> void {
                
                AABB b0, b1; int count0=0, count1=0;

                for (size_t j=0; j<=i; ++j) {
                    b0 = AABB::make(b0, buckets[j].bbox);
                    count0 += buckets[j].count;
                }
                for (size_t j=i+1; j<nBuckets; ++j) {
                    b1 = AABB::make(b1, buckets[j].bbox);
                    count1 += buckets[j].count;
                }
                
                cost[i] = 1 + (count0 * b0.area() + count1 * b1.area()) / cbox.area();
            };
            
            for (int i=0; i<(nBuckets-1); ++i) {
                prepareCost(i);
            }
            
            float minCost = cost[0];
            int minCostSplitBucket = 0;
            for (int i=1; i<(nBuckets-1); ++i) {
                if (cost[i] < minCost) {
                    minCost = cost[i];
                    minCostSplitBucket = i;
                }
            }
            
            auto tester = [&](const uint idx) {
                
                auto& primi = bvh_list[index_list[idx]];
                auto centroid = primi.bBOX.centroid();
            
                uint b = nBuckets * cbox.relative(centroid)[dim];
                b = std::min(b, nBuckets - 1);
                
                return b <= minCostSplitBucket;
            };
            
            uint mid = ^{
                
                auto first = start; auto last = end;

                while (first!=last) {
                    while ( tester(first) ) { ++first;
                      if (first==last) return first;
                    }
                    do { --last;
                      if (first==last) return first;
                    } while ( !tester(last) );
                        
                    std::swap(index_list[first], index_list[last]);
                    ++first;
                }
                return first;
            } ();
            
//            auto pmid = std::partition(
//                            index_list.begin()+start,
//                            index_list.begin()+end,
//                    [&](const uint index) {
//
//                auto& primi = bvh_list[index];
//                auto centroid = primi.bBOX.centroid();
//
//                uint b = nBuckets * cbox.relative(centroid)[dim];
//                b = std::min(b, nBuckets - 1);
//
//                return b <= minCostSplitBucket;
//            });
//
//            auto mid = (uint)(pmid - index_list.begin());
            //mid = std::distance(index_list.begin(), pmid);
                
            if (mid <= start || mid >= end) {
                
                auto comp = [&](const uint a, const uint b) -> bool {
                    return comparator(a, b, dim);
                };
                
                std::sort(index_list.begin()+start, index_list.begin()+end, comp);
                mid = start + span / 2;
            }
            
            if (depth > 2) {
                
                left = BVH::make(bvh_list, index_list, start, mid, depth+1, cqueue, semaphore);
                right = BVH::make(bvh_list, index_list, mid, end, depth+1, cqueue, semaphore);
                
            } else {
                
                dispatch_group_t group = dispatch_group_create();
                
                dispatch_group_enter(group);
                dispatch_async(cqueue, ^{
                    left = BVH::make(bvh_list, index_list, start, mid, depth+1, cqueue, semaphore);
                    dispatch_group_leave(group);
                });
                
                dispatch_group_enter(group);
                dispatch_async(cqueue, ^{
                    right = BVH::make(bvh_list, index_list, mid, end, depth+1, cqueue, semaphore);
                    dispatch_group_leave(group);
                });
                
                dispatch_group_wait(group, DISPATCH_TIME_FOREVER);
            } // else
        }
        
        BVH newBVH;
        newBVH.axis = dim;
        
        newBVH.left = left + 1;
        newBVH.right = right + 1;
        
        dispatch_semaphore_wait(semaphore, DISPATCH_TIME_FOREVER);
        
        auto parent = (uint32_t)bvh_list.size()+1;
        
        bvh_list[left].parent = parent;
        bvh_list[right].parent = parent;
        
        newBVH.pType = PrimitiveType::BVH;
        
        auto& leftBOX = bvh_list[left].bBOX;
        auto& rightBOX = bvh_list[right].bBOX;
        
        newBVH.bBOX = AABB::make(leftBOX, rightBOX);
        
        bvh_list.emplace_back(newBVH);
        
        dispatch_semaphore_signal(semaphore);
        
        return parent - 1;
        //return (uint32_t)bvh_list.size() - 1;
    }
    
    static void buildTree(std::vector<BVH>& bvh_list)
    {
        std::vector<uint> index_list;
        
        for (uint i=0; i<bvh_list.size(); i++) {
            index_list.push_back(i);
        }
        
        auto semaphore = dispatch_semaphore_create(1);
        auto cqueue = dispatch_queue_create("com.unique", DISPATCH_QUEUE_CONCURRENT);
        
        BVH::make(bvh_list, index_list, 0, (uint32_t)index_list.size(), 0, cqueue, semaphore);
        //BVH::make(bvh_copy, index_copy, 0, (uint32_t)index_copy.size(), 999, cqueue, semaphore);
        
        auto root = bvh_list.back();
        root.parent = 0; bvh_list.pop_back();
        bvh_list.insert(bvh_list.begin(), root);
        
        bvh_list[root.left].parent = 0;
        bvh_list[root.right].parent = 0;
    }
    
    static inline void buildNode(const AABB& box, const float4x4& model_matrix,
                                 PrimitiveType pType, uint pIndex,
                                 std::vector<BVH>& bvh_list)
    {
        
        packed_float3 ele[] { box.mini, box.maxi };
        
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
        
        newBVH.pType = pType;
        newBVH.pIndex = pIndex;
        
        AABB newBOX;
        
        newBOX.mini = newMINI;
        newBOX.maxi = newMAXI;
        
        newBVH.bBOX = newBOX;
        
        bvh_list.emplace_back(newBVH);
    }
    
#endif
    
};

#endif /* BVH_h */
