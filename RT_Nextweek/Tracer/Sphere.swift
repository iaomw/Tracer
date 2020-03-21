import Foundation

class Sphere: Hittable {
    
    let center: Vec3
    let radius: Float
    let material: Material
    
    init(center: Vec3, radius: Float, material: Material) {
        self.center = center
        self.radius = radius
        self.material = material
    }
    
    func hitTest(ray: Ray, t_min: Float, t_max: Float) -> HitRecord? {
        
        let oc = ray.origin - self.center
        let a = ray.direction.dot(ray.direction)
        let b = oc.dot(ray.direction)
        let c = oc.dot(oc) - radius*radius
        let discriminant = b*b - a*c
        
        if discriminant <= 0 { return nil }
        
        var tmp = (-b - sqrt(discriminant)) / a;
        if (tmp < t_max && tmp > t_min) {
            let point = ray.pointAtParameter(t: tmp)
            let offset = (point - self.center).normalize()
            let (u, v) = Sphere.sphereUV(p: offset)
            let record = HitRecord(t: tmp, p: point, n: (point-self.center)/radius, m: self.material, u: u, v: v)
            return record
        }
        tmp = (-b + sqrt(discriminant)) / a;
        if (tmp < t_max && tmp > t_min) {
            let point = ray.pointAtParameter(t: tmp)
            let offset = (point - self.center).normalize()
            let (u, v) = Sphere.sphereUV(p: offset)
            let record = HitRecord(t: tmp, p: point, n: (point-self.center)/radius, m: self.material, u: u, v:v)
            return record
        }
        return nil
    }
    
    func boundingBox(_ ts: Float, _ te: Float) -> AABB? {
        return AABB(a: center-Vec3(radius), b: center+Vec3(radius))
    }
    
    static func sphereUV(p: Vec3) -> (Float, Float) {
        let phi = atan2(p.z, p.x)
        let theta = asin(p.y)
        
        let u = 1-(phi+Float.pi)/(2*Float.pi)
        let v = (theta+Float.pi/2)/Float.pi
        
        return (u, v)
    }
}
