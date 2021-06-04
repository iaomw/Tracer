#ifndef Render_h
#define Render_h

#include "Common.hh"
#include "SobolSampler.hh"
#include "RandomSampler.hh"

#include "Triangle.hh"
#include "BVH.hh"

#include "Sphere.hh"
#include "Square.hh"
#include "Cube.hh"

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
    
    constant Triangle*    triList [[id(3)]];
    constant uint32_t*    idxList [[id(4)]];
    
    constant BVH*         bvhList [[id(5)]];
};

#endif /* Render_h */
