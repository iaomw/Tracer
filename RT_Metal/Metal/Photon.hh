#pragma once

#ifndef Photon_h
#define Photon_h

#include "Common.hh"

#ifdef __METAL_VERSION__
#include "Render.hh"
#endif

struct PhotonRecord {
    
    float3 flux = 1;
    
    float3 normal;
    float3 position;
    float3 direction;
    
    uint8_t step = 0;
    bool active = false;
    
    void reset() {
        flux = 1;
        step = 0;
        active = false;
    }
};

struct CameraRecord {
    float3 ratio = 1;
    float3 position = 0;
    float3 direction = 0;
    
    bool valid = false;
    float3 alternative = 0;

    float3 flux = 0;
    float radius = 0;
    uint photonCount = 0;
    
    void reset() {
        ratio = 1;
        position = 0;
        direction = 0;
        
        valid = false;

        flux = 0;
        radius = 0;
        photonCount = 0;
    }
};

#ifdef __METAL_VERSION__

inline float mod(float x, float y) {
    return x - y * floor(x/y);
}

inline float2 convert1Dto2D(const float t, const float BufInfo)
{
    float2 tmp;
    
    tmp.x = mod(t, BufInfo);
    tmp.y = floor(t / BufInfo);

    return tmp;
}

inline float hash(const float3 idx, const float HashScale, const float BufInfo)
{
    const float HashNum = BufInfo * BufInfo;
    // use the same procedure as GPURnd
    float4 n = float4(idx, idx.x + idx.y - idx.z) * 4194304.0 / HashScale;
    //float4 n = float4(idx, HashScale * 0.5) * 4194304.0 / HashScale;

    const float4 q = float4(   1225.0,    1585.0,    2457.0,    2098.0);
    const float4 r = float4(   1112.0,     367.0,      92.0,     265.0);
    const float4 a = float4(   3423.0,    2646.0,    1707.0,    1999.0);
    const float4 m = float4(4194287.0, 4194277.0, 4194191.0, 4194167.0);

    float4 beta = floor(n / q);
    float4 p = a * (n - beta * q) - beta * r;
    beta = (sign(-p) + float4(1.0)) * float4(0.5) * m;
    n = (p + beta);

    return floor( fract(dot(n / m, float4(1.0, -1.0, 1.0, -1.0))) * HashNum );
}

#endif

#endif /* Photon_h */
