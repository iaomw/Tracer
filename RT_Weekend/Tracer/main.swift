import Foundation

func color(ray: Ray, world: Hittable, depth: Int) -> Vec3 {
    guard let hitRecord = world.hitTest(ray: ray, t_min: 0.001, t_max: Float.greatestFiniteMagnitude) else {
        let directon = ray.direction.normalize()
        let t = 0.5*(directon.y + 1.0)
        return (1.0-t)*Vec3(1.0) + t*Vec3(0.5, 0.7, 1.0)
    }
    
    if depth < 50 {
        if let (scattered, attenuation) = hitRecord.material.scatter(ray: ray, hitRecord: hitRecord) {
            return attenuation*color(ray: scattered, world: world, depth: depth+1)
        }
    }
        
    return Vec3()
}

func randomScene() -> HittableList {
    
    var list:[Hittable] = [
        Sphere(center: Vec3(0, -1000, 0), radius: 1000, material: Lambertian(v: Vec3(0.5))),
        Sphere(center: Vec3(0, 1, 0), radius: 1.0, material: Dielectric(v: 1.5)),
        Sphere(center: Vec3(-4, 1, 0), radius: 1.0, material: Lambertian(v: Vec3(0.4, 0.2, 0.1))),
        Sphere(center: Vec3(4, 1, 0), radius: 1.0, material: Metal(albedo: Vec3(0.7, 0.6, 0.5), fuzz: 0.0))
    ]
    
    for a in -11...11 {
        for b in -11...11 {
            let mat: Float = randomFloat()
            let center = Vec3(Float(a)+0.9*randomFloat(), 0.2, Float(b)+0.9*randomFloat())
            if (center-Vec3(4, 0.2, 0)).length() > 0.9 {
                if mat < 0.8 {
                    list.append(
                        Sphere(center: center, radius: 0.2,
                               material: Lambertian(v: Vec3(
                                            randomFloat()*randomFloat(),
                                            randomFloat()*randomFloat(),
                                            randomFloat()*randomFloat()
                    ))))
                } else if mat < 0.95 {
                    list.append(
                        Sphere(center: center, radius: 0.2,
                               material: Metal(albedo: Vec3(
                                            0.5*(1+randomFloat()),
                                            0.5*(1+randomFloat()),
                                            0.5*(1+randomFloat())
                    ), fuzz: 0.5*(1+randomFloat()))))
                } else {
                    list.append(Sphere(center: center, radius: 0.2, material: Dielectric(v: 1.5)))
                }
            }
        }
    }
    
    return HittableList(list: list)
}

func main() {
    let nx = 200
    let ny = 100
    let ns = 8
    
    var image = [[String]](repeating: [String](repeating:"0 0 0\n", count: ny), count: nx)
    var result = "P3\n\(nx) \(ny)\n255\n"
    
    let world = randomScene()
    
    let lookFrom = Vec3(13, 2, 3)
    let lookAt = Vec3()
    
    let distToFocus: Float = 10.0
    let aperture: Float = 0.1
    
    let cam = Camera(lookFrom: lookFrom, lookAt: lookAt, viewUp: Vec3(0, 1, 0), vfov: 20, aspect: Float(nx)/Float(ny), aperture: aperture, focus_dist: distToFocus)
    
    let processorCount = ProcessInfo().processorCount
    let dataUnit = ny/processorCount
    let remain = ny%processorCount
    
    let queue = DispatchQueue(label: "array")
    DispatchQueue.concurrentPerform(iterations: processorCount) { (index) in
        
        var upper = dataUnit
        if index == (processorCount-1) && remain != 0 {
            upper += remain
        }
        
        for value in 0..<upper {
            let j = value + index * dataUnit
            
            for i in 0..<nx {
                var col = Vec3()
                for _ in 0..<ns {
                    let u = (Float(i) + randomFloat())/Float(nx)
                    let v = (Float(j) + randomFloat())/Float(ny)
                    let ray = cam.cast(s: u, t: v)
                    col += color(ray: ray, world: world, depth: 0)
                }
                col = col/Float(ns)
                col = Vec3(sqrt(col.x), sqrt(col.y), sqrt(col.z))
                
                let ir = Int(255.99*col.r)
                let ig = Int(255.99*col.g)
                let ib = Int(255.99*col.b)
                
                let ss = "\(ir) \(ig) \(ib)\n"
                
                queue.sync {
                    image[i][j] = ss
                }
            }
        }
    }
    
    let file = "tmp.ppm" //this is the file. we will write to and read from it
    
    for j in 0..<ny {
        let k = ny-1-j
        for i in 0..<nx {
            result.append(image[i][k])
        }
    }

    let dir = FileManager.default.currentDirectoryPath
    let fileURL = URL(fileURLWithPath: dir, isDirectory: true).appendingPathComponent(file)

        //writing
        do {
            try result.write(to: fileURL, atomically: false, encoding: .utf8)
        }
        catch let error {
            print(error)
        }
}

main();
