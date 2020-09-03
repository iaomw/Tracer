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
             thread pcg32_t* seed,
             thread HitRecord& hitRecord,
             thread ScatRecord& scatRecord,
             
             constant Material* materials,
             
             thread texture2d<float, access::sample> &texAO,
             thread texture2d<float, access::sample> &texAlbedo,
             thread texture2d<float, access::sample> &texMetallic,
             thread texture2d<float, access::sample> &texNormal,
             thread texture2d<float, access::sample> &texRoughness );

#endif /* MetalObject_h */
