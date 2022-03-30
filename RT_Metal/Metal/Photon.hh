#pragma once

#ifndef Photon_h
#define Photon_h

#include "Common.hh"

#ifdef __METAL_VERSION__
#include "Render.hh"
#endif

struct PhotonRecord {
    
    float3 flux;
    
    float3 normal;
    float3 position;
    float3 direction;
    
    uint8_t step = 0;
    bool valid = false;
    
    void reset() {
        step = 0;
        valid = false;
    }
};

struct CameraRecord {
    float3 ratio;
    float3 position;
    float3 direction;

    bool valid = false;
    
    float radius;
    float photonCount;
    
    float3 flux;
};

#ifdef __METAL_VERSION__

inline float mod(float x, float y) {
    return x - y * floor(x/y);
}

//BufferSize * BufferSize = element count of the buffer
//BufInfo = float4(BufferSize, BufferSize, 1.0/BufferSize, 1.0/BufferSize)
inline float2 convert1Dto2D(const float t, const float BufInfo)
{
    float2 tmp;
    
    tmp.x = mod(t, BufInfo) + 0.5;
    tmp.y = floor(t / BufInfo) + 0.5;

    return tmp;
}

// HashNum = BufferSize * BufferSize;
// HashNum - the number of elements in the buffer.
inline float hash(const float3 idx, const float HashScale, const float HashNum)
{
    // use the same procedure as GPURnd
    float4 n = float4(idx, idx.x + idx.y - idx.z) * 4194304.0 / HashScale;

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
