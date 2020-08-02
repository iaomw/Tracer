#include "MetalRender.hh"
#include "Tracer.hh"

#import <ModelIO/ModelIO.h>

typedef struct {
    float x, y;
    float u, v;
} VertexWithUV;

const VertexWithUV canvas[] =
{
    { 1, -1, 1, 1},
    {-1, -1, 0, 1},
    {-1,  1, 0, 0},
    
    { 1, -1, 1, 1},
    {-1,  1, 0, 0},
    { 1,  1, 1, 0}
};

typedef struct  {
    float2 tex_size;
    float2 view_size;// = simd_float2(1, 1);
    float running_time;
    uint32_t frame_count;
} SceneComplex;

// The main class performing the rendering.
@implementation AAPLRenderer
{
    MTKView* _view;
    id<MTLDevice> _device;

    id<MTLBuffer> _vertex_buffer;
    id<MTLCommandQueue> _commandQueue;
    
    id<MTLBuffer> _cube_list_buffer;
    id<MTLBuffer> _cornell_box_buffer;
    id<MTLBuffer> _sphere_list_buffer;
   
    SceneComplex _scene_meta;
    
    Camera _camera;
    id<MTLBuffer> _camera_buffer;
    id<MTLBuffer> _scene_meta_buffer;
    
    float launchTime;
    
    id<MTLRenderPipelineState> _renderPipelineState;
    id<MTLComputePipelineState> _computePipelineState;
    //id<MTLComputePipelineState> _Nullable _computePipelineState;
    
    id<MTLTexture> _textureA;
    id<MTLTexture> _textureB;
    id<MTLTexture> _textureARNG;
    id<MTLTexture> _textureBRNG;
    
    id<MTLTexture> _viewTexture;
    
    id<MTLTexture> _textureHDR;
    
    MTLSize _threadGroupSize;
    MTLSize _threadGroupGrid;
}

- (nonnull instancetype)initWithMetalKitView:(nonnull MTKView *)view
{
    self = [super init];
    if(self)
    {
        _view = view;
        _device = view.device;
        //_view.preferredFramesPerSecond = 30;
        _commandQueue = [_device newCommandQueue];
        _view.colorPixelFormat = MTLPixelFormatRGBA16Float;
        
        NSError* ERROR;

        let defaultLibrary = [_device newDefaultLibrary];
        
        let kernelFunction = [defaultLibrary newFunctionWithName:@"tracerKernel"];
        
        _computePipelineState = [_device newComputePipelineStateWithFunction:kernelFunction error:&ERROR];
        
        let vertexFunction = [defaultLibrary newFunctionWithName:@"vertexShader"];
        let fragmentFunction = [defaultLibrary newFunctionWithName:@"fragmentShader"];
        
        _vertex_buffer = [_device newBufferWithBytes:canvas
                                              length:sizeof(VertexWithUV)*6
                                             options: MTLResourceStorageModeShared];
        
        uint width = 1920;
        uint height = 1080;
        
        _scene_meta.frame_count = 0;
        _scene_meta.running_time = 0;
        _scene_meta.view_size.x = width;
        _scene_meta.view_size.y = height;
        
        _scene_meta.tex_size.x = width;
        _scene_meta.tex_size.y = height;
        
        _scene_meta_buffer = [_device newBufferWithBytes:&_scene_meta length:sizeof(SceneComplex) options: MTLResourceStorageModeShared];
        
        //struct Camera camera;
        prepareCamera(&_camera, _scene_meta.view_size, simd_make_float2(0, 0));
        _camera_buffer = [_device newBufferWithBytes:&_camera
                        length:sizeof(struct Camera)
                        options: MTLResourceStorageModeShared];
        
        std::vector<Cube> cube_list;
        prepareCubeList(cube_list);
        _cube_list_buffer = [_device newBufferWithBytes:cube_list.data()
                            length:sizeof(struct Cube)*cube_list.size()
                            options: MTLResourceStorageModeManaged];
        
        std::vector<Square> cornell_box;
        prepareCornellBox(cornell_box);
        _cornell_box_buffer = [_device newBufferWithBytes:cornell_box.data()
                            length:sizeof(struct Square)*cornell_box.size()
                            options: MTLResourceStorageModeManaged];
        
        std::vector<Sphere> sphere_list;
        prepareSphereList(sphere_list);
        _sphere_list_buffer = [_device newBufferWithBytes:sphere_list.data()
                            length:sizeof(struct Sphere)*sphere_list.size()
                            options: MTLResourceStorageModeManaged];
        
        // Create a reusable pipeline state object.
        let pipelineStateDescriptor = [[MTLRenderPipelineDescriptor alloc] init];
        
        pipelineStateDescriptor.label = @"Canvas pipeline";
        //pipelineStateDescriptor.sampleCount = _view.sampleCount;
        
        pipelineStateDescriptor.vertexFunction = vertexFunction;
        pipelineStateDescriptor.fragmentFunction = fragmentFunction;
        pipelineStateDescriptor.colorAttachments[0].pixelFormat = _view.colorPixelFormat;
        
        _renderPipelineState = [_device newRenderPipelineStateWithDescriptor:pipelineStateDescriptor error:&ERROR];
        
        int widthLevels = ceil(log2(width));
        int heightLevels = ceil(log2(height));
        int mipCount = (heightLevels > widthLevels) ? heightLevels : widthLevels;
        
        let td = [[MTLTextureDescriptor alloc] init];
        td.textureType = MTLTextureType2D;
        td.pixelFormat = _view.colorPixelFormat; //MTLPixelFormatBGRA8Unorm;
        td.width = width;
        td.height = height;
        td.mipmapLevelCount = mipCount;
        td.storageMode = MTLStorageModePrivate;
        
        td.usage = MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead | MTLTextureUsageShaderWrite;
        
        _textureA = [_device newTextureWithDescriptor:td];
        _textureB = [_device newTextureWithDescriptor:td];
        
        _viewTexture = [_textureA newTextureViewWithPixelFormat:_textureA.pixelFormat
                                                    textureType:_textureA.textureType
                                                         levels:NSMakeRange(10, 1)
                                                         slices:NSMakeRange(0, 1)];
        
        let tdr = [[MTLTextureDescriptor alloc] init];
        tdr.textureType = MTLTextureType2D;
        tdr.pixelFormat = MTLPixelFormatRGBA32Uint;
        tdr.width = width;
        tdr.height = height;
        tdr.storageMode = MTLStorageModePrivate;
        
        tdr.usage = MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead | MTLTextureUsageShaderWrite;
        
        _textureARNG = [_device newTextureWithDescriptor:tdr];
        _textureBRNG = [_device newTextureWithDescriptor:tdr];
        
        UInt32 count = width * height * 4;
        UInt32* seeds = (UInt32*)malloc(count*sizeof(UInt32));

        for (int i = 0; i < count; i++) {
            seeds[i] = arc4random();
        }
        
        id <MTLBuffer> _sourceBuffer;
        _sourceBuffer = [_device newBufferWithBytes:seeds
                                             length:sizeof(UInt32)*4*width*height
                                            options:MTLResourceStorageModeShared];
        free(seeds);
        
        MTKTextureLoader *loader = [[MTKTextureLoader alloc] initWithDevice: _device];
        
        let path = [NSBundle.mainBundle pathForResource:@"vulture_hide_4k" ofType:@"hdr"];
        let url = [[NSURL alloc] initFileURLWithPath:path];
        
        NSDictionary *textureLoaderOptions = @{
                        MTKTextureLoaderOptionTextureUsage : @(MTLTextureUsageShaderRead),
                        MTKTextureLoaderOptionTextureStorageMode : @(MTLStorageModePrivate),
                    };
        
        let image = [[[NSImage alloc] initWithContentsOfURL:url] TIFFRepresentation];
        
        _textureHDR = [loader newTextureWithData:image options:textureLoaderOptions error:&ERROR];
        
        //_textureHDR = [loader newTextureWithContentsOfURL:url options: textureLoaderOptions error: &error];
            
            if(!_textureHDR)
            {
                NSLog(@"Failed to create the texture from %@", url.absoluteString);
                return nil;
            }
        
        _threadGroupSize = MTLSizeMake(16, 16, 1);
        
        unsigned long gridX = (_textureA.width + _threadGroupSize.width - 1)/_threadGroupSize.width;
        unsigned long gridY = (_textureA.height + _threadGroupSize.height - 1)/_threadGroupSize.height;
        
        _threadGroupGrid = MTLSizeMake(gridX, gridY, 1);
        
        launchTime = [[NSDate date] timeIntervalSince1970];
        
        // Create a command buffer for GPU work.
        id <MTLCommandBuffer> commandBuffer = [_commandQueue commandBuffer];
        // Encode a blit pass to copy data from the source buffer to the private texture.
        id <MTLBlitCommandEncoder> blitCommandEncoder = [commandBuffer blitCommandEncoder];
        
        [blitCommandEncoder copyFromBuffer:_sourceBuffer
                              sourceOffset:0
                         sourceBytesPerRow:sizeof(UInt32)*4 * width
                       sourceBytesPerImage:sizeof(UInt32)*4 * width * height
                                sourceSize: { width, height, 1 }
                                 toTexture:_textureARNG
                          destinationSlice:0
                          destinationLevel:0
                         destinationOrigin: {0,0,0}];
        [blitCommandEncoder endEncoding];

        // Add a completion handler and commit the command buffer.
        [commandBuffer addCompletedHandler:^(id<MTLCommandBuffer> cb) {
            self->_view.delegate = self; // Private texture is populated.
        }];
        [commandBuffer commit];
    }
    
    _view.paused = YES;
    _view.enableSetNeedsDisplay = YES;
    
    return self;
}

#pragma mark - MetalKit View Delegate
- (void)mtkView:(nonnull MTKView *)view drawableSizeWillChange:(CGSize)size
{
    //_scene_meta.frame_count = 0;
    _scene_meta.view_size = simd_make_float2(size.width, size.height);
}

- (void)drawInMTKView:(nonnull MTKView *)view
{
    //if (_view.isPaused) {return;}
    
    _scene_meta.running_time = [[NSDate date] timeIntervalSince1970] - launchTime;
    
                let commandBuffer = [_commandQueue commandBuffer];
    
                id<MTLBlitCommandEncoder> blit = [commandBuffer blitCommandEncoder];
    
                [blit generateMipmapsForTexture:self->_textureA];
                [blit generateMipmapsForTexture:self->_textureB];
    
                [blit endEncoding];
    
    //__weak AAPLRenderer *weakSelf = self;
    [commandBuffer addCompletedHandler:^(id<MTLCommandBuffer> buffer) {
        self->_scene_meta.frame_count += 1;
        
        [[NSOperationQueue mainQueue] addOperationWithBlock:^{
            
            self->_view.needsDisplay = YES;
        }];
    }];
    
    let computeEncoder = [commandBuffer computeCommandEncoder];
    [computeEncoder setComputePipelineState:_computePipelineState];
    
    [computeEncoder setTexture:_textureA atIndex:_scene_meta.frame_count % 2];
    [computeEncoder setTexture:_textureB atIndex:(_scene_meta.frame_count+1) % 2];
    
    [computeEncoder setTexture:_textureARNG atIndex:2 + _scene_meta.frame_count % 2];
    [computeEncoder setTexture:_textureBRNG atIndex:2 + (_scene_meta.frame_count+1) % 2];
    
    [computeEncoder setTexture:_textureHDR atIndex:4];
    
    memcpy(_scene_meta_buffer.contents, &_scene_meta, sizeof(SceneComplex));
    [computeEncoder setBuffer:_scene_meta_buffer offset:0 atIndex:0];
    
    memcpy(_camera_buffer.contents, &_camera, sizeof(Camera));
    [computeEncoder setBuffer:_camera_buffer offset:0 atIndex:1];
    
    [computeEncoder setBuffer:_sphere_list_buffer offset:0 atIndex:2];
    [computeEncoder setBuffer:_cornell_box_buffer offset:0 atIndex:3];
    [computeEncoder setBuffer:_cube_list_buffer offset:0 atIndex:4];
    
    _threadGroupSize = MTLSizeMake(16, 16, 1);
    
    unsigned long gridX = (_textureA.width + _threadGroupSize.width - 1)/_threadGroupSize.width;
    unsigned long gridY = (_textureA.height + _threadGroupSize.height - 1)/_threadGroupSize.height;
    
    _threadGroupGrid = MTLSizeMake(gridX, gridY, 1);
    
    [computeEncoder dispatchThreadgroups:_threadGroupGrid threadsPerThreadgroup:_threadGroupSize];
    [computeEncoder endEncoding];
    
    let renderPassDescriptor = _view.currentRenderPassDescriptor;
    let renderEncoder = [commandBuffer renderCommandEncoderWithDescriptor:renderPassDescriptor];
    
    MTLViewport viewport = {0, 0,
        _scene_meta.view_size.x, _scene_meta.view_size.y,
        0, 1.0};
    
    [renderEncoder setViewport:viewport];
    [renderEncoder setRenderPipelineState:_renderPipelineState];
    
    [renderEncoder setVertexBuffer:_vertex_buffer offset:0 atIndex:0];
    
    [renderEncoder setFragmentBuffer:_scene_meta_buffer offset:0 atIndex:0];
    [renderEncoder setFragmentTexture:_textureB atIndex:_scene_meta.frame_count % 2];
    [renderEncoder setFragmentTexture:_textureA atIndex:(_scene_meta.frame_count+1) % 2];
    
    [renderEncoder drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:6];
    [renderEncoder endEncoding];
    
    let drawable = _view.currentDrawable;
    [commandBuffer presentDrawable:drawable];
    [commandBuffer commit];
}

static float2 ddddd = simd_make_float2(0, 0);

- (void)drag:(float2)delta
{
    let ratio = delta / _scene_meta.view_size;
    
    ddddd += ratio;
    
    self->_scene_meta.frame_count = 0;
    self->_scene_meta.running_time = 0;
    
    prepareCamera(&_camera, _scene_meta.view_size, ddddd);
}

@end
