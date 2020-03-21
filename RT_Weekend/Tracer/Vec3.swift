import Foundation

public class Vec3 {
    
    public let x: Float
    public let y: Float
    public let z: Float
    
    public var r: Float {x}
    public var g: Float {y}
    public var b: Float {z}
    
    public init() {
        x = 0; y = 0; z = 0;
    }
    
    public init(_ v: Float) {
        x = v; y = v; z = v;
    }
    
    public init(_ x: Float, _ y: Float, _ z: Float) {
        self.x = x;
        self.y = y;
        self.z = z;
    }
    
    public func length() -> Float {
         sqrt(squreLength());
    }
    
    public func squreLength() -> Float {
         pow(x,2) + pow(y,2) + pow(z,2);
    }
    
    public func normalize() -> Vec3 {
        self / length()
    }
    
    public static func + (lhs: Vec3, rhs: Vec3) -> Vec3 {
        Vec3(lhs.x+rhs.x, lhs.y+rhs.y, lhs.z+rhs.z);
    }

    public static func - (lhs: Vec3, rhs: Vec3) -> Vec3 {
        Vec3(lhs.x-rhs.x, lhs.y-rhs.y, lhs.z-rhs.z);
    }

    public static func * (lhs: Vec3, rhs: Vec3) -> Vec3 {
        Vec3(lhs.x*rhs.x, lhs.y*rhs.y, lhs.z*rhs.z);
    }

    public static func / (lhs: Vec3, rhs: Vec3) -> Vec3 {
        Vec3(lhs.x/rhs.x, lhs.y/rhs.y, lhs.z/rhs.z);
    }
    
    public static func += (lhs: inout Vec3, rhs: Vec3) {
        lhs = Vec3(lhs.x+rhs.x, lhs.y+rhs.y, lhs.z+rhs.z);
    }

    public static func -= (lhs: inout Vec3, rhs: Vec3) {
        lhs = Vec3(lhs.x-rhs.x, lhs.y-rhs.y, lhs.z-rhs.z);
    }

    public static func *= (lhs: inout Vec3, rhs: Vec3) {
        lhs = Vec3(lhs.x*rhs.x, lhs.y*rhs.y, lhs.z*rhs.z);
    }

    public static func /= (lhs: inout Vec3, rhs: Vec3) {
        lhs = Vec3(lhs.x/rhs.x, lhs.y/rhs.y, lhs.z/rhs.z);
    }
    
    public static func * (lhs: Vec3, rhs: Float) -> Vec3 {
        Vec3(lhs.x*rhs, lhs.y*rhs, lhs.z*rhs);
    }
    
    public static func * (lhs: Float, rhs: Vec3) -> Vec3 {
        Vec3(lhs*rhs.x, lhs*rhs.y, lhs*rhs.z);
    }

    public static func / (lhs: Vec3, rhs: Float) -> Vec3 {
        Vec3(lhs.x/rhs, lhs.y/rhs, lhs.z/rhs);
    }
    
    public func dot(_ v: Vec3) -> Float {
        x*v.x + y*v.y + z*v.z
    }
    
    public func cross(_ v: Vec3) -> Vec3 {
        Vec3(y*v.z - z*v.y, z*v.x-x*v.z, x*v.y-y*v.x)
    }
}

