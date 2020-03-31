import Foundation

struct HitRecord {
    let t: Float
    let p: Vec3
    let n: Vec3
    let m: Material
    
    let u: Float
    let v: Float
    
    var front: Bool
    
    init(t: Float, p: Vec3, n: Vec3, m: Material, u: Float, v: Float, r: Ray?) {
        self.t = t
        self.p = p
        self.m = m
        self.u = u
        self.v = v
    
        if let ray = r {
            if ray.direction.dot(n) < 0 {
                self.front = true
                self.n = n
            } else {
                self.front = false
                self.n = -n
            }
        } else {
            self.front = true
            self.n = n
        }
    }
}

protocol Hittable {
    func hitTest(ray: Ray, t_min: Float, t_max: Float) -> HitRecord?
    func boundingBox(_ ts: Float, _ te: Float) -> AABB?
    func valuePDF(_ o: Vec3, _ v: Vec3) -> Float
    func random(_ o: Vec3) -> Vec3
}

extension Hittable {
    
    func valuePDF(_ o: Vec3, _ v: Vec3) -> Float {
        return 0.0
    }
    
    func random(_ o: Vec3) -> Vec3 {
        return Vec3(1, 0, 0)
    }
}

class HittableList: Hittable {
    
    let list: [Hittable]
    
    init(list: [Hittable]) {
        self.list = list
    }
    
    func hitTest(ray: Ray, t_min: Float, t_max: Float) -> HitRecord? {
        
        var hitRecord: HitRecord?
        var closest_so_far = t_max
        
        for sample in list {
            if let record = sample.hitTest(ray: ray, t_min: t_min, t_max: closest_so_far) {
                hitRecord = record
                closest_so_far = record.t
            }
        }
        
        return hitRecord
    }
    
    func boundingBox(_ ts: Float, _ te: Float) -> AABB? {
        if list.count < 1 { return nil }
        
        guard let firstBox = list.first?.boundingBox(ts, te) else {return nil}
        
        var result = firstBox
        
        for i in 1..<list.count {
            if let thisBox = list[i].boundingBox(ts, te) {
                result = surroundingBox(boxS: result, boxE: thisBox)
            } else {return nil}
        }
        
        return result
    }
    
    func valuePDF(_ o: Vec3, _ v: Vec3) -> Float {
        let weight = Float(1.0)/Float(list.count)
        
        return list.reduce(Float(0)) { (p, h) -> Float in
            p + weight*h.valuePDF(o, v)
        }
    }
    
    func random(_ o: Vec3) -> Vec3 {
        let index = Int(randomFloat()*Float(list.count-1))
        return list[index].random(o)
    }
}

class NormalFlipped: Hittable {
    let hittable: Hittable
    
    init(_ h: Hittable) {
        self.hittable = h
    }
    
    func hitTest(ray: Ray, t_min: Float, t_max: Float) -> HitRecord? {
        if var r = hittable.hitTest(ray: ray, t_min: t_min, t_max: t_max) {
            r.front = !r.front
            return r //HitRecord(t: r.t, p: r.p, n: -r.n, m: r.m, u: r.u, v: r.v, r: ray)
        }
        return nil
    }
    
    func boundingBox(_ ts: Float, _ te: Float) -> AABB? {
        return hittable.boundingBox(ts, te)
    }
}

class Translate: Hittable {
    let hittable: Hittable
    let offset: Vec3
    
    init(_ hittable: Hittable, _ offset: Vec3) {
        self.hittable = hittable
        self.offset = offset
    }
    
    func hitTest(ray: Ray, t_min: Float, t_max: Float) -> HitRecord? {
        let rayOffsetted = Ray(ray.origin-offset, ray.direction, ray.time)
        if let r = hittable.hitTest(ray: rayOffsetted, t_min: t_min, t_max: t_max) {
            return HitRecord(t: r.t, p: r.p+offset, n: r.n, m: r.m, u: r.u, v: r.v, r: rayOffsetted)
        }
        return nil
    }
    
    func boundingBox(_ ts: Float, _ te: Float) -> AABB? {
        if let box = hittable.boundingBox(ts, te) {
            return AABB(a: box.min+offset, b: box.max+offset)
        }
        return nil
    }
}

class Rotate: Hittable {
    let hittable: Hittable
    let axis: AxisName
    let angle: Float
    let box: AABB
    
    let sinTheta: Float
    let cosTheta: Float
    
    init(_ hittable: Hittable, _ axis: AxisName,  _ angle: Float) {
        self.hittable = hittable
        self.angle = angle
        self.axis = axis
        
        let radians = (Float.pi/180)*angle
        
        sinTheta = sin(radians)
        cosTheta = cos(radians)
        
        let box = hittable.boundingBox(0, 1) ?? AABB(a: Vec3(), b: Vec3())
        
        var mini = Vec3(Float.greatestFiniteMagnitude)
        var maxi = Vec3(-Float.greatestFiniteMagnitude)
        
        for i in 0...1 {
            for j in 0...1 {
                for k in 0...1 {
                    let x = Float(i)*box.max.x + Float(1-i)*box.min.x
                    let y = Float(j)*box.max.y + Float(1-j)*box.min.y
                    let z = Float(k)*box.max.z + Float(1-k)*box.min.z
                    
                    var (newX, newY, newZ) = (x, y, z)
                    
                    switch axis {
                    case .x: do {
                        //newX = x
                        newY = cosTheta*y - sinTheta*z
                        newZ = sinTheta*y + cosTheta*z
                        }
                    case .y: do {
                        newX = cosTheta*x + sinTheta*z
                        //newY = y
                        newZ = -sinTheta*x + cosTheta*z
                        }
                    case .z: do {
                        newX = cosTheta*x - sinTheta*y
                        newY = sinTheta*x + cosTheta*y
                        //newZ = z
                        }
                    }
                    
                    let dict = [AxisName.x: newX, AxisName.y: newY, AxisName.z: newZ]
                    let tester = Vec3(dict)
                    
                    for c in [AxisName.x, AxisName.y, AxisName.z] {
                        if tester[c] > maxi[c] {
                            maxi = maxi.update(c, tester[c])
                        }
                        if tester[c] < mini[c] {
                            mini = mini.update(c, tester[c])
                        }
                    }
                }
            }
        }
        
        self.box = AABB(a: mini, b: maxi)
    }
    
    func hitTest(ray: Ray, t_min: Float, t_max: Float) -> HitRecord? {
        var origin = ray.origin
        var direction = ray.direction
        
        switch axis {
        case .x: do {
            let y = cosTheta*ray.origin.y + sinTheta*ray.origin.z
            let z = -sinTheta*ray.origin.y + cosTheta*ray.origin.z
            
            origin = Vec3(ray.origin.x, y, z)
            
            let dy = cosTheta*ray.direction.y + sinTheta*ray.direction.z
            let dz = -sinTheta*ray.direction.y + cosTheta*ray.direction.z
            
            direction = Vec3(ray.direction.x, dy, dz)
            }
        case .y: do {
            let x = cosTheta*ray.origin.x - sinTheta*ray.origin.z
            let z = sinTheta*ray.origin.x + cosTheta*ray.origin.z
            
            origin = Vec3(x, ray.origin.y, z)
            
            let dx = cosTheta*ray.direction.x - sinTheta*ray.direction.z
            let dz = sinTheta*ray.direction.x + cosTheta*ray.direction.z
            
            direction = Vec3(dx, ray.direction.y, dz)
            }
        case .z: do {
            let x = cosTheta*ray.origin.x + sinTheta*ray.origin.y
            let y = -sinTheta*ray.origin.x + cosTheta*ray.origin.y
            
            origin = Vec3(x, y, ray.origin.z)
            
            let dx = cosTheta*ray.direction.x + sinTheta*ray.direction.y
            let dy = -sinTheta*ray.direction.x + cosTheta*ray.direction.y
            
            direction = Vec3(dx, dy, ray.direction.z)
            }
        }
        
        let rayRotated = Ray(origin, direction, ray.time)
        
        guard let record = hittable.hitTest(ray: rayRotated, t_min: t_min, t_max: t_max) else {return nil}
        
        let point = record.p
        let normal = record.n
        
        var pointRotated = point
        var normalRotated = normal
        
        switch axis {
        case .x: do {
            let py = cosTheta*point.y - sinTheta*point.z
            let pz = sinTheta*point.y + cosTheta*point.z
            
            pointRotated = Vec3(point.x, py, pz)
            
            let ny = cosTheta*normal.y - sinTheta*normal.z
            let nz = -sinTheta*normal.y + cosTheta*normal.z
            
            normalRotated = Vec3(normal.x, ny, nz)
            }
        case .y: do {
            let px = cosTheta*point.x + sinTheta*point.z
            let pz = -sinTheta*point.x + cosTheta*point.z
            
            pointRotated = Vec3(px, point.y, pz)
            
            let nx = cosTheta*normal.x + sinTheta*normal.z
            let nz = -sinTheta*normal.x + cosTheta*normal.z
            
            normalRotated = Vec3(nx, normal.y, nz)
            }
        case .z: do {
            let px = cosTheta*point.x - sinTheta*point.y
            let py = sinTheta*point.x + cosTheta*point.y
            
            pointRotated = Vec3(px, py, point.z)
            
            let nx = cosTheta*normal.x - sinTheta*normal.y
            let ny = -sinTheta*normal.x + cosTheta*normal.y
            
            normalRotated = Vec3(nx, ny, normal.z)
            }
        }
        
        return HitRecord(t: record.t,
                         p: pointRotated,
                         n: normalRotated,
                         m: record.m,
                         u: record.u,
                         v: record.v,
                         r: rayRotated)
    }
    
    func boundingBox(_ ts: Float, _ te: Float) -> AABB? {
        return box
    }
}

class ConstantMedium: Hittable {
    let hittable: Hittable
    let texture: Texture
    let density: Float
    
    let phaseFunction: Material
    
    init(_ h: Hittable, _ t: Texture, _ d: Float) {
        self.hittable = h
        self.texture = t
        self.density = d
        
        phaseFunction = Isotropic(t)
    }
    
    func hitTest(ray: Ray, t_min: Float, t_max: Float) -> HitRecord? {
        
        var rs, re: HitRecord
        let FloatMAX = Float.greatestFiniteMagnitude
        
        guard let recordS = hittable.hitTest(ray: ray, t_min: -FloatMAX, t_max: FloatMAX) else {return nil}
        rs = recordS
        guard let recordE = hittable.hitTest(ray: ray, t_min: recordS.t+0.0001, t_max: FloatMAX) else {return nil}
        re = recordE
        if recordS.t < t_min {
            //recordS.t = t_min
            rs = HitRecord(t: t_min,
                           p: recordS.p,
                           n: recordS.n,
                           m: recordS.m,
                           u: recordS.u,
                           v: recordS.v,
                           r: nil)
        }
        if recordE.t > t_max {
            //recordE.t = t_max
            re = HitRecord(t: t_max,
                           p: recordE.p,
                           n: recordE.n,
                           m: recordE.m,
                           u: recordE.u,
                           v: recordE.v,
                           r: nil)
        }
        if rs.t >= re.t { return nil }
        if (rs.t < 0) {
            //recordE.t = 0
            re = HitRecord(t:0,
                           p: recordE.p,
                           n: recordE.n,
                           m: recordE.m,
                           u: recordE.u,
                           v: recordE.v,
                           r: nil)
        }
        let distanceInside = (re.t - rs.t) * ray.direction.length()
        let hitDistance = -(1/density) * log(randomFloat())
        
        if hitDistance < distanceInside {
            let new_t = rs.t + hitDistance/ray.direction.length()
            let new_p = ray.pointAtParameter(t: new_t)
            let newRecord = HitRecord(t: new_t,
                                      p: new_p,
                                      n: Vec3(1,0,0),
                                      m: phaseFunction,
                                      u: 0, v: 0, r: nil)
            
            return newRecord
        }
        
        return nil
    }
    
    func boundingBox(_ ts: Float, _ te: Float) -> AABB? {
        return hittable.boundingBox(ts, te)
    }
}
