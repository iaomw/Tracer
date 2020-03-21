import Foundation
import simd

protocol Material {
    func scatter(ray: Ray, hitRecord: HitRecord) -> (Ray, Vec3)?
    func emitted(u: Float, v: Float, p: Vec3) -> Vec3
}

extension Material {
    func emitted(u: Float, v: Float, p: Vec3) -> Vec3 {
        return Vec3()
    }
}

func emitted(u: Float, v: Float, p: Vec3) -> Vec3 {
    return Vec3()
}

func schlick(cosine: Float, ref_idx: Float) -> Float {
    
    let r0 = (1-ref_idx) / (1+ref_idx)
    let R0 = r0 * r0
    return R0 + (1-R0) * pow((1-cosine), 5)
}

func reflect(v: Vec3, n: Vec3) -> Vec3 {
    Vec3(simd_reflect(v.fff, n.fff))
    //return v - 2*v.dot(n)*n
}

func refract(v: Vec3, n: Vec3, ni_over_nt: Float) -> Vec3? {
    
    //let fff = simd_refract(v.fff, n.fff, ni_over_nt)
    //return simd_equal(fff, simd_float3(repeating: 0.0)) ? nil:Vec3(fff)
    
    let i = v.normalize()
    let idn = i.dot(n)

    let discriminant = 1 - ni_over_nt*ni_over_nt*(1-idn*idn)

    if discriminant > 0 {
        let refracted = ni_over_nt * (i-n*idn) - n * sqrt(discriminant)
        return refracted
    } else {
        return nil
    }
}

class Lambertian : Material {
    let albedo: Texture
    
    init(_ t: Texture) {
        self.albedo = t
    }
    
    func scatter(ray: Ray, hitRecord: HitRecord) -> (Ray, Vec3)? {
        let target = hitRecord.p + hitRecord.n + randomInUnitSphere()
        let scattered = Ray(hitRecord.p, target-hitRecord.p, ray.time)
        
        let (u, v) = (hitRecord.u, hitRecord.v)
        
        return (scattered, albedo.value(u, v, hitRecord.p))
    }
}

class Metal: Material {
    
    let albedo: Vec3
    let fuzz: Float
    
    init(albedo: Vec3, fuzz: Float) {
        self.albedo = albedo
        self.fuzz = min(fuzz, 1.0)
    }
    
    func scatter(ray: Ray, hitRecord: HitRecord) -> (Ray, Vec3)? {
        let reflected = reflect(v: ray.direction.normalize(), n: hitRecord.n)
        let scattered = Ray(hitRecord.p, reflected + fuzz*randomInUnitSphere(), ray.time)
        if scattered.direction.dot(hitRecord.n) > 0 {
            return (scattered, albedo)
        }
        return nil
    }
}

class DiffuseLight: Material {
    
    let texture: Texture
    
    init(_ texture: Texture) {
        self.texture = texture
    }
    
    func scatter(ray: Ray, hitRecord: HitRecord) -> (Ray, Vec3)? {
        return nil
    }
    
    func emitted(u: Float, v: Float, p: Vec3) -> Vec3 {
        return texture.value(u, v, p)
    }
}

class Isotropic: Material {
    let texture: Texture
    
    init(_ t: Texture) {
        self.texture = t
    }
    
    func scatter(ray: Ray, hitRecord: HitRecord) -> (Ray, Vec3)? {
        
        let scatter = Ray(hitRecord.p, randomInUnitSphere(), 0)
        let attenuation = texture.value(hitRecord.u, hitRecord.v, hitRecord.p)
        return (scatter, attenuation)
    }
}

class Dielectric : Material {
    let ref_idx: Float
    
    init(_ v: Float) {
        self.ref_idx = v
    }
    
    func scatter(ray: Ray, hitRecord: HitRecord) -> (Ray, Vec3)? {
        
        let reflected = reflect(v: ray.direction, n: hitRecord.n)
        
        let (outNormal, ni_over_nt, cosine):(Vec3, Float, Float) = {
            
            let idn = ray.direction.dot(hitRecord.n)
            let tmp = idn / ray.direction.length()
            
            if idn > 0 {
                let oNormal = -hitRecord.n
                let ref = ref_idx
                let cos = sqrt(1 - ref_idx * ref_idx * (1-tmp*tmp))
                //let cos = ref_idx * tmp
                return (oNormal, ref, cos)
                
            } else {
                
                let oNormal = hitRecord.n
                let ref = 1.0 / ref_idx
                let cos = -tmp
                return (oNormal, ref, cos)
            }
        } ()
        
        let attenuation = Vec3(1.0);
        
        let (refracted, reflect_prob):(Vec3?, Float) = {
            
            if let refract = refract(v: ray.direction, n: outNormal, ni_over_nt: ni_over_nt) {
                let prob = schlick(cosine: cosine, ref_idx: ref_idx)
                return (refract, prob)
            } else {
                return (nil, 1.0)
            }
        } ()
        
        if randomFloat() < reflect_prob {
            return (Ray(hitRecord.p, reflected, ray.time), attenuation);
        } else {
            if let test = refracted {
                return (Ray(hitRecord.p, test, ray.time), attenuation);
            }
            return nil
        }
    }
}
