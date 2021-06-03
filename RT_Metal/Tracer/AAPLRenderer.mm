#import <SceneKit/SceneKit.h>
#import <ModelIO/ModelIO.h>

#include "AAPLRenderer.hh"

#include "Tracer.hh"
#include "BVH.hh"

typedef struct
{
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

typedef struct
{
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
    
    id<MTLBuffer> _kernelArgumentBuffer;
    
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
    
    id<MTLTexture> _textureUV;
    id<MTLTexture> _textureHDR;
    
    std::vector<id<MTLTexture>> texPBR;
}

- (nonnull instancetype)initWithMetalKitView:(nonnull MTKView *)view
{
    self = [super init];
    if(self)
    {
        // Get the display ID of the display in which the view appears
        //CGDirectDisplayID viewDisplayID = (CGDirectDisplayID) [_view.window.screen.deviceDescription[@"NSScreenNumber"] unsignedIntegerValue];

        // Get the Metal device that drives the display
        //id<MTLDevice> preferredDevice = CGDirectDisplayCopyCurrentMetalDevice(viewDisplayID);
        
        _view = view;
        _device = view.preferredDevice;
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
        
        var argumentEncoder = [kernelFunction newArgumentEncoderWithBufferIndex:9];
        let argumentBufferLength = argumentEncoder.encodedLength * 2;
        
        _kernelArgumentBuffer = [_device newBufferWithLength:argumentBufferLength options:0];
        _kernelArgumentBuffer.label = @"Argument Buffer";
        
        _computePipelineState = [_device newComputePipelineStateWithFunction:kernelFunction error:&ERROR];
        
        let vertexFunction = [defaultLibrary newFunctionWithName:@"vertexShader"];
        let fragmentFunction = [defaultLibrary newFunctionWithName:@"fragmentShader"];
        
        #if TARGET_OS_OSX
            let CommonStorageMode = MTLResourceStorageModeManaged;
        #else
            let CommonStorageMode = MTLResourceStorageModeShared;
        #endif
        
        _vertex_buffer = [_device newBufferWithBytes:canvas length:sizeof(VertexWithUV)*6 options: CommonStorageMode];
        
        uint width = 1920;
        uint height = 1080;
        
        _scene_meta.frame_count = 0;
        _scene_meta.running_time = 0;
        
        _scene_meta.tex_size = float2 {static_cast<float>(width), static_cast<float>(height)};
        _scene_meta.view_size = float2 {static_cast<float>(width), static_cast<float>(height)};
        
        _scene_meta_buffer = [_device newBufferWithBytes: &_scene_meta
                                                  length: sizeof(SceneComplex)
                                                 options: MTLResourceStorageModeShared];
        
        _camera_rotation = simd_make_float2(0, 0);
        
        prepareCamera(&_camera, _scene_meta.tex_size, _camera_rotation);
        _camera_buffer = [_device newBufferWithBytes: &_camera
                                              length: sizeof(struct Camera)
                                             options: MTLResourceStorageModeShared];
        
        std::vector<Material> materials;
        
        std::vector<Cube> cube_list;
        prepareCubeList(cube_list, materials);
        _cube_list_buffer = [_device newBufferWithBytes: cube_list.data()
                                                 length: sizeof(struct Cube)*cube_list.size()
                                                options: CommonStorageMode];
        
        std::vector<Square> cornell_box;
        prepareCornellBox(cornell_box, materials);
        _cornell_box_buffer = [_device newBufferWithBytes: cornell_box.data()
                                                   length: sizeof(struct Square)*cornell_box.size()
                                                  options: CommonStorageMode];
        
        std::vector<Sphere> sphere_list;
        prepareSphereList(sphere_list, materials);
        _sphere_list_buffer = [_device newBufferWithBytes: sphere_list.data()
                                                   length: sizeof(struct Sphere)*sphere_list.size()
                                                  options: CommonStorageMode];
        
        Material pbr;
        pbr.type = MaterialType::PBR;
        materials.emplace_back(pbr);
        
        _material_buffer = [_device newBufferWithBytes: materials.data()
                                                length: sizeof(struct Material)*materials.size()
                                               options: CommonStorageMode];
        
        // Create a reusable pipeline state object.
        let pipelineStateDescriptor = [[MTLRenderPipelineDescriptor alloc] init];
        
        pipelineStateDescriptor.label = @"Canvas Pipeline";
        pipelineStateDescriptor.sampleCount = _view.sampleCount;
        
        pipelineStateDescriptor.vertexFunction = vertexFunction;
        pipelineStateDescriptor.fragmentFunction = fragmentFunction;
        pipelineStateDescriptor.colorAttachments[0].pixelFormat = MTLPixelFormatRGBA16Float;
        
        _renderPipelineState = [_device newRenderPipelineStateWithDescriptor:pipelineStateDescriptor error:&ERROR];
        
        uint widthLevels = ceil(log2(width)), heightLevels = ceil(log2(height));
        uint mipCount = (heightLevels > widthLevels) ? heightLevels : widthLevels;
        
        let td = [[MTLTextureDescriptor alloc] init];
        td.textureType = MTLTextureType2D;
        td.pixelFormat = MTLPixelFormatRGBA16Float;
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
        
        let _sourceBuffer = [_device newBufferWithBytes: seeds
                                                 length: sizeof(UInt32)*4*width*height
                                                options: MTLResourceStorageModeShared];
        free(seeds);
        
        // Create a command buffer for GPU work.
        let commandBuffer = [_commandQueue commandBuffer];
        // Encode a blit pass to copy data from the source buffer to the private texture.
        let blitCommandEncoder = [commandBuffer blitCommandEncoder];
        
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
        
        MTKTextureLoader *loader = [[MTKTextureLoader alloc] initWithDevice: _device];
        
        NSDictionary *textureLoaderOptions = @ {
                    MTKTextureLoaderOptionSRGB: @NO,
                    MTKTextureLoaderOptionAllocateMipmaps: @YES,
                    MTKTextureLoaderOptionGenerateMipmaps: @YES,
                    MTKTextureLoaderOptionTextureUsage : @(MTLTextureUsageShaderRead),
                    MTKTextureLoaderOptionTextureStorageMode : @(MTLStorageModePrivate),
                    MTKTextureLoaderOptionOrigin: MTKTextureLoaderOriginFlippedVertically };
        
        let pathHDR = [NSBundle.mainBundle pathForResource:@"vulture_hide_4k" ofType:@"hdr"];
        
        #if TARGET_OS_OSX
            let urlHDR = [[NSURL alloc] initFileURLWithPath:pathHDR];
            let imageData = [[[NSImage alloc] initWithContentsOfURL:urlHDR] TIFFRepresentation];
        #else
            //let imageData = [[NSData alloc] initWithContentsOfFile:pathHDR];
            let image = [[UIImage alloc] initWithContentsOfFile:pathHDR];
            let imageData = UIImageJPEGRepresentation(image, 1.0);
            //UIImagePNGRepresentation(image);
        #endif
        
        _textureHDR = [loader newTextureWithData:imageData options:textureLoaderOptions error:&ERROR];
        
        let mdlUV = [MDLTexture textureNamed:@"uv_test/uv_test.png"];
        let _textureUV = [loader newTextureWithMDLTexture:mdlUV options:textureLoaderOptions error:&ERROR];
        
        let mdlAO = [MDLTexture textureNamed:@"coatball/tex_ao.png"];
        let _textureAO = [loader newTextureWithMDLTexture:mdlAO options:textureLoaderOptions error:&ERROR];
        
        texPBR.emplace_back(_textureUV);
        texPBR.emplace_back(_textureAO);
        
        var mdlAlbedo = [MDLTexture textureNamed:@"coatball/tex_base.png"];
        var mdlNormal = [MDLTexture textureNamed:@"coatball/tex_normal.png"];
        var mdlMetallic = [MDLTexture textureNamed:@"coatball/tex_metallic.png"];
        var mdlRoughness = [MDLTexture textureNamed:@"coatball/tex_roughness.png"];
        
        var _textureAlbedo = [loader newTextureWithMDLTexture:mdlAlbedo options:textureLoaderOptions error:&ERROR];
        var _textureNormal = [loader newTextureWithMDLTexture:mdlNormal options:textureLoaderOptions error:&ERROR];
        var _textureMetallic = [loader newTextureWithMDLTexture:mdlMetallic options:textureLoaderOptions error:&ERROR];
        var _textureRoughness = [loader newTextureWithMDLTexture:mdlRoughness options:textureLoaderOptions error:&ERROR];
        
        [argumentEncoder setArgumentBuffer:_kernelArgumentBuffer startOffset:0 arrayElement:0];

        [argumentEncoder setTexture:_textureAO atIndex:0];
        [argumentEncoder setTexture:_textureAlbedo atIndex:1];
        [argumentEncoder setTexture:_textureNormal atIndex:2];
        [argumentEncoder setTexture:_textureMetallic atIndex:3];
        [argumentEncoder setTexture:_textureRoughness atIndex:4];

        [argumentEncoder setTexture:_textureUV atIndex:5];
        
        auto tmp = std::vector<id<MTLTexture>>{ _textureAlbedo, _textureNormal, _textureMetallic, _textureRoughness};
        texPBR.insert(texPBR.end(), std::begin(tmp), std::end(tmp));
                

            mdlAlbedo = [MDLTexture textureNamed:@"goldscuffed/gold-scuffed_basecolor.png"];
            mdlNormal = [MDLTexture textureNamed:@"goldscuffed/gold-scuffed_normal.png"];
            mdlMetallic = [MDLTexture textureNamed:@"goldscuffed/gold-scuffed_metallic.png"];
            mdlRoughness = [MDLTexture textureNamed:@"goldscuffed/gold-scuffed_roughness.png"];

            _textureAlbedo = [loader newTextureWithMDLTexture:mdlAlbedo options:textureLoaderOptions error:&ERROR];
            _textureNormal = [loader newTextureWithMDLTexture:mdlNormal options:textureLoaderOptions error:&ERROR];
            _textureMetallic = [loader newTextureWithMDLTexture:mdlMetallic options:textureLoaderOptions error:&ERROR];
            _textureRoughness = [loader newTextureWithMDLTexture:mdlRoughness options:textureLoaderOptions error:&ERROR];

            [argumentEncoder setArgumentBuffer:_kernelArgumentBuffer startOffset:0 arrayElement:1];

            [argumentEncoder setTexture:_textureAO atIndex:0];
            [argumentEncoder setTexture:_textureAlbedo atIndex:1];
            [argumentEncoder setTexture:_textureNormal atIndex:2];
            [argumentEncoder setTexture:_textureMetallic atIndex:3];
            [argumentEncoder setTexture:_textureRoughness atIndex:4];

            [argumentEncoder setTexture:_textureUV atIndex:5];
        
            tmp = std::vector<id<MTLTexture>>{ _textureAlbedo, _textureNormal, _textureMetallic, _textureRoughness};
            texPBR.insert(texPBR.end(), std::begin(tmp), std::end(tmp));
        
            if(!_textureHDR)
            {
                NSLog(@"Failed to create the texture from %@", pathHDR);
                return nil;
            }
        
        //let modelPath = [NSBundle.mainBundle pathForResource:@"coatball/coatball" ofType:@"obj"];
        let modelPath = [NSBundle.mainBundle pathForResource:@"meshes/teapot" ofType:@"obj"];
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
        
        let allocator = [[MTKMeshBufferAllocator alloc] initWithDevice:_device];
        let testAsset = [[MDLAsset alloc] initWithURL: modelURL
                                     vertexDescriptor: modelIOVertexDescriptor
                                      bufferAllocator: allocator]; // preserveTopology: NO
        let testMesh = (MDLMesh *) [testAsset objectAtIndex:0];
        [testMesh addNormalsWithAttributeNamed:MDLVertexAttributeNormal creaseThreshold:0];
        //let voxelArray = [[MDLVoxelArray alloc] initWithAsset:testAsset divisions:1 patchRadius:0.2];
                
                std::vector<BVH> bvh_list;
                
                for (int i=1; i<sphere_list.size(); i++) {
                    auto& sphere = sphere_list[i];
                    BVH::buildNode(sphere.boundingBOX, sphere.model_matrix, ShapeType::Sphere, i, bvh_list);
                }
        
//                for (int i=0; i<cube_list.size()-1; i++) {
//                    auto& cube = cube_list[i];
//                    BVH::buildNode(cube.boundingBOX, cube.model_matrix, ShapeType::Cube, i, bvh_list);
//                }
        
                for (int i=4; i<5; i++) {
                //for (int i=0; i<cornell_box.size(); i++) {
                    auto& square = cornell_box[i];
                    BVH::buildNode(square.boundingBOX, square.model_matrix, ShapeType::Square, i, bvh_list);
                }
                
            let bBox = testMesh.boundingBox;
            let minB = (float3)bBox.minBounds;
            let maxB = (float3)bBox.maxBounds;
        
            let meshBox = AABB::make(minB, maxB);
            let centroid = meshBox.centroid();
            
            let maxAxis = meshBox.maximumExtent();
            let maxDime = meshBox.diagonal()[maxAxis];
            
            auto meshScale = 400.0 / maxDime;
            auto meshOffset = float3(278)-centroid;
            meshOffset.y = 20 - minB.y * meshScale;
        
            auto index_ptr = (uint32_t*)testMesh.submeshes.firstObject.indexBuffer.map.bytes;
            auto vertex_ptr = (MeshStrut*)testMesh.vertexBuffers.firstObject.map.bytes;
            auto index_count = testMesh.submeshes.firstObject.indexCount;
        
            for (uint i=0; i<index_count; i+=3) {
                
                auto index_a = index_ptr[i];
                auto index_b = index_ptr[i+1];
                auto index_c = index_ptr[i+2];
                
                auto vertex_a = (vertex_ptr + index_a);
                auto vertex_b = (vertex_ptr + index_b);
                auto vertex_c = (vertex_ptr + index_c);
                
                for (auto ele : { vertex_a, vertex_b, vertex_c }) {
                    
                    ele->vx *= meshScale;
                    ele->vy *= meshScale;
                    ele->vz *= -meshScale;
                    
                    ele->nz *= -1;
                    
                    ele->vx += meshOffset.x;
                    ele->vy += meshOffset.y;
                    ele->vz += meshOffset.z;
                }
                
                auto max_x = std::max( {vertex_a->vx, vertex_b->vx, vertex_c->vx} );
                auto max_y = std::max( {vertex_a->vy, vertex_b->vy, vertex_c->vy} );
                auto max_z = std::max( {vertex_a->vz, vertex_b->vz, vertex_c->vz} );
                
                auto min_x = std::min( {vertex_a->vx, vertex_b->vx, vertex_c->vx} );
                auto min_y = std::min( {vertex_a->vy, vertex_b->vy, vertex_c->vy} );
                auto min_z = std::min( {vertex_a->vz, vertex_b->vz, vertex_c->vz} );
                
                AABB box;
                
                box.maxi = { max_x, max_y, max_z };
                box.mini = { min_x, min_y, min_z };
                
                BVH::buildNode(box, matrix_identity_float4x4, ShapeType::Triangle, i/3, bvh_list);
            }
                NSLog(@"Begin processing BVH");
                let time_s = [[NSDate date] timeIntervalSince1970];
                BVH::buildTree(bvh_list);
                let time_e = [[NSDate date] timeIntervalSince1970];
                NSLog(@"Time cost %fs", time_e - time_s);
                NSLog(@"End processing BVH");
                
                _index_buffer = [_device newBufferWithBytes: [testMesh.submeshes.firstObject indexBuffer].map.bytes
                                                     length: [testMesh.submeshes.firstObject indexBuffer].length
                                                    options: CommonStorageMode];
                
                _mesh_buffer = [_device newBufferWithBytes: testMesh.vertexBuffers.firstObject.map.bytes
                                                    length: testMesh.vertexBuffers.firstObject.length
                                                   options: CommonStorageMode];
                
                _bvh_buffer = [_device newBufferWithBytes: bvh_list.data()
                                                   length: sizeof(struct BVH)*bvh_list.size()
                                                  options: CommonStorageMode];
                
        launchTime = [[NSDate date] timeIntervalSince1970];
        
        // Add a completion handler and commit the command buffer.
        [commandBuffer addCompletedHandler:^(id<MTLCommandBuffer> cb) {
            
            self->_view.paused = YES;
            self->_view.delegate = self;
            self->_view.enableSetNeedsDisplay = YES;
        }];
        
        [commandBuffer commit];
    }
    
    return self;
}

static std::vector<std::vector<int>> predefined_index { { 0, 1, 2, 3 }, {1, 0, 3, 2} };

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
        // not on main thread
        if (self->_dragging) {
            self->_scene_meta.frame_count = 0;
        } else {
            let fcount = self->_scene_meta.frame_count;
            self->_scene_meta.frame_count = fcount + 1;
        }
        
        //[self drag:simd_make_float2(1, 0) state:NO];
        
        [[NSOperationQueue mainQueue] addOperationWithBlock:^{
            #if TARGET_OS_OSX
                self->_view.needsDisplay = YES;
            #else
                [self->_view setNeedsDisplay];
            #endif
        }];
    }];
    
    let computeEncoder = [commandBuffer computeCommandEncoder];
    [computeEncoder setComputePipelineState:_computePipelineState];
 
    let tex_index = predefined_index[_scene_meta.frame_count % 2];
    
    [computeEncoder setTexture:_textureA atIndex: tex_index[0]];
    [computeEncoder setTexture:_textureB atIndex: tex_index[1]];
    
    [computeEncoder setTexture:_textureARNG atIndex: tex_index[2]];
    [computeEncoder setTexture:_textureBRNG atIndex: tex_index[3]];
    
    [computeEncoder setTexture:_textureHDR atIndex:4];
    
//    [computeEncoder useResource:_textureAO usage:MTLResourceUsageSample];
//    [computeEncoder useResource:_textureAlbedo usage:MTLResourceUsageSample];
//    [computeEncoder useResource:_textureNormal usage:MTLResourceUsageSample];
//    [computeEncoder useResource:_textureMetallic usage:MTLResourceUsageSample];
//    [computeEncoder useResource:_textureRoughness usage:MTLResourceUsageSample];
    
    [computeEncoder setBuffer:_kernelArgumentBuffer offset:0 atIndex:9];
    
    if (self->_scene_meta.frame_count > 3 || self->_scene_meta.frame_count < 1) {
        memcpy(_scene_meta_buffer.contents, &_scene_meta, sizeof(SceneComplex));
    }
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
    
    let _threadGroupSize = MTLSizeMake(8, 8, 1);
    let _gridSize = MTLSize {_textureA.width, _textureA.height, 1};
    
    [computeEncoder dispatchThreads:_gridSize threadsPerThreadgroup:_threadGroupSize];
    [computeEncoder endEncoding];
    
    let renderPassDescriptor = _view.currentRenderPassDescriptor;
    let renderEncoder = [commandBuffer renderCommandEncoderWithDescriptor:renderPassDescriptor];
    
    let& viewsize = _scene_meta.view_size;
    
    MTLViewport viewport {0, 0, viewsize.x, viewsize.y, 0, 1.0};
    
    [renderEncoder setViewport:viewport];
    [renderEncoder setRenderPipelineState:_renderPipelineState];
    
    [renderEncoder setVertexBuffer:_vertex_buffer offset:0 atIndex:0];
    
    [renderEncoder setFragmentBuffer:_scene_meta_buffer offset:0 atIndex:0];
    [renderEncoder setFragmentTexture:_textureB atIndex: tex_index[0]];
    [renderEncoder setFragmentTexture:_textureA atIndex: tex_index[1]];
    
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
