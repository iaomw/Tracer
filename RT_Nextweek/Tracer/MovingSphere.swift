import Foundation

class MovingSphere : Hittable {
    
    let centerS: Vec3
    let centerE: Vec3
    
    let timeS: Float
    let timeE: Float
    
    let radius: Float
    let material: Material
    
    init(centerS: Vec3, centerE: Vec3, timeS: Float, timeE: Float, radius: Float, material: Material) {
        
        self.centerS = centerS
        self.centerE = centerE
        
        self.timeS = timeS
        self.timeE = timeE
        
        self.radius = radius
        self.material = material
    }
    
    func center(time: Float) -> Vec3 {
        
        return centerS + ((time-timeS)/(timeE-timeS))*(centerE-centerS)
    }
    
    func hitTest(ray: Ray, t_min: Float, t_max: Float) -> HitRecord? {
        
        let theCenter = center(time: ray.time)
        let oc = ray.origin - theCenter
        let a = ray.direction.dot(ray.direction)
        let b = oc.dot(ray.direction)
        let c = oc.dot(oc) - radius*radius
        let discriminant = b*b - a*c
        
        if discriminant <= 0 { return nil }
        
        var tmp = (-b - sqrt(discriminant)) / a;
        if (tmp < t_max && tmp > t_min) {
            let point = ray.pointAtParameter(t: tmp)
            let offset = (point - theCenter).normalize()
            let (u, v) = Sphere.sphereUV(p: offset)
            let record = HitRecord(t: tmp, p: point, n: (point-theCenter)/radius, m: self.material, u: u, v: v)
            return record
        }
        tmp = (-b + sqrt(discriminant)) / a;
        if (tmp < t_max && tmp > t_min) {
            let point = ray.pointAtParameter(t: tmp)
            let offset = (point - theCenter).normalize()
            let (u, v) = Sphere.sphereUV(p: offset)
            let record = HitRecord(t: tmp, p: point, n: (point-theCenter)/radius, m: self.material, u: u, v: v)
            return record
        }
        return nil
    }
    
    func boundingBox(_ ts: Float, _ te: Float) -> AABB? {
        
        let boxS = AABB(a: center(time: timeS)-Vec3(radius), b: center(time: timeS)+Vec3(radius))
        let boxE = AABB(a: center(time: timeE)-Vec3(radius), b: center(time: timeE)+Vec3(radius))
        
        return surroundingBox(boxS: boxS, boxE: boxE)
    }
}
