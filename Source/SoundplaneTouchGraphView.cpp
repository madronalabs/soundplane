
// Part of the Soundplane client software by Madrona Labs.
// Copyright (c) 2013 Madrona Labs LLC. http://www.madronalabs.com
// Distributed under the MIT license: http://madrona-labs.mit-license.org/

#include "SoundplaneTouchGraphView.h"

SoundplaneTouchGraphView::SoundplaneTouchGraphView(MLWidget* pContainer) :
MLWidget(pContainer),
mpModel(nullptr)
{
  setInterceptsMouseClicks (false, false);
  MLWidget::setComponent(this);
  setupGL();

  mTimer.start([&](){ triggerRepaint(); }, milliseconds(1000/60));
}

SoundplaneTouchGraphView::~SoundplaneTouchGraphView()
{
}

void SoundplaneTouchGraphView::setModel(SoundplaneModel* m)
{
  mpModel = m;
}

void SoundplaneTouchGraphView::mouseDrag (const MouseEvent& e)
{
}

void SoundplaneTouchGraphView::setupOrthoView()
{
  int viewW = getBackingLayerWidth();
  int viewH = getBackingLayerHeight();
  MLGL::orthoView(viewW, viewH);
}

void SoundplaneTouchGraphView::renderTouchBarGraphs()
{
  if (!mpModel) return;

  int viewW = getBackingLayerWidth();
  int viewH = getBackingLayerHeight();
  int viewScale = getRenderingScale();

  const MLSignal& currentTouch = mpModel->getTouchFrame();
  const MLSignal& touchHistory = mpModel->getTouchHistory();
  const int frames = mpModel->getFloatProperty("max_touches");
  if (!frames) return;

  int margin = viewH / 30;
  int numSize = margin*2;
  int left = margin*2 + numSize;

  int right = viewW - margin;
  int top = margin;
  int bottom = viewH - margin;

  int frameWidth = right - left;
  int frameOffset = (bottom - top)/frames;
  int frameHeight = frameOffset - margin;
  MLRect frameSize(0, 0, frameWidth, frameHeight);

  setupOrthoView();

  for(int j=0; j<frames; ++j)
  {
    // graph frame background
    float p = 0.85f;
    glColor4f(p, p, p, 1.0f);
    MLRect fr = frameSize.translated(Vec2(left, margin + j*frameOffset));
    MLGL::fillRect(fr);

    // graph frame border
    p = 0.1f;
    glColor4f(p, p, p, 1.0f);
    MLGL::strokeRect(fr, viewScale);

    // draw touch activity indicators at left
    float indDark[4];
    float indLight[4];
    float indDarker[4];
    const float* co = MLGL::getIndicatorColor(j);
    for(int i=0; i<4; ++i) // brighten indicator color, TODO move to utilities
    {
      indDark[i] = co[i];
      indLight[i] = ml::clamp(co[i] + 0.3f, 0.f, 1.f);
      indDarker[i] = ml::clamp(co[i] - 0.3f, 0.f, 1.f);
    }
    indDark[3] = indLight[3] = indDarker[3] = 1.f;
    glColor4fv(indLight);
    MLRect r(0, 0, numSize, numSize);
    MLRect tr = r.translated(Vec2(margin, margin + j*frameOffset + (frameHeight - numSize)/2));
    int age = currentTouch(ageColumn, j);
    if (age > 0)
    {
      glColor4fv(indLight);
      MLGL::fillRect(tr);
      glColor4fv(indDark);
      MLGL::strokeRect(tr, viewScale);
    }
    else
    {
      p = 0.6f;
      glColor4f(p, p, p, 1.f);
      MLGL::fillRect(tr);
      p = 0.1f;
      glColor4f(p, p, p, 1.f);
      MLGL::strokeRect(tr, viewScale);
    }

    // draw history
    glColor4fv(indDark);
    MLRange frameXRange(fr.left(), fr.right());
    frameXRange.convertTo(MLRange(0, (float)kSoundplaneHistorySize));
    MLRange frameYRange(1., 0.);
    frameYRange.convertTo(MLRange(fr.bottom(), fr.top()));
    glBegin(GL_LINES);
    for(int i=fr.left() + 1; i<fr.right()-1; ++i)
    {
      int time = frameXRange(i);
      float force = touchHistory(2, j, time);
      force =  ml::clamp(force, 0.f, 1.f);
      float y = frameYRange.convert(force);
      // draw line
      glVertex2f(i, fr.top());
      glVertex2f(i, y);
    }
    glEnd();

    /*
     // DEBUG to draw x position graph
     glLineWidth(viewH / 100.f);
     MLRange xToYRange(0., 30., fr.top() + margin, fr.bottom() - margin);
     glColor4fv(indDarker);

     glBegin(GL_LINES);
     for(int i=fr.left() + 1; i<fr.right()-1; ++i)
     {
     int time = frameXRange(i);

     float x = touchHistory(0, j, time);
     float y = xToYRange.convert(x);

     // draw line
     glVertex2f(i, y);

     }
     glEnd();
     */

  }
}

void SoundplaneTouchGraphView::renderOpenGL()
{
  if (!mpModel) return;
  if (!mpModel->getDeviceState()) return;
  const Colour c = findColour(MLLookAndFeel::backgroundColor);
  OpenGLHelpers::clear (c);
  renderTouchBarGraphs();
}



