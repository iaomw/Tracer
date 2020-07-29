//#import <Foundation/Foundation.h>

#include "Tracer.hh"

enum class Axis { X=0, Y=1, Z=2 };
//const Axis AxisList[3]{Axis::X, Axis::Y, Axis::Z};

float4x4 matrix4x4_scale(float sx, float sy, float sz) {
    return (float4x4) {{
        { sx,  0,  0,  0 },
        { 0,  sy,  0,  0 },
        { 0,   0, sz,  0 },
        { 0,   0,  0,  1 }
    }};
}

float4x4 matrix4x4_rotation(float radians, float3 axis) {
    axis = vector_normalize(axis);
    float ct = cosf(radians);
    float st = sinf(radians);
    float ci = 1 - ct;
    float x = axis.x, y = axis.y, z = axis.z;

    return (float4x4) {{
        { ct + x * x * ci,     y * x * ci + z * st, z * x * ci - y * st, 0},
        { x * y * ci - z * st,     ct + y * y * ci, z * y * ci + x * st, 0},
        { x * z * ci + y * st, y * z * ci - x * st,     ct + z * z * ci, 0},
        {                   0,                   0,                   0, 1}
    }};
}

float4x4 matrix4x4_translation(float tx, float ty, float tz) {
    return (float4x4) {{
        { 1,   0,  0,  0 },
        { 0,   1,  0,  0 },
        { 0,   0,  1,  0 },
        { tx, ty, tz,  1 }
    }};
}

void MakeCamera(Camera* camera,
                float3 lookFrom,
                float3 lookAt,
                float3 viewUp,
                float aperture,
                float aspect,
                float vfov,
                float focus_dist) {
    //Camera camera;
    camera->lookFrom = lookFrom;
    camera->lookAt = lookAt;
    camera->viewUp = viewUp;
    
    camera->aperture = aperture;
    camera->aspect = aspect;
    camera->vfov = vfov;
    
    camera->focus_dist = focus_dist;
    
    camera->lenRadius = aperture / 2;
    auto theta = vfov; //* M_PI_F / 180;
    
    auto halfHeight = tan(theta/2);
    auto halfWidth = aspect * halfHeight;
    
    auto w = simd_normalize(lookFrom - lookAt);
    auto t = simd_cross(viewUp, w);
    auto u = simd_normalize(t);
    auto v = simd_cross(w, u);
    camera->u = u; camera->v = v; camera->w = w;
    
    auto vertical = 2*halfHeight*focus_dist*camera->v;
    auto horizontal = 2*halfWidth*focus_dist*camera->u;
    
    camera->vertical = vertical;
    camera->horizontal = horizontal;
    
    camera->cornerLowLeft = lookFrom - vertical/2 - horizontal/2 - focus_dist*w;
}
    
inline AABB MakeAABB(float3& a, float3& b) {

    auto mini = simd_make_float3(fminf(a.x, b.x),
                                  fminf(a.y, b.y),
                                  fminf(a.z, b.z));
    
    auto maxi = simd_make_float3(fmaxf(a.x, b.x),
                                 fmaxf(a.y, b.y),
                                 fmaxf(a.z, b.z));
    
    AABB r; r.mini = mini; r.maxi = maxi;
    
    return r;
}

inline AABB MakeAABB(AABB& box_s, AABB& box_e) {
    
    auto small = simd_make_float3(fminf(box_s.mini.x, box_e.mini.x),
                                  fminf(box_s.mini.y, box_e.mini.y),
                                  fminf(box_s.mini.z, box_e.mini.z));
    
    auto big = simd_make_float3(fmaxf(box_s.maxi.x, box_e.maxi.x),
                                fmaxf(box_s.maxi.y, box_e.maxi.y),
                                fmaxf(box_s.maxi.z, box_e.maxi.z));

    return MakeAABB(small, big);
}
    
Square MakeSquare(uint8_t axis_i, float2 rang_i, uint8_t axis_j, float2 rang_j, uint8_t axis_k, float k) {
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
    
Cube MakeCube(float3 a, float3 b, Material& material) {
    
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
    Sphere s; s.radius = r+0.0001; s.center = c;
    auto offset = simd_make_float3(r, r, r);
    auto a = c-offset, b = c+offset;
    s.boundingBOX = MakeAABB(a, b);
    return s;
}

void prepareCubeList(std::vector<Cube>& list) {
    
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

    list.emplace_back(bigger);
    list.emplace_back(smaller);
}

void prepareCornellBox(std::vector<Square>& list) {
    
    Material light; light.type= MaterialType::Diffuse; light.albedo = simd_make_float3(20, 20, 20);
    
    Material red; red.type = MaterialType::Lambert; red.albedo = simd_make_float3(0.65, 0.05, 0.05);
    Material green; green.type = MaterialType::Lambert; green.albedo = simd_make_float3(0.12, 0.45, 0.15);
    Material white; white.type = MaterialType::Lambert; white.albedo = simd_make_float3(0.73, 0.73, 0.73);
    
    Material metal; metal.type = MaterialType::Metal;
    metal.albedo = simd_make_float3(0.8, 0.85, 0.88);
    
    //Material material_list[] = {light, red, green, white};
    
    auto lightSource = MakeSquare(0, simd_make_float2(204, 360), 2, simd_make_float2(220, 340), 1, 554);
    //auto lightSource = MakeSquare(0, simd_make_float2(113, 443), 2, simd_make_float2(127, 432), 1, 554);
    lightSource.material = light;

    auto right = MakeSquare(1, simd_make_float2(0, 555), 2, simd_make_float2(0, 555), 0, 555); //flip
    right.material = red;

    auto left = MakeSquare(1, simd_make_float2(0, 555), 2, simd_make_float2(0, 555), 0, 0);
    left.material = green;

    auto top = MakeSquare(0, simd_make_float2(0, 555), 2, simd_make_float2(0, 555), 1, 555);
    top.material = white;

    auto bottom = MakeSquare(0, simd_make_float2(0, 555), 2, simd_make_float2(0, 555), 1, 0);
    bottom.material = white;

    auto back =  MakeSquare(0, simd_make_float2(0, 555), 1, simd_make_float2(0, 555), 2, 555);
    back.material = white;

    list.emplace_back(right);
    list.emplace_back(left);
    list.emplace_back(top);
    list.emplace_back(bottom);
    list.emplace_back(back);
    list.emplace_back(lightSource);
}

void prepareSphereList(std::vector<Sphere>& list) {
    
    Material glass; glass.type = MaterialType::Dielectric;
    glass.albedo = simd_make_float3(1.0, 1.0, 1.0);
    glass.parameter = 1.5;
    
    auto sphere = MakeSphere(64, simd_make_float3(200, 250, 200));
    sphere.material = glass;
    
    list.emplace_back(sphere);
    
    Material specu; specu.type = MaterialType::Specular;
    specu.albedo = simd_make_float3(0.3f, 1.0f, 0.3f);
    
    sphere = MakeSphere(50, simd_make_float3(100, 450, 400));
    specu.parameter = 0.0;
    sphere.material = specu;
    
    list.emplace_back(sphere);
    
    sphere = MakeSphere(50, simd_make_float3(250, 450, 400));
    specu.parameter = 0.25;
    sphere.material = specu;
    
    list.emplace_back(sphere);
    
    sphere = MakeSphere(50, simd_make_float3(400, 450, 400));
    specu.parameter = 0.5;
    sphere.material = specu;
    
    list.emplace_back(sphere);
    
    sphere = MakeSphere(32, simd_make_float3(300, 100, 10));
    specu.parameter = 0.75;
    sphere.material = specu;
    
    list.emplace_back(sphere);
    
    sphere = MakeSphere(32, simd_make_float3(400, 100, 10));
    specu.parameter = 1.00;
    sphere.material = specu;
    
    list.emplace_back(sphere);
}

void prepareCamera(struct Camera* camera, float2 viewSize, float2 rotate) {
    
    auto aspect = viewSize.x/viewSize.y;
    
    auto lookFrom = simd_make_float3(278, 278, -800);
    auto lookAt = simd_make_float3(278, 278, 278);
    auto viewUp = simd_make_float3(0, 1, 0);
    
    auto dist_to_focus = 10;
    auto aperture = 0.0;
    
    auto vfov = 45 * (M_PI/180);
    auto hfov = 2 * atan(tan(vfov * 0.5) * aspect);
    
    let offset = simd_make_float4(lookFrom - lookAt, 0.0f);
    
    let rotH = matrix4x4_rotation(rotate.x * hfov * 10, viewUp);
    let rotV = matrix4x4_rotation(rotate.y * vfov * 10,  simd_make_float3(1, 0, 0));
    //let rotV = matrix4x4_rotation(rotate.y * vfov * 10, simd_mul(rotH, simd_make_float4(1, 0, 0, 0)).xyz);
    
    lookFrom = lookAt + simd_mul(simd_mul(rotH, rotV), offset).xyz;
    
    MakeCamera(camera, lookFrom, lookAt, viewUp, aperture, aspect, vfov, dist_to_focus);
}
