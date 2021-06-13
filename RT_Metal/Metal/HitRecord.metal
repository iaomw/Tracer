#include "HitRecord.hh"

bool emit(thread HitRecord& hitRecord, thread float3& color, constant Material* materials) {
    
    switch(materials[hitRecord.material].type) {
        case MaterialType::Diffuse: {
            color = materials[hitRecord.material].textureInfo.albedo;
            return true;
        }
        default:{
            return false;
        }
    }
}
