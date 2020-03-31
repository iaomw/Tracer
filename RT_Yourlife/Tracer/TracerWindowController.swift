import Cocoa

class TracerWindowController: NSWindowController {
    @IBOutlet var toolBar: NSToolbar!
    
    @IBOutlet var sampleList: NSPopUpButton! {
        didSet {
            sampleList.target = self
            sampleList.action = #selector(sampleListChanged)
        }
    }
    private var sampleNumber = 8
    
    @IBOutlet var resolutonList: NSPopUpButton! {
        didSet {
            resolutonList.target = self
            resolutonList.action = #selector(resolutonListChanged)
        }
    }
    private var resolutonSize = CGSize(width: 256, height: 256)
    
    @IBOutlet var sceneList: NSPopUpButton! {
           didSet {
               sceneList.target = self
               sceneList.action = #selector(sceneListChanged)
           }
    }
    //private var sceneSelected: HittableList = randomScene()
    lazy var render = Render()
    
    lazy var expansionButton: NSButton = {
        
        if let window = self.window, let closeButton = window.standardWindowButton(.closeButton) {
          //window.titleVisibility = .hidden
          let button = NSButton(radioButtonWithTitle: "", target: self, action: #selector(switchToolbar))
            button.title = ""
            //myButton.bezelStyle = .roundedDisclosure

            let titleBarView = closeButton.superview!
            titleBarView.addSubview(button)
            button.translatesAutoresizingMaskIntoConstraints = false
            
            button.rightAnchor.constraint(equalTo: titleBarView.rightAnchor, constant: -4).isActive = true
            button.centerYAnchor.constraint(equalTo: closeButton.centerYAnchor, constant: 0.5).isActive = true
            
            return button
        }
        return NSButton()
    } ()
    
    override func windowDidLoad() {
        super.windowDidLoad()
        
        self.expansionButton.state = .on
    }
    
    @objc func switchToolbar() {
        
        self.toolBar.isVisible = !self.toolBar.isVisible
        
        if !self.toolBar.isVisible {
            
            guard let newScene = self.render.cachedScene(key:self.sceneList.title) else {
                self.toolBar.isVisible = !self.toolBar.isVisible
                return
            }
            
            if let vc = self.window?.contentViewController as? TracerViewController {
                self.expansionButton.state = .off
                self.expansionButton.isEnabled = false
                DispatchQueue.global(qos: .default).async {
                    
                    self.render.work(
                            imageView: vc.imageView,
                            sample: self.sampleNumber,
                            size:self.resolutonSize,
                            scene:newScene ,callback: {
                        self.expansionButton.state = .on
                        self.expansionButton.isEnabled = true
                        NSApp.requestUserAttention(.informationalRequest)
                    })
                }
            }
        }
    }
    
    @objc func sampleListChanged(sender: NSPopUpButton) {
        sender.title = sender.titleOfSelectedItem ?? ""
        self.sampleNumber = Int(sender.title) ?? 8
    }
    
    @objc func resolutonListChanged(sender: NSPopUpButton) {
        sender.title = sender.titleOfSelectedItem ?? ""
        
        let xy = sender.title.split(separator: "*")
        if xy.count == 2 {
            let x = Int(xy[0]) ?? 256
            let y = Int(xy[1]) ?? 256
            self.resolutonSize = CGSize(width: x, height: y)
        }
    }
    
    @objc func sceneListChanged(sender: NSPopUpButton) {
        sender.title = sender.titleOfSelectedItem ?? ""
//        if let newScene = sceneDict[sender.title]?() {
//            self.sceneSelected = newScene
//        }
    }
}
