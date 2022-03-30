#ifndef Render_h
#define Render_h

#include "Common.hh"
#include "Random.hh"
#include "Camera.hh"

//#include "SobolSampler.hh"
#include "RandomSampler.hh"

#include "BVH.hh"
#include "Triangle.hh"

#include "Cube.hh"
#include "Square.hh"
#include "Sphere.hh"

#include "Light.hh"
#include "Spectrum.hh"

#include "Medium.hh"
#include "Material.hh"

struct PackageEnv {
    texture2d<float>       texHDR [[id(0)]];
    texture2d<float>       texUVT [[id(1)]];
    
    constant Material*  materials [[id(2)]];
    
    constant GridDensityInfo*   densityInfo [[id(3)]];
    constant float*            densityArray [[id(4)]];
};

struct PackagePBR {
    texture2d<float>        texAO [[id(0)]];
    texture2d<float>    texAlbedo [[id(1)]];
    texture2d<float>    texNormal [[id(2)]];
    texture2d<float>  texMetallic [[id(3)]];
    texture2d<float> texRoughness [[id(4)]];
};

inline float2 SampleSphericalMap(float3 v)
{
    float2 uv = float2(atan2(v.z, v.x), asin(v.y));
    float2 invAtan = float2(0.1591, 0.3183);
    uv *= invAtan; uv += 0.5;
    return uv;
}

inline float3 LessThan(float3 f, float value)
{
    return float3(
        (f.x < value) ? 1.0f : 0.0f,
        (f.y < value) ? 1.0f : 0.0f,
        (f.z < value) ? 1.0f : 0.0f);
}
 
inline float3 LinearToSRGB(float3 rgb)
{
    rgb = clamp(rgb, 0.0f, 1.0f);
    return mix(
        pow(rgb, float3(1.0f / 2.4f)) * 1.055f - 0.055f,
        rgb * 12.92f,
        LessThan(rgb, 0.0031308f)
    );
}
 
inline float3 SRGBToLinear(float3 rgb)
{
    rgb = clamp(rgb, 0.0f, 1.0f);
    return mix(
        pow(((rgb + 0.055f) / 1.055f), float3(2.4f)),
        rgb / 12.92f,
        LessThan(rgb, 0.04045f)
    );
}

inline float3 ACESTone(float3 color, float adapted_lum)
{
    const float A = 2.51f;
    const float B = 0.03f;
    const float C = 2.43f;
    const float D = 0.59f;
    const float E = 0.14f;

    color *= adapted_lum;
    return (color * (A * color + B)) / (color * (C * color + D) + E);
}

inline float3 CETone(float3 color, float adapted_lum)
{
    return 1 - exp(-adapted_lum * color);
}

inline float CETone(float color, float adapted_lum)
{
    return 1 - exp(-adapted_lum * color);
}

inline pcg32_t toRNG(thread vec<uint32_t, 4> &rng_cache) {
    //return as_type<pcg32_t>(rng_cache);
    
    uint32_t rr = rng_cache.r;
    uint32_t gg = rng_cache.g;
    uint32_t bb = rng_cache.b;
    uint32_t aa = rng_cache.a;
    
    uint64_t rng_state = (uint64_t(rr) << 32) | gg;
    uint64_t rng_inc = (uint64_t(bb) << 32) | aa;
    return pcg32_t { rng_inc, rng_state };
}

inline vec<uint32_t, 4> exRNG(thread pcg32_t &rng) {
    //return as_type<vec<uint32_t, 4>>(rng);
    
    vec<uint32_t, 4> rng_cache;
    
    rng_cache.r = rng.state >> 32;
    rng_cache.g = rng.state;
    rng_cache.b = rng.inc >> 32;
    rng_cache.a = rng.inc;
    
    return rng_cache;
}

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
    
    bool hit(const thread Ray& ray, thread HitRecord& hitRecord, const float test_t, bool any = false, thread bool* edge = nullptr) {
        
        uint the_index = 0;
        uint tested_index = UINT_MAX;
        
        uint32_t stack_mark = 0;
        uint32_t stack_level = 0;
        
        float2 range_t = float2(FLT_MIN, test_t);
        
        if ( primitives.bvhList[the_index].bBOX.hit(ray, range_t) ) {
            
            do { // travel in bvh
                
                uint selected_index = UINT_MAX;
                
                uint left_index = primitives.bvhList[the_index].left;
                uint right_index = primitives.bvhList[the_index].right;
                uint parent_index = primitives.bvhList[the_index].parent;
                
                if (tested_index != left_index && tested_index != right_index) {
                    
                    float t_left = range_t.y, t_right = range_t.y;
                    
                    bool left_test = primitives.bvhList[left_index].bBOX.hit_t(ray, range_t, t_left);
                    bool right_test = primitives.bvhList[right_index].bBOX.hit_t(ray, range_t, t_right);
                    
                    if (!left_test && !right_test) {
                        
                        tested_index = the_index;
                        the_index = parent_index;
                        stack_level -= 1; // pop stack
                        
                        continue;
                    }
                    
                    bool needTestAnother = (left_test) && (right_test);
                    if (needTestAnother) { stack_mark |= 1U << stack_level; }
                    
                    selected_index = (t_left < t_right)? left_index : right_index;
                    
//                    if (nullptr != edge) {
//
//                        if (t_left < t_right) {
//                            auto done = primitives.bvhList[left_index].bBOX.hit_edge(ray, range_t, t_left, stack_level);
//                            if (done) {*edge = true; return false;}
//
//                        } else {
//                            auto done = primitives.bvhList[right_index].bBOX.hit_edge(ray, range_t, t_right, stack_level);
//                            if (done) { *edge = true; return false;}
//                        }
//                    }
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
                    default: { break; }
                } // switch
                
                if (any && range_t.y < test_t) { return true; }
                
                tested_index = selected_index;
                
            } while (tested_index != 0);
        }
        
        return range_t.y < test_t;
    }
    
};

#endif /* Render_h */
