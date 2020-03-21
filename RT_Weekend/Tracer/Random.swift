import Foundation

func randomFloat() -> Float {
    //return Float.random(in: 0.0..<1.0)
    Float(arc4random())/Float(UInt32.max)
}

func randomInUnitSphere() -> Vec3 {
    var p: Vec3
    
    repeat {
        p = 2.0 * Vec3(randomFloat(), randomFloat(), randomFloat()) - Vec3(1, 1, 1)
    } while (p.squreLength() >= 1.0)
    
    return p;
}

func randomInUnitDisk() -> Vec3 {
    var p: Vec3;
    
    repeat {
        p = 2.0 * Vec3(randomFloat(), randomFloat(), 0) - Vec3(1, 1, 0)
    } while (p.dot(p) >= 1.0)
    
    return p
}
