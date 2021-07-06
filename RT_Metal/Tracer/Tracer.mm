#include "Tracer.hh"

float4x4 scale4x4(float sx, float sy, float sz) {
    return (float4x4) {{
        { sx,  0,  0,  0 },
        { 0,  sy,  0,  0 },
        { 0,   0, sz,  0 },
        { 0,   0,  0,  1 }
    }};
}

float4x4 rotation4x4(float radians, float3 axis) {
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

float4x4 translation4x4(float tx, float ty, float tz) {
    return (float4x4) {{
        { 1,   0,  0,  0 },
        { 0,   1,  0,  0 },
        { 0,   0,  1,  0 },
        { tx, ty, tz,  1 }
    }};
}

float4x4 LookAt(const float3 &pos, const float3 &look, const float3 &up) {
    
    float4x4 cameraToWorld;
    
    float3 dir = simd::normalize(look - pos);
    
    if (simd::length( simd::cross(simd::normalize(up), dir) ) == 0) {
//        Error(
//            "\"up\" vector (%f, %f, %f) and viewing direction (%f, %f, %f) "
//            "passed to LookAt are pointing in the same direction.  Using "
//            "the identity transformation.",
//            up.x, up.y, up.z, dir.x, dir.y, dir.z);
        return float4x4();
    }
    
    float3 right = simd::normalize(simd::cross(simd::normalize(up), dir));
    float3 newUp = simd::cross(dir, right);
    
    return simd::inverse(cameraToWorld);
    
    return float4x4 {{
        {right.x, right.y, right.z, 0},
        {newUp.x, newUp.y, newUp.z, 0},
        {dir.x,   dir.y,   dir.z,   0},
        {pos.x,   pos.y,   pos.z,   1},
    }}; // or inverse
}

float4x4 Perspective(float fov, float n, float f) {
    // Perform projective divide for perspective projection
    float4x4 persp {{
        { 1, 0, 0, 0 },
        { 0, 1, 0, 0 },
        { 0, 0, f / (f - n), -f * n / (f - n) },
        { 0, 0, 1, 0 }
    }}; // or inverse
    // Scale canonical perspective view to specified field of view
    float invTanAng = 1 / tan(Radians(fov) / 2);
    auto scale = scale4x4(invTanAng, invTanAng, 1);
    
    return simd_mul(scale, persp);
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
    
Square MakeSquare(uint8_t axis_i, float2 range_i, uint8_t axis_j, float2 range_j, uint8_t axis_k, float k) {
    Square r;
    
    r.axis_i = axis_i;
    r.axis_j = axis_j;
    r.range_i = range_i;
    r.range_j = range_j;
    r.axis_k = axis_k;
    r.value_k = k;
    
    auto a = float3();
    a[axis_i] = range_i.x;
    a[axis_j] = range_j.x;
    a[axis_k] = k - 0.0001;
    
    auto b = float3();
    b[axis_i] = range_i.y;
    b[axis_j] = range_j.y;
    b[axis_k] = k + 0.0001;
    
    r.boundingBOX = AABB::make(a, b);
    r.model_matrix = matrix_identity_float4x4;
    
    return r;
}
    
Cube MakeCube(float3 a, float3 b, uint32_t material) {
    
    Cube r;
    r.box = AABB::make(a, b);
    r.boundingBOX = r.box;
    r.material = material;
    r.model_matrix = matrix_identity_float4x4;
    
    return r;
};

Sphere MakeSphere(float r, float3 c) {
    Sphere s; s.radius = r+0.0001; s.center = c;
    auto offset = simd_make_float3(r, r, r);
    auto a = c-offset, b = c+offset;
    s.boundingBOX = AABB::make(a, b);
    s.model_matrix = matrix_identity_float4x4;
    return s;
}

void prepareCubeList(std::vector<Cube>& list, std::vector<Material>& materials) {
    
    Material metal; metal.type = MaterialType::Metal;
    
    metal.textureInfo.type = TextureType::Constant;
    metal.textureInfo.albedo = simd_make_float3(0.8, 0.85, 0.88);
    
    auto metal_index = (uint32_t)materials.size();
    materials.emplace_back(metal);
    
    auto bigger = MakeCube(simd_make_float3(0, 0, 0),
                           simd_make_float3(165, 330, 165), metal_index);
    
    auto translate = translation4x4(265, 0, 295);
    auto rotate = rotation4x4(M_PI*15/180, simd_make_float3(0, 1, 0));
    // left-bottom-front point is the rotation point, that's bad.
    
    bigger.model_matrix = simd_mul(translate, rotate);
    bigger.inverse_matrix = simd_inverse(bigger.model_matrix);
    bigger.normal_matrix = simd_transpose(bigger.inverse_matrix);
    
    list.emplace_back(bigger);
    
//    translate = matrix4x4_translation(126, 0, 295); //126
//    rotate = matrix4x4_rotation(-0.5*M_PI*15/180, simd_make_float3(0, 1, 0));
//
//    bigger.model_matrix = simd_mul(translate, rotate);
//    bigger.inverse_matrix = simd_inverse(bigger.model_matrix);
//    bigger.normal_matrix = simd_transpose(bigger.inverse_matrix);
//
//    list.emplace_back(bigger);
    
    Material white; white.type = MaterialType::Isotropic;
    white.textureInfo.albedo = simd_make_float3(0.73, 0.73, 0.73);
    white.textureInfo.albedo = simd_make_float3(1, 1, 1);
    white.parameter = 0.01;
    
    auto white_index = (uint32_t)materials.size();
    materials.emplace_back(white);
    
    auto smaller = MakeCube(simd_make_float3(0, 0, 0),
                            simd_make_float3(165, 165, 165), white_index);
    
    translate = translation4x4(130, 0, 65);
    rotate = rotation4x4(-0.1*M_PI, simd_make_float3(0, 1, 0));
    
    smaller.model_matrix = simd_mul(translate, rotate);
    smaller.inverse_matrix = simd_inverse(smaller.model_matrix);
    smaller.normal_matrix = simd_transpose(smaller.inverse_matrix);
    
    list.emplace_back(smaller);
}

void prepareCornellBox(std::vector<Square>& list, std::vector<Material>& materials) {
    
    Material light; light.type= MaterialType::Diffuse;
    light.textureInfo.albedo = float3(11);
    
    auto light_index = (uint32_t)materials.size();
    materials.emplace_back(light);
    
    Material red; red.type = MaterialType::Lambert;
    red.textureInfo.type = TextureType::Constant;
    red.textureInfo.albedo = { 0.65, 0.05, 0.05 };
    
    auto red_index = (uint32_t)materials.size();
    materials.emplace_back(red);
    
    Material green; green.type = MaterialType::Lambert;
    green.textureInfo.type = TextureType::Constant;
    green.textureInfo.albedo = {0.05, 0.65, 0.05};
    
    auto green_index = (uint32_t)materials.size();
    materials.emplace_back(green);
    
    Material white; white.type = MaterialType::Lambert;
    white.textureInfo.type = TextureType::Checker;
    white.textureInfo.albedo = 0.73;
    
    auto white_index = (uint32_t)materials.size();
    materials.emplace_back(white);
    
    auto lightSource = MakeSquare(0, float2{400, 555}, 2, float2{200, 355}, 1, 550);
    lightSource.material = light_index;

    auto right = MakeSquare(1, float2{0, 555}, 2, float2{0, 555}, 0, 800); //flip
    right.material = red_index;

    auto left = MakeSquare(1, float2{0, 555}, 2, float2{0, 555}, 0, -245);
    left.material = green_index;

    auto top = MakeSquare(0, float2{-245, 800}, 2, float2{0, 555}, 1, 555);
    top.material = white_index;

    auto bottom = MakeSquare(0, float2{-245, 800}, 2, float2{0, 555}, 1, 0);
    bottom.material = white_index;

    auto back =  MakeSquare(0, float2{-245, 800}, 1, float2{0, 555}, 2, 555);
    back.material = white_index;

    list.emplace_back(left);
    list.emplace_back(right);

    list.emplace_back(top);
    list.emplace_back(back);
    list.emplace_back(bottom);
    
    list.emplace_back(lightSource);
    
    auto little = MakeSquare(1, float2{200, 300}, 2, float2{200, 300}, 0, -200);
    little.material = light_index;
    list.emplace_back(little);
}

void prepareSphereList(std::vector<Sphere>& list, std::vector<Material>& materials) {
    
    Material glass; glass.type = MaterialType::Dielectric;
    glass.textureInfo.albedo = { 1.0, 1.0, 1.0 };
    glass.textureInfo.type = TextureType::Noise;
    glass.parameter = 1.5;
    
    auto glass_index = (uint32_t)materials.size();
    materials.emplace_back(glass);
    
    auto sphere = MakeSphere(64, float3{200, 250, 200});
    sphere.material = glass_index;
    list.emplace_back(sphere);
    
    Material specu; specu.type = MaterialType::Demofox;
    
    specu.textureInfo.albedo = { 0.9, 0.25, 0.25 };
    specu.textureInfo.type = TextureType::Constant;
    specu.specularProb = 0.02f;
    specu.specularRoughness = 0.0;
    specu.specularColor = { 1.0f, 1.0f, 1.0f };
    specu.parameter = 1.1f;
    specu.refractionProb = 1.0f;
    specu.refractionRoughness = 0.0;
    specu.refractionColor = { 0.0f, 0.5f, 1.0f };
    
    for(auto i : {0, 1, 2, 3, 4, 5} ) {
        
        sphere = MakeSphere(40, simd_make_float3(0 + 100 * (5-i), 50, 50));
        specu.specularRoughness = i * 0.2;
        specu.refractionRoughness = i * 0.2;
        
        auto m_index = (uint32_t)materials.size();
        materials.push_back(specu);
        sphere.material = m_index;
        
        list.emplace_back(sphere);
    }
    
    Material gloss; gloss.type = MaterialType::Demofox;
    
    gloss.textureInfo.albedo = simd_make_float3(1.0);
    gloss.textureInfo.type = TextureType::Constant;
    gloss.specularProb = 1.0f;
    gloss.specularRoughness = 0.0;
    gloss.specularColor = {0.3f, 1.0f, 0.3f};
    gloss.parameter = 1.1f;
    gloss.refractionProb = 0.0f;
    gloss.refractionRoughness = 0.0;
    gloss.refractionColor = {0.0f, 0.5f, 1.0f};
    
    for(auto i : {0, 1, 2, 3, 4} ) {
        
        sphere = MakeSphere(40, simd_make_float3(-10 + 150 * i, 500, 400));
        gloss.specularRoughness = fmax(FLT_MIN, i * 0.25);
        gloss.refractionRoughness = fmax(FLT_MIN, i * 0.25);
        
        auto m_index = (uint32_t)materials.size();
        materials.push_back(gloss);
        sphere.material = m_index;
        
        list.emplace_back(sphere);
    }
}

void prepareCamera(struct Camera* camera, float2 viewSize, float2 rotate) {
    
    auto aspect = viewSize.x/viewSize.y;
    
    auto lookFrom = simd_make_float3(278, 278, -800);
    auto lookAt = simd_make_float3(278, 278, 278);
    auto viewUp = simd_make_float3(0, 1, 0);
    
    auto dist_focus = 10;
    auto aperture = 0.01;
    
    auto vfov = 45 * (M_PI/180);
    auto hfov = 2 * atan(tan(vfov * 0.5) * aspect);
    
    let offset = simd_make_float4(lookFrom - lookAt, 0.0f);
    
    let rotH = rotation4x4(rotate.x * hfov * 10, viewUp);
    let rotV = rotation4x4(rotate.y * vfov * 10, simd_make_float3(1, 0, 0));
    
    lookFrom = lookAt + simd_mul(simd_mul(rotH, rotV), offset).xyz;
    
    MakeCamera(camera, lookFrom, lookAt, viewUp, aperture, aspect, vfov, dist_focus);
}
