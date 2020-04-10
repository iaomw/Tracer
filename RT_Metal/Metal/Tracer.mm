//#import <Foundation/Foundation.h>

#include "Tracer.hh"

enum class Axis { X=0, Y=1, Z=2 };
//const Axis AxisList[3]{Axis::X, Axis::Y, Axis::Z};

matrix_float4x4 matrix_float4x4_translation(vector_float3 t)
{
    vector_float4 X = { 1, 0, 0, 0 };
    vector_float4 Y = { 0, 1, 0, 0 };
    vector_float4 Z = { 0, 0, 1, 0 };
    vector_float4 W = { t.x, t.y, t.z, 1 };

    matrix_float4x4 mat = { X, Y, Z, W };
    return mat;
}

matrix_float4x4 matrix_float4x4_uniform_scale(float scale)
{
    vector_float4 X = { scale, 0, 0, 0 };
    vector_float4 Y = { 0, scale, 0, 0 };
    vector_float4 Z = { 0, 0, scale, 0 };
    vector_float4 W = { 0, 0, 0, 1 };

    matrix_float4x4 mat = { X, Y, Z, W };
    return mat;
}

matrix_float4x4 matrix_float4x4_rotation(vector_float3 axis, float angle)
{
    float c = cos(angle);
    float s = sin(angle);
    
    vector_float4 X;
    X.x = axis.x * axis.x + (1 - axis.x * axis.x) * c;
    X.y = axis.x * axis.y * (1 - c) - axis.z * s;
    X.z = axis.x * axis.z * (1 - c) + axis.y * s;
    X.w = 0.0;
    
    vector_float4 Y;
    Y.x = axis.x * axis.y * (1 - c) + axis.z * s;
    Y.y = axis.y * axis.y + (1 - axis.y * axis.y) * c;
    Y.z = axis.y * axis.z * (1 - c) - axis.x * s;
    Y.w = 0.0;
    
    vector_float4 Z;
    Z.x = axis.x * axis.z * (1 - c) - axis.y * s;
    Z.y = axis.y * axis.z * (1 - c) + axis.x * s;
    Z.z = axis.z * axis.z + (1 - axis.z * axis.z) * c;
    Z.w = 0.0;
    
    vector_float4 W;
    W.x = 0.0;
    W.y = 0.0;
    W.z = 0.0;
    W.w = 1.0;
    
    matrix_float4x4 mat = { X, Y, Z, W };
    return mat;
}

matrix_float4x4 matrix_float4x4_perspective(float aspect, float fovy, float near, float far)
{
    float yScale = 1 / tan(fovy * 0.5);
    float xScale = yScale / aspect;
    float zRange = far - near;
    float zScale = -(far + near) / zRange;
    float wzScale = -2 * far * near / zRange;

    vector_float4 P = { xScale, 0, 0, 0 };
    vector_float4 Q = { 0, yScale, 0, 0 };
    vector_float4 R = { 0, 0, zScale, -1 };
    vector_float4 S = { 0, 0, wzScale, 0 };

    matrix_float4x4 mat = { P, Q, R, S };
    return mat;
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
    
    camera.cornerLowLeft = lookFrom - vertical/2 - horizontal/2 -focus_dist*w;
    
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
    
    Cube r; r.a = a; r.b = b; //r.boundingBOX = MakeAABB(a, b);
    
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

static struct Square* cornell_box() {
    
    Material light; light.type= MaterialType::Diffuse; light.albedo = simd_make_float3(15, 15, 15);
    
    Material red; red.type = MaterialType::Lambert; red.albedo = simd_make_float3(0.65, 0.05, 0.05);
    Material green; green.type = MaterialType::Lambert; green.albedo = simd_make_float3(0.12, 0.45, 0.15);
    Material white; white.type = MaterialType::Lambert; white.albedo = simd_make_float3(0.73, 0.73, 0.73);
    
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

    static Square result[] = { right, left, top, bottom, back, lightSource };

    return result;
}
    
@implementation Tracer : NSObject

+ (float*)system_time {
    static float f;
    f = [[[NSDate alloc] init] timeIntervalSince1970];
    return &f;
}

+ (struct Cube*)cube_list; {
    
    Material white; white.type = MaterialType::Lambert;
    white.albedo = simd_make_float3(0.73, 0.73, 0.73);
    
    auto bigger = MakeCube(simd_make_float3(0,0,0),
                           simd_make_float3(165, 330, 165), white);
    
    auto translate = matrix_float4x4_translation(simd_make_float3(265, 0, 295));
    auto rotate = matrix_float4x4_rotation(simd_make_float3(0, 1, 0), 0);
    
    bigger.model_matrix = simd_mul(translate, rotate);
    bigger.inverse_matrix = simd_inverse(bigger.model_matrix);
    bigger.normal_matrix = simd_transpose(simd_inverse(bigger.model_matrix));
    
    auto smaller = MakeCube(simd_make_float3(0,0,0),
                            simd_make_float3(165, 165, 165), white);
    
    translate = matrix_float4x4_translation(simd_make_float3(130, 0, 65));
    rotate = matrix_float4x4_rotation(simd_make_float3(0, 1, 0), -18);
    
    smaller.model_matrix = simd_mul(translate, rotate);
    smaller.inverse_matrix = simd_inverse(smaller.model_matrix);
    smaller.normal_matrix = simd_transpose(simd_inverse(smaller.model_matrix));
    
    static Cube result[] = {bigger, smaller};
    return result;
}

+ (struct Square*)cornell_box {
    return cornell_box();
}

+ (struct Camera*)camera:(float2)viewSize {
    
    static Camera camera;
    auto aspect = viewSize.x/viewSize.y;
    
    auto lookFrom = simd_make_float3(278, 278, -800);
    auto lookAt = simd_make_float3(278, 278, 0);
    auto viewUp = simd_make_float3(0, 1, 0);
    auto vfov = 40; auto aperture = 0.0;
    auto dist_to_focus = 10;
    
    camera = MakeCamera(lookFrom, lookAt, viewUp, aperture, aspect, vfov, dist_to_focus);
   
    return &camera;
}

@end
