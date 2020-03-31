
import Foundation

protocol Texture {
    func value(_ u: Float, _ v: Float, _ p: Vec3) -> Vec3
}

class ConstantTexture: Texture {
    let color: Vec3
    
    init(_ color: Vec3) {
        self.color = color
    }
    
    func value(_ u: Float, _ v: Float, _ p: Vec3) -> Vec3 {
        return color
    }
}

class CheckerTexture: Texture {
    let texOdd: Texture
    let texEven: Texture
    
    init(_ textureOdd: Texture, _ textureEven: Texture) {
        texOdd = textureOdd
        texEven = textureEven
    }
    
    func value(_ u: Float, _ v: Float, _ p: Vec3) -> Vec3 {
        let sines = sin(10*p.x)*sin(10*p.y)*sin(10*p.z)
        return sines < 0 ? texOdd.value(u, v, p) : texEven.value(u, v, p)
    }
}

class NoiseTexture: Texture {
    let perlin: Perlin = Perlin()
    let scale: Float
    
    init(_ scale: Float = 1.0) {
        self.scale = scale
    }
    
    func value(_ u: Float, _ v: Float, _ p: Vec3) -> Vec3 {
        return Vec3(1) * 0.5 * (1 + sin(scale*p.z + 5*perlin.turb(scale*p)))
    }
}

class ImageTexture: Texture {
    
    let data: [[Vec3]]
    
    let nx: Int
    let ny: Int
    
    init(_ pixelMap: [[Vec3]]) {
        self.data = pixelMap
        self.ny = pixelMap.count
        self.nx = pixelMap.first?.count ?? 0
    }
    
    func value(_ u: Float, _ v: Float, _ p: Vec3) -> Vec3 {
        var i = Int( (  u) * Float(nx) )
        var j = Int( (1-v) * Float(ny) - 0.001)
        
        if i<0 { i=0 }
        if j<0 { j=0 }
        
        if i > (nx-1) { i = nx - 1 }
        if j > (ny-1) { j = ny - 1 }
        
        let rgb = data[j][i]
        
        return rgb
    }
}
