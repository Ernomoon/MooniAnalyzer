#include "PluginEditor.h"

namespace mooni
{
using namespace ui;
using juce::Point;

static const char* kRoomNames[] = { "Обзор", "Эквалайзер", "Стерео", "Громкость", "Референс", "История" };
static const char* kRoomHints[] = {
    "Оценка, проблемы и что делать",
    "Тональный баланс и коррекция",
    "Ширина, фаза и моно",
    "Лимитер и цель в LUFS",
    "Сравнение с другим треком",
    "Прошлые анализы, A/B, отмена" };
static const char* kRoomTitles[] = {
    "Обзор мастера",
    "Эквалайзер",
    "Стерео и фаза",
    "Громкость и максимайзер",
    "Сравнение с референсом",
    "История анализов" };
static const char* kRoomDescriptions[] = {
    "Оценка трека, найденные проблемы и следующий шаг. Начни отсюда.",
    "Спектр твоего микса, ориентир стиля и рекомендованная кривая. Ничего не меняется, пока не нажмёшь «Применить».",
    "Ширина стерео в четырёх полосах, фаза и проверка в моно. Низ лучше держать в моно.",
    "Выбери цель в LUFS — я посчитаю драйв по прослушанному материалу и покажу, сколько это стоит по сжатию.",
    "Необязательно: загрузи готовый трек того же стиля и сравни баланс, громкость и ширину.",
    "Каждое прослушивание с настройками. Можно вернуться к любому шагу, сравнить A/B и отменить изменения." };

juce::String MooniEditor::roomName (int r) { return juce::String::fromUTF8 (kRoomNames[juce::jlimit (0, 5, r)]); }
juce::String MooniEditor::roomHint (int r) { return juce::String::fromUTF8 (kRoomHints[juce::jlimit (0, 5, r)]); }

static void drawRoomIcon (juce::Graphics& g, juce::Rectangle<float> r, int room, juce::Colour c)
{
    const auto cx = r.getCentreX(), cy = r.getCentreY(); const float s = r.getWidth() * 0.5f;
    juce::PathStrokeType st (1.8f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded);
    g.setColour (c);
    juce::Path p;
    switch (room)
    {
        case 0: // gauge
            p.addCentredArc (cx, cy + s * 0.2f, s * 0.85f, s * 0.85f, 0, -2.3f, 2.3f, true);
            g.strokePath (p, st);
            g.drawLine (cx, cy + s * 0.2f, cx + s * 0.45f, cy - s * 0.35f, 2.0f);
            g.fillEllipse (cx - 2.5f, cy + s * 0.2f - 2.5f, 5, 5);
            break;
        case 1: // EQ curve
            p.startNewSubPath (cx - s, cy + s * 0.2f);
            p.cubicTo (cx - s * 0.5f, cy + s * 0.2f, cx - s * 0.45f, cy - s * 0.8f, cx, cy - s * 0.1f);
            p.cubicTo (cx + s * 0.35f, cy + s * 0.5f, cx + s * 0.5f, cy + s * 0.6f, cx + s, cy + s * 0.1f);
            g.strokePath (p, st);
            break;
        case 2: // two overlapping circles (L / R)
            g.drawEllipse (cx - s * 0.95f, cy - s * 0.55f, s * 1.1f, s * 1.1f, 1.8f);
            g.drawEllipse (cx - s * 0.15f, cy - s * 0.55f, s * 1.1f, s * 1.1f, 1.8f);
            break;
        case 3: // meter bars
            for (int i = 0; i < 4; ++i)
            {
                const float h = s * (0.6f + 0.35f * (float) i);
                g.fillRoundedRectangle (cx - s + (float) i * s * 0.55f, cy + s * 0.8f - h, s * 0.32f, h, 1.5f);
            }
            break;
        case 4: // target
            g.drawEllipse (cx - s * 0.9f, cy - s * 0.9f, s * 1.8f, s * 1.8f, 1.8f);
            g.drawEllipse (cx - s * 0.45f, cy - s * 0.45f, s * 0.9f, s * 0.9f, 1.8f);
            g.fillEllipse (cx - 2.5f, cy - 2.5f, 5, 5);
            break;
        default: // clock
            g.drawEllipse (cx - s * 0.9f, cy - s * 0.9f, s * 1.8f, s * 1.8f, 1.8f);
            g.drawLine (cx, cy, cx, cy - s * 0.55f, 1.8f);
            g.drawLine (cx, cy, cx + s * 0.4f, cy + s * 0.2f, 1.8f);
            break;
    }
}

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
    world.setBounds (kSide, 0, kRoomW, kH);
    world.onEarthClicked = [this] { if (brain) brain->onEarthClicked(); };
    world.onBipClicked = [this] { if (brain) brain->onBipClicked(); };

    rooms.push_back (std::make_unique<ObservatoryRoom> (ctx));
    rooms.push_back (std::make_unique<EqRoom> (ctx));
    rooms.push_back (std::make_unique<TidesRoom> (ctx));
    rooms.push_back (std::make_unique<GravityRoom> (ctx));
    rooms.push_back (std::make_unique<ReferenceRoom> (ctx));
    rooms.push_back (std::make_unique<LogbookRoom> (ctx));
    for (auto& r : rooms) { canvas.addChildComponent (*r); r->setBounds (kSide, kTop, kRoomW, kH - kTop - kBottom); }
    canvas.addAndMakeVisible (bars);
    bars.setBounds (0, 0, kW, kH);
    canvas.addAndMakeVisible (sidebar);
    sidebar.setBounds (0, 0, kSide, kH - kBottom);

    // module switches in the sidebar: the whole processing chain at a glance
    modEq.setButtonText (ru ("Эквалайзер"));
    modStereo.setButtonText (ru ("Стерео"));
    modMax.setButtonText (ru ("Максимайзер"));
    modEq.setTooltip (ru ("Включить / выключить обработку эквалайзером"));
    modStereo.setTooltip (ru ("Включить / выключить обработку стерео-ширины"));
    modMax.setTooltip (ru ("Включить / выключить максимайзер (лимитер)"));
    modEqAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (proc.apvts, params::eqOn, modEq);
    modStereoAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (proc.apvts, params::imgOn, modStereo);
    modMaxAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (proc.apvts, params::maxOn, modMax);
    for (auto* b : { &modEq, &modStereo, &modMax }) canvas.addAndMakeVisible (*b);

    // top bar
    undoBtn.setButtonText (juce::String::fromUTF8 ("↶"));  undoBtn.setTooltip (ru ("Отменить (Ctrl+Z)"));
    redoBtn.setButtonText (juce::String::fromUTF8 ("↷"));  redoBtn.setTooltip (ru ("Вернуть (Ctrl+Shift+Z)"));
    undoBtn.onClick = [this] { if (proc.undoStep() && brain) brain->onAction ("undo"); };
    redoBtn.onClick = [this] { if (proc.redoStep() && brain) brain->onAction ("redo"); };
    abBtn.setTooltip (ru ("Два набора настроек: переключай и сравнивай"));
    abBtn.onClick = [this] { proc.abSwitch(); if (brain) brain->onAction ("ab"); };
    fairBtn.setButtonText (ru ("Выравнивать громкость"));
    fairBtn.setTooltip (ru ("При обходе оригинал звучит так же громко, как обработанный трек — чтобы сравнивать звук, а не громкость. Громче ≠ лучше."));
    setButtonStyle (fairBtn, ButtonStyle::Pill);
    fairBtn.onClick = [this] { auto s = proc.settings; s.fairCompare = ! s.fairCompare; proc.setSettings (s); if (s.fairCompare && brain) brain->onAction ("fair"); };
    bypassBtn.setButtonText (ru ("Обход"));
    bypassBtn.setClickingTogglesState (true);
    bypassBtn.setTooltip (ru ("Bypass: выключить всю обработку (эквалайзер, стерео, максимайзер) и слушать оригинал. Анализ продолжает работать, щелчков нет."));
    setButtonStyle (bypassBtn, ButtonStyle::Danger);
    bypassAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (proc.apvts, params::bypass, bypassBtn);
    gearBtn.setButtonText (juce::String::fromUTF8 ("⚙")); gearBtn.setTooltip (ru ("Настройки"));
    gearBtn.onClick = [this] { showSettings (true); };
    infoBtn.setButtonText ("i"); infoBtn.setTooltip (ru ("О программе"));
    infoBtn.onClick = [this] { showAbout (true); };
    helpBtn.setButtonText ("?"); helpBtn.setTooltip (ru ("Как пользоваться"));
    helpBtn.onClick = [this] { showHelp (true); };
    abBtn.setTooltip (ru ("Два набора настроек A и B: переключай и сравнивай на слух"));
    for (auto* b : { &undoBtn, &redoBtn, &abBtn, &fairBtn, &bypassBtn, &helpBtn, &gearBtn, &infoBtn }) canvas.addAndMakeVisible (*b);
    for (auto* b : { &undoBtn, &redoBtn, &helpBtn, &gearBtn, &infoBtn }) setButtonStyle (*b, ButtonStyle::Ghost);

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
    eclipse.setBounds (kSide, kTop, kRoomW, kH - kTop - kBottom);

    canvas.addAndMakeVisible (mooni);
    mooni.setBounds (0, 0, kW, kH);
    mooni.onPoked = [this] { if (brain) brain->onPoked(); };
    canvas.addAndMakeVisible (bubble);
    bubble.setBounds (0, 0, kW, kH);
    canvas.addAndMakeVisible (toast);
    toast.setBounds (kW - 420, kH - kBottom - 96, 404, 84);

    backdrop.modal = true;
    backdrop.onClick = [this] { closeOverlays(); };
    canvas.addChildComponent (backdrop);
    backdrop.setBounds (0, 0, kW, kH);

    brain = std::make_unique<MooniBrain> (proc, mooni, [this] (const juce::String& t, float s) { bubble.say (t, s); });

    setResizable (true, true);
    if (auto* c = getConstrainer()) c->setFixedAspectRatio ((double) kW / kH);
    setResizeLimits (1000, 553, 1974, 1092);
    const float sc = juce::jlimit (0.71f, 1.4f, proc.settings.uiScale);
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

    // top bar (right side)
    int x = kW - 16;
    auto place = [&x] (juce::Component& c, int w, int h = 32) { x -= w; c.setBounds (x, (kTop - h) / 2, w, h); x -= 6; };
    place (infoBtn, 32); place (gearBtn, 32); place (helpBtn, 32);
    x -= 6;
    place (bypassBtn, 96, 36);
    place (fairBtn, 178);
    place (abBtn, 56);
    x -= 6;
    place (redoBtn, 34); place (undoBtn, 34);

    // sidebar module switches
    const int my = Sidebar::itemBounds (5).getBottom() + 46;
    modEq.setBounds (22, my, kSide - 36, 26);
    modStereo.setBounds (22, my + 30, kSide - 36, 26);
    modMax.setBounds (22, my + 60, kSide - 36, 26);
    stopListenBtn.setBounds (kW - 400, kH - 50, 180, 32);
    monitorResetBtn.setBounds (kW - 400, kH - 50, 180, 32);
}

void MooniEditor::setRoom (int r)
{
    r = juce::jlimit (0, (int) rooms.size() - 1, r);
    const bool changed = r != room || ! rooms[(size_t) r]->isVisible();
    room = r;
    for (size_t i = 0; i < rooms.size(); ++i) rooms[i]->setVisible ((int) i == r);
    sidebar.repaint();
    world.setRoom (r);
    proc.lastRoom = r;
    rooms[(size_t) r]->reportChanged();
    const auto spot = rooms[(size_t) r]->mooniSpot();
    mooni.setAnchor (spot.centre + Point<float> ((float) kSide, (float) kTop), spot.radius, changed && lastTick > 0);
    if (changed && brain) brain->onRoomChanged (r);
    canvas.repaint();
}

void MooniEditor::showSettings (bool b)
{
    if (b)
    {
        showAbout (false); showHelp (false);
        settingsPanel = std::make_unique<SettingsPanel> (ctx);
        settingsPanel->onClose = [this] { showSettings (false); };
        canvas.addAndMakeVisible (*settingsPanel);
        settingsPanel->setBounds (kSide + kRoomW / 2 - 320, 130, 640, 500);
        backdrop.setVisible (true); backdrop.toFront (false); settingsPanel->toFront (true);
    }
    else if (settingsPanel) { settingsPanel.reset(); backdrop.setVisible (aboutPanel != nullptr || helpPanel != nullptr); }
}

void MooniEditor::showAbout (bool b)
{
    if (b)
    {
        showSettings (false); showHelp (false);
        aboutPanel = std::make_unique<AboutPanel> (ctx);
        aboutPanel->onClose = [this] { showAbout (false); };
        canvas.addAndMakeVisible (*aboutPanel);
        aboutPanel->setBounds (kSide + kRoomW / 2 - 330, 110, 660, 540);
        backdrop.setVisible (true); backdrop.toFront (false); aboutPanel->toFront (true);
        mooni.play ("bow", true);
    }
    else if (aboutPanel) { aboutPanel.reset(); backdrop.setVisible (settingsPanel != nullptr || helpPanel != nullptr); }
}

void MooniEditor::showHelp (bool b)
{
    if (b)
    {
        showSettings (false); showAbout (false);
        helpPanel = std::make_unique<HelpPanel>();
        helpPanel->onClose = [this] { showHelp (false); };
        canvas.addAndMakeVisible (*helpPanel);
        helpPanel->setBounds (kSide + kRoomW / 2 - 380, 90, 760, 600);
        backdrop.setVisible (true); backdrop.toFront (false); helpPanel->toFront (true);
    }
    else if (helpPanel) { helpPanel.reset(); backdrop.setVisible (settingsPanel != nullptr || aboutPanel != nullptr); }
}

void MooniEditor::closeOverlays() { showSettings (false); showAbout (false); showHelp (false); }

int MooniEditor::problemsForRoom (int r) const
{
    if (! ctx.report || ! ctx.report->valid) return 0;
    if (r == 0) return (int) ctx.report->problems.size();
    int n = 0;
    for (auto& p : ctx.report->problems) if (ui::roomForModule (p.module) == r) ++n;
    return n;
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
    if (k == juce::KeyPress::escapeKey) { closeOverlays(); proc.bandListen = false; return true; }
    if (k.getKeyCode() == juce::KeyPress::F1Key) { showHelp (true); return true; }
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
    mooni.setAnchor (spot.centre + Point<float> ((float) kSide, (float) kTop), spot.radius, true);
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
    const auto sa = spot.speechArea.translated ((float) kSide, (float) kTop);
    bubble.setAnchor ({ mc.x, mc.y - mr * 1.45f }, sa.isEmpty() ? juce::Rectangle<float> ((float) kSide, (float) kTop, (float) kRoomW, (float) (kH - kTop)) : sa.getUnion (juce::Rectangle<float> (mc.x - 10, mc.y - mr * 1.5f, 20, 10)), mc.x > kSide + kRoomW * 0.6f);
    bubble.tick (dt);
    toast.tick (dt);
    if (aboutPanel) aboutPanel->tick (dt);

    // world shake from big reactions
    const float shake = ctx.reducedMotion ? 0.0f : mooni.getShakeForWorld();
    if (shake > 0.3f) world.setTopLeftPosition (kSide + (int) ((juce::Random::getSystemRandom().nextFloat() - 0.5f) * shake * 2), (int) ((juce::Random::getSystemRandom().nextFloat() - 0.5f) * shake * 2));
    else if (world.getX() != kSide || world.getY() != 0) world.setTopLeftPosition (kSide, 0);

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
    if (helpPanel == nullptr && settingsPanel == nullptr && aboutPanel == nullptr && backdrop.isVisible()) backdrop.setVisible (false);
    undoBtn.setEnabled (proc.undo.canUndo());
    redoBtn.setEnabled (proc.undo.canRedo());
    stopListenBtn.setVisible (proc.bandListen.load());
    monitorResetBtn.setVisible (! proc.bandListen.load() && (proc.monitor.load() != 0 || proc.referenceListen.load()));
    bars.repaint (0, kH - kBottom, kW, kBottom);
    if (std::fmod (uiTime, 0.5f) < dt) sidebar.repaint();
}

// ---------------------------------------------------------------------------
void MooniEditor::paintTopBar (juce::Graphics& g)
{
    auto bar = juce::Rectangle<float> ((float) kSide, 0, (float) kRoomW, (float) kTop);
    juce::ColourGradient bg (col::night0.withAlpha (0.96f), 0, 0, col::night1.withAlpha (0.92f), 0, (float) kTop, false);
    g.setGradientFill (bg); g.fillRect (bar);
    g.setColour (col::line); g.drawHorizontalLine (kTop - 1, (float) kSide, (float) kW);

    // current section: title + what it is for
    const float right = (float) undoBtn.getX() - 16.0f;
    g.setColour (col::text); g.setFont (display (17.0f));
    g.drawText (juce::String::fromUTF8 (kRoomTitles[room]), (int) bar.getX() + 22, 9, (int) (right - bar.getX() - 22), 24, juce::Justification::centredLeft);
    g.setColour (col::textDim); g.setFont (body (12.5f));
    g.drawFittedText (juce::String::fromUTF8 (kRoomDescriptions[room]), (int) bar.getX() + 22, 33, (int) (right - bar.getX() - 22), 22, juce::Justification::centredLeft, 1, 0.85f);
}

void MooniEditor::Sidebar::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    juce::ColourGradient bg (col::night1, 0, 0, col::night0, 0, r.getBottom(), false);
    g.setGradientFill (bg); g.fillRect (r);
    g.setColour (col::line); g.drawVerticalLine (kSide - 1, 0, r.getBottom());

    // logo
    const Point<float> c (34, 32);
    drawGlow (g, c, 30, col::lilac, 0.22f);
    g.setColour (col::cream); g.fillEllipse (juce::Rectangle<float> (32, 32).withCentre (c));
    g.setColour (col::night1); g.fillEllipse (juce::Rectangle<float> (28, 28).withCentre (c + Point<float> (8, -5)));
    drawStar (g, c + Point<float> (12, -13), 4.5f, 2.0f, ed.uiTime * 0.8f, col::amber);
    g.setColour (col::text); g.setFont (display (17.0f));
    g.drawText ("Mooni", 58, 12, 140, 22, juce::Justification::centredLeft);
    g.setColour (col::lilac); g.setFont (heavy (10.0f));
    g.drawText (ru ("ANALYZER · by Ernomoon"), 59, 34, 150, 14, juce::Justification::centredLeft);
    g.setColour (col::line); g.drawHorizontalLine (63, 12, (float) kSide - 12);

    for (int i = 0; i < 6; ++i)
    {
        auto item = itemBounds (i).toFloat();
        const bool sel = i == ed.room, hov = i == hover;
        if (sel) { g.setColour (col::lilac.withAlpha (0.18f)); g.fillRoundedRectangle (item, 12); g.setColour (col::lilac); g.fillRoundedRectangle (item.withWidth (4).reduced (0, 12), 2); }
        else if (hov) { g.setColour (col::lilac.withAlpha (0.08f)); g.fillRoundedRectangle (item, 12); }
        const auto ic = sel ? col::lilac : col::textDim;
        auto iconBox = juce::Rectangle<float> (34, 34).withCentre ({ item.getX() + 28, item.getCentreY() });
        g.setColour ((sel ? col::lilac : col::night3).withAlpha (sel ? 0.2f : 0.8f)); g.fillRoundedRectangle (iconBox, 9);
        drawRoomIcon (g, iconBox.reduced (8), i, ic);
        g.setColour (sel ? col::text : col::text.withAlpha (0.88f)); g.setFont (heavy (14.0f));
        g.drawText (roomName (i), juce::Rectangle<float> (item.getX() + 52, item.getY() + 10, item.getWidth() - 60, 18), juce::Justification::centredLeft);
        g.setColour (col::textDim); g.setFont (body (11.0f));
        g.drawText (roomHint (i), juce::Rectangle<float> (item.getX() + 52, item.getY() + 30, item.getWidth() - 56, 16), juce::Justification::centredLeft);

        // badge: number of problems that belong to this section
        const int n = ed.problemsForRoom (i);
        if (n > 0 && i != 0)
        {
            auto badge = juce::Rectangle<float> (20, 18).withPosition (item.getRight() - 26, item.getY() + 9);
            g.setColour (col::orange); g.fillRoundedRectangle (badge, 9);
            g.setColour (col::night0); g.setFont (heavy (11.0f));
            g.drawText (juce::String (n), badge, juce::Justification::centred);
        }
        if (i == 0 && ed.ctx.report && ed.ctx.report->valid)
        {
            auto badge = juce::Rectangle<float> (30, 18).withPosition (item.getRight() - 36, item.getY() + 9);
            g.setColour (col::forScore (ed.ctx.report->score)); g.fillRoundedRectangle (badge, 9);
            g.setColour (col::night0); g.setFont (heavy (11.0f));
            g.drawText (juce::String (ed.ctx.report->score), badge, juce::Justification::centred);
        }
    }

    // processing chain
    const float my = (float) itemBounds (5).getBottom() + 18;
    g.setColour (col::textFaint); g.setFont (heavy (10.5f));
    g.drawText (ru ("ОБРАБОТКА"), 22, (int) my, 150, 16, juce::Justification::centredLeft);
    g.setColour (col::textFaint); g.setFont (body (11.0f));
    g.drawFittedText (ed.proc.value (params::bypass) > 0.5f ? ru ("Сейчас включён обход: слышен оригинал.")
                                                            : ru ("Порядок: эквалайзер → стерео → максимайзер."),
                      22, (int) my + 122, kSide - 36, 30, juce::Justification::topLeft, 2);
}

int MooniEditor::Sidebar::itemAt (juce::Point<int> p) const
{
    for (int i = 0; i < 6; ++i) if (itemBounds (i).contains (p)) return i;
    return -1;
}

void MooniEditor::Sidebar::mouseMove (const juce::MouseEvent& e)
{
    const int h = itemAt (e.getPosition());
    if (h != hover) { hover = h; setMouseCursor (h >= 0 ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor); repaint(); }
}

void MooniEditor::Sidebar::mouseUp (const juce::MouseEvent& e)
{
    const int i = itemAt (e.getPosition());
    if (i >= 0 && e.mouseWasClicked()) ed.setRoom (i);
}

MooniEditor::HelpPanel::HelpPanel()
{
    closeBtn.setButtonText (ru ("Понятно"));
    setButtonStyle (closeBtn, ButtonStyle::Primary);
    closeBtn.onClick = [this] { if (onClose) onClose(); };
    addAndMakeVisible (closeBtn);
}

void MooniEditor::HelpPanel::paint (juce::Graphics& g)
{
    drawGlassCard (g, getLocalBounds().toFloat(), 22, 0.98f);
    g.setColour (col::text); g.setFont (display (19.0f));
    g.drawText (ru ("Как пользоваться"), 34, 24, getWidth() - 68, 28, juce::Justification::centredLeft);

    struct Step { const char* title; const char* text; };
    const Step steps[] = {
        { "Поставь плагин последним на мастер-канал", "В FL Studio: Mixer → Master → последний слот. Mooni анализирует то, что выходит из плагина." },
        { "Включи воспроизведение", "Mooni начнёт слушать сам. «Весь трек» — дослушай или останови в любой момент. «Умный анализ» — сыграй самый характерный кусок (припев, дроп), он сам скажет, когда хватит." },
        { "Посмотри «Обзор»", "Оценка 1–100 и список проблем — самое важное сверху. Нажми на проблему: там объяснение, «Подслушать» (только этот диапазон частот) и «Применить»." },
        { "Примени исправления", "«Применить всё» на Обзоре или кнопки в разделах Эквалайзер и Стерео. До нажатия звук не меняется. Любой шаг отменяется: Ctrl+Z." },
        { "Доведи громкость", "Раздел «Громкость»: выбери цель в LUFS → «1 · Рассчитать» → «2 · Применить». Послушай ещё раз; если не попало в цель — «3 · Подогнать»." },
        { "Проверь результат", "Нажми «Слушать заново» — новая оценка сравнится с прошлой. «Обход» с выравниванием громкости покажет честную разницу с оригиналом." } };
    int y = 70;
    for (int i = 0; i < 6; ++i)
    {
        auto num = juce::Rectangle<float> (30, 30).withPosition (34, (float) y);
        g.setColour (col::lilac); g.fillEllipse (num);
        g.setColour (col::night0); g.setFont (heavy (15.0f)); g.drawText (juce::String (i + 1), num, juce::Justification::centred);
        g.setColour (col::text); g.setFont (heavy (14.5f));
        g.drawText (juce::String::fromUTF8 (steps[i].title), 78, y, getWidth() - 112, 20, juce::Justification::centredLeft);
        g.setColour (col::textDim); g.setFont (body (12.5f));
        g.drawFittedText (juce::String::fromUTF8 (steps[i].text), 78, y + 21, getWidth() - 112, 34, juce::Justification::topLeft, 2, 0.9f);
        y += 74;
    }
    g.setColour (col::textFaint); g.setFont (body (12.0f));
    g.drawFittedText (ru ("Подсказки есть у всех кнопок — наведи курсор. Горячие клавиши: Ctrl+1…6 — разделы, Ctrl+Z / Ctrl+Shift+Z — отмена и возврат, F1 — эта справка."),
                      34, getHeight() - 104, getWidth() - 68, 36, juce::Justification::topLeft, 2);
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
    meter (ru ("СЕЙЧАС, LUFS"), formatLufs (L.momentary), col::text, 104);
    meter (ru ("ЗА 3 С, LUFS"), formatLufs (L.shortTerm), col::text, 104);
    meter (ru ("ВЕСЬ ТРЕК, LUFS"), formatLufs (L.integrated), col::mint, 118);
    meter (ru ("ПИК, dBTP"), formatDb (L.truePeakHold, 1), L.truePeakHold > -0.1f ? col::coral : col::text, 92);
    meter (ru ("СЖАТИЕ, дБ"), formatDb (std::abs (proc.meterGrDb.load()) < 0.05f ? 0.0f : proc.meterGrDb.load(), 1), col::amber, 92);
    {
        auto cell = r.removeFromLeft (110);
        g.setColour (col::textFaint); g.setFont (semi (10.5f)); g.drawText (ru ("КОРРЕЛЯЦИЯ"), cell.removeFromTop (16), juce::Justification::centredLeft);
        drawMoonPhase (g, juce::Rectangle<float> (22, 22).withPosition (cell.getX(), cell.getY() + 4), L.correlation, L.correlation < 0 ? col::coral : col::cream, col::night0.brighter (0.2f));
        g.setColour (col::text); g.setFont (display (15.0f));
        g.drawText (juce::String (L.correlation, 2), cell.withTrimmedLeft (28), juce::Justification::centredLeft);
    }
    // status / banners
    auto status = r.removeFromLeft (360);
    juce::String text; juce::Colour c = col::textDim;
    if (proc.bandListen.load()) { text = fmt ("Подслушиваю {}", hzRange (proc.bandListenLo.load(), proc.bandListenHi.load())); c = col::amber; }
    else if (proc.referenceListen.load()) { text = ru ("Звучит референс (громкость выровнена)"); c = col::pink; }
    else if (proc.monitor.load() == (int) MonitorMode::Mono) { text = ru ("Мониторинг: МОНО"); c = col::amber; }
    else if (proc.monitor.load() == (int) MonitorMode::Side) { text = ru ("Мониторинг: только Side"); c = col::amber; }
    else if (proc.value (params::bypass) > 0.5f) { text = proc.settings.fairCompare ? fmt ("Обход: слышен оригинал, громкость выровнена ({} дБ)", signedNum (proc.appliedFairGainDb.load(), 1)) : ru ("Обход: обработка выключена, слышен оригинал"); c = col::cream; }
    else if (L.state == AnalysisState::Listening) { text = fmt ("Анализ: слушаю {} с, уверенность {}%", (int) L.activeSeconds, juce::roundToInt (L.confidence * 100)); c = col::earth; }
    else if (L.state == AnalysisState::Ready) { text = ru ("Анализ готов — можно остановить воспроизведение"); c = col::mint; }
    else if (L.fifoDrops > 0 && std::fmod (uiTime, 6.0f) < 3.0f) { text = ru ("Анализ не успевал за звуком — часть данных пропущена"); c = col::amber; }
    else text = L.signalPresent ? ru ("Звук есть — нажми Play, чтобы начать анализ") : ru ("Жду звук: включи воспроизведение");
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
    g.drawText (ru ("ОБХОД"), 140, 36, 240, 22, juce::Justification::centredLeft);
    g.setFont (semi (12.0f)); g.setColour (col::textDim.withAlpha (alpha));
    g.drawText (ru ("обработка выключена, слышен оригинал"), 140, 58, 280, 18, juce::Justification::centredLeft);
}
} // namespace mooni
