#ifndef MetalRender_h
#define MetalRender_h

#include "Common.h"

// A platform-independent renderer class.
@interface AAPLRenderer : NSObject<MTKViewDelegate>

- (nonnull instancetype)initWithMetalKitView:(nonnull MTKView *)view;

- (void)drag:(float2)delta;

@end

#endif /* MetalRender_h */
