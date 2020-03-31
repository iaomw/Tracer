import Foundation

class BVH: Hittable {
    
    let right: Hittable
    let left: Hittable
    let box: AABB
    
    init(_ list: [Hittable], _ timeS: Float, _ timeE: Float) throws {
        
        let axis = axisNames[Int(3*randomFloat())]
        
        let sortByAsis = { (array: [Hittable], para: AxisName) -> [Hittable] in
            return try array.sorted { (l, r) -> Bool in
                guard let lBox = l.boundingBox(0, 0),
                    let rBox = r.boundingBox(0, 0) else {
                        throw NSError()
                }
                return lBox.min[para] < rBox.min[para]
            }
        }
        
        guard let sortedArray: [Hittable] = {
            switch axis {
            case .x:
                return try? sortByAsis(list, AxisName.x)
            case .y:
                return try? sortByAsis(list, AxisName.y)
            case .z:
                return try? sortByAsis(list, AxisName.z)
            }
            } () else { throw NSError()}
        
        let length = list.count
        if length == 1 {
            right = list[0]
            left = list[0]
        } else if (length == 2) {
            right = list[1]
            left = list[0]
        } else {
            let mid = length/2
            left = try BVH(Array(sortedArray[0..<mid]), timeS, timeE)
            right = try BVH(Array(sortedArray[mid..<length]), timeS, timeE)
        }
        
        guard let lBox = left.boundingBox(timeS, timeE),
            let rBox = right.boundingBox(timeS, timeE) else { throw NSError() }
        
        box = surroundingBox(boxS: lBox, boxE: rBox)
    }
    
    func hitTest(ray: Ray, t_min: Float, t_max: Float) -> HitRecord? {
        
        if (box.hit(ray: ray, tmin: t_min, tmax: t_max)) {
            
            let leftHit = left.hitTest(ray: ray, t_min: t_min, t_max: t_max)
            let rightHit = right.hitTest(ray: ray, t_min: t_min, t_max: t_max)
            
            if (leftHit==nil && rightHit==nil) {return nil}
            if (leftHit==nil) {return rightHit}
            if (rightHit==nil) {return leftHit}
            
            return leftHit!.t < rightHit!.t ? leftHit : rightHit
        }
        
        return nil
    }
    
    func boundingBox(_ ts: Float, _ te: Float) -> AABB? {
        return box
    }
}
