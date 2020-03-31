import Foundation

class ONB {
    
    private(set) var u = Vec3()
    private(set) var v = Vec3()
    private(set) var w = Vec3()
    
    init(_ n: Vec3) {
        buidlFromW(n)
    }
    
    func local(_ a: Float, _ b: Float, _ c: Float) -> Vec3 {
        a*u + b*v + c*w
    }
        
    func local(_ l: Vec3) -> Vec3 {
        l.x*u + l.y*v + l.z*w
    }
    
    func buidlFromW(_ n: Vec3) {
        w = n.normalize()
        
        let a = abs(w.x) > 0.9 ? Vec3(0,1,0):Vec3(1,0,0)
        
        v = w.cross(a).normalize()
        u = w.cross(v)
    }
}
