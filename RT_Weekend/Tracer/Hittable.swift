import Foundation

public struct HitRecord {
    let t: Float
    let point: Vec3
    let normal: Vec3
    let material: Material
}

public protocol Hittable {
    func hitTest(ray: Ray, t_min: Float, t_max: Float) -> HitRecord?
}

public class HittableList: Hittable {
    
    public let list: [Hittable]
    
    public init(list: [Hittable]) {
        self.list = list
    }
    
    public func hitTest(ray: Ray, t_min: Float, t_max: Float) -> HitRecord? {
        
        var hitRecord: HitRecord?
        var closest_so_far = t_max
            
            for sample in list {
                if let record = sample.hitTest(ray: ray, t_min: t_min, t_max: closest_so_far) {
                    hitRecord = record
                    closest_so_far = record.t
                }
            }
        
        return hitRecord
    }
}
