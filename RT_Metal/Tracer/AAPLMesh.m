/*
See LICENSE folder for this sample’s licensing information.

Abstract:
Implementation of mesh and submesh objects used for managing model data.
*/
@import MetalKit;
@import ModelIO;

#import "AAPLMesh.h"
#import "AAPLShaderTypes.h"

@implementation AAPLSubmesh {
    NSMutableArray<id<MTLTexture>> *_textures;
}

@synthesize textures = _textures;

+ (nonnull id<MTLTexture>) createMetalTextureFromMaterial:(nonnull MDLMaterial *)material
                                  modelIOMaterialSemantic:(MDLMaterialSemantic)materialSemantic
                                    metalKitTextureLoader:(nonnull MTKTextureLoader *)textureLoader
{
    id<MTLTexture> texture;

    NSArray<MDLMaterialProperty *> *propertiesWithSemantic =
        [material propertiesWithSemantic:materialSemantic];

    for (MDLMaterialProperty *property in propertiesWithSemantic)
    {
        if(property.type == MDLMaterialPropertyTypeString ||
           property.type == MDLMaterialPropertyTypeURL)
        {
            // Load the textures with shader read using private storage
            NSDictionary *textureLoaderOptions =
            @{
              MTKTextureLoaderOptionTextureUsage       : @(MTLTextureUsageShaderRead),
              MTKTextureLoaderOptionTextureStorageMode : @(MTLStorageModePrivate),
              MTKTextureLoaderOptionSRGB : @(NO)
              };

            // First will interpret the string as a file path and attempt to load it with
            //    -[MTKTextureLoader newTextureWithContentsOfURL:options:error:]
            
            NSURL *url = property.URLValue;
            NSMutableString *URLString = nil;
            if(property.type == MDLMaterialPropertyTypeURL) {
                URLString = [[NSMutableString alloc] initWithString:[url absoluteString]];
            } else {
                URLString = [[NSMutableString alloc] initWithString:@"file://"];
                [URLString appendString:property.stringValue];
            }

            NSURL *textureURL = [NSURL URLWithString:URLString];
            // Attempt to load the texture from the file system
            texture = [textureLoader newTextureWithContentsOfURL:textureURL
                                                         options:textureLoaderOptions
                                                           error:nil];

            // If MetalKit foune found a texture using the string as a file path name...
            if(texture) {
                // ...return it
                return texture;
            }

            // If MetalKit  did not find a texture by interpreting the string as a path, interpret
            //   the last component of the string as an asset catalog name and attempt to load it
            //   with -[MTKTextureLoader newTextureWithName:scaleFactor:bundle:options::error:]

            NSString *lastComponent =
                [[URLString componentsSeparatedByString:@"/"] lastObject];

            texture = [textureLoader newTextureWithName:lastComponent
                                            scaleFactor:1.0
                                                 bundle:nil
                                                options:textureLoaderOptions
                                                  error:nil];

            // If there exists a texture with the string in the asset catalog...
            if(texture) {
                // ...return it
                return texture;
            }
            

            // If the texture loader did not find a texture when using the string as a name,
            //  something went wrong (Perhaps the file was missing or
            //  misnamed in the asset catalog, model/material file, or file system)

            // Depending on how the Metal render pipeline use with this submesh is implemented,
            //   this condition can be handled more gracefully.  The app could load a dummy texture
            //   that will look okay when set with the pipelin or ensure that  that the pipeline
            //   rendeirng this submesh does not require a material with this property.
           
            [NSException raise:@"Texture data for material property not found"
                        format:@"Requested material property semantic: %lu string: %@",
                                materialSemantic, property.stringValue];
        }
    }

   [NSException raise:@"No appropriate material property from which to create texture"
                format:@"Requested material property semantic: %lu", materialSemantic];

    return nil;
}

- (nonnull instancetype) initWithModelIOSubmesh:(nonnull MDLSubmesh *)modelIOSubmesh
                                metalKitSubmesh:(nonnull MTKSubmesh *)metalKitSubmesh
                          metalKitTextureLoader:(nonnull MTKTextureLoader *)textureLoader
{
    self = [super init];
    if(self)
    {
        _metalKitSubmmesh = metalKitSubmesh;

        _textures = [[NSMutableArray alloc] initWithCapacity:AAPLNumTextureIndices];

        // Fill up the texture array with null objects which will later be replaced
        for(NSUInteger shaderIndex = 0; shaderIndex < AAPLNumTextureIndices; shaderIndex++) {
            [_textures addObject:(id<MTLTexture>)[NSNull null]];
        }

        // Set each index in the array with the appropriate material semantic specified in the
        //   submesh's material property

        _textures[AAPLTextureIndexBaseColor] =
            [AAPLSubmesh createMetalTextureFromMaterial:modelIOSubmesh.material
                                modelIOMaterialSemantic:MDLMaterialSemanticBaseColor
                                  metalKitTextureLoader:textureLoader];

        _textures[AAPLTextureIndexSpecular] =
            [AAPLSubmesh createMetalTextureFromMaterial:modelIOSubmesh.material
                                modelIOMaterialSemantic:MDLMaterialSemanticSpecular
                                  metalKitTextureLoader:textureLoader];

        _textures[AAPLTextureIndexNormal] =
            [AAPLSubmesh createMetalTextureFromMaterial:modelIOSubmesh.material
                                modelIOMaterialSemantic:MDLMaterialSemanticTangentSpaceNormal
                                  metalKitTextureLoader:textureLoader];
    }
    
    return self;
}

@end


@implementation AAPLMesh {
    NSMutableArray<AAPLSubmesh *> *_submeshes;
}

@synthesize submeshes = _submeshes;

- (nonnull instancetype) initWithModelIOMesh:(nonnull MDLMesh *)modelIOMesh
                     modelIOVertexDescriptor:(nonnull MDLVertexDescriptor *)vertexDescriptor
                       metalKitTextureLoader:(nonnull MTKTextureLoader *)textureLoader
                                 metalDevice:(nonnull id<MTLDevice>)device
                                       error:(NSError * __nullable * __nullable)error
{
    self = [super init];
    if(!self) {
        return nil;
    }

    // Have ModelIO create the tangents from mesh texture coordinates and normals
    [modelIOMesh addTangentBasisForTextureCoordinateAttributeNamed:MDLVertexAttributeTextureCoordinate
                                              normalAttributeNamed:MDLVertexAttributeNormal
                                             tangentAttributeNamed:MDLVertexAttributeTangent];

    // Have ModelIO create bitangents from mesh texture coordinates and the newly created tangents
    [modelIOMesh addTangentBasisForTextureCoordinateAttributeNamed:MDLVertexAttributeTextureCoordinate
                                             tangentAttributeNamed:MDLVertexAttributeTangent
                                           bitangentAttributeNamed:MDLVertexAttributeBitangent];

    // Apply the ModelIO vertex descriptor created to match the Metal vertex descriptor.
    // Assigning a new vertex descriptor to a ModelIO mesh performs a re-layout of the vertex
    //   vertex data.  In this case we created the ModelIO vertex descriptor so that the layout
    //   of the vertices in the ModelIO mesh match the layout of vertices the Metal render pipeline
    //   expects as input into its vertex shader
    // Note that this re-layout operation can only be performed after tangents and
    //   bitangents have been created.  This is because Model IO's addTangentBasis methods only works
    //   with vertex data is all in 32-bit floating-point.  The vertex descriptor applied here cans
    //   change those floats into 16-bit floats or other types from which ModelIO cannot produce
    //   tangents

    modelIOMesh.vertexDescriptor = vertexDescriptor;

    // Create the metalKit mesh which will contain the Metal buffer(s) with the mesh's vertex data
    //   and submeshes with info to draw the mesh
    MTKMesh* metalKitMesh = [[MTKMesh alloc] initWithMesh:modelIOMesh
                                                   device:device
                                                    error:error];

    _metalKitMesh = metalKitMesh;

    // There should always be the same number of MetalKit submeshes in the MetalKit mesh as there
    //   are Mode lIO submesnes in the ModelIO mesh
    assert(metalKitMesh.submeshes.count == modelIOMesh.submeshes.count);

    // Create an array to hold this AAPLMesh object's AAPLSubmesh objects
    _submeshes = [[NSMutableArray alloc] initWithCapacity:metalKitMesh.submeshes.count];

    // Create an AAPLSubmesh object for each submesh and a add it to the submeshes array
    for(NSUInteger index = 0; index < metalKitMesh.submeshes.count; index++)
    {
        // Create an app specifc submesh to hold the MetalKit submesh
        AAPLSubmesh *submesh =
        [[AAPLSubmesh alloc] initWithModelIOSubmesh:modelIOMesh.submeshes[index]
                                    metalKitSubmesh:metalKitMesh.submeshes[index]
                              metalKitTextureLoader:textureLoader];

        [_submeshes addObject:submesh];
    }

    return self;
}

+ (NSArray<AAPLMesh*> *) newMeshesFromObject:(nonnull MDLObject*)object
                     modelIOVertexDescriptor:(nonnull MDLVertexDescriptor*)vertexDescriptor
                       metalKitTextureLoader:(nonnull MTKTextureLoader *)textureLoader
                                 metalDevice:(nonnull id<MTLDevice>)device
                                       error:(NSError * __nullable * __nullable)error {

    NSMutableArray<AAPLMesh *> *newMeshes = [[NSMutableArray alloc] init];

    // If this ModelIO  object is a mesh object (not a camera, light, or soemthing else)...
    if ([object isKindOfClass:[MDLMesh class]])
    {
        //...create an app-specific AAPLMesh object from it
        MDLMesh* mesh = (MDLMesh*) object;

        AAPLMesh *newMesh = [[AAPLMesh alloc] initWithModelIOMesh:mesh
                                          modelIOVertexDescriptor:vertexDescriptor
                                            metalKitTextureLoader:textureLoader
                                                      metalDevice:device
                                                            error:error];

        [newMeshes addObject:newMesh];
    }

    // Recursively traverse the ModelIO  asset hierarchy to find ModelIO  meshes that are children
    //   of this ModelIO  object and create app-specific AAPLMesh objects from those ModelIO meshes
    for (MDLObject *child in object.children)
    {
        NSArray<AAPLMesh*> *childMeshes;

        childMeshes = [AAPLMesh newMeshesFromObject:child
                            modelIOVertexDescriptor:vertexDescriptor
                              metalKitTextureLoader:textureLoader
                                        metalDevice:device
                                              error:error];

        [newMeshes addObjectsFromArray:childMeshes];
    }

    return newMeshes;
}

+ (nullable NSArray<AAPLMesh *> *) newMeshesFromURL:(nonnull NSURL *)url
                            modelIOVertexDescriptor:(nonnull MDLVertexDescriptor *)vertexDescriptor
                                        metalDevice:(nonnull id<MTLDevice>)device
                                              error:(NSError * __nullable * __nullable)error
{

    // Create a MetalKit mesh buffer allocator so that ModelIO  will load mesh data directly into
    //   Metal buffers accessible by the GPU
    MTKMeshBufferAllocator *bufferAllocator =
    [[MTKMeshBufferAllocator alloc] initWithDevice:device];

    // Use ModelIO  to load the model file at the URL.  This returns a ModelIO  asset object, which
    //   contains a hierarchy of ModelIO objects composing a "scene" described by the model file.
    //   This hierarchy may include lights, cameras, but, most importantly, mesh and submesh data
    //   that we'll render with Metal
    MDLAsset *asset = [[MDLAsset alloc] initWithURL:url
                                   vertexDescriptor:nil
                                    bufferAllocator:bufferAllocator];
    
    NSAssert(asset, @"Failed to open model file with given URL: %@", url.absoluteString);
    
    // Create a MetalKit texture loader to load material textures from files or the asset catalog
    //   into Metal textures
    MTKTextureLoader *textureLoader = [[MTKTextureLoader alloc] initWithDevice:device];

    NSMutableArray<AAPLMesh *> *newMeshes = [[NSMutableArray alloc] init];

    // Traverse the ModelIO asset hierarchy to find ModelIO meshes and create app-specific
    //   AAPLMesh objects from those ModelIO meshes
    for(MDLObject* object in asset)
    {
        NSArray<AAPLMesh *> *assetMeshes;

        assetMeshes = [AAPLMesh newMeshesFromObject:object
                            modelIOVertexDescriptor:vertexDescriptor
                              metalKitTextureLoader:textureLoader
                                        metalDevice:device
                                              error:error];

        [newMeshes addObjectsFromArray:assetMeshes];
    }

    return newMeshes;
}

@end
