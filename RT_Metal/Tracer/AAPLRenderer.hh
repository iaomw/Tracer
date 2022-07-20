#ifndef AAPLRenderer_h
#define AAPLRenderer_h

#include "Common.hh"

// A platform-independent renderer class.
@interface AAPLRenderer : NSObject<MTKViewDelegate>

- (nonnull instancetype)initWithMetalKitView:(nonnull MTKView *)view;

- (void)pin:(float2)delta state:(BOOL)ended;
- (void)drag:(float3)delta state:(BOOL)ended;

@end

#endif /* MetalRender_h */
