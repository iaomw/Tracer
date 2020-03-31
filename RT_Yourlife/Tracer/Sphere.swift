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
        let root = sqrt(discriminant)
        var tmp = (-b - root) / a;
        if (tmp < t_max && tmp > t_min) {
            let point = ray.pointAtParameter(t: tmp)
            let offset = (point - self.center)/radius
            let (u, v) = Sphere.sphereUV(p: offset)
            let record = HitRecord(t: tmp, p: point, n: offset, m: self.material,
                                   u: u, v: v, r: ray)
            return record
        }
        tmp = (-b + root) / a;
        if (tmp < t_max && tmp > t_min) {
            let point = ray.pointAtParameter(t: tmp)
            let offset = (point - self.center)/radius
            let (u, v) = Sphere.sphereUV(p: offset)
            let record = HitRecord(t: tmp, p: point, n: offset, m: self.material,
                                   u: u, v:v, r: ray)
            return record
        }
        return nil
    }
    
    func boundingBox(_ ts: Float, _ te: Float) -> AABB? {
        return AABB(a: center-Vec3(radius), b: center+Vec3(radius))
    }
    
    func valuePDF(_ o: Vec3, _ v: Vec3) -> Float {
        
        if let _ = hitTest(ray: Ray(o, v), t_min: 0.001, t_max: Float.greatestFiniteMagnitude) {
            let cosThetaMax = sqrt(1 - radius*radius/(center-o).squreLength())
            let solidAngle = 2*Float.pi*(1-cosThetaMax)
            return 1.0/solidAngle
        }
        
        return 0.0
    }
    
    func random(_ o: Vec3) -> Vec3 {
        let direction = center - o;
        let distanceSquared = direction.squreLength()
        
        let uvw = ONB(direction)
        return uvw.local(randomToSphere(radius, distanceSquared))
    }
    
    static func sphereUV(p: Vec3) -> (Float, Float) {
        let phi = atan2(p.z, p.x)
        let theta = asin(p.y)
        
        let u = 1-(phi+Float.pi)/(2*Float.pi)
        let v = (theta+Float.pi/2)/Float.pi
        
        return (u, v)
    }
}
