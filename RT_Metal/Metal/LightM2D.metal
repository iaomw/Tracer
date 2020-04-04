
#include <metal_stdlib>
#include "Random.h"

#define N 256
#define MAX_STEP 64
#define MAX_DISTANCE 5.0f

#define BIAS 1e-4f
#define EPSILON 1e-6f

#define MAX_DEPTH 3

using namespace metal;

typedef struct {
    float sd, reflectivity, eta;
    float3 emissive, absorption;
} ResultM2D;

static ResultM2D unionOp(ResultM2D a, ResultM2D b) {
    //return (a.sd<b.sd)? a:b;
    if (a.sd < b.sd) {
        return a;
    } else {
        return b;
    }
}

static ResultM2D intersectOp(ResultM2D a, ResultM2D b) {
    if (a.sd > b.sd) {
        return a;
    } else {
        return b;
    }
}

static ResultM2D subtractOp(ResultM2D a, ResultM2D b) {
    ResultM2D r = a;
    if (a.sd > -b.sd) {
        r.sd = a.sd;
    } else {
        r.sd = -b.sd;
    }
    return r;
}

static ResultM2D complementOp(ResultM2D a) {
    a.sd = -a.sd;
    return a;
}

static bool refractM2D(float ix, float iy,
                       float nx, float ny,
                       float eta,
                       thread float* rx, thread float* ry) {
    float idotn = ix * nx + iy * ny;
    float k = 1.0f - eta * eta * (1.0f - idotn * idotn);
    if (k < 0.0f)
        return false; // 全内反射
    float a = eta * idotn + sqrt(k);
    *rx = eta * ix - a * nx;
    *ry = eta * iy - a * ny;
    return true;
}

static float2 reflectM2D(float ix, float iy, float nx, float ny) {
    float idotn2 = (ix * nx + iy * ny) * 2.0f;
    auto rx = ix - idotn2 * nx;
    auto ry = iy - idotn2 * ny;
    return float2(rx, ry);
}

static float fresnelM2D(float cosi, float cost, float etai, float etat) {
    float rs = (etat * cosi - etai * cost) / (etat * cosi + etai * cost);
    float rp = (etai * cosi - etat * cost) / (etai * cosi + etat * cost);
    return (rs * rs + rp * rp) * 0.5f;
}

static float schlickM2D(float cosi, float cost, float etai, float etat) {
    float r0 = (etai - etat) / (etai + etat);
    r0 *= r0;
    float a = 1.0f - (etai < etat ? cosi : cost);
    float aa = a * a;
    return r0 + (1.0f - r0) * aa * aa * a;
}

static float3 beerLambertM2D(float3 a, float3 d) {
    return exp(-a * d);
}

static float segmentSDF(float x, float y, float ax, float ay, float bx, float by) {
    float vx = x-ax, vy = y-ay, ux = bx-ax, uy = by-ay;
    float t = max(min((vx*ux+vy*uy)/(ux*ux+uy*uy), 1.0), 0.0);
    float dx = vx-ux*t, dy = vy-uy*t;
    return sqrt(dx*dx+dy*dy);
}

static float capsuleSDF(float x, float y,
                        float ax, float ay,
                        float bx, float by, float r){
    return segmentSDF(x, y, ax, ay, bx, by) - r;
}

static float boxSDF(float x, float y, float cx, float cy, float theta, float sx, float sy) {
    auto costheta = cos(theta), sintheta = sin(theta);
    
    auto dx = abs((x-cx)*costheta + (y-cy)*sintheta) - sx;
    auto dy = abs((y-cy)*costheta - (x-cx)*sintheta) - sy;
    
    float ax = fmax(dx, 0.0), ay = fmax(dy, 0.0);
    return min(max(dx, dy), 0.0) + sqrt(ax*ax+ay*ay);
}

static float triangleSDF(float x, float y,
                  float ax, float ay,
                  float bx, float by,
                  float cx, float cy) {
    
  auto d = min(min(
    segmentSDF(x, y, ax, ay, bx, by),
    segmentSDF(x, y, bx, by, cx, cy)),
    segmentSDF(x, y, cx, cy, ax, ay));
    
    auto check = (bx - ax) * (y - ay) > (by - ay) * (x - ax) &&
    (cx - bx) * (y - by) > (cy - by) * (x - bx) &&
    (ax - cx) * (y - cy) > (ay - cy) * (x - cx);
    
    if (check) {
        return -d;
    } else {
        return d;
    }
}

static float circleSDF(float x, float y, float cx, float cy, float r) {
    float ux = x - cx, uy = y - cy;
    return sqrt(ux*ux + uy*uy) - r;
}

static float planeSDF(float x, float y, float px, float py, float nx, float ny) {
    return (x-px)*nx + (y-py)*ny;
}

static float ngonSDF(float x, float y, float cx, float cy, float r, float n) {
    float ux = x - cx, uy = y - cy, a = 2*M_PI_F / n;
    float t = modf(atan2(uy, ux) + 2*M_PI_F, a), s = sqrt(ux * ux + uy * uy);
    return planeSDF(s * cos(t), s * sin(t), r, 0.0f, cos(a * 0.5f), sin(a * 0.5f));
}

static ResultM2D scene(float x, float y) {
    
    ResultM2D a = { circleSDF(x, y, 0.5f, -0.2f, 0.1f),
                    0.0f, 0.0f, float3(0), float3(0) };
    ResultM2D b = {  ngonSDF(x, y, 0.5f, 0.5f, 0.25f, 5.0f),
                    0.0f, 1.5f, float3(0), float3(4.0f, 4.0f, 1.0f) };
    return unionOp(a, b);
    
    //ResultM2D a = { circleSDF(x, y, -0.2f, -0.2f, 0.1f), 10.0f, 0.0f, 0.0f, 0.0f };
    //ResultM2D b = { boxSDF(x, y, 0.5f, 0.5f, 0.0f, 0.3, 0.2f), 0.0f, 0.2f, 1.5f, 4.0f };
    
    //return unionOp(a, b);
//    ResultM2D c = { circleSDF(x, y, 0.5f, -0.5f, 0.05f), 20.0f, 0.0f, 0.0f };
//    ResultM2D d = { circleSDF(x, y, 0.5f, 0.2f, 0.35f), 0.0f, 0.2f, 1.5f };
//    ResultM2D e = { circleSDF(x, y, 0.5f, 0.8f, 0.35f), 0.0f, 0.2f, 1.5f };
//    ResultM2D f = {    boxSDF(x, y, 0.5f, 0.5f, 0.0f, 0.2, 0.1f), 0.0f, 0.2f, 1.5f };
//    ResultM2D g = { circleSDF(x, y, 0.5f, 0.12f, 0.35f), 0.0f, 0.2f, 1.5f };
//    ResultM2D h = { circleSDF(x, y, 0.5f, 0.87f, 0.35f), 0.0f, 0.2f, 1.5f };
//    ResultM2D i = { circleSDF(x, y, 0.5f, 0.5f, 0.2f), 0.0f, 0.2f, 1.5f };
//    ResultM2D j = {  planeSDF(x, y, 0.5f, 0.5f, 0.0f, -1.0f), 0.0f, 0.2f, 1.5f };
    //return unionOp(a, b);
    //return unionOp(c, intersectOp(d, e));
    //return unionOp(c, subtractOp(f, unionOp(g, h)));
    //return unionOp(c, intersectOp(i, j));

    //ResultM2D a = { circleSDF(x, y, 0.4, 0.2, 0.1), 2.0, 0.0, 0.0 };
    //ResultM2D b = { boxSDF(x, y, 0.5, 0.8, M_PI_F/16, 0.1, 0.1), 0.0, 0.9, 1.5 };
    //ResultM2D c = { boxSDF(x, y, 0.8, 0.5, M_PI_F/16, 0.1, 0.1), 0.0, 0.9 , 0.0};
    
    //return unionOp(unionOp(a, b), c);
    
    //ResultM2D f = { triangleSDF(x, y, 0.5, 0.2, 0.8, 0.8, 0.3, 0.6), 1.0 };
    //return f;
    
    //ResultM2D d = { boxSDF(x, y, 0.5, 0.5, M_PI_F/16, 0.3, 0.1)-0.1, 1.0 };
    //return d;
    
    //ResultM2D c = { capsuleSDF(x, y, 0.4, 0.4, 0.6, 0.6, 0.1), 1.0f};
    //return c;
    
    //ResultM2D a = { circleSDF(x, y, 0.5, 0.5, 0.2), 1.0 };
    //ResultM2D b = { planeSDF(x, y, 0, 0.5, 0, 1.0), 0.8 };
    //return intersectOp(a, b);
    
//    ResultM2D r1 = { circleSDF(x, y, 0.3f, 0.3f, 0.10f), 2.0f };
//    ResultM2D r2 = { circleSDF(x, y, 0.3f, 0.7f, 0.05f), 0.8f };
//    ResultM2D r3 = { circleSDF(x, y, 0.7f, 0.5f, 0.10f), 0.3f };
    //return unionOp(unionOp(r1, r2), r3);
    
//    ResultM2D a;
//    a.sd = circleSDF(x, y, 0.4, 0.5, 0.2);
//    a.emissive = 1.0f;
//
//    ResultM2D b;
//    b.sd = circleSDF(x, y, 0.6, 0.5, 0.2);
//    b.emissive = 0.8f;
    
    //return unionOp(a, b);
    //return intersectOp(a, b);
    //return subtractOp(a, b);
    //return subtractOp(b, a);
}

static void gradientM2D(float x, float y, thread float* nx, thread float* ny) {
    *nx = (scene(x + EPSILON, y).sd - scene(x - EPSILON, y).sd) * (0.5f / EPSILON);
    *ny = (scene(x, y + EPSILON).sd - scene(x, y - EPSILON).sd) * (0.5f / EPSILON);
}

typedef struct {
    uint depth;
    bool has_color;
    float3 color;
    
    bool has_refract;
    float2 refract;
    float2 refract_pos;
    float3 refract_ratio;
    
    bool has_reflect;
    float2 reflect;
    float2 reflect_pos;
    float3 reflect_ratio;
    
} TraceResult;

static TraceResult traceM2D(float ox, float oy,
                            float dx, float dy,
                            float3 base_ratio, int depth)
{
    
    TraceResult traceResult;
    traceResult.depth = depth;
    traceResult.has_color = false;
    traceResult.reflect_ratio = base_ratio;
    traceResult.refract_ratio = base_ratio;
    //if (depth>=MAX_DEPTH) {return traceResult;}
    
    float t = 1e-3f;
    float sign;
    
    if(scene(ox, oy).sd > 0) {
        sign = 1;
    } else {
        sign = -1;
    }
    
    for (int i=0; i<MAX_STEP && t<MAX_DISTANCE; i++) {
        float x = ox + dx * t, y = oy + dy *t;
        auto r = scene(x, y);
        
        if (sign*r.sd < EPSILON) {
            auto bee = beerLambertM2D(r.absorption, t);
            float3 color = float3(r.emissive);
            traceResult.color = color*bee;
            traceResult.has_color = true;
            
            if(depth<MAX_DEPTH && (r.reflectivity>0.0 || r.eta>0.0)) {
                float nx, ny;
                gradientM2D(x, y, &nx, &ny);
                auto refl = r.reflectivity;
                
                auto eta = 1.0/r.eta;
                if (sign<0) {
                    nx*=-1; ny*=-1;
                    eta = r.eta;
                }
                
                float rx, ry;
                if (r.eta>0.0) {
                    if (refractM2D(dx, dy, nx, ny, eta, &rx, &ry)) {
                        
                        auto cosi = -(dx*nx + dy*ny);
                        auto cost = -(rx*nx + ry*ny);
                        
                        if (sign<0) {
                            refl = schlickM2D(cosi, cost, r.eta, 1.0);
                        } else {
                            refl = schlickM2D(cosi, cost, 1.0, r.eta);
                        }
                        
                        traceResult.has_refract = true;
                        traceResult.refract = float2(rx, ry);
                        traceResult.refract_ratio *= bee*(1.0-refl);
                        traceResult.refract_pos = float2(x-nx*BIAS, y-ny*BIAS);
                        
                    } else {
                        refl = 1.0;
                    }
                }
                
                if (refl>0.0) {
                    
                    float2 reflected = reflectM2D(dx, dy, nx, ny);
                    traceResult.has_reflect = true;
                    traceResult.reflect = reflected;
                    traceResult.reflect_ratio *= bee*refl;
                    traceResult.reflect_pos = float2(x+nx*BIAS, y+ny*BIAS);
                }
            }
            return traceResult;
        }
        t += sign*r.sd;
    }
    return traceResult;
}

static float3 sampleM2D(float x, float y, float t) {
    float3 color = float3(0);
    
    for (int i=0; i<N; i++) {
        //float a = 2*M_PI_F*i/N;
        float a = 2*M_PI_F*(i+randFromF2(float2(x,y)*t))/N;
        float2 direction = float2(cos(a), sin(a));
        
        auto traceResult = traceM2D(x, y, direction.x, direction.y, 1, 0);
        
        if (traceResult.has_color) {
            color += traceResult.color.x;
        } else {
            continue;
        }
        
        TraceResult tmp[3];
        tmp[0] = traceResult;
        
        int checkIndex = 0;
        int storeIndex = 0;
        
        for (checkIndex=0; checkIndex<=storeIndex && checkIndex<3; ++checkIndex) {
            
            traceResult = tmp[checkIndex];
            auto depth = 1+traceResult.depth;
            
            if (traceResult.has_reflect) {
                auto _pos = traceResult.reflect_pos;
                auto _dir = traceResult.reflect;

                auto reflect_result = traceM2D(_pos.x, _pos.y,
                                               _dir.x, _dir.y,
                                               traceResult.reflect_ratio, depth);

                if (reflect_result.has_color) {
                    if(depth<2) {
                        ++storeIndex;
                        tmp[storeIndex] = reflect_result;
                    }
                    color += traceResult.reflect_ratio*reflect_result.color.x;
                }
            }
            
            if (traceResult.has_refract) {
                auto _pos = traceResult.refract_pos;
                auto _dir = traceResult.refract;

                auto refract_result = traceM2D(_pos.x, _pos.y,
                                               _dir.x, _dir.y,
                                               traceResult.refract_ratio, depth);
                
                if (refract_result.has_color) {
                    
                    if(depth<2) {
                        ++storeIndex;
                        tmp[storeIndex] = refract_result;
                    }
                    color += traceResult.refract_ratio*refract_result.color.x;
                }
            }
        }
    }
    
    return min(color/N, float3(1.0));
}


