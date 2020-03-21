import Foundation

public class Ray
{
    public let origin: Vec3
    public let direction: Vec3
    
    public init(_ origin: Vec3, _ direction: Vec3) {
        self.origin = origin
        self.direction = direction
    }
    
    public func pointAtParameter(t: Float) -> Vec3 {
        origin + direction * t
    }
};
