import Cocoa
import MetalKit

class TracerViewController: NSViewController {
    
    var metalRender: AAPLRenderer?
    
    lazy var metalView: MTKView = {
        let mview = MTKView()
        mview.autoResizeDrawable = true
        mview.device = MTLCreateSystemDefaultDevice()
        mview.translatesAutoresizingMaskIntoConstraints = false
        
        self.view.addSubview(mview)
        self.view.topAnchor.constraint(equalTo: mview.topAnchor).isActive = true
        self.view.leftAnchor.constraint(equalTo: mview.leftAnchor).isActive = true
        self.view.rightAnchor.constraint(equalTo: mview.rightAnchor).isActive = true
        self.view.bottomAnchor.constraint(equalTo: mview.bottomAnchor).isActive = true
        //self.view.leadingAnchor.constraint(equalTo: imageView.leadingAnchor).isActive = true
        //self.view.trailingAnchor.constraint(equalTo: imageView.trailingAnchor).isActive = true
        return mview
    } ()
    
    override func viewDidLoad() {
        metalRender = AAPLRenderer(metalKitView: self.metalView)
    }
    
    override func mouseUp(with event: NSEvent) {
        //print("mouseUp \(event)")
    }

    override func mouseDown(with event: NSEvent) {
        //print("mouseDown \(event)")
    }

    override func mouseDragged(with event: NSEvent) {
        //print("mouseDragged \(event)")
        let delta = simd_float2(Float(event.deltaX), Float(event.deltaY))
        //print("mouseDragged \(delta)")
        metalRender?.drag(delta)
    }
}


