import UIKit

class ViewController: UIViewController {
    
    var metalRender: AAPLRenderer?
    
    lazy var metalView: MTKView = {
        let mview = MTKView()
        mview.autoResizeDrawable = true
        mview.device = MTLCreateSystemDefaultDevice()
        mview.translatesAutoresizingMaskIntoConstraints = false
        
        self.view.addSubview(mview)
        
        //self.view.topAnchor.constraint(equalTo: mview.topAnchor).isActive = true
        //self.view.bottomAnchor.constraint(equalTo: mview.bottomAnchor).isActive = true
        mview.leftAnchor.constraint(equalTo: self.view.leftAnchor).isActive = true
        mview.rightAnchor.constraint(equalTo: self.view.rightAnchor).isActive = true
        
        mview.centerXAnchor.constraint(equalTo: self.view.centerXAnchor).isActive = true
        mview.centerYAnchor.constraint(equalTo: self.view.centerYAnchor).isActive = true
        
        mview.widthAnchor.constraint(equalTo: self.view.widthAnchor, multiplier: 1.0).isActive = true
        mview.heightAnchor.constraint(equalTo: self.view.widthAnchor, multiplier: 9.0/16).isActive = true
        
        return mview
    } ()
    
    override func viewDidLoad() {
        super.viewDidLoad()
        self.view.backgroundColor = .black
        metalRender = AAPLRenderer(metalKitView: self.metalView)
    }
}

