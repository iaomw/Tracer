#ifndef Render_h
#define Render_h

#include "SobolSampler.hh"
#include "RandomSampler.hh"

struct PackPBR {
    texture2d<float>        texAO [[id(0)]];
    texture2d<float>    texAlbedo [[id(1)]];
    texture2d<float>    texNormal [[id(2)]];
    texture2d<float>  texMetallic [[id(3)]];
    texture2d<float> texRoughness [[id(4)]];
    
    texture2d<float>         texUV[[id(5)]];
};

#endif /* Render_h */
