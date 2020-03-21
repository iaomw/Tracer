import Foundation

class Ray
{
    let time: Float
    let origin: Vec3
    let direction: Vec3
    
    init(_ origin: Vec3, _ direction: Vec3, _ time: Float) {
        self.origin = origin
        self.direction = direction
        
        self.time = time
    }
    
    func pointAtParameter(t: Float) -> Vec3 {
        origin + direction * t
    }
};
