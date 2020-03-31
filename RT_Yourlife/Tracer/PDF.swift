import Foundation

protocol PDF {
    func value(_ direction: Vec3) -> Float
    func generate() -> Vec3
}

class CosinePDF: PDF {
    let uvw: ONB
    
    init(_ w: Vec3) {
        uvw = ONB(w)
    }
    
    func value(_ direction: Vec3) -> Float {
        let cosine = direction.normalize().dot(uvw.w)
        
        if cosine > 0 {
            return cosine/Float.pi
        } else {
            return 0
        }
    }
    
    func generate() -> Vec3 {
        uvw.local(randomCosineDirection())
    }
}

class HittablePDF: PDF {
    let hittable: Hittable
    let origin: Vec3
    
    init(_ hittable: Hittable, _ origin: Vec3) {
        self.hittable = hittable
        self.origin = origin
    }
    
    func value(_ direction: Vec3) -> Float {
        return hittable.valuePDF(origin, direction)
    }
    
    func generate() -> Vec3 {
        return hittable.random(origin)
    }
}

class MixturePDF: PDF {
    let a: PDF
    let b: PDF
    
    init(_ a: PDF, _ b: PDF) {
        self.a = a
        self.b = b
    }
    
    func value(_ direction: Vec3) -> Float {
        0.5*a.value(direction) + 0.5*b.value(direction)
    }
    
    func generate() -> Vec3 {
        randomFloat() < 0.5 ? a.generate():b.generate()
    }
}
