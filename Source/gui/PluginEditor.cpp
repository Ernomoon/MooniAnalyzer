#include "PluginEditor.h"

namespace mooni
{
using namespace ui;
using juce::Point;

static const char* kRoomNames[] = { "Обсерватория", "Кратер частот", "Приливы", "Гравитация", "Эталон", "Журнал" };
static const char* kRoomTips[] = {
    "Главный пульт: оценка, проблемы и быстрые действия",
    "Эквалайзер: рельеф микса, ориентир и рекомендации",
    "Стерео: ширина по полосам, фаза и моно-совместимость",
    "Громкость: максимайзер, цель в LUFS и расчёт драйва",
    "Эталонный трек для сравнения (необязательно)",
    "Журнал: история прослушиваний, A/B и отмена шагов" };

MooniEditor::MooniEditor (MooniProcessor& p) : AudioProcessorEditor (p), proc (p), tooltips (nullptr, 600), ctx (p)
{
    setLookAndFeel (&laf);
    juce::LookAndFeel::setDefaultLookAndFeel (&laf);
    tooltips.setLookAndFeel (&laf);
    addAndMakeVisible (canvas);

    ctx.goRoom = [this] (int r) { setRoom (r); };
    ctx.toast = [this] (const juce::String& t, juce::Colour c) { toast.push (t, c); };
    ctx.action = [this] (const juce::String& id, const juce::String& d) { if (brain) brain->onAction (id, d); };
    ctx.listenBand = [this] (float lo, float hi) { toggleListenBand (lo, hi); };

    canvas.addAndMakeVisible (world);
    world.setBounds (0, 0, kW, kH);
    world.onEarthClicked = [this] { if (brain) brain->onEarthClicked(); };
    world.onBipClicked = [this] { if (brain) brain->onBipClicked(); };

    rooms.push_back (std::make_unique<ObservatoryRoom> (ctx));
    rooms.push_back (std::make_unique<EqRoom> (ctx));
    rooms.push_back (std::make_unique<TidesRoom> (ctx));
    rooms.push_back (std::make_unique<GravityRoom> (ctx));
    rooms.push_back (std::make_unique<ReferenceRoom> (ctx));
    rooms.push_back (std::make_unique<LogbookRoom> (ctx));
    for (auto& r : rooms) { canvas.addChildComponent (*r); r->setBounds (0, kTop, kW, kH - kTop - kBottom); }
    canvas.addAndMakeVisible (bars);
    bars.setBounds (0, 0, kW, kH);

    // top bar
    for (int i = 0; i < 6; ++i)
    {
        auto* b = nav.add (new juce::TextButton (juce::String::fromUTF8 (kRoomNames[i])));
        setButtonStyle (*b, ButtonStyle::Pill);
        b->setTooltip (juce::String::fromUTF8 (kRoomTips[i]) + ru ("  (Ctrl+") + juce::String (i + 1) + ")");
        b->onClick = [this, i] { setRoom (i); };
        canvas.addAndMakeVisible (b);
    }
    undoBtn.setButtonText (juce::String::fromUTF8 ("↶"));  undoBtn.setTooltip (ru ("Отменить (Ctrl+Z)"));
    redoBtn.setButtonText (juce::String::fromUTF8 ("↷"));  redoBtn.setTooltip (ru ("Вернуть (Ctrl+Shift+Z)"));
    undoBtn.onClick = [this] { if (proc.undoStep() && brain) brain->onAction ("undo"); };
    redoBtn.onClick = [this] { if (proc.redoStep() && brain) brain->onAction ("redo"); };
    abBtn.setTooltip (ru ("Два набора настроек: переключай и сравнивай"));
    abBtn.onClick = [this] { proc.abSwitch(); if (brain) brain->onAction ("ab"); };
    fairBtn.setButtonText (ru ("Честно"));
    fairBtn.setTooltip (ru ("Честное сравнение: при Затмении оригинал звучит так же громко, как обработка. Громче ≠ лучше."));
    setButtonStyle (fairBtn, ButtonStyle::Pill);
    fairBtn.onClick = [this] { auto s = proc.settings; s.fairCompare = ! s.fairCompare; proc.setSettings (s); if (s.fairCompare && brain) brain->onAction ("fair"); };
    bypassBtn.setButtonText (ru ("ЗАТМЕНИЕ"));
    bypassBtn.setClickingTogglesState (true);
    bypassBtn.setTooltip (ru ("Обход всей обработки (EQ, стерео, пресс). Анализ продолжает работать. Латентность не меняется, щелчков нет."));
    setButtonStyle (bypassBtn, ButtonStyle::Danger);
    bypassAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (proc.apvts, params::bypass, bypassBtn);
    gearBtn.setButtonText (juce::String::fromUTF8 ("⚙")); gearBtn.setTooltip (ru ("Настройки"));
    gearBtn.onClick = [this] { showSettings (true); };
    infoBtn.setButtonText ("i"); infoBtn.setTooltip (ru ("О станции"));
    infoBtn.onClick = [this] { showAbout (true); };
    for (auto* b : { &undoBtn, &redoBtn, &abBtn, &fairBtn, &bypassBtn, &gearBtn, &infoBtn }) canvas.addAndMakeVisible (*b);
    for (auto* b : { &undoBtn, &redoBtn, &gearBtn, &infoBtn }) setButtonStyle (*b, ButtonStyle::Ghost);

    stopListenBtn.setButtonText (ru ("Хватит подслушивать"));
    stopListenBtn.onClick = [this] { proc.bandListen = false; };
    monitorResetBtn.setButtonText (ru ("Обычный звук"));
    monitorResetBtn.onClick = [this] { proc.monitor = (int) MonitorMode::Normal; proc.referenceListen = false; };
    setButtonStyle (stopListenBtn, ButtonStyle::Primary);
    setButtonStyle (monitorResetBtn, ButtonStyle::Primary);
    canvas.addChildComponent (stopListenBtn);
    canvas.addChildComponent (monitorResetBtn);

    eclipse.setInterceptsMouseClicks (false, false);
    canvas.addAndMakeVisible (eclipse);
    eclipse.setBounds (0, kTop, kW, kH - kTop - kBottom);

    canvas.addAndMakeVisible (mooni);
    mooni.setBounds (0, 0, kW, kH);
    mooni.onPoked = [this] { if (brain) brain->onPoked(); };
    canvas.addAndMakeVisible (bubble);
    bubble.setBounds (0, 0, kW, kH);
    canvas.addAndMakeVisible (toast);
    toast.setBounds (kW - 420, kH - kBottom - 96, 404, 84);

    backdrop.modal = true;
    backdrop.onClick = [this] { showSettings (false); showAbout (false); };
    canvas.addChildComponent (backdrop);
    backdrop.setBounds (0, 0, kW, kH);

    brain = std::make_unique<MooniBrain> (proc, mooni, [this] (const juce::String& t, float s) { bubble.say (t, s); });

    setResizable (true, true);
    if (auto* c = getConstrainer()) c->setFixedAspectRatio ((double) kW / kH);
    setResizeLimits (900, 585, 1800, 1170);
    const float sc = juce::jlimit (0.75f, 1.5f, proc.settings.uiScale);
    setSize ((int) (kW * sc), (int) (kH * sc));
    setWantsKeyboardFocus (true);

    setRoom (juce::jlimit (0, 5, proc.lastRoom));
    updateReport();
    lastTick = juce::Time::getMillisecondCounterHiRes();
    startTimerHz (30);
}

MooniEditor::~MooniEditor()
{
    stopTimer();
    proc.bandListen = false;
    juce::LookAndFeel::setDefaultLookAndFeel (nullptr);
    setLookAndFeel (nullptr);
}

void MooniEditor::resized()
{
    const float s = (float) getWidth() / (float) kW;
    canvas.setBounds (0, 0, kW, kH);
    canvas.setTransform (juce::AffineTransform::scale (s));
    proc.settings.uiScale = s;

    int x = 236;
    const int widths[] = { 106, 110, 80, 96, 70, 70 };
    for (int i = 0; i < nav.size(); ++i) { nav[i]->setBounds (x, 16, widths[i], 32); x += widths[i] + 6; }
    undoBtn.setBounds (790, 16, 34, 32);
    redoBtn.setBounds (828, 16, 34, 32);
    abBtn.setBounds (870, 16, 56, 32);
    fairBtn.setBounds (932, 16, 70, 32);
    bypassBtn.setBounds (1008, 14, 104, 36);
    gearBtn.setBounds (1118, 16, 32, 32);
    infoBtn.setBounds (1154, 16, 32, 32);
    stopListenBtn.setBounds (826, kH - 50, 172, 32);
    monitorResetBtn.setBounds (826, kH - 50, 172, 32);
}

void MooniEditor::setRoom (int r)
{
    r = juce::jlimit (0, (int) rooms.size() - 1, r);
    const bool changed = r != room || ! rooms[(size_t) r]->isVisible();
    room = r;
    for (size_t i = 0; i < rooms.size(); ++i) rooms[i]->setVisible ((int) i == r);
    for (int i = 0; i < nav.size(); ++i) nav[i]->setToggleState (i == r, juce::dontSendNotification);
    world.setRoom (r);
    proc.lastRoom = r;
    rooms[(size_t) r]->reportChanged();
    const auto spot = rooms[(size_t) r]->mooniSpot();
    mooni.setAnchor (spot.centre + Point<float> (0, (float) kTop), spot.radius, changed && lastTick > 0);
    if (changed && brain) brain->onRoomChanged (r);
    canvas.repaint();
}

void MooniEditor::showSettings (bool b)
{
    if (b)
    {
        showAbout (false);
        settingsPanel = std::make_unique<SettingsPanel> (ctx);
        settingsPanel->onClose = [this] { showSettings (false); };
        canvas.addAndMakeVisible (*settingsPanel);
        settingsPanel->setBounds (kW / 2 - 320, 130, 640, 500);
        backdrop.setVisible (true); backdrop.toFront (false); settingsPanel->toFront (true);
    }
    else if (settingsPanel) { settingsPanel.reset(); backdrop.setVisible (aboutPanel != nullptr); }
}

void MooniEditor::showAbout (bool b)
{
    if (b)
    {
        showSettings (false);
        aboutPanel = std::make_unique<AboutPanel> (ctx);
        aboutPanel->onClose = [this] { showAbout (false); };
        canvas.addAndMakeVisible (*aboutPanel);
        aboutPanel->setBounds (kW / 2 - 330, 110, 660, 540);
        backdrop.setVisible (true); backdrop.toFront (false); aboutPanel->toFront (true);
        mooni.play ("bow", true);
    }
    else if (aboutPanel) { aboutPanel.reset(); backdrop.setVisible (settingsPanel != nullptr); }
}

void MooniEditor::toggleListenBand (float lo, float hi)
{
    if (hi <= lo) return;
    const bool same = proc.bandListen.load() && std::abs (proc.bandListenLo.load() - lo) < 1 && std::abs (proc.bandListenHi.load() - hi) < 1;
    if (same) { proc.bandListen = false; return; }
    proc.bandListenLo = std::max (15.0f, lo);
    proc.bandListenHi = std::min (20000.0f, hi);
    proc.bandListen = true;
    if (brain) brain->onAction ("listenBand");
}

void MooniEditor::updateReport()
{
    ReportPtr r = proc.engine.getLatestReport();
    if (! r || ! r->valid)
    {
        const auto entries = proc.history.entries();
        if (! entries.empty()) r = entries.back().report;
    }
    if (r != shownReport)
    {
        shownReport = r;
        ctx.report = r;
        for (auto& rm : rooms) rm->reportChanged();
    }
}

bool MooniEditor::keyPressed (const juce::KeyPress& k)
{
    const auto m = k.getModifiers();
    if (m.isCommandDown() && k.getKeyCode() == 'Z' && ! m.isShiftDown()) { if (proc.undoStep() && brain) brain->onAction ("undo"); return true; }
    if (m.isCommandDown() && (k.getKeyCode() == 'Y' || (k.getKeyCode() == 'Z' && m.isShiftDown()))) { if (proc.redoStep() && brain) brain->onAction ("redo"); return true; }
    if (m.isCommandDown() && k.getKeyCode() >= '1' && k.getKeyCode() <= '6') { setRoom (k.getKeyCode() - '1'); return true; }
    if (k == juce::KeyPress::escapeKey) { showSettings (false); showAbout (false); proc.bandListen = false; return true; }
    return false;
}

void MooniEditor::timerCallback()
{
    const double now = juce::Time::getMillisecondCounterHiRes();
    const float dt = (float) juce::jlimit (0.0, 0.1, (now - lastTick) / 1000.0);
    lastTick = now;
    tickOnce (dt);
}

void MooniEditor::tickOnce (float dt)
{
    uiTime += dt;
    ctx.time = uiTime;
    ctx.reducedMotion = proc.settings.reducedMotion;
    proc.engine.getLive (ctx.live);
    if (proc.history.changeCounter() != seenHistory || proc.engine.reportCounter() >= 0) { seenHistory = proc.history.changeCounter(); updateReport(); }

    const auto& L = ctx.live;
    const float level = juce::jlimit (0.0f, 1.0f, (L.momentary + 40.0f) / 34.0f);
    const float gr = -proc.meterGrDb.load();
    world.setBandWidths ({ proc.value (params::imgWidth (0)), proc.value (params::imgWidth (1)), proc.value (params::imgWidth (2)), proc.value (params::imgWidth (3)) });
    world.tick (dt, L.signalPresent ? level : 0.0f, L.widthPct, gr, L.signalPresent, ctx.reducedMotion);

    rooms[(size_t) room]->refresh (dt);
    if (brain) brain->update (L, dt);

    // Mooni placement & continuous behaviour
    const auto spot = rooms[(size_t) room]->mooniSpot();
    mooni.setAnchor (spot.centre + Point<float> (0, (float) kTop), spot.radius, true);
    mooni.setPeekLine (spot.peekLine > 0 ? spot.peekLine + (float) kTop : 0.0f);
    mooni.setGlasses (spot.glasses || proc.reference.getStatus() == ReferenceTrack::Status::Loading);
    mooni.setReducedMotion (ctx.reducedMotion);
    mooni.setSquish (room == 3 ? juce::jlimit (0.0f, 1.0f, gr / 12.0f) : 0.0f);
    if (room == 2 && proc.value (params::imgOn) > 0.5f)
    {
        float w = 0; for (int i = 1; i < 4; ++i) w += proc.value (params::imgWidth (i));
        mooni.setStretch (0.75f + 0.25f * (w / 300.0f));
    }
    else mooni.setStretch (1.0f);
    const auto mouse = canvas.getMouseXYRelative().toFloat();
    mooni.setLookTarget (mouse, canvas.isMouseOver (true));
    mooni.tick (dt);

    const auto mc = mooni.getCentre();
    const float mr = mooni.getRadius();
    const auto sa = spot.speechArea.translated (0, (float) kTop);
    bubble.setAnchor ({ mc.x, mc.y - mr * 1.45f }, sa.isEmpty() ? juce::Rectangle<float> (0, (float) kTop, (float) kW, (float) (kH - kTop)) : sa.getUnion (juce::Rectangle<float> (mc.x - 10, mc.y - mr * 1.5f, 20, 10)), mc.x > kW * 0.6f);
    bubble.tick (dt);
    toast.tick (dt);
    if (aboutPanel) aboutPanel->tick (dt);

    // world shake from big reactions
    const float shake = ctx.reducedMotion ? 0.0f : mooni.getShakeForWorld();
    if (shake > 0.3f) world.setTopLeftPosition ((int) ((juce::Random::getSystemRandom().nextFloat() - 0.5f) * shake * 2), (int) ((juce::Random::getSystemRandom().nextFloat() - 0.5f) * shake * 2));
    else if (world.getX() != 0 || world.getY() != 0) world.setTopLeftPosition (0, 0);

    // eclipse
    const bool byp = proc.value (params::bypass) > 0.5f;
    const float target = byp ? 1.0f : 0.0f;
    const float before = eclipseAmt;
    eclipseAmt += (target - eclipseAmt) * std::min (1.0f, dt * 3.0f);
    if (std::abs (eclipseAmt - target) < 0.002f) eclipseAmt = target;
    if (std::abs (before - eclipseAmt) > 0.0005f) { eclipse.alpha = eclipseAmt; eclipse.repaint(); }

    // top/bottom bars
    abBtn.setButtonText (proc.abCurrent() == 0 ? "A | b" : "a | B");
    fairBtn.setToggleState (proc.settings.fairCompare, juce::dontSendNotification);
    undoBtn.setEnabled (proc.undo.canUndo());
    redoBtn.setEnabled (proc.undo.canRedo());
    stopListenBtn.setVisible (proc.bandListen.load());
    monitorResetBtn.setVisible (! proc.bandListen.load() && (proc.monitor.load() != 0 || proc.referenceListen.load()));
    bars.repaint (0, kH - kBottom, kW, kBottom);
    if (std::fmod (uiTime, 0.25f) < dt) bars.repaint (0, 0, 232, kTop);
}

// ---------------------------------------------------------------------------
void MooniEditor::paintTopBar (juce::Graphics& g)
{
    auto bar = juce::Rectangle<float> (0, 0, (float) kW, (float) kTop);
    juce::ColourGradient bg (col::night0.withAlpha (0.96f), 0, 0, col::night1.withAlpha (0.9f), 0, (float) kTop, false);
    g.setGradientFill (bg); g.fillRect (bar);
    g.setColour (col::line); g.drawHorizontalLine (kTop - 1, 0, (float) kW);

    // logo: a crescent with a little star on its tip
    const Point<float> c (38, 32);
    drawGlow (g, c, 34, col::lilac, 0.25f);
    g.setColour (col::cream); g.fillEllipse (juce::Rectangle<float> (36, 36).withCentre (c));
    g.setColour (col::night0); g.fillEllipse (juce::Rectangle<float> (32, 32).withCentre (c + Point<float> (9, -5)));
    drawStar (g, c + Point<float> (13, -14), 5.0f, 2.2f, uiTime * 0.8f, col::amber);
    g.setColour (col::text); g.setFont (display (19.0f));
    g.drawText ("Mooni", 66, 10, 140, 26, juce::Justification::centredLeft);
    g.setColour (col::lilac); g.setFont (heavy (10.5f));
    g.drawText (ru ("ANALYZER  ·  by Ernomoon"), 67, 35, 160, 14, juce::Justification::centredLeft);
}

void MooniEditor::paintBottomBar (juce::Graphics& g)
{
    const auto& L = ctx.live;
    auto bar = juce::Rectangle<float> (0, (float) (kH - kBottom), (float) kW, (float) kBottom);
    juce::ColourGradient bg (col::night1.withAlpha (0.9f), 0, bar.getY(), col::night0, 0, bar.getBottom(), false);
    g.setGradientFill (bg); g.fillRect (bar);
    g.setColour (col::line); g.drawHorizontalLine (kH - kBottom, 0, (float) kW);

    auto r = bar.reduced (18, 10);
    auto meter = [&] (const juce::String& name, const juce::String& v, juce::Colour c, float w)
    {
        auto cell = r.removeFromLeft (w);
        g.setColour (col::textFaint); g.setFont (semi (10.5f)); g.drawText (name, cell.removeFromTop (16), juce::Justification::centredLeft);
        g.setColour (c); g.setFont (display (15.0f)); g.drawText (v, cell, juce::Justification::centredLeft);
    };
    meter (ru ("МГНОВ. LUFS"), formatLufs (L.momentary), col::text, 92);
    meter (ru ("КРАТК. LUFS"), formatLufs (L.shortTerm), col::text, 92);
    meter (ru ("ИНТЕГР. LUFS"), formatLufs (L.integrated), col::mint, 96);
    meter (ru ("TRUE PEAK"), formatDb (L.truePeakHold, 1), L.truePeakHold > -0.1f ? col::coral : col::text, 84);
    meter (ru ("СЖАТИЕ"), formatDb (std::abs (proc.meterGrDb.load()) < 0.05f ? 0.0f : proc.meterGrDb.load(), 1), col::amber, 70);
    {
        auto cell = r.removeFromLeft (110);
        g.setColour (col::textFaint); g.setFont (semi (10.5f)); g.drawText (ru ("КОРРЕЛЯЦИЯ"), cell.removeFromTop (16), juce::Justification::centredLeft);
        drawMoonPhase (g, juce::Rectangle<float> (22, 22).withPosition (cell.getX(), cell.getY() + 4), L.correlation, L.correlation < 0 ? col::coral : col::cream, col::night0.brighter (0.2f));
        g.setColour (col::text); g.setFont (display (15.0f));
        g.drawText (juce::String (L.correlation, 2), cell.withTrimmedLeft (28), juce::Justification::centredLeft);
    }
    // status / banners
    auto status = r.removeFromLeft (250);
    juce::String text; juce::Colour c = col::textDim;
    if (proc.bandListen.load()) { text = fmt ("Подслушиваю {}", hzRange (proc.bandListenLo.load(), proc.bandListenHi.load())); c = col::amber; }
    else if (proc.referenceListen.load()) { text = ru ("Звучит эталон (громкость выровнена)"); c = col::pink; }
    else if (proc.monitor.load() == (int) MonitorMode::Mono) { text = ru ("Мониторинг: МОНО"); c = col::amber; }
    else if (proc.monitor.load() == (int) MonitorMode::Side) { text = ru ("Мониторинг: только Side"); c = col::amber; }
    else if (proc.value (params::bypass) > 0.5f) { text = proc.settings.fairCompare ? fmt ("Затмение · оригинал выровнен на {} дБ", signedNum (proc.appliedFairGainDb.load(), 1)) : ru ("Затмение · обработка выключена"); c = col::cream; }
    else if (L.state == AnalysisState::Listening) { text = fmt ("Слушаю · {} с · уверенность {}%", (int) L.activeSeconds, juce::roundToInt (L.confidence * 100)); c = col::earth; }
    else if (L.state == AnalysisState::Ready) { text = ru ("Характер понятен · можно остановить"); c = col::mint; }
    else if (L.fifoDrops > 0 && std::fmod (uiTime, 6.0f) < 3.0f) { text = ru ("Анализ не успевал за звуком — часть данных пропущена"); c = col::amber; }
    else text = L.signalPresent ? ru ("Звук есть · анализ ждёт Play") : ru ("Жду звук");
    g.setColour (c); g.setFont (heavy (13.0f));
    g.drawFittedText (text, status.toNearestInt().withTrimmedRight (8), juce::Justification::centredLeft, 2);

    g.setColour (col::textFaint); g.setFont (semi (10.5f));
    g.drawText (fmt ("латентность {} мс", juce::String (proc.latencySamples.load() * 1000.0 / std::max (1.0, proc.getCurrentSampleRate()), 1)),
                juce::Rectangle<float> ((float) kW - 190, bar.getY() + 12, 170, 16), juce::Justification::centredRight);
    g.setColour (col::lilacDim); g.setFont (heavy (10.5f));
    g.drawText (ru ("создано Ernomoon"), juce::Rectangle<float> ((float) kW - 190, bar.getY() + 32, 170, 16), juce::Justification::centredRight);
}

void MooniEditor::paint (juce::Graphics& g) { g.fillAll (col::night0); }

void MooniEditor::Canvas::paint (juce::Graphics& g) { g.fillAll (col::night0); }

void MooniEditor::Canvas::paintOverChildren (juce::Graphics& g)
{
    // bars are drawn over the world but under the buttons (buttons are children painted earlier,
    // so we only draw backgrounds where no buttons live)
    juce::ignoreUnused (g);
}

void MooniEditor::Dimmer::paint (juce::Graphics& g)
{
    if (modal) { g.fillAll (juce::Colours::black.withAlpha (0.45f)); return; }
    if (alpha <= 0.001f) return;
    g.fillAll (col::night0.withAlpha (0.58f * alpha));
    // the eclipsed sun hangs in the observatory sky, with its caption beside it
    const auto c = Point<float> (96.0f, 56.0f);
    drawGlow (g, c, 110, col::cream, 0.16f * alpha);
    g.setColour (col::night0.withAlpha (alpha)); g.fillEllipse (juce::Rectangle<float> (60, 60).withCentre (c));
    g.setColour (col::cream.withAlpha (0.8f * alpha)); g.drawEllipse (juce::Rectangle<float> (60, 60).withCentre (c), 2.0f);
    g.setColour (col::cream.withAlpha (0.95f * alpha)); g.setFont (display (16.0f));
    g.drawText (ru ("ЗАТМЕНИЕ"), 140, 36, 240, 22, juce::Justification::centredLeft);
    g.setFont (semi (12.0f)); g.setColour (col::textDim.withAlpha (alpha));
    g.drawText (ru ("обработка выключена · анализ слушает"), 140, 58, 260, 18, juce::Justification::centredLeft);
}
} // namespace mooni
