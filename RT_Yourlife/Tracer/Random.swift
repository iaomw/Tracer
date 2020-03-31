import Foundation

func randomFloat() -> Float {
    //return Float.random(in: 0.0..<1.0)
    Float(arc4random())/Float(UInt32.max)
}

func randomInUnitDisk() -> Vec3 {
    var p: Vec3;
    
    repeat {
        p = 2.0 * Vec3(randomFloat(), randomFloat(), 0) - Vec3(1, 1, 0)
    } while (p.dot(p) >= 1.0)
    
    return p
}

func randomInUnitSphere() -> Vec3 {
    var p: Vec3
    
    repeat {
        p = 2.0 * Vec3({randomFloat()}) - Vec3(1)
    } while (p.dot(p) >= 1.0)
    
    return p;
}

func randomCosineDirection() -> Vec3 {
    let r1 = randomFloat()
    let r2 = randomFloat()
    let z = sqrt(1-r2)
    let phi = 2*r1*Float.pi;
    let x = cos(phi)*sqrt(r2);
    let y = sin(phi)*sqrt(r2);
    return Vec3(x, y, z);
}

func randomToSphere(_ radius: Float, _ distanceSquared: Float) -> Vec3 {
    let r1 = randomFloat()
    let r2 = randomFloat()
    let z = 1 + r2*(sqrt(1-radius*radius/distanceSquared) - 1)
    let phi = 2*Float.pi*r1
    let x = cos(phi)*sqrt(1-z*z)
    let y = sin(phi)*sqrt(1-z*z)
    return Vec3(x, y, z)
}
