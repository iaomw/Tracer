import Foundation
import AppKit
import Accelerate
import simd

typealias SceneComplex = (HittableList, ((Float) -> Camera), ((Ray, Vec3, Hittable, Hittable, Int) -> Vec3))

class Render {
    
    lazy var sceneDict: [String: (()->SceneComplex?)] = [
        "Motion Blur": randomScene,
        "Solid Texture": twoSpheres,
        "Perlin Noise": twoPerlinSpheres,
        "Image Texture": celestial,
        "Rectangles and Lights": simpleLight,
        "Cornell Box": cornellBox,
        "Cornell Smoke": cornellSmoke,
        "Cornell Glass": cornellGlass,
        "All New Features": finalScene ]
    
    private var sceneCache = [String: SceneComplex?]()
    
    func cachedScene(key: String) -> SceneComplex? {
        if let scene = sceneCache[key] {
            return scene
        }
        
        if let scene = sceneDict[key]?() {
            sceneCache[key] = scene
            return scene
        }
        return nil
    }
    
    lazy var camera0: ((Float) -> Camera) = {
        let lookFrom = Vec3(13,2,3)
        let lookAt = Vec3(0,0,0)
        
        let distToFocus: Float = 10.0
        let aperture: Float = 0.0
        let vfov: Float = 20.0
        
        let cameraCallback = { (aspect: Float) -> Camera in
            
            let camera = Camera(lookFrom: lookFrom,
                                lookAt: lookAt,
                                viewUp: Vec3(0, 1, 0),
                                vfov: vfov,
                                aspect: aspect,
                                aperture: aperture,
                                focus_dist: distToFocus,
                                timeS: 0.0,
                                timeE: 1.0)
            return camera
        }
        return cameraCallback
    } ()
    
    lazy var camera1: ((Float) -> Camera) = {
        let lookFrom = Vec3(278, 278, -800)
        let lookAt = Vec3(278, 278, 0)
        
        let distToFocus: Float = 10.0
        let aperture: Float = 0.0
        let vfov: Float = 40.0
        
        let cameraCallback = { (aspect: Float) -> Camera in
            
            let camera = Camera(lookFrom: lookFrom,
                                lookAt: lookAt,
                                viewUp: Vec3(0, 1, 0),
                                vfov: vfov,
                                aspect: aspect,
                                aperture: aperture,
                                focus_dist: distToFocus,
                                timeS: 0.0,
                                timeE: 1.0)
            return camera
        }
        return cameraCallback
    } ()
    
    lazy var randomScene: (()->SceneComplex?) = {
        
        let checkTexture = CheckerTexture(ConstantTexture(Vec3(0.2, 0.3, 0.1)), ConstantTexture(Vec3(0.9)))
        
        var list:[Hittable] = [
            Sphere(center: Vec3(0, -1000, 0), radius: 1000, material: Lambertian(checkTexture)),
            Sphere(center: Vec3(0, 1, 0), radius: 1.0, material: Dielectric(1.5)),
            Sphere(center: Vec3(-4, 1, 0), radius: 1.0, material: Lambertian(ConstantTexture(Vec3(0.4, 0.2, 0.1)))),
            Sphere(center: Vec3(4, 1, 0), radius: 1.0, material: Metal(albedo: Vec3(0.7, 0.6, 0.5), fuzz: 0.0))
        ]
        
        for a in -10...10 {
            for b in -10...10 {
                let mat: Float = randomFloat()
                let center = Vec3(Float(a)+0.9*randomFloat(), 0.2, Float(b)+0.9*randomFloat())
                if (center-Vec3(4, 0.2, 0)).length() > 0.9 {
                    if mat < 0.8 {
                        list.append(
                            MovingSphere(centerS: center,
                                         centerE: center+Vec3(0, 0.5*randomFloat(), 0),
                                         timeS: 0.0,
                                         timeE: 1.0,
                                         radius: 0.2,
                                         material: Lambertian(ConstantTexture(Vec3(
                                            randomFloat()*randomFloat(),
                                            randomFloat()*randomFloat(),
                                            randomFloat()*randomFloat())))))
                    } else if mat < 0.95 {
                        list.append(
                            Sphere(center: center, radius: 0.2,
                                   material: Metal(albedo: Vec3(
                                    0.5*(1+randomFloat()),
                                    0.5*(1+randomFloat()),
                                    0.5*(1+randomFloat())
                                   ), fuzz: 0.5*(1+randomFloat()))))
                    } else {
                        list.append(Sphere(center: center, radius: 0.2, material: Dielectric(1.5)))
                    }
                }
            }
        }
        
        guard let treeBVH = try? BVH(list, 0, 1) else {
            return nil
        }
        
        return (HittableList(list: [treeBVH]), self.camera0, self.color1)
    }
    
    lazy var twoSpheres: (()->SceneComplex?) = {
        let checkerTexture = CheckerTexture(ConstantTexture(Vec3(0.2, 0.3, 0.1)), ConstantTexture(Vec3(0.9)))
        let list = [
            Sphere(center: Vec3(0, -10, 0), radius: 10, material: Lambertian(checkerTexture)),
            Sphere(center: Vec3(0, 10, 0), radius: 10, material: Lambertian(checkerTexture))]
        
        return (HittableList(list: list), self.camera0, self.color1)
    }
    
    lazy var twoPerlinSpheres: (()->SceneComplex?) = {
        let noiseTexture = NoiseTexture()
        let list = [
            Sphere(center: Vec3(0, -1000, 0), radius: 1000, material: Lambertian(noiseTexture)),
            Sphere(center: Vec3(0, 2, 0), radius: 2, material: Lambertian(noiseTexture))]
        
        return (HittableList(list: list), self.camera0, self.color1)
    }

    lazy var celestial: (()->SceneComplex?) = {
        
        guard let pixelMap = Image.gerPixelMap(imageName: "2k_moon.jpg") else {return nil}
        let material = Lambertian(ImageTexture(pixelMap))
        let list = [Sphere(center: Vec3(0), radius: 2, material: material)]
        
        return (HittableList(list: list), self.camera0, self.color1)
    }

    lazy var simpleLight: (()->SceneComplex?) = {
        let pertext = NoiseTexture(4)
        
        let list:[Hittable] = [
            Sphere(center: Vec3(0,-1000,0), radius: 1000, material: Lambertian(pertext)),
            Sphere(center: Vec3(0,2,0), radius: 2, material: Lambertian(pertext)),
            Sphere(center: Vec3(0,7,0), radius: 2, material: DiffuseLight(ConstantTexture(Vec3(4)))),
            Rect(.x, 3, 5, .y, 1, 3, -2, DiffuseLight(ConstantTexture(Vec3(4))))]
        
        return (HittableList(list: list), self.camera0, self.color1)
    }

    lazy var cornellBox: (()->SceneComplex?) = {
        
        let red = Lambertian(ConstantTexture(Vec3(0.65, 0.05, 0.05)))
        let white = Lambertian(ConstantTexture(Vec3(0.73)))
        let green = Lambertian(ConstantTexture(Vec3(0.12, 0.45, 0.15)))
        
        let light = DiffuseLight(ConstantTexture(Vec3(15)))
        let aluminum = Metal(albedo: Vec3(0.8, 0.85, 0.88), fuzz: 0.0)
        let glass = Dielectric(1.5)
        
        let list: [Hittable] = [
            NormalFlipped(Rect(.y, 0, 555, .z, 0, 555, 555, green)),
            Rect(.y, 0, 555, .z, 0, 555, 0, red),
            NormalFlipped(Rect(.x, 213, 343, .z, 227, 332, 554, light)),
            NormalFlipped(Rect(.x, 0, 555, .z, 0, 555, 555, white)),
            Rect(.x, 0, 555, .z, 0, 555, 0, white),
            NormalFlipped(Rect(.x, 0, 555, .y, 0, 555, 555, white)),
            //Box(ps: Vec3(130, 0, 65), pe: Vec3(295, 165, 230), material: white),
            //Box(ps: Vec3(265, 0, 295), pe: Vec3(430, 330, 460), material: white),
            
            //Translate(Rotate(Box(Vec3(), Vec3(165), white), .y, -18), Vec3(130, 0, 65)),
            Translate(Rotate(Box(Vec3(), Vec3(165, 330, 165), white), .y, 15), Vec3(265, 0, 295)),
            Sphere(center: Vec3(190, 90, 190), radius: 90, material: glass)]
        
        return (HittableList(list: list), self.camera1, self.color1)
    }

    lazy var cornellSmoke: (()->SceneComplex?) = {
        
        let red = Lambertian(ConstantTexture(Vec3(0.65, 0.05, 0.05)))
        let white = Lambertian(ConstantTexture(Vec3(0.73)))
        let green = Lambertian(ConstantTexture(Vec3(0.12, 0.45, 0.15)))
        let light = DiffuseLight(ConstantTexture(Vec3(15)))
        
        let b1 = Translate(Rotate(Box(Vec3(), Vec3(165), white), .y, -18), Vec3(130, 0, 65))
        let b2 = Translate(Rotate(Box(Vec3(), Vec3(165, 330, 165), white), .y, 15), Vec3(265, 0, 295))
        
        let list: [Hittable] = [
            NormalFlipped(Rect(.y, 0, 555, .z, 0, 555, 555, green)),
            Rect(.y, 0, 555, .z, 0, 555, 0, red),
            Rect(.x, 113, 443, .z, 127, 432, 554, light),
            NormalFlipped(Rect(.x, 0, 555, .z, 0, 555, 555, white)),
            Rect(.x, 0, 555, .z, 0, 555, 0, white),
            NormalFlipped(Rect(.x, 0, 555, .y, 0, 555, 555, white)),
            //Box(ps: Vec3(130, 0, 65), pe: Vec3(295, 165, 230), material: white),
            //Box(ps: Vec3(265, 0, 295), pe: Vec3(430, 330, 460), material: white),
            ConstantMedium(b1, ConstantTexture(Vec3(1.0)), 0.01),
            ConstantMedium(b2, ConstantTexture(Vec3()), 0.01)
        ]
        
        return (HittableList(list: list), self.camera1, self.color1)
    }
 
    lazy var cornellGlass: (()->SceneComplex?) = {
        
        var resultList = [Hittable]()
        
        guard let pixelMap = Image.gerPixelMap(imageName: "2k_sun.jpg") else {return nil}
        
        let material = Lambertian(ImageTexture(pixelMap))
        resultList.append(Sphere(center: Vec3(400, 200, 400), radius: 80, material: material))
        
        let red = Lambertian(ConstantTexture(Vec3(0.65, 0.05, 0.05)))
        let white = Lambertian(ConstantTexture(Vec3(0.73)))
        let green = Lambertian(ConstantTexture(Vec3(0.12, 0.45, 0.15)))
        let light = DiffuseLight(ConstantTexture(Vec3(15)))
        
        let list: [Hittable] = [
            NormalFlipped(Rect(.y, 0, 555, .z, 0, 555, 555, green)),
            Rect(.y, 0, 555, .z, 0, 555, 0, red),
            Rect(.x, 113, 443, .z, 127, 432, 554, light),
            NormalFlipped(Rect(.x, 0, 555, .z, 0, 555, 555, white)),
            Rect(.x, 0, 555, .z, 0, 555, 0, white),
            NormalFlipped(Rect(.x, 0, 555, .y, 0, 555, 555, white))
        ]
        
        resultList.append(contentsOf: list)
        resultList.append(
            Translate(Rotate(Box(Vec3(), Vec3(165), Dielectric(1.5)), .y, -18), Vec3(130, 0, 65))
        )
        
        return (HittableList(list: resultList), self.camera1, self.color1)
    }

    lazy var finalScene: (()->SceneComplex?) = {
        
        let white = Lambertian(ConstantTexture(Vec3(0.73)))
        let ground = Lambertian(ConstantTexture(Vec3(0.48, 0.83, 0.53)))
        
        var boxList = [Hittable]()
        var resultList = [Hittable]()
        
        for i in 0..<20 {
            for j in 0..<20 {
                let w = 100
                let x0 = -1000 + i*w
                let y0 = 0
                let z0 = -1000 + j*w
                let x1 = Float(x0 + w)
                let y1 = 100*(randomFloat()+0.01)
                let z1 = Float(z0 + w)
                let box = Box(Vec3(x0, y0, z0), Vec3(x1, y1, z1), ground)
                boxList.append(box)
            }
        }
        
        if let treeBVH = try? BVH(boxList, 0, 1) {
            resultList.append(treeBVH)
        }
        
        let light = DiffuseLight(ConstantTexture(Vec3(7)))
        resultList.append(Rect(.x, 123, 423, .z, 147, 412, 554, light))
        
        let center = Vec3(400, 400, 200)
        resultList.append(MovingSphere(centerS: center, centerE: center+Vec3(30, 0, 0), timeS: 0, timeE: 1, radius: 50, material: Lambertian(ConstantTexture(Vec3(0.7, 0.3, 0.1)))))
        
        resultList.append(Sphere(center: Vec3(260, 150, 45), radius: 50, material: Dielectric(1.5)))
        resultList.append(Sphere(center: Vec3(0, 150, 145), radius: 70, material: Metal(albedo: Vec3(0.8, 0.8, 0.9), fuzz: 10.0)))
        
        let boundary = Sphere(center: Vec3(360, 150, 145), radius: 70, material: Dielectric(1.5))
        resultList.append(ConstantMedium(boundary, ConstantTexture(Vec3(1.0)), 0.2))
        resultList.append(boundary)
        let bound = Sphere(center: Vec3(), radius: 5000, material: Dielectric(1.5))
        resultList.append(ConstantMedium(bound, ConstantTexture(Vec3(1.0)), 0.0001))
        
        guard let pixelMap = Image.gerPixelMap(imageName: "2k_sun.jpg") else {return nil}
        
        let material = Lambertian(ImageTexture(pixelMap))
        resultList.append(Sphere(center: Vec3(400, 200, 400), radius: 80, material: material))
        let pertext = NoiseTexture(0.1)
        resultList.append(Sphere(center: Vec3(220, 280, 300), radius: 80, material: Lambertian(pertext)))
        
        var sphereList = [Hittable]()
        for _ in 0..<1000 {
            sphereList.append(Sphere(center: Vec3(165*randomFloat(), 165*randomFloat(), 165*randomFloat()), radius: 10, material: white))
        }
        guard let spheres = try? Translate(Rotate(BVH(sphereList, 0, 1.0), .y, 15), Vec3(-100, 270, 395)) else {
            return nil //HittableList(list: resultList)
        }
        resultList.append(spheres)
        
        return (HittableList(list: resultList), self.camera1, self.color1)
    }
    
    func color1(ray: Ray, background: Vec3, world: Hittable, lightList: Hittable, depth: Int) -> Vec3 {
        
        if depth <= 0 { return Vec3() }
        
        guard let hitRecord = world.hitTest(ray: ray, t_min: 0.001, t_max: Float.greatestFiniteMagnitude) else {
            return background
        }
        
        let emitted = hitRecord.m.emitted(ray: ray, record: hitRecord, u: hitRecord.u, v: hitRecord.v, p: hitRecord.p);
        
        guard let scatterRecord = hitRecord.m.scatter(ray: ray, hitRecord: hitRecord) else { return emitted }
            
        if scatterRecord.isSpecular {
            return scatterRecord.attenuation * color1(ray: scatterRecord.specularRay!,
                                                      background: background,
                                                      world: world,
                                                      lightList: lightList,
                                                      depth: depth-1)
        }
        
        let lightPDF = HittablePDF(lightList, hitRecord.p)
        let mixPDF = MixturePDF(lightPDF, scatterRecord.pdf!)
        
        let scattered = Ray(hitRecord.p, mixPDF.generate(), ray.time)
        let value_PDF = mixPDF.value(scattered.direction)
        
        return emitted + scatterRecord.attenuation *
            hitRecord.m.scatterPDF(ray: ray, hitRecord: hitRecord, scattered: scattered) *
            color1(ray: scattered, background: background, world: world, lightList: lightList, depth: depth-1) / value_PDF
    }
    
    func work(imageView: NSImageView, sample: Int, size: NSSize, scene: SceneComplex, callback:@escaping (()->Void)) {
        
        let nx = Int(size.width); let ny = Int(size.height); let ns = sample;
        //let nx = 256; let ny = 256; let ns = 8;
        
        var result = "P3\n\(nx) \(ny)\n255\n"
        
        var pixelMap = [[PixelData]](repeating: [PixelData](repeating:PixelData(r: 0, g: 0, b: 0), count: nx),
                                     count: ny)
        
        let newImage = Image.imageFromRGB24Bitmap(pixelMap: pixelMap, width: nx, height: ny)
        DispatchQueue.main.async {
            autoreleasepool { //fix memory leak
                imageView.image = newImage
            }
        }
        
        let world = scene.0 //else { callback(); return; }
        let cam = scene.1(Float(nx)/Float(ny))
        let colorFunction = scene.2
        
        let processorCount = ProcessInfo().processorCount - 1
        let dataUnit = ny/processorCount
        let remain = ny%processorCount
        
        let light = DiffuseLight(ConstantTexture(Vec3(15)))
        
        let lightList = HittableList(list: [
            Rect(.x, 213, 343, .y, 227, 332, 554, light),
            Sphere(center: Vec3(190, 90, 190), radius: 90, material: light)
        ])
        
        let startTime = CFAbsoluteTimeGetCurrent()
                
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
                    var newR = Float(0)
                    var newG = Float(0)
                    var newB = Float(0)
                    for _ in 0..<ns {
                        let u = (Float(i) + randomFloat())/Float(nx)
                        let v = (Float(j) + randomFloat())/Float(ny)
                        let ray = cam.cast(s: u, t: v)
                        let ccc = colorFunction(ray, Vec3(), world, lightList, 50)
                        
                        newR += ccc.r.isNaN ? Float(0):ccc.r
                        newG += ccc.g.isNaN ? Float(0):ccc.g
                        newB += ccc.b.isNaN ? Float(0):ccc.b
                        
                        //col += Vec3(newR, newG, newB)
                    }
                    col = Vec3(newR, newG, newB)
                    col = col/Float(ns)
                    
                    //col = Vec3(newR, newG, newB)
                    newR = sqrt(col.r)
                    newG = sqrt(col.g)
                    newB = sqrt(col.b)
                    
                    let fff = simd_float3(newR, newG, newB) * 255.99
                    let fcol = simd_min(fff, simd_float3(repeating: 255))
                    
                    //let ir = UInt8(min(255, 255.99*col.r))
                    //let ig = UInt8(min(255, 255.99*col.g))
                    //let ib = UInt8(min(255, 255.99*col.b))
                    
                    //let pixel = PixelData(r: ir, g: ig, b: ib)
                    let pixel = PixelData(r: UInt8(fcol.x), g: UInt8(fcol.y), b: UInt8(fcol.z))
                    
                    queue.sync {
                        pixelMap[ny-1-j][i] = pixel
                    }
                    let newImage = Image.imageFromRGB24Bitmap(pixelMap: pixelMap, width: nx, height: ny)
                    DispatchQueue.main.async {
                        autoreleasepool { //fix memory leak
                            imageView.image = newImage
                        }
                    }
                }
            }
        }
        
        let timeElapsed = CFAbsoluteTimeGetCurrent() - startTime
        print("Rendering time: \(timeElapsed)")
        
        DispatchQueue.main.async(execute: callback)
        
        let file = "tmp.ppm" //this is the file. we will write to and read from it
        
        for j in 0..<ny {
            let k = ny-1-j
            for i in 0..<nx {
                let p = pixelMap[k][i]
                result.append("\(p.r) \(p.g) \(p.b)\n" )
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

}
