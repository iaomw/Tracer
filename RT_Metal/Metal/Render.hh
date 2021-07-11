#ifndef Render_h
#define Render_h

#include "Common.hh"
#include "SobolSampler.hh"
#include "RandomSampler.hh"

#include "Random.hh"


#include "Triangle.hh"
#include "BVH.hh"

#include "Sphere.hh"
#include "Square.hh"
#include "Cube.hh"

#include "Geo.hh"
#include "Light.hh"
#include "Spectrum.hh"

#include "Material.hh"

struct PackageEnv {
    texture2d<float>       texHDR [[id(0)]];
    texture2d<float>       texUVT [[id(1)]];
    
    constant Material*  materials [[id(2)]];
};

struct PackagePBR {
    texture2d<float>        texAO [[id(0)]];
    texture2d<float>    texAlbedo [[id(1)]];
    texture2d<float>    texNormal [[id(2)]];
    texture2d<float>  texMetallic [[id(3)]];
    texture2d<float> texRoughness [[id(4)]];
};

struct Primitive {
    constant Sphere*   sphereList [[id(0)]];
    constant Square*   squareList [[id(1)]];
    constant Cube*       cubeList [[id(2)]];
    
    constant TriangleVertex*  triList [[id(3)]];
    constant uint32_t*        idxList [[id(4)]];
    constant BVH*             bvhList [[id(5)]];
};

struct Scene {
    constant Primitive& primitives;
    
    bool hit(const thread Ray& ray, thread HitRecord& hitRecord, thread bool* edge) {
        
        uint the_index = 0;
        uint tested_index = UINT_MAX;
        
        uint32_t stack_mark = 0;
        uint32_t stack_level = 0;
        
        float2 range_t = float2(FLT_MIN, FLT_MAX);
        
        if ( primitives.bvhList[the_index].boundingBOX.hit(ray, range_t) ) {
            
            do { // travel in bvh
                
                uint selected_index = UINT_MAX;
                
                uint left_index = primitives.bvhList[the_index].left;
                uint right_index = primitives.bvhList[the_index].right;
                uint parent_index = primitives.bvhList[the_index].parent;
                
                if (tested_index != left_index && tested_index != right_index) {
                    
                    float t_left = FLT_MAX, t_right = FLT_MAX;
                    
                    bool left_test = primitives.bvhList[left_index].boundingBOX.hit_get_t(ray, range_t, t_left);
                    bool right_test = primitives.bvhList[right_index].boundingBOX.hit_get_t(ray, range_t, t_right);
                    
                    if (!left_test && !right_test) {
                        
                        tested_index = the_index;
                        the_index = parent_index;
                        stack_level -= 1; // pop stack
                        
                        continue;
                    }
                    
                    bool needTestAnother = (left_test) && (right_test);
                    if (needTestAnother) { stack_mark |= 1U << stack_level; }
                    
                    selected_index = (t_left < t_right)? left_index : right_index;
                    
                    if (nullptr != edge) {
                        
                        if (t_left < t_right) {
                            auto done = primitives.bvhList[left_index].boundingBOX.hit_edge(ray, range_t, t_left, stack_level);
                            if (done) {*edge = true; return false;}
                            
                        } else {
                            auto done = primitives.bvhList[right_index].boundingBOX.hit_edge(ray, range_t, t_right, stack_level);
                            if (done) { *edge = true; return false;}
                        }
                    }
                    
                } // came from parent
                
                else { // came from child
                    
                    uint needCheckChild = (stack_mark >> stack_level) & 1U;
                    // don't need check child in case of go back;
                    stack_mark &= ~(1U << stack_level);
                    
                    if (0 == needCheckChild) { // go up
                        
                        tested_index = the_index;
                        the_index = parent_index;
                        stack_level -= 1; // pop stack
                        
                        continue;
                    }
                    
                    if (tested_index == left_index) {
                        selected_index = right_index;
                    } else {
                        selected_index = left_index;
                    }
                }
                
                auto pIndex = primitives.bvhList[selected_index].pIndex;
                
                switch(primitives.bvhList[selected_index].pType) {
                        
                    case PrimitiveType::BVH: { // Should already tested before reaching this step
                         
                        the_index = selected_index;
                        stack_level += 1;
                        continue;
                    }
                    case PrimitiveType::Sphere: {
                        primitives.sphereList[pIndex].hit_test(ray, range_t, hitRecord); break;
                    }
                    case PrimitiveType::Square: {
                        primitives.squareList[pIndex].hit_test(ray, range_t, hitRecord); break;
                    }
                    case PrimitiveType::Cube: {
                        primitives.cubeList[pIndex].hit_test(ray, range_t, hitRecord); break;
                    }
                    case PrimitiveType::Triangle: {
                        
                        auto index_r = pIndex * 3;
                        auto index_a = primitives.idxList[index_r];
                        auto index_b = primitives.idxList[index_r + 1];
                        auto index_c = primitives.idxList[index_r + 2];
                        
                        uint3 abc {index_a, index_b, index_c};
                        auto tri = Triangle(primitives.triList, abc);
                        tri.hit_test(ray, range_t, hitRecord); break;
                    }
                    default: { return false; }
                } // switch
                
                tested_index = selected_index;
                
            } while (tested_index != 0);
        }
        
        return FLT_MAX != range_t.y;
    }
    
    bool block(const thread Ray& ray, thread HitRecord& hitRecord, float test_t) {
        
        uint the_index = 0;
        uint tested_index = UINT_MAX;
        
        uint32_t stack_mark = 0;
        uint32_t stack_level = 0;
        
        float2 range_t = float2(FLT_MIN, test_t);
        
        if ( primitives.bvhList[the_index].boundingBOX.hit(ray, range_t) ) {
            
            do { // travel in bvh
                
                uint selected_index = UINT_MAX;
                
                uint left_index = primitives.bvhList[the_index].left;
                uint right_index = primitives.bvhList[the_index].right;
                uint parent_index = primitives.bvhList[the_index].parent;
                
                if (tested_index != left_index && tested_index != right_index) {
                    
                    float t_left = test_t, t_right = test_t;
                    
                    bool left_test = primitives.bvhList[left_index].boundingBOX.hit_get_t(ray, range_t, t_left);
                    bool right_test = primitives.bvhList[right_index].boundingBOX.hit_get_t(ray, range_t, t_right);
                    
                    if (!left_test && !right_test) {
                        
                        tested_index = the_index;
                        the_index = parent_index;
                        stack_level -= 1; // pop stack
                        
                        continue;
                    }
                    
                    bool needTestAnother = (left_test) && (right_test);
                    if (needTestAnother) { stack_mark |= 1U << stack_level; }
                    
                    selected_index = (t_left < t_right)? left_index : right_index;
                    
                } // came from parent
                
                else { // came from child
                    
                    uint needCheckChild = (stack_mark >> stack_level) & 1U;
                    // don't need check child in case of go back;
                    stack_mark &= ~(1U << stack_level);
                    
                    if (0 == needCheckChild) { // go up
                        
                        tested_index = the_index;
                        the_index = parent_index;
                        stack_level -= 1; // pop stack
                        
                        continue;
                    }
                    
                    if (tested_index == left_index) {
                        selected_index = right_index;
                    } else {
                        selected_index = left_index;
                    }
                }
                
                auto pIndex = primitives.bvhList[selected_index].pIndex;
                
                auto hitted = false;
                
                switch(primitives.bvhList[selected_index].pType) {
                        
                    case PrimitiveType::BVH: { // Should already tested before reaching this step
                         
                        the_index = selected_index;
                        stack_level += 1;
                        continue;
                    }
                    case PrimitiveType::Sphere: {
                        hitted = primitives.sphereList[pIndex].hit_test(ray, range_t, hitRecord); break;
                    }
                    case PrimitiveType::Square: {
                        hitted = primitives.squareList[pIndex].hit_test(ray, range_t, hitRecord); break;
                    }
                    case PrimitiveType::Cube: {
                        hitted = primitives.cubeList[pIndex].hit_test(ray, range_t, hitRecord); break;
                    }
                    case PrimitiveType::Triangle: {
                        
                        auto index_r = pIndex * 3;
                        auto index_a = primitives.idxList[index_r];
                        auto index_b = primitives.idxList[index_r + 1];
                        auto index_c = primitives.idxList[index_r + 2];
                        
                        uint3 abc {index_a, index_b, index_c};
                        auto tri = Triangle(primitives.triList, abc);
                        
                        hitted = tri.hit_test(ray, range_t, hitRecord); break;
                    }
                    default: { return false; }
                } // switch
                
                if (hitted && hitRecord.t < test_t) { return true; }
                
                tested_index = selected_index;
                
            } while (tested_index != 0);
        }
        
        return range_t.y < test_t;
    }
};

#endif /* Render_h */
