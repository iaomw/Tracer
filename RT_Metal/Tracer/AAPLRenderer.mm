#import <SceneKit/SceneKit.h>
#import <ModelIO/ModelIO.h>

#include "AAPLRenderer.hh"

#include "Medium.hh"
#include "Tracer.hh"

#include "minipbrt.h"

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
    
} Complex;

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
    
    id<MTLRenderPipelineState> _renderPipelineState;
    id<MTLComputePipelineState> _computePipelineState;
    
    id<MTLTexture> _textureA;
    id<MTLTexture> _textureB;
    id<MTLTexture> _textureARNG;
    id<MTLTexture> _textureBRNG;
    
    id<MTLTexture> _textureUVT;
    id<MTLTexture> _textureHDR;
    
    std::vector<id<MTLTexture>> vectorTexPBR;
    std::vector<id<MTLTexture>> _vectorTexAll;
    
    std::vector<id<MTLBuffer>> _vectorBufferAll;
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
        
        _view.colorPixelFormat = MTLPixelFormatRGBA16Float;
        //_view.colorspace = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
        //_view.colorspace = CGColorSpaceCreateWithName(kCGColorSpaceLinearSRGB);
        //_view.colorspace = CGColorSpaceCreateWithName(kCGColorSpaceExtendedSRGB);
        //_view.colorspace = CGColorSpaceCreateWithName(kCGColorSpaceExtendedLinearSRGB);
        
        NSError* ERROR;

        let defaultLibrary = [_device newDefaultLibrary];
        let kernelFunction = [defaultLibrary newFunctionWithName:@"tracerKernel"];
        
        _computePipelineState = [_device newComputePipelineStateWithFunction:kernelFunction error:&ERROR];
        
        let argumentEncoderPri = [kernelFunction newArgumentEncoderWithBufferIndex:7];
        let argumentBufferLengthPri = argumentEncoderPri.encodedLength;
        _argumentBufferPri = [_device newBufferWithLength:argumentBufferLengthPri options:0];
        _argumentBufferPri.label = @"Argument Pri";
        
        let argumentEncoderEnv = [kernelFunction newArgumentEncoderWithBufferIndex:8];
        let argumentBufferLengthEnv = argumentEncoderEnv.encodedLength;
        _argumentBufferEnv = [_device newBufferWithLength:argumentBufferLengthEnv options:0];
        _argumentBufferEnv.label = @"Argument Env";
        
        let argumentEncoderPBR = [kernelFunction newArgumentEncoderWithBufferIndex:9];
        let argumentBufferLengthPBR = argumentEncoderPBR.encodedLength * 2;
        _argumentBufferPBR = [_device newBufferWithLength:argumentBufferLengthPBR options:0];
        _argumentBufferPBR.label = @"Argument PBR";
        
        let vertexFunction = [defaultLibrary newFunctionWithName:@"vertexShader"];
        let fragmentFunction = [defaultLibrary newFunctionWithName:@"fragmentShader"];
        
        #if TARGET_OS_OSX
            let CommonStorageMode = MTLResourceStorageModeManaged;
        #else
            let CommonStorageMode = MTLResourceStorageModeShared;
        #endif
        
        _canvas_buffer = [_device newBufferWithBytes:canvas length:sizeof(VertexWithUV)*6 options: CommonStorageMode];
        
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
                                                options: CommonStorageMode];
        
        std::vector<Square> cornell_box;
        prepareCornellBox(cornell_box, materials);
        _square_list_buffer = [_device newBufferWithBytes: cornell_box.data()
                                                   length: sizeof(Square)*cornell_box.size()
                                                  options: CommonStorageMode];
        
        std::vector<Sphere> sphere_list;
        prepareSphereList(sphere_list, materials);
        _sphere_list_buffer = [_device newBufferWithBytes: sphere_list.data()
                                                   length: sizeof(Sphere)*sphere_list.size()
                                                  options: CommonStorageMode];
        
        Material pbr;
        pbr.type = MaterialType::Glass;
        pbr.medium = MediumType::Homogeneous;
        
        pbr.textureInfo.type = TextureType::Constant;
        pbr.textureInfo.albedo = float3 {1, 1, 1};
        
        materials.emplace_back(pbr);
        
        _material_buffer = [_device newBufferWithBytes: materials.data()
                                                length: sizeof(Material)*materials.size()
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
        
        MTKTextureLoader *textureLoader = [[MTKTextureLoader alloc] initWithDevice: _device];
        
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
        
        _textureHDR = [textureLoader newTextureWithData:imageData options:textureLoaderOptions error:&ERROR];
        
        let mdlUVT = [MDLTexture textureNamed:@"uv_test/uv_test.png"];
        _textureUVT = [textureLoader newTextureWithMDLTexture:mdlUVT options:textureLoaderOptions error:&ERROR];
        
        let mdlAO = [MDLTexture textureNamed:@"coatball/tex_ao.png"];
        var _textureAO = [textureLoader newTextureWithMDLTexture:mdlAO options:textureLoaderOptions error:&ERROR];
        
        var mdlAlbedo = [MDLTexture textureNamed:@"coatball/tex_base.png"];
        var mdlNormal = [MDLTexture textureNamed:@"coatball/tex_normal.png"];
        var mdlMetallic = [MDLTexture textureNamed:@"coatball/tex_metallic.png"];
        var mdlRoughness = [MDLTexture textureNamed:@"coatball/tex_roughness.png"];
        
        var _textureAlbedo = [textureLoader newTextureWithMDLTexture:mdlAlbedo options:textureLoaderOptions error:&ERROR];
        var _textureNormal = [textureLoader newTextureWithMDLTexture:mdlNormal options:textureLoaderOptions error:&ERROR];
        var _textureMetallic = [textureLoader newTextureWithMDLTexture:mdlMetallic options:textureLoaderOptions error:&ERROR];
        var _textureRoughness = [textureLoader newTextureWithMDLTexture:mdlRoughness options:textureLoaderOptions error:&ERROR];
        
        auto tmp = std::vector<id<MTLTexture>>{ _textureAO, _textureAlbedo, _textureNormal, _textureMetallic, _textureRoughness};
        vectorTexPBR.insert(vectorTexPBR.end(), std::begin(tmp), std::end(tmp));
                
            mdlAlbedo = [MDLTexture textureNamed:@"scuffed/gold-scuffed_base.png"];
            mdlNormal = [MDLTexture textureNamed:@"scuffed/gold-scuffed_normal.png"];
            mdlMetallic = [MDLTexture textureNamed:@"scuffed/gold-scuffed_metallic.png"];
            mdlRoughness = [MDLTexture textureNamed:@"scuffed/gold-scuffed_roughness.png"];

            _textureAlbedo = [textureLoader newTextureWithMDLTexture:mdlAlbedo options:textureLoaderOptions error:&ERROR];
            _textureNormal = [textureLoader newTextureWithMDLTexture:mdlNormal options:textureLoaderOptions error:&ERROR];
            _textureMetallic = [textureLoader newTextureWithMDLTexture:mdlMetallic options:textureLoaderOptions error:&ERROR];
            _textureRoughness = [textureLoader newTextureWithMDLTexture:mdlRoughness options:textureLoaderOptions error:&ERROR];
        
        tmp = std::vector<id<MTLTexture>>{ _textureAO, _textureAlbedo, _textureNormal, _textureMetallic, _textureRoughness};
        vectorTexPBR.insert(vectorTexPBR.end(), std::begin(tmp), std::end(tmp));
        
            if(!_textureHDR)
            {
                NSLog(@"Failed to create the texture from %@", pathHDR);
                return nil;
            }
        
        _vectorTexAll = {_textureHDR, _textureUVT};
        _vectorTexAll.insert(_vectorTexAll.end(), std::begin(vectorTexPBR), std::end(vectorTexPBR));
        
        std::vector<BVH> bvh_list;
        
//        for (int i=1; i<sphere_list.size(); i++) {
//            auto& sphere = sphere_list[i];
//            BVH::buildNode(sphere.boundingBOX, sphere.model_matrix, PrimitiveType::Sphere, i, bvh_list);
//        }

            for (int i=0; i<cube_list.size(); i++) {
                auto& cube = cube_list[i];
                BVH::buildNode(cube.box, cube.model_matrix, PrimitiveType::Cube, i, bvh_list);
            }

        for (int i=4; i<7; i++) {
        //for (int i=0; i<cornell_box.size(); i++) {
            auto& square = cornell_box[i];
            BVH::buildNode(square.boundingBOX, square.model_matrix, PrimitiveType::Square, i, bvh_list);
        }
        
        //let modelPath = [NSBundle.mainBundle pathForResource:@"coatball/coatball" ofType:@"obj"];
        let modelPath = [NSBundle.mainBundle pathForResource:@"meshes/dragon" ofType:@"obj"];
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
            
            auto meshScale = 500.0 / maxDime;
            auto meshOffset = float3(278)-centroid;
            meshOffset.y = 20 - minB.y * meshScale;
        
            auto vertex_ptr = (MeshStrut*) testMesh.vertexBuffers.firstObject.map.bytes;
        
            int totalIndexBufferLength = 0, triangleIndexOffset = 0;
        
            for(MDLSubmesh* submesh in testMesh.submeshes) {
                
                auto index_ptr = (uint32_t*) submesh.indexBuffer.map.bytes;
                auto index_count = submesh.indexCount;
                
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
                        
                        ele->vx += 300;
                        
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
                    
                    let triangleIndex = triangleIndexOffset + i/3;
                    
                    //BVH::buildNode(box, matrix_identity_float4x4, PrimitiveType::Triangle, triangleIndex, bvh_list);
                }
                
                totalIndexBufferLength += submesh.indexBuffer.length; triangleIndexOffset += index_count/3;
            }
        
            char* totalIndexData = (char*)malloc(totalIndexBufferLength);
        
            int totalIndexOffset = 0;
            for(MDLSubmesh* submesh in testMesh.submeshes) {
                let length = submesh.indexBuffer.length;
                memcpy(totalIndexData + totalIndexOffset, submesh.indexBuffer.map.bytes, length);
                totalIndexOffset += length;
            }
        
                NSLog(@"Begin processing BVH");
                let time_s = [[NSDate date] timeIntervalSince1970];
                BVH::buildTree(bvh_list);
                let time_e = [[NSDate date] timeIntervalSince1970];
                NSLog(@"Time cost %fs", time_e - time_s);
                NSLog(@"End processing BVH");
                
                _idx_buffer = [_device newBufferWithBytes: totalIndexData //[testMesh.submeshes.firstObject indexBuffer].map.bytes
                                                   length: totalIndexOffset //[testMesh.submeshes.firstObject indexBuffer].length
                                                  options: CommonStorageMode]; free(totalIndexData);
                
                _tri_buffer = [_device newBufferWithBytes: testMesh.vertexBuffers.firstObject.map.bytes
                                                   length: testMesh.vertexBuffers.firstObject.length
                                                  options: CommonStorageMode];
                
                _bvh_buffer = [_device newBufferWithBytes: bvh_list.data()
                                                   length: sizeof(BVH)*bvh_list.size()
                                                  options: CommonStorageMode];
        
        
                minipbrt::Loader loaderPBRT;
                auto pathPBRT = [NSBundle.mainBundle pathForResource:@"cloud/cloud" ofType:@"pbrt"];
        
                if (loaderPBRT.load([pathPBRT UTF8String])) {
                    minipbrt::Scene* scene = loaderPBRT.take_scene();
                    auto* medium = dynamic_cast<minipbrt::HeterogeneousMedium*>(scene->mediums[0]);
                    
                    auto size_grid = sizeof(float) * medium->nx * medium->ny * medium->nz;
                    auto info_grid = GridDensityInfo(10, 90, 0.5, medium->nx, medium->ny, medium->nz, medium->density);
                    
                    _densityInfoBuffer = [_device newBufferWithBytes: &info_grid length: sizeof(info_grid) options: CommonStorageMode];
                    _densityDataBuffer = [_device newBufferWithBytes: medium->density length: size_grid options: CommonStorageMode];
                    
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
        
        _vectorBufferAll = { _cube_list_buffer, _square_list_buffer, _sphere_list_buffer,
                                _bvh_buffer, _idx_buffer, _tri_buffer, _material_buffer,
                                _densityInfoBuffer, _densityDataBuffer };
        
        [self createHeap];
        [self copyToHeap];
        
        _cube_list_buffer = _vectorBufferAll[0];
        _square_list_buffer = _vectorBufferAll[1];
        _sphere_list_buffer = _vectorBufferAll[2];
        
        _bvh_buffer = _vectorBufferAll[3];
        _idx_buffer = _vectorBufferAll[4];
        _tri_buffer = _vectorBufferAll[5];
        
        _material_buffer = _vectorBufferAll[6];
        
        _densityInfoBuffer = _vectorBufferAll[7];
        _densityDataBuffer = _vectorBufferAll[8];
        
        _vectorBufferAll.clear();
        
        std::copy(vectorTexPBR.begin(), vectorTexPBR.end(), _vectorTexAll.begin()+2);
        
        for (int i=0; i<vectorTexPBR.size(); i+=5) {
            
            [argumentEncoderPBR setArgumentBuffer:_argumentBufferPBR startOffset:0 arrayElement:i/5];

            [argumentEncoderPBR setTexture:vectorTexPBR[i+0] atIndex:0];
            [argumentEncoderPBR setTexture:vectorTexPBR[i+1] atIndex:1];
            [argumentEncoderPBR setTexture:vectorTexPBR[i+2] atIndex:2];
            [argumentEncoderPBR setTexture:vectorTexPBR[i+3] atIndex:3];
            [argumentEncoderPBR setTexture:vectorTexPBR[i+4] atIndex:4];
        }
        
        _textureHDR = _vectorTexAll[0];
        _textureUVT = _vectorTexAll[1];
        
        _vectorTexAll.clear();
        
        _textureAO = NULL;
        _textureAlbedo = NULL;
        _textureNormal = NULL;
        _textureMetallic = NULL;
        _textureRoughness = NULL;
        
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

-(void)render:(MTKView *)view
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
    [computeEncoder setComputePipelineState:_computePipelineState];
 
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
    [renderEncoder setRenderPipelineState:_renderPipelineState];
    
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

- (void)drawInMTKView:(nonnull MTKView *)view
{
    @autoreleasepool {
        [self render:view];
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
