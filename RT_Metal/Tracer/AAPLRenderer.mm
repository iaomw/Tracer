#import <SceneKit/SceneKit.h>
#import <ModelIO/ModelIO.h>

#include "AAPLRenderer.hh"

#include "pcg_basic.h"
#include "minipbrt.h"

#include "Medium.hh"
#include "Tracer.hh"

#include "Photon.hh"

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

// The main class performing the rendering.
@implementation AAPLRenderer
{
    MTKView* _view;
    BOOL _dragging;
    
    id<MTLDevice> _device;
    id<MTLCommandQueue> _commandQueue;
    
    id<MTLBuffer> _canvas_buffer;
    
    id<MTLBuffer> _argumentBufferPBR;
    id<MTLBuffer> _argumentBufferPri;
    id<MTLBuffer> _argumentBufferEnv;
    
    id<MTLBuffer> _material_buffer;
    
    id<MTLBuffer> _sphere_list_buffer;
    id<MTLBuffer> _square_list_buffer;
    id<MTLBuffer> _cube_list_buffer;
    
    id<MTLBuffer> _bvh_buffer;
    id<MTLBuffer> _idx_buffer;
    id<MTLBuffer> _tri_buffer;
    
    id<MTLBuffer> _densityInfoBuffer;
    id<MTLBuffer> _densityDataBuffer;
    
    id<MTLHeap> _heap;
   
    Camera _camera;
    float2 _camera_rotation;
    id<MTLBuffer> _camera_buffer;
    
    Complex _complex;
    id<MTLBuffer> _complex_buffer;
    
    MTLVertexDescriptor *_defaultVertexDescriptor;
    
    float launchTime;
    
    id<MTLComputePipelineState> _pipelineStatePathTracing;
    id<MTLRenderPipelineState> _pipelineStatePostprocessing;
    
        id<MTLBuffer> _cameraRecordBuffer;
        id<MTLBuffer> _cameraBoundsBuffer;
        id<MTLBuffer> _aremacBoundsBuffer;
        
        id<MTLBuffer> _photonRecordBuffer;
        id<MTLBuffer> _photonHashedBuffer;
        //id<MTLBuffer> _photonRadiusBuffer;
        
        id<MTLTexture>              _texturePhotonMark;
        id<MTLTexture>              _texturePhotonCount;
        
        MTLRenderPassDescriptor*    _renderPhotonCountPassDescriptor;
        id<MTLRenderPipelineState>  _renderPhotonCountPipelineState;
        id<MTLRenderPipelineState>  _renderPhotonScatePipelineState;
        
        id<MTLComputePipelineState> _pipelineStateCameraRecording;
        id<MTLComputePipelineState> _pipelineStateCameraReducing;
    
        id<MTLComputePipelineState> _pipelineStatePhotonParams;
        id<MTLComputePipelineState> _pipelineStatePhotonRadius;
        
        id<MTLComputePipelineState> _pipelineStatePhotonRecording;
        id<MTLComputePipelineState> _pipelineStatePhotonHashing;
    
        id<MTLComputePipelineState> _pipelineStatePhotonRefine;
    
    id<MTLTexture> _textureA;
    id<MTLTexture> _textureB;
    id<MTLTexture> _textureARNG;
    id<MTLTexture> _textureBRNG;
    
    id<MTLTexture> _textureUVT;
    id<MTLTexture> _textureHDR;
    
    std::vector<id<MTLTexture>> _vectorTexPBR;
    std::vector<id<MTLTexture>> _vectorTexAll;
    
    std::vector<id<MTLBuffer>> _vectorBufferAll;
}

- (nonnull instancetype)initWithMetalKitView:(nonnull MTKView *)view
{
    self = [super init];
    if(self)
    {
//        Get the display ID of the display in which the view appears
//          CGDirectDisplayID viewDisplayID = (CGDirectDisplayID) [_view.window.screen.deviceDescription[@"NSScreenNumber"] unsignedIntegerValue];
//        Get the Metal device that drives the display
//          id<MTLDevice> preferredDevice = CGDirectDisplayCopyCurrentMetalDevice(viewDisplayID);
        
//        id <NSObject> deviceObserver  = nil;
//        NSArray<id<MTLDevice>> *deviceList = nil;
//        deviceList = MTLCopyAllDevicesWithObserver(&deviceObserver,
//                                                   ^(id<MTLDevice> device, MTLDeviceNotificationName name) {
//                                                       //[self handleExternalGPUEventsForDevice:device notification:name];
//                                                   }); MTLCopyAllDevices();
        _view = view;
        _device = view.preferredDevice;
        //_view.preferredFramesPerSecond = 30;
        _commandQueue = [_device newCommandQueue];
        
        //NSLog(@"\n\n\n\u00a0")
        NSLog(@"Using device: %@", _device.name);
        
        _view.colorPixelFormat = MTLPixelFormatRGBA16Float;
        //_view.colorspace = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
        //_view.colorspace = CGColorSpaceCreateWithName(kCGColorSpaceLinearSRGB);
        //_view.colorspace = CGColorSpaceCreateWithName(kCGColorSpaceExtendedSRGB);
        //_view.colorspace = CGColorSpaceCreateWithName(kCGColorSpaceExtendedLinearSRGB);
        
        __block NSError* ERROR;
        
        NSTimeInterval _time_s, _time_e;

        let defaultLibrary = [_device newDefaultLibrary];
        
        let _kernelPathTracing = [defaultLibrary newFunctionWithName:@"kernelPathTracing"];
        _pipelineStatePathTracing = [_device newComputePipelineStateWithFunction:_kernelPathTracing error:&ERROR];
        
        let argumentEncoderPri = [_kernelPathTracing newArgumentEncoderWithBufferIndex:7];
        let argumentBufferLengthPri = argumentEncoderPri.encodedLength;
        _argumentBufferPri = [_device newBufferWithLength:argumentBufferLengthPri options:0];
        _argumentBufferPri.label = @"Argument Pri";
        
        let argumentEncoderEnv = [_kernelPathTracing newArgumentEncoderWithBufferIndex:8];
        let argumentBufferLengthEnv = argumentEncoderEnv.encodedLength;
        _argumentBufferEnv = [_device newBufferWithLength:argumentBufferLengthEnv options:0];
        _argumentBufferEnv.label = @"Argument Env";
        
        let argumentEncoderPBR = [_kernelPathTracing newArgumentEncoderWithBufferIndex:9];
        let argumentBufferLengthPBR = argumentEncoderPBR.encodedLength * 2;
        _argumentBufferPBR = [_device newBufferWithLength:argumentBufferLengthPBR options:0];
        _argumentBufferPBR.label = @"Argument PBR";
        
        let vertexFunction = [defaultLibrary newFunctionWithName:@"vertexShader"];
        let fragmentFunction = [defaultLibrary newFunctionWithName:@"fragmentShader"];
        
        #if TARGET_OS_OSX
            let _commonStorageMode = MTLResourceStorageModeManaged;
        #else
            let _commonStorageMode = MTLResourceStorageModeShared;
        #endif
        
        _canvas_buffer = [_device newBufferWithBytes:canvas length:sizeof(VertexWithUV)*6 options: _commonStorageMode];
        
        uint width = 1920;
        uint height = 1080;
        
        _complex.frame_count = 0;
        _complex.running_time = 0;
        
        _complex.tex_size = float2 {static_cast<float>(width), static_cast<float>(height)};
        _complex.view_size = float2 {static_cast<float>(width), static_cast<float>(height)};
        
        _complex_buffer = [_device newBufferWithBytes: &_complex
                                               length: sizeof(Complex)
                                              options: MTLResourceStorageModeShared];
        
        _camera_rotation = simd_make_float2(0, 0);
        
        prepareCamera(&_camera, _complex.tex_size, _camera_rotation);
        _camera_buffer = [_device newBufferWithBytes: &_camera
                                              length: sizeof(Camera)
                                             options: MTLResourceStorageModeShared];
        
        std::vector<Material> materials;
        
        std::vector<Cube> cube_list;
        prepareCubeList(cube_list, materials);
        _cube_list_buffer = [_device newBufferWithBytes: cube_list.data()
                                                 length: sizeof(Cube)*cube_list.size()
                                                options: _commonStorageMode];
        
        std::vector<Square> cornell_box;
        prepareCornellBox(cornell_box, materials);
        _square_list_buffer = [_device newBufferWithBytes: cornell_box.data()
                                                   length: sizeof(Square)*cornell_box.size()
                                                  options: _commonStorageMode];
        
        std::vector<Sphere> sphere_list;
        prepareSphereList(sphere_list, materials);
        _sphere_list_buffer = [_device newBufferWithBytes: sphere_list.data()
                                                   length: sizeof(Sphere)*sphere_list.size()
                                                  options: _commonStorageMode];
        
        Material testMaterial;
        testMaterial.type = MaterialType::Glass;
        testMaterial.medium = MediumType::Homogeneous;
        
        testMaterial.textureInfo.type = TextureType::Constant;
        testMaterial.textureInfo.albedo = float3 {1, 1, 1};
        
        materials.emplace_back(testMaterial);
        
        _material_buffer = [_device newBufferWithBytes: materials.data()
                                                length: sizeof(Material)*materials.size()
                                               options: _commonStorageMode];
        
        // Create a reusable pipeline state object.
        auto drawablePipelineDescriptor = [[MTLRenderPipelineDescriptor alloc] init];
        
        drawablePipelineDescriptor.label = @"Canvas Pipeline";
        drawablePipelineDescriptor.sampleCount = _view.sampleCount;
        
        drawablePipelineDescriptor.vertexFunction = vertexFunction;
        drawablePipelineDescriptor.fragmentFunction = fragmentFunction;
        drawablePipelineDescriptor.colorAttachments[0].pixelFormat = MTLPixelFormatRGBA16Float;
        
        _pipelineStatePostprocessing = [_device newRenderPipelineStateWithDescriptor:drawablePipelineDescriptor error:&ERROR];
        
        uint widthLevels = ceil(log2(width)), heightLevels = ceil(log2(height));
        uint mipCount = (heightLevels > widthLevels) ? heightLevels : widthLevels;
        
        let td = [[MTLTextureDescriptor alloc] init];
        td.textureType = MTLTextureType2D;
        td.pixelFormat = MTLPixelFormatRGBA32Float;
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
        
NSLog(@"Processing RNG");
_time_s = [[NSDate date] timeIntervalSince1970];
        
        uint32_t pixel_count = width * height * 4;
        uint32_t* pixel_seed = (uint32_t*)malloc(pixel_count*sizeof(uint32_t));
        
        typedef struct pcg_state_setseq_64 pcg32_t;
        
        var thread_count = (uint32_t) [[NSProcessInfo processInfo] activeProcessorCount];
        var thread_quota = pixel_count / thread_count;
        var thread_remai = pixel_count % thread_count;
        
        dispatch_apply(thread_count, dispatch_get_global_queue(QOS_CLASS_UTILITY, 0), ^(size_t idx){
            
            pcg32_t seed = { arc4random(), arc4random() };
            //  seed = { pcg32_random(), pcg32_random() };
            let offset = idx * thread_quota;
            
            for (int i=0; i<thread_quota; i++) {
                pixel_seed[offset + i] = pcg32_random_r(&seed); //pcg32_random();
            }
        });
        
        for (int i=0; i<thread_remai; i++) {
            pixel_seed[pixel_count-1-i] = pcg32_random();
        }
        //for (int i = 0; i < count; i++) { seeds[i] = arc4random(); }
        
        let _sourceBuffer = [_device newBufferWithBytes: pixel_seed
                                                 length: sizeof(UInt32)*4*width*height
                                                options: MTLResourceStorageModeShared];
        free(pixel_seed);
        
        let commandBuffer = [_commandQueue commandBuffer];
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
        
_time_e = [[NSDate date] timeIntervalSince1970];
NSLog(@"Done  %fs", _time_e - _time_s);
        
NSLog(@"Loading Texture");
_time_s = [[NSDate date] timeIntervalSince1970];
        
        MTKTextureLoader *textureLoader = [[MTKTextureLoader alloc] initWithDevice: _device];
        
        NSDictionary *textureLoaderOptions = @ {
                    MTKTextureLoaderOptionSRGB: @NO,
                    MTKTextureLoaderOptionAllocateMipmaps: @YES,
                    MTKTextureLoaderOptionGenerateMipmaps: @YES,
                    MTKTextureLoaderOptionTextureUsage : @(MTLTextureUsageShaderRead),
                    MTKTextureLoaderOptionTextureStorageMode : @(MTLStorageModePrivate),
                    MTKTextureLoaderOptionOrigin: MTKTextureLoaderOriginFlippedVertically };
        
        auto cqueue = dispatch_get_global_queue(QOS_CLASS_UTILITY, 0);
        auto semaphore = dispatch_semaphore_create(1);
        auto wait_group = dispatch_group_create();
        
        __block NSString *pathHDR;
        
        dispatch_group_enter(wait_group);
        dispatch_async(cqueue, ^{
            
            pathHDR = [NSBundle.mainBundle pathForResource:@"vulture_hide_4k" ofType:@"hdr"];
            
            #if TARGET_OS_OSX
                let urlHDR = [[NSURL alloc] initFileURLWithPath:pathHDR];
                let imageData = [[[NSImage alloc] initWithContentsOfURL:urlHDR] TIFFRepresentation];
            #else
                //let imageData = [[NSData alloc] initWithContentsOfFile:pathHDR];
                let image = [[UIImage alloc] initWithContentsOfFile:pathHDR];
                let imageData = UIImageJPEGRepresentation(image, 1.0);
                //UIImagePNGRepresentation(image);
            #endif
            
            self->_textureHDR = [textureLoader newTextureWithData:imageData options:textureLoaderOptions error:&ERROR];
            
            let mdlUVT = [MDLTexture textureNamed:@"uv_test/uv_test.png"];
            self->_textureUVT = [textureLoader newTextureWithMDLTexture:mdlUVT options:textureLoaderOptions error:&ERROR];
            
            dispatch_group_leave(wait_group);
        });
        
        let mdlAO = [MDLTexture textureNamed:@"coatball/tex_ao.png"];
        let _textureAO = [textureLoader newTextureWithMDLTexture:mdlAO options:textureLoaderOptions error:&ERROR];
        
        dispatch_group_enter(wait_group);
        dispatch_async(cqueue, ^{
            
            var mdlAlbedo = [MDLTexture textureNamed:@"coatball/tex_base.png"];
            var mdlNormal = [MDLTexture textureNamed:@"coatball/tex_normal.png"];
            var mdlMetallic = [MDLTexture textureNamed:@"coatball/tex_metallic.png"];
            var mdlRoughness = [MDLTexture textureNamed:@"coatball/tex_roughness.png"];
            
            var _textureAlbedo = [textureLoader newTextureWithMDLTexture:mdlAlbedo options:textureLoaderOptions error:&ERROR];
            var _textureNormal = [textureLoader newTextureWithMDLTexture:mdlNormal options:textureLoaderOptions error:&ERROR];
            var _textureMetallic = [textureLoader newTextureWithMDLTexture:mdlMetallic options:textureLoaderOptions error:&ERROR];
            var _textureRoughness = [textureLoader newTextureWithMDLTexture:mdlRoughness options:textureLoaderOptions error:&ERROR];
            
            auto tmp = std::vector<id<MTLTexture>>{ _textureAO, _textureAlbedo, _textureNormal, _textureMetallic, _textureRoughness};
            
            dispatch_semaphore_wait(semaphore, DISPATCH_TIME_FOREVER);
                self->_vectorTexPBR.insert(self->_vectorTexPBR.end(), std::begin(tmp), std::end(tmp));
            dispatch_semaphore_signal(semaphore);
                    
            dispatch_group_leave(wait_group);
        });
        
        dispatch_group_enter(wait_group);
        dispatch_async(cqueue, ^{
            
            let mdlAlbedo = [MDLTexture textureNamed:@"scuffed/gold-scuffed_base.png"];
            let mdlNormal = [MDLTexture textureNamed:@"scuffed/gold-scuffed_normal.png"];
            let mdlMetallic = [MDLTexture textureNamed:@"scuffed/gold-scuffed_metallic.png"];
            let mdlRoughness = [MDLTexture textureNamed:@"scuffed/gold-scuffed_roughness.png"];

            let _textureAlbedo = [textureLoader newTextureWithMDLTexture:mdlAlbedo options:textureLoaderOptions error:&ERROR];
            let _textureNormal = [textureLoader newTextureWithMDLTexture:mdlNormal options:textureLoaderOptions error:&ERROR];
            let _textureMetallic = [textureLoader newTextureWithMDLTexture:mdlMetallic options:textureLoaderOptions error:&ERROR];
            let _textureRoughness = [textureLoader newTextureWithMDLTexture:mdlRoughness options:textureLoaderOptions error:&ERROR];
        
            auto tmp = std::vector<id<MTLTexture>>{ _textureAO, _textureAlbedo, _textureNormal, _textureMetallic, _textureRoughness};
            
            dispatch_semaphore_wait(semaphore, DISPATCH_TIME_FOREVER);
                self->_vectorTexPBR.insert(self->_vectorTexPBR.end(), std::begin(tmp), std::end(tmp));
            dispatch_semaphore_signal(semaphore);
                    
            dispatch_group_leave(wait_group);
        });
        
        dispatch_group_wait(wait_group, DISPATCH_TIME_FOREVER);
        
            if(!_textureHDR)
            {
                NSLog(@"Failed to create the texture from %@", pathHDR);
                return nil;
            }
        
        _vectorTexAll = {_textureHDR, _textureUVT};
        _vectorTexAll.insert(_vectorTexAll.end(), std::begin(_vectorTexPBR), std::end(_vectorTexPBR));
        
_time_e = [[NSDate date] timeIntervalSince1970];
NSLog(@"Done  %fs", _time_e - _time_s);
        
        std::vector<BVH> bvh_list;
        
//        for (int i=1; i<sphere_list.size(); i++) {
//            auto& sphere = sphere_list[i];
//            BVH::buildNode(sphere.boundingBOX, sphere.model_matrix, PrimitiveType::Sphere, i, bvh_list);
//        }

            for (int i=0; i<cube_list.size()-1; i++) {
                auto& cube = cube_list[i];
                BVH::buildNode(cube.box, cube.model_matrix, PrimitiveType::Cube, i, bvh_list);
            }

        for (int i=4; i<7; i++) {
        //for (int i=0; i<cornell_box.size(); i++) {
            auto& square = cornell_box[i];
            BVH::buildNode(square.boundingBOX, square.model_matrix, PrimitiveType::Square, i, bvh_list);
        }
        
NSLog(@"Loading Mesh");
_time_s = [[NSDate date] timeIntervalSince1970];
        
        //let modelPath = [NSBundle.mainBundle pathForResource:@"coatball/coatball" ofType:@"obj"];
        let modelPath = [NSBundle.mainBundle pathForResource:@"meshes/bunny" ofType:@"obj"];
        //let modelPath = [NSBundle.mainBundle pathForResource:@"uv_test/uv_test" ofType:@"obj"];
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
        
_time_e = [[NSDate date] timeIntervalSince1970];
NSLog(@"Done  %fs", _time_e - _time_s);
        
NSLog(@"Processing Mesh");
_time_s = [[NSDate date] timeIntervalSince1970];
        
        let testMesh = (MDLMesh *) [testAsset objectAtIndex:0];
        [testMesh addNormalsWithAttributeNamed:MDLVertexAttributeNormal creaseThreshold:1];
        //let voxelArray = [[MDLVoxelArray alloc] initWithAsset:testAsset divisions:1 patchRadius:0.2];
                
            let bBox = testMesh.boundingBox;
            let minB = (float3)bBox.minBounds;
            let maxB = (float3)bBox.maxBounds;
        
            let meshBox = AABB::make(minB, maxB);
            let centroid = meshBox.centroid();
            
            let maxAxis = meshBox.maximumExtent();
            let maxDime = meshBox.diagonal()[maxAxis];
            
            auto meshScale = 300.0 / maxDime;
            auto meshOffset = float3(278)-centroid;
            meshOffset.y = 20 - minB.y * meshScale;
        
            auto vertex_ptr = (MeshElement*) testMesh.vertexBuffers.firstObject.map.bytes;
            int totalIndexBufferLength = 0, triangleIndexOffset = 0;
        
            for(MDLSubmesh* submesh in testMesh.submeshes) {
                
                auto index_bytes = (uint32_t*) submesh.indexBuffer.map.bytes;
                auto index_count = submesh.indexCount;
                auto tr_count = (uint)(index_count/3);
                //thread_count = 8;
                
                thread_quota = tr_count / thread_count;
                thread_remai = tr_count % thread_count;
                
                auto old_size = (uint)bvh_list.size();
                bvh_list.reserve(old_size + tr_count);
                
                auto thread_slot = std::vector<uint>(thread_count, thread_quota);
                thread_slot.back() += thread_remai;
                
                dispatch_apply(thread_count, dispatch_get_global_queue(QOS_CLASS_UTILITY, 0), [&] (size_t thread_idx) {
                    
                    for (uint task_idx=0; task_idx<thread_slot[thread_idx]; task_idx++) {
                        
                        uint i = (uint)(thread_idx * thread_quota + task_idx) * 3;
                        
                        auto index_a = index_bytes[i];
                        auto index_b = index_bytes[i+1];
                        auto index_c = index_bytes[i+2];
                        
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
                            
                            ele->vx += 350;
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
                        
                        let triangleIndex = triangleIndexOffset + i/3;
                        BVH::buildNode(box, identity_4x4, PrimitiveType::Triangle, triangleIndex, bvh_list);
                    } // for
                });
                
                totalIndexBufferLength += submesh.indexBuffer.length; triangleIndexOffset += tr_count;
            }
        
            char* totalIndexData = (char*)malloc(totalIndexBufferLength);
        
            int totalIndexOffset = 0;
            for(MDLSubmesh* submesh in testMesh.submeshes) {
                let length = submesh.indexBuffer.length;
                memcpy(totalIndexData + totalIndexOffset, submesh.indexBuffer.map.bytes, length);
                totalIndexOffset += length;
            }

_time_e = [[NSDate date] timeIntervalSince1970];
NSLog(@"Done  %fs", _time_e - _time_s);

NSLog(@"Processing BVH");
_time_s = [[NSDate date] timeIntervalSince1970];
            BVH::buildTree(bvh_list);
_time_e = [[NSDate date] timeIntervalSince1970];
NSLog(@"Done  %fs", _time_e - _time_s);
                
                _idx_buffer = [_device newBufferWithBytes: totalIndexData //[testMesh.submeshes.firstObject indexBuffer].map.bytes
                                                   length: totalIndexOffset //[testMesh.submeshes.firstObject indexBuffer].length
                                                  options: _commonStorageMode]; free(totalIndexData);
                
                _tri_buffer = [_device newBufferWithBytes: testMesh.vertexBuffers.firstObject.map.bytes
                                                   length: testMesh.vertexBuffers.firstObject.length
                                                  options: _commonStorageMode];
                
                _bvh_buffer = [_device newBufferWithBytes: bvh_list.data()
                                                   length: sizeof(BVH)*bvh_list.size()
                                                  options: _commonStorageMode];
        
NSLog(@"Loading volume");
_time_s = [[NSDate date] timeIntervalSince1970];
        
                minipbrt::Loader loaderPBRT;
                auto pathPBRT = [NSBundle.mainBundle pathForResource:@"cloud/cloud" ofType:@"pbrt"];
        
                if (loaderPBRT.load([pathPBRT UTF8String])) {
                    minipbrt::Scene* scene = loaderPBRT.take_scene();
                    auto* medium = dynamic_cast<minipbrt::HeterogeneousMedium*>(scene->mediums[0]);
                    
                    auto size_grid = sizeof(float) * medium->nx * medium->ny * medium->nz;
                    auto info_grid = GridDensityInfo(10, 90, 0.5, medium->nx, medium->ny, medium->nz, medium->density);
                    
                    _densityInfoBuffer = [_device newBufferWithBytes: &info_grid length: sizeof(info_grid) options: _commonStorageMode];
                    _densityDataBuffer = [_device newBufferWithBytes: medium->density length: size_grid options: _commonStorageMode];
                    
                    delete scene;
                }
                else {
                    // If parsing failed, the parser will have an error object.
                    const minipbrt::Error* err = loaderPBRT.error();
                    fprintf(stderr, "[%s, line %lld, column %lld] %s\n",
                            err->filename(), err->line(), err->column(), err->message());
                    // Don't delete err, it's still owned by the parser.
                    return nil;
                }
        
_time_e = [[NSDate date] timeIntervalSince1970];
NSLog(@"Done  %fs", _time_e - _time_s);
        
        _vectorBufferAll = { _cube_list_buffer, _square_list_buffer, _sphere_list_buffer,
                                _bvh_buffer, _idx_buffer, _tri_buffer, _material_buffer,
                                _densityInfoBuffer, _densityDataBuffer };
        
        [self createHeap];
        [self copyToHeap];
        
        _cube_list_buffer   = _vectorBufferAll[0];
        _square_list_buffer = _vectorBufferAll[1];
        _sphere_list_buffer = _vectorBufferAll[2];
        
        _bvh_buffer = _vectorBufferAll[3];
        _idx_buffer = _vectorBufferAll[4];
        _tri_buffer = _vectorBufferAll[5];
        
        _material_buffer = _vectorBufferAll[6];
        
        _densityInfoBuffer = _vectorBufferAll[7];
        _densityDataBuffer = _vectorBufferAll[8];
        
        _vectorBufferAll.clear();
        
        std::copy(_vectorTexPBR.begin(), _vectorTexPBR.end(), _vectorTexAll.begin()+2);
        
        for (int i=0; i<_vectorTexPBR.size(); i+=5) {
            
            [argumentEncoderPBR setArgumentBuffer:_argumentBufferPBR startOffset:0 arrayElement:i/5];

            [argumentEncoderPBR setTexture:_vectorTexPBR[i+0] atIndex:0];
            [argumentEncoderPBR setTexture:_vectorTexPBR[i+1] atIndex:1];
            [argumentEncoderPBR setTexture:_vectorTexPBR[i+2] atIndex:2];
            [argumentEncoderPBR setTexture:_vectorTexPBR[i+3] atIndex:3];
            [argumentEncoderPBR setTexture:_vectorTexPBR[i+4] atIndex:4];
        }
        
        _textureHDR = _vectorTexAll[0];
        _textureUVT = _vectorTexAll[1];
        
        _vectorTexAll.clear();
        
//        _textureAO = NULL;
//        _textureAlbedo = NULL;
//        _textureNormal = NULL;
//        _textureMetallic = NULL;
//        _textureRoughness = NULL;
        
        [argumentEncoderEnv setArgumentBuffer:_argumentBufferEnv offset:0];
        
        [argumentEncoderEnv setTexture:_textureHDR atIndex:0];
        [argumentEncoderEnv setTexture:_textureUVT atIndex:1];
        
        [argumentEncoderEnv setBuffer:_material_buffer offset:0 atIndex:2];
        
        [argumentEncoderEnv setBuffer:_densityInfoBuffer offset:0 atIndex:3];
        [argumentEncoderEnv setBuffer:_densityDataBuffer offset:0 atIndex:4];
        
        [argumentEncoderPri setArgumentBuffer:_argumentBufferPri offset:0];
        
        [argumentEncoderPri setBuffer:_sphere_list_buffer offset:0 atIndex:0];
        [argumentEncoderPri setBuffer:_square_list_buffer offset:0 atIndex:1];
        [argumentEncoderPri setBuffer:_cube_list_buffer offset:0 atIndex:2];
        
        [argumentEncoderPri setBuffer:_tri_buffer offset:0 atIndex:3];
        [argumentEncoderPri setBuffer:_idx_buffer offset:0 atIndex:4];
        [argumentEncoderPri setBuffer:_bvh_buffer offset:0 atIndex:5];
        
        launchTime = [[NSDate date] timeIntervalSince1970];
        // Add a completion handler and commit the command buffer.
        [commandBuffer addCompletedHandler:^(id<MTLCommandBuffer> cb) {
            self->_view.paused = YES;
            self->_view.delegate = self;
            self->_view.enableSetNeedsDisplay = YES;
        }];
        [commandBuffer commit];
        
            let _kernelCameraRecording = [defaultLibrary newFunctionWithName:@"kernelCameraRecording"];
            _pipelineStateCameraRecording = [_device newComputePipelineStateWithFunction:_kernelCameraRecording error:&ERROR];
            let _kernalCameraReducing = [defaultLibrary newFunctionWithName:@"kernelCameraReducing"];
            _pipelineStateCameraReducing = [_device newComputePipelineStateWithFunction:_kernalCameraReducing error:&ERROR];
        
            let _kernalPhotonParams = [defaultLibrary newFunctionWithName:@"kernelPhotonParams"];
            _pipelineStatePhotonParams = [_device newComputePipelineStateWithFunction:_kernalPhotonParams error:&ERROR];
            let _kernelPhotonRadius = [defaultLibrary newFunctionWithName:@"kernelPhotonRadius"];
            _pipelineStatePhotonRadius = [_device newComputePipelineStateWithFunction:_kernelPhotonRadius error:&ERROR];
            
            let _kernalPhotonRecording = [defaultLibrary newFunctionWithName:@"kernelPhotonRecording"];
            _pipelineStatePhotonRecording = [_device newComputePipelineStateWithFunction:_kernalPhotonRecording error:&ERROR];
            
            let _kernalPhotonHashing = [defaultLibrary newFunctionWithName:@"kernelPhotonHashing"];
            _pipelineStatePhotonHashing = [_device newComputePipelineStateWithFunction:_kernalPhotonHashing error:&ERROR];
        
            let _kernelPhotonRefine = [defaultLibrary newFunctionWithName:@"kernelPhotonRefine"];
            _pipelineStatePhotonRefine = [_device newComputePipelineStateWithFunction:_kernelPhotonRefine error:&ERROR];
        
            _cameraRecordBuffer = [_device newBufferWithLength:sizeof(CameraRecord) * 1920 * 1080
                                                       options:_commonStorageMode];
            [_cameraRecordBuffer setLabel:@"_cameraRecordBuffer"];
        
            _cameraBoundsBuffer = [_device newBufferWithLength:sizeof(AABB) * 1920 * 1080
                                                       options:_commonStorageMode];
            _aremacBoundsBuffer = [_device newBufferWithLength:sizeof(AABB) * 4096
                                                       options:_commonStorageMode];
        
            _photonRecordBuffer = [_device newBufferWithLength:sizeof(PhotonRecord) * 512 * 512
                                                       options:_commonStorageMode];
            [_photonRecordBuffer setLabel:@"_photonRecordBuffer"];
        
            _photonHashedBuffer = [_device newBufferWithLength:sizeof(float4) * 512 * 512
                                                       options:_commonStorageMode];
            [_photonHashedBuffer setLabel:@"_photonHashedBuffer"];
        
            // Set up a texture for rendering to and sampling from
            auto photonMarkDescription = [[MTLTextureDescriptor alloc] init];
            photonMarkDescription.textureType = MTLTextureType2D;
            photonMarkDescription.width = 512;
            photonMarkDescription.height = 512;
            photonMarkDescription.usage = MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead;
            
            photonMarkDescription.pixelFormat = MTLPixelFormatRGBA32Float;
            _texturePhotonMark = [_device newTextureWithDescriptor:photonMarkDescription];
        
            photonMarkDescription.pixelFormat = MTLPixelFormatR32Float;
            _texturePhotonCount = [_device newTextureWithDescriptor:photonMarkDescription];
        
            _renderPhotonCountPassDescriptor = [[MTLRenderPassDescriptor alloc] init];
            _renderPhotonCountPassDescriptor.colorAttachments[0].texture = _texturePhotonMark;
            _renderPhotonCountPassDescriptor.colorAttachments[0].loadAction = MTLLoadActionClear;
            _renderPhotonCountPassDescriptor.colorAttachments[0].clearColor = MTLClearColorMake(0, 0, 0, 0);
            _renderPhotonCountPassDescriptor.colorAttachments[0].storeAction = MTLStoreActionStore;
            _renderPhotonCountPassDescriptor.colorAttachments[1].texture = _texturePhotonCount;
            _renderPhotonCountPassDescriptor.colorAttachments[1].loadAction = MTLLoadActionClear;
            _renderPhotonCountPassDescriptor.colorAttachments[1].clearColor = MTLClearColorMake(0, 0, 0, 0);
            _renderPhotonCountPassDescriptor.colorAttachments[1].storeAction = MTLStoreActionStore;

            auto photonMarkPipelineDescriptor = [[MTLRenderPipelineDescriptor alloc] init];
            
            // Set up pipeline for rendering to the offscreen texture. Reuse the
            // descriptor and change properties that differ.
            photonMarkPipelineDescriptor.label = @"Photon Count Pipeline";
            photonMarkPipelineDescriptor.sampleCount = 1;
            photonMarkPipelineDescriptor.vertexFunction = [defaultLibrary newFunctionWithName:@"PhotonMarkVS"];
            photonMarkPipelineDescriptor.fragmentFunction = [defaultLibrary newFunctionWithName:@"PhotonMarkFS"];
            photonMarkPipelineDescriptor.colorAttachments[0].pixelFormat = _texturePhotonMark.pixelFormat;
            photonMarkPipelineDescriptor.colorAttachments[1].pixelFormat = _texturePhotonCount.pixelFormat;
            
            photonMarkPipelineDescriptor.colorAttachments[0].blendingEnabled = false;
            
            photonMarkPipelineDescriptor.colorAttachments[1].blendingEnabled = true;
            photonMarkPipelineDescriptor.colorAttachments[1].rgbBlendOperation = MTLBlendOperationAdd;
            photonMarkPipelineDescriptor.colorAttachments[1].alphaBlendOperation = MTLBlendOperationAdd;
            photonMarkPipelineDescriptor.colorAttachments[1].sourceRGBBlendFactor = MTLBlendFactorOne;
            photonMarkPipelineDescriptor.colorAttachments[1].sourceAlphaBlendFactor = MTLBlendFactorOne;
            photonMarkPipelineDescriptor.colorAttachments[1].destinationRGBBlendFactor = MTLBlendFactorOne;
            photonMarkPipelineDescriptor.colorAttachments[1].destinationAlphaBlendFactor = MTLBlendFactorOne;
        
            _renderPhotonCountPipelineState = [_device newRenderPipelineStateWithDescriptor:photonMarkPipelineDescriptor error:&ERROR];
    }
    
    return self;
}

static std::vector<std::vector<int>> predefined_index { { 0, 1, 2, 3 }, {1, 0, 3, 2} };

#pragma mark - MetalKit View Delegate
- (void)mtkView:(nonnull MTKView *)view drawableSizeWillChange:(CGSize)size
{
    //_scene_meta.frame_count = 0;
    _complex.view_size = simd_make_float2(size.width, size.height);
}

- (void)photonPrepare:(MTKView *)view
{
    auto commandBuffer = [_commandQueue commandBuffer];
    
    auto computeEncoder = [commandBuffer computeCommandEncoder];
    [computeEncoder setComputePipelineState:_pipelineStateCameraRecording];
 
    let tex_index = predefined_index[_complex.frame_count % 2];
    
    [computeEncoder setTexture:_textureA atIndex: tex_index[0]];
    [computeEncoder setTexture:_textureB atIndex: tex_index[1]];
    
    [computeEncoder setTexture:_textureARNG atIndex: tex_index[2]];
    [computeEncoder setTexture:_textureBRNG atIndex: tex_index[3]];
    
    memcpy(_camera_buffer.contents, &_camera, sizeof(Camera));
    
    if (self->_complex.frame_count > 3 || self->_complex.frame_count < 1) {
        memcpy(_complex_buffer.contents, &_complex, sizeof(Complex));
    }
    
    [computeEncoder setBuffer:_camera_buffer offset:0 atIndex:0];
    [computeEncoder setBuffer:_complex_buffer offset:0 atIndex:1];
    [computeEncoder setBuffer:_cameraRecordBuffer offset:0 atIndex:2];
    [computeEncoder setBuffer:_cameraBoundsBuffer offset:0 atIndex:3];
    
    [computeEncoder useHeap:_heap];
    [computeEncoder setBuffer:_argumentBufferPri offset:0 atIndex:7];
    [computeEncoder setBuffer:_argumentBufferEnv offset:0 atIndex:8];
    [computeEncoder setBuffer:_argumentBufferPBR offset:0 atIndex:9];
    
    let _threadGroupSize = MTLSizeMake(8, 8, 1);
    let _threadGridSize = MTLSize {_textureA.width, _textureA.height, 1};
    
    [computeEncoder dispatchThreads:_threadGridSize threadsPerThreadgroup:_threadGroupSize];
    
    [computeEncoder setComputePipelineState:_pipelineStateCameraReducing];
    [computeEncoder setBuffer:_camera_buffer offset:0 atIndex:0];
    [computeEncoder setBuffer:_complex_buffer offset:0 atIndex:1];
    
    uint data_bound = uint(_textureA.width * _textureA.height);
    [computeEncoder setBytes:&data_bound length:sizeof(uint) atIndex:2];
    
    [computeEncoder setBuffer:_cameraBoundsBuffer offset:0 atIndex:3];
    [computeEncoder setBuffer:_aremacBoundsBuffer offset:0 atIndex:4];
    
    auto thread_count = (uint)1 << (uint)floor(log2(data_bound));
    
    auto _input = _cameraBoundsBuffer;
    auto _output = _aremacBoundsBuffer;
    
    uint threadgroup_size = 256; uint step = (uint)floor(log2(threadgroup_size));
    
    do {
        [computeEncoder dispatchThreads:{ thread_count, 1, 1 }
                  threadsPerThreadgroup:{ threadgroup_size, 1, 1 }];

        if (thread_count <= threadgroup_size) { break; }

        data_bound = thread_count >> step; thread_count = data_bound >> 1;
        [computeEncoder setBytes:&data_bound length:sizeof(uint) atIndex:2];

        std::swap(_input, _output);

        [computeEncoder setBuffer:_input offset:0 atIndex:3];
        [computeEncoder setBuffer:_output offset:0 atIndex:4];

    } while (thread_count > 0);
    
    [computeEncoder setComputePipelineState:_pipelineStatePhotonParams];
    [computeEncoder setBuffer:_complex_buffer offset:0 atIndex:0];
    [computeEncoder setBuffer:_output         offset:0 atIndex:1];
    [computeEncoder dispatchThreads:{1, 1, 1} threadsPerThreadgroup:{1, 1, 1}];
    
    [computeEncoder setComputePipelineState:_pipelineStatePhotonRadius];
    [computeEncoder setBuffer:_complex_buffer     offset:0 atIndex:0];
    [computeEncoder setBuffer:_cameraRecordBuffer offset:0 atIndex:1];
    [computeEncoder dispatchThreads:{1920, 1080, 1} threadsPerThreadgroup:{8, 8 ,1}];
    
    [computeEncoder endEncoding];
    [commandBuffer commit];
}

- (void)photonWork:(MTKView *)view
{
    auto commandBuffer = [_commandQueue commandBuffer]; //commandBuffer.label = @"name";
    auto computeEncoder = [commandBuffer computeCommandEncoder];
    
    let tex_index = predefined_index[_complex.frame_count % 2];
    [computeEncoder setComputePipelineState:_pipelineStatePhotonRecording];
    [computeEncoder setTexture:_textureARNG atIndex: tex_index[2]];
    [computeEncoder setTexture:_textureBRNG atIndex: tex_index[3]];
    
    [computeEncoder setBuffer:_camera_buffer offset:0 atIndex:0];
    [computeEncoder setBuffer:_complex_buffer offset:0 atIndex:1];
    
    [computeEncoder setBuffer:_photonRecordBuffer offset:0 atIndex:2];
    
    [computeEncoder useHeap:_heap];
    [computeEncoder setBuffer:_argumentBufferPri offset:0 atIndex:7];
    [computeEncoder setBuffer:_argumentBufferEnv offset:0 atIndex:8];
    [computeEncoder setBuffer:_argumentBufferPBR offset:0 atIndex:9];
    
    [computeEncoder dispatchThreads:{512, 512, 1} threadsPerThreadgroup:{8, 8, 1}];
    
    [computeEncoder setComputePipelineState:_pipelineStatePhotonHashing];
    [computeEncoder setBuffer:_complex_buffer offset:0 atIndex:1];
    [computeEncoder setBuffer:_photonRecordBuffer offset:0 atIndex:2];
    [computeEncoder setBuffer:_photonHashedBuffer offset:0 atIndex:3];
    
    [computeEncoder dispatchThreads:{512, 512, 1} threadsPerThreadgroup:{8, 8, 1}];
    [computeEncoder endEncoding];
    
    //[commandBuffer commit];
    //[commandBuffer waitUntilCompleted];
    //commandBuffer = [_commandQueue commandBuffer];
    
    auto _photonCountRenderEncoder = [commandBuffer renderCommandEncoderWithDescriptor:_renderPhotonCountPassDescriptor];
    _photonCountRenderEncoder.label = @"PhotonCount Render Pass";
    [_photonCountRenderEncoder setRenderPipelineState:_renderPhotonCountPipelineState];
    
    [_photonCountRenderEncoder setVertexBuffer:_photonHashedBuffer offset:0 atIndex:0];
    [_photonCountRenderEncoder setVertexBuffer:_photonRecordBuffer offset:0 atIndex:1];
    
    [_photonCountRenderEncoder drawPrimitives:MTLPrimitiveTypePoint
                                  vertexStart:0 vertexCount:512*512];
    [_photonCountRenderEncoder endEncoding];
    
    computeEncoder = [commandBuffer computeCommandEncoder];
    [computeEncoder setComputePipelineState:_pipelineStatePhotonRefine];
    [computeEncoder setBuffer:_complex_buffer offset:0 atIndex:0];
    [computeEncoder setBuffer:_cameraRecordBuffer offset:0 atIndex:1];
    [computeEncoder setBuffer:_photonRecordBuffer offset:0 atIndex:2];
    
    [computeEncoder setTexture:_texturePhotonMark atIndex:0];
    [computeEncoder setTexture:_texturePhotonCount atIndex:1];
    
    if (_complex.frame_count % 2) {
        [computeEncoder setTexture:self->_textureA atIndex:2];
    } else {
        [computeEncoder setTexture:self->_textureB atIndex:2];
    }
    
    [computeEncoder dispatchThreads:{1920, 1080, 1} threadsPerThreadgroup:{8, 8, 1}];
    [computeEncoder endEncoding];
    
    //commandBuffer = [_commandQueue commandBuffer];
    
    {
        let blit = [commandBuffer blitCommandEncoder];
        if (_complex.frame_count % 2) {
            [blit generateMipmapsForTexture:self->_textureA];
        } else {
            [blit generateMipmapsForTexture:self->_textureB];
        }
        [blit endEncoding];
    }
    
    [commandBuffer addCompletedHandler:^(id<MTLCommandBuffer> buffer) {
        // not on main thread
        if (self->_dragging) {
            self->_complex.frame_count = 0;
        } else {
            let fcount = self->_complex.frame_count;
            self->_complex.frame_count = fcount + 1;
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
    
    //let tex_index = predefined_index[_complex.frame_count % 2];
    let renderPassDescriptor = _view.currentRenderPassDescriptor;
    let renderEncoder = [commandBuffer renderCommandEncoderWithDescriptor:renderPassDescriptor];
    
    let viewsize = _complex.view_size;
    
    MTLViewport viewport {0, 0, viewsize.x, viewsize.y, 0, 1.0};
    
    [renderEncoder setViewport:viewport];
    [renderEncoder setRenderPipelineState:_pipelineStatePostprocessing];
    
    [renderEncoder setVertexBuffer:_canvas_buffer offset:0 atIndex:0];
    
    [renderEncoder setFragmentBuffer:_complex_buffer offset:0 atIndex:0];
    [renderEncoder setFragmentTexture:_textureB atIndex: tex_index[0]];
    [renderEncoder setFragmentTexture:_textureA atIndex: tex_index[1]];
    
    [renderEncoder drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:6];
    [renderEncoder endEncoding];
    
    let drawable = _view.currentDrawable;
    [commandBuffer presentDrawable:drawable];
    [commandBuffer commit];
}

- (void)photon:(MTKView *)view
{
    let time = [[NSDate date] timeIntervalSince1970];
    _complex.running_time = time - launchTime;
    
    if (_complex.frame_count == 0) {
        [self photonPrepare:view];
    }
    [self photonWork:view];
    
//    // test kernel result between CPU and GPU
//    id <MTLBlitCommandEncoder> blitCommandEncoder = [commandBuffer blitCommandEncoder];
//    [blitCommandEncoder synchronizeResource:_cameraBoundsBuffer];
//    [blitCommandEncoder endEncoding];
    
//    {
//        let w = _computePipelineState.threadExecutionWidth
//        let h = _computePipelineState.maxTotalThreadsPerThreadgroup / w
//        let threadsPerThreadgroup = MTLSizeMake(w, h, 1)
//
//        let threadgroupsPerGrid = MTLSize(width: (_textureA.width + w - 1) / w,
//                                          height: (_textureA.height + h - 1) / h, depth: 1)
//
//        computeEncoder.dispatchThreadgroups(threadgroupsPerGrid, threadsPerThreadgroup: threadsPerThreadgroup)
//    }
}

- (void)drawInMTKView:(nonnull MTKView *)view
{
    @autoreleasepool {
        //[self render:view];
        [self photon:view];
    }
}

- (void)drag:(float2)delta state:(BOOL)ended;
{
    _dragging = !ended;
    
    let ratio = delta / _complex.view_size;
    
    _camera_rotation += ratio;
    
    self->_complex.frame_count = 0;
    self->_complex.running_time = 0;
    
    prepareCamera(&_camera, _complex.tex_size, _camera_rotation);
}

- (void)render:(MTKView *)view
{
    let commandBuffer = [_commandQueue commandBuffer];
    let time = [[NSDate date] timeIntervalSince1970];
    _complex.running_time = time - launchTime;
    
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
            self->_complex.frame_count = 0;
        } else {
            let fcount = self->_complex.frame_count;
            self->_complex.frame_count = fcount + 1;
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
    [computeEncoder setComputePipelineState:_pipelineStatePathTracing];
 
    let tex_index = predefined_index[_complex.frame_count % 2];
    
    [computeEncoder setTexture:_textureA atIndex: tex_index[0]];
    [computeEncoder setTexture:_textureB atIndex: tex_index[1]];
    
    [computeEncoder setTexture:_textureARNG atIndex: tex_index[2]];
    [computeEncoder setTexture:_textureBRNG atIndex: tex_index[3]];
    
    if (self->_complex.frame_count > 3 || self->_complex.frame_count < 1) {
        memcpy(_complex_buffer.contents, &_complex, sizeof(Complex));
    }
    [computeEncoder setBuffer:_complex_buffer offset:0 atIndex:1];
    
    memcpy(_camera_buffer.contents, &_camera, sizeof(Camera));
    [computeEncoder setBuffer:_camera_buffer offset:0 atIndex:0];
    
    [computeEncoder useHeap:_heap];
    
    [computeEncoder setBuffer:_argumentBufferPri offset:0 atIndex:7];
    [computeEncoder setBuffer:_argumentBufferEnv offset:0 atIndex:8];
    [computeEncoder setBuffer:_argumentBufferPBR offset:0 atIndex:9];
    
    let _threadGroupSize = MTLSizeMake(8, 8, 1);
    let _threadGridSize = MTLSize {_textureA.width, _textureA.height, 1};
    
    [computeEncoder dispatchThreads:_threadGridSize threadsPerThreadgroup:_threadGroupSize];
    [computeEncoder endEncoding];
    
//    {
//        let w = _computePipelineState.threadExecutionWidth
//        let h = _computePipelineState.maxTotalThreadsPerThreadgroup / w
//        let threadsPerThreadgroup = MTLSizeMake(w, h, 1)
//
//        let threadgroupsPerGrid = MTLSize(width: (_textureA.width + w - 1) / w,
//                                          height: (_textureA.height + h - 1) / h, depth: 1)
//
//        computeEncoder.dispatchThreadgroups(threadgroupsPerGrid, threadsPerThreadgroup: threadsPerThreadgroup)
//    }
    
    let renderPassDescriptor = _view.currentRenderPassDescriptor;
    let renderEncoder = [commandBuffer renderCommandEncoderWithDescriptor:renderPassDescriptor];
    
    let& viewsize = _complex.view_size;
    
    MTLViewport viewport {0, 0, viewsize.x, viewsize.y, 0, 1.0};
    
    [renderEncoder setViewport:viewport];
    [renderEncoder setRenderPipelineState:_pipelineStatePostprocessing];
    
    [renderEncoder setVertexBuffer:_canvas_buffer offset:0 atIndex:0];
    
    [renderEncoder setFragmentBuffer:_complex_buffer offset:0 atIndex:0];
    [renderEncoder setFragmentTexture:_textureB atIndex: tex_index[0]];
    [renderEncoder setFragmentTexture:_textureA atIndex: tex_index[1]];
    
    [renderEncoder drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:6];
    [renderEncoder endEncoding];
    
    let drawable = _view.currentDrawable;
    [commandBuffer presentDrawable:drawable];
    [commandBuffer commit];
}

- (void) createHeap
{
    MTLHeapDescriptor *heapDescriptor = [MTLHeapDescriptor new];
    heapDescriptor.storageMode = MTLStorageModePrivate;
    heapDescriptor.size =  0;

    // Build a descriptor for each texture and calculate the size required to store all textures in the heap
    for(uint32_t i = 0; i < _vectorTexAll.size(); i++)
    {
        // Create a descriptor using the texture's properties
        MTLTextureDescriptor *descriptor = [AAPLRenderer newDescriptorFromTexture:_vectorTexAll[i]
                                                                      storageMode:heapDescriptor.storageMode];

        // Determine the size required for the heap for the given descriptor
        MTLSizeAndAlign sizeAndAlign = [_device heapTextureSizeAndAlignWithDescriptor:descriptor];

        // Align the size so that more resources will fit in the heap after this texture
        sizeAndAlign.size += (sizeAndAlign.size & (sizeAndAlign.align - 1)) + sizeAndAlign.align;

        // Accumulate the size required to store this texture in the heap
        heapDescriptor.size += sizeAndAlign.size;
    }
    
    // Calculate the size required to store all buffers in the heap
    for(uint32_t i = 0; i < _vectorBufferAll.size(); i++)
    {
        // Determine the size required for the heap for the given buffer size
        MTLSizeAndAlign sizeAndAlign = [_device heapBufferSizeAndAlignWithLength:_vectorBufferAll[i].length
                                                                         options:MTLResourceStorageModePrivate];

        // Align the size so that more resources will fit in the heap after this buffer
        sizeAndAlign.size +=  (sizeAndAlign.size & (sizeAndAlign.align - 1)) + sizeAndAlign.align;

        // Accumulate the size required to store this buffer in the heap
        heapDescriptor.size += sizeAndAlign.size;
    }

    // Create a heap large enough to store all resources
    _heap = [_device newHeapWithDescriptor:heapDescriptor];
}

- (void)copyToHeap
{
    // Create a command buffer and blit encoder to copy data from the existing resources to
    // the new resources created from the heap
    id <MTLCommandBuffer> commandBuffer = [_commandQueue commandBuffer];
    commandBuffer.label = @"Heap Copy Command Buffer";

    id <MTLBlitCommandEncoder> blitEncoder = commandBuffer.blitCommandEncoder;
    blitEncoder.label = @"Heap Transfer Blit Encoder";

    // Create new textures from the heap and copy the contents of the existing textures to
    // the new textures
    for(uint32_t i = 0; i < _vectorTexAll.size(); i++)
    {
        // Create a descriptor using the texture's properties
        MTLTextureDescriptor *descriptor = [AAPLRenderer newDescriptorFromTexture:_vectorTexAll[i]
                                                                      storageMode:_heap.storageMode];

        // Create a texture from the heap
        id<MTLTexture> heapTexture = [_heap newTextureWithDescriptor:descriptor];

        heapTexture.label = _vectorTexAll[i].label;

        [blitEncoder pushDebugGroup:[NSString stringWithFormat:@"%@ Blits", heapTexture.label]];

        // Blit every slice of every level from the existing texture to the new texture
        MTLRegion region = MTLRegionMake2D(0, 0, _vectorTexAll[i].width, _vectorTexAll[i].height);
        for(NSUInteger level = 0; level < _vectorTexAll[i].mipmapLevelCount;  level++)
        {

            [blitEncoder pushDebugGroup:[NSString stringWithFormat:@"Level %lu Blit", level]];

            for(NSUInteger slice = 0; slice < _vectorTexAll[i].arrayLength; slice++)
            {
                [blitEncoder copyFromTexture:_vectorTexAll[i]
                                 sourceSlice:slice
                                 sourceLevel:level
                                sourceOrigin:region.origin
                                  sourceSize:region.size
                                   toTexture:heapTexture
                            destinationSlice:slice
                            destinationLevel:level
                           destinationOrigin:region.origin];
            }
            region.size.width /= 2;
            region.size.height /= 2;
            if(region.size.width == 0) region.size.width = 1;
            if(region.size.height == 0) region.size.height = 1;

            [blitEncoder popDebugGroup];
        }

        [blitEncoder popDebugGroup];

        // Replace the existing texture with the new texture
        _vectorTexAll[i] = heapTexture;
    }

    // Create new buffers from the heap and copy the contents of existing buffers to the
    // new buffers
    for(uint32_t i = 0; i < _vectorBufferAll.size(); i++)
    {
        // Create a buffer from the heap
        id<MTLBuffer> heapBuffer = [_heap newBufferWithLength:_vectorBufferAll[i].length
                                                      options:MTLResourceStorageModePrivate];

        heapBuffer.label = _vectorBufferAll[i].label;

        // Blit contents of the original buffer to the new buffer
        [blitEncoder copyFromBuffer:_vectorBufferAll[i]
                       sourceOffset:0
                           toBuffer:heapBuffer
                  destinationOffset:0
                               size:heapBuffer.length];

        // Replace the existing buffer with the new buffer
        _vectorBufferAll[i] = heapBuffer;
    }

    [blitEncoder endEncoding];
    [commandBuffer commit];
}

+ (nonnull MTLTextureDescriptor*) newDescriptorFromTexture:(nonnull id<MTLTexture>)texture
                                               storageMode:(MTLStorageMode)storageMode
{
    MTLTextureDescriptor * descriptor = [MTLTextureDescriptor new];

    descriptor.textureType      = texture.textureType;
    descriptor.pixelFormat      = texture.pixelFormat;
    descriptor.width            = texture.width;
    descriptor.height           = texture.height;
    descriptor.depth            = texture.depth;
    descriptor.mipmapLevelCount = texture.mipmapLevelCount;
    descriptor.arrayLength      = texture.arrayLength;
    descriptor.sampleCount      = texture.sampleCount;
    descriptor.storageMode      = storageMode;

    return descriptor;
}

@end
