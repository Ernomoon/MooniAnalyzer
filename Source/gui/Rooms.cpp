#include "Rooms.h"
#include "MooniBrain.h"

namespace mooni::ui
{
using juce::Point;
using namespace params;

juce::String makeTextReport (const Report& r, const MooniProcessor& p)
{
    juce::juce_wchar dash = 0x2014; juce::ignoreUnused (dash);
    const auto& s = r.stats;
    juce::String t;
    t << "Mooni Analyzer " << MOONI_VERSION_STRING << ru (" — отчёт (создано Ernomoon)\n");
    t << juce::Time (r.timeMs).formatted ("%d.%m.%Y %H:%M") << "\n\n";
    t << ru ("Оценка: ") << r.score << ru (" / 100 — ") << MooniBrain::gradeWord (r.score) << "\n" << r.verdict << "\n";
    t << ru ("Профиль: ") << genreProfiles()[(size_t) r.genreProfile].name;
    if (r.genreGuess.isNotEmpty()) t << ru (" (автоопределение: ") << r.genreGuess << ")";
    t << "\n\n";
    for (auto& sc : r.subscores) t << "  " << sc.name << ": " << sc.value << "  (" << sc.note << ")\n";
    t << ru ("\nИзмерения (выход плагина, ") << juce::String (s.secondsAnalysed, 1) << ru (" с):\n");
    t << ru ("  Интегральная громкость: ") << juce::String (s.integrated, 1) << " LUFS, LRA " << juce::String (s.lra, 1) << " LU\n";
    t << ru ("  Макс. кратковременная: ") << juce::String (s.maxShortTerm, 1) << ru (" LUFS, макс. мгновенная: ") << juce::String (s.maxMomentary, 1) << " LUFS\n";
    t << "  True Peak: " << juce::String (s.truePeakDb, 2) << ru (" дБTP, пик сэмплов: ") << juce::String (s.samplePeakDb, 2) << ru (" дБFS\n");
    t << "  PLR: " << juce::String (s.plr, 1) << ru (" дБ, крест-фактор: ") << juce::String (s.crestDb, 1) << ru (" дБ, микродинамика: ") << juce::String (s.microDynamicsDb, 1) << ru (" дБ\n");
    t << ru ("  Клиппинг: ") << s.clipEvents << ru (" событий, межсэмпловых перегрузок: ") << s.truePeakOvers << "\n";
    t << ru ("  Стерео: ширина ") << juce::roundToInt (s.widthPct) << ru ("%, корреляция ") << juce::String (s.correlation, 2)
      << ru (", баланс ") << juce::String (s.balanceDb, 1) << ru (" дБ, потеря в моно ") << juce::String (s.monoLossDb, 1) << ru (" дБ\n");
    t << ru ("  Ширина по полосам: ");
    for (int b = 0; b < 4; ++b) t << juce::roundToInt (s.bandWidthPct[(size_t) b]) << "% ";
    t << "\n\n" << ru ("Проблемы:\n");
    int i = 1;
    for (auto& pr : r.problems)
    {
        t << i++ << ". " << pr.title << (pr.measured ? "" : ru (" [оценка]")) << "\n   " << pr.what << "\n   " << pr.why << "\n   -> " << pr.fix << "\n";
    }
    if (! r.eq.empty())
    {
        t << ru ("\nРекомендации EQ:\n");
        const auto tn = filterTypeNames(); const auto cn = channelNames();
        for (auto& e : r.eq) t << "  " << tn[e.type] << " " << hz (e.freq) << " " << signedNum (e.gainDb, 1) << ru (" дБ, Q ") << juce::String (e.q, 1) << " (" << cn[e.channel] << ") — " << e.reason << "\n";
    }
    t << ru ("\nРекомендуемая ширина полос: ");
    for (int b = 0; b < 4; ++b) t << juce::roundToInt (r.recWidthPct[(size_t) b]) << "% ";
    t << ru ("\nОриентир громкости: ") << juce::String (r.recTargetLufs, 1) << " LUFS. " << r.targetReason << "\n";
    juce::ignoreUnused (p);
    return t;
}

// ===========================================================================
ObservatoryRoom::ObservatoryRoom (UiContext& c) : Room (c), problems (c)
{
    addAndMakeVisible (dial);
    addAndMakeVisible (subs);
    addAndMakeVisible (problems);
    addAndMakeVisible (metrics);

    modeFull.setButtonText (ru ("Весь трек"));
    modeSmart.setButtonText (ru ("Умный анализ"));
    for (auto* b : { &modeFull, &modeSmart }) { setButtonStyle (*b, ButtonStyle::Pill); b->setClickingTogglesState (false); addAndMakeVisible (*b); }
    modeFull.setTooltip (ru ("Слушаю трек целиком. Когда пойму характер — скажу. Полный отчёт — после остановки."));
    modeSmart.setTooltip (ru ("Сыграй самый характерный фрагмент: я сам пойму, когда материала достаточно, и сразу покажу отчёт."));
    modeFull.onClick = [this] { auto s = ctx.proc.settings; s.mode = AnalysisMode::Full; ctx.proc.setSettings (s); };
    modeSmart.onClick = [this] { auto s = ctx.proc.settings; s.mode = AnalysisMode::Smart; ctx.proc.setSettings (s); };

    listenBtn.setButtonText (ru ("Слушать заново"));
    listenBtn.setTooltip (ru ("Начать новое прослушивание. Предыдущий отчёт останется в журнале."));
    listenBtn.onClick = [this] { ctx.proc.engine.startSession(); };
    finishBtn.setButtonText (ru ("Готово — анализируй"));
    finishBtn.setTooltip (ru ("Завершить прослушивание сейчас, не дожидаясь остановки."));
    finishBtn.onClick = [this] { ctx.proc.engine.finishSession(); };
    setButtonStyle (finishBtn, ButtonStyle::Primary);
    addAndMakeVisible (listenBtn); addAndMakeVisible (finishBtn);

    autoBtn.setButtonText (ru ("Слушать сам при Play"));
    autoBtn.onClick = [this] { auto s = ctx.proc.settings; s.autoListen = autoBtn.getToggleState(); ctx.proc.setSettings (s); };
    addAndMakeVisible (autoBtn);

    applyAllBtn.setButtonText (ru ("Применить всё"));
    applyAllBtn.setTooltip (ru ("Применить рекомендации EQ и стерео. Сигнал до этого не менялся. Отменить: Ctrl+Z."));
    applyAllBtn.onClick = [this]
    {
        if (! ctx.report || ! ctx.report->valid) return;
        ctx.proc.applyEqRecommendations (*ctx.report);
        ctx.proc.applyStereoRecommendations (*ctx.report);
        if (ctx.action) ctx.action ("applyEq", {});
        if (ctx.toast) ctx.toast (ru ("Рекомендации EQ и стерео применены. Нажми «Слушать заново», чтобы проверить результат."), col::mint);
    };
    setButtonStyle (applyAllBtn, ButtonStyle::Primary);
    copyBtn.setButtonText (ru ("Отчёт в буфер"));
    copyBtn.onClick = [this]
    {
        if (! ctx.report) return;
        juce::SystemClipboard::copyTextToClipboard (makeTextReport (*ctx.report, ctx.proc));
        if (ctx.action) ctx.action ("copied", {});
    };
    addAndMakeVisible (applyAllBtn); addAndMakeVisible (copyBtn);
}

MooniSpot ObservatoryRoom::mooniSpot() const
{
    return { { 262.0f, 300.0f }, 78.0f, 0.0f, false, { 10.0f, 10.0f, 370.0f, 200.0f } };
}

void ObservatoryRoom::resized()
{
    dial.setBounds (408, 52, 328, 300);
    subs.setBounds (412, 430, 320, 148);
    problems.setBounds (784, 58, 386, 380);
    metrics.setBounds (776, 486, 400, 108);
    modeFull.setBounds (40, 532, 150, 28);
    modeSmart.setBounds (196, 532, 160, 28);
    autoBtn.setBounds (40, 566, 316, 24);
    listenBtn.setBounds (40, 596, 150, 30);
    finishBtn.setBounds (196, 596, 160, 30);
    applyAllBtn.setBounds (412, 590, 156, 30);
    copyBtn.setBounds (576, 590, 156, 30);
}

void ObservatoryRoom::reportChanged()
{
    dial.setReport (ctx.report, ctx.isStale());
    subs.setReport (ctx.report);
    problems.setReport (ctx.report);
    applyAllBtn.setEnabled (ctx.report && ctx.report->valid && ! ctx.isStale());
    copyBtn.setEnabled (ctx.report && ctx.report->valid);

    std::vector<Metric> m;
    if (ctx.report && ctx.report->valid)
    {
        const auto& s = ctx.report->stats;
        const auto& g = genreProfiles()[(size_t) ctx.report->genreProfile];
        m.push_back ({ ru ("Громкость"), juce::String (s.integrated, 1), "LUFS", ru ("Интегральная громкость (EBU R128) за всё прослушивание."), std::abs (s.integrated - ctx.report->recTargetLufs) < 2.5f ? col::mint : col::amber });
        m.push_back ({ "True Peak", juce::String (s.truePeakDb, 1), ru ("дБTP"), ru ("Межсэмпловый пик (4x передискретизация, BS.1770)."), s.truePeakDb > -0.1f ? col::coral : col::mint });
        m.push_back ({ "PLR", juce::String (s.plr, 1), ru ("дБ"), ru ("Пик минус громкость. Мало — пересжато, много — запас для громкости."), s.plr < g.minPlr - 1 ? col::coral : col::text });
        m.push_back ({ "LRA", juce::String (s.lra, 1), "LU", ru ("Диапазон громкости между частями трека (EBU Tech 3342)."), col::text });
        m.push_back ({ ru ("Ширина"), juce::String (juce::roundToInt (s.widthPct)), "%", ru ("Отношение Side к Mid. 0% — моно, 100% — несвязанные каналы."), s.widthPct > 140 || s.widthPct < 8 ? col::amber : col::text });
        m.push_back ({ ru ("Корреляция"), juce::String (s.correlation, 2), "", ru ("+1 — моно, 0 — широкое стерео, < 0 — противофаза."), s.correlation < 0.15f ? col::coral : col::text });
        auto spot = [&] (float lufs) { return std::max (0.0f, s.integrated - lufs); };
        translation = fmt ("Стриминги убавят: Spotify/YouTube −{} дБ, Apple Music −{} дБ  •  телефон: {} дБ  •  моно: {} дБ",
                           num (spot (-14), 1), num (spot (-16), 1), num (s.phoneLossDb, 1), num (s.monoLossDb, 1));
    }
    else translation.clear();
    metrics.setMetrics (m, 3);
    repaint();
}

void ObservatoryRoom::refresh (float dt)
{
    dial.tick (dt);
    const auto mode = ctx.proc.settings.mode;
    modeFull.setToggleState (mode == AnalysisMode::Full, juce::dontSendNotification);
    modeSmart.setToggleState (mode == AnalysisMode::Smart, juce::dontSendNotification);
    autoBtn.setToggleState (ctx.proc.settings.autoListen, juce::dontSendNotification);
    const bool active = ctx.live.state == AnalysisState::Listening || ctx.live.state == AnalysisState::Ready;
    finishBtn.setEnabled (active && ctx.live.activeSeconds >= 3.0);
    repaint (24, 396, 352, 240);
}

void ObservatoryRoom::paint (juce::Graphics& g)
{
    // status card (bottom-left, under Mooni)
    const auto& L = ctx.live;
    auto status = juce::Rectangle<int> (24, 400, 352, 234);
    drawGlassCard (g, status.toFloat());

    // where are we in the workflow: 1 listen -> 2 report -> 3 fix -> 4 check
    const bool hasReport = ctx.report && ctx.report->valid;
    const bool listening = L.state == AnalysisState::Listening || L.state == AnalysisState::Ready;
    int step = 0;
    if (listening) step = 0;
    else if (hasReport && ctx.isStale()) step = 3;
    else if (hasReport) step = 2;
    const char* stepNames[] = { "Прослушать", "Отчёт", "Исправить", "Проверить" };
    {
        const float w = (float) (status.getWidth() - 24) / 4.0f;
        for (int i = 0; i < 4; ++i)
        {
            auto cell = juce::Rectangle<float> ((float) status.getX() + 12 + w * (float) i, (float) status.getY() + 12, w - 4, 24);
            const bool done = i < step || (i == 1 && hasReport), cur = i == step;
            g.setColour (cur ? col::lilac : (done ? col::mint.withAlpha (0.22f) : col::night0.withAlpha (0.6f)));
            g.fillRoundedRectangle (cell, 12);
            g.setColour (cur ? col::night0 : (done ? col::mint : col::textFaint));
            g.setFont (heavy (11.0f));
            g.drawText ((done && ! cur ? juce::String::fromUTF8 ("✓ ") : juce::String (i + 1) + " ") + juce::String::fromUTF8 (stepNames[i]), cell, juce::Justification::centred);
        }
    }

    juce::String title, sub;
    juce::Colour accent = col::lilac;
    const int secs = (int) L.activeSeconds;
    switch (L.state)
    {
        case AnalysisState::Idle:
            title = hasReport ? (ctx.isStale() ? ru ("Проверь изменения") : ru ("Отчёт готов")) : ru ("Жду звук");
            sub = hasReport ? (ctx.isStale() ? ru ("Настройки изменились. Нажми Play (или «Слушать заново») — я оценю результат.")
                                             : ru ("Смотри список «Что исправить» справа и применяй рекомендации."))
                            : (ctx.proc.settings.autoListen ? ru ("Нажми Play в FL Studio — я начну слушать сам.") : ru ("Нажми «Слушать заново», затем Play."));
            break;
        case AnalysisState::Listening:
            title = fmt ("Слушаю… {} с", secs); accent = col::earth;
            sub = L.mode == AnalysisMode::Full ? ru ("Слушаю трек целиком. Останови воспроизведение — получишь отчёт.")
                                               : ru ("Играй самый характерный фрагмент — припев или дроп.");
            if (! L.signalPresent) sub = ru ("Тишина на входе… я жду.");
            break;
        case AnalysisState::Ready:
            accent = col::mint;
            title = L.mode == AnalysisMode::Full ? ru ("Характер понятен") : ru ("Отчёт готов, уточняю");
            sub = L.mode == AnalysisMode::Full ? ru ("Можешь продолжать или остановить — я доделаю отчёт.") : ru ("Можно остановить. Если играешь дальше, отчёт обновится.");
            break;
        case AnalysisState::Done:
            title = ctx.isStale() ? ru ("Проверь изменения") : ru ("Анализ готов"); accent = ctx.isStale() ? col::amber : col::mint;
            sub = ctx.isStale() ? ru ("Ты изменил настройки. Нажми Play или «Слушать заново» — я сравню с прошлой оценкой.")
                                : (ctx.report ? fmt ("Прослушано {} с · стиль «{}». Теперь — исправления справа.", juce::roundToInt (ctx.report->stats.secondsAnalysed), genreProfiles()[(size_t) ctx.report->genreProfile].name) : juce::String());
            break;
        case AnalysisState::TooShort:
            title = ru ("Мало звука"); accent = col::amber;
            sub = ru ("Нужно хотя бы несколько секунд музыки. Попробуй ещё раз.");
            break;
    }
    g.setColour (accent); g.setFont (display (16.0f));
    g.drawText (title, status.getX() + 16, status.getY() + 46, status.getWidth() - 32, 22, juce::Justification::centredLeft);
    g.setColour (col::textDim); g.setFont (body (12.5f));
    g.drawFittedText (sub, status.getX() + 16, status.getY() + 70, status.getWidth() - 32, 34, juce::Justification::topLeft, 2);
    if (listening)
    {
        auto bar = juce::Rectangle<float> ((float) status.getX() + 16, (float) status.getY() + 106, (float) status.getWidth() - 32, 6);
        g.setColour (col::night0); g.fillRoundedRectangle (bar, 3);
        g.setColour (accent); g.fillRoundedRectangle (bar.withWidth (bar.getWidth() * juce::jlimit (0.0f, 1.0f, L.confidence)), 3);
        g.setColour (col::textFaint); g.setFont (semi (10.5f));
        g.drawText (fmt ("уверенность {}%", juce::roundToInt (L.confidence * 100)), bar.translated (0, 4).withHeight (14), juce::Justification::centredLeft);
    }

    // score card
    auto sc = juce::Rectangle<int> (392, 16, 360, 616);
    card (g, sc, ru ("Оценка мастера"), ru ("насколько трек готов к релизу, 1–100"));
    if (ctx.report && ctx.report->valid)
    {
        g.setColour (col::text); g.setFont (semi (14.0f));
        g.drawFittedText (ctx.report->verdict, 412, 352, 320, 40, juce::Justification::centred, 2);
        juce::String chip = ctx.proc.settings.genreChoice > 0 ? ru ("профиль: ") + genreProfiles()[(size_t) ctx.proc.settings.genreChoice].name
                                                              : ru ("похоже на: ") + ctx.report->genreGuess + ru (" (предположение)");
        if (ctx.report->usedReference) chip << ru (" • с референсом");
        drawChip (g, juce::Rectangle<float> (412, 398, 320, 22), chip, col::lilac, col::night0.withAlpha (0.7f), 11.5f);
    }

    auto pc = juce::Rectangle<int> (768, 16, 416, 430);
    card (g, pc, ru ("Что исправить"), ctx.report && ctx.report->valid ? plural ((int) ctx.report->problems.size(), "замечание", "замечания", "замечаний") + ru (", главное — сверху") : ru ("появится после первого прослушивания"));
    auto mc = juce::Rectangle<int> (768, 456, 416, 176);
    card (g, mc, ru ("Ключевые измерения"));
    if (translation.isNotEmpty())
    {
        g.setColour (col::textDim); g.setFont (body (11.0f));
        g.drawFittedText (translation, mc.getX() + 14, mc.getBottom() - 34, mc.getWidth() - 28, 30, juce::Justification::centredLeft, 2);
    }
}

// ===========================================================================
EqBandStrip::EqBandStrip (UiContext& c) : ctx (c)
{
    auto& ap = ctx.proc.apvts;
    for (int i = 0; i < kEqBands; ++i)
    {
        auto& col_ = cols[(size_t) i];
        col_.on.setButtonText (ru ("Полоса ") + juce::String (i + 1));
        col_.type.addItemList (filterTypeNames(), 1);
        col_.channel.addItemList (channelNames(), 1);
        for (auto* s : { &col_.freq, &col_.gain, &col_.q })
        {
            s->setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
            s->setTextBoxStyle (juce::Slider::TextBoxBelow, false, 56, 14);
            s->setColour (juce::Slider::rotarySliderFillColourId, s == &col_.gain ? col::amber : (s == &col_.q ? col::mint : col::lilac));
            addAndMakeVisible (*s);
        }
        col_.freq.setTooltip (ru ("Частота")); col_.gain.setTooltip (ru ("Усиление, дБ")); col_.q.setTooltip (ru ("Добротность Q"));
        addAndMakeVisible (col_.on); addAndMakeVisible (col_.type); addAndMakeVisible (col_.channel);
        col_.aOn = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (ap, eqBandOn (i), col_.on);
        col_.aType = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (ap, eqBandType (i), col_.type);
        col_.aCh = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (ap, eqBandCh (i), col_.channel);
        col_.aF = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (ap, eqBandFreq (i), col_.freq);
        col_.aG = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (ap, eqBandGain (i), col_.gain);
        col_.aQ = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (ap, eqBandQ (i), col_.q);
        // formatters must be set after the attachments (they install their own)
        col_.freq.textFromValueFunction = [] (double v) { return hz ((float) v); };
        col_.freq.valueFromTextFunction = [] (const juce::String& t) { const double v = t.retainCharacters ("0123456789.,").replaceCharacter (',', '.').getDoubleValue(); return t.containsIgnoreCase (juce::String::fromUTF8 ("к")) || t.containsIgnoreCase ("k") ? v * 1000.0 : v; };
        col_.gain.textFromValueFunction = [] (double v) { return signedNum ((float) v, 1) + ru (" дБ"); };
        col_.gain.valueFromTextFunction = [] (const juce::String& t) { return t.retainCharacters ("-0123456789.,").replaceCharacter (',', '.').getDoubleValue(); };
        col_.q.textFromValueFunction = [] (double v) { return "Q " + juce::String (v, v < 10 ? 2 : 1); };
        col_.q.valueFromTextFunction = [] (const juce::String& t) { return t.retainCharacters ("0123456789.,").replaceCharacter (',', '.').getDoubleValue(); };
        col_.freq.updateText(); col_.gain.updateText(); col_.q.updateText();
    }
}

void EqBandStrip::resized()
{
    const int w = getWidth() / kEqBands;
    for (int i = 0; i < kEqBands; ++i)
    {
        auto r = juce::Rectangle<int> (i * w, 0, w, getHeight()).reduced (5, 2);
        auto& c = cols[(size_t) i];
        c.on.setBounds (r.removeFromTop (22));
        c.type.setBounds (r.removeFromTop (22).reduced (0, 1));
        c.channel.setBounds (r.removeFromTop (22).reduced (0, 1));
        r.removeFromTop (2);
        auto knobs = r;
        auto top = knobs.removeFromTop (knobs.getHeight() / 2);
        c.freq.setBounds (top.removeFromLeft (top.getWidth() / 2));
        c.gain.setBounds (top);
        c.q.setBounds (knobs.withSizeKeepingCentre (knobs.getWidth() / 2, knobs.getHeight()));
    }
}

void EqBandStrip::paint (juce::Graphics& g)
{
    const int w = getWidth() / kEqBands;
    const auto bands = ctx.proc.currentEqBands();
    for (int i = 0; i < kEqBands; ++i)
    {
        auto r = juce::Rectangle<int> (i * w, 0, w, getHeight()).reduced (2, 0).toFloat();
        g.setColour ((bands[(size_t) i].enabled ? col::night0.withAlpha (0.55f) : col::night0.withAlpha (0.25f)));
        g.fillRoundedRectangle (r, 10);
        if (i > 0) { g.setColour (col::line.withAlpha (0.5f)); g.drawVerticalLine (i * w, 8.0f, (float) getHeight() - 8.0f); }
    }
}

// ===========================================================================
EqRoom::EqRoom (UiContext& c) : Room (c), graph (c), strip (c)
{
    addAndMakeVisible (graph);
    addAndMakeVisible (strip);

    applyBtn.setButtonText (ru ("Применить рекомендации"));
    applyBtn.setTooltip (ru ("Настроить EQ по рекомендациям анализа. Существующие полосы аккуратно дополняются. Отменить: Ctrl+Z."));
    applyBtn.onClick = [this]
    {
        if (! ctx.report || ctx.report->eq.empty()) return;
        ctx.proc.applyEqRecommendations (*ctx.report);
        if (ctx.action) ctx.action ("applyEq", {});
    };
    setButtonStyle (applyBtn, ButtonStyle::Primary);
    resetBtn.setButtonText (ru ("Сбросить EQ"));
    resetBtn.onClick = [this] { ctx.proc.resetEq(); if (ctx.action) ctx.action ("reset", {}); };
    compareBtn.setButtonText (ru ("Сравнить без EQ (удерживай)"));
    compareBtn.setTooltip (ru ("Пока кнопка нажата, EQ выключен — слушай разницу."));
    compareBtn.onStateChange = [this]
    {
        static bool wasDown = false;
        const bool down = compareBtn.isDown();
        if (down != wasDown)
        {
            wasDown = down;
            if (auto* p = ctx.proc.param (eqOn)) { ctx.proc.suppressSnapshots = true; p->setValueNotifyingHost (down ? 0.0f : 1.0f); ctx.proc.suppressSnapshots = false; }
        }
    };
    for (auto* b : { &applyBtn, &resetBtn, &compareBtn }) addAndMakeVisible (*b);

    eqOnBtn.setButtonText (ru ("EQ включён"));
    aEqOn = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (ctx.proc.apvts, eqOn, eqOnBtn);
    showInBtn.setButtonText (ru ("Спектр входа"));
    showInBtn.onClick = [this] { graph.setShowInput (showInBtn.getToggleState()); };
    showRecBtn.setButtonText (ru ("Рекомендация"));
    showRecBtn.setToggleState (true, juce::dontSendNotification);
    showRecBtn.onClick = [this] { graph.setShowRecommendation (showRecBtn.getToggleState()); };
    for (auto* b : { &eqOnBtn, &showInBtn, &showRecBtn }) addAndMakeVisible (*b);
}

MooniSpot EqRoom::mooniSpot() const
{
    // peeks over the top edge of the actions card, watching the curve
    return { { 1064.0f, 446.0f }, 40.0f, 448.0f, false, { 700.0f, 290.0f, 480.0f, 150.0f } };
}

void EqRoom::resized()
{
    graph.setBounds (22, 52, 1156, 382);
    strip.setBounds (24, 450, 904, 176);
    applyBtn.setBounds (958, 500, 214, 34);
    compareBtn.setBounds (958, 542, 214, 30);
    resetBtn.setBounds (958, 580, 214, 30);
    eqOnBtn.setBounds (700, 20, 130, 26);
    showInBtn.setBounds (836, 20, 140, 26);
    showRecBtn.setBounds (982, 20, 150, 26);
}

void EqRoom::refresh (float dt)
{
    graph.refresh (dt);
    const bool rec = ctx.report && ctx.report->valid && ! ctx.report->eq.empty();
    applyBtn.setEnabled (rec && ! ctx.isStale());
    strip.repaint();
}

void EqRoom::paint (juce::Graphics& g)
{
    juce::String sub = ru ("рельеф твоего микса и кривая эквалайзера");
    if (ctx.report && ctx.report->valid)
        sub = ctx.isStale() ? ru ("настройки изменились после анализа — переслушай, чтобы обновить рекомендацию")
                            : fmt ("рекомендация из анализа {} — сигнал не изменится, пока не применишь", juce::Time (ctx.report->timeMs).formatted ("%H:%M"));
    card (g, { 16, 16, 1168, 424 }, ru ("Кривая эквалайзера"), sub);
    card (g, { 16, 446, 920, 186 }, juce::String());
    card (g, { 946, 446, 238, 186 }, ru ("Действия"));
}

// ===========================================================================
WidthBands::WidthBands (UiContext& c) : ctx (c)
{
    for (int i = 0; i < 4; ++i)
    {
        auto& s = sliders[(size_t) i];
        s.setSliderStyle (juce::Slider::LinearVertical);
        s.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 60, 16);
        s.setColour (juce::Slider::trackColourId, col::earth);
        s.setTooltip (ru ("Ширина полосы: 0% — моно, 100% — без изменений, 200% — вдвое шире."));
        addAndMakeVisible (s);
        att[(size_t) i] = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (ctx.proc.apvts, imgWidth (i), s);
        s.textFromValueFunction = [] (double v) { return juce::String (juce::roundToInt (v)) + "%"; };
        s.valueFromTextFunction = [] (const juce::String& t) { return t.retainCharacters ("0123456789.,").replaceCharacter (',', '.').getDoubleValue(); };
        s.updateText();
    }
}

juce::Rectangle<float> WidthBands::ruler() const { return getLocalBounds().toFloat().removeFromTop (40).reduced (8, 6); }
float WidthBands::xForFreq (float f) const { auto r = ruler(); return r.getX() + r.getWidth() * std::log (f / 20.0f) / std::log (1000.0f); }
float WidthBands::freqForX (float x) const { auto r = ruler(); return 20.0f * std::pow (1000.0f, juce::jlimit (0.0f, 1.0f, (x - r.getX()) / r.getWidth())); }

void WidthBands::resized()
{
    auto r = getLocalBounds().withTrimmedTop (48);
    const int w = r.getWidth() / 4;
    for (int i = 0; i < 4; ++i)
        sliders[(size_t) i].setBounds (juce::Rectangle<int> (r.getX() + i * w, r.getY(), w, r.getHeight()).reduced (w / 2 - 22, 30).withTrimmedBottom (-26));
}

void WidthBands::paint (juce::Graphics& g)
{
    auto ru_ = ruler();
    g.setColour (col::night0.withAlpha (0.6f)); g.fillRoundedRectangle (ru_, 8);
    const float xo[3] { ctx.proc.value (imgXover (0)), ctx.proc.value (imgXover (1)), ctx.proc.value (imgXover (2)) };
    const juce::Colour bandCol[4] { col::coral.withAlpha (0.35f), col::amber.withAlpha (0.3f), col::earth.withAlpha (0.3f), col::mint.withAlpha (0.3f) };
    float prev = ru_.getX();
    for (int b = 0; b < 4; ++b)
    {
        const float x = b < 3 ? xForFreq (xo[b]) : ru_.getRight();
        g.setColour (bandCol[b]); g.fillRoundedRectangle (juce::Rectangle<float> (prev, ru_.getY(), x - prev, ru_.getHeight()).reduced (1, 3), 5);
        prev = x;
    }
    for (float f : { 50.f, 100.f, 200.f, 500.f, 1000.f, 2000.f, 5000.f, 10000.f })
    {
        g.setColour (col::textFaint); g.setFont (semi (9.5f));
        g.drawText (f >= 1000 ? juce::String (juce::roundToInt (f / 1000)) + ru ("к") : juce::String ((int) f), juce::Rectangle<float> (30, 10).withCentre ({ xForFreq (f), ru_.getBottom() + 1 }), juce::Justification::centred);
    }
    for (int i = 0; i < 3; ++i)
    {
        const float x = xForFreq (xo[i]);
        const bool hl = i == dragXo || i == hoverXo;
        g.setColour (hl ? col::cream : col::lilac); g.fillRoundedRectangle (x - 3, ru_.getY() - 2, 6, ru_.getHeight() + 4, 3);
        if (hl) { g.setFont (heavy (11)); g.drawText (hz (xo[i]), juce::Rectangle<float> (70, 14).withCentre ({ x, ru_.getY() - 9 }), juce::Justification::centred); }
    }

    // band columns: measured width (bar), recommendation (marker), correlation moon
    auto area = getLocalBounds().withTrimmedTop (48);
    const int w = area.getWidth() / 4;
    const juce::String names[4] = { ru ("Низ"), ru ("Низ. середина"), ru ("Середина"), ru ("Верх") };
    const bool has = ctx.report && ctx.report->valid;
    for (int b = 0; b < 4; ++b)
    {
        auto col_ = juce::Rectangle<int> (area.getX() + b * w, area.getY(), w, area.getHeight()).reduced (6, 0).toFloat();
        g.setColour (col::night0.withAlpha (0.35f)); g.fillRoundedRectangle (col_, 12);
        g.setColour (col::text); g.setFont (heavy (13.0f));
        g.drawText (names[b], col_.removeFromTop (24), juce::Justification::centred);
        const float lo = b == 0 ? 20.0f : xo[b - 1], hi = b == 3 ? 20000.0f : xo[b];
        g.setColour (col::textFaint); g.setFont (semi (10.5f));
        g.drawText (hzRange (lo, hi), col_.removeFromTop (14), juce::Justification::centred);

        auto slider = sliders[(size_t) b].getBounds().toFloat();
        auto barArea = juce::Rectangle<float> (slider.getRight() + 4, slider.getY() + 8, 10, slider.getHeight() - 34);
        g.setColour (col::night0); g.fillRoundedRectangle (barArea, 4);
        if (has)
        {
            const float measured = ctx.report->stats.bandWidthPct[(size_t) b];
            const float h = barArea.getHeight() * juce::jlimit (0.0f, 1.0f, measured / 200.0f);
            g.setColour (col::cream.withAlpha (0.7f)); g.fillRoundedRectangle (barArea.withTop (barArea.getBottom() - h), 4);
            const float rec = ctx.report->recWidthPct[(size_t) b];
            const float sy = slider.getY() + 8 + (slider.getHeight() - 34) * (1.0f - rec / 200.0f);
            g.setColour (col::amber); g.fillRoundedRectangle (slider.getX() - 16, sy - 2, 14, 4, 2);
            g.setFont (semi (9.5f)); g.drawText (juce::String (juce::roundToInt (rec)), juce::Rectangle<float> (slider.getX() - 40, sy - 7, 22, 14), juce::Justification::centredRight);
            drawMoonPhase (g, juce::Rectangle<float> (22, 22).withCentre ({ col_.getCentreX() + 36, col_.getY() + 10 }), ctx.report->stats.bandCorr[(size_t) b], col::cream, col::night0.brighter (0.2f));
            g.setColour (col::textDim); g.setFont (semi (10.5f));
            g.drawText (fmt ("изм. {}%", juce::roundToInt (measured)), juce::Rectangle<float> (col_.getX(), col_.getY(), col_.getWidth() * 0.5f, 20), juce::Justification::centredRight);
        }
    }
}

void WidthBands::mouseMove (const juce::MouseEvent& e)
{
    int h = -1;
    if (ruler().expanded (0, 6).contains (e.position))
        for (int i = 0; i < 3; ++i) if (std::abs (e.position.x - xForFreq (ctx.proc.value (imgXover (i)))) < 8) h = i;
    if (h != hoverXo) { hoverXo = h; setMouseCursor (h >= 0 ? juce::MouseCursor::LeftRightResizeCursor : juce::MouseCursor::NormalCursor); repaint(); }
}

void WidthBands::mouseDown (const juce::MouseEvent& e)
{
    mouseMove (e);
    dragXo = hoverXo;
    if (dragXo >= 0) if (auto* p = ctx.proc.param (imgXover (dragXo))) p->beginChangeGesture();
}

void WidthBands::mouseDrag (const juce::MouseEvent& e)
{
    if (dragXo < 0) return;
    if (auto* p = ctx.proc.param (imgXover (dragXo))) p->setValueNotifyingHost (p->convertTo0to1 (freqForX (e.position.x)));
    repaint();
}

void WidthBands::mouseUp (const juce::MouseEvent&)
{
    if (dragXo >= 0) if (auto* p = ctx.proc.param (imgXover (dragXo))) p->endChangeGesture();
    dragXo = -1;
}

// ===========================================================================
TidesRoom::TidesRoom (UiContext& c) : Room (c), bands (c)
{
    addAndMakeVisible (bands);
    addAndMakeVisible (mirror);
    addAndMakeVisible (phase);
    phase.setTooltip (ru ("Корреляция: полная луна — моно (+1), половина — широкое стерео (0), тёмная — противофаза (−1, плохо)."));

    balance.setSliderStyle (juce::Slider::LinearHorizontal);
    balance.setTextBoxStyle (juce::Slider::TextBoxRight, false, 64, 18);
    balance.setTooltip (ru ("Баланс левый/правый. Плюс — громче правый."));
    aBal = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (ctx.proc.apvts, imgBal, balance);
    balance.textFromValueFunction = [] (double v) { return signedNum ((float) v, 2) + ru (" дБ"); };
    balance.valueFromTextFunction = [] (const juce::String& t) { return t.retainCharacters ("-0123456789.,").replaceCharacter (',', '.').getDoubleValue(); };
    balance.updateText();
    addAndMakeVisible (balance);

    onBtn.setButtonText (ru ("Стерео-обработка включена"));
    aOn = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (ctx.proc.apvts, imgOn, onBtn);
    addAndMakeVisible (onBtn);

    applyBtn.setButtonText (ru ("Применить рекомендации"));
    applyBtn.onClick = [this] { if (ctx.report && ctx.report->valid) { ctx.proc.applyStereoRecommendations (*ctx.report); if (ctx.action) ctx.action ("applyStereo", {}); } };
    setButtonStyle (applyBtn, ButtonStyle::Primary);
    resetBtn.setButtonText (ru ("Сбросить"));
    resetBtn.onClick = [this] { ctx.proc.resetStereo(); if (ctx.action) ctx.action ("reset", {}); };
    monoBtn.setButtonText (ru ("Моно-проверка"));
    monoBtn.setClickingTogglesState (true);
    monoBtn.onClick = [this]
    {
        ctx.proc.monitor = monoBtn.getToggleState() ? (int) MonitorMode::Mono : (int) MonitorMode::Normal;
        if (monoBtn.getToggleState() && ctx.action) ctx.action ("mono", {});
    };
    sideBtn.setButtonText (ru ("Только Side"));
    sideBtn.setClickingTogglesState (true);
    sideBtn.onClick = [this]
    {
        ctx.proc.monitor = sideBtn.getToggleState() ? (int) MonitorMode::Side : (int) MonitorMode::Normal;
        if (sideBtn.getToggleState() && ctx.action) ctx.action ("side", {});
    };
    for (auto* b : { &applyBtn, &resetBtn, &monoBtn, &sideBtn }) addAndMakeVisible (*b);
}

MooniSpot TidesRoom::mooniSpot() const
{
    return { { 1058.0f, 548.0f }, 56.0f, 0.0f, false, { 760.0f, 380.0f, 430.0f, 140.0f } };
}

void TidesRoom::resized()
{
    bands.setBounds (24, 62, 744, 378);
    mirror.setBounds (800, 54, 270, 270);
    phase.setBounds (1080, 70, 96, 96);
    balance.setBounds (40, 506, 420, 28);
    onBtn.setBounds (480, 506, 200, 26);
    applyBtn.setBounds (40, 560, 210, 34);
    resetBtn.setBounds (258, 560, 120, 34);
    monoBtn.setBounds (386, 560, 140, 34);
    sideBtn.setBounds (534, 560, 130, 34);
}

void TidesRoom::refresh (float)
{
    mirror.setData (ctx.live.scope, ctx.live.correlation);
    phase.setCorrelation (ctx.live.correlation);
    applyBtn.setEnabled (ctx.report && ctx.report->valid && ! ctx.isStale());
    const int mon = ctx.proc.monitor.load();
    monoBtn.setToggleState (mon == (int) MonitorMode::Mono, juce::dontSendNotification);
    sideBtn.setToggleState (mon == (int) MonitorMode::Side, juce::dontSendNotification);
    repaint (786, 330, 398, 110);
    bands.repaint();
}

void TidesRoom::paint (juce::Graphics& g)
{
    card (g, { 16, 16, 760, 430 }, ru ("Ширина по полосам"), ru ("белый столбик — измерено в твоём миксе, жёлтая метка — рекомендация"));
    card (g, { 786, 16, 398, 430 }, ru ("Вектороскоп и фаза"));
    card (g, { 16, 456, 720, 176 }, ru ("Баланс и проверка"));

    const auto& L = ctx.live;
    auto info = juce::Rectangle<int> (800, 334, 370, 100);
    g.setFont (semi (12.5f));
    auto line = [&] (const juce::String& k, const juce::String& v, juce::Colour c)
    {
        auto r = info.removeFromTop (22);
        g.setColour (col::textDim); g.drawText (k, r, juce::Justification::centredLeft);
        g.setColour (c); g.setFont (heavy (13.0f)); g.drawText (v, r, juce::Justification::centredRight); g.setFont (semi (12.5f));
    };
    line (ru ("Корреляция сейчас"), juce::String (L.correlation, 2), L.correlation < 0 ? col::coral : col::text);
    line (ru ("Ширина сейчас"), juce::String (juce::roundToInt (L.widthPct)) + "%", col::text);
    if (ctx.report && ctx.report->valid)
    {
        const auto& s = ctx.report->stats;
        line (ru ("Потеря в моно (анализ)"), juce::String (s.monoLossDb, 1) + ru (" дБ"), s.monoLossDb < -4.5f ? col::amber : col::text);
        line (ru ("Баланс Л/П (анализ)"), signedNum (s.balanceDb, 1) + ru (" дБ"), std::abs (s.balanceDb) > 0.8f ? col::amber : col::text);
    }
    // tide summary
    auto sum = juce::Rectangle<int> (744, 470, 0, 0);
    juce::ignoreUnused (sum);
    if (ctx.report && ctx.report->valid)
    {
        g.setColour (col::textDim); g.setFont (body (12.0f));
        juce::String rec = ru ("Рекомендация: ");
        for (int b = 0; b < 4; ++b) rec << juce::roundToInt (ctx.report->recWidthPct[(size_t) b]) << (b < 3 ? "% · " : "%");
        rec << ru ("; баланс ") << signedNum (ctx.report->recBalanceDb, 1) << ru (" дБ");
        g.drawFittedText (rec, 40, 604, 680, 20, juce::Justification::centredLeft, 1);
    }
}
} // namespace mooni::ui
