import Foundation

class AABB  {
    let min: Vec3
    let max: Vec3
    
    init(a: Vec3, b: Vec3) {
        self.min = a
        self.max = b
    }
    
    func tih(ray: Ray, tmin: Float, tmax: Float) -> Bool {
        
        for i in axisNames {
            let minBounding = (min[i] - ray.origin[i])/ray.direction[i]
            let maxBounding = (max[i] - ray.origin[i])/ray.direction[i]
            
            let (t0, t1): (Float, Float) = minBounding < maxBounding ? (minBounding, maxBounding):(maxBounding, minBounding)
            
            let tmin = Swift.max(t0, tmin)
            let tmax = Swift.min(t1, tmax)
            if tmax <= tmin {return false}
        }
        return true
    }
    
    func hit(ray: Ray, tmin: Float, tmax: Float) -> Bool {
        
        for i in axisNames {
            let minBounding = (min[i] - ray.origin[i])/ray.direction[i]
            let maxBounding = (max[i] - ray.origin[i])/ray.direction[i]
            
            let (t0, t1) = ray.direction[i] < 0.0 ? (maxBounding, minBounding) : (minBounding, maxBounding)
            
            let tmin = Swift.max(t0, tmin)
            let tmax = Swift.min(t1, tmax)
            if tmax <= tmin {return false}
        }
        return true
    }
}

func surroundingBox(boxS: AABB, boxE: AABB) -> AABB {
    let small = Vec3(min(boxS.min.x, boxE.min.x), min(boxS.min.y, boxE.min.y), min(boxS.min.z, boxE.min.z))
    let big = Vec3(max(boxS.max.x, boxE.max.x), max(boxS.max.y, boxE.max.y), max(boxS.max.z, boxE.max.z))
    
    return AABB(a: small, b: big)
}

