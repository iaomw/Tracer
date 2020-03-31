import Foundation
import simd

struct ScatterRecord {
    let isSpecular: Bool
    let specularRay: Ray?
    let attenuation: Vec3
    let pdf: PDF?
}

protocol Material {
    func scatter(ray: Ray, hitRecord: HitRecord) -> ScatterRecord?
    func scatterPDF(ray: Ray, hitRecord: HitRecord, scattered: Ray) -> Float
    func emitted(ray: Ray, record: HitRecord, u: Float, v: Float, p: Vec3) -> Vec3
}

extension Material {
    
    func scatter(ray: Ray, hitRecord: HitRecord) -> ScatterRecord? {
        return nil
    }

    func scatterPDF(ray: Ray, hitRecord: HitRecord, scattered: Ray) -> Float {
        return 0
    }

    func emitted(ray: Ray, record: HitRecord, u: Float, v: Float, p: Vec3) -> Vec3 {
        return Vec3()
    }
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
    
    func scatterPDF(ray: Ray, hitRecord: HitRecord, scattered: Ray) -> Float {
        let cosine = hitRecord.n.dot(scattered.direction.normalize())
        if cosine < 0 {return 0}
        return cosine/Float.pi
    }
    
    func scatter(ray: Ray, hitRecord: HitRecord) -> ScatterRecord? {
        
        let (u, v, p) = (hitRecord.u, hitRecord.v, hitRecord.p)
        let attenuation = albedo.value(u, v, p)
        let pdf = CosinePDF(hitRecord.n)
        
        return ScatterRecord(isSpecular: false, specularRay: nil, attenuation: attenuation, pdf: pdf)
    }
}

class Metal: Material {
    
    let albedo: Vec3
    let fuzz: Float
    
    init(albedo: Vec3, fuzz: Float) {
        self.albedo = albedo
        self.fuzz = min(fuzz, 1.0)
    }
    
    func scatter(ray: Ray, hitRecord: HitRecord) -> ScatterRecord? {
        
        let reflected = reflect(v: ray.direction.normalize(), n: hitRecord.n)
        let newRay = Ray(hitRecord.p, reflected+fuzz*randomInUnitSphere(), 0)
        let scatterRecord = ScatterRecord(isSpecular: true, specularRay: newRay, attenuation: albedo, pdf: nil)
        
        return scatterRecord
    }
}

class Isotropic: Material {
    let texture: Texture
    
    init(_ t: Texture) {
        self.texture = t
    }
    
    func scatter(ray: Ray, hitRecord: HitRecord) -> ScatterRecord? {
        
        let scatter = Ray(hitRecord.p, randomInUnitSphere(), 0)
        let attenuation = texture.value(hitRecord.u, hitRecord.v, hitRecord.p)
        
        return ScatterRecord(isSpecular: true, specularRay: scatter, attenuation: attenuation, pdf: nil)
        //return (scatter, attenuation, 0)
    }
}

class Dielectric : Material {
    let ref_idx: Float
    
    init(_ v: Float) {
        self.ref_idx = v
    }
    
    func scatter(ray: Ray, hitRecord: HitRecord) -> ScatterRecord? {
        
        let etai_over_etat = hitRecord.front ? (1.0/ref_idx):(ref_idx)
        let unit_dir = ray.direction.normalize()
        
        let cos_theta = min(-unit_dir.dot(hitRecord.n), 1.0)
        let sin_theta = sqrt(1.0 - cos_theta*cos_theta)
        
        if etai_over_etat*sin_theta > 1.0 {
            let reflected = reflect(v: unit_dir, n: hitRecord.n)
            let theRay = Ray(hitRecord.p, reflected, ray.time)
            return ScatterRecord(isSpecular: true, specularRay: theRay, attenuation: Vec3(1.0), pdf: nil)
        }
        
        let reflect_prob = schlick(cosine: cos_theta, ref_idx: etai_over_etat)
        
        if randomFloat() < reflect_prob {
            let reflected = reflect(v: unit_dir, n: hitRecord.n)
            let theRay = Ray(hitRecord.p, reflected, ray.time)
            return ScatterRecord(isSpecular: true, specularRay: theRay, attenuation: Vec3(1.0), pdf: nil)
        }
        
        if let refracted = refract(v: unit_dir, n: hitRecord.n, ni_over_nt: etai_over_etat) {
            let theRay = Ray(hitRecord.p, refracted, ray.time)
            return ScatterRecord(isSpecular: true, specularRay: theRay, attenuation: Vec3(1.0), pdf: nil)
        }
        return nil
    }
}

class DiffuseLight: Material {
    
    let texture: Texture
    
    init(_ texture: Texture) {
        self.texture = texture
    }
    
    func emitted(ray: Ray, record: HitRecord, u: Float, v: Float, p: Vec3) -> Vec3 {
        
        if record.front {
            return texture.value(u, v, p)
        }
        return Vec3()
        
//        if record.n.dot(ray.direction) < 0.0 {
//            return texture.value(u, v, p)
//        }
//        return Vec3()
    }
}
