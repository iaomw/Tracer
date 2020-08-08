#include "HitRecord.hh"

bool emit(thread HitRecord& hitRecord, thread float3& color) {
    
    switch(hitRecord.material.type) {
        case MaterialType::Diffuse: {
            color = hitRecord.material.textureInfo.albedo;
            return true;
        }
        case MaterialType::Metal:
        case MaterialType::Lambert:
        case MaterialType::Isotropic:
        case MaterialType::Dielectric:
        default:{
            return false;
        }
    }
}
