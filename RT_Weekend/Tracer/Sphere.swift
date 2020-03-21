import Foundation

public class Sphere: Hittable {
   
    public let center: Vec3
    public let radius: Float
    public let material: Material
    
    public init(center: Vec3, radius: Float, material: Material) {
        self.center = center
        self.radius = radius
        self.material = material
    }
    
    public func hitTest(ray: Ray, t_min: Float, t_max: Float) -> HitRecord? {
    
        let oc = ray.origin - self.center
        let a = ray.direction.dot(ray.direction)
        let b = oc.dot(ray.direction)
        let c = oc.dot(oc) - radius*radius
        let discriminant = b*b - a*c
        
        let hitRecord: HitRecord? = {
        
            if discriminant <= 0 { return nil }
            
            var tmp = (-b - sqrt(discriminant)) / a;
            if (tmp < t_max && tmp > t_min) {
                let point = ray.pointAtParameter(t: tmp)
                let record = HitRecord(t: tmp, point: point, normal: (point-self.center)/radius, material: self.material)
                return record
            }
            tmp = (-b + sqrt(discriminant)) / a;
            if (tmp < t_max && tmp > t_min) {
                let point = ray.pointAtParameter(t: tmp)
                let record = HitRecord(t: tmp, point: point, normal: (point-self.center)/radius, material: self.material)
                return record
            }
            return nil
        } ()
        
        return hitRecord
    }
       
}
