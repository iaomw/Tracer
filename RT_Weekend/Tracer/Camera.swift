import Foundation

public class Camera {
    
    public let lookFrom: Vec3
    public let lookAt: Vec3
    public let viewUp: Vec3
    public let vfov: Float
    
    public let aspect: Float
    public let aperture: Float
    public let lenRadius: Float
    public let focus_dist: Float
    
    public let u: Vec3
    public let v: Vec3
    public let w: Vec3
    
    public let vertical: Vec3
    public let horizontal: Vec3
    
    public let cornerLowLeft: Vec3

    public init(lookFrom: Vec3, lookAt: Vec3, viewUp: Vec3, vfov: Float, aspect: Float, aperture: Float, focus_dist: Float) {
        
        self.lookFrom = lookFrom
        self.lookAt = lookAt
        self.viewUp = viewUp
        self.vfov = vfov
        
        self.aspect = aspect
        self.aperture = aperture
        self.focus_dist = focus_dist
        
        self.lenRadius = aperture / 2
        let theta = vfov * Float.pi / 180
        
        let halfHeight = tan(theta/2)
        let halfWidth = aspect * halfHeight
        
        w = (lookFrom - lookAt).normalize()
        u = viewUp.cross(w).normalize()
        v = w.cross(u)
        
        cornerLowLeft = lookFrom - halfWidth*focus_dist*u - halfHeight*focus_dist*v - focus_dist*w
        
        vertical = 2*halfHeight*focus_dist*v
        horizontal = 2*halfWidth*focus_dist*u
    }
    
    public func cast(s: Float, t: Float) -> Ray {
        let rd = lenRadius*randomInUnitDisk()
        let offset = u*rd.x + v*rd.y
        let origin = lookFrom+offset
        let sample = cornerLowLeft+s*horizontal+t*vertical
        
        //let time = timeS + randomFloat()*(timeE-timeS)
        
        return Ray(origin, sample-origin)
    }
}
