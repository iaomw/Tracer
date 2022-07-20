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
        super.viewDidLoad()
        
        metalRender = AAPLRenderer(metalKitView: self.metalView)
        
        NSEvent.addLocalMonitorForEvents(matching: .keyUp) { (event) -> NSEvent? in
            self.keyUp(with: event)
            return event
        }
        
        NSEvent.addLocalMonitorForEvents(matching: .keyDown) { (event) -> NSEvent? in
            self.keyDown(with: event)
            return event
        }
    }
    
    override func mouseUp(with event: NSEvent) {
        //print("mouseUp \(event)")
        let delta = simd_float2(0, 0)
        metalRender?.pin(delta, state: true)
    }

    override func mouseDown(with event: NSEvent) {
        //print("mouseDown \(event)")
        let delta = simd_float2(0, 0)
        metalRender?.pin(delta, state: false)
    }

    override func mouseDragged(with event: NSEvent) {
        //print("mouseDragged \(event)")
        let delta = simd_float2(Float(event.deltaX), Float(event.deltaY))
        metalRender?.pin(delta, state: false)
    }
    
    let kRightArrowKeyCode: UInt16  = 124
    let kLeftArrowKeyCode:  UInt16  = 123
    let kDownArrowKeyCode:  UInt16  = 125
    let kUpArrowKeyCode:    UInt16  = 126
    let kSpaceKeyCode:      UInt16  = 049
    
    let kWKeyCode: UInt16 = 13
    let kAKeyCode: UInt16 = 0
    let kSKeyCode: UInt16 = 1
    let kDKeyCode: UInt16 = 2
    
    override func keyDown(with event: NSEvent) {
        
        switch event.keyCode {

        case kLeftArrowKeyCode:
            metalRender?.pin(simd_float2( 1,  0), state: false)
        case kRightArrowKeyCode:
            metalRender?.pin(simd_float2(-1,  0), state: false)
        case kDownArrowKeyCode:
            metalRender?.pin(simd_float2( 0,  1), state: false)
        case kUpArrowKeyCode:
            metalRender?.pin(simd_float2( 0, -1), state: false)
        case kSpaceKeyCode:
            metalRender?.pin(simd_float2( 0,  0), state: false)
            
        case kWKeyCode:
            metalRender?.drag(simd_float3(0, 2, 0), state: false)
        case kSKeyCode:
            metalRender?.drag(simd_float3(0, -2, 0), state: false)
        case kAKeyCode:
            metalRender?.drag(simd_float3(-2, 0, 0), state: false)
        case kDKeyCode:
            metalRender?.drag(simd_float3(2, 0, 0), state: false)
        
        default:
            break
        }
    }
    
    override func keyUp(with event: NSEvent) {
        
        switch event.keyCode {
        case kSpaceKeyCode:
            metalRender?.pin(simd_float2( 0,  0), state: true)
        default:
            break
        }
    }
}


