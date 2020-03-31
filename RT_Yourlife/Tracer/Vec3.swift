import Foundation
import simd

enum AxisName {
    case x, y, z
}

let axisNames = [AxisName.x, AxisName.y, AxisName.z]

class Vec3 {
    let fff: simd_float3
    
    var x: Float {fff.x}
    var y: Float {fff.y}
    var z: Float {fff.z}
    
    var r: Float {fff.x}
    var g: Float {fff.y}
    var b: Float {fff.z}
    
    init() {
        fff = simd_float3(0, 0, 0)
    }
    
    init(_ v: Float) {
        fff = simd_float3(v, v, v)
    }
    
    init(_ fff: simd_float3) {
        self.fff = fff
    }
    
    init(_ block: (()->Float)) {
        fff = simd_float3(block(), block(), block())
    }
    
    init(_ dict: [AxisName: Float]) {
        let nx = dict[.x] ?? 0
        let ny = dict[.y] ?? 0
        let nz = dict[.z] ?? 0
        fff = simd_float3(nx, ny, nz)
    }
    
    init(_ x: Float, _ y: Float, _ z: Float) {
        fff = simd_float3(x, y, z)
    }
    
    init(_ x: Int, _ y: Int, _ z: Int) {
        fff = simd_float3(Float(x), Float(y), Float(z))
    }
    
    func length() -> Float {
        simd_length(fff)
    }
    
    func squreLength() -> Float {
        simd_length_squared(fff)
    }
    
    func normalize() -> Vec3 {
        Vec3(simd_normalize(fff))
    }
    
    func update(_ vname: AxisName, _ value: Float) -> Vec3 {
        var dict = [AxisName.x: x, AxisName.y: y, AxisName.z: z]
        dict[vname] = value
        return Vec3(dict)
    }
    
    static prefix func - (this: Vec3) -> Vec3 {
        Vec3(-this.fff)
    }
    
    static func + (lhs: Vec3, rhs: Vec3) -> Vec3 {
        Vec3(lhs.fff+rhs.fff);
    }
    
    static func - (lhs: Vec3, rhs: Vec3) -> Vec3 {
        Vec3(lhs.fff-rhs.fff);
    }
    
    static func * (lhs: Vec3, rhs: Vec3) -> Vec3 {
        Vec3(lhs.fff*rhs.fff);
    }
    
    static func / (lhs: Vec3, rhs: Vec3) -> Vec3 {
        Vec3(lhs.fff/rhs.fff);
    }
    
    static func += (lhs: inout Vec3, rhs: Vec3) {
        lhs = Vec3(lhs.fff+rhs.fff);
    }
    
    static func -= (lhs: inout Vec3, rhs: Vec3) {
        lhs = Vec3(lhs.fff-rhs.fff);
    }
    
    static func *= (lhs: inout Vec3, rhs: Vec3) {
        lhs = Vec3(lhs.fff*rhs.fff);
    }
    
    static func /= (lhs: inout Vec3, rhs: Vec3) {
        lhs = Vec3(lhs.fff/rhs.fff);
    }
    
    static func * (lhs: Vec3, rhs: Float) -> Vec3 {
        Vec3(lhs.fff*rhs);
    }
    
    static func * (lhs: Float, rhs: Vec3) -> Vec3 {
        Vec3(lhs*rhs.fff);
    }
    
    static func / (lhs: Vec3, rhs: Float) -> Vec3 {
        Vec3(lhs.fff/rhs);
    }
    
    static func / (lhs: Float, rhs: Vec3) -> Vec3 {
        Vec3(lhs/rhs.fff);
    }
    
    func dot(_ v: Vec3) -> Float {
        simd_dot(self.fff, v.fff)
    }
    
    func cross(_ v: Vec3) -> Vec3 {
        Vec3(simd_cross(self.fff, v.fff))
    }
    
    subscript(index: AxisName) -> Float {
        get {
            switch index {
            case .x:
                return x
            case .y:
                return y
            case .z:
                return z
            }
        }
    }
}

class VecTest <T: FloatingPoint & SIMDScalar> {
    let fff: SIMD3<T>
    
    init(_ v: T) {
        self.fff = SIMD3<T>(v, v, v)
    }
    
    init(_ fff: SIMD3<T>) {
        self.fff = fff
    }
    
//    convenience init() {
//        self.init(0.0)
//    }
    
    init(_ block: (()->T)) {
        fff = SIMD3<T>(block(), block(), block())
    }
    
    init(_ dict: [AxisName: T]) {
        
        let nx = dict[.x] ?? T(0)
        let ny = dict[.y] ?? T(0)
        let nz = dict[.z] ?? T(0)
        fff = SIMD3<T>(nx, ny, nz)
    }
    
    init(_ x: T, _ y: T, _ z: T) {
        fff = SIMD3<T>(x, y, z)
    }
    
    func length() -> T {
        switch fff {
        case let vvv as simd_float3:
            do {
                return simd_length(vvv) as! T
            }
        case let vvv as simd_double3:
            do {
                return simd_length(vvv) as! T
            }
        default:
            do {return T(0)}
        }
    }
    
    func normalize() -> VecTest<T> {
        
        switch fff {
        case let vvv as simd_float3 where fff is simd_float3:
            do {
                return VecTest(simd_normalize(vvv) as! SIMD3<T>)
            }
        case let vvv as simd_double3 where fff is simd_float3:
            do {
                return VecTest(simd_normalize(vvv) as! SIMD3<T>)
            }
        default:
            do {return VecTest(0)}
        }
    }
}
