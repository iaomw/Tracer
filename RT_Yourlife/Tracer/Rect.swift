import Foundation

enum RectType {
    case XY, YZ, ZX
}

class Rect: Hittable {
    let material: Material
    
    let axisA: AxisName
    let axisB: AxisName
    let axisC: AxisName
    
    let _SA: Float
    let _EA: Float
    let _SB: Float
    let _EB: Float
    
    let k: Float
    
    init(_ axisA: AxisName, _ SA: Float, _ EA: Float,
         _ axisB: AxisName, _ SB: Float, _ EB: Float,
         _ k: Float, _ material: Material) {
        
        self.material = material
        
        self.axisA = axisA
        self.axisB = axisB
        
        var axisSet: Set = Set(axisNames)
        axisSet.remove(axisA)
        axisSet.remove(axisB)
        self.axisC = axisSet.first!
        
        _SA = SA
        _EA = EA
        _SB = SB
        _EB = EB
        
        self.k = k
    }
    
    func hitTest(ray: Ray, t_min: Float, t_max: Float) -> HitRecord? {
        
        let t = (k-ray.origin[axisC]) / ray.direction[axisC]
        
        if (t<t_min || t>t_max) { return nil }
        
        let _a = ray.origin[axisA] + t*ray.direction[axisA]
        let _b = ray.origin[axisB] + t*ray.direction[axisB]
        
        if (_a<_SA || _a>_EA || _b<_SB || _b>_EB) { return nil }
        
        let point = ray.pointAtParameter(t: t)
        let u = (_a-_SA)/(_EA-_SA)
        let v = (_b-_SB)/(_EB-_SB)
        
        return HitRecord(t: t, p: point, n: Vec3( [axisC: 1]), m: material, u: u, v: v, r: ray)
    }
    
    func boundingBox(_ ts: Float, _ te: Float) -> AABB? {
        
        let dictS: [AxisName: Float] = [axisA: _SA, axisB: _SB, axisC: k-0.0001]
        let dictE: [AxisName: Float] = [axisA: _EA, axisB: _EB, axisC: k+0.0001]
        
        let box = AABB(a: Vec3(dictS), b: Vec3(dictE))
        return box
    }
    
    func valuePDF(_ o: Vec3, _ v: Vec3) -> Float {
        guard let hitRecord = self.hitTest(ray: Ray(o, v, 0),
                                           t_min: 0.001,
                                           t_max: Float.greatestFiniteMagnitude) else {return 0}
        
        let area = (_EA-_SA)*(_EB-_SB)
        let distanceSquared = hitRecord.t * hitRecord.t * v.squreLength()
        let cosine = abs(v.dot(hitRecord.n))/v.length()
        return distanceSquared/(cosine*area);
    }
    
    func random(_ o: Vec3) -> Vec3 {
        
        let dict = [axisA: _SA+randomFloat()*(_EA-_SA),
                    axisB: _SB+randomFloat()*(_EB-_SB),
                    axisC: k]
        
        let randomPoint = Vec3(dict)
        return randomPoint - o;
    }
}
