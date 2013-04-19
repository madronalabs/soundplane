
// Part of the Soundplane client software by Madrona Labs.
// Copyright (c) 2013 Madrona Labs LLC. http://www.madronalabs.com
// Distributed under the MIT license: http://madrona-labs.mit-license.org/

#include "SoundplaneView.h"

const float kCanvasRectHeight = 0.8f;
const float kMenuRectWidth = 0.25f;
const float kControlsRectWidth = 0.15f;
const float kVoicesRectWidth = 0.25f;

// --------------------------------------------------------------------------------
#pragma mark header view

SoundplaneHeaderView::SoundplaneHeaderView(SoundplaneModel* pModel, MLResponder* pResp, MLReporter* pRep) :
	MLAppView(pResp, pRep),
	mpModel(pModel)
{
	float w = kViewGridUnitsX;
	float margin = 0.20f;
	float pw = 4.;
	float h = 1.f;
	float ph = h - margin*2.;
	MLRect t(0, 0, pw, ph);
	addMenuButton("", t.withCenter(w/2., h - margin - ph/2.), "preset"); 
	
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

SoundplaneFooterView::SoundplaneFooterView(SoundplaneModel* pModel, MLResponder* pResp, MLReporter* pRep) :
	MLAppView(pResp, pRep),
	mpModel(pModel),
	mpDevice(0),
	mpStatus(0)
{
	MLLookAndFeel* myLookAndFeel = MLLookAndFeel::getInstance();	
	
	float labelWidth = 6.;
	float w = kViewGridUnitsX;
	float h = 0.5;
	mpDevice = addLabel("---", MLRect(0, 0, labelWidth, h));
	mpDevice->setFont(myLookAndFeel->mCaptionFont);
	mpDevice->setJustification(Justification::topLeft); 
	mpDevice->setResizeToText(false);

	mpStatus = addLabel("---", MLRect(w - labelWidth, 0, labelWidth, h));
	mpStatus->setFont(myLookAndFeel->mCaptionFont);
	mpStatus->setJustification(Justification::topRight);
	mpStatus->setResizeToText(false);
	
	// TODO add calibration progress in footer
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
		mpStatus->setText(temp.c_str());
	}
}

void SoundplaneFooterView::setDevice (const char* c)
{
	if(mpDevice)
	{
		std::string temp(c);
		temp += " [client ";
		temp += ProjectInfo::versionString;
		temp += "]";
		mpDevice->setText(temp.c_str());
	}
}

/*
// calibrate UI
// TODO clean this up
void SoundplaneFooterView::drawCalibrate()
{
	int viewW = getWidth();
	int viewH = getHeight();
	glColor3f(0.f, 0.f, 0.f);
	drawTextAt(viewW/2, viewH/2, 0., "CALIBRATING...");
	
	float p = mpModel->getCalibrateProgress();

	// draw progress bar
	// TODO nicer code
	float margin = 20.f;
	float barSize = 20.f;
	float top = viewH/2 - barSize/2;
	float bottom = viewH/2 + barSize/2;
	float left = margin;
	float right = viewW - margin;
	MLRange pRange(0., 1.);
	pRange.convertTo(MLRange(left, right));
	float pr = pRange(p);


}

*/

void SoundplaneFooterView::paint (Graphics& g)
{
	MLLookAndFeel* myLookAndFeel = MLLookAndFeel::getInstance();
	const Colour c1 = findColour(MLLookAndFeel::backgroundColor);
	const Colour c2 = findColour(MLLookAndFeel::backgroundColor2);
//	int h = getHeight();
//	int w = getWidth();

	myLookAndFeel->drawBackground(g, this);	

	// TEST paint grid
//	myLookAndFeel->drawUnitGrid(g);	

	// top line
//	g.setColour(c2);
//	g.drawLine(0, 0.25f, w, 0.25f);

	// calibrate UI
	if (mpModel->isCalibrating())
	{
//		drawCalibrate();
	}

}

#pragma mark main view

// --------------------------------------------------------------------------------
SoundplaneView::SoundplaneView (SoundplaneModel* pModel, MLResponder* pResp, MLReporter* pRep) :
	MLAppView(pResp, pRep),
	mpHeader(0),
	mpFooter(0),
	mpPages(0),
	mpModel(pModel),
	mpGridView(0),
	mpTouchView(0),
	mpGLView3(0),
	mSoundplaneState(-1),
	mSoundplaneClientState(-1),
	mpCurveGraph(0),
	mpViewModeButton(0),
	mpMIDIDeviceButton(0),
	MLModelListener(pModel)
{		
	// setup application's look and feel 
	MLLookAndFeel* myLookAndFeel = MLLookAndFeel::getInstance();
	LookAndFeel::setDefaultLookAndFeel (myLookAndFeel);	
	myLookAndFeel->setGradientMode(1); // A->B->A

	myLookAndFeel->setGlobalTextScale(1.0f);

	MLDial* pD;
	MLButton* pB;
	
	// resize and add subviews.
//	int u = myLookAndFeel->getGridUnitSize(); 

	float width = kViewGridUnitsX;
	float height = kViewGridUnitsY;
	float headerHeight = 1.0f;
	float footerHeight = 0.5f;
	
	mpHeader = new SoundplaneHeaderView(pModel, pResp, pRep);
//	mpHeader->setGridUnitSize(u);
	mpHeader->setGridBounds(MLRect(0, 0, width, headerHeight));
	addAndMakeVisible(mpHeader);
	mWidgets["header"] = mpHeader;

	mpFooter = new SoundplaneFooterView(pModel, pResp, pRep);
//	mpFooter->setGridUnitSize(u);
	mpFooter->setGridBounds(MLRect(0, height - footerHeight, width, footerHeight));
	addAndMakeVisible(mpFooter);
	mWidgets["footer"] = mpFooter;
	
	// set gradient size so that area around OpenGL views is flat color.
	myLookAndFeel->setGradientSize(0.07f); 
	const Colour c1 = findColour(MLLookAndFeel::backgroundColor);
	const Colour c2 = Colours::white;//findColour(MLLookAndFeel::backgroundColor2);
	const Colour c3 = Colours::white;

	// get drawing resources
	myLookAndFeel->addPicture ("arrowleft", SoundplaneBinaryData::arrowleft_svg, SoundplaneBinaryData::arrowleft_svgSize);
	myLookAndFeel->addPicture ("arrowleftdown", SoundplaneBinaryData::arrowleftdown_svg, SoundplaneBinaryData::arrowleftdown_svgSize);
	myLookAndFeel->addPicture ("arrowright", SoundplaneBinaryData::arrowright_svg, SoundplaneBinaryData::arrowright_svgSize);
	myLookAndFeel->addPicture ("arrowrightdown", SoundplaneBinaryData::arrowrightdown_svg, SoundplaneBinaryData::arrowrightdown_svgSize);

	// pass unhandled mouse clicks to parent, do allow children to catch them first.
	setInterceptsMouseClicks (false, true);	

	// setup controls on main view
	MLRect t(0, 0, 0.5, 0.75);
	MLRect prevRect = t.withCenter(0.175, kViewGridUnitsY/2.f);
	MLRect nextRect = t.withCenter(kViewGridUnitsX - 0.175, kViewGridUnitsY/2.f);
	
	mpPrevButton = addRawImageButton(prevRect, "prev", c1, myLookAndFeel->getPicture("arrowleft"));
	mpNextButton = addRawImageButton(nextRect, "next", c1, myLookAndFeel->getPicture("arrowright"));

	mpPrevButton->setListener(this);
	mpNextButton->setListener(this);
	mpPrevButton->setConnectedEdges(Button::ConnectedOnLeft);
	mpNextButton->setConnectedEdges(Button::ConnectedOnRight);

	// setup pages
	float mx = 0.5;
	float pageWidth = width - mx*2.;
	MLRect pageRect(mx, headerHeight, pageWidth, height - headerHeight - footerHeight);
	mpPages = new MLPageView(pResp, pRep);
	mpPages->setParent(this);
	addWidgetToView(mpPages, pageRect, "pages");
		
	// page 0 - raw touches
	//
	MLAppView* page0 = mpPages->addPage();

	// title
	MLRect pageTitleRect(0., 0., 3.0, 1.);
	MLLabel* pL = page0->addLabel("Touches", pageTitleRect, 1.5f, eMLTitle);
	pL->setJustification(Justification::centredLeft); 

	// GL views
	//
	MLRect GLRect1(0, 1.f, pageWidth, 2.5);  // top GL rect on all pages
	mpGridView = new SoundplaneGridView();
	mpGridView->setModel(pModel);
	page0->addWidgetToView (mpGridView, GLRect1, "grid_view");		

	MLRect GLRect2(0, 3.5, pageWidth, 3.);
	mpTouchView = new SoundplaneTouchView();
	mpTouchView->setModel(pModel);
	page0->addWidgetToView (mpTouchView, GLRect2, "touch_view");	
	
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
		mpCarrierToggles[i] = page0->addToggleButton("", toggleRectTiny.withCenter(i*0.3 + 1., 5), tSym, c2);
		sprintf(numBuf, "%d", i);	
		mpCarrierLabels[i] = page0->addLabel(numBuf, toggleRectTiny.withCenter(i*0.3 + 1., 4.75));
	}
//	page0->addToggleButton("all", toggleRectTiny.withCenter(0*0.3 + 1., 6.), "all_toggle", c2);

	// controls
	//
	float dialY = 7.25;	// center line for dials		
	MLRect dialRect (0, 0, 1.25, 1.0);	
	MLRect dialRectSmall (0, 0, 1., 0.75);	
	MLRect buttonRect(0, 0, 1, 0.5);	
	MLRect toggleRect(0, 0, 1, 0.5);	
	MLRect textButtonRect(0, 0, 5.5, 0.4);
	MLRect textButtonRect2(0, 0, 2., 0.4);
	page0->addTextButton("recalibrate", textButtonRect.withCenter(2.75, 8.), "calibrate");

	// page0->addTextButton("test", textButtonRect2.withCenter(7., 8.), "test");

	pD = page0->addDial("view scale", dialRectSmall.withCenter(13.5, 0.75), "display_scale", c2);
	pD->setRange(0.01, 5., 0.01);	
	pD->setDefault(1.);	
	
	pD = page0->addDial("touches", dialRect.withCenter(0.5, dialY), "max_touches", c2);
	pD->setRange(0., 16., 1.);	
	pD->setDefault(4);	

	pD = page0->addDial("thresh", dialRect.withCenter(2.0, dialY), "z_thresh", c2);
	pD->setRange(0., 0.025, 0.001);	
	pD->setDefault(0.005);	
	
	pD = page0->addDial("max force", dialRect.withCenter(3.5, dialY), "z_max", c2);
	pD->setRange(0.01, 0.1, 0.001);	
	pD->setDefault(0.05);	
	
	pD = page0->addDial("z curve", dialRect.withCenter(5.0, dialY), "z_curve", c2);
	pD->setRange(0., 1., 0.01);	
	pD->setDefault(0.25);	

	pD = page0->addDial("lopass", dialRect.withCenter(7, dialY), "lopass", c2);
	pD->setRange(1., 250., 1);	
	pD->setDefault(100.);	
	
	pB = page0->addToggleButton("rotate", toggleRect.withCenter(8.5, dialY), "rotate", c2);
	
//	page0->addToggleButton("show frets", toggleRect.withCenter(10, dialY - 0.25), "frets", c2);
	mpViewModeButton = page0->addMenuButton("view mode", textButtonRect2.withCenter(13, 8.), "viewmode");

//	mpCurveGraph = page0->addGraph("zgraph", Colours::black);
	
	// page 1 - zones, OSC, MIDI
	//
	MLAppView* page1 = mpPages->addPage();
	
	MLDrawing* pDraw = page1->addDrawing(MLRect(0, 0, 11, 9));
	page1->renameWidget(pDraw, "page1_lines");
	int p[20]; // point indices
	p[0] = pDraw->addPoint(Vec2(7.25, 5.0));
	p[1] = pDraw->addPoint(Vec2(7.25, 8.5));
	p[2] = pDraw->addPoint(Vec2(10.75, 5.0));
	p[3] = pDraw->addPoint(Vec2(10.75, 8.5));
	pDraw->addOperation(MLDrawing::drawLine, p[0], p[1]);
	pDraw->addOperation(MLDrawing::drawLine, p[2], p[3]);
	
	// title
	pL = page1->addLabel("Zones", pageTitleRect, 1.5f, eMLTitle);
	pL->setJustification(Justification::centredLeft); 
	
//	mpGLView3 = new SoundplaneZoneView();
//	mpGLView3->setModel(pModel);
//	page1->addWidgetToView (mpGLView3, GLRect1, "zone_view");		
	
	MLRect zoneLabelRect(0, 0, 3., 0.25);
	float sectionLabelsY = 5.125;
	page1->addLabel("ZONE", zoneLabelRect.withCenter(3.75, sectionLabelsY), 1.00f, eMLTitle);
	page1->addLabel("MIDI", zoneLabelRect.withCenter(9., sectionLabelsY), 1.00f, eMLTitle);
	page1->addLabel("OSC", zoneLabelRect.withCenter(12.5, sectionLabelsY), 1.00f, eMLTitle);

	// all-zone controls up top
	float topDialsY = 0.66f;

	pB = page1->addToggleButton("quantize", toggleRect.withCenter(4.5, topDialsY), "quantize", c2);
	pB = page1->addToggleButton("note lock", toggleRect.withCenter(5.5, topDialsY), "lock", c2);
	pB = page1->addToggleButton("retrig", toggleRect.withCenter(6.5, topDialsY), "retrig", c2);

	pD = page1->addDial("portamento", dialRectSmall.withCenter(8., topDialsY), "snap", c2);
	pD->setRange(0., 1000., 10);	
	pD->setDefault(250.);	
	pD = page1->addDial("vibrato", dialRectSmall.withCenter(9., topDialsY), "vibrato", c2);
	pD->setRange(0., 1., 0.01);	
	pD->setDefault(0.5);	
	pD = page1->addDial("transpose", dialRectSmall.withCenter(10., topDialsY), "transpose", c2);
	pD->setRange(-24, 24., 1.);	
	pD->setDefault(0);	
	
	// ZONE	
	float bottomDialsY = 6.25f;
	float bottomDialsY2 = 7.25f;
	// for all zone types: start channel # (dial)
	// zone type: {note, note row, control}
	// if note: note #, single / multi
	// if note grid: start note, y skip, single / multi
	// if control: ctrl x, ctrl y, ctrl dx, ctrl dy

	// MIDI
	pB = page1->addToggleButton("active", toggleRect.withCenter(8, bottomDialsY), "midi_active", c2);
	pB = page1->addToggleButton("pressure", toggleRect.withCenter(8, bottomDialsY2), "midi_pressure_active", c2);
	pD = page1->addDial("rate", dialRect.withCenter(9, bottomDialsY), "data_freq_midi", c2);
	pD->setRange(10., 500., 10.);	
	pD->setDefault(250.);	
	pD = page1->addDial("bend range", dialRect.withCenter(10, bottomDialsY), "bend_range", c2);
	pD->setRange(0., 48., 1.);	
	
	
//	pB = page1->addToggleButton("one / multi", toggleRect.withCenter(6.5, 5.25), "single_multi", c2);
//	pB->setSplitMode(true);
//	pB->setEnabled(false);

//	pD = page1->addDial("channel", dialRectSmall.withCenter(4.5, 7.75), "channel", c2);
//	pD->setRange(1., 16., 1.);	

	pD->setDefault(48);	
	
	MLRect textButtonRect3(0, 0, 3., 0.4);
	mpMIDIDeviceButton = page1->addMenuButton("device", textButtonRect3.withCenter(9., 8.), "midi_device");
	
	// OSC
	
	pB = page1->addToggleButton("active", toggleRect.withCenter(11.5, bottomDialsY), "osc_active", c2);
	pD = page1->addDial("rate", dialRect.withCenter(12.5, bottomDialsY), "data_freq_osc", c2);
	pD->setRange(13.5, 500., 10.);	
	pD->setDefault(250.);	
	
	mpOSCServicesButton = page1->addMenuButton("destination", textButtonRect3.withCenter(12.5, 8.), "osc_services");

	// page 2 - expert stuff
	//
	MLAppView* page2 = mpPages->addPage();

	// title
	pL = page2->addLabel("Expert", pageTitleRect, 1.5f, eMLTitle);
	pL->setJustification(Justification::centredLeft); 
	
	// utility buttons
	page2->addTextButton("select carriers", MLRect(0, 1, 3, 0.4), "select_carriers");	
	page2->addTextButton("calibrate tracker", MLRect(3.5, 1, 3, 0.4), "calibrate_tracker");	
	page2->addTextButton("cancel", MLRect(3.5, 1.5, 3, 0.4), "calibrate_tracker_cancel");	
	page2->addTextButton("use default", MLRect(3.5, 2., 3, 0.4), "calibrate_tracker_default");	
	
	// tracker calibration view
	MLRect TCVRect(0, 3.f, 6.5, 3.); 
	mpTrkCalView = new TrackerCalibrateView();
	mpTrkCalView->setModel(pModel);
	page2->addWidgetToView (mpTrkCalView, TCVRect, "trk_cal_view");		

	// debug pane
	MLDebugDisplay* pDebug = page2->addDebugDisplay(MLRect(7., 1., 7., 5.));
	debug().setListener(pDebug);
	
	//page2->addToggleButton("pause", toggleRect.withCenter(13.5, 5.5), "debug_pause", c2);
	
	pD = page2->addDial("bg filter", dialRect.withCenter(2, dialY), "bg_filter", c2);
	pD->setRange(0.01, 1.0, 0.01);	
	pD->setDefault(0.05);
	
	pD = page2->addDial("hysteresis", dialRect.withCenter(4, dialY), "hysteresis", c2);
	pD->setRange(0.01, 1.0, 0.01);	
	pD->setDefault(0.5);
	
	pD = page2->addDial("template", dialRect.withCenter(6, dialY), "t_thresh", c2);
	pD->setRange(0., 1., 0.001);	
	pD->setDefault(0.2);
		
	pModel->addParamListener(this); 
	
}

SoundplaneView::~SoundplaneView()
{
	stopTimer();
}

void SoundplaneView::initialize()
{
	goToPage(0);	
		
	startTimer(500);
	
	setAnimationsActive(true);
	
	updateAllParams();
}

void SoundplaneView::timerCallback()
{
	if (mDoAnimations)
	{	
		/*
		if (mpGridView && mpGridView->isShowing())
		{
			mpGridView->repaint();
		}
		*/
		
		
	// TODO different intervals!

		// poll soundplane status and get info every second or so.
		// we don't have to know what the states mean -- just an index. 
		// if the index changes, pull current info from Soundplane and redraw.
		//
		if (mpFooter)
		{
			int state = mpModel->getDeviceState();
			int clientState = mpModel->getClientState();
			if ((state != mSoundplaneState) || (clientState != mSoundplaneClientState))
			{
				mpFooter->setDevice(mpModel->getHardwareStr());
				mpFooter->setStatus(mpModel->getStatusStr(), mpModel->getClientStr());
				repaint();
				mSoundplaneState = state;
				mSoundplaneClientState = clientState;
			}
		}
	}
	updateChangedParams();

}

/*
void SoundplaneView::modelStateChanged()
{
	// draw force curve
	//
	const float c = mpModel->getParam("zcurve");
	std::vector<float> p;
	p.resize(4);
	p[1] = 1.f - c;
	p[3] = c;
	if(mpCurveGraph)
	{
		mpCurveGraph->setPolyCoeffs(p);
		mpCurveGraph->repaint();
	}
}
*/

// --------------------------------------------------------------------------------
// MLModelListener implementation
// an updateChangedParams() is needed to get these actions sent by the Model.
//
void SoundplaneView::doParamChangeAction(MLSymbol param, const MLModelParam & oldVal, const MLModelParam & newVal)
{
	// debug() << "SoundplaneView::doParamChangeAction: " << param << " from " << oldVal << " to " << newVal << "\n";	
	if(param == "viewmode")
	{
		const std::string& v = newVal.getStringValue();		
		if(v == "raw data")
		{
			setViewMode(kRaw);
		}
		else if(v == "calibrated")
		{
			setViewMode(kCalibrated);
		}
		else if(v == "cooked")
		{
			setViewMode(kCooked);
		}
		else if(v == "xy")
		{
			setViewMode(kXY);
		}
		else if(v == "test")
		{
			setViewMode(kTest);
		}
		else if(v == "norm. map")
		{
			setViewMode(kNrmMap);
		}
	}
	else if(param == "")
	{
	
	}
}

SoundplaneViewMode SoundplaneView::getViewMode()
{
	return mViewMode;
}

void SoundplaneView::setViewMode(SoundplaneViewMode v)
{
	mViewMode = v;
	
	if (mpGridView)
	{
		mpGridView->setViewMode(v);
	}
		
	switch(v)
	{
		case kRaw:
			makeCarrierTogglesVisible(1);
			if(mpTouchView)
			{
				mpTouchView->setWidgetVisible(0);
			}
		break;
		default:
			makeCarrierTogglesVisible(0);
			if(mpTouchView)
			{
				mpTouchView->setWidgetVisible(1);
			}
		break;
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
}			

// TODO make this code part of menus!!!

void SoundplaneView::setMIDIDeviceString(const std::string& str)
{	
	// TODO auto get button text from menu code
	if(mpMIDIDeviceButton)
		mpMIDIDeviceButton->setButtonText(String(str.c_str()));
}

void SoundplaneView::setOSCServicesString(const std::string& str)
{	
	// TODO auto get button text from menu code
	if(mpOSCServicesButton)
		mpOSCServicesButton->setButtonText(String(str.c_str()));
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

// SoundplaneView just listens to the prev / next buttons directly.
// 
void SoundplaneView::buttonClicked (MLButton* pButton)
{
	MLSymbol p (pButton->getParamName());
	if (!mpPages) return;
	int page = mpPages->getCurrentPage();
	if(p == "prev")
	{
		goToPage(--page);
	}
	else if (p == "next")
	{
		goToPage(++page);
	}		
}

