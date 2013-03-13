//	$Id: Controller.m,v 1.35 2010/10/26 07:19:31 sounds Exp $
//  Controller.m
//  Soundplane
//
//  Created by Sound Consulting on 2010/07/08.
//  Copyright 2010 Sound Consulting. All rights reserved.
//

#import "Controller.h"
#import "K1Private.h"
#import "CellHex.h"
#include <time.h>


@implementation Controller

- init
{
	if (!(self = [super init]))
		return nil;
	max = 0x0FFF;
	return self;
}

- (void)setMask:(unsigned long)mask
{
	MLSP_disableCarriers(mlspRef, mask);
	printf("mask: %08lx\n", mask);
}

#pragma mark Action

- (IBAction)takeCarrierFlagFrom:sender
{
	BOOL carrierFlag = [[sender selectedCell] tag];
//	[carrierSlider setEnabled:carrierFlag];
	[self setMask:(carrierFlag ? ~(1 << [carrierSlider intValue]) : 0)];
}

- (IBAction)takeCarrierFrom:sender
{
	[carrierMatrix selectCellWithTag:1];
	[self setMask:~(1 << [sender intValue])];
}

- (IBAction)takeChannelFrom:sender
{
	channel = [sender intValue];
}

- (IBAction)takeMinMaxFrom:sender
{
	NSCell *cell = [sender selectedCell];
	unsigned value, column = [sender selectedColumn];

	if ([cell isKindOfClass:[NSTextFieldCell class]])
	{
		value = [cell intValueFromHexString];
		[[rangeMatrix cellAtRow:0 column:column] setIntValue:value];
	}
	else if ([cell isKindOfClass:[NSSliderCell class]])
	{
		value = [cell intValue];
		[[rangeHexMatrix cellAtRow:0 column:column] setHexStringValueFromInt:value];
	}
	switch (column)
	{
		case 0:
			min = value;
			break;
		case 1:
			max = value;
			break;
	}
	[boostButton setState:(0 == min && 0x00FF == max ? YES : NO)];
	[surfaceView setRangeMinimum:(min / (float)0x1000) maximum:(max / (float)0x1000)];
}

- (IBAction)takeBoostEnableFrom:sender
{
	min = 0;
	[[rangeMatrix cellAtRow:0 column:0] setIntValue:0];
	[[rangeHexMatrix cellAtRow:0 column:0] setStringValue:@"0"];
	max = [sender state] ? 0x00FF : 0x0FFF;
	[[rangeMatrix cellAtRow:0 column:1] setIntValue:max];
	[[rangeHexMatrix cellAtRow:0 column:1] setStringValue:[NSString stringWithFormat:@"%X", max]];
	[surfaceView takeBoostEnableFrom:sender];
}


#pragma mark Soundplane A callbacks

- (void)handleSurface:(unsigned)endpointIndex taxels:(unsigned short *)p
{
	unsigned short surface[MLK1_N_SURFACE_TAXELS];

	[[knobMatrix cellAtRow:0 column:(6 + endpointIndex)] setIntValue:USBToHostWord(p[MLSP_N_TAXEL_WORDS])];
	K1_unpack(p, surface);
	[surfaceView setSurface:endpointIndex fromUnsignedValues:surface];
	[histogramView computeHistogramFromUnsignedShort:surface count:MLK1_N_SURFACE_TAXELS];
	p = surface + channel * MLK1_N_CARRIERS;
	if (!endpointIndex)
		[fftView0 setCarriers:p];
	else
		[fftView1 setCarriers:p];
}

- (void)handleSurfaceTimer:(NSTimer *)timer
{
	K1Instance *k1 = mlspRef;
	K1IsocInstance *k1i0, *k1i1;
	int i;
	unsigned displayIndex = k1->displayIndex[0];

	if (k1->isTerminating || !k1->dev)
		return;
	while (!k1->isTerminating && k1->isocList[0][displayIndex].hasCompleted && k1->isocList[1][displayIndex].hasCompleted)
	{
		displayIndex = (displayIndex + 1) & SP_MASK_BUFFERS;
		k1->displayIndex[1] = k1->displayIndex[0] = displayIndex;
		k1->payloadIndex[1] = k1->payloadIndex[0] = 0;
	}
	if (k1->isTerminating)
		return;
	k1i0 = &k1->isocList[0][displayIndex];
	k1i1 = &k1->isocList[1][displayIndex];
	if (!k1i0 || !k1i1)
		return;
	for (i = SP_NUM_FRAMES - 1; i >= 0; i--)
	{
		if (!k1->isTerminating && k1i0->isocFrame[i].frActCount && k1i1->isocFrame[i].frActCount)
		{
			[self handleSurface:0 taxels:(k1i0->payload + i * MLSP_N_ISOCH_WORDS)];
			[self handleSurface:1 taxels:(k1i1->payload + i * MLSP_N_ISOCH_WORDS)];
			return;
		}
	}
	// if no matched pairs exist, then at least show something
	for (i = SP_NUM_FRAMES - 1; i >= 0; i--)
	{
		if (!k1->isTerminating && k1i0->isocFrame[i].frActCount)
		{
			[self handleSurface:0 taxels:(k1i0->payload + i * MLSP_N_ISOCH_WORDS)];
			break;
		}
	}
	for (i = SP_NUM_FRAMES - 1; i >= 0; i--)
	{
		if (!k1->isTerminating && k1i1->isocFrame[i].frActCount)
		{
			[self handleSurface:1 taxels:(k1i1->payload + i * MLSP_N_ISOCH_WORDS)];
			break;
		}
	}
}

- (void)handleStatusTimer:(NSTimer *)timer
{
	static UInt32 lastStatus = -1;
	static UInt16 lastStatus1 = -1;
	static UInt16 lastStatus2 = -1;
	static UInt16 lastStatusC1 = -1;
	static UInt16 lastStatusC2 = -1;
	K1Instance *k1 = mlspRef;
	UInt32 lStatus;
	UInt16 wStatus;
	time_t clock = time(NULL);

	if (!MLSP_getLongStatus(k1->dev, codeDropCnt, &lStatus))
	{
		if (lastStatus != lStatus)
		{
			lastStatus = lStatus;
			printf("DAC DMA drop count: %lu @ %s", lStatus, ctime(&clock));
		}
	}
	if (!MLSP_getWordStatus(k1->dev, codeCmdDropCnt1, &wStatus))
	{
		if (lastStatusC1 != wStatus)
		{
			lastStatusC1 = wStatus;
			printf("ADS1Cmd drop count: %u @ %s", wStatus, ctime(&clock));
		}
	}
	if (!MLSP_getWordStatus(k1->dev, codeCmdDropCnt2, &wStatus))
	{
		if (lastStatusC2 != wStatus)
		{
			lastStatusC2 = wStatus;
			printf("ADS2Cmd drop count: %u @ %s", wStatus, ctime(&clock));
		}
	}
	if (!MLSP_getWordStatus(k1->dev, codeDropCnt1, &wStatus))
	{
		if (lastStatus1 != wStatus)
		{
			lastStatus1 = wStatus;
			printf("ADS1Dat drop count: %u @ %s", wStatus, ctime(&clock));
		}
	}
	if (!MLSP_getWordStatus(k1->dev, codeDropCnt2, &wStatus))
	{
		if (lastStatus2 != wStatus)
		{
			lastStatus2 = wStatus;
			printf("ADS2Dat drop count: %u @ %s", wStatus, ctime(&clock));
		}
	}
}

- (void)userInputsUpdate:(unsigned)numInputs values:(unsigned short *)inputs
{
	unsigned i, input, ch, val;

	if (!numInputs)
		return;
	[window disableFlushWindow];
	for (i = 0; i < numInputs; i++)
	{
		input = inputs[i];
		ch = input & MLSP_MASK_USER_CHANNEL;// 8 channels maximum
		val = input & MLSP_MASK_USER_VALUE;	// 10-bit value
		[[knobMatrix cellAtRow:0 column:ch] setIntValue:val];
	}
	[window enableFlushWindow];
	[window flushWindowIfNeeded];
}

void UserInputsCallback(void *controller, unsigned numInputs, unsigned short *inputs)
{
	[(Controller *)controller userInputsUpdate:numInputs values:inputs];
}


#pragma mark Application Delegate

- (void)applicationDidFinishLaunching:(NSNotification *)aNotification
{
	mlspRef = MLSP_create(false, NULL, UserInputsCallback, self);
	surfaceTimer = [[NSTimer alloc] initWithFireDate:[NSDate date] interval:(20./1000.) target:self selector:@selector(handleSurfaceTimer:) userInfo:NULL repeats:YES];
	[[NSRunLoop currentRunLoop] addTimer:surfaceTimer forMode:NSDefaultRunLoopMode];
	statusTimer = [[NSTimer alloc] initWithFireDate:[NSDate date] interval:2. target:self selector:@selector(handleStatusTimer:) userInfo:NULL repeats:YES];
	[[NSRunLoop currentRunLoop] addTimer:statusTimer forMode:NSDefaultRunLoopMode];
}

- (void)applicationWillTerminate:(NSNotification *)aNotification
{
	[surfaceTimer invalidate];
	[statusTimer invalidate];
	MLSP_destroy(mlspRef);
	[surfaceTimer release];
	[statusTimer release];
}

@end
