#include "AAPLRenderer.hh"
#include "AAPLMesh.h"
#import "AAPLShaderTypes.h"

#import <SceneKit/SceneKit.h>
#include <ModelIO/ModelIO.h>
#import <SceneKit/ModelIO.h>

#include "Tracer.hh"

#include "BVH.hh"

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
    float2 view_size;
    float running_time;
    uint32_t frame_count;
} SceneComplex;

// The main class performing the rendering.
@implementation AAPLRenderer
{
    MTKView* _view;
    BOOL _dragging;
    
    id<MTLDevice> _device;

    id<MTLBuffer> _vertex_buffer;
    id<MTLCommandQueue> _commandQueue;
    
    id<MTLBuffer> _cube_list_buffer;
    id<MTLBuffer> _cornell_box_buffer;
    id<MTLBuffer> _sphere_list_buffer;
    
    id<MTLBuffer> _mesh_buffer;
    id<MTLBuffer> _index_buffer;
    
    id<MTLBuffer> _bvh_buffer;
    
    id<MTLBuffer> _material_buffer;
   
    Camera _camera;
    float2 _camera_rotation;
    SceneComplex _scene_meta;
    
    id<MTLBuffer> _camera_buffer;
    id<MTLBuffer> _scene_meta_buffer;
    
    MTLVertexDescriptor *_defaultVertexDescriptor;
    
    float launchTime;
    
    id<MTLRenderPipelineState> _renderPipelineState;
    id<MTLComputePipelineState> _computePipelineState;
    
    id<MTLTexture> _textureA;
    id<MTLTexture> _textureB;
    id<MTLTexture> _textureARNG;
    id<MTLTexture> _textureBRNG;
    
    id<MTLTexture> _textureHDR;
    id<MTLTexture> _textureTest;
    
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
        //_view.colorspace = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
        //_view.colorspace = CGColorSpaceCreateWithName(kCGColorSpaceLinearSRGB);
        //_view.colorspace = CGColorSpaceCreateWithName(kCGColorSpaceExtendedSRGB);
        //_view.colorspace = CGColorSpaceCreateWithName(kCGColorSpaceExtendedLinearSRGB);
        
        NSError* ERROR;

        let defaultLibrary = [_device newDefaultLibrary];
        
        let kernelFunction = [defaultLibrary newFunctionWithName:@"tracerKernel"];
        
        _computePipelineState = [_device newComputePipelineStateWithFunction:kernelFunction error:&ERROR];
        
        let vertexFunction = [defaultLibrary newFunctionWithName:@"vertexShader"];
        let fragmentFunction = [defaultLibrary newFunctionWithName:@"fragmentShader"];
        
        _vertex_buffer = [_device newBufferWithBytes:canvas length:sizeof(VertexWithUV)*6 options: MTLResourceStorageModeManaged];
        
        uint width = 1920;
        uint height = 1080;
        
        _scene_meta.frame_count = 0;
        _scene_meta.running_time = 0;
        
        _scene_meta.tex_size.x = width;
        _scene_meta.tex_size.y = height;
        
        _scene_meta.view_size.x = width;
        _scene_meta.view_size.y = height;
        
        _scene_meta_buffer = [_device newBufferWithBytes:&_scene_meta length:sizeof(SceneComplex) options: MTLResourceStorageModeShared];
        
        prepareCamera(&_camera, _scene_meta.tex_size, simd_make_float2(0, 0));
        _camera_buffer = [_device newBufferWithBytes:&_camera
                        length:sizeof(struct Camera)
                        options: MTLResourceStorageModeShared];
        
        _camera_rotation = simd_make_float2(0, 0);
        
        std::vector<Material> materials;
        
        std::vector<Cube> cube_list;
        prepareCubeList(cube_list, materials);
        _cube_list_buffer = [_device newBufferWithBytes:cube_list.data()
                            length:sizeof(struct Cube)*cube_list.size()
                            options: MTLResourceStorageModeManaged];
        
        std::vector<Square> cornell_box;
        prepareCornellBox(cornell_box, materials);
        _cornell_box_buffer = [_device newBufferWithBytes:cornell_box.data()
                            length:sizeof(struct Square)*cornell_box.size()
                            options: MTLResourceStorageModeManaged];
        
        std::vector<Sphere> sphere_list;
        prepareSphereList(sphere_list, materials);
        _sphere_list_buffer = [_device newBufferWithBytes:sphere_list.data()
                            length:sizeof(struct Sphere)*sphere_list.size()
                            options: MTLResourceStorageModeManaged];
        
        _material_buffer = [_device newBufferWithBytes:materials.data()
                                                length:sizeof(struct Material)*materials.size()
                                               options:MTLResourceStorageModeManaged];
        
        // Create a reusable pipeline state object.
        let pipelineStateDescriptor = [[MTLRenderPipelineDescriptor alloc] init];
        
        pipelineStateDescriptor.label = @"Canvas pipeline";
        pipelineStateDescriptor.sampleCount = _view.sampleCount;
        
        pipelineStateDescriptor.vertexFunction = vertexFunction;
        pipelineStateDescriptor.fragmentFunction = fragmentFunction;
        pipelineStateDescriptor.colorAttachments[0].pixelFormat = MTLPixelFormatRGBA16Float;
        
        _renderPipelineState = [_device newRenderPipelineStateWithDescriptor:pipelineStateDescriptor error:&ERROR];
        
        int widthLevels = ceil(log2(width));
        int heightLevels = ceil(log2(height));
        int mipCount = (heightLevels > widthLevels) ? heightLevels : widthLevels;
        
        let td = [[MTLTextureDescriptor alloc] init];
        td.textureType = MTLTextureType2D;
        td.pixelFormat = MTLPixelFormatRGBA16Float; //MTLPixelFormatBGRA8Unorm;
        td.width = width;
        td.height = height;
        td.mipmapLevelCount = mipCount;
        td.storageMode = MTLStorageModePrivate;
        
        td.usage = MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead | MTLTextureUsageShaderWrite;
        
        _textureA = [_device newTextureWithDescriptor:td];
        _textureB = [_device newTextureWithDescriptor:td];
        
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

        for (int i = 0; i < count; i++) { seeds[i] = arc4random(); }
        
        id <MTLBuffer> _sourceBuffer = [_device newBufferWithBytes:seeds
                                                            length:sizeof(UInt32)*4*width*height
                                                           options:MTLResourceStorageModeShared];
        free(seeds);
        
        // Create a command buffer for GPU work.
        id <MTLCommandBuffer> commandBuffer = [_commandQueue commandBuffer];
        // Encode a blit pass to copy data from the source buffer to the private texture.
        id <MTLBlitCommandEncoder> blitCommandEncoder = [commandBuffer blitCommandEncoder];
        
        [blitCommandEncoder copyFromBuffer: _sourceBuffer
                              sourceOffset: 0
                         sourceBytesPerRow: sizeof(UInt32)*4 * width
                       sourceBytesPerImage: sizeof(UInt32)*4 * width * height
                                sourceSize: { width, height, 1 }
                                 toTexture: _textureARNG
                          destinationSlice: 0
                          destinationLevel: 0
                         destinationOrigin: {0,0,0}];
        [blitCommandEncoder endEncoding];

        // Add a completion handler and commit the command buffer.
        [commandBuffer addCompletedHandler:^(id<MTLCommandBuffer> cb) {
            
            self->_view.paused = YES;
            self->_view.delegate = self;
            self->_view.enableSetNeedsDisplay = YES;
        }];
        
        MTKTextureLoader *loader = [[MTKTextureLoader alloc] initWithDevice: _device];
                
        let pathHDR = [NSBundle.mainBundle pathForResource:@"vulture_hide_4k" ofType:@"hdr"];
        let urlHDR = [[NSURL alloc] initFileURLWithPath:pathHDR];
        
        NSDictionary *textureLoaderOptions = @ {
            
                    MTKTextureLoaderOptionTextureUsage : @(MTLTextureUsageShaderRead),
                    MTKTextureLoaderOptionTextureStorageMode : @(MTLStorageModePrivate),

                    MTKTextureLoaderOptionOrigin: MTKTextureLoaderOriginFlippedVertically };
        
        let pathTest = [NSBundle.mainBundle pathForResource:@"coatball/uv_test" ofType:@"png"];
        let urlTest = [[NSURL alloc] initFileURLWithPath:pathTest];
    
        #if TARGET_OS_IPHONE
        
            let image = [[UIImage alloc] initWithContentsOfFile:path];
            //let imageData = [[NSData alloc] initWithContentsOfFile:path];
            let imageData = UIImagePNGRepresentation(image);
        #else
            let imageData = [[[NSImage alloc] initWithContentsOfURL:urlHDR] TIFFRepresentation];
            let testData =  [[[NSImage alloc] initWithContentsOfURL:urlTest] TIFFRepresentation];
        #endif
                
        _textureHDR = [loader newTextureWithData:imageData options:textureLoaderOptions error:&ERROR];
        _textureTest = [loader newTextureWithData:testData options:textureLoaderOptions error:&ERROR];
        
            if(!_textureHDR)
            {
                NSLog(@"Failed to create the texture from %@", urlHDR.absoluteString);
                return nil;
            }
                
        let allocator = [[MTKMeshBufferAllocator alloc] initWithDevice:_device];
        
        let modelPath = [NSBundle.mainBundle pathForResource:@"coatball/coatball" ofType:@"obj"];
        let modelURL = [[NSURL alloc] initFileURLWithPath:modelPath];
                
            MDLVertexDescriptor *modelIOVertexDescriptor = MTKModelIOVertexDescriptorFromMetal(_defaultVertexDescriptor);

            // Indicate how each Metal vertex descriptor attribute maps to each Model I/O attribute.
            modelIOVertexDescriptor.attributes[0].name = MDLVertexAttributePosition;
            modelIOVertexDescriptor.attributes[0].offset = 0;
            modelIOVertexDescriptor.attributes[0].bufferIndex = 0;
            modelIOVertexDescriptor.attributes[0].format = MDLVertexFormatFloat3;
            
            modelIOVertexDescriptor.attributes[1].name = MDLVertexAttributeNormal;
            modelIOVertexDescriptor.attributes[1].offset = 12;
            modelIOVertexDescriptor.attributes[1].bufferIndex = 0;
            modelIOVertexDescriptor.attributes[1].format = MDLVertexFormatFloat3;
            
            modelIOVertexDescriptor.attributes[2].name = MDLVertexAttributeTextureCoordinate;
            modelIOVertexDescriptor.attributes[2].offset = 24;
            modelIOVertexDescriptor.attributes[2].bufferIndex = 0;
            modelIOVertexDescriptor.attributes[2].format = MDLVertexFormatFloat2;
            
            modelIOVertexDescriptor.layouts[0].stride = 32;
                
        let testAsset = [[MDLAsset alloc] initWithURL:modelURL
                                    vertexDescriptor:modelIOVertexDescriptor
                                     bufferAllocator:allocator]; // preserveTopology: NO
        let testMesh = (MDLMesh *) [testAsset objectAtIndex:0];
        [testMesh addNormalsWithAttributeNamed:MDLVertexAttributeNormal creaseThreshold:0.0];
                
            //[[MDLVoxelArray alloc] init]
                
        std::vector<BVH> bvh_list;
                
        //        for (int i=0; i<sphere_list.size(); i++) {
        //            auto& sphere = sphere_list[i];
        //            BVH::build(sphere.boundingBOX, sphere.model_matrix, ShapeType::Sphere, i, bvh_list);
        //        }
        //
        //        for (int i=0; i<cube_list.size(); i++) {
        //            auto& cube = cube_list[i];
        //            BVH::build(cube.boundingBOX, cube.model_matrix, ShapeType::Cube, i, bvh_list);
        //        }
        //
        //        //for (int i=0; i<0; i++) {
        //        for (int i=4; i<cornell_box.size(); i++) {
        //            auto& square = cornell_box[i];
        //            BVH::build(square.boundingBOX, square.model_matrix, ShapeType::Square, i, bvh_list);
        //        }
                
            //let bBox = newMesh.boundingBox;
            //float3 minB = (float3)bBox.minBounds;
            //float3 maxB = (float3)bBox.maxBounds;
            //let meshBox = AABB::make(minB, maxB);
            //BVH::build(meshBox, meshTransform, ShapeType::Mesh, 0, bvh_list);
                
            auto index_ptr = (uint32_t*)testMesh.submeshes.firstObject.indexBuffer.map.bytes;
            auto vertex_ptr = (MeshStrut*)testMesh.vertexBuffers.firstObject.map.bytes;
            
            auto vertexCount = testMesh.submeshes.firstObject.indexCount;
            
            float scale = 30; float offset = 278;
            for (uint32_t i=0; i<vertexCount; i+=3) {
                
                auto index_a = index_ptr[i];
                auto index_b = index_ptr[i+1];
                auto index_c = index_ptr[i+2];
                
                auto vertex_a = (vertex_ptr + index_a);
                auto vertex_b = (vertex_ptr + index_b);
                auto vertex_c = (vertex_ptr + index_c);
                
                for (auto ele : { vertex_a, vertex_b, vertex_c }) {
                    
                    ele->vx *= scale;
                    ele->vy *= scale;
                    ele->vz *= scale;
                    
                    ele->vx += offset;
                    ele->vy += offset;
                    ele->vz += offset;
                }
                
                auto max_x = std::max( {vertex_a->vx, vertex_b->vx, vertex_c->vx} );
                auto max_y = std::max( {vertex_a->vy, vertex_b->vy, vertex_c->vy} );
                auto max_z = std::max( {vertex_a->vz, vertex_b->vz, vertex_c->vz} );
                
                auto min_x = std::min( {vertex_a->vx, vertex_b->vx, vertex_c->vx} );
                auto min_y = std::min( {vertex_a->vy, vertex_b->vy, vertex_c->vy} );
                auto min_z = std::min( {vertex_a->vz, vertex_b->vz, vertex_c->vz} );
                
                AABB box;
                
                box.maxi = simd_make_float3(max_x, max_y, max_z);
                box.mini = simd_make_float3(min_x, min_y, min_z);
                
                BVH::buildNode(box, matrix_identity_float4x4, ShapeType::Triangle, i/3, bvh_list);
            }
        
                BVH::buildTree(bvh_list);
                
                _mesh_buffer =  [_device newBufferWithBytes: testMesh.vertexBuffers.firstObject.map.bytes
                                                     length: testMesh.vertexBuffers.firstObject.length
                                                    options: MTLResourceStorageModeManaged];
                
                _index_buffer = [_device newBufferWithBytes: [testMesh.submeshes.firstObject indexBuffer].map.bytes
                                                     length: [testMesh.submeshes.firstObject indexBuffer].length
                                                    options: MTLResourceStorageModeManaged];
                
                _bvh_buffer = [_device newBufferWithBytes:bvh_list.data()
                                                   length:sizeof(struct BVH)*bvh_list.size()
                                                  options: MTLResourceStorageModeManaged];
                
                _threadGroupSize = MTLSizeMake(16, 16, 1);

                unsigned long gridX = (_textureA.width + _threadGroupSize.width - 1)/_threadGroupSize.width;
                unsigned long gridY = (_textureA.height + _threadGroupSize.height - 1)/_threadGroupSize.height;
                
                _threadGroupGrid = MTLSizeMake(gridX, gridY, 1);
                
        launchTime = [[NSDate date] timeIntervalSince1970];
        
        [commandBuffer commit];
    }
    
    return self;
}

#pragma mark - MetalKit View Delegate
- (void)mtkView:(nonnull MTKView *)view drawableSizeWillChange:(CGSize)size
{
    //_scene_meta.frame_count = 0;
    _scene_meta.view_size = simd_make_float2(size.width, size.height);
}

-(void)render:(MTKView *)view
{
    let commandBuffer = [_commandQueue commandBuffer];
    let time = [[NSDate date] timeIntervalSince1970];
    _scene_meta.running_time = time - launchTime;
    
        {
            let blit = [commandBuffer blitCommandEncoder];
            [blit generateMipmapsForTexture:self->_textureA];
            [blit generateMipmapsForTexture:self->_textureB];
            [blit endEncoding];
        }
    
    //__weak AAPLRenderer *weakSelf = self;
    [commandBuffer addCompletedHandler:^(id<MTLCommandBuffer> buffer) {
        
        if (self->_dragging) {
            self->_scene_meta.frame_count = 0;
        } else {
            self->_scene_meta.frame_count += 1;
        }
        
        [[NSOperationQueue mainQueue] addOperationWithBlock:^{
            
            #if TARGET_OS_IPHONE
                [self->_view setNeedsDisplay];
            #else
                self->_view.needsDisplay = YES;
            #endif
        }];
    }];
    
    let computeEncoder = [commandBuffer computeCommandEncoder];
    [computeEncoder setComputePipelineState:_computePipelineState];
    
    [computeEncoder setTexture:_textureA atIndex: _scene_meta.frame_count % 2];
    [computeEncoder setTexture:_textureB atIndex: (_scene_meta.frame_count+1) % 2];
    
    [computeEncoder setTexture:_textureARNG atIndex: 2 + _scene_meta.frame_count % 2];
    [computeEncoder setTexture:_textureBRNG atIndex: 2 + (_scene_meta.frame_count+1) % 2];
    
    [computeEncoder setTexture:_textureHDR atIndex:4];
    [computeEncoder setTexture:_textureTest atIndex:5];
    
    memcpy(_scene_meta_buffer.contents, &_scene_meta, sizeof(SceneComplex));
    [computeEncoder setBuffer:_scene_meta_buffer offset:0 atIndex:0];
    
    memcpy(_camera_buffer.contents, &_camera, sizeof(Camera));
    [computeEncoder setBuffer:_camera_buffer offset:0 atIndex:1];
    
    [computeEncoder setBuffer:_sphere_list_buffer offset:0 atIndex:2];
    [computeEncoder setBuffer:_cornell_box_buffer offset:0 atIndex:3];
    [computeEncoder setBuffer:_cube_list_buffer offset:0 atIndex:4];
    
    [computeEncoder setBuffer:_index_buffer offset:0 atIndex:5];
    [computeEncoder setBuffer:_mesh_buffer offset:0 atIndex:6];
    [computeEncoder setBuffer:_bvh_buffer offset:0 atIndex:7];
    
    [computeEncoder setBuffer:_material_buffer offset:0 atIndex:8];
    
    _threadGroupSize = MTLSizeMake(16, 16, 1);
    
    unsigned long gridX = (_textureA.width + _threadGroupSize.width - 1)/_threadGroupSize.width;
    unsigned long gridY = (_textureA.height + _threadGroupSize.height - 1)/_threadGroupSize.height;
    
    _threadGroupGrid = MTLSizeMake(gridX, gridY, 1);
    
    [computeEncoder dispatchThreadgroups:_threadGroupGrid threadsPerThreadgroup:_threadGroupSize];
    [computeEncoder endEncoding];
    
    let renderPassDescriptor = _view.currentRenderPassDescriptor;
    let renderEncoder = [commandBuffer renderCommandEncoderWithDescriptor:renderPassDescriptor];
    
    MTLViewport viewport = {0, 0, _scene_meta.view_size.x, _scene_meta.view_size.y, 0, 1.0};
    
    [renderEncoder setViewport:viewport];
    [renderEncoder setRenderPipelineState:_renderPipelineState];
    
    [renderEncoder setVertexBuffer:_vertex_buffer offset:0 atIndex:0];
    
    [renderEncoder setFragmentBuffer:_scene_meta_buffer offset:0 atIndex:0];
    [renderEncoder setFragmentTexture:_textureB atIndex: _scene_meta.frame_count % 2];
    [renderEncoder setFragmentTexture:_textureA atIndex: (_scene_meta.frame_count+1) % 2];
    
    [renderEncoder drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:6];
    [renderEncoder endEncoding];
    
    let drawable = _view.currentDrawable;
    [commandBuffer presentDrawable:drawable];
    [commandBuffer commit];
}

- (void)drawInMTKView:(nonnull MTKView *)view
{
    @autoreleasepool {
        [self render:view];
    }
}

- (void)drag:(float2)delta state:(BOOL)ended;
{
    _dragging = !ended;
    
    let ratio = delta / _scene_meta.view_size;
    
    _camera_rotation += ratio;
    
    self->_scene_meta.frame_count = 0;
    self->_scene_meta.running_time = 0;
    
    prepareCamera(&_camera, _scene_meta.tex_size, _camera_rotation);
}

@end
