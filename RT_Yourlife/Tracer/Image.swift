import Foundation
import Cocoa


struct PixelData {
    var r: UInt8
    var g: UInt8
    var b: UInt8
}

class Image {
    
    static func imageFromRGB24Bitmap(pixelMap: [[PixelData]], width: Int, height: Int) -> NSImage? {
        
        guard width > 0 && height > 0 else { return nil }
        guard pixelMap.count*pixelMap.first!.count == width * height else { return nil }
        
        let rgbColorSpace = CGColorSpaceCreateDeviceRGB()
        let bitmapInfo = CGBitmapInfo(rawValue: CGImageAlphaInfo.none.rawValue)
        
        let bitsPerComponent = 8
        let bitsPerPixel = 24//32
        
        var pixelArray = pixelMap.reduce([], +) // Copy to mutable []
        let pixelData = NSData(bytes: &pixelArray, length: pixelArray.count * MemoryLayout<PixelData>.size)
        
        guard let providerRef = CGDataProvider(data: pixelData)
            else { return nil }
        
        guard let cgim = CGImage(
            width: width,
            height: height,
            bitsPerComponent: bitsPerComponent,
            bitsPerPixel: bitsPerPixel,
            bytesPerRow: width * MemoryLayout<PixelData>.size,
            space: rgbColorSpace,
            bitmapInfo: bitmapInfo,
            provider: providerRef,
            decode: nil,
            shouldInterpolate: true,
            intent: .defaultIntent
            )
            else { return nil }
        
        let scale = NSScreen.main?.backingScaleFactor ?? 1.0
        
        return autoreleasepool { () -> NSImage in
            return NSImage(cgImage: cgim, size: NSSize(width: CGFloat(width)/scale, height: CGFloat(height)/scale))
        }
    }
    
    static func gerPixelMap(imageName: String) -> [[Vec3]]? {
        let tex = NSImage(imageLiteralResourceName:imageName)
        let nx = Int(tex.size.width)
        let ny = Int(tex.size.height)
        
        guard let tiff = tex.tiffRepresentation, let bitMap = NSBitmapImageRep(data: tiff) else { return nil}
        
        var pixelMap = [[Vec3]](repeating: [Vec3](repeating:Vec3(), count: nx), count: ny)
        
        for i in 0..<nx {
            for j in 0..<ny {
                guard let color = bitMap.colorAt(x: i, y: j) else {break}
                
                let red = Float(color.redComponent)
                let green = Float(color.greenComponent)
                let blue = Float(color.blueComponent)
                
                pixelMap[j][i] = Vec3(red, green, blue)
            }
        }
        return pixelMap
    }
}
