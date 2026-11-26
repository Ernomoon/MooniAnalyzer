#include "MooniBrain.h"

namespace mooni::ui
{
static int BrainGrade (int s) { return s >= 92 ? 4 : s >= 82 ? 3 : s >= 68 ? 2 : s >= 50 ? 1 : 0; }

static juce::StringArray L (std::initializer_list<const char*> items)
{
    juce::StringArray a;
    for (auto* s : items) a.add (juce::String::fromUTF8 (s));
    return a;
}

MooniBrain::MooniBrain (MooniProcessor& p, MooniView& m, SayFn s) : proc (p), mooni (m), sayFn (std::move (s))
{
    lines["hello"] = L ({ "Привет! Я Mooni. Включи трек — я послушаю и скажу честно.",
                          "Лунная слуховая станция на связи. Жми Play на Земле — я слушаю.",
                          "Я Mooni. Живу на этой луне и слушаю чужие миксы. Давай твой." });
    lines["hello_back"] = L ({ "С возвращением на орбиту.", "О, снова ты. Я тут кратеры подметал.", "Станция в порядке. Ждём звук." });
    lines["late"] = L ({ "Ты вообще спишь? Ладно, давай мастерить.", "Три часа ночи — лучшее время для лимитера. Наверное.", "Даже Луна уже зевает. Но я с тобой." });

    lines["listen_first"] = L ({ "Тсс… слушаю.", "Уши на максимум. Играй.", "Приём, Земля. Сигнал идёт." });
    lines["listen_again"] = L ({ "Ещё разок, внимательнее.", "Переслушиваю. Посмотрим, что ты подкрутил.", "Второй дубль. Я весь внимание." });
    lines["ready_full"] = L ({ "Я уже понял характер твоего микса. Можешь продолжать воспроизведение или остановить его, чтобы получить полный анализ." });
    lines["ready_smart"] = L ({ "Готово! Этого фрагмента хватило — смотри отчёт. Можешь слушать дальше, я буду уточнять.",
                                "Понял, что тут происходит. Отчёт готов, а если играешь дальше — я дослушаю." });
    lines["too_short"] = L ({ "Ты остановил слишком рано — я ничего не успел понять.", "Мало звука. Дай мне хотя бы секунд десять." });

    lines["s_explode"] = L ({ "Это уже мастер. Я… кажется, у меня взорвалась голова.", "БУМ. Всё. Я в восторге. Не трогай ничего!", "Ого. Это звучит как с соседней галактики — в хорошем смысле." });
    lines["s_great"] = L ({ "Очень достойно. Пара штрихов — и будет огонь.", "Вот это уже разговор! Почти идеально.", "Хорошо. Я бы даже сказал — красиво." });
    lines["s_meh"] = L ({ "Ну, пойдёт… но можно было гораздо лучше.", "Хм. Нормально. Скучно, но нормально.", "Сойдёт для демки. Для релиза — давай поработаем." });
    lines["s_bad"] = L ({ "Хм. Тут есть над чем поработать. Серьёзно.", "Я морщусь. Ты видишь, как я морщусь?", "Это не ужасно. Но и не хорошо. Давай чинить." });
    lines["s_awful"] = L ({ "Что. Это. Было?!", "У меня уши свернулись в трубочку.", "Я буду жаловаться в Лунный союз звукорежиссёров!", "Это не микс, это авария на орбите." });

    lines["p_bass"] = L ({ "Бас трясёт всю мою луну!", "Внизу перебор — у меня кратеры дрожат." });
    lines["p_lowbass"] = L ({ "Низа маловато. Где саб? Я его ищу.", "Бас худенький. Покорми его." });
    lines["p_mud"] = L ({ "В низкой середине мутно, как в лунной пыли.", "250–400 Гц — тут каша. Я в ней застрял." });
    lines["p_harsh"] = L ({ "Ай-ай, верх режет уши!", "2–6 кГц колются. Я прикрыл уши." });
    lines["p_dull"] = L ({ "Глуховато. Будто слушаю через скафандр.", "Верха мало — открой окно, впусти воздух." });
    lines["p_clip"] = L ({ "Щёлк! Тут цифровой клиппинг.", "Оно трещит! Я слышу срезанные вершины." });
    lines["p_tp"] = L ({ "Пики выше нуля. На стримингах это захрипит.", "True Peak выше 0 дБTP — поставь потолок." });
    lines["p_phase"] = L ({ "Я слышу двоих себя… это фаза.", "В моно что-то исчезает. Фазовые проблемы!" });
    lines["p_lowstereo"] = L ({ "Бас гуляет по стерео. Сведи его в моно.", "Низ широкий — в клубе он развалится." });
    lines["p_squash"] = L ({ "Дышать нечем! Пересжато.", "Ты раздавил все пики. Им больно." });
    lines["p_quiet"] = L ({ "Тихо… Можно погромче? Если хочешь — посчитаю, сколько.", "Громкость скромная. Это выбор или забыл про пресс?" });
    lines["p_wide"] = L ({ "Меня растягивает! Слишком широко.", "Стерео шире, чем Луна. Сузь чуть-чуть." });
    lines["p_mono"] = L ({ "Это почти моно. Стерео на мастере не нарисовать.", "Левый и правый — близнецы. Где простор?" });
    lines["p_default"] = L ({ "Смотри список — я всё расписал по пунктам.", "Начни с верхней проблемы, она главная." });

    lines["better"] = L ({ "Лучше! На {} баллов лучше!", "Прогресс! +{} к счёту. Танцую.", "Вот! +{}. Я знал, что ты можешь." });
    lines["worse"] = L ({ "Стало хуже на {}. Ctrl+Z — твой друг.", "Минус {}. Может, откатим?", "Хм, −{}. Предыдущий вариант был лучше." });

    lines["bypass_on"] = L ({ "Обход включён — слышен оригинал. Я вздремну.", "Обработка выключена. Сплю.", "Луна в тени. Слушай оригинал." });
    lines["bypass_off"] = L ({ "А? Что? Я не спал!", "Обход выключен — обработка снова работает.", "Проснулся. Всё снова в деле." });
    lines["bypass_fair"] = L ({ "При обходе громкость оригинала выравнивается с обработкой — сравнивай звук, а не громкость." });

    lines["apply_eq"] = L ({ "Подкрутил! Нажми «Слушать заново», и я проверю.", "Готово. Эквалайзер настроен. Переслушаем?", "EQ на месте. Теперь честно сравни с оригиналом." });
    lines["apply_stereo"] = L ({ "Ширина на месте. Низ — в моно, как положено.", "Стерео настроено. Проверь в моно." });
    lines["apply_max"] = L ({ "Максимайзер настроен. Послушай и сравни с оригиналом через «Обход».", "Максимайзер включён. Смотри на «Достигнуто» после прослушивания." });
    lines["plan_ok"] = L ({ "Посчитал: драйв {}. Лимитер почти не напрягается.", "Цель достижима: драйв {}. Звучать будет чисто." });
    lines["plan_heavy"] = L ({ "Ради этой цифры лимитер будет давить ≈{} дБ. Я бы остановился на {} LUFS.", "Можно, но дорого: ≈{} дБ компрессии. Безопаснее — {} LUFS." });
    lines["plan_fail"] = L ({ "Громче этот материал не станет — только начнёт «качать».", "Упёрлись в потолок. Тут помогут клиппер или работа с пиками в миксе." });
    lines["reset"] = L ({ "Сбросил. Чистый лист.", "Как будто ничего и не было." });
    lines["undo"] = L ({ "Откатили.", "Шаг назад. Иногда это лучший шаг." });
    lines["redo"] = L ({ "Вернули.", "Шаг вперёд." });
    lines["ab"] = L ({ "Переключил вариант. Слушай разницу.", "Другой вариант настроек. Сравни." });
    lines["ref_loaded"] = L ({ "О, референс! Сравним.", "Референс загружен. Интересно, как у соседей…" });
    lines["ref_error"] = L ({ "Не получилось открыть этот файл. Попробуй WAV или FLAC.", "Телескоп не видит эту планету. Другой файл?" });
    lines["mono"] = L ({ "Моно-проверка: если что-то пропало — это фаза.", "Слушаем в моно, как телефон на столе." });
    lines["side"] = L ({ "Только Side — это всё, что живёт по краям.", "Слушаем разницу каналов. Ревер, ширина, призраки." });
    lines["listen_band"] = L ({ "Подслушиваем только эту полосу. Слышишь, о чём я?", "Вот он, проблемный участок. Без всего остального." });
    lines["copied"] = L ({ "Отчёт в буфере обмена. Можешь отправить кому угодно.", "Скопировал. Покажи это своему сведенщику." });

    lines["overs"] = L ({ "Ай! Пик выше нуля.", "Ой, перегруз мелькнул." });
    lines["silence"] = L ({ "Эй, звук пропал?", "Тишина… Ты там?" });
    lines["idle"] = L ({ "Я тут. Жду звук…", "Скучаю. Включи что-нибудь.", "Пересчитываю кратеры. Их 47. Включай трек." });
    lines["poke"] = L ({ "Хи-хи, щекотно!", "Эй!", "Я здесь, я здесь.", "Буп." });
    lines["poke_annoyed"] = L ({ "Эй, я работаю!", "Ещё раз — и я обижусь.", "Ты мне так анализ собьёшь." });
    lines["poke_hide"] = L ({ "Всё, я в домике.", "Ухожу за кратер. Позовёшь, когда успокоишься." });

    lines["room0"] = L ({ "Обзор: оценка и всё самое важное.", "Отсюда я слушаю Землю — твой DAW." });
    lines["room1"] = L ({ "Эквалайзер. Пушистые точки на кривой — это полосы, их можно таскать.", "Эквалайзер: пунктир — моя рекомендация." });
    lines["room2"] = L ({ "Стерео. Держи низ в моно, а верх — пусть гуляет.", "Проверь трек в моно — это быстро и полезно." });
    lines["room3"] = L ({ "Громкость. Выбери цель — я посчитаю драйв.", "Не раздави трек: громче ≠ лучше." });
    lines["room4"] = L ({ "Референс — ориентир, а не цель.", "Загрузи трек, который нравится по звуку." });
    lines["room5"] = L ({ "История: всё, что мы натворили.", "Отсюда можно вернуться к любому шагу." });

    lines["earth"] = L ({ "Это Земля. Там твой DAW. Привет, Земля!", "Земля пульсирует в такт твоему треку. Красиво.", "Отсюда видно всё. Даже твой мастер-канал." });
    lines["tips"] = L ({
        "Совет: сравнивай с оригиналом на одинаковой громкости. Громче почти всегда кажется лучше — это обман.",
        "Совет: держи всё ниже 100–120 Гц в моно. Клубы и колонки скажут спасибо.",
        "Совет: потолок −1 dBTP — безопасный выбор для стримингов. Громкие жанры часто ставят −0.3…−0.5.",
        "Совет: Spotify и YouTube нормализуют треки к ≈−14 LUFS. Лишняя громкость просто убавится.",
        "Совет: если EQ на мастере нужен больше ±4 дБ — проблему лучше чинить в миксе.",
        "Совет: проверь трек в моно. Что пропало — то в противофазе.",
        "Совет: делай перерывы. Уши устают за 40–60 минут, и всё начинает звучать нормально.",
        "Совет: слушай на тихой громкости — баланс видно лучше, чем на громкой.",
        "Совет: широкий колокол на −1 дБ слышно лучше, чем узкий на −6. Мастеринг — это мелочи.",
        "Совет: референс должен быть того же жанра. Сравнивать трэп с джазом бесполезно.",
        "Совет: клиппер перед лимитером помогает громким жанрам — но слушай, не трещит ли.",
        "Совет: в FL Studio ставь меня последним на мастер-канале — после всех остальных плагинов.",
        "Совет: кнопка «Подслушать» в проблеме включает только этот диапазон частот. Очень полезно." });

    sinceStart = 0;
    const int hour = juce::Time::getCurrentTime().getHours();
    if (hour >= 2 && hour < 5) say (pick ("late"), 5.0f);
    else if (proc.history.size() == 0) say (pick ("hello"), 6.0f);
    else say (pick ("hello_back"), 3.5f);

    seenSessions = proc.engine.sessionCounter();
    seenReady = proc.engine.readyCounter();
    seenReports = proc.engine.reportCounter();
    seenGestures = proc.paramGestureCount.load();
    seenRefChanges = proc.reference.changeCounter();
    wasBypassed = proc.value (params::bypass) > 0.5f;
    if (auto rep = proc.engine.getLatestReport()) { mooni.setMood (moodForScore (rep->score)); lastReactedSession = rep->session; lastReactedScore = rep->score; }
    else if (proc.history.size() > 0) { auto e = proc.history.entries(); mooni.setMood (moodForScore (e.back().report->score)); }
}

juce::String MooniBrain::pick (const juce::String& key)
{
    auto it = lines.find (key);
    if (it == lines.end() || it->second.isEmpty()) return {};
    const int n = it->second.size();
    int i = rng.nextInt (n);
    auto& last = lastPick[key];
    if (n > 1 && i == last) i = (i + 1) % n;
    last = i;
    return it->second[i];
}

bool MooniBrain::chatty (int minLevel) const { return proc.settings.chatter >= minLevel; }

void MooniBrain::say (const juce::String& text, float secs)
{
    if (text.isEmpty() || ! sayFn) return;
    sayFn (text, secs > 0 ? secs : juce::jlimit (2.5f, 9.0f, 1.2f + (float) text.length() * 0.055f));
}

void MooniBrain::queue (const juce::String& reaction, const juce::String& line)
{
    pending.emplace_back (reaction, line);
}

Mood MooniBrain::moodForScore (int s)
{
    if (s >= 90) return Mood::Excited;
    if (s >= 80) return Mood::Happy;
    if (s >= 65) return Mood::Meh;
    if (s >= 50) return Mood::Annoyed;
    return Mood::Furious;
}

juce::String MooniBrain::gradeWord (int s)
{
    if (s >= 92) return ru ("Взрыв мозга");
    if (s >= 82) return ru ("Отлично");
    if (s >= 68) return ru ("Нормально");
    if (s >= 50) return ru ("Слабовато");
    return ru ("Авария");
}

void MooniBrain::reactToReport (const Report& r, ReportPtr previous)
{
    const int s = r.score;
    juce::String reaction, key;
    if (s >= 92)      { reaction = "explode";     key = "s_explode"; }
    else if (s >= 82) { reaction = "excited";     key = "s_great"; }
    else if (s >= 68) { reaction = "meh";         key = "s_meh"; }
    else if (s >= 50) { reaction = "annoyed_mix"; key = "s_bad"; }
    else              { reaction = rng.nextBool() ? "furious" : "disgust"; key = "s_awful"; }

    if (previous && previous->valid && ! previous->fromReferenceFile)
    {
        const int d = s - previous->score;
        if (d >= 5 && s < 92) { reaction = "dance"; say (fmt (pick ("better").toRawUTF8(), d)); }
        else if (d <= -5)     { reaction = "facepalm"; say (fmt (pick ("worse").toRawUTF8(), -d)); }
        else say (pick (key));
    }
    else say (pick (key));

    mooni.play (reaction, true);
    mooni.setMood (moodForScore (s));

    // follow-up: the most important problem gets its own little performance
    if (! r.problems.empty() && s < 92)
    {
        const auto& top = r.problems.front();
        const auto id = top.id;
        juce::String re, k;
        if (id.startsWith ("sub_much") || id.startsWith ("bass_much"))       { re = "bass_shake";  k = "p_bass"; }
        else if (id.startsWith ("sub_little") || id.startsWith ("bass_little")) { re = "confused"; k = "p_lowbass"; }
        else if (id.startsWith ("lowmid_much") || id == "masking_low" || id == "muddy_space") { re = "muddy"; k = "p_mud"; }
        else if (id.startsWith ("highmid_much") || id.startsWith ("treble_much") || id == "harsh_dense") { re = "harsh_ears"; k = "p_harsh"; }
        else if (id.startsWith ("treble_little") || id.startsWith ("air_little")) { re = "meh"; k = "p_dull"; }
        else if (id == "clipping" || id == "flattops") { re = "clip_zap"; k = "p_clip"; }
        else if (id == "truepeak") { re = "flinch"; k = "p_tp"; }
        else if (id == "phase" || id == "mono_loss") { re = "phase_ghost"; k = "p_phase"; }
        else if (id == "low_stereo") { re = "too_wide"; k = "p_lowstereo"; }
        else if (id == "overlimited") { re = "squashed"; k = "p_squash"; }
        else if (id == "quiet") { re = "quiet_lean"; k = "p_quiet"; }
        else if (id == "too_wide") { re = "too_wide"; k = "p_wide"; }
        else if (id == "mono_mix") { re = "mono_squint"; k = "p_mono"; }
        else { re = "think"; k = "p_default"; }
        if (top.severity >= 0.3f) queue (re, pick (k));
    }
}

void MooniBrain::update (const LiveData& live, float dt)
{
    sinceStart += dt;
    overCooldown = std::max (0.0f, overCooldown - dt);
    gestureCooldown = std::max (0.0f, gestureCooldown - dt);
    pokeWindow = std::max (0.0f, pokeWindow - dt);
    if (pokeWindow <= 0) pokes = 0;

    // queued follow-ups play when Mooni is free
    if (! pending.empty() && ! mooni.isBusy())
    {
        auto [re, line] = pending.front(); pending.pop_front();
        mooni.play (re, true);
        if (line.isNotEmpty()) say (line);
    }

    // bypass -> eclipse sleep
    const bool byp = proc.value (params::bypass) > 0.5f;
    if (byp != wasBypassed)
    {
        wasBypassed = byp;
        if (byp) { mooni.play ("yawn", true); sayKey ("bypass_on"); }
        else     { mooni.setAsleep (false); mooni.play ("wake", true); sayKey ("bypass_off"); }
    }
    if (byp && ! mooni.isBusy()) mooni.setAsleep (true);

    const bool listening = live.state == AnalysisState::Listening || live.state == AnalysisState::Ready;
    mooni.setListening (listening && live.signalPresent && ! byp);

    // sessions
    if (const int s = proc.engine.sessionCounter(); s != seenSessions)
    {
        seenSessions = s;
        if (! byp)
        {
            const bool again = proc.history.size() > 0;
            mooni.play (again ? "reanalyze" : "listen_start", true);
            if (chatty (1) || ! again) sayKey (again ? "listen_again" : "listen_first");
        }
        idleSeconds = 0; idleQuipDone = false; silenceQuipDone = false;
    }
    if (const int rd = proc.engine.readyCounter(); rd != seenReady)
    {
        seenReady = rd;
        mooni.play ("aha", true);
        sayKey (live.mode == AnalysisMode::Smart ? "ready_smart" : "ready_full", 7.5f);
    }
    if (const int rc = proc.engine.reportCounter(); rc != seenReports)
    {
        seenReports = rc;
        if (auto rep = proc.engine.getLatestReport(); rep && rep->valid)
        {
            const bool newSession = rep->session != lastReactedSession;
            const bool bigChange = std::abs (rep->score - lastReactedScore) >= 4
                                   || BrainGrade (rep->score) != BrainGrade (lastReactedScore);
            if (newSession || bigChange)
            {
                auto prev = proc.history.previousReport (rep->session);
                reactToReport (*rep, newSession ? prev : nullptr);
                lastReactedSession = rep->session; lastReactedScore = rep->score;
            }
            else mooni.setMood (moodForScore (rep->score));
        }
    }
    if (live.state == AnalysisState::TooShort && wasListening) { mooni.play ("confused", true); sayKey ("too_short"); }
    wasListening = listening;

    // live events while listening
    if (listening)
    {
        if (live.onsetsTotal != seenOnsets) { if (live.onsetsTotal > seenOnsets) mooni.nod(); seenOnsets = live.onsetsTotal; }
        if (live.overs > seenOvers && overCooldown <= 0 && seenOvers >= 0)
        {
            mooni.play ("flinch");
            if (chatty (1) && rng.nextFloat() < 0.5f) sayKey ("overs");
            overCooldown = 8.0f;
        }
        seenOvers = live.overs;
        if (live.transportPlaying && ! live.signalPresent) silenceWhileListening += dt; else silenceWhileListening = 0;
        if (silenceWhileListening > 1.6f && ! silenceQuipDone && chatty (1)) { silenceQuipDone = true; mooni.play ("confused"); sayKey ("silence"); }
    }
    else { seenOvers = 0; seenOnsets = live.onsetsTotal; }

    // idleness
    if (! live.signalPresent && ! listening && ! byp) idleSeconds += dt; else { idleSeconds = 0; idleQuipDone = false; if (mooni.getMood() == Mood::Sleepy) mooni.setMood (Mood::Neutral); }
    if (idleSeconds > 40 && ! idleQuipDone && chatty (2)) { idleQuipDone = true; mooni.play ("tap_foot"); sayKey ("idle"); }
    if (idleSeconds > 150 && mooni.getMood() != Mood::Sleepy && ! proc.engine.getLatestReport()) mooni.setMood (Mood::Sleepy);

    // parameter gestures (from the UI or host automation)
    if (const int gc = proc.paramGestureCount.load(); gc != seenGestures)
    {
        seenGestures = gc;
        if (gestureCooldown <= 0 && ! byp)
        {
            const auto id = proc.lastGestureParam;
            if (id.startsWith ("imgW")) mooni.play ("stretch");
            else if (id == params::maxDrive || id == params::maxClip) mooni.play ("squish");
            else if (id.startsWith ("eq") && rng.nextFloat() < 0.3f) mooni.play ("think");
            gestureCooldown = 2.5f;
        }
    }

    // reference loaded / failed
    if (const int rc = proc.reference.changeCounter(); rc != seenRefChanges)
    {
        seenRefChanges = rc;
        const auto st = proc.reference.getStatus();
        if (st == ReferenceTrack::Status::Ready) { mooni.play ("reference_wow", true); sayKey ("ref_loaded"); }
        else if (st == ReferenceTrack::Status::Error) { mooni.play ("confused", true); sayKey ("ref_error"); }
    }
}

void MooniBrain::onRoomChanged (int r)
{
    room = r;
    if (sinceStart > 2.0f && chatty (1) && rng.nextFloat() < (proc.settings.chatter >= 2 ? 0.7f : 0.3f))
        sayKey ("room" + juce::String (r));
}

void MooniBrain::onPoked()
{
    ++pokes; pokeWindow = 8.0f;
    if (pokes >= 10) { mooni.play ("hide", true); sayKey ("poke_hide"); pokes = 0; }
    else if (pokes >= 5) { mooni.play ("annoyed", true); if (pokes == 5) sayKey ("poke_annoyed"); }
    else { mooni.play (pokes % 2 == 0 ? "giggle" : "poke", true); if (pokes == 1 || rng.nextFloat() < 0.3f) sayKey ("poke"); }
}

void MooniBrain::onEarthClicked() { mooni.play ("wave", true); sayKey ("earth"); }
void MooniBrain::onBipClicked()   { sayTip(); }
void MooniBrain::sayTip()         { mooni.play ("think", true); say (pick ("tips"), 8.0f); }

void MooniBrain::onAction (const juce::String& id, const juce::String& detail)
{
    if (id == "applyEq")          { mooni.play ("apply_fix", true); sayKey ("apply_eq"); }
    else if (id == "applyStereo") { mooni.play ("stretch", true); sayKey ("apply_stereo"); }
    else if (id == "applyMax")    { mooni.play ("squish", true); sayKey ("apply_max"); }
    else if (id == "reset")       { mooni.play ("bow", true); sayKey ("reset"); }
    else if (id == "undo")        { mooni.play ("flinch", true); sayKey ("undo"); }
    else if (id == "redo")        { mooni.play ("poke", true); sayKey ("redo"); }
    else if (id == "ab")          { mooni.play ("confused", true); if (chatty (1)) sayKey ("ab"); }
    else if (id == "mono")        { mooni.play ("mono_squint", true); sayKey ("mono"); }
    else if (id == "side")        { mooni.play ("phase_ghost", true); sayKey ("side"); }
    else if (id == "listenBand")  { mooni.play ("think", true); sayKey ("listen_band"); }
    else if (id == "copied")      { mooni.play ("happy", true); sayKey ("copied"); }
    else if (id == "fair")        { if (chatty (1)) sayKey ("bypass_fair"); }
    else if (id == "planOk")      { mooni.play ("happy", true); say (fmt (pick ("plan_ok").toRawUTF8(), detail)); }
    else if (id == "planHeavy")
    {
        auto parts = juce::StringArray::fromTokens (detail, "|", "");
        mooni.play ("annoyed_mix", true);
        say (fmt (pick ("plan_heavy").toRawUTF8(), parts[0], parts[1]));
    }
    else if (id == "planFail")    { mooni.play ("confused", true); sayKey ("plan_fail"); }
    else if (id == "say")         { say (detail); }
}
} // namespace mooni::ui
