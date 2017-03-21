//
// Part of the Soundplane client software by Madrona Labs.
// Copyright (c) 2013 Madrona Labs LLC. http://www.madronalabs.com
// Distributed under the MIT license: http://madrona-labs.mit-license.org/

#include "SoundplaneView.h"

// --------------------------------------------------------------------------------
#pragma mark header view

SoundplaneHeaderView::SoundplaneHeaderView(SoundplaneModel* pModel, MLWidget::Listener* pResp, MLReporter* pRep) :
	MLAppView(pResp, pRep),
	mpModel(pModel)
{
    setWidgetName("soundplane_header_view");

	float margin = 0.20f;
	float pw = 4.;
	float h = 1.f;
	float ph = h - margin*2.;
	MLRect t(0, 0, pw, ph);
	//addMenuButton("", t.withCenter(w/2., h - margin - ph/2.), "preset");
	
	/*
	// registration
	float regSize = 2.5f;
	MLRect ulRect(0., 0.125, regSize, 0.75);
	std::string reg (ProjectInfo::projectName);
	reg += "\nversion ";
	reg += ProjectInfo::versionString;
	
	MLLabel* pUL = addLabel(reg.c_str(), ulRect, 1.0f, eMLCaption); 
	pUL->setJustification(Justification::centredLeft);	
	pUL->setResizeToText(false);
	*/
}

SoundplaneHeaderView::~SoundplaneHeaderView(){}

void SoundplaneHeaderView::paint (Graphics& g)
{
	const Colour c2 = findColour(MLLookAndFeel::backgroundColor2);
	int h = getHeight();
	int w = getWidth();
	
	// bottom line
	g.setColour(c2);
	g.drawLine(0, h - 0.5f, w, h - 0.5f);
}

// --------------------------------------------------------------------------------
#pragma mark footer view

SoundplaneFooterView::SoundplaneFooterView(SoundplaneModel* pModel, MLWidget::Listener* pResp, MLReporter* pRep) :
	MLAppView(pResp, pRep),
	mpModel(pModel),
	mpDevice(0),
	mpStatus(0),
	mCalibrateState(0),
	mCalibrateProgress(0.)
{
    setWidgetName("soundplane_footer_view");
	MLLookAndFeel* myLookAndFeel = MLLookAndFeel::getInstance();
	
	float labelWidth = 6.;
	float w = kSoundplaneViewGridUnitsX;
	float h = 0.5;
	mpDevice = addLabel("---", MLRect(0, 0, labelWidth, h));
	mpDevice->setFont(myLookAndFeel->mCaptionFont);
	mpDevice->setJustification(Justification::topLeft); 
	mpDevice->setResizeToText(false);

	mpStatus = addLabel("---", MLRect(w - labelWidth, 0, labelWidth, h));
	mpStatus->setFont(myLookAndFeel->mCaptionFont);
	mpStatus->setJustification(Justification::topRight);
	mpStatus->setResizeToText(false);
	
	mpCalibrateText = addLabel("calibrating...", MLRect(w - labelWidth*0.75f, 0, labelWidth*0.25, h));
	mpCalibrateText->setFont(myLookAndFeel->mCaptionFont);
	mpCalibrateText->setJustification(Justification::topLeft);
	mpCalibrateText->setResizeToText(false);
	
	mpCalibrateProgress = addProgressBar(MLRect(w - labelWidth*0.5f, 0, labelWidth*0.5f, h/2));
	
	setCalibrateState(false);
}

SoundplaneFooterView::~SoundplaneFooterView(){}

void SoundplaneFooterView::setStatus (const char* stat, const char* client)
{
	if(mpStatus)
	{
		std::string temp(stat);
		if(strlen(client) > 1)
		{
			temp += " / ";
			temp += client;
		}
		temp += ".";
		mpStatus->setProperty("text", temp.c_str());
	}
}

void SoundplaneFooterView::setHardware(const char* c)
{
	if(mpDevice)
	{
		std::string temp(c);
		temp += ", client v.";
		temp += MLProjectInfo::versionString;
		mpDevice->setProperty("text", temp.c_str());
		mpDevice->repaint();
	}
}

void SoundplaneFooterView::setCalibrateProgress(float p)
{
	mCalibrateProgress = p;
	mpCalibrateProgress->setProperty("progress", p);
	mpCalibrateProgress->repaint();
}

void SoundplaneFooterView::setCalibrateState(bool b)
{
	mCalibrateState = b;
	mpStatus->setVisible(!b);
	mpCalibrateText->setVisible(b);
	mpCalibrateProgress->setVisible(b);
}

void SoundplaneFooterView::paint (Graphics& g)
{
//	MLLookAndFeel* myLookAndFeel = MLLookAndFeel::getInstance();
//	const Colour c2 = findColour(MLLookAndFeel::backgroundColor2);	
	// top line
//	g.setColour(c2);
//	g.drawLine(0, 0.25f, w, 0.25f);
}

#pragma mark main view

// --------------------------------------------------------------------------------
SoundplaneView::SoundplaneView (SoundplaneModel* pModel, MLWidget::Listener* pResp, MLReporter* pRep) :
	MLAppView(pResp, pRep),
	mpFooter(0),
	mpPages(0),
	mpModel(pModel),
	mCalibrateState(-1),
	mSoundplaneClientState(-1),
	mSoundplaneDeviceState(-1),
	mpCurveGraph(0),
	mpViewModeButton(0),
	mpMIDIDeviceButton(0), 
	mpOSCServicesButton(0),
	mpMidiChannelDial(0)
{
    setWidgetName("soundplane_view");
	//pModel->addListener(this);
    
	// setup application's look and feel 
	MLLookAndFeel* myLookAndFeel = MLLookAndFeel::getInstance();
	LookAndFeel::setDefaultLookAndFeel (myLookAndFeel);	
	myLookAndFeel->setGradientMode(1); // A->B->A
	myLookAndFeel->setGlobalTextScale(1.0f);

	MLDial* pD;
	MLButton* pB;
    MLLabel* pL;
    
	float width = kSoundplaneViewGridUnitsX;
	float height = kSoundplaneViewGridUnitsY;
	float footerHeight = 0.5f;
    
	mpFooter = new SoundplaneFooterView(pModel, pResp, pRep);

	mpFooter->setGridBounds(MLRect(0, height - footerHeight, width, footerHeight));
	addAndMakeVisible(mpFooter);
	mWidgets["footer"] = mpFooter;
	
	// set gradient size so that area around OpenGL views is flat color.
	myLookAndFeel->setGradientSize(0.07f); 
	const Colour c1 = findColour(MLLookAndFeel::backgroundColor);
	const Colour c2 = findColour(MLLookAndFeel::defaultFillColor);

	// get drawing resources
	myLookAndFeel->addPicture ("arrowleft", SoundplaneBinaryData::arrowleft_svg, SoundplaneBinaryData::arrowleft_svgSize);
	myLookAndFeel->addPicture ("arrowleftdown", SoundplaneBinaryData::arrowleftdown_svg, SoundplaneBinaryData::arrowleftdown_svgSize);
	myLookAndFeel->addPicture ("arrowright", SoundplaneBinaryData::arrowright_svg, SoundplaneBinaryData::arrowright_svgSize);
	myLookAndFeel->addPicture ("arrowrightdown", SoundplaneBinaryData::arrowrightdown_svg, SoundplaneBinaryData::arrowrightdown_svgSize);

	// pass unhandled mouse clicks to parent, do allow children to catch them first.
	setInterceptsMouseClicks (false, true);	

	// setup controls on main view
	MLRect t(0, 0, 0.5, 0.75);
	MLRect prevRect = t.withCenter(0.175, kSoundplaneViewGridUnitsY/2.f);
	MLRect nextRect = t.withCenter(kSoundplaneViewGridUnitsX - 0.175, kSoundplaneViewGridUnitsY/2.f);
	
	mpPrevButton = addRawImageButton(prevRect, "prev", c1, myLookAndFeel->getPicture("arrowleft"));
	mpNextButton = addRawImageButton(nextRect, "next", c1, myLookAndFeel->getPicture("arrowright"));

	// setup pages
	float mx = 0.5;
	float pageWidth = width - mx*2.;
	MLRect pageRect(mx, 0, pageWidth, height - footerHeight);
	mpPages = new MLPageView(pResp, pRep);
	mpPages->setParent(this);
	addWidgetToView(mpPages, pageRect, "pages");
    
	MLRect pageTitleRect(0., 0., 3.0, 1.);
    // get rect for top preset menus
	float pmw = pageWidth;
	float pmh = 1.;
	float presetMenuWidth = 6;
	float presetMenuHeight = 0.5f;
	MLRect presetMenuRect(0, 0, presetMenuWidth, presetMenuHeight);
    
 	float dialY = 8.25;	// center line for dials
	MLRect dialRect (0, 0, 1.0, 0.875);
	MLRect dialRectSmall (0, 0, 1.0, 0.625);
	MLRect buttonRect(0, 0, 1, 0.5);
	MLRect toggleRect(0, 0, 1, 0.5);
	MLRect textButtonRect(0, 0, 5.5, 0.4);
	MLRect textButtonRect2(0, 0, 2., 0.4);
    
    // --------------------------------------------------------------------------------
	// page 0 - zones, OSC, MIDI
	//
	MLAppView* page0 = mpPages->addPage();
    
    // zone preset menu
    MLMenuButton* mB = page0->addMenuButton("", presetMenuRect.withCenter(pmw/2., pmh/2.), "zone_preset");
	mB->setProperty("strip", 1); // set button to strip displayed filenames
    
	MLDrawing* pDraw = page0->addDrawing(MLRect(0, 1, 14, 9));
	page0->renameWidget(pDraw, "page0_lines");
	int p[20]; // point indices
	p[0] = pDraw->addPoint(Vec2(7., 5.0));
	p[1] = pDraw->addPoint(Vec2(7., 8.5));
	p[2] = pDraw->addPoint(Vec2(10.5, 5.0));
	p[3] = pDraw->addPoint(Vec2(10.5, 8.5));
	pDraw->addOperation(MLDrawing::drawLine, p[0], p[1]);
	pDraw->addOperation(MLDrawing::drawLine, p[2], p[3]);
	
	// title
	pL = page0->addLabel("Zones", pageTitleRect, 1.5f, eMLTitle);
    pL->setResizeToText(false);
	pL->setJustification(Justification::centredLeft);
	
	mGLView3.setModel(pModel);
	MLRect zoneViewRect(0, 2.f, pageWidth, 3.75);
	page0->addWidgetToView (&mGLView3, zoneViewRect, "zone_view");
	
	MLRect zoneLabelRect(0, 0, 3., 0.25);
	float sectionLabelsY = 6.125; 
	page0->addLabel("ZONE", zoneLabelRect.withCenter(3.5, sectionLabelsY), 1.00f, eMLTitle);
	page0->addLabel("MIDI", zoneLabelRect.withCenter(8.75, sectionLabelsY), 1.00f, eMLTitle);
	page0->addLabel("OSC", zoneLabelRect.withCenter(12.25, sectionLabelsY), 1.00f, eMLTitle);
    
	// all-zone controls up top
	float topDialsY = 1.66f;
    
	pB = page0->addToggleButton("quantize", toggleRect.withCenter(4.25, topDialsY), "quantize", c2);
	pB = page0->addToggleButton("note lock", toggleRect.withCenter(5.25, topDialsY), "lock", c2);
	pB = page0->addToggleButton("glissando", toggleRect.withCenter(6.25, topDialsY), "glissando", c2);
    
	pD = page0->addDial("portamento", dialRect.withCenter(7.75, topDialsY), "snap", c2);
	pD->setRange(0., 1000., 10);
	pD->setDefault(250.);
	pD = page0->addDial("vibrato", dialRect.withCenter(8.75, topDialsY), "vibrato", c2);
	pD->setRange(0., 1., 0.01);
	pD->setDefault(0.5);
	pD = page0->addDial("transpose", dialRect.withCenter(9.75, topDialsY), "transpose", c2);
	pD->setRange(-24, 24., 1.);
	pD->setDefault(0);
	
	// ZONE
	float bottomDialsY = 7.25f;
	float bottomDialsY2 = 8.25f;
    
	// for all zone types: start channel # (dial)
	// zone type: {note, note row, control}
	// if note: note #, single / multi
	// if note grid: start note, y skip, single / multi
	// if control: ctrl x, ctrl y, ctrl dx, ctrl dy
    
	// MIDI
	pB = page0->addToggleButton("active", toggleRect.withCenter(7.75, bottomDialsY), "midi_active", c2);
	pB = page0->addToggleButton("pressure", toggleRect.withCenter(7.75, bottomDialsY2), "midi_pressure_active", c2);
	pD = page0->addDial("rate", dialRect.withCenter(8.75, bottomDialsY), "data_freq_midi", c2);
	pD->setRange(1., 500., 1.);
	pD->setDefault(250.);
	pD = page0->addDial("bend range", dialRect.withCenter(9.75, bottomDialsY), "bend_range", c2);
	pD->setRange(0., 96., 1.);
	for(int i=0; i<13; ++i)
	{
		pD->addDetent(i);
	}
	for(int i=24; i<97; i += 12)
	{
		pD->addDetent(i);
	}
	
	pB = page0->addToggleButton("MPE", toggleRect.withCenter(8.75, bottomDialsY2), "midi_mpe", c2);
	mpMidiChannelDial = page0->addDial("channel", dialRect.withCenter(9.75, bottomDialsY2), "midi_channel", c2);
	mpMidiChannelDial->setRange(1., 16., 1.);
	
	// 	addWidgetToView(button, r, paramName);
	// addPropertyView(paramName, button, MLSymbol("value"));
	
    //	pB = page0->addToggleButton("one / multi", toggleRect.withCenter(6.5, 5.25), "single_multi", c2);
    //	pB->setSplitMode(true);
    //	pB->setEnabled(false);
    
    //	pD = page0->addDial("channel", dialRectSmall.withCenter(4.5, 7.75), "channel", c2);
    //	pD->setRange(1., 16., 1.);
    
	pD->setDefault(48);
	
	MLRect textButtonRect3(0, 0, 3., 0.4);
	mpMIDIDeviceButton = page0->addMenuButton("device", textButtonRect3.withCenter(8.75, 9.), "midi_device");
	
	// OSC
	
	pB = page0->addToggleButton("active", toggleRect.withCenter(11.25, bottomDialsY), "osc_active", c2);
	pD = page0->addDial("rate", dialRect.withCenter(12.25, bottomDialsY), "data_freq_osc", c2);
	pD->setRange(1., 500., 1.);
	pD->setDefault(250.);
	pB = page0->addToggleButton("matrix", toggleRect.withCenter(13.25, bottomDialsY), "osc_send_matrix", c2);
	
	mpOSCServicesButton = page0->addMenuButton("destination", textButtonRect3.withCenter(12.25, 9.), "osc_service_name");
	
	// additional parameter views allow us to adapt UI based on Model properties.
	page0->addPropertyView("midi_mpe", this, MLSymbol("mpe")); 	

    // --------------------------------------------------------------------------------
	// page 1 - raw touches
	//
	MLAppView* page1 = mpPages->addPage();

	// title
	pL = page1->addLabel("Touches", pageTitleRect, 1.5f, eMLTitle);
    pL->setResizeToText(false);
	pL->setJustification(Justification::centredLeft); 

	// GL views
	//
	MLRect GLRect1(0, 1.f, pageWidth, 3.5);
	mGridView.setModel(pModel);
	page1->addWidgetToView (&mGridView, GLRect1, "grid_view");
    
	MLRect GLRect2(0, 4.5, pageWidth, 3.);
	mTouchView.setModel(pModel);
	page1->addWidgetToView (&mTouchView, GLRect2, "touch_view");
	
	// temp toggles
	//
	MLRect toggleRectTiny(0, 0, 0.25, 0.25);	
	char numBuf[64] = {0};
	int c = pModel->getNumCarriers();
	mpCarrierToggles.resize(c);
	mpCarrierLabels.resize(c);
	for(int i=0; i<c; ++i)
	{
		MLSymbol tSym = MLSymbol("carrier_toggle").withFinalNumber(i);
		mpCarrierToggles[i] = page1->addToggleButton("", toggleRectTiny.withCenter(i*0.3 + 1., 5), tSym, c2);
		sprintf(numBuf, "%d", i);	
		mpCarrierLabels[i] = page1->addLabel(numBuf, toggleRectTiny.withCenter(i*0.3 + 1., 4.75));
	}
	
	mpCarriersOverrideToggle = page1->addToggleButton("override", toggleRect.withCenter(12, 5), "override_carriers", c2);
	mpCarriersOverrideDial = page1->addDial("with set", dialRectSmall.withCenter(13, 5), "override_carrier_set", c2);
	mpCarriersOverrideDial->setRange(0., 7., 1);	
	mpCarriersOverrideDial->setDefault(0);

//	page1->addToggleButton("all", toggleRectTiny.withCenter(0*0.3 + 1., 6.), "all_toggle", c2);

	// controls
	//

	page1->addTextButton("recalibrate", textButtonRect.withCenter(2.75, 9.), "calibrate");

	// page1->addTextButton("test", textButtonRect2.withCenter(7., 8.), "test");

	pD = page1->addDial("view scale", dialRectSmall.withCenter(13, dialY - 0.125), "display_scale", c2);
	pD->setRange(0.5, 10., 0.1);
	pD->setDefault(1.);	
	
	pD = page1->addDial("touches", dialRect.withCenter(0.5, dialY), "max_touches", c2);
	pD->setRange(0., 16., 1.);	
	pD->setDefault(4);	

	pD = page1->addDial("thresh", dialRect.withCenter(2.0, dialY), "z_thresh", c2);
	pD->setRange(0., 0.05, 0.001);	
	pD->setDefault(0.01);	
	
	pD = page1->addDial("z scale", dialRect.withCenter(3.5, dialY), "z_scale", c2);
	pD->setRange(0.25, 5.0, 0.01);	
	pD->setDefault(1.0);
	
	pD = page1->addDial("z curve", dialRect.withCenter(5.0, dialY), "z_curve", c2);
	pD->setRange(0., 1., 0.01);	
	pD->setDefault(0.25);	

	pD = page1->addDial("lopass", dialRect.withCenter(7, dialY), "lopass", c2);
	pD->setRange(1., 250., 1);	
	pD->setDefault(100.);	
	
	pB = page1->addToggleButton("rotate", toggleRect.withCenter(8.5, dialY), "rotate", c2);
	
	mpViewModeButton = page1->addMenuButton("view mode", textButtonRect2.withCenter(13, 9.), "viewmode");

//	mpCurveGraph = page1->addGraph("zgraph", Colours::black);

	// add parameter views handled directly by this Widget
	page1->addPropertyView("viewmode", this, MLSymbol("viewmode"));	
	
	// grid view gets viewmode changes
	page1->addPropertyView("viewmode", &mGridView, MLSymbol("viewmode"));

    // --------------------------------------------------------------------------------
    // page 2 - expert stuff
	//
	MLAppView* page2 = mpPages->addPage();

	// title
	pL = page2->addLabel("Expert", pageTitleRect, 1.5f, eMLTitle);
    pL->setResizeToText(false);
	pL->setJustification(Justification::centredLeft);
	
	// utility buttons
	page2->addTextButton("select carriers", MLRect(0, 2, 3, 0.4), "select_carriers");
	page2->addTextButton("restore defaults", MLRect(0, 3., 3, 0.4), "restore_defaults");
	
	page2->addTextButton("normalize", MLRect(3.5, 2, 3, 0.4), "normalize");
	page2->addTextButton("cancel normalize", MLRect(3.5, 2.5, 3, 0.4), "normalize_cancel");
	page2->addTextButton("use defaults", MLRect(3.5, 3., 3, 0.4), "normalize_default");
	
	// tracker calibration view
	MLRect TCVRect(0, 4.f, 6.5, 3.);
	mTrkCalView.setModel(pModel);
	page2->addWidgetToView (&mTrkCalView, TCVRect, "trk_cal_view");

	// console
	MLDebugDisplay* pDebug = page2->addDebugDisplay(MLRect(7., 2., 7., 5.));
	
	MLConsole().sendOutputToListener(pDebug);
	
	// MLTEST temp
	//debug().sendOutputToListener(pDebug);

	pD = page2->addDial("bg filter", dialRect.withCenter(0.5, dialY), "bg_filter", c2);
	pD->setRange(0.01, 1.0, 0.01);	
	pD->setDefault(0.05);
	
	pD = page2->addDial("hysteresis", dialRect.withCenter(2, dialY), "hysteresis", c2);
	pD->setRange(0.01, 1.0, 0.01);	
	pD->setDefault(0.5);
	
	pD = page2->addDial("template", dialRect.withCenter(3.5, dialY), "t_thresh", c2);
	pD->setRange(0., 1., 0.001);	
	pD->setDefault(0.2);
	
	pB = page2->addToggleButton("kyma", toggleRect.withCenter(12, dialY), "kyma_poll", c2);
	pB = page2->addToggleButton("test", toggleRect.withCenter(13, dialY), "test_signal", c2);

}

SoundplaneView::~SoundplaneView()
{
	stopTimer();
}

// --------------------------------------------------------------------------------
// MLModelListener implementation
// an updateChangedParams() or updateAllParams() is needed to get these actions sent by the Model.
//
void SoundplaneView::doPropertyChangeAction(MLSymbol p, const MLProperty & val)
{
	bool handled = false;
	if(p == "viewmode")
	{
		handled = true;
		const std::string& v = val.getStringValue();
		if(v == "raw data")
		{
			makeCarrierTogglesVisible(1);
            mTouchView.setWidgetVisible(0);
		}
		else
		{
			makeCarrierTogglesVisible(0);
            mTouchView.setWidgetVisible(1);
		}
		repaint();
	}
	else if(p == "view_page")
	{
		handled = true;
		int v = val.getFloatValue();
		goToPage(v);
		repaint();
	}
	else if(p == "mpe")
	{
		int v = val.getFloatValue();
		if(mpMidiChannelDial)
		{
			if(v) // MPE mode on
			{
				mpMidiChannelDial->setWidgetEnabled(false);	
			}
			else
			{
				mpMidiChannelDial->setWidgetEnabled(true);	
			}
		}
		handled = true;
		repaint();
	}

	if(!handled)
	{
		MLAppView::doPropertyChangeAction(p, val);
	}
}


void SoundplaneView::initialize()
{
	startTimer(50);	
	MLAppView::initialize();
}

// TODO take this away and use Model Properties and doPropertyChangeAction() instead.
void SoundplaneView::timerCallback()
{
	// poll soundplane status and get info.
	// we don't have to know what the states mean -- just an index. 
	// if the status changes, pull current info from Soundplane and redraw.
	//
	if (mpFooter)
	{
		bool needsRepaint = false;
		int calState = mpModel->isCalibrating();
		int deviceState = mpModel->getDeviceState();
		int clientState = mpModel->getClientState();
		
		if(calState)
		{
			mpFooter->setCalibrateProgress(mpModel->getCalibrateProgress());
			needsRepaint = true;
		}

		if(calState != mCalibrateState)
		{
			mpFooter->setCalibrateState(calState);
			needsRepaint = true;
			mCalibrateState = calState;
		}
		
		if ((clientState != mSoundplaneClientState) || (deviceState != mSoundplaneDeviceState))
		{
			mpFooter->setHardware(mpModel->getHardwareStr());
			mpFooter->setStatus(mpModel->getStatusStr(), mpModel->getClientStr());
			mSoundplaneClientState = clientState;
			mSoundplaneDeviceState = deviceState;
			needsRepaint = true;
		}
		
		if(needsRepaint)
		{
			mpFooter->repaint();
		}
	}
}

int SoundplaneView::getCurrentPage()
{
    if(mpPages)
    {
        return mpPages->getCurrentPage();
    }
    else
    {
        return -1;
    }
}

void SoundplaneView::makeCarrierTogglesVisible(int v)
{
	int carriers = mpCarrierToggles.size();
	for(int i=0; i<carriers; ++i)
	{
		//MLWidget* pw = getWidget(MLSymbol("carrier_toggle").withFinalNumber(i));
		
		MLWidget* pw = mpCarrierToggles[i];
		if(pw)
		{
			pw->setWidgetVisible(v);
		}
		MLWidget* pL = mpCarrierLabels[i];
		if(pL)
		{
			pL->setWidgetVisible(v);
		}
	}
	mpCarriersOverrideToggle->setWidgetVisible(v);
	mpCarriersOverrideDial->setWidgetVisible(v);
}


// TODO make this code part of menus!!!

void SoundplaneView::setMIDIDeviceString(const std::string& str)
{	
	// TODO auto get button text from menu code
	if(mpMIDIDeviceButton)
		mpMIDIDeviceButton->setProperty("text", str);
}

void SoundplaneView::setOSCServicesString(const std::string& str)
{	
	// TODO auto get button text from menu code
	if(mpOSCServicesButton)
		mpOSCServicesButton->setProperty("text", str);
}

void SoundplaneView::paint (Graphics& g)
{
	MLLookAndFeel* myLookAndFeel = MLLookAndFeel::getInstance();
	myLookAndFeel->drawBackground(g, this);	

	// TEST paint grid
	// myLookAndFeel->drawUnitGrid(g);	
}

void SoundplaneView::goToPage (int page)
{
	if (mpPages)
	{
		mpPages->goToPage(page, true, mpPrevButton, mpNextButton); 
		int newPage = mpPages->getCurrentPage();
		mpPrevButton->setVisible(newPage > 0);
		mpNextButton->setVisible(newPage < mpPages->getNumPages() - 1);
	}
}

void SoundplaneView::prevPage()
{
	if (mpPages)
	{
		int page = mpPages->getCurrentPage();
		goToPage(page - 1);
	}
}

void SoundplaneView::nextPage()
{
	if (mpPages)
	{
		int page = mpPages->getCurrentPage();
		goToPage(page + 1);
	}
}
