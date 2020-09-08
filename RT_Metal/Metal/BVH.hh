#ifndef BVH_h
#define BVH_h

#include "AABB.hh"
#include "HitRecord.hh"

enum struct ShapeType {
    Sphere, Square, Cube, Triangle, BVH, UNKNOW
};

struct BucketInfo {
    uint count = 0;
    AABB bbox;
};

struct BVH {
    uint parent=0, left=0, right=0; // index in BVH array
    uint shapeIndex = 0;
    
    ShapeType shape = ShapeType::UNKNOW;
    
    AABB boundingBOX;
    
#ifdef __METAL_VERSION__
#else
    
    static inline bool box_compare(const AABB& a, const AABB& b, int axis) {

        return a.centroid()[axis] < b.centroid()[axis];
    }
    
    static uint make(std::vector<BVH>&  bvh_list,
                     std::vector<uint>& index_list,
                     
                     uint start, uint end, uint depth, dispatch_queue_t squeue)
    {
        auto comparator = [&](const uint a, const uint b, uint axis) -> bool {
            
            auto& node_a = bvh_list[a];
            auto& node_b = bvh_list[b];
            
            return box_compare(node_a.boundingBOX, node_b.boundingBOX, axis);
        };
        
        __block uint left, right;
        uint span = end - start;
        
        if (1 == span) {
            
            left = right = 0;
            return index_list[start];
        }
        
        if (2 == span) {
            
            let index_a = index_list[start];
            let index_b = index_list[start+1];
            
            let centroid_a = bvh_list[index_a].boundingBOX.centroid();
            let centroid_b = bvh_list[index_b].boundingBOX.centroid();
            
            let cbox = AABB::make(centroid_a, centroid_b);
            auto dim = cbox.maximumExtent();
            
            if (comparator(index_a, index_a, dim)) {
                left = index_a;
                right = index_b;
            } else {
                left = index_b;
                right = index_a;
            }
            
        } else {
            
            AABB cbox;
            
            for (int i=start; i<end; i++) {
                 
                auto i_centroid = bvh_list[index_list[i]].boundingBOX.centroid();
                cbox = AABB::make(cbox, i_centroid);
            }
            
            auto dim = cbox.maximumExtent();
            
            const uint nBuckets = 10;
            BucketInfo buckets[nBuckets];
            
            //let queue = dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_HIGH, 0);
            
            auto prepareBucket = [&](const size_t i) -> void {
                
                auto& primi = bvh_list[index_list[i]];
                auto centroid = primi.boundingBOX.centroid();
                
                uint b = nBuckets * cbox.relative(centroid)[dim];
                b = std::min(b, nBuckets-1);
                
                buckets[b].bbox = AABB::make(buckets[b].bbox, primi.boundingBOX);
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
                
                cost[i] = 1 + (count0 * b0.surfaceArea() + count1 * b1.surfaceArea()) / cbox.surfaceArea();
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
            
            //float leafCost = span;
            //uint maxPrimiUnderNode = 255;
            //if (span > maxPrimiUnderNode || minCost < leafCost) {}
            
            __block uint mid;
            //dispatch_sync(squeue, ^{
                auto pmid = std::partition(
                                index_list.begin()+start,
                                index_list.begin()+end,
                                           
                       [&](const uint index) {
                    
                    auto& primi = bvh_list[index];
                    auto centroid = primi.boundingBOX.centroid();
                
                    uint b = nBuckets * cbox.relative(centroid)[dim];
                    b = std::min(b, nBuckets - 1);
                    
                    return b <= minCostSplitBucket;
                       
                });
                
                mid = (uint)(pmid - index_list.begin());
            //});
                
            if (mid <= start || mid >= end) {
                
                auto comp = [&](const uint a, const uint b) -> bool {
                    return comparator(a, b, dim);
                };
                
                std::sort(index_list.begin()+start, index_list.begin()+end, comp);
                mid = start + span / 2;
            }
            
            left = BVH::make(bvh_list, index_list, start, mid, depth+1, squeue);
            right = BVH::make(bvh_list, index_list, mid, end, depth+1, squeue);
        }
        
        dispatch_sync(squeue, ^{
            
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
        });
        
        return (uint32_t)bvh_list.size() - 1;
    }
    
    static void buildTree(std::vector<BVH>& bvh_list)
    {
        
        auto old_size = (uint32_t)bvh_list.size();
        
        std::vector<uint> index_list;
        
        for (int i=0; i<old_size; i++) {
            index_list.push_back(i);
        }
        
        dispatch_queue_t squeue = dispatch_queue_create("com.unique", DISPATCH_QUEUE_SERIAL);
        
        BVH::make(bvh_list, index_list, 0, (uint32_t)index_list.size(), 0, squeue);
                       
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
    
    static inline void buildNode(const AABB& box, const float4x4& model_matrix,
                                 ShapeType shapeType, uint shapeIndex,
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
