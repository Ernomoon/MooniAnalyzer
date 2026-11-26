#include "Rooms.h"
#include "MooniBrain.h"
#include "MooniCharacter.h"
#include "../analysis/Diagnostics.h"

namespace mooni::ui
{
using juce::Point;
using namespace params;

static void knob (juce::Slider& s, juce::Colour c)
{
    s.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    s.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 70, 16);
    s.setColour (juce::Slider::rotarySliderFillColourId, c);
}

static void caption (juce::Label& l, const juce::String& t)
{
    l.setText (t, juce::dontSendNotification);
    l.setJustificationType (juce::Justification::centred);
    l.setColour (juce::Label::textColourId, col::textDim);
    l.setFont (semi (11.5f));
}

// ===========================================================================
GravityRoom::GravityRoom (UiContext& c) : Room (c)
{
    for (auto* m : { &inMeter, &outMeter, &tpMeter, &grMeter }) addAndMakeVisible (*m);
    grMeter.setInverted (true);
    tpMeter.setColours (col::mint, col::coral);
    addAndMakeVisible (timeline);

    auto& ap = ctx.proc.apvts;
    target.setSliderStyle (juce::Slider::LinearHorizontal);
    target.setTextBoxStyle (juce::Slider::TextBoxRight, false, 86, 26);
    target.setColour (juce::Slider::trackColourId, col::amber);
    target.setTooltip (ru ("Желаемая интегральная громкость. Можно ввести число вручную (щёлкни по значению)."));
    aTarget = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (ap, maxTarget, target);
    target.textFromValueFunction = [] (double v) { return juce::String (v, 1) + " LUFS"; };
    target.valueFromTextFunction = [] (const juce::String& t) { return t.retainCharacters ("-0123456789.,").replaceCharacter (',', '.').getDoubleValue(); };
    addAndMakeVisible (target);

    knob (drive, col::coral); knob (ceiling, col::mint); knob (release, col::lilac); knob (clip, col::amber);
    drive.setTooltip (ru ("Сколько громкости подаётся в лимитер. Обычно его считает кнопка «Рассчитать»."));
    ceiling.setTooltip (ru ("Потолок True Peak. −1 dBTP — безопасно для стримингов."));
    release.setTooltip (ru ("Скорость восстановления лимитера. Быстрее — громче, но может «качать»."));
    clip.setTooltip (ru ("Доля пиков, которую срезает мягкий клиппер (4x передискретизация) до лимитера. Помогает громким стилям."));
    aDrive = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (ap, maxDrive, drive);
    aCeil = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (ap, maxCeil, ceiling);
    aRel = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (ap, maxRel, release);
    aClip = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (ap, maxClip, clip);
    drive.textFromValueFunction = [] (double v) { return signedNum ((float) v, 1) + ru (" дБ"); };
    ceiling.textFromValueFunction = [] (double v) { return juce::String (v, 2) + ru (" дБTP"); };
    release.textFromValueFunction = [] (double v) { return juce::String (juce::roundToInt (v)) + ru (" мс"); };
    clip.textFromValueFunction = [] (double v) { return juce::String (juce::roundToInt (v)) + "%"; };
    for (auto* s : { &drive, &ceiling, &release, &clip })
    {
        s->valueFromTextFunction = [] (const juce::String& t) { return t.retainCharacters ("-0123456789.,").replaceCharacter (',', '.').getDoubleValue(); };
        addAndMakeVisible (*s); s->updateText();
    }
    caption (driveLabel, ru ("Драйв")); caption (ceilLabel, ru ("Потолок")); caption (relLabel, ru ("Восстановление")); caption (clipLabel, ru ("Клиппер"));
    for (auto* l : { &driveLabel, &ceilLabel, &relLabel, &clipLabel }) addAndMakeVisible (*l);
    target.updateText();

    onBtn.setButtonText (ru ("Пресс включён"));
    aOn = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (ap, maxOn, onBtn);
    addAndMakeVisible (onBtn);

    const std::pair<float, const char*> pr[] = { { -14.0f, "−14 стриминг" }, { -9.0f, "−9 поп/рок" }, { -7.0f, "−7 трэп" }, { -5.0f, "−5 жёстко" } };
    for (auto& [v, label] : pr)
    {
        auto* b = presets.add (new juce::TextButton (juce::String::fromUTF8 (label)));
        setButtonStyle (*b, ButtonStyle::Pill);
        const float val = v;
        b->onClick = [this, val] { ctx.proc.setValue (maxTarget, val); };
        addAndMakeVisible (b);
    }
    auto* recBtn = presets.add (new juce::TextButton (ru ("рекомендация")));
    setButtonStyle (*recBtn, ButtonStyle::Pill);
    recBtn->onClick = [this] { if (ctx.report && ctx.report->valid) ctx.proc.setValue (maxTarget, ctx.report->recTargetLufs); };
    addAndMakeVisible (recBtn);

    struct Ch { const char* name; float rel, clipPct; const char* tip; };
    const Ch chars[] = { { "Прозрачно", 250, 0, "Медленное восстановление, без клиппера: чище всего, громкость скромнее." },
                         { "Плотно", 120, 15, "Баланс плотности и чистоты." },
                         { "Агрессивно", 50, 40, "Быстрое восстановление и клиппер: для трэпа и жёстких стилей." } };
    for (auto& ch : chars)
    {
        auto* b = characters.add (new juce::TextButton (juce::String::fromUTF8 (ch.name)));
        b->setTooltip (juce::String::fromUTF8 (ch.tip));
        const float r = ch.rel, cl = ch.clipPct;
        b->onClick = [this, r, cl] { ctx.proc.suppressSnapshots = true; ctx.proc.setValue (maxRel, r); ctx.proc.setValue (maxClip, cl); ctx.proc.suppressSnapshots = false; ctx.proc.pushUndoSnapshot(); };
        addAndMakeVisible (b);
    }

    planBtn.setButtonText (ru ("Рассчитать"));
    planBtn.setTooltip (ru ("Я прогоню прослушанный материал через пресс офлайн и найду драйв для цели. Ничего не меняется, пока не нажмёшь «Применить»."));
    planBtn.onClick = [this] { runPlan(); };
    setButtonStyle (planBtn, ButtonStyle::Primary);
    applyPlanBtn.setButtonText (ru ("Применить расчёт"));
    applyPlanBtn.onClick = [this]
    {
        if (! plan.valid) return;
        ctx.proc.applyMaximizerDrive (plan.driveDb, plan.targetLufs);
        if (ctx.action) ctx.action ("applyMax", {});
    };
    fitBtn.setButtonText (ru ("Подогнать по факту"));
    fitBtn.setTooltip (ru ("После прослушивания с включённым прессом: скорректировать драйв по реально измеренной громкости на выходе."));
    fitBtn.onClick = [this]
    {
        const float measured = ctx.live.integrated;
        const float tgt = ctx.proc.value (maxTarget);
        if (measured < -60) return;
        const float d = juce::jlimit (-6.0f, 24.0f, ctx.proc.value (maxDrive) + (tgt - measured) * 0.9f);
        ctx.proc.setValue (maxDrive, d);
        ctx.proc.pushUndoSnapshot();
        if (ctx.toast) ctx.toast (fmt ("Драйв скорректирован до {} дБ (измерено {} LUFS при цели {}). Переслушай для проверки.", signedNum (d, 1), num (measured, 1), num (tgt, 1)), col::amber);
        ctx.proc.engine.startSession();
    };
    for (auto* b : { &planBtn, &applyPlanBtn, &fitBtn }) addAndMakeVisible (*b);
    applyPlanBtn.setEnabled (false);
}

void GravityRoom::runPlan()
{
    LoudnessPlanner::Request r;
    r.targetLufs = ctx.proc.value (maxTarget);
    r.ceilingDb = ctx.proc.value (maxCeil);
    r.releaseMs = ctx.proc.value (maxRel);
    r.clipPct = ctx.proc.value (maxClip);
    const int g = ctx.report && ctx.report->valid ? ctx.report->genreProfile : std::max (1, ctx.proc.settings.genreChoice);
    r.maxAvgGrDb = genreProfiles()[(size_t) g].maxAvgGrDb;
    if (! ctx.proc.engine.getPlanner().requestPlan (r))
    {
        if (ctx.toast) ctx.toast (ru ("Сначала дай мне послушать трек хотя бы секунд 10 — считать не по чему."), col::amber);
    }
}

MooniSpot GravityRoom::mooniSpot() const
{
    return { { 1000.0f, 566.0f }, 50.0f, 0.0f, false, { 820.0f, 340.0f, 360.0f, 120.0f } };
}

void GravityRoom::resized()
{
    inMeter.setBounds (28, 220, 64, 360);
    outMeter.setBounds (98, 220, 64, 360);
    tpMeter.setBounds (168, 220, 64, 360);
    grMeter.setBounds (238, 220, 64, 360);
    target.setBounds (346, 92, 440, 34);
    int x = 346;
    for (auto* b : presets) { const int w = b == presets.getLast() ? 104 : 78; b->setBounds (x, 136, w, 26); x += w + 6; }
    onBtn.setBounds (346, 178, 180, 26);
    x = 346;
    for (auto* b : characters) { b->setBounds (x, 216, 140, 30); x += 148; }
    const int ky = 262;
    drive.setBounds (346, ky, 100, 96);   driveLabel.setBounds (346, ky + 96, 100, 16);
    ceiling.setBounds (458, ky, 100, 96); ceilLabel.setBounds (458, ky + 96, 100, 16);
    release.setBounds (570, ky, 100, 96); relLabel.setBounds (570, ky + 96, 100, 16);
    clip.setBounds (682, ky, 100, 96);    clipLabel.setBounds (682, ky + 96, 100, 16);
    planBtn.setBounds (346, 396, 140, 34);
    applyPlanBtn.setBounds (494, 396, 160, 34);
    fitBtn.setBounds (662, 396, 128, 34);
    timeline.setBounds (826, 66, 348, 240);
}

void GravityRoom::reportChanged()
{
    if (ctx.report && ctx.report->valid)
        presets.getLast()->setButtonText (fmt ("совет {}", num (ctx.report->recTargetLufs, 1)));
}

void GravityRoom::refresh (float dt)
{
    const auto& L = ctx.live;
    const float gr = -ctx.proc.meterGrDb.load();
    grSmooth = std::max (gr, grSmooth - dt * 8.0f);
    tpHold = L.truePeakHold;
    inMeter.setValue (ctx.live.histIn.empty() ? -100.0f : ctx.live.histIn.back(), -100, -1000);
    outMeter.setValue (L.shortTerm, -100, ctx.proc.value (maxTarget));
    tpMeter.setValue (L.truePeak, tpHold, ctx.proc.value (maxCeil));
    grMeter.setValue (grSmooth, 0);
    timeline.setData (L.histOut, L.histIn, ctx.proc.value (maxTarget));

    auto& pl = ctx.proc.engine.getPlanner();
    const bool busy = pl.isBusy();
    planBtn.setEnabled (! busy && L.planCaptured >= 6.0);
    planBtn.setButtonText (busy ? ru ("Считаю…") : ru ("Рассчитать"));
    if (pl.resultCounter() != seenPlan)
    {
        seenPlan = pl.resultCounter();
        plan = pl.getResult();
        applyPlanBtn.setEnabled (plan.valid);
        if (ctx.action)
        {
            if (plan.error.isNotEmpty()) ctx.action ("planFail", {});
            else if (plan.risk >= 1) ctx.action ("planHeavy", num (plan.avgGrDb, 1) + "|" + num (plan.safeTargetLufs, 1));
            else ctx.action ("planOk", signedNum (plan.driveDb, 1) + ru (" дБ"));
        }
    }
    fitBtn.setEnabled (ctx.proc.value (maxOn) > 0.5f && L.integrated > -60 && L.activeSeconds > 8);
    spin += dt;
    repaint (16, 16, 300, 200);
    repaint (336, 436, 470, 196);
    repaint (816, 326, 368, 306);
}

void GravityRoom::paint (juce::Graphics& g)
{
    const auto& L = ctx.live;
    card (g, { 16, 16, 300, 616 }, ru ("Громкость"), ru ("выход плагина, EBU R128"));
    auto big = [&] (int y, const juce::String& label, float v, juce::Colour c)
    {
        g.setColour (col::textDim); g.setFont (semi (11.5f)); g.drawText (label, 32, y, 140, 16, juce::Justification::centredLeft);
        g.setColour (c); g.setFont (display (20.0f)); g.drawText (formatLufs (v), 160, y - 4, 100, 24, juce::Justification::centredRight);
        g.setColour (col::textFaint); g.setFont (semi (10.5f)); g.drawText ("LUFS", 262, y, 40, 16, juce::Justification::centredLeft);
    };
    big (64, ru ("Мгновенная (M)"), L.momentary, col::text);
    big (94, ru ("Кратковременная (S)"), L.shortTerm, col::text);
    big (124, ru ("Интегральная (I)"), L.integrated, col::mint);
    big (154, ru ("Вход (I)"), L.integratedIn, col::lilacDim);
    g.setColour (col::textDim); g.setFont (semi (11.5f));
    const bool measuredWithPress = ctx.proc.value (maxOn) > 0.5f && L.integrated > -60 && L.activeSeconds > 3;
    g.drawFittedText (measuredWithPress ? fmt ("Достигнуто: {} LUFS (измерено за {} с), цель {}", num (L.integrated, 1), juce::roundToInt (L.activeSeconds), num (ctx.proc.value (maxTarget), 1))
                                        : ru ("«Достигнуто» появится после прослушивания с прессом."), 30, 180, 276, 32, juce::Justification::centredLeft, 2);

    card (g, { 326, 16, 480, 616 }, ru ("Гравитационный пресс"), ru ("лимитер True Peak + мягкий клиппер, цель — в LUFS"));
    g.setColour (col::textDim); g.setFont (semi (12.0f));
    g.drawText (ru ("Цель громкости"), 346, 70, 200, 18, juce::Justification::centredLeft);

    // plan result
    auto res = juce::Rectangle<int> (340, 440, 452, 184);
    g.setColour (col::night0.withAlpha (0.5f)); g.fillRoundedRectangle (res.toFloat(), 12);
    auto& pl = ctx.proc.engine.getPlanner();
    g.setFont (body (12.5f));
    if (pl.isBusy())
    {
        g.setColour (col::amber);
        for (int i = 0; i < 8; ++i)
        {
            const float a = spin * 4 + (float) i * 0.785f;
            g.setColour (col::amber.withAlpha (0.2f + 0.1f * (float) i));
            g.fillEllipse (juce::Rectangle<float> (6, 6).withCentre ({ 380 + std::cos (a) * 14, 480 + std::sin (a) * 14 }));
        }
        g.setColour (col::text); g.drawText (ru ("Прогоняю прослушанный материал через пресс…"), 410, 470, 380, 20, juce::Justification::centredLeft);
    }
    else if (plan.timeMs > 0)
    {
        auto r = res.reduced (14, 10);
        auto row = [&] (const juce::String& k, const juce::String& v, juce::Colour c)
        {
            auto line = r.removeFromTop (20);
            g.setColour (col::textDim); g.setFont (semi (12.0f)); g.drawText (k, line, juce::Justification::centredLeft);
            g.setColour (c); g.setFont (heavy (13.0f)); g.drawText (v, line, juce::Justification::centredRight);
        };
        const auto riskCol = plan.risk == 0 ? col::mint : plan.risk == 1 ? col::amber : col::coral;
        row (ru ("Нужный драйв"), signedNum (plan.driveDb, 1) + ru (" дБ"), col::text);
        row (ru ("Прогноз громкости"), num (plan.predictedLufs, 1) + " LUFS", std::abs (plan.predictedLufs - plan.targetLufs) < 0.6f ? col::mint : col::amber);
        row (ru ("Сжатие лимитера: среднее / 95% / макс"), num (plan.avgGrDb, 1) + " / " + num (plan.p95GrDb, 1) + " / " + num (plan.maxGrDb, 1) + ru (" дБ"), riskCol);
        if (plan.clipShareDb > 0.05f) row (ru ("Работа клиппера (в среднем)"), num (plan.clipShareDb, 1) + ru (" дБ"), col::amber);
        row (ru ("Материал"), fmt ("{} с самых громких мест из {} с", juce::roundToInt (plan.secondsUsed), juce::roundToInt (plan.secondsSession)), col::textDim);
        r.removeFromTop (6);
        g.setFont (semi (12.5f)); g.setColour (riskCol);
        juce::String verdict = plan.error.isNotEmpty() ? plan.error
                             : plan.risk == 0 ? ru ("Чисто: лимитер почти не напрягается.")
                             : plan.risk == 1 ? fmt ("Заметное сжатие. Безопаснее — около {} LUFS.", num (plan.safeTargetLufs, 1))
                                              : fmt ("Тяжело: атака пострадает. Я бы остановился на {} LUFS или добавил клиппер.", num (plan.safeTargetLufs, 1));
        g.drawFittedText (verdict, r, juce::Justification::topLeft, 3);
    }
    else
    {
        g.setColour (col::textDim);
        g.drawFittedText (ru ("1. Дай мне послушать трек (с выключенным прессом или как есть).\n2. Выбери цель и нажми «Рассчитать» — я посчитаю драйв по реальному материалу и покажу цену в сжатии.\n3. «Применить расчёт», затем послушай снова и, если надо, «Подогнать по факту»."),
                          res.reduced (14, 12), juce::Justification::topLeft, 7);
        g.setColour (col::textFaint);
        g.drawText (fmt ("для расчёта собрано: {} с", juce::roundToInt (L.planCaptured)), res.reduced (14, 10), juce::Justification::bottomLeft);
    }

    card (g, { 816, 16, 368, 300 }, ru ("Путь громкости"), ru ("кратковременная: выход и вход, 60 с"));
    // the press scene
    auto scene = juce::Rectangle<int> (816, 326, 368, 306);
    drawGlassCard (g, scene.toFloat());
    drawCardTitle (g, scene.toFloat(), ru ("Пресс"), fmt ("сжатие сейчас: {} дБ", num (grSmooth < 0.05f ? 0.0f : grSmooth, 1)));
    const float press = juce::jlimit (0.0f, 1.0f, grSmooth / 10.0f);
    const float plateY = 380.0f + press * 90.0f;
    g.setColour (juce::Colour (0xff4a3d8f)); g.fillRect (990.0f, 360.0f, 20.0f, plateY - 360.0f);
    g.setColour (col::lilacDim); g.fillRoundedRectangle (920, plateY, 160, 18, 5);
    g.setColour (col::lilac); g.drawRoundedRectangle (920, plateY, 160, 18, 5, 1.2f);
    g.setColour (juce::Colour (0xff2e2666)); g.fillRoundedRectangle (900, 614, 200, 10, 4);
}

// ===========================================================================
ReferenceRoom::ReferenceRoom (UiContext& c) : Room (c)
{
    loadBtn.setButtonText (ru ("Загрузить эталон…"));
    setButtonStyle (loadBtn, ButtonStyle::Primary);
    loadBtn.onClick = [this]
    {
        chooser = std::make_unique<juce::FileChooser> (ru ("Выбери эталонный трек"), juce::File(), "*.wav;*.aif;*.aiff;*.flac;*.ogg;*.mp3");
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [this] (const juce::FileChooser& fc)
        {
            const auto f = fc.getResult();
            if (f.existsAsFile()) ctx.proc.reference.load (f, ctx.proc.getCurrentSampleRate());
        });
    };
    clearBtn.setButtonText (ru ("Убрать"));
    clearBtn.onClick = [this] { ctx.proc.referenceListen = false; ctx.proc.reference.clear(); ctx.proc.engine.rediagnose(); };
    restartBtn.setButtonText (ru ("С начала"));
    restartBtn.onClick = [this] { ctx.proc.reference.restartPlayback(); };
    listenBtn.setButtonText (ru ("Слушать эталон"));
    listenBtn.setTooltip (ru ("Подменить выход плагина эталоном (во время воспроизведения). Анализ твоего микса при этом не сбивается."));
    listenBtn.onClick = [this] { ctx.proc.referenceListen = listenBtn.getToggleState(); };
    matchBtn.setButtonText (ru ("Выровнять громкость"));
    matchBtn.onClick = [this] { auto s = ctx.proc.settings; s.matchReferenceLoudness = matchBtn.getToggleState(); ctx.proc.setSettings (s); };
    useBtn.setButtonText (ru ("Учитывать в анализе"));
    useBtn.setTooltip (ru ("Использовать баланс эталона как ориентир для рекомендаций (вместо типичного баланса стиля)."));
    useBtn.onClick = [this] { auto s = ctx.proc.settings; s.useReferenceAsTarget = useBtn.getToggleState(); ctx.proc.setSettings (s); ctx.proc.engine.rediagnose(); };
    for (juce::Component* b : { (juce::Component*) &loadBtn, (juce::Component*) &clearBtn, (juce::Component*) &restartBtn, (juce::Component*) &listenBtn, (juce::Component*) &matchBtn, (juce::Component*) &useBtn })
        addAndMakeVisible (b);
}

MooniSpot ReferenceRoom::mooniSpot() const
{
    return { { 1100.0f, 136.0f }, 40.0f, 132.0f, true, { 760.0f, 10.0f, 330.0f, 110.0f } };
}

void ReferenceRoom::resized()
{
    loadBtn.setBounds (34, 56, 180, 34);
    clearBtn.setBounds (222, 56, 90, 34);
    restartBtn.setBounds (320, 56, 90, 34);
    listenBtn.setBounds (560, 34, 190, 24);
    matchBtn.setBounds (560, 62, 190, 24);
    useBtn.setBounds (760, 34, 200, 24);
}

void ReferenceRoom::refresh (float)
{
    const auto st = ctx.proc.reference.getStatus();
    const bool ready = st == ReferenceTrack::Status::Ready;
    for (auto* b : { (juce::Component*) &clearBtn, (juce::Component*) &restartBtn, (juce::Component*) &listenBtn, (juce::Component*) &matchBtn, (juce::Component*) &useBtn }) b->setEnabled (ready);
    listenBtn.setToggleState (ctx.proc.referenceListen.load(), juce::dontSendNotification);
    matchBtn.setToggleState (ctx.proc.settings.matchReferenceLoudness, juce::dontSendNotification);
    useBtn.setToggleState (ctx.proc.settings.useReferenceAsTarget, juce::dontSendNotification);
    // loudness-matched reference monitoring
    if (ready)
        if (auto rs = ctx.proc.reference.getStats())
        {
            const float mix = ctx.live.integrated > -60 ? ctx.live.integrated : (ctx.report ? ctx.report->stats.integrated : rs->integrated);
            ctx.proc.referenceGainDb = ctx.proc.settings.matchReferenceLoudness ? juce::jlimit (-24.0f, 12.0f, mix - rs->integrated) : 0.0f;
        }
    if (ctx.proc.reference.changeCounter() != seenChanges || st == ReferenceTrack::Status::Loading) { seenChanges = ctx.proc.reference.changeCounter(); repaint(); }
}

void ReferenceRoom::paint (juce::Graphics& g)
{
    card (g, { 16, 16, 1168, 104 }, ru ("Эталонный телескоп"));
    const auto st = ctx.proc.reference.getStatus();
    auto info = juce::Rectangle<int> (970, 30, 200, 70);
    g.setFont (semi (12.5f));
    if (st == ReferenceTrack::Status::Loading)
    {
        g.setColour (col::amber); g.drawText (ru ("Навожу телескоп…"), 420, 60, 130, 20, juce::Justification::centredLeft);
        auto bar = juce::Rectangle<float> (420, 84, 120, 5);
        g.setColour (col::night0); g.fillRoundedRectangle (bar, 2.5f);
        g.setColour (col::amber); g.fillRoundedRectangle (bar.withWidth (bar.getWidth() * ctx.proc.reference.getProgress()), 2.5f);
    }
    else if (st == ReferenceTrack::Status::Error) { g.setColour (col::coral); g.drawFittedText (ctx.proc.reference.getError(), info, juce::Justification::centredLeft, 3); }
    else if (st == ReferenceTrack::Status::Ready)
    {
        g.setColour (col::text); g.setFont (heavy (13.0f));
        g.drawFittedText (ctx.proc.reference.getFile().getFileName(), 420, 52, 130, 40, juce::Justification::centredLeft, 2);
    }

    auto spec = juce::Rectangle<int> (16, 130, 740, 502);
    card (g, spec, ru ("Баланс: твой микс и эталон"), ru ("оба выровнены по громкости (среднее 500 Гц – 2 кГц = 0 дБ)"));
    auto table = juce::Rectangle<int> (766, 130, 418, 502);
    card (g, table, ru ("Сравнение"));

    auto rs = ctx.proc.reference.getStats();
    const bool hasMix = ctx.report && ctx.report->valid;
    if (st != ReferenceTrack::Status::Ready || rs == nullptr)
    {
        g.setColour (col::textDim); g.setFont (body (14.0f));
        g.drawFittedText (ru ("Эталон — необязательный ориентир: готовый трек того же стиля, который тебе нравится по звуку.\n\n"
                              "Я проанализирую его так же, как твой микс, выровняю громкость и покажу разницу в балансе, ширине и динамике. "
                              "Это контекст для решений, а не цель, которую нужно скопировать."),
                          spec.reduced (40, 80), juce::Justification::centredTop, 8);
        return;
    }

    // spectrum comparison
    auto plot = spec.toFloat().reduced (50, 64).withTrimmedBottom (-20);
    auto xFor = [&] (float f) { return plot.getX() + plot.getWidth() * std::log (f / 20.0f) / std::log (1000.0f); };
    auto yFor = [&] (float db) { return plot.getCentreY() - juce::jlimit (-24.0f, 24.0f, db) / 24.0f * plot.getHeight() * 0.5f; };
    g.setFont (semi (10.0f));
    for (float f : { 50.f, 100.f, 200.f, 500.f, 1000.f, 2000.f, 5000.f, 10000.f })
    {
        g.setColour (col::line.withAlpha (0.5f)); g.drawVerticalLine ((int) xFor (f), plot.getY(), plot.getBottom());
        g.setColour (col::textFaint); g.drawText (f >= 1000 ? juce::String (juce::roundToInt (f / 1000)) + ru ("к") : juce::String ((int) f), juce::Rectangle<float> (30, 12).withCentre ({ xFor (f), plot.getBottom() + 10 }), juce::Justification::centred);
    }
    for (float db = -24; db <= 24; db += 6)
    {
        g.setColour (col::line.withAlpha (db == 0 ? 0.9f : 0.4f)); g.drawHorizontalLine ((int) yFor (db), plot.getX(), plot.getRight());
        g.setColour (col::textFaint); g.drawText (juce::String ((int) db), juce::Rectangle<float> (plot.getX() - 36, yFor (db) - 6, 30, 12), juce::Justification::centredRight);
    }
    const auto ref = diagnostics::relativeBands (rs->sixth);
    juce::Path pr, pm, diffFill;
    std::array<float, BandGrid::kSixthBands> mix {};
    if (hasMix) mix = diagnostics::relativeBands (ctx.report->stats.sixth);
    bool first = true;
    for (int i = 0; i < BandGrid::kSixthBands; ++i)
    {
        const float f = BandGrid::sixthCentre (i);
        if (f > 20000) break;
        const float x = xFor (f);
        if (first) { pr.startNewSubPath (x, yFor (ref[(size_t) i])); if (hasMix) pm.startNewSubPath (x, yFor (mix[(size_t) i])); first = false; }
        else { pr.lineTo (x, yFor (ref[(size_t) i])); if (hasMix) pm.lineTo (x, yFor (mix[(size_t) i])); }
        if (hasMix)
        {
            const float d = mix[(size_t) i] - ref[(size_t) i];
            if (std::abs (d) > 1.5f)
            {
                g.setColour ((d > 0 ? col::coral : col::earth).withAlpha (juce::jlimit (0.05f, 0.35f, std::abs (d) / 12.0f)));
                g.fillRect (juce::Rectangle<float> (x - 3.5f, std::min (yFor (mix[(size_t) i]), yFor (ref[(size_t) i])), 7.0f, std::abs (yFor (mix[(size_t) i]) - yFor (ref[(size_t) i]))));
            }
        }
    }
    g.setColour (col::pink); g.strokePath (pr, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved));
    if (hasMix) { g.setColour (col::cream); g.strokePath (pm, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved)); }
    g.setFont (semi (11.5f));
    g.setColour (col::cream); g.drawText (ru ("— твой микс"), (int) plot.getX(), (int) plot.getY() - 20, 120, 16, juce::Justification::centredLeft);
    g.setColour (col::pink); g.drawText (ru ("— эталон"), (int) plot.getX() + 120, (int) plot.getY() - 20, 100, 16, juce::Justification::centredLeft);
    g.setColour (col::textFaint); g.drawText (ru ("красное — у тебя больше, синее — меньше"), (int) plot.getRight() - 260, (int) plot.getY() - 20, 260, 16, juce::Justification::centredRight);

    // comparison table + interpretation
    auto r = table.reduced (18, 0).withTrimmedTop (44);
    auto header = r.removeFromTop (20);
    g.setColour (col::textFaint); g.setFont (semi (11.0f));
    g.drawText (ru ("микс"), header.withTrimmedLeft (170).withWidth (70), juce::Justification::centredRight);
    g.drawText (ru ("эталон"), header.withTrimmedLeft (240).withWidth (70), juce::Justification::centredRight);
    g.drawText (ru ("разница"), header.withTrimmedLeft (310).withWidth (72), juce::Justification::centredRight);
    auto lowShare = [] (const MixStats& s) { return s.lowEndShare * 100.0f; };
    struct Row { juce::String name; float a, b; int dec; juce::String unit; };
    std::vector<Row> rows;
    const MixStats& m = hasMix ? ctx.report->stats : *rs;
    rows.push_back ({ ru ("Громкость, LUFS"), m.integrated, rs->integrated, 1, "" });
    rows.push_back ({ ru ("True Peak, дБTP"), m.truePeakDb, rs->truePeakDb, 1, "" });
    rows.push_back ({ ru ("PLR, дБ"), m.plr, rs->plr, 1, "" });
    rows.push_back ({ ru ("Микродинамика, дБ"), m.microDynamicsDb, rs->microDynamicsDb, 1, "" });
    rows.push_back ({ ru ("LRA, LU"), m.lra, rs->lra, 1, "" });
    rows.push_back ({ ru ("Ширина, %"), m.widthPct, rs->widthPct, 0, "" });
    rows.push_back ({ ru ("Корреляция"), m.correlation, rs->correlation, 2, "" });
    rows.push_back ({ ru ("Ширина низа, %"), m.bandWidthPct[0], rs->bandWidthPct[0], 0, "" });
    rows.push_back ({ ru ("Доля низа < 150 Гц, %"), lowShare (m), lowShare (*rs), 0, "" });
    for (auto& row : rows)
    {
        auto line = r.removeFromTop (24);
        g.setColour (col::textDim); g.setFont (semi (12.0f)); g.drawText (row.name, line.withWidth (170), juce::Justification::centredLeft);
        g.setColour (col::text); g.setFont (heavy (12.5f));
        g.drawText (hasMix ? num (row.a, row.dec) : ru ("—"), line.withTrimmedLeft (170).withWidth (70), juce::Justification::centredRight);
        g.setColour (col::pink); g.drawText (num (row.b, row.dec), line.withTrimmedLeft (240).withWidth (70), juce::Justification::centredRight);
        if (hasMix) { g.setColour (col::textDim); g.drawText (signedNum (row.a - row.b, row.dec), line.withTrimmedLeft (310).withWidth (72), juce::Justification::centredRight); }
    }
    r.removeFromTop (10);
    if (hasMix)
    {
        juce::StringArray notes;
        const float dI = m.integrated - rs->integrated;
        if (std::abs (dI) > 1.0f) notes.add (fmt ("Эталон {} на {} LU — графики уже выровнены, так что сравнивай баланс, а не громкость.", dI < 0 ? ru ("громче") : ru ("тише"), num (std::abs (dI), 1)));
        auto bandDiff = [&] (float lo, float hi)
        {
            float s = 0; int n = 0;
            for (int i = 0; i < BandGrid::kSixthBands; ++i) { const float f = BandGrid::sixthCentre (i); if (f >= lo && f <= hi) { s += mix[(size_t) i] - ref[(size_t) i]; ++n; } }
            return n > 0 ? s / (float) n : 0.0f;
        };
        const float low = bandDiff (40, 120), mid = bandDiff (200, 500), hi = bandDiff (3000, 10000);
        if (std::abs (low) > 2) notes.add (fmt ("Низа у тебя {} на {} дБ (40–120 Гц).", low > 0 ? ru ("больше") : ru ("меньше"), num (std::abs (low), 1)));
        if (std::abs (mid) > 2) notes.add (fmt ("Низкой середины {} на {} дБ.", mid > 0 ? ru ("больше") : ru ("меньше"), num (std::abs (mid), 1)));
        if (std::abs (hi) > 2) notes.add (fmt ("Верха (3–10 кГц) {} на {} дБ.", hi > 0 ? ru ("больше") : ru ("меньше"), num (std::abs (hi), 1)));
        if (m.microDynamicsDb < rs->microDynamicsDb - 2) notes.add (ru ("Твой мастер плотнее эталона — проверь, не потерялась ли атака."));
        if (notes.isEmpty()) notes.add (ru ("По основным параметрам ты близок к эталону. Красота."));
        g.setColour (col::text); g.setFont (body (12.5f));
        g.drawFittedText (notes.joinIntoString ("\n"), r, juce::Justification::topLeft, 8);
    }
    else
    {
        g.setColour (col::textDim); g.setFont (body (12.5f));
        g.drawFittedText (ru ("Послушай свой трек — и я покажу разницу."), r, juce::Justification::topLeft, 3);
    }
}

// ===========================================================================
LogbookRoom::LogbookRoom (UiContext& c) : Room (c)
{
    list.setRowHeight (54);
    list.setColour (juce::ListBox::backgroundColourId, juce::Colours::transparentBlack);
    list.setOutlineThickness (0);
    addAndMakeVisible (list);

    restoreBtn.setButtonText (ru ("Вернуть настройки этого шага"));
    restoreBtn.setTooltip (ru ("Восстановить параметры обработки, с которыми был сделан выбранный отчёт. Можно отменить (Ctrl+Z)."));
    restoreBtn.onClick = [this]
    {
        const int row = list.getSelectedRow();
        if (row < 0 || row >= (int) entries.size()) return;
        ctx.proc.restoreParams (entries[(size_t) entries.size() - 1 - (size_t) row].params);
        if (ctx.toast) ctx.toast (ru ("Настройки этого шага восстановлены."), col::mint);
    };
    setButtonStyle (restoreBtn, ButtonStyle::Primary);
    copyBtn.setButtonText (ru ("Отчёт в буфер"));
    copyBtn.onClick = [this]
    {
        const int row = list.getSelectedRow();
        if (row < 0 || row >= (int) entries.size()) return;
        juce::SystemClipboard::copyTextToClipboard (makeTextReport (*entries[(size_t) entries.size() - 1 - (size_t) row].report, ctx.proc));
        if (ctx.action) ctx.action ("copied", {});
    };
    abBtn.setButtonText (ru ("Переключить A/B"));
    abBtn.onClick = [this] { ctx.proc.abSwitch(); if (ctx.action) ctx.action ("ab", {}); };
    abCopyBtn.setButtonText (ru ("Копировать в другой слот"));
    abCopyBtn.onClick = [this] { ctx.proc.abCopyToOther(); if (ctx.toast) ctx.toast (ru ("Текущие настройки скопированы в другой слот A/B."), col::lilac); };
    undoBtn.setButtonText (ru ("Отменить"));
    undoBtn.onClick = [this] { if (ctx.proc.undoStep() && ctx.action) ctx.action ("undo", {}); };
    redoBtn.setButtonText (ru ("Вернуть"));
    redoBtn.onClick = [this] { if (ctx.proc.redoStep() && ctx.action) ctx.action ("redo", {}); };
    for (auto* b : { &restoreBtn, &copyBtn, &abBtn, &abCopyBtn, &undoBtn, &redoBtn }) addAndMakeVisible (*b);
}

MooniSpot LogbookRoom::mooniSpot() const
{
    return { { 1124.0f, 586.0f }, 40.0f, 0.0f, true, { 760.0f, 430.0f, 330.0f, 100.0f } };
}

void LogbookRoom::resized()
{
    list.setBounds (24, 66, 424, 558);
    restoreBtn.setBounds (486, 520, 240, 34);
    copyBtn.setBounds (734, 520, 130, 34);
    undoBtn.setBounds (872, 520, 90, 34);
    redoBtn.setBounds (970, 520, 90, 34);
    abBtn.setBounds (486, 584, 150, 30);
    abCopyBtn.setBounds (644, 584, 190, 30);
}

int LogbookRoom::getNumRows() { return (int) entries.size(); }

void LogbookRoom::paintListBoxItem (int row, juce::Graphics& g, int w, int h, bool selected)
{
    if (row < 0 || row >= (int) entries.size()) return;
    const auto& e = entries[(size_t) entries.size() - 1 - (size_t) row];   // newest first
    auto r = juce::Rectangle<float> (0, 0, (float) w, (float) h).reduced (4, 3);
    g.setColour (selected ? col::lilac.withAlpha (0.22f) : col::night0.withAlpha (0.4f)); g.fillRoundedRectangle (r, 10);
    if (selected) { g.setColour (col::lilac); g.drawRoundedRectangle (r, 10, 1); }
    const auto sc = col::forScore (e.report->score);
    g.setColour (sc); g.setFont (display (20.0f));
    g.drawText (juce::String (e.report->score), r.removeFromLeft (56), juce::Justification::centred);
    g.setColour (col::text); g.setFont (heavy (13.5f));
    g.drawText (e.report->label + (e.isOriginal ? ru ("  ★") : ""), r.removeFromTop (r.getHeight() * 0.55f), juce::Justification::bottomLeft);
    g.setColour (col::textDim); g.setFont (semi (11.5f));
    g.drawText (juce::Time (e.report->timeMs).formatted ("%d.%m %H:%M") + "  ·  " + juce::String (e.report->stats.integrated, 1) + " LUFS  ·  "
                + juce::String (juce::roundToInt (e.report->stats.secondsAnalysed)) + ru (" с"), r, juce::Justification::topLeft);
}

void LogbookRoom::refresh (float)
{
    if (ctx.proc.history.changeCounter() != seenChanges)
    {
        seenChanges = ctx.proc.history.changeCounter();
        entries = ctx.proc.history.entries();
        list.updateContent();
        if (list.getSelectedRow() < 0 && ! entries.empty()) list.selectRow (0);
        repaint();
    }
    undoBtn.setEnabled (ctx.proc.undo.canUndo());
    redoBtn.setEnabled (ctx.proc.undo.canRedo());
    const bool sel = list.getSelectedRow() >= 0 && list.getSelectedRow() < (int) entries.size();
    restoreBtn.setEnabled (sel && entries[(size_t) entries.size() - 1 - (size_t) list.getSelectedRow()].params.isValid());
    copyBtn.setEnabled (sel);
}

void LogbookRoom::paint (juce::Graphics& g)
{
    card (g, { 16, 16, 440, 616 }, ru ("Бортовой журнал"), plural ((int) entries.size(), "запись", "записи", "записей") + ru (", новые сверху · ★ — оригинал"));
    auto d = juce::Rectangle<int> (466, 16, 718, 616);
    card (g, d, ru ("Сравнение"), ru ("оригинал · выбранный шаг · последний анализ"));
    const int row = list.getSelectedRow();
    if (row < 0 || row >= (int) entries.size())
    {
        g.setColour (col::textDim); g.setFont (body (14.0f));
        g.drawFittedText (ru ("Журнал пока пуст. Каждое прослушивание попадает сюда вместе с настройками — к любому шагу можно вернуться."), d.reduced (40, 120), juce::Justification::centredTop, 4);
        return;
    }
    const auto& sel = entries[(size_t) entries.size() - 1 - (size_t) row];
    const History::Entry* orig = nullptr;
    for (auto& e : entries) if (e.isOriginal) { orig = &e; break; }
    const auto& last = entries.back();

    const ReportPtr cols3[3] = { orig ? orig->report : nullptr, sel.report, last.report };
    const juce::String heads[3] = { ru ("Оригинал"), sel.report->label, ru ("Последний") };
    auto table = d.reduced (20, 0).withTrimmedTop (60).withHeight (440);
    const int c0 = 200, cw = 150;
    g.setFont (heavy (12.5f));
    for (int k = 0; k < 3; ++k) { g.setColour (k == 1 ? col::lilac : col::textDim); g.drawText (heads[k], table.getX() + c0 + k * cw, table.getY(), cw, 18, juce::Justification::centred); }
    struct Row { juce::String name; std::function<float (const Report&)> get; int dec; bool higherBetter; };
    std::vector<Row> rows {
        { ru ("Оценка"), [] (const Report& r) { return (float) r.score; }, 0, true },
        { ru ("Громкость, LUFS"), [] (const Report& r) { return r.stats.integrated; }, 1, true },
        { ru ("True Peak, дБTP"), [] (const Report& r) { return r.stats.truePeakDb; }, 1, false },
        { ru ("PLR, дБ"), [] (const Report& r) { return r.stats.plr; }, 1, true },
        { ru ("Ширина, %"), [] (const Report& r) { return r.stats.widthPct; }, 0, true },
        { ru ("Корреляция"), [] (const Report& r) { return r.stats.correlation; }, 2, true },
        { ru ("Ширина низа, %"), [] (const Report& r) { return r.stats.bandWidthPct[0]; }, 0, false },
        { ru ("Клип-события"), [] (const Report& r) { return (float) r.stats.clipEvents; }, 0, false } };
    for (size_t i = 0; i < sel.report->subscores.size(); ++i)
    {
        const auto name = sel.report->subscores[i].name;
        rows.push_back ({ name, [i] (const Report& r) { return i < r.subscores.size() ? (float) r.subscores[i].value : 0.0f; }, 0, true });
    }
    int y = table.getY() + 26;
    for (auto& rw : rows)
    {
        g.setColour (col::textDim); g.setFont (semi (12.0f));
        g.drawText (rw.name, table.getX(), y, c0, 22, juce::Justification::centredLeft);
        for (int k = 0; k < 3; ++k)
        {
            if (! cols3[k]) continue;
            const float v = rw.get (*cols3[k]);
            juce::Colour c = col::text;
            if (k > 0 && cols3[0])
            {
                const float base = rw.get (*cols3[0]);
                if (std::abs (v - base) > std::pow (10.0f, (float) -rw.dec)) c = ((v > base) == rw.higherBetter) ? col::mint : col::coral;
            }
            g.setColour (c); g.setFont (heavy (12.5f));
            g.drawText (num (v, rw.dec), table.getX() + c0 + k * cw, y, cw, 22, juce::Justification::centred);
        }
        y += 23;
        if (y > table.getBottom()) break;
    }
    g.setColour (col::textFaint); g.setFont (semi (11.5f));
    g.drawText (fmt ("A/B: сейчас слот {} · шагов отмены: {}", ctx.proc.abCurrent() == 0 ? "A" : "B", ctx.proc.undo.position()), 486, 562, 600, 18, juce::Justification::centredLeft);
}

// ===========================================================================
SettingsPanel::SettingsPanel (UiContext& c) : ctx (c)
{
    for (auto& g : genreProfiles()) genre.addItem (g.name, genre.getNumItems() + 1);
    genre.onChange = [this] { push(); ctx.proc.engine.rediagnose(); };
    chatter.addItem (ru ("Молчун — только важное"), 1);
    chatter.addItem (ru ("Обычный"), 2);
    chatter.addItem (ru ("Болтун — шутки и советы"), 3);
    chatter.onChange = [this] { push(); };
    autoListen.setButtonText (ru ("Начинать слушать автоматически при Play"));
    reduced.setButtonText (ru ("Меньше анимации (экономия CPU, спокойнее)"));
    fair.setButtonText (ru ("Честное сравнение: выравнивать громкость при Затмении"));
    useRef.setButtonText (ru ("Учитывать эталон в рекомендациях"));
    for (auto* b : { &autoListen, &reduced, &fair }) { b->onClick = [this] { push(); }; addAndMakeVisible (*b); }
    useRef.onClick = [this] { push(); ctx.proc.engine.rediagnose(); };
    addAndMakeVisible (useRef);
    modeFull.setButtonText (ru ("Весь трек")); modeSmart.setButtonText (ru ("Умный анализ"));
    for (auto* b : { &modeFull, &modeSmart }) { setButtonStyle (*b, ButtonStyle::Pill); addAndMakeVisible (*b); }
    modeFull.onClick = [this] { auto s = ctx.proc.settings; s.mode = AnalysisMode::Full; ctx.proc.setSettings (s); sync(); };
    modeSmart.onClick = [this] { auto s = ctx.proc.settings; s.mode = AnalysisMode::Smart; ctx.proc.setSettings (s); sync(); };
    closeBtn.setButtonText (ru ("Готово"));
    setButtonStyle (closeBtn, ButtonStyle::Primary);
    closeBtn.onClick = [this] { if (onClose) onClose(); };
    addAndMakeVisible (genre); addAndMakeVisible (chatter); addAndMakeVisible (closeBtn);
    sync();
}

void SettingsPanel::sync()
{
    const auto& s = ctx.proc.settings;
    genre.setSelectedItemIndex (s.genreChoice, juce::dontSendNotification);
    chatter.setSelectedItemIndex (s.chatter, juce::dontSendNotification);
    autoListen.setToggleState (s.autoListen, juce::dontSendNotification);
    reduced.setToggleState (s.reducedMotion, juce::dontSendNotification);
    fair.setToggleState (s.fairCompare, juce::dontSendNotification);
    useRef.setToggleState (s.useReferenceAsTarget, juce::dontSendNotification);
    modeFull.setToggleState (s.mode == AnalysisMode::Full, juce::dontSendNotification);
    modeSmart.setToggleState (s.mode == AnalysisMode::Smart, juce::dontSendNotification);
}

void SettingsPanel::push()
{
    auto s = ctx.proc.settings;
    s.genreChoice = std::max (0, genre.getSelectedItemIndex());
    s.chatter = std::max (0, chatter.getSelectedItemIndex());
    s.autoListen = autoListen.getToggleState();
    s.reducedMotion = reduced.getToggleState();
    s.fairCompare = fair.getToggleState();
    s.useReferenceAsTarget = useRef.getToggleState();
    ctx.proc.setSettings (s);
}

void SettingsPanel::resized()
{
    auto r = getLocalBounds().reduced (30).withTrimmedTop (50);
    auto row = [&] (int h) { auto x = r.removeFromTop (h); r.removeFromTop (8); return x; };
    auto m = row (30); modeFull.setBounds (m.getX() + 190, m.getY(), 140, 30); modeSmart.setBounds (m.getX() + 338, m.getY(), 160, 30);
    auto gr = row (30); genre.setBounds (gr.withTrimmedLeft (190).withWidth (300));
    auto ch = row (30); chatter.setBounds (ch.withTrimmedLeft (190).withWidth (300));
    for (auto* b : { &autoListen, &fair, &useRef, &reduced }) b->setBounds (row (26));
    closeBtn.setBounds (getWidth() / 2 - 70, getHeight() - 60, 140, 36);
}

void SettingsPanel::paint (juce::Graphics& g)
{
    drawGlassCard (g, getLocalBounds().toFloat(), 22, 0.98f);
    g.setColour (col::text); g.setFont (display (18.0f));
    g.drawText (ru ("Настройки станции"), 30, 22, getWidth() - 60, 26, juce::Justification::centredLeft);
    g.setColour (col::textDim); g.setFont (semi (13.0f));
    g.drawText (ru ("Режим анализа"), 30, 80, 180, 30, juce::Justification::centredLeft);
    g.drawText (ru ("Профиль стиля"), 30, 118, 180, 30, juce::Justification::centredLeft);
    g.drawText (ru ("Характер Mooni"), 30, 156, 180, 30, juce::Justification::centredLeft);
    g.setColour (col::textFaint); g.setFont (body (12.0f));
    g.drawFittedText (ru ("Профиль задаёт контекст: ожидаемый баланс, плотность и громкость стиля. «Авто» угадывает стиль по звуку и честно помечает это как предположение. "
                          "Твои ориентиры: жёсткие треки ≈ −5 LUFS, трэп ≈ −7 LUFS — они учтены в профилях, но рекомендации всегда опираются на реальный материал."),
                      30, getHeight() - 140, getWidth() - 60, 70, juce::Justification::topLeft, 5);
}

// ===========================================================================
void AboutPanel::paint (juce::Graphics& g)
{
    drawGlassCard (g, getLocalBounds().toFloat(), 22, 0.98f);
    const float w = (float) getWidth();
    Pose p; p.smile = 0.8f; p.eyes = Eyes::Happy; p.blush = 0.6f; p.star = col::mint; p.starGlow = 1; p.starSpin = t * 0.8f;
    p.y = -0.06f * std::sin (t * 1.6f); p.arms = Arms::Wave; p.armPhase = t * 6; p.antennaBend = 0.3f * std::sin (t * 1.3f);
    MooniView::drawMooni (g, { w * 0.5f, 140.0f }, 62.0f, p, t);

    g.setColour (col::text); g.setFont (display (26.0f));
    g.drawText ("Mooni Analyzer", 0, 236, getWidth(), 34, juce::Justification::centred);
    g.setColour (col::textDim); g.setFont (semi (13.0f));
    g.drawText (ru ("версия ") + juce::String (MOONI_VERSION_STRING) + ru (" · лунная слуховая станция для мастеринга"), 0, 270, getWidth(), 20, juce::Justification::centred);

    // the credit, written in the stars
    const juce::String credit = ru ("Создано Ernomoon");
    g.setFont (display (20.0f));
    g.setColour (col::amber);
    g.drawText (credit, 0, 308, getWidth(), 30, juce::Justification::centred);
    juce::Random r (11);
    for (int i = 0; i < 14; ++i)
    {
        const float x = w * 0.25f + r.nextFloat() * w * 0.5f, y = 300.0f + r.nextFloat() * 46.0f;
        const float a = 0.3f + 0.6f * (0.5f + 0.5f * std::sin (t * 2.0f + (float) i));
        drawStar (g, { x, y }, 3.0f, 1.2f, t + (float) i, col::cream.withAlpha (a));
    }
    g.setColour (col::textDim); g.setFont (body (12.5f));
    g.drawFittedText (ru ("Mooni живёт на маленькой луне, которая вращается вокруг твоего мастер-канала, и слушает Землю — твой DAW.\n"
                          "Все измерения настоящие: громкость по EBU R128 / ITU-R BS.1770-4, True Peak с передискретизацией, спектр, стерео и фаза по полосам. "
                          "Где вывод — оценка, а не измерение, Mooni так и говорит.\n\n"
                          "Шрифты Nunito и Unbounded — SIL Open Font License. Сделано на JUCE. VST3 — торговая марка Steinberg Media Technologies GmbH."),
                      40, 356, getWidth() - 80, 120, juce::Justification::centredTop, 8);
    juce::ignoreUnused (ctx);
}
} // namespace mooni::ui
