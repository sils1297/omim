#include "../base/SRC_FIRST.hpp"
#include "../base/mutex.hpp"
#include "../base/timer.hpp"
#include "../base/logging.hpp"

#include "../std/bind.hpp"

#include "../yg/internal/opengl.hpp"
#include "../yg/render_state.hpp"
#include "../yg/rendercontext.hpp"
#include "../yg/framebuffer.hpp"
#include "../yg/renderbuffer.hpp"
#include "../yg/texture.hpp"
#include "../yg/resource_manager.hpp"
#include "../yg/screen.hpp"
#include "../yg/pen_info.hpp"
#include "../yg/skin.hpp"

#include "../indexer/scales.hpp"

#include "events.hpp"
#include "drawer_yg.hpp"
#include "window_handle.hpp"
#include "render_queue_routine.hpp"
RenderQueueRoutine::RenderModelCommand::RenderModelCommand(ScreenBase const & frameScreen,
                                                             render_fn_t renderFn)
                                                               : m_frameScreen(frameScreen),
                                                               m_renderFn(renderFn)
{}

RenderQueueRoutine::RenderQueueRoutine(shared_ptr<yg::gl::RenderState> const & renderState,
                                       string const & skinName,
                                       bool isMultiSampled,
                                       bool doPeriodicalUpdate,
                                       double updateInterval,
                                       bool isBenchmarking,
                                       unsigned scaleEtalonSize,
                                       double visualScale,
                                       yg::Color const & bgColor)
{
  m_skinName = skinName;
  m_visualScale = visualScale;
  m_renderState = renderState;
  m_renderState->addInvalidateFn(bind(&RenderQueueRoutine::invalidate, this));
  m_isMultiSampled = isMultiSampled;
  m_doPeriodicalUpdate = doPeriodicalUpdate;
  m_updateInterval = updateInterval;
  m_isBenchmarking = isBenchmarking;
  m_scaleEtalonSize = scaleEtalonSize;
  m_bgColor = bgColor;
}

void RenderQueueRoutine::Cancel()
{
  IRoutine::Cancel();
  /// Waking up the sleeping thread...
  m_hasRenderCommands.Signal();
  /// ...Or cancelling the current rendering command in progress.
  if (m_currentRenderCommand != 0)
    m_currentRenderCommand->m_paintEvent->Cancel();
}

void RenderQueueRoutine::processResize(ScreenBase const & frameScreen)
{
  if (m_renderState->m_isResized)
  {
    size_t texW = m_renderState->m_textureWidth;
    size_t texH = m_renderState->m_textureHeight;

    m_renderState->m_depthBuffer.reset();

    if (!m_isMultiSampled)
    {
      m_renderState->m_depthBuffer = make_shared_ptr(new yg::gl::RenderBuffer(texW, texH, true));
      m_threadDrawer->screen()->frameBuffer()->setDepthBuffer(m_renderState->m_depthBuffer);
    }

    m_threadDrawer->onSize(texW, texH);
    m_threadDrawer->screen()->frameBuffer()->onSize(texW, texH);

    shared_ptr<yg::gl::BaseTexture> oldActualTarget = m_renderState->m_actualTarget;

    m_renderState->m_actualTarget.reset();
    m_renderState->m_actualTarget = m_resourceManager->createRenderTarget(texW, texH);

    m_auxScreen->onSize(texW, texH);
    m_auxScreen->setRenderTarget(m_renderState->m_actualTarget);
    m_auxScreen->beginFrame();
    m_auxScreen->clear(m_bgColor);

    if (oldActualTarget != 0)
    {
      m_auxScreen->blit(oldActualTarget,
                        m_renderState->m_actualScreen,
                        frameScreen);
      oldActualTarget.reset();
    }
    m_auxScreen->endFrame();

    for (size_t i = 0; i < m_renderState->m_backBufferLayers.size(); ++i)
    {
      shared_ptr<yg::gl::BaseTexture> oldBackBuffer = m_renderState->m_backBufferLayers[i];
      m_renderState->m_backBufferLayers[i].reset();
      m_renderState->m_backBufferLayers[i] = m_resourceManager->createRenderTarget(texW, texH);
      m_auxScreen->setRenderTarget(m_renderState->m_backBufferLayers[i]);
      m_auxScreen->beginFrame();
      m_auxScreen->clear(m_bgColor);

      if (oldBackBuffer != 0)
      {
        m_auxScreen->blit(oldBackBuffer,
                          m_renderState->m_actualScreen,
                          frameScreen);
        oldBackBuffer.reset();
      }
      m_auxScreen->endFrame();

      m_renderState->m_actualScreen = frameScreen;
    }

    m_renderState->m_isResized = false;
  }
}

void RenderQueueRoutine::getUpdateAreas(
    ScreenBase const & oldScreen,
    m2::RectI const & oldRect,
    ScreenBase const & newScreen,
    m2::RectI const & newRect,
    vector<m2::RectI> & areas)
{
  areas.clear();

  if (IsPanningAndRotate(oldScreen, newScreen))
  {
    m2::RectD o(newScreen.GtoP(oldScreen.PtoG(m2::PointD(oldRect.minX(), oldRect.minY()))),
                 newScreen.GtoP(oldScreen.PtoG(m2::PointD(oldRect.maxX(), oldRect.maxY()))));
    m2::RectD n(newRect);

    /// checking two corner cases
    if (o.IsRectInside(n))
      return;
    if (!o.IsIntersect(n))
    {
      areas.push_back(newRect);
      return;
    }

    double leftBarMinX = 0;
    double leftBarMaxX = 0;
    double rightBarMinX = n.maxX();
    double rightBarMaxX = n.maxX();
    double topBarMinY = 0;
    double topBarMaxY = 0;
    double bottomBarMinY = n.maxY();
    double bottomBarMaxY = n.maxY();

    if (o.minX() > n.minX())
      leftBarMaxX = ceil(o.minX());
    if (o.maxX() < n.maxX())
      rightBarMinX = floor(o.maxX());
    if (o.minY() > n.minY())
      topBarMaxY = ceil(o.minY());
    if (o.maxY() < n.maxY())
      bottomBarMinY = floor(o.maxY());

    if ((int)leftBarMinX != (int)leftBarMaxX)
      areas.push_back(m2::RectI((int)leftBarMinX, (int)topBarMinY, (int)leftBarMaxX, (int)bottomBarMinY));
    if ((int)topBarMinY != (int)topBarMaxY)
      areas.push_back(m2::RectI((int)leftBarMaxX, (int)topBarMinY, (int)rightBarMaxX, (int)topBarMaxY));
    if ((int)rightBarMinX != (int)rightBarMaxX)
      areas.push_back(m2::RectI((int)rightBarMinX, (int)topBarMaxY, (int)rightBarMaxX, (int)bottomBarMaxY));
    if ((int)bottomBarMinY != (int)bottomBarMaxY)
      areas.push_back(m2::RectI((int)leftBarMinX, (int)bottomBarMinY, (int)rightBarMinX, (int)bottomBarMaxY));
  }
  else
  {
    areas.push_back(newRect);

/*    int rectW = (w + 9) / 5;
    int rectH = (h + 9) / 5;
    m2::RectI r( 2 * rectW, 2 * rectH, 3 * rectW, 3 * rectH);
    areas.push_back(r);
    areas.push_back(m2::Offset(r, -rectW, 0));
    areas.push_back(m2::Offset(r, -rectW, -rectH));
    areas.push_back(m2::Offset(r, 0, -rectH));
    areas.push_back(m2::Offset(r, rectW, -rectH));
    areas.push_back(m2::Offset(r, rectW, 0));
    areas.push_back(m2::Offset(r, rectW, rectH));
    areas.push_back(m2::Offset(r, 0, rectH));
    areas.push_back(m2::Offset(r, -rectW, rectH));
    areas.push_back(m2::Offset(r, -2 * rectW, rectH));
    areas.push_back(m2::Offset(r, -2 * rectW, 0));
    areas.push_back(m2::Offset(r, -2 * rectW, -rectH));
    areas.push_back(m2::Offset(r, -2 * rectW, -2*rectH));
    areas.push_back(m2::Offset(r, -rectW, -2*rectH));
    areas.push_back(m2::Offset(r, 0, -2*rectH));
    areas.push_back(m2::Offset(r, rectW, -2*rectH));
    areas.push_back(m2::Offset(r, 2 * rectW, -2*rectH));
    areas.push_back(m2::Offset(r, 2 * rectW, -rectH));
    areas.push_back(m2::Offset(r, 2 * rectW, 0));
    areas.push_back(m2::Offset(r, 2 * rectW, rectH));
    areas.push_back(m2::Offset(r, 2 * rectW, 2*rectH));
    areas.push_back(m2::Offset(r, rectW, 2*rectH));
    areas.push_back(m2::Offset(r, 0, 2*rectH));
    areas.push_back(m2::Offset(r, -rectW, 2*rectH));
    areas.push_back(m2::Offset(r, -2*rectW, 2*rectH));
*/
  }
}

void RenderQueueRoutine::setVisualScale(double visualScale)
{
  m_visualScale = visualScale;
}

void RenderQueueRoutine::waitForRenderCommand(list<shared_ptr<RenderModelCommand> > & cmdList,
                                              threads::ConditionGuard & guard)
{
  while (cmdList.empty())
  {
    guard.Wait();
    if (IsCancelled())
      break;
  }
}

void RenderQueueRoutine::Do()
{
  m_renderContext->makeCurrent();

  m_frameBuffer = make_shared_ptr(new yg::gl::FrameBuffer());

  DrawerYG::params_t params;

  params.m_resourceManager = m_resourceManager;
  params.m_frameBuffer = m_frameBuffer;
  params.m_renderState = m_renderState;
  params.m_doPeriodicalUpdate = m_doPeriodicalUpdate;
  params.m_updateInterval = m_updateInterval;
  params.m_skinName = m_skinName;
  params.m_visualScale = m_visualScale;
  params.m_threadID = 0;
  params.m_glyphCacheID = m_resourceManager->renderThreadGlyphCacheID(0);
/*  params.m_isDebugging = true;
  params.m_drawPathes = false;
  params.m_drawAreas = false;
  params.m_drawTexts = false;*/

  m_threadDrawer = make_shared_ptr(new DrawerYG(params));

  yg::gl::Screen::Params auxParams;
  auxParams.m_frameBuffer = make_shared_ptr(new yg::gl::FrameBuffer());
  auxParams.m_resourceManager = m_resourceManager;

  m_auxScreen = make_shared_ptr(new yg::gl::Screen(auxParams));

  bool isPanning = false;
  bool doRedrawAll = false;
  bool fullRectRepaint = false;
  /// update areas in pixel coordinates.
  vector<m2::RectI> areas;

  m2::RectI  surfaceRect;
  m2::RectI  textureRect;

  shared_ptr<yg::InfoLayer> infoLayer(new yg::InfoLayer());
  infoLayer->setCouldOverlap(false);
  m_threadDrawer->screen()->setInfoLayer(infoLayer);

  while (!IsCancelled())
  {
    {
      threads::ConditionGuard guard(m_hasRenderCommands);

      waitForRenderCommand(m_renderCommands, guard);

      if (IsCancelled())
        break;

      m_currentRenderCommand = m_renderCommands.front();
      m_renderCommands.erase(m_renderCommands.begin());

      m_currentRenderCommand->m_paintEvent = make_shared_ptr(new PaintEvent(m_threadDrawer.get()));

      /// this prevents the framework from flooding us with a bunch of RenderCommands with the same screen.
      {
        threads::MutexGuard guard(*m_renderState->m_mutex.get());

        m_renderState->m_currentScreen = m_currentRenderCommand->m_frameScreen;

        m2::RectI prevRect(0, 0, 0, 0);
        ScreenBase prevScreen = m_renderState->m_actualScreen;

        if (m_renderState->m_actualTarget != 0)
          prevRect = m2::RectI(0, 0,
                               m_renderState->m_actualTarget->width(),
                               m_renderState->m_actualTarget->height());

        processResize(m_currentRenderCommand->m_frameScreen);

        /// saving parameters, which might be changed from the GUI thread for later use.
        surfaceRect = m2::RectI(0, 0, m_renderState->m_surfaceWidth, m_renderState->m_surfaceHeight);
        textureRect = m2::RectI(0, 0, m_renderState->m_textureWidth, m_renderState->m_textureHeight);

        m2::RectI curRect = textureRect;

        doRedrawAll = m_renderState->m_doRepaintAll;

        fullRectRepaint = false;

        if (m_renderState->m_doRepaintAll)
        {
          areas.clear();
          areas.push_back(curRect);
          fullRectRepaint = true;
          m_threadDrawer->screen()->infoLayer()->clear();
          m_renderState->m_doRepaintAll = false;
        }
        else
        {
          getUpdateAreas(prevScreen,
                         prevRect,
                         m_currentRenderCommand->m_frameScreen,
                         curRect,
                         areas);
          if ((areas.size() == 1) && (areas[0] == curRect))
            fullRectRepaint = true;
        }

        isPanning = IsPanningAndRotate(prevScreen, m_renderState->m_currentScreen);

        if (isPanning)
        {
          math::Matrix<double, 3, 3> offsetM = prevScreen.PtoGMatrix() * m_renderState->m_currentScreen.GtoPMatrix();
          m2::RectD oldRect = m2::RectD(m2::PointD(prevRect.minX(), prevRect.minY()) * offsetM,
                                        m2::PointD(prevRect.maxX(), prevRect.maxY()) * offsetM);
          m2::RectD redrawTextRect(curRect);

          if (!redrawTextRect.Intersect(oldRect))
            redrawTextRect = m2::RectD(0, 0, 0, 0);

          m_threadDrawer->screen()->infoLayer()->offset(
                m2::PointD(0, 0) * offsetM,
                redrawTextRect
                );

/*          shared_ptr<yg::InfoLayer> infoLayer(new yg::InfoLayer());
          infoLayer->merge(*m_threadDrawer->screen()->infoLayer().get(),
                           prevScreen.PtoGMatrix() * m_renderState->m_currentScreen.GtoPMatrix());

          m_threadDrawer->screen()->setInfoLayer(infoLayer);*/
        }
        else
          m_threadDrawer->screen()->infoLayer()->clear();
      }
    }

    my::Timer timer;

    /// At this point renderQueue->mutex is released to allow
    /// main thread to add new rendering tasks and blit already
    /// rendered model while the current command is actually rendering.

    if (m_currentRenderCommand != 0)
    {
      /// this fixes some strange issue with multisampled framebuffer.
      /// setRenderTarget should be made here.
      m_threadDrawer->screen()->setRenderTarget(m_renderState->m_backBufferLayers.front());

      m_threadDrawer->beginFrame();

      m_threadDrawer->screen()->enableClipRect(true);
      m_threadDrawer->screen()->setClipRect(textureRect);
      m_threadDrawer->clear(m_bgColor);

      if ((isPanning) && (!doRedrawAll))
      {
        m_threadDrawer->screen()->blit(
            m_renderState->m_actualTarget,
            m_renderState->m_actualScreen,
            m_renderState->m_currentScreen);
      }

      //m_threadDrawer->screen()->setNeedTextRedraw(isPanning);

      ScreenBase const & frameScreen = m_currentRenderCommand->m_frameScreen;
      m2::RectD glbRect;
      frameScreen.PtoG(m2::RectD(m2::RectD(textureRect).Center() - m2::PointD(m_scaleEtalonSize / 2, m_scaleEtalonSize / 2),
                                 m2::RectD(textureRect).Center() + m2::PointD(m_scaleEtalonSize / 2, m_scaleEtalonSize / 2)),
                       glbRect);
//      frameScreen.PtoG(m2::RectD(surfaceRect), glbRect);
      int scaleLevel = scales::GetScaleLevel(glbRect);

      for (size_t i = 0; i < areas.size(); ++i)
      {
        if ((areas[i].SizeX() != 0) && (areas[i].SizeY() != 0))
        {
          frameScreen.PtoG(m2::Inflate<double>(m2::RectD(areas[i]), 30 * m_visualScale, 30 * m_visualScale), glbRect);
          if ((glbRect.SizeX() == 0) || (glbRect.SizeY() == 0))
            continue;

          m_threadDrawer->screen()->setClipRect(areas[i]);

          m_currentRenderCommand->m_renderFn(
              m_currentRenderCommand->m_paintEvent,
              m_currentRenderCommand->m_frameScreen,
              glbRect,
              glbRect,
              scaleLevel);
        }
      }

      /// if something were actually drawn, or (exclusive or) we are repainting the whole rect
      if ((!m_renderState->m_isEmptyModelCurrent) || (fullRectRepaint))
        m_renderState->m_isEmptyModelActual = m_renderState->m_isEmptyModelCurrent;

      /// setting the "whole texture" clip rect to render texts opened by panning.
      m_threadDrawer->screen()->setClipRect(textureRect);

      m_threadDrawer->screen()->infoLayer()->draw(m_threadDrawer->screen().get(), math::Identity<double, 3>());

      m_threadDrawer->endFrame();
    }

    double duration = timer.ElapsedSeconds();

    if (!IsCancelled())
    {
      /// We shouldn't update the actual target as it's already happened (through callback function)
      /// in the endFrame function
      /// updateActualTarget();

      {
        threads::ConditionGuard g1(m_hasRenderCommands);

        {
          threads::MutexGuard g2(*m_renderState->m_mutex.get());
          m_renderState->m_duration = duration;
        }

        m_currentRenderCommand.reset();

        if (m_renderCommands.empty())
          g1.Signal();
      }

      invalidate();
    }

  }

  // By VNG: We can't destroy render context in drawing thread.
  // Notify render context instead.
  m_renderContext->endThreadDrawing();
}

void RenderQueueRoutine::addWindowHandle(shared_ptr<WindowHandle> window)
{
  m_windowHandles.push_back(window);
}

void RenderQueueRoutine::invalidate()
{
  for_each(m_windowHandles.begin(),
           m_windowHandles.end(),
           bind(&WindowHandle::invalidate, _1));
}

void RenderQueueRoutine::addCommand(render_fn_t const & fn, ScreenBase const & frameScreen)
{
  /// Command queue modification is syncronized by mutex
  threads::ConditionGuard guard(m_hasRenderCommands);

  bool needToSignal = m_renderCommands.empty();

  /// Clearing a list of commands.
  m_renderCommands.clear();

  /// pushing back new RenderCommand.
  m_renderCommands.push_back(make_shared_ptr(new RenderModelCommand(frameScreen, fn)));

  /// if we are in benchmarking mode, we shouldn't cancel any render command
  /// else, if we are not panning, we should cancel the render command in progress to start a new one
  if ((m_currentRenderCommand != 0)
   && (!IsPanningAndRotate(m_currentRenderCommand->m_frameScreen, frameScreen)))
    m_currentRenderCommand->m_paintEvent->Cancel();

  if (needToSignal)
    guard.Signal();
}

void RenderQueueRoutine::initializeGL(shared_ptr<yg::gl::RenderContext> const & renderContext,
                                      shared_ptr<yg::ResourceManager> const & resourceManager)
{
  m_renderContext = renderContext;
  m_resourceManager = resourceManager;
}

void RenderQueueRoutine::memoryWarning()
{
  m_threadDrawer->screen()->memoryWarning();
}

void RenderQueueRoutine::enterBackground()
{
  m_threadDrawer->screen()->enterBackground();
}

void RenderQueueRoutine::enterForeground()
{
  m_threadDrawer->screen()->enterForeground();
}

void RenderQueueRoutine::waitForEmptyAndFinished()
{
  /// Command queue modification is syncronized by mutex
  threads::ConditionGuard guard(m_hasRenderCommands);

  if (!m_renderCommands.empty() || (m_currentRenderCommand != 0))
    guard.Wait();
}
