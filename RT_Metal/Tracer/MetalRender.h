#ifndef MetalRender_h
#define MetalRender_h

#include "Common.h"
#include "Tracer.hh"

// A platform-independent renderer class.
@interface AAPLRenderer : NSObject<MTKViewDelegate>

- (nonnull instancetype)initWithMetalKitView:(nonnull MTKView *)view;

@end

#endif /* MetalRender_h */
