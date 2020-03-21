import Foundation

class Camera {
    
    let lookFrom: Vec3
    let lookAt: Vec3
    let viewUp: Vec3
    let vfov: Float
    
    let aspect: Float
    let aperture: Float
    let lenRadius: Float
    let focus_dist: Float
    
    let timeS: Float
    let timeE: Float
    
    let u: Vec3
    let v: Vec3
    let w: Vec3
    
    let vertical: Vec3
    let horizontal: Vec3
    
    let cornerLowLeft: Vec3
    
    init(lookFrom: Vec3,
         lookAt: Vec3,
         viewUp: Vec3,
         vfov: Float,
         aspect: Float,
         aperture: Float,
         focus_dist: Float,
         timeS: Float,
         timeE: Float) {
        
        self.lookFrom = lookFrom
        self.lookAt = lookAt
        self.viewUp = viewUp
        self.vfov = vfov
        
        self.aspect = aspect
        self.aperture = aperture
        self.focus_dist = focus_dist
        
        self.timeS = timeS
        self.timeE = timeE
        
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
    
    func cast(s: Float, t: Float) -> Ray {
        let rd = lenRadius*randomInUnitDisk()
        let offset = u*rd.x + v*rd.y
        let origin = lookFrom+offset
        let sample = cornerLowLeft+s*horizontal+t*vertical
        
        let time = timeS + randomFloat()*(timeE-timeS)
        return Ray(origin, sample-origin, time)
    }
}
