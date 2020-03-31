import Foundation

class Box: Hittable {
    let material: Material
    let list: HittableList
    
    let ps: Vec3
    let pe: Vec3
    
    init(_ ps: Vec3, _ pe: Vec3, _ material: Material) {
        
        self.material = material
        self.ps = ps
        self.pe = pe
        
        self.list = HittableList(list: [
            Rect(.x, ps.x, pe.x, .y, ps.y, pe.y, pe.z, material),
            NormalFlipped(Rect(.x, ps.x, pe.x, .y, ps.y, pe.y, ps.z, material)),
            Rect(.x, ps.x, pe.x, .z, ps.z, pe.z, pe.y, material),
            NormalFlipped(Rect(.x, ps.x, pe.x, .z, ps.z, pe.z, ps.y, material)),
            Rect(.y, ps.y, pe.y, .z, ps.z, pe.z, pe.x, material),
            NormalFlipped(Rect(.y, ps.y, pe.y, .z, ps.z, pe.z, ps.x, material))
        ])
    }
    
    func hitTest(ray: Ray, t_min: Float, t_max: Float) -> HitRecord? {
        return list.hitTest(ray: ray, t_min: t_min, t_max: t_max)
    }
    
    func boundingBox(_ ts: Float, _ te: Float) -> AABB? {
        return AABB(a: ps, b: pe)
    }
}
