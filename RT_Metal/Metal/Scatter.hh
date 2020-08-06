#ifndef Scatter_h
#define Scatter_h

#include "Ray.hh"
#include "Camera.hh"
#include "Texture.hh"
#include "Material.hh"
#include "AABB.hh"

#include "BVH.hh"
#include "Cube.hh"
#include "Square.hh"
#include "Sphere.hh"

#include "HitRecord.hh"
    
bool scatter(thread Ray& ray,
             thread HitRecord& hitRecord,
             thread ScatRecord& scatRecord,
             thread pcg32_random_t* seed);

#endif /* MetalObject_h */
