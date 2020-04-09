import Cocoa
import MetalKit

class TracerViewController: NSViewController {
    
    lazy var imageView: NSImageView = {
        
        let imageView  = NSImageView()
        imageView.imageScaling = .scaleProportionallyUpOrDown
        imageView.translatesAutoresizingMaskIntoConstraints = false
        
        self.view.addSubview(imageView)
        self.view.topAnchor.constraint(equalTo: imageView.topAnchor).isActive = true
        self.view.leftAnchor.constraint(equalTo: imageView.leftAnchor).isActive = true
        self.view.rightAnchor.constraint(equalTo: imageView.rightAnchor).isActive = true
        self.view.bottomAnchor.constraint(equalTo: imageView.bottomAnchor).isActive = true
        self.view.leadingAnchor.constraint(equalTo: imageView.leadingAnchor).isActive = true
        self.view.trailingAnchor.constraint(equalTo: imageView.trailingAnchor).isActive = true
        
        return imageView
    } ()
    
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
        
        //self.imageView.allowsCutCopyPaste = true
    }
}
