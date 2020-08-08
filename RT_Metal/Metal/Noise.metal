#include <metal_stdlib>
using namespace metal;

float hash( float n )
{
    return fract(sin(n) * 43758.5453123);
}

float2 hash22(float2 p)
{
    float3 p3 = fract(float3(p.xyx) * float3(.1031, .1030, .0973));
    p3 += dot(p3, p3.yzx+33.33);
    return fract((p3.xx+p3.yz)*p3.zy);
}

float noise( float3 x )
{
    // The noise function returns a value in the range -1.0f -> 1.0f

    float3 p = floor(x);
    float3 f = fract(x);

    f       = f*f*(3.0-2.0*f);
    float n = p.x + p.y*57.0 + 113.0*p.z;

    return mix(mix(mix( hash(n+0.0), hash(n+1.0),f.x),
                   mix( hash(n+57.0), hash(n+58.0),f.x),f.y),
               mix(mix( hash(n+113.0), hash(n+114.0),f.x),
                   mix( hash(n+170.0), hash(n+171.0),f.x),f.y),f.z);
}

float perlinNoise(float2 p) {
    
    float2 pi = floor(p);
    float2 pf = fract(p);

    float2 w = pf * pf * (3.0 - 2.0 * pf);

    return mix(mix(dot(hash22(pi + float2(0.0, 0.0)), pf - float2(0.0, 0.0)),
                   dot(hash22(pi + float2(1.0, 0.0)), pf - float2(1.0, 0.0)), w.x),
               mix(dot(hash22(pi + float2(0.0, 1.0)), pf - float2(0.0, 1.0)),
                   dot(hash22(pi + float2(1.0, 1.0)), pf - float2(1.0, 1.0)), w.x),
               w.y);
}

constant float2x2 mtx = float2x2( 0.80,  0.60, -0.60,  0.80 );

float fbm6( float2 p ) {
  float f = 0.0;

  f += 0.500000*perlinNoise( p ); p = mtx*p*2.02;
  f += 0.250000*perlinNoise( p ); p = mtx*p*2.03;
  f += 0.125000*perlinNoise( p ); p = mtx*p*2.01;
  f += 0.062500*perlinNoise( p ); p = mtx*p*2.04;
  f += 0.031250*perlinNoise( p ); p = mtx*p*2.01;
  f += 0.015625*perlinNoise( p );

  return f/0.96875;
}

constant float3x3 m3 = float3x3( 0.00,  0.80,  0.60,
                                -0.80,  0.36, -0.48,
                                -0.60, -0.48,  0.64 );
float fbm(float3 q)
{
    float f = 0.0;
    
    f += 0.5000*noise( q ); q = m3*q*2.01;
    f += 0.2500*noise( q ); q = m3*q*2.02;
    f += 0.1250*noise( q ); q = m3*q*2.03;
    f += 0.0625*noise( q ); q = m3*q*2.04;

    f += 0.03125*noise( q ); q = m3*q*2.05;
    f += 0.015625*noise( q ); q = m3*q*2.06;
    f += 0.0078125*noise( q ); q = m3*q*2.07;
    f += 0.00390625*noise( q ); //q = m3*q*2.08;
    
    return f;
}
