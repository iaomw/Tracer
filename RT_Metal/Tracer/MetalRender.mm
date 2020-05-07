#include "MetalRender.h"

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
    float2 viewSize;// = simd_float2(1, 1);
    float runningTime;
    uint32_t sample_frame_count;
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
    
    SceneComplex _scene_meta;
    
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
        _view.preferredFramesPerSecond = 30;
        _commandQueue = [_device newCommandQueue];
        
        NSError* ERROR;

        let defaultLibrary = [_device newDefaultLibrary];
        
        let kernelFunction = [defaultLibrary newFunctionWithName:@"tracerKernel"];
        
        _computePipelineState = [_device newComputePipelineStateWithFunction:kernelFunction error:&ERROR];
        
        let vertexFunction = [defaultLibrary newFunctionWithName:@"vertexShader"];
        let fragmentFunction = [defaultLibrary newFunctionWithName:@"fragmentShader"];
        
        _vertex_buffer = [_device newBufferWithBytes:canvas length:sizeof(VertexWithUV)*6 options:MTLResourceStorageModeShared];
        
        _scene_meta.runningTime = 0;
        _scene_meta.viewSize = {1024, 1024};
        _scene_meta.sample_frame_count = 0;
        
        _scene_meta_buffer = [_device newBufferWithBytes:&_scene_meta length:sizeof(SceneComplex) options:MTLResourceStorageModeShared];
        
        struct Camera camera;
        prepareCamera(&camera, _scene_meta.viewSize);
        _camera_buffer = [_device newBufferWithBytes:&camera
                        length:sizeof(struct Camera)
                        options:MTLResourceStorageModeShared];
        
        struct Cube cube_list[2];
        prepareCubeList(cube_list);
        _cube_list_buffer = [_device newBufferWithBytes:cube_list
                            length:sizeof(struct Cube)*2
                            options:MTLResourceStorageModeShared];
        
        struct Square cornell_box[6];
        prepareCornellBox(cornell_box);
        _cornell_box_buffer = [_device newBufferWithBytes:cornell_box
                            length:sizeof(struct Square)*6
                            options:MTLResourceStorageModeShared];
        
        // Create a reusable pipeline state object.
        let pipelineStateDescriptor = [[MTLRenderPipelineDescriptor alloc] init];
        
        pipelineStateDescriptor.label = @"2D Canvas pipeline";
        //pipelineStateDescriptor.sampleCount = _view.sampleCount;
        
        pipelineStateDescriptor.vertexFunction = vertexFunction;
        pipelineStateDescriptor.fragmentFunction = fragmentFunction;
        pipelineStateDescriptor.colorAttachments[0].pixelFormat = _view.colorPixelFormat;
        
        _renderPipelineState = [_device newRenderPipelineStateWithDescriptor:pipelineStateDescriptor error:&ERROR];
        
        let td = [[MTLTextureDescriptor alloc] init];
        td.textureType = MTLTextureType2D;
        td.pixelFormat = MTLPixelFormatBGRA8Unorm; //MTLPixelFormatRGBA8Unorm;
        td.width = 1024;
        td.height = 1024;
        
        td.usage = MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead | MTLTextureUsageShaderWrite;
        
        _textureA = [_device newTextureWithDescriptor:td];
        _textureB = [_device newTextureWithDescriptor:td];
        
        let tdr = [[MTLTextureDescriptor alloc] init];
        tdr.textureType = MTLTextureType2D;
        tdr.pixelFormat = MTLPixelFormatRGBA32Uint;
        tdr.width = 1024;
        tdr.height = 1024;
        
        tdr.usage = MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead | MTLTextureUsageShaderWrite;
        
        _textureARNG = [_device newTextureWithDescriptor:tdr];
        _textureBRNG = [_device newTextureWithDescriptor:tdr];
        
        UInt32 count = 1024*1024*4;
        UInt32* seeds = (UInt32*)malloc(count*sizeof(UInt32));

        for (int i = 0; i < count; i++) {
            seeds[i] = arc4random();
        }

        MTLRegion region = { 0, 0, 0, 1024, 1024, 1};
        [_textureARNG replaceRegion:region
                     mipmapLevel:0
                       withBytes:seeds
                     bytesPerRow:sizeof(UInt32)*4*1024];
        free(seeds);
        
        _threadGroupSize = MTLSizeMake(16, 16, 1);
        
        unsigned long gridX = (_textureA.width + _threadGroupSize.width - 1)/_threadGroupSize.width;
        unsigned long gridY = (_textureA.height + _threadGroupSize.height - 1)/_threadGroupSize.height;
        
        _threadGroupGrid = MTLSizeMake(gridX, gridY, 1);
        
        launchTime = [[NSDate date] timeIntervalSince1970];
        
        _view.delegate = self;
    }
    return self;
}

#pragma mark - MetalKit View Delegate
- (void)mtkView:(nonnull MTKView *)view drawableSizeWillChange:(CGSize)size
{
    _scene_meta.sample_frame_count = 0;
    _scene_meta.viewSize = simd_make_float2(size.width, size.height);
}

- (void)drawInMTKView:(nonnull MTKView *)view
{
    if (_view.isPaused) {return;}
    
    _scene_meta.runningTime = [[NSDate date] timeIntervalSince1970] - launchTime;
    
    let commandBuffer = [_commandQueue commandBuffer];
    
    //__weak AAPLRenderer *weakSelf = self;
    [commandBuffer addCompletedHandler:^(id<MTLCommandBuffer> buffer) {
        self->_scene_meta.sample_frame_count += 1;
        self->_view.paused = (self->_scene_meta.sample_frame_count > 1024);
    }];
    
    let computeEncoder = [commandBuffer computeCommandEncoder];
    [computeEncoder setComputePipelineState:_computePipelineState];
    
    [computeEncoder setTexture:_textureA atIndex:_scene_meta.sample_frame_count % 2];
    [computeEncoder setTexture:_textureB atIndex:(1+_scene_meta.sample_frame_count) % 2];
    
    [computeEncoder setTexture:_textureARNG atIndex:2 + _scene_meta.sample_frame_count % 2];
    [computeEncoder setTexture:_textureBRNG atIndex:2 + (1+_scene_meta.sample_frame_count) % 2];
    
    memcpy(_scene_meta_buffer.contents, &_scene_meta, sizeof(SceneComplex));
    
    [computeEncoder setBuffer:_scene_meta_buffer offset:0 atIndex:0];
    [computeEncoder setBuffer:_camera_buffer offset:0 atIndex:1];
    
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
        _scene_meta.viewSize.x, _scene_meta.viewSize.y,
        -1.0, 1.0};
    [renderEncoder setViewport:viewport];
    [renderEncoder setRenderPipelineState:_renderPipelineState];
    
    [renderEncoder setVertexBuffer:_vertex_buffer offset:0 atIndex:0];
    
    [renderEncoder setFragmentBuffer:_scene_meta_buffer offset:0 atIndex:0];
    [renderEncoder setFragmentTexture:_textureB atIndex:_scene_meta.sample_frame_count % 2];
    [renderEncoder setFragmentTexture:_textureA atIndex:(1+_scene_meta.sample_frame_count) % 2];
    
    [renderEncoder drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:6];
    [renderEncoder endEncoding];
    
    let drawable = _view.currentDrawable;
    [commandBuffer presentDrawable:drawable];
    [commandBuffer commit];
    _view.paused = YES;
}

@end
