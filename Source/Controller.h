//	$Id: Controller.h,v 1.15 2010/08/23 05:57:59 sounds Exp $
//  Controller.h
//  Soundplane
//
//  Created by Sound Consulting on 2010/07/08.
//  Copyright 2010 Sound Consulting. All rights reserved.
//

#import <Cocoa/Cocoa.h>
#import "SoundplaneAView.h"
#import "HistogramFFFView.h"
#import "FFTView.h"


@interface Controller : NSObject
{
	IBOutlet NSWindow			*window;
	IBOutlet NSMatrix			*knobMatrix;
	IBOutlet SoundplaneAView	*surfaceView;
	IBOutlet NSButton			*boostButton;
	IBOutlet NSMatrix			*carrierMatrix;
	IBOutlet NSSlider			*carrierSlider;
	IBOutlet HistogramFFFView	*histogramView;
	IBOutlet NSSlider			*channelSlider;
	IBOutlet FFTView			*fftView0;
	IBOutlet FFTView			*fftView1;
	IBOutlet NSSliderCell		*minSlider;
	IBOutlet NSSliderCell		*maxSlider;
	IBOutlet NSMatrix			*rangeMatrix;
	IBOutlet NSMatrix			*rangeHexMatrix;

	unsigned	channel;
	unsigned	min, max;
	void		*mlspRef;
	NSTimer		*surfaceTimer;
	NSTimer		*statusTimer;
}

- (IBAction)takeCarrierFlagFrom:sender;
- (IBAction)takeCarrierFrom:sender;
- (IBAction)takeChannelFrom:sender;
- (IBAction)takeMinMaxFrom:sender;
- (IBAction)takeBoostEnableFrom:sender;

@end
