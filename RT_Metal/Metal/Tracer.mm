//#import <Foundation/Foundation.h>

#include "Tracer.hh"

enum class Axis { X=0, Y=1, Z=2 };
//const Axis AxisList[3]{Axis::X, Axis::Y, Axis::Z};

matrix_float4x4 matrix4x4_translation(float tx, float ty, float tz) {
    return (matrix_float4x4) {{
        { 1,   0,  0,  0 },
        { 0,   1,  0,  0 },
        { 0,   0,  1,  0 },
        { tx, ty, tz,  1 }
    }};
}

matrix_float4x4 matrix4x4_rotation(float radians, vector_float3 axis) {
    axis = vector_normalize(axis);
    float ct = cosf(radians);
    float st = sinf(radians);
    float ci = 1 - ct;
    float x = axis.x, y = axis.y, z = axis.z;

    return (matrix_float4x4) {{
        { ct + x * x * ci,     y * x * ci + z * st, z * x * ci - y * st, 0},
        { x * y * ci - z * st,     ct + y * y * ci, z * y * ci + x * st, 0},
        { x * z * ci + y * st, y * z * ci - x * st,     ct + z * z * ci, 0},
        {                   0,                   0,                   0, 1}
    }};
}

matrix_float4x4 matrix4x4_scale(float sx, float sy, float sz) {
    return (matrix_float4x4) {{
        { sx,  0,  0,  0 },
        { 0,  sy,  0,  0 },
        { 0,   0, sz,  0 },
        { 0,   0,  0,  1 }
    }};
}

static Camera MakeCamera(float3 lookFrom,
                    float3 lookAt,
                    float3 viewUp,
                    float aperture,
                    float aspect,
                    float vfov,
                    float focus_dist) {
    Camera camera;
    camera.lookFrom = lookFrom;
    camera.lookAt = lookAt;
    camera.viewUp = viewUp;
    
    camera.aperture = aperture;
    camera.aspect = aspect;
    camera.vfov = vfov;
    
    camera.focus_dist = focus_dist;
    
    camera.lenRadius = aperture / 2;
    auto theta = vfov * M_PI_F / 180;
    
    auto halfHeight = tan(theta/2);
    auto halfWidth = aspect * halfHeight;
    
    auto w = simd_normalize(lookFrom - lookAt);
    auto t = simd_cross(viewUp, w);
    auto u = simd_normalize(t);
    auto v = simd_cross(w, u);
    camera.u = u; camera.v = v; camera.w = w;
    
    auto vertical = 2*halfHeight*focus_dist*camera.v;
    auto horizontal = 2*halfWidth*focus_dist*camera.u;
    
    camera.vertical = vertical;
    camera.horizontal = horizontal;
    
    camera.cornerLowLeft = lookFrom - vertical/2 - horizontal/2 - focus_dist*w;
    
    return camera;
}
    
inline AABB MakeAABB(float3 a, float3 b) {

    auto mini = simd_make_float3(fminf(a.x, b.x),
                                  fminf(a.y, b.y),
                                  fminf(a.z, b.z));
    
    auto maxi = simd_make_float3(fmaxf(a.x, b.x),
                                 fmaxf(a.y, b.y),
                                 fmaxf(a.z, b.z));
    
    AABB r; r.mini = mini; r.maxi = maxi;
    
    return r;
}

inline AABB MakeAABB(AABB box_s, AABB box_e) {
    
    auto small = simd_make_float3(fminf(box_s.mini.x, box_e.mini.x),
                                  fminf(box_s.mini.y, box_e.mini.y),
                                  fminf(box_s.mini.z, box_e.mini.z));
    
    auto big = simd_make_float3(fmaxf(box_s.maxi.x, box_e.maxi.x),
                                fmaxf(box_s.maxi.y, box_e.maxi.y),
                                fmaxf(box_s.maxi.z, box_e.maxi.z));

    return MakeAABB(small, big);
}
    
static Square MakeSquare(uint8_t axis_i, float2 rang_i, uint8_t axis_j, float2 rang_j, uint8_t axis_k, float k) {
    Square r;
    
    r.axis_i = axis_i;
    r.axis_j = axis_j;
    r.rang_i = rang_i;
    r.rang_j = rang_j;
    r.axis_k = axis_k;
    r.value_k = k;
    
    auto a = float3();
    a[axis_i] = rang_i.x;
    a[axis_j] = rang_j.x;
    a[axis_k] = k - 0.0001;
    
    auto b = float3();
    b[axis_i] = rang_i.y;
    b[axis_j] = rang_j.y;
    b[axis_k] = k + 0.0001;
    
    r.boundingBOX = MakeAABB(a, b);
    
    return r;
}
    
Cube MakeCube(float3 a, float3 b, Material material) {
    
    Cube r; r.a = a; r.b = b; r.boundingBOX = MakeAABB(a, b);
    
    r.rectList[0] = MakeSquare(0, simd_make_float2(a.x, b.x), 1, simd_make_float2(a.y, b.y), 2, a.z);
    r.rectList[1] = MakeSquare(0, simd_make_float2(a.x, b.x), 1, simd_make_float2(a.y, b.y), 2, b.z);
    
    r.rectList[2] = MakeSquare(1, simd_make_float2(a.y, b.y), 2, simd_make_float2(a.z, b.z), 0, a.x);
    r.rectList[3] = MakeSquare(1, simd_make_float2(a.y, b.y), 2, simd_make_float2(a.z, b.z), 0, b.x);
    
    r.rectList[4] = MakeSquare(2, simd_make_float2(a.z, b.z), 0, simd_make_float2(a.x, b.x), 1, a.y);
    r.rectList[5] = MakeSquare(2, simd_make_float2(a.z, b.z), 0, simd_make_float2(a.x, b.x), 1, b.y);
    
    for (int i=0; i<6; i++) {
        r.rectList[i].material = material;
    }
    
    return r;
};

void sphereUV(float3& p, float2& uv) {
    auto phi = atan2(p.z, p.x);
    auto theta = asin(p.y);
    uv[0] = 1-(phi + M_PI_F) / (2*M_PI_F);
    uv[1] = (theta + M_PI_F/2) / M_PI_F;
}

Sphere MakeSphere(float r, float3 c) {
    Sphere s; s.radius = r; s.center = c;
    auto offset = simd_make_float3(r);
    s.boundingBOX = MakeAABB(c-offset, c+offset);
    return s;
}

void prepareCornellBox(struct Square* pointer) {
    
    Material light; light.type= MaterialType::Diffuse; light.albedo = simd_make_float3(15, 15, 15);
    
    Material red; red.type = MaterialType::Lambert; red.albedo = simd_make_float3(0.65, 0.05, 0.05);
    Material green; green.type = MaterialType::Lambert; green.albedo = simd_make_float3(0.12, 0.45, 0.15);
    Material white; white.type = MaterialType::Lambert; white.albedo = simd_make_float3(0.73, 0.73, 0.73);
    
    Material metal; metal.type = MaterialType::Metal;
    metal.albedo = simd_make_float3(0.8, 0.85, 0.88);
    
    //Material material_list[] = {light, red, green, white};
    
    //auto lightSource = MakeSquare(0, simd_make_float2(213, 343), 2, simd_make_float2(227, 332), 1, 554);
    auto lightSource = MakeSquare(0, simd_make_float2(113, 443), 2, simd_make_float2(127, 432), 1, 554);
    lightSource.material = light;

    auto right = MakeSquare(1, simd_make_float2(0, 555), 2, simd_make_float2(0, 555), 0, 555); //flip
    right.material = green;

    auto left = MakeSquare(1, simd_make_float2(0, 555), 2, simd_make_float2(0, 555), 0, 0);
    left.material = red;

    auto top = MakeSquare(0, simd_make_float2(0, 555), 2, simd_make_float2(0, 555), 1, 555);
    top.material = white;

    auto bottom = MakeSquare(0, simd_make_float2(0, 555), 2, simd_make_float2(0, 555), 1, 0);
    bottom.material = white;

    auto back =  MakeSquare(0, simd_make_float2(0, 555), 1, simd_make_float2(0, 555), 2, 555);
    back.material = white;

    //static Square result[] = { right, left, top, bottom, back, lightSource };
    
    pointer[0] = right;
    pointer[1] = left;
    pointer[2] = top;
    pointer[3] = bottom;
    pointer[4] = back;
    pointer[5] = lightSource;
}

void prepareCubeList(struct Cube* pointer) {
    
    Material metal; metal.type = MaterialType::Metal;
    metal.albedo = simd_make_float3(0.8, 0.85, 0.88);
    
    auto bigger = MakeCube(simd_make_float3(0, 0, 0),
                           simd_make_float3(165, 330, 165), metal);
    
    auto translate = matrix4x4_translation(265, 0, 295);
    auto rotate = matrix4x4_rotation(M_PI*15/180, simd_make_float3(0, 1, 0));
    
    bigger.model_matrix = simd_mul(translate, rotate);
    bigger.inverse_matrix = simd_inverse(bigger.model_matrix);
    bigger.normal_matrix = simd_transpose(bigger.inverse_matrix);
    
    Material white; white.type = MaterialType::Lambert;
    white.albedo = simd_make_float3(0.73, 0.73, 0.73);
    
    auto smaller = MakeCube(simd_make_float3(0, 0, 0),
                            simd_make_float3(165, 165, 165), white);
    
    translate = matrix4x4_translation(130, 0, 65);
    rotate = matrix4x4_rotation(-0.1*M_PI, simd_make_float3(0, 1, 0));
    
    smaller.model_matrix = simd_mul(translate, rotate);
    smaller.inverse_matrix = simd_inverse(smaller.model_matrix);
    smaller.normal_matrix = simd_transpose(smaller.inverse_matrix);
     
    //static Cube result[] = {bigger, smaller};
    pointer[0] = bigger;
    pointer[1] = smaller;
}

void prepareCamera(struct Camera* pointer, float2 viewSize) {
    
    static Camera camera;
    auto aspect = viewSize.x/viewSize.y;
    
    auto lookFrom = simd_make_float3(278, 278, -800);
    auto lookAt = simd_make_float3(278, 278, 0);
    auto viewUp = simd_make_float3(0, 1, 0);
    auto vfov = 40; auto aperture = 0.0;
    auto dist_to_focus = 10;
    
    camera = MakeCamera(lookFrom, lookAt, viewUp, aperture, aspect, vfov, dist_to_focus);
    *pointer = camera;
}
