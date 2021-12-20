//
//  MachServer.h
//  obs-mac-virtualcam
//
//  Created by John Boiles  on 5/5/20.
//

#import <Foundation/Foundation.h>

typedef enum {
	MachClientConnectStateConnect = 1,
	MachClientConnectStateDisconnect = 2,
} MachClientConnectState;

NS_ASSUME_NONNULL_BEGIN

@interface OBSDALMachServer : NSObject

@property BOOL mirror;

@property (nonatomic, copy) void (^machClientConnectStateChanged)(MachClientConnectState state);

- (void)run;

/*!
 Will eventually be used for sending frames to all connected clients
 */
- (void)sendFrameWithSize:(NSSize)size
		timestamp:(uint64_t)timestamp
	     fpsNumerator:(uint32_t)fpsNumerator
	   fpsDenominator:(uint32_t)fpsDenominator
	       frameBytes:(uint8_t *)frameBytes;

- (void)stop;

@end

NS_ASSUME_NONNULL_END
