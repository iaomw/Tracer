import Foundation
import simd

func trilinearInterp(c: [[[Vec3]]], u: Float, v: Float, w: Float) -> Float {
    
    let fff = simd_float3(u, v, w)
    let ttt = fff*fff*(3-2*fff)
    let uu = ttt.x
    let vv = ttt.y
    let ww = ttt.z
    
//    let uu: Float = u*u*(3-2*u)
//    let vv: Float = v*v*(3-2*v)
//    let ww: Float = w*w*(3-2*w)
    var accum = Float(0.0)
    
    for i in 0...1 {
        let fi = Float(i)
        for j in 0...1 {
            let fj = Float(j)
            for k in 0...1 {
                let fk = Float(k)
                let weight = Vec3(u-fi, v-fj, w-fk)
                accum += (fi*uu + (1-fi)*(1-uu)) *
                    (fj*vv + (1-fj)*(1-vv)) *
                    (fk*ww + (1-fk)*(1-ww)) * (c[i][j][k].dot(weight))
            }
        }
    }
    return accum
}

class Perlin {
    
    lazy var ran: [Vec3] = { generatePerlin() } ()
    lazy var permX: [Int] = { generatePerlinPerm() }()
    lazy var permY: [Int] = { generatePerlinPerm() }()
    lazy var permZ: [Int] = { generatePerlinPerm() }()
    
    func noise(_ p: Vec3) -> Float {
        
        let u = p.x - floor(p.x)
        let v = p.y - floor(p.y)
        let w = p.z - floor(p.z)
        
        //        u = u*u*(3-2*u)
        //        v = v*v*(3-2*v)
        //        w = w*w*(3-2*w)
        
        let i = Int(floor(p.x))
        let j = Int(floor(p.y))
        let k = Int(floor(p.z))
        
        var c = [[[Vec3]]](repeating:
            [[Vec3]](repeating:
                [Vec3](repeating: Vec3(), count: 2),
                     count: 2), count:2)
        
        for di in 0...1 {
            for dj in 0...1 {
                for dk in 0...1 {
                    c[di][dj][dk] = ran[permX[(i+di)&255] ^ permY[(j+dj)&255] ^ permZ[(k+dk)&255]]
                }
            }
        }
        
        return trilinearInterp(c: c, u: u, v: v, w: w)
    }
    
    func generatePerlin() -> [Vec3] {
        return (0...255).map { (index) -> Vec3 in
            let x = 2*randomFloat() - 1
            let y = 2*randomFloat() - 1
            let z = 2*randomFloat() - 1
            return Vec3(x, y, z).normalize()
        }
    }
    
    func permute(p: [Int]) -> [Int] {
        var tmp = p
        for i in (1...(p.count-1)).reversed() {
            let target = Int(randomFloat()*Float(i+1))
            (tmp[i], tmp[target]) = (tmp[target], tmp[i])
        }
        return tmp
    }
    
    func generatePerlinPerm() -> [Int] {
        let p = Array(0...255)
        let perm = permute(p: p)
        return perm
    }
    
    func turb(_ p: Vec3, _ depth: Int = 7) -> Float {
        var accum = Float(0)
        var temp = p
        var weight = Float(1)
        for _ in 0..<depth {
            accum += weight*noise(temp)
            weight *= 0.5
            temp = temp*2
        }
        return abs(accum)
    }
}
