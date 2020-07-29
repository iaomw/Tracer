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
        
        NSEvent.addLocalMonitorForEvents(matching: .keyDown) { (event) -> NSEvent? in
            self.keyDown(with: event)
            return event
        }
       // NSEvent.addLocalMonitorForEventsMatchingMask(NSEventMask.KeyDownMask, handler: keyDownkeyDown)
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
    
    let kLeftArrowKeyCode:  UInt16  = 123
    let kRightArrowKeyCode: UInt16  = 124
    let kDownArrowKeyCode:  UInt16  = 125
    let kUpArrowKeyCode:    UInt16  = 126
    
    override func keyDown(with event: NSEvent) {
        
        switch event.keyCode {

        case kLeftArrowKeyCode:
            metalRender?.drag(simd_float2(1, 0))
        case kRightArrowKeyCode:
            metalRender?.drag(simd_float2(-1, 0))
        case kDownArrowKeyCode:
            metalRender?.drag(simd_float2(0, 1))
        case kUpArrowKeyCode:
            metalRender?.drag(simd_float2(0, -1))
        default:
            break
        }
    }
}


