import Foundation

public protocol Material {
    func scatter(ray: Ray, hitRecord: HitRecord) -> (Ray, Vec3)?
}

func schlick(cosine: Float, ref_idx: Float) -> Float {
    let r0 = (1-ref_idx) / (1+ref_idx)
    let R0 = r0 * r0
    return R0 + (1-R0) * pow((1-cosine), 5)
}

func reflect(v: Vec3, n: Vec3) -> Vec3 {
    return v - 2*v.dot(n)*n
}

func refract(v: Vec3, n: Vec3, ni_over_nt: Float) -> Vec3? {
    
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

public class Lambertian : Material {
    public let albedo: Vec3
    
    public init(v: Vec3) {
        self.albedo = v
    }
    
    public func scatter(ray: Ray, hitRecord: HitRecord) -> (Ray, Vec3)? {
        let target = hitRecord.point + hitRecord.normal + randomInUnitSphere()
        let scattered = Ray(hitRecord.point, target-hitRecord.point)
        return (scattered, albedo)
    }
}

public class Metal: Material {
    
    public let albedo: Vec3
    public let fuzz: Float
    
    public init(albedo: Vec3, fuzz: Float) {
        self.albedo = albedo
        self.fuzz = min(fuzz, 1.0)
    }
    
    public func scatter(ray: Ray, hitRecord: HitRecord) -> (Ray, Vec3)? {
        let reflected = reflect(v: ray.direction.normalize(), n: hitRecord.normal)
        let scattered = Ray(hitRecord.point, reflected + fuzz*randomInUnitSphere())
        if scattered.direction.dot(hitRecord.normal) > 0 {
            return (scattered, albedo)
        }
        return nil
    }
}

public class Dielectric : Material {
    public let ref_idx: Float
    
    public init(v: Float) {
        self.ref_idx = v
    }
    
    public func scatter(ray: Ray, hitRecord: HitRecord) -> (Ray, Vec3)? {
        
        let reflected = reflect(v: ray.direction, n: hitRecord.normal)
        
        let (outNormal, ni_over_nt, cosine):(Vec3, Float, Float) = {
            
            let idn = ray.direction.dot(hitRecord.normal)
            
            if idn > 0 {
                let oNormal = Vec3() - hitRecord.normal
                let ref = ref_idx
                let tmp = idn / ray.direction.length()
                let cos = sqrt(1 - ref_idx * ref_idx * (1-tmp*tmp))
                return (oNormal, ref, cos)
                
            } else {
                
                let oNormal = hitRecord.normal
                let ref = 1.0 / ref_idx
                let cos = -idn/ray.direction.length()
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
            return (Ray(hitRecord.point, reflected), attenuation);
        } else {
            if let test = refracted {
                return (Ray(hitRecord.point, test), attenuation);
            }
            return nil
        }
    }
}
