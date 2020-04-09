import Foundation
import MetalKit

struct VertexUV {
    let x: Float
    let y: Float
    
    let u: Float
    let v: Float
    
    init(_ x: Float, _ y: Float, _ u: Float, _ v: Float) {
        self.x = x
        self.y = y
        
        self.u = u
        self.v = v
    }
}

struct SceneMeta {
    var view_size = float2(1, 1)
    var running_time = Float(0)
    var sample_frame_count = UInt(0)
}

enum VertexInput: Int {
    case Vertices = 0
    case Viewport = 1
    case SystemTime = 2
}

enum RenderError: Error {
    case runtimeError(String)
}

class MetalRender: NSObject, MTKViewDelegate {
    let view: MTKView
    let device: MTLDevice
    
    let vertexBuffer: MTLBuffer
    let commandQueue: MTLCommandQueue
    
    let cube_list_buffer: MTLBuffer
    let cornell_box_buffer: MTLBuffer
    
    let sceneMetaBuffer: MTLBuffer
    private var sceneMeta = SceneMeta()
    let launch_time = Date().timeIntervalSince1970
    
    let cameraBuffer: MTLBuffer
    
    let renderPipelineState: MTLRenderPipelineState
    let computePipelineState: MTLComputePipelineState
    
    private var textureA: MTLTexture;
    private var textureB: MTLTexture;
    
    private var threadGroupSize: MTLSize
    private var threadGroupDimension: MTLSize
    
    static let vertex: [VertexUV]  = [
        VertexUV( 1, -1, 1, 1),
        VertexUV(-1, -1, 0, 1),
        VertexUV(-1,  1, 0, 0),
        
        VertexUV( 1, -1, 1, 1),
        VertexUV(-1,  1, 0, 0),
        VertexUV( 1,  1, 1, 0)]
    
    init(view: MTKView) throws {
        
        self.view = view
        view.preferredFramesPerSecond = 15
        
        guard let device = view.device else {
            throw RenderError.runtimeError ("Metal device not found")
        }
        self.device = device
        
        guard let defaultLibrary = device.makeDefaultLibrary() else {
            throw RenderError.runtimeError ("Metal Shader library not found")
            
        }
        guard let kernelFunction = defaultLibrary.makeFunction(name: "tracerKernel") else {
            throw RenderError.runtimeError ("Tracer Kernel not found")
        }
        self.computePipelineState = try device.makeComputePipelineState(function: kernelFunction)
        
        guard let vertexFunction = defaultLibrary.makeFunction(name: "vertexShader") else {
            throw RenderError.runtimeError ("Vertex Shader not found")
        }
        guard let fragmentFunction = defaultLibrary.makeFunction(name: "fragmentShader") else {
            throw RenderError.runtimeError ("Fragment Shader not found")
        }
        
        var vertexList = MetalRender.vertex
        let memorySize = MemoryLayout<VertexUV>.stride(ofValue: vertexList[0])*vertexList.count
        let vertexPointer = UnsafePointer(&vertexList)
        guard let vertexBuffer = device.makeBuffer(bytes: vertexPointer,
                                                   length: memorySize,
                                                   options: MTLResourceOptions.storageModeShared) else {
                                                    throw RenderError.runtimeError ("Vertex Buffer failed") }
        self.vertexBuffer = vertexBuffer
        
        let sceneMetaPointer = UnsafePointer(&sceneMeta)
        guard let sceneMetaBuffer = device.makeBuffer(bytes: sceneMetaPointer,
                                                       length: MemoryLayout<SceneMeta>.stride,
                                                       options: .storageModeShared) else {throw NSError()}
        self.sceneMetaBuffer = sceneMetaBuffer
        
        guard let cube_list = Tracer.cube_list() else {throw NSError()}
        let cube_list_length = MemoryLayout<Cube>.stride*2
        guard let cube_list_buffer = device.makeBuffer(bytes: cube_list, length: cube_list_length, options: .storageModeShared) else {throw NSError()}
        self.cube_list_buffer = cube_list_buffer;
        
        guard let cb = Tracer.cornell_box() else { throw NSError() };
        let cb_size = MemoryLayout<Square>.stride*6
        guard let cb_buffer = device.makeBuffer(bytes: cb, length: cb_size, options: .storageModeShared) else { throw NSError()}
        self.cornell_box_buffer = cb_buffer
        
        guard let camera = Tracer.camera(self.sceneMeta.view_size) else {
            throw NSError();
        }
        let camera_size = MemoryLayout<Camera>.stride
        guard let camera_buffer = device.makeBuffer(bytes: camera, length: camera_size, options: .storageModeShared) else { throw NSError()}
        self.cameraBuffer = camera_buffer
        
        let piplineDescriptor = MTLRenderPipelineDescriptor()
        piplineDescriptor.label = "2D Canvas pipeline"
        piplineDescriptor.vertexFunction = vertexFunction
        piplineDescriptor.fragmentFunction = fragmentFunction
        piplineDescriptor.colorAttachments[0].pixelFormat = view.colorPixelFormat
        self.renderPipelineState = try device.makeRenderPipelineState(descriptor: piplineDescriptor)
        
        guard let commandQueue = device.makeCommandQueue() else {
            throw RenderError.runtimeError ("Command Queue failed") }
        self.commandQueue = commandQueue
        
        let textureDescriptor = MTLTextureDescriptor()
        textureDescriptor.textureType = MTLTextureType.type2D
        textureDescriptor.pixelFormat = MTLPixelFormat.bgra8Unorm
        textureDescriptor.width = 800
        textureDescriptor.height = 800
        
        textureDescriptor.usage = [.renderTarget, .shaderRead, .shaderWrite]
        guard let texA = device.makeTexture(descriptor: textureDescriptor) else {throw NSError()}
        self.textureA = texA
        
        textureDescriptor.usage = [.renderTarget, .shaderRead, .shaderWrite]
        guard let texB = device.makeTexture(descriptor: textureDescriptor) else {throw NSError()}
        self.textureB = texB
        
        self.threadGroupSize = MTLSize(width: 16, height: 16, depth: 1)
        let gridX = (textureA.width + threadGroupSize.width - 1)/threadGroupSize.width
        let gridY = (textureA.height + threadGroupSize.height - 1)/threadGroupSize.height
        self.threadGroupDimension = MTLSize(width: gridX, height: gridY, depth: 1)
        
        super.init()
        
        view.delegate = self
    }
    
    func mtkView(_ view: MTKView, drawableSizeWillChange size: CGSize) {
        
        self.view.isPaused = true;
        
        self.sceneMeta.sample_frame_count = 0;
        let fwidth = Float(size.width)
        let fheight = Float(size.height)
        self.sceneMeta.view_size = float2(fwidth, fheight)
        
        self.textureA.setPurgeableState(MTLPurgeableState.empty)
        self.textureB.setPurgeableState(MTLPurgeableState.empty)
        
        let textureDescriptor = MTLTextureDescriptor()
        textureDescriptor.textureType = MTLTextureType.type2D
        textureDescriptor.pixelFormat = MTLPixelFormat.bgra8Unorm
        textureDescriptor.width = Int(size.width)
        textureDescriptor.height = Int(size.height)
        
        textureDescriptor.usage = [.renderTarget, .shaderRead, .shaderWrite]
        guard let texA = device.makeTexture(descriptor: textureDescriptor) else {return}
        self.textureA = texA
        
        textureDescriptor.usage = [.renderTarget, .shaderRead, .shaderWrite]
        guard let texB = device.makeTexture(descriptor: textureDescriptor) else {return}
        self.textureB = texB
        
        self.view.isPaused = false;
    }
    
    func draw(in view: MTKView) {
        
        if view.isPaused { return }
        
        self.sceneMeta.running_time = Float(Date().timeIntervalSince1970-launch_time)
        
        guard let commandBuffer = commandQueue.makeCommandBuffer() else {return}
        commandBuffer.label = ""
        
        commandBuffer.addCompletedHandler { (buffer) in
            //view.isPaused = false
            self.sceneMeta.sample_frame_count += 1;
        }
        
        guard let computeEncoder = commandBuffer.makeComputeCommandEncoder() else {return}
        computeEncoder.setComputePipelineState(self.computePipelineState)
        computeEncoder.setTexture(textureA, index: Int(self.sceneMeta.sample_frame_count % 2));
        computeEncoder.setTexture(textureB, index: Int((1+self.sceneMeta.sample_frame_count) % 2))
        
        let sceneMetaPointer = UnsafePointer(&sceneMeta)
        let sceneMetaLength = MemoryLayout<SceneMeta>.stride
        sceneMetaBuffer.contents().copyMemory(from: sceneMetaPointer, byteCount: sceneMetaLength)
        computeEncoder.setBuffer(sceneMetaBuffer, offset: 0, index: 0)
               
        guard let camera = Tracer.camera(self.sceneMeta.view_size) else {return}
        let length_camera = MemoryLayout<Camera>.stride
        self.cameraBuffer.contents().copyMemory(from: camera, byteCount: length_camera)
        computeEncoder.setBuffer(self.cameraBuffer, offset: 0, index: 1)
        
        computeEncoder.setBuffer(self.cornell_box_buffer, offset: 0, index: 3)
        computeEncoder.setBuffer(self.cube_list_buffer, offset: 0, index: 4)
        
        self.threadGroupSize = MTLSize(width: 16, height: 16, depth: 1)
        let gridX = (textureA.width + threadGroupSize.width - 1)/threadGroupSize.width
        let gridY = (textureA.height + threadGroupSize.height - 1)/threadGroupSize.height
        self.threadGroupDimension = MTLSize(width: gridX, height: gridY, depth: 1)
        
        computeEncoder.dispatchThreadgroups(self.threadGroupDimension, threadsPerThreadgroup: self.threadGroupSize)
        computeEncoder.endEncoding()
        
        guard let renderPassDescriptor = view.currentRenderPassDescriptor else {return}
        guard let renderEncoder = commandBuffer.makeRenderCommandEncoder(descriptor: renderPassDescriptor) else {return}
        renderEncoder.label = ""
        
        let viewport = MTLViewport(originX: 0,
                                   originY: 0,
                                   width: Double(self.sceneMeta.view_size.x),
                                   height: Double(self.sceneMeta.view_size.y),
                                   znear: -1.0, zfar: 1.0)
        renderEncoder.setViewport(viewport)
        renderEncoder.setRenderPipelineState(renderPipelineState)
        
        renderEncoder.setVertexBuffer(self.vertexBuffer, offset: 0, index: VertexInput.Vertices.rawValue)
    
        renderEncoder.setFragmentTexture(textureB, index: Int(self.sceneMeta.sample_frame_count % 2));
        renderEncoder.setFragmentTexture(textureA, index: Int((1+self.sceneMeta.sample_frame_count) % 2))
        
        renderEncoder.drawPrimitives(type: MTLPrimitiveType.triangle, vertexStart: 0, vertexCount: MetalRender.vertex.count)
        renderEncoder.endEncoding()
        
        guard let drawable = view.currentDrawable else {return}
        //commandBuffer.present(drawable, atTime: 1.0)
        commandBuffer.present(drawable)
        commandBuffer.commit()
        
        //view.isPaused = true
    }
}
