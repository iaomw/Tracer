#ifndef Noise_h
#define Noise_h

#include "Common.hh"

#ifdef __METAL_VERSION__

float noise( float3 x );

float fbm6( float2 p );

float fbm( float3 q );

#endif

#endif /* Noise_h */
