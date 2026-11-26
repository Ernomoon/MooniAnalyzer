#include "Diagnostics.h"
#include "../core/Text.h"
#include "../dsp/Biquad.h"
#include <algorithm>
#include <numeric>

namespace mooni::diagnostics
{
using dsp::FilterType;
static constexpr int N = BandGrid::kSixthBands;

float niceFrequency (float f)
{
    if (f < 100) return std::round (f);
    if (f < 1000) return std::round (f / 10.0f) * 10.0f;
    if (f < 10000) return std::round (f / 100.0f) * 100.0f;
    return std::round (f / 500.0f) * 500.0f;
}

static float interpLog (const std::vector<std::pair<float, float>>& pts, float f)
{
    if (f <= pts.front().first) return pts.front().second;
    if (f >= pts.back().first) return pts.back().second;
    for (size_t i = 1; i < pts.size(); ++i)
        if (f <= pts[i].first)
        {
            const float t = std::log (f / pts[i - 1].first) / std::log (pts[i].first / pts[i - 1].first);
            return pts[i - 1].second + t * (pts[i].second - pts[i - 1].second);
        }
    return pts.back().second;
}

std::array<float, N> relativeBands (const std::array<float, N>& absDb)
{
    double sum = 0; int n = 0;
    for (int i = 0; i < N; ++i)
    {
        const float f = BandGrid::sixthCentre (i);
        if (f >= 500 && f <= 2000 && absDb[(size_t) i] > -110) { sum += std::pow (10.0, absDb[(size_t) i] / 10.0); ++n; }
    }
    const float ref = n > 0 ? (float) (10.0 * std::log10 (sum / n)) : 0.0f;
    std::array<float, N> out {};
    for (int i = 0; i < N; ++i) out[(size_t) i] = absDb[(size_t) i] > -110 ? absDb[(size_t) i] - ref : -60.0f;
    return out;
}

static std::array<float, N> smooth (const std::array<float, N>& x, int radius)
{
    std::array<float, N> y {};
    for (int i = 0; i < N; ++i)
    {
        float s = 0; int n = 0;
        for (int k = -radius; k <= radius; ++k)
        {
            const int j = i + k;
            if (j >= 0 && j < N) { s += x[(size_t) j]; ++n; }
        }
        y[(size_t) i] = s / (float) n;
    }
    return y;
}

std::array<float, N> targetCurve (const GenreProfile& g, const MixStats* reference)
{
    const float L = g.lowShelfDb, S = g.subExtraDb, k = g.highSlopeDbOct;
    const std::vector<std::pair<float, float>> pts {
        { 20, L + S - 9 }, { 30, L + S - 2 }, { 45, L + S }, { 60, L + 0.5f * S }, { 90, L * 0.9f },
        { 150, L * 0.55f }, { 250, L * 0.2f }, { 500, 0 }, { 1000, 0 }, { 2000, k }, { 4000, 2 * k },
        { 8000, 3 * k }, { 12000, 3.58f * k - 0.5f }, { 16000, 4.0f * k - 2.5f }, { 20000, 4.32f * k - 6.0f } };

    std::array<float, N> t {};
    for (int i = 0; i < N; ++i) t[(size_t) i] = interpLog (pts, BandGrid::sixthCentre (i));
    // re-normalise the target the same way measurements are normalised
    std::array<float, N> asAbs {};
    for (int i = 0; i < N; ++i) asAbs[(size_t) i] = t[(size_t) i];
    t = relativeBands (asAbs);

    if (reference != nullptr && reference->isValid())
    {
        auto ref = smooth (relativeBands (reference->sixth), 1);
        for (int i = 0; i < N; ++i)
            if (reference->sixth[(size_t) i] > -110)
                t[(size_t) i] = 0.75f * ref[(size_t) i] + 0.25f * t[(size_t) i];
    }
    return t;
}

int detectGenre (const MixStats& s, float& confidence)
{
    const float I = s.integrated, micro = s.microDynamicsDb, low = s.lowEndShare;
    const float onsetRate = s.secondsAnalysed > 1 ? (float) (s.onsets / s.secondsAnalysed) : 0.0f;
    auto rel = relativeBands (s.sixth);
    float hi = 0; int n = 0;
    for (int i = 0; i < N; ++i) { const float f = BandGrid::sixthCentre (i); if (f >= 3000 && f <= 10000) { hi += rel[(size_t) i]; ++n; } }
    hi = n > 0 ? hi / (float) n : -8.0f;

    auto bell = [] (float x, float centre, float width) { const float d = (x - centre) / width; return std::exp (-d * d); };
    std::array<float, 7> sc {};
    sc[1] = bell (low, 0.45f, 0.15f) * bell (I, -7.5f, 3.0f) * bell (micro, 5.5f, 2.5f);            // trap
    sc[2] = bell (I, -5.5f, 2.0f) * bell (micro, 4.0f, 1.8f) * (0.6f + 0.4f * bell (hi, -4.0f, 4.0f)); // heavy
    sc[3] = bell (low, 0.32f, 0.12f) * bell (I, -7.5f, 2.5f) * bell (onsetRate, 2.0f, 1.2f);          // edm
    sc[4] = bell (low, 0.24f, 0.12f) * bell (I, -9.0f, 2.5f) * bell (micro, 7.0f, 2.0f);              // pop
    sc[5] = bell (low, 0.16f, 0.10f) * bell (I, -9.0f, 3.0f) * bell (hi, -4.0f, 3.0f) * bell (micro, 7.0f, 2.5f); // rock
    sc[6] = std::max (bell (I, -15.0f, 4.0f), bell (micro, 11.0f, 2.5f)) * bell (low, 0.15f, 0.15f);  // acoustic
    int best = 1; float bestV = -1, second = 0;
    for (int i = 1; i < 7; ++i) if (sc[(size_t) i] > bestV) { second = bestV; bestV = sc[(size_t) i]; best = i; } else second = std::max (second, sc[(size_t) i]);
    confidence = bestV > 1e-6f ? juce::jlimit (0.0f, 1.0f, (bestV - std::max (0.0f, second)) / bestV + 0.25f * bestV) : 0.0f;
    return best;
}

// ---------------------------------------------------------------------------
namespace
{
struct Region { const char* id; float lo, hi; };
const Region kRegions[] = {
    { "sub", 25, 60 }, { "bass", 60, 200 }, { "lowmid", 200, 500 }, { "mid", 500, 2000 },
    { "highmid", 2000, 6000 }, { "treble", 6000, 12000 }, { "air", 12000, 18000 } };

float regionMean (const std::array<float, N>& d, float lo, float hi, float nyquist)
{
    float s = 0; int n = 0;
    for (int i = 0; i < N; ++i)
    {
        const float f = BandGrid::sixthCentre (i);
        if (f >= lo && f <= hi && f < nyquist * 0.9f) { s += d[(size_t) i]; ++n; }
    }
    return n > 0 ? s / (float) n : 0.0f;
}

float sevFrom (float excess, float scale) { return juce::jlimit (0.0f, 1.0f, excess / scale); }

int scoreFrom (float x, float good, float bad)
{
    // linear map: x == good -> 100, x == bad -> 0
    const float t = (x - bad) / (good - bad);
    return juce::jlimit (0, 100, juce::roundToInt (t * 100.0f));
}
} // namespace

Report diagnose (const MixStats& s, const DiagnosticsContext& ctx)
{
    Report r;
    r.stats = s;
    r.timeMs = juce::Time::currentTimeMillis();
    r.paramFingerprint = ctx.fingerprint;
    if (! s.isValid())
    {
        r.valid = false;
        r.verdict = ru ("Слишком мало звука для выводов. Дай мне послушать хотя бы несколько секунд.");
        return r;
    }
    r.valid = true;

    // --- style profile ------------------------------------------------------
    float conf = 0;
    const int guessed = detectGenre (s, conf);
    r.genreGuess = genreProfiles()[(size_t) guessed].name;
    r.genreConfidence = conf;
    r.genreProfile = ctx.genreChoice > 0 ? ctx.genreChoice : guessed;
    const auto& g = genreProfiles()[(size_t) r.genreProfile];
    const float nyq = (float) s.sampleRate * 0.5f;
    const bool hasRef = ctx.reference != nullptr && ctx.reference->isValid();
    r.usedReference = hasRef;

    // --- tonal balance ---------------------------------------------------------
    const auto rel = relativeBands (s.sixth);
    r.targetSixth = targetCurve (g, hasRef ? ctx.reference : nullptr);
    std::array<float, N> dev {};
    for (int i = 0; i < N; ++i) dev[(size_t) i] = rel[(size_t) i] - r.targetSixth[(size_t) i];
    const auto devS = smooth (dev, 1);
    const float tol = hasRef ? std::min (g.tolerance, 2.5f) : g.tolerance;

    auto tolAt = [&] (float f) { return tol + (f < 60 ? 1.5f : 0.0f) + (f > 12000 ? 1.5f : 0.0f); };

    double wsum = 0, esum = 0;
    for (int i = 0; i < N; ++i)
    {
        const float f = BandGrid::sixthCentre (i);
        if (f < 30 || f > 16000 || f > nyq * 0.9f) continue;
        const float w = (f >= 100 && f <= 8000) ? 1.0f : 0.5f;
        const float e = std::max (0.0f, std::abs (devS[(size_t) i]) - 0.5f * tolAt (f));
        esum += w * e * e; wsum += w;
    }
    const float tonalRms = wsum > 0 ? (float) std::sqrt (esum / wsum) : 0.0f;

    auto& P = r.problems;
    auto add = [&] (Problem p) { if (p.severity > 0.05f) P.push_back (p); };

    struct RegionText { const char* id; const char* tooMuchTitle; const char* tooMuchWhy; const char* tooMuchFix;
                        const char* tooLittleTitle; const char* tooLittleWhy; const char* tooLittleFix; };
    static const RegionText texts[] = {
        { "sub",
          "Саб-бас перегружен",
          "Лишний саб съедает запас по пикам: лимитер начинает «качать» весь микс, а на большинстве систем этого низа даже не слышно.",
          "Ослабь область {} на {} дБ полкой или широким колоколом. Проверь, что 808/кик остаются плотными.",
          "Не хватает саб-баса",
          "Для этого стиля низ ниже 60 Гц обычно плотнее — без него трек звучит легче, чем задумано.",
          "Аккуратно подними {} на {} дБ (полка НЧ) или добавь саб в аранжировке — эквалайзером из пустоты низ не вытащить." },
        { "bass",
          "Слишком много баса",
          "Перегруженный бас маскирует кик и низ вокала, микс становится тяжёлым и «гудящим».",
          "Убери {} дБ в районе {}. Слушай, чтобы кик не потерял удар.",
          "Бас тонковат",
          "Без тела в 60–200 Гц микс кажется худым, особенно на нормальных колонках.",
          "Добавь {} дБ широким колоколом в районе {}." },
        { "lowmid",
          "Мутность в низкой середине",
          "В районе 200–500 Гц копится энергия от всего сразу: бас, пэды, гитары, ревер. Это главный источник «каши».",
          "Ослабь {} на {} дБ широким колоколом — микс станет прозрачнее.",
          "Провал в низкой середине",
          "Без 200–500 Гц пропадает теплота и тело инструментов, микс звучит пусто.",
          "Мягко добавь {} дБ в районе {}." },
        { "mid",
          "Середина выпирает",
          "Избыток 500 Гц – 2 кГц даёт «коробочный» или гнусавый звук и быстро утомляет.",
          "Убери {} дБ в районе {} — найди точное место колоколом, послушай.",
          "Провал середины",
          "Середина — это вокал и основная мелодия. Если её мало, главное в треке теряется на телефонах и в машине.",
          "Подними {} на {} дБ или сделай вокал/лид громче в миксе." },
        { "highmid",
          "Резкость в верхней середине",
          "Ухо особенно чувствительно к 2–6 кГц: избыток здесь режет слух и утомляет уже через минуту.",
          "Ослабь {} на {} дБ. Если резкость только на пиках — лучше динамический EQ на источнике.",
          "Мало верхней середины",
          "Без 2–6 кГц теряются атака, разборчивость слов и «читаемость» на маленьких динамиках.",
          "Добавь {} дБ в районе {} — осторожно, эта зона быстро становится резкой." },
        { "treble",
          "Верх перегружен",
          "Избыток 6–12 кГц даёт шипящие, колючие тарелки и сибилянты.",
          "Попробуй аккуратно убрать {} дБ в районе {}, но проверь, не потеряется ли ощущение воздуха.",
          "Тусклый верх",
          "Без 6–12 кГц микс звучит глухо, будто через одеяло.",
          "Добавь {} дБ полкой ВЧ в районе {}." },
        { "air",
          "Слишком много «воздуха»",
          "Избыток выше 12 кГц звучит как шипение и усиливает шумы и артефакты.",
          "Срежь {} дБ полкой в районе {}.",
          "Нет воздуха",
          "Самый верх отвечает за ощущение открытости и дорогого звука.",
          "Попробуй +{} дБ мягкой полкой ВЧ в районе {}." },
    };

    float regionDev[7] {};
    for (int k = 0; k < 7; ++k)
    {
        const auto& reg = kRegions[k];
        if (reg.lo > nyq * 0.85f) continue;
        const float m = regionMean (devS, reg.lo, reg.hi, nyq);
        regionDev[k] = m;
        const float t = tolAt (std::sqrt (reg.lo * reg.hi));
        if (std::abs (m) <= t) continue;

        const auto& tx = texts[k];
        Problem p;
        p.id = juce::String (reg.id) + (m > 0 ? "_much" : "_little");
        p.category = ru ("Тональный баланс");
        p.module = Module::Eq;
        p.freqLo = reg.lo; p.freqHi = reg.hi;
        const float amount = std::round (juce::jlimit (1.0f, 5.0f, (std::abs (m) - t * 0.5f) * 0.7f) * 2.0f) / 2.0f;
        const auto range = hzRange (reg.lo, reg.hi);
        const auto refWord = hasRef ? ru ("эталона") : ru ("ориентира стиля «") + g.name + ru ("»");
        if (m > 0)
        {
            p.title = ru (tx.tooMuchTitle);
            p.what = fmt ("Энергия в диапазоне {} выше {} примерно на {} дБ.", range, refWord, num (m, 1));
            p.why = ru (tx.tooMuchWhy);
            p.fix = (k == 0 || k == 2 || k == 4) ? fmt (tx.tooMuchFix, range, num (amount, 1)) : fmt (tx.tooMuchFix, num (amount, 1), range);
        }
        else
        {
            p.title = ru (tx.tooLittleTitle);
            p.what = fmt ("Энергия в диапазоне {} ниже {} примерно на {} дБ.", range, refWord, num (-m, 1));
            p.why = ru (tx.tooLittleWhy);
            p.fix = (k == 0 || k == 3) ? fmt (tx.tooLittleFix, range, num (amount, 1)) : fmt (tx.tooLittleFix, num (amount, 1), range);
        }
        p.severity = sevFrom (std::abs (m) - t, 5.0f) * (k == 0 || k == 6 ? 0.7f : 1.0f) + 0.15f;
        add (p);
    }

    // masking: bass + low-mid both heavy
    if (regionDev[1] > tol * 0.6f && regionDev[2] > tol * 0.6f)
    {
        Problem p;
        p.id = "masking_low"; p.category = ru ("Маскировка"); p.module = Module::Eq; p.measured = false;
        p.title = ru ("Низ и низкая середина маскируют друг друга");
        p.what = fmt ("И 60–200 Гц (+{} дБ), и 200–500 Гц (+{} дБ) выше ориентира одновременно.", num (regionDev[1]), num (regionDev[2]));
        p.why = ru ("Когда обе зоны перегружены, кик, бас и низ вокала сливаются в одно пятно — это оценка по спектру, проверь на слух.");
        p.fix = ru ("Реши, кто главный внизу. Обычно помогает вырез 250–350 Гц и чуть меньше 100–150 Гц на мастере, но лучше — разведение кика и баса в миксе.");
        p.freqLo = 80; p.freqHi = 400;
        p.severity = 0.35f;
        add (p);
    }

    // --- EQ fitting (greedy, gentle) -------------------------------------------
    {
        auto resid = devS;
        const double fs = s.sampleRate > 0 ? s.sampleRate : 48000.0;
        for (int iter = 0; iter < 5; ++iter)
        {
            int peak = -1; float best = 0;
            for (int i = 0; i < N; ++i)
            {
                const float f = BandGrid::sixthCentre (i);
                if (f < 25 || f > 18000 || f > nyq * 0.85f) continue;
                // never chase deficits at the extremes: missing sub < 35 Hz or air > 16 kHz is usually
                // the arrangement, and boosting there only wastes headroom
                if (resid[(size_t) i] < 0 && (f < 35.0f || f > 16000.0f)) continue;
                const float excess = std::abs (resid[(size_t) i]) - tolAt (f) * 0.75f;
                const float w = (f >= 60 && f <= 10000) ? 1.0f : 0.7f;
                if (excess * w > best) { best = excess * w; peak = i; }
            }
            if (peak < 0) break;
            const float sign = resid[(size_t) peak] > 0 ? 1.0f : -1.0f;
            const float pv = resid[(size_t) peak];
            int lo = peak, hi = peak;
            while (lo > 0 && resid[(size_t) lo - 1] * sign > std::abs (pv) * 0.5f) --lo;
            while (hi < N - 1 && resid[(size_t) hi + 1] * sign > std::abs (pv) * 0.5f && BandGrid::sixthCentre (hi + 1) < nyq * 0.85f) ++hi;
            const float fLo = BandGrid::sixthCentre (lo), fHi = BandGrid::sixthCentre (hi);
            float centre = std::sqrt (fLo * fHi);
            const float bwOct = juce::jlimit (0.5f, 3.0f, std::log2 (fHi / fLo) + 1.0f / 3.0f);
            const float qBell = std::sqrt (std::pow (2.0f, bwOct)) / (std::pow (2.0f, bwOct) - 1.0f);

            EqSuggestion e;
            e.gainDb = juce::jlimit (-5.0f, 3.5f, -pv * 0.65f);
            e.gainDb = std::round (e.gainDb * 2.0f) / 2.0f;
            if (std::abs (e.gainDb) < 0.5f) break;
            if (lo == 0 || (fLo < 45 && centre < 80))
            {
                e.type = (int) FilterType::LowShelf; e.freq = niceFrequency (juce::jlimit (40.0f, 200.0f, fHi * 0.8f)); e.q = 0.7f;
            }
            else if (fHi > 9000 && hi >= N - 4)
            {
                e.type = (int) FilterType::HighShelf; e.freq = niceFrequency (juce::jlimit (5000.0f, 14000.0f, fLo * 1.2f)); e.q = 0.7f;
            }
            else
            {
                e.type = (int) FilterType::Bell; e.freq = niceFrequency (centre); e.q = std::round (juce::jlimit (0.4f, 2.5f, qBell) * 10.0f) / 10.0f;
            }
            e.reason = e.gainDb < 0 ? fmt ("Избыток в районе {}", hzRange (fLo, fHi)) : fmt ("Недостаток в районе {}", hzRange (fLo, fHi));

            // merge with an existing suggestion of the same kind nearby instead of stacking filters
            auto near = std::find_if (r.eq.begin(), r.eq.end(), [&] (const EqSuggestion& x)
                                      { return x.type == e.type && x.channel == e.channel && std::abs (std::log2 (x.freq / e.freq)) < 0.5f; });
            if (near != r.eq.end())
            {
                const float merged = juce::jlimit (-6.0f, 4.0f, near->gainDb + e.gainDb);
                const auto prev = *near;
                if (std::abs (merged) < 0.5f) r.eq.erase (near); else near->gainDb = std::round (merged * 2.0f) / 2.0f;
                // undo the previous filter's contribution and apply the merged one
                const auto cPrev = dsp::BiquadCoeffs::make ((FilterType) prev.type, fs, prev.freq, prev.q, prev.gainDb);
                const auto cNew = dsp::BiquadCoeffs::make ((FilterType) prev.type, fs, prev.freq, prev.q, std::abs (merged) < 0.5f ? 0.0f : merged);
                for (int i = 0; i < N; ++i)
                {
                    const double f = BandGrid::sixthCentre (i);
                    resid[(size_t) i] += (float) (cNew.magnitudeDb (f, fs) - cPrev.magnitudeDb (f, fs));
                }
                if (std::abs (merged - prev.gainDb) < 0.25f) break;   // no further progress possible
                continue;
            }
            r.eq.push_back (e);

            const auto c = dsp::BiquadCoeffs::make ((FilterType) e.type, fs, e.freq, e.q, e.gainDb);
            for (int i = 0; i < N; ++i) resid[(size_t) i] += (float) c.magnitudeDb (BandGrid::sixthCentre (i), fs);
            (void) centre;
        }
    }

    // rumble / DC
    if (s.infrasonicRatioDb > -14.0f || s.dcOffsetDb > -50.0f)
    {
        EqSuggestion e;
        e.type = (int) FilterType::LowCut; e.freq = (r.genreProfile == 1 || r.genreProfile == 2) ? 20.0f : 25.0f; e.q = 0.7f; e.gainDb = 0;
        e.reason = ru ("Инфранизкий гул / DC");
        r.eq.insert (r.eq.begin(), e);

        Problem p;
        p.id = "infrasonic"; p.category = ru ("Тональный баланс"); p.module = Module::Eq;
        p.title = s.dcOffsetDb > -50.0f ? ru ("Постоянная составляющая (DC) в сигнале") : ru ("Неслышимый инфранизкий гул");
        p.what = s.dcOffsetDb > -50.0f ? fmt ("Смещение нуля около {} дБFS.", num (s.dcOffsetDb, 0))
                                        : fmt ("Энергия ниже 25 Гц всего на {} дБ тише полезного низа (25–200 Гц).", num (-s.infrasonicRatioDb, 0));
        p.why = ru ("Это почти не слышно, но съедает запас по пикам и заставляет лимитер работать впустую.");
        p.fix = fmt ("Поставь срез НЧ около {} — я уже добавил его в рекомендации.", hz (e.freq));
        p.freqLo = 10; p.freqHi = 30;
        p.severity = s.dcOffsetDb > -40.0f ? 0.55f : 0.3f;
        add (p);
    }

    // narrow resonances (fine resolution vs local average)
    {
        struct Res { float f, prom; };
        std::vector<Res> res;
        for (int i = 4; i < BandGrid::kFineBands - 4; ++i)
        {
            const float f = BandGrid::fineCentre (i);
            if (f < 150 || f > 8000 || f > nyq * 0.8f) continue;
            float avg = 0; int n = 0;
            for (int k = -8; k <= 8; ++k) if (std::abs (k) >= 2) { avg += s.fine[(size_t) (i + k)]; ++n; }
            avg /= (float) n;
            const float prom = s.fine[(size_t) i] - avg;
            if (prom > 7.0f && s.fine[(size_t) i] >= s.fine[(size_t) i - 1] && s.fine[(size_t) i] >= s.fine[(size_t) i + 1])
                res.push_back ({ f, prom });
        }
        std::sort (res.begin(), res.end(), [] (auto& a, auto& b) { return a.prom > b.prom; });
        for (size_t k = 0; k < std::min<size_t> (2, res.size()); ++k)
        {
            Problem p;
            p.id = "resonance_" + juce::String (juce::roundToInt (res[k].f));
            p.category = ru ("Резонансы"); p.module = Module::Eq;
            p.title = fmt ("Возможный резонанс около {}", hz (niceFrequency (res[k].f)));
            p.what = fmt ("Узкий пик на {} дБ выше соседних частот, держится на протяжении всего прослушивания.", num (res[k].prom, 1));
            p.why = ru ("Постоянный узкий пик часто звучит как «звон» или гул. Но это может быть и нота тональности трека — проверь на слух.");
            p.fix = fmt ("Нажми «Подслушать». Если звенит — узкий вырез (Q 6–8) на 2–3 дБ около {}.", hz (niceFrequency (res[k].f)));
            p.freqLo = res[k].f / 1.12f; p.freqHi = res[k].f * 1.12f;
            p.severity = sevFrom (res[k].prom - 6.0f, 8.0f) * 0.6f;
            add (p);
            const bool covered = std::any_of (r.eq.begin(), r.eq.end(), [&] (const EqSuggestion& x)
                                              { return x.gainDb < 0 && std::abs (std::log2 (x.freq / res[k].f)) < 0.35f; });
            if (res[k].prom >= 9.0f && r.eq.size() < 7 && ! covered)
            {
                EqSuggestion e; e.type = (int) FilterType::Bell; e.freq = niceFrequency (res[k].f);
                e.q = 6.0f; e.gainDb = -std::round (juce::jlimit (1.5f, 4.0f, (res[k].prom - 5.0f) * 0.6f) * 2.0f) / 2.0f;
                e.reason = ru ("Узкий резонанс"); r.eq.push_back (e);
            }
        }
    }

    // --- loudness & peaks ----------------------------------------------------------
    if (s.truePeakDb > -0.1f)
    {
        Problem p;
        p.id = "truepeak"; p.category = ru ("Пики"); p.module = Module::Maximizer;
        p.title = ru ("Пики выше 0 дБTP");
        p.what = fmt ("True Peak {} дБTP, межсэмпловые перегрузки в {} фрагментах по 100 мс.", signedNum (s.truePeakDb, 2), s.truePeakOvers);
        p.why = ru ("Такие пики искажаются при конвертации в MP3/AAC/OGG на стримингах и в дешёвых ЦАПах.");
        p.fix = ru ("Включи Гравитационный пресс с потолком −1.0 дБTP (для очень громких мастеров допустимо −0.3…−0.5).");
        p.severity = juce::jlimit (0.3f, 1.0f, 0.4f + (s.truePeakDb + 0.1f) * 0.25f + s.truePeakOvers * 0.002f);
        add (p);
    }
    if (s.clipEvents > 0 && s.clipEventsPerMinute > 0.3f)
    {
        Problem p;
        p.id = "clipping"; p.category = ru ("Искажения"); p.module = Module::Maximizer;
        p.title = ru ("Цифровой клиппинг");
        p.what = fmt ("Найдено {} срезанных вершин на уровне пика (≈{} в минуту): несколько одинаковых сэмплов подряд.", s.clipEvents, num (s.clipEventsPerMinute, 1));
        p.why = ru ("Жёсткий срез даёт неприятные гармоники и «треск», особенно на кике, 808 и вокале.");
        p.fix = ru ("Найди, где в цепочке сигнал упирается в 0 дБFS (мастер-канал, клиппер, сумма), убавь вход или замени жёсткий клип мягким клиппером Гравитации с передискретизацией.");
        p.severity = juce::jlimit (0.3f, 1.0f, 0.3f + s.clipEventsPerMinute / 30.0f);
        add (p);
    }
    else if (s.flatTopRuns > 20 && s.secondsAnalysed > 5)
    {
        Problem p;
        p.id = "flattops"; p.category = ru ("Искажения"); p.module = Module::Maximizer;
        p.title = ru ("Следы клиппинга внутри микса");
        p.what = fmt ("{} участков с «плоскими» вершинами ниже пикового уровня.", s.flatTopRuns);
        p.why = ru ("Похоже, где-то раньше сигнал срезался, а потом громкость уменьшили. Это может быть и намеренный звук (клиппер, square-синт).");
        p.fix = ru ("Если клиппинг не задуман — проверь уровни на каналах и шине до мастера.");
        p.severity = 0.25f; p.measured = true;
        add (p);
    }
    if (s.dcOffsetDb > -50.0f) { /* reported together with rumble above */ }

    const float micro = s.microDynamicsDb;
    const bool squashed = s.plr < g.minPlr - 1.0f || micro < g.minMicroDyn - 1.0f;
    if (squashed)
    {
        Problem p;
        p.id = "overlimited"; p.category = ru ("Динамика"); p.module = Module::Maximizer;
        p.title = ru ("Мастер пересжат");
        p.what = fmt ("PLR {} дБ и микродинамика {} дБ — ниже типичного для стиля «{}» (≥{} / ≥{} дБ).",
                      num (s.plr), num (micro), g.name, num (g.minPlr, 0), num (g.minMicroDyn, 0));
        p.why = ru ("Когда пики раздавлены, удары теряют атаку, а громкость перестаёт расти — звук становится плоским и утомительным. Стриминги всё равно убавят громкость, и сжатый трек проиграет.");
        p.fix = ru ("Убавь драйв в Гравитации на 1–3 дБ или отдай часть работы клипперу. Проверь «Честным сравнением», что стало хуже, а не просто тише.");
        p.severity = juce::jlimit (0.2f, 1.0f, std::max ((g.minPlr - s.plr) / 4.0f, (g.minMicroDyn - micro) / 4.0f) + 0.2f);
        add (p);
    }

    // loudness recommendation
    {
        float target = g.lufsTarget;
        juce::String reason = fmt ("Типично для стиля «{}».", g.name);
        if (micro > g.minMicroDyn + 4.0f && r.genreProfile != 6)
        {
            target -= juce::jlimit (0.0f, 3.0f, (micro - (g.minMicroDyn + 4.0f)) * 0.5f);   // dynamic material: aim lower
            reason = fmt ("Трек довольно динамичный (микродинамика {} дБ): громче ≈{} LUFS лимитер начнёт заметно давить атаку. Для стиля «{}» типично {} LUFS.",
                          num (micro), num (target, 1), g.name, num (g.lufsTarget, 1));
        }
        if (hasRef && ctx.reference->integrated > -40.0f)
        {
            const float refI = ctx.reference->integrated;
            if (std::abs (refI - target) > 1.0f)
                reason += fmt (" Эталон звучит на {} LUFS — учитывай, но не гонись за цифрой.", num (refI, 1));
        }
        r.recTargetLufs = std::round (target * 2.0f) / 2.0f;
        r.targetReason = reason;

        if (s.integrated < r.recTargetLufs - 3.0f && ! squashed)
        {
            Problem p;
            p.id = "quiet"; p.category = ru ("Громкость"); p.module = Module::Maximizer;
            p.title = ru ("Тише ориентира");
            p.what = fmt ("Интегральная громкость {} LUFS, ориентир для стиля — около {} LUFS.", num (s.integrated, 1), num (r.recTargetLufs, 1));
            p.why = ru ("Это не ошибка, а выбор. Но рядом с другими треками плейлиста твой может звучать слабее (если платформа не нормализует).");
            p.fix = ru ("Открой Гравитацию, нажми «Рассчитать» — я посчитаю нужный драйв по прослушанному материалу и покажу, сколько это будет стоить по пикам.");
            p.severity = juce::jlimit (0.1f, 0.45f, (r.recTargetLufs - s.integrated - 3.0f) / 8.0f + 0.1f);
            add (p);
        }
        else if (s.integrated > r.recTargetLufs + 2.5f)
        {
            Problem p;
            p.id = "loud"; p.category = ru ("Громкость"); p.module = Module::Maximizer;
            p.title = ru ("Громче, чем нужно стилю");
            p.what = fmt ("{} LUFS при ориентире около {} LUFS.", num (s.integrated, 1), num (r.recTargetLufs, 1));
            p.why = ru ("Лишние децибелы громкости почти всегда оплачены атакой и чистотой. Spotify и YouTube всё равно убавят трек до ~−14 LUFS.");
            p.fix = ru ("Если это не осознанный звук — убавь драйв лимитера и сравни на одинаковой громкости.");
            p.severity = juce::jlimit (0.15f, 0.6f, (s.integrated - r.recTargetLufs - 2.5f) / 6.0f + 0.15f);
            add (p);
        }
    }

    if (s.lra > 14.0f && r.genreProfile != 6)
    {
        Problem p;
        p.id = "lra"; p.category = ru ("Динамика"); p.module = Module::Maximizer;
        p.title = ru ("Большие перепады громкости между частями");
        p.what = fmt ("Диапазон громкости (LRA) {} LU.", num (s.lra, 1));
        p.why = ru ("Тихие куски могут потеряться в машине или в наушниках на улице. Для кино и акустики это нормально, для клубной и поп-музыки — редкость.");
        p.fix = ru ("Если так не задумано — выровняй части автоматизацией громкости до мастера, а не компрессором на мастере.");
        p.severity = juce::jlimit (0.1f, 0.5f, (s.lra - 14.0f) / 10.0f + 0.1f);
        add (p);
    }

    // --- stereo & phase --------------------------------------------------------------
    std::array<float, 4> recMeasuredTarget = s.bandWidthPct;
    {
        const float lowW = s.bandWidthPct[0], lowC = s.bandCorr[0];
        const float lowEnergyOk = rel[(size_t) 10] > -40.0f;  // ~ 63 Hz band carries something
        if (lowEnergyOk && (lowW > 30.0f || lowC < 0.6f))
        {
            Problem p;
            p.id = "low_stereo"; p.category = ru ("Стерео"); p.module = Module::Stereo;
            p.title = ru ("Бас не в моно");
            p.what = fmt ("Ширина ниже {} — {}%, корреляция {}.", hz (s.xovers[0]), juce::roundToInt (lowW), num (lowC, 2));
            p.why = ru ("Широкий низ теряет энергию в моно (клубы, колонки, телефоны), «плавает» в наушниках и мешает лимитеру.");
            p.fix = ru ("Сузь нижнюю полосу в Приливах до 0–20%. Я подготовил значение.");
            p.severity = juce::jlimit (0.3f, 1.0f, (lowW - 20.0f) / 60.0f + (0.8f - lowC) * 0.8f);
            add (p);
            recMeasuredTarget[0] = 8.0f;
        }

        const float ranges[4][2] = { { 0, 20 }, { 20, 75 }, { 35, 105 }, { 45, 130 } };
        for (int b = 1; b < 4; ++b)
        {
            const float w = s.bandWidthPct[(size_t) b];
            if (w < ranges[b][0]) recMeasuredTarget[(size_t) b] = ranges[b][0] + 10.0f;
            else if (w > ranges[b][1]) recMeasuredTarget[(size_t) b] = ranges[b][1] - 10.0f;
        }

        if (s.widthPct < 8.0f)
        {
            Problem p;
            p.id = "mono_mix"; p.category = ru ("Стерео"); p.module = Module::Stereo;
            p.title = ru ("Микс почти моно");
            p.what = fmt ("Общая ширина {}% — левый и правый каналы почти одинаковые.", juce::roundToInt (s.widthPct));
            p.why = ru ("Расширить мастер, в котором нет стерео-информации, нельзя: расширитель усиливает разницу каналов, а её нет.");
            p.fix = ru ("Ширину нужно создавать в миксе: панорама, дабл-треки, стерео-эффекты. На мастере я её не нарисую.");
            p.severity = 0.45f;
            add (p);
            for (auto& t : recMeasuredTarget) t = std::min (t, std::max (s.widthPct, 1.0f));
        }
        else if (s.bandWidthPct[3] < 30.0f && s.bandWidthPct[3] >= 4.0f)
        {
            Problem p;
            p.id = "narrow_top"; p.category = ru ("Стерео"); p.module = Module::Stereo;
            p.title = ru ("Узкий верх");
            p.what = fmt ("Ширина выше {} — {}%.", hz (s.xovers[2]), juce::roundToInt (s.bandWidthPct[3]));
            p.why = ru ("Широкий верх даёт ощущение простора, не трогая моно-совместимость низа.");
            p.fix = ru ("Расширь верхнюю полосу в Приливах на 20–30% и проверь в моно.");
            p.severity = 0.25f;
            add (p);
        }

        if (s.widthPct > 140.0f || s.bandCorr[3] < -0.1f || s.bandCorr[2] < -0.1f)
        {
            Problem p;
            p.id = "too_wide"; p.category = ru ("Стерео"); p.module = Module::Stereo;
            p.title = ru ("Слишком широко");
            p.what = fmt ("Ширина {}%, корреляция в верхних полосах {} / {}.", juce::roundToInt (s.widthPct), num (s.bandCorr[2], 2), num (s.bandCorr[3], 2));
            p.why = ru ("Сверхширокое стерео разваливается в моно и звучит «вне головы» — середина проседает.");
            p.fix = ru ("Сузь верхние полосы в Приливах до рекомендованных значений.");
            p.severity = juce::jlimit (0.3f, 0.9f, (s.widthPct - 140.0f) / 80.0f + 0.3f);
            add (p);
        }

        if (s.correlation < 0.15f || s.negativeCorrFraction > 0.12f)
        {
            Problem p;
            p.id = "phase"; p.category = ru ("Фаза"); p.module = Module::Stereo;
            p.title = ru ("Фазовые проблемы");
            p.what = fmt ("Средняя корреляция {}, отрицательная корреляция в {}% времени, минимум {}.", num (s.correlation, 2),
                          juce::roundToInt (s.negativeCorrFraction * 100.0f), num (s.worstCorrelation, 2));
            p.why = ru ("Противофаза между каналами — части микса исчезают при сложении в моно: на телефоне, в клубе, в Bluetooth-колонке.");
            p.fix = ru ("Включи «Моно» в верхней панели и послушай, что пропадает. Чаще всего виноваты стерео-расширители, хорусы и инвертированная фаза на дабл-треке.");
            p.severity = juce::jlimit (0.4f, 1.0f, (0.15f - s.correlation) * 2.0f + s.negativeCorrFraction * 2.0f + 0.4f);
            add (p);
        }
        if (s.monoLossDb < -4.5f && s.correlation >= 0.15f)
        {
            Problem p;
            p.id = "mono_loss"; p.category = ru ("Фаза"); p.module = Module::Stereo;
            p.title = ru ("Заметная потеря в моно");
            p.what = fmt ("При сложении в моно микс теряет около {} дБ.", num (-s.monoLossDb, 1));
            p.why = ru ("Для обычного стерео-микса потеря 1.5–3 дБ нормальна, больше — признак лишней ширины или фазовых эффектов.");
            p.fix = ru ("Проверь в моно, сузь самые широкие полосы.");
            p.severity = juce::jlimit (0.2f, 0.7f, (-s.monoLossDb - 4.5f) / 4.0f + 0.2f);
            add (p);
        }

        if (std::abs (s.balanceDb) > 0.8f)
        {
            Problem p;
            p.id = "balance"; p.category = ru ("Стерео"); p.module = Module::Stereo;
            p.title = s.balanceDb > 0 ? ru ("Правый канал громче левого") : ru ("Левый канал громче правого");
            p.what = fmt ("Разница каналов {} дБ.", num (std::abs (s.balanceDb), 1));
            p.why = ru ("Микс смещается в сторону — особенно заметно в наушниках.");
            p.fix = ru ("Проверь панораму главных элементов. Для быстрого исправления — баланс в Приливах (я подготовил значение).");
            p.severity = juce::jlimit (0.2f, 0.8f, (std::abs (s.balanceDb) - 0.8f) / 2.0f + 0.2f);
            add (p);
            r.recBalanceDb = juce::jlimit (-3.0f, 3.0f, ctx.currentBalanceParam - s.balanceDb);
        }
        else
            r.recBalanceDb = ctx.currentBalanceParam;
    }
    for (size_t b = 0; b < 4; ++b)
    {
        const float measured = s.bandWidthPct[b], cur = ctx.imagerOn ? ctx.currentWidthParams[b] : 100.0f;
        if (std::abs (recMeasuredTarget[b] - measured) < 1.0f || measured < 1.0f) { r.recWidthPct[b] = cur; continue; }
        r.recWidthPct[b] = std::round (juce::jlimit (0.0f, 180.0f, cur * recMeasuredTarget[b] / measured));
    }

    // --- space / character estimates ------------------------------------------------------
    if (s.bandWidthPct[1] > 85.0f && regionDev[2] > 0.0f)
    {
        Problem p;
        p.id = "muddy_space"; p.category = ru ("Пространство"); p.module = Module::Eq; p.measured = false;
        p.title = ru ("Размытое пространство в низкой середине");
        p.what = fmt ("В полосе {}–{} много стерео-информации (ширина {}%).", hz (s.xovers[0]), hz (s.xovers[1]), juce::roundToInt (s.bandWidthPct[1]));
        p.why = ru ("Обычно это длинные ревербераторы и широкие пэды — они размывают середину и забирают место у вокала. Это оценка, не измерение реверберации.");
        p.fix = ru ("Попробуй срез Side-канала около 300 Гц (−2 дБ, режим «Side» в Кратере частот) или укороти/отфильтруй ревер в миксе.");
        p.freqLo = 200; p.freqHi = 600;
        p.severity = 0.3f;
        add (p);
        if (r.eq.size() < 7)
        {
            EqSuggestion e; e.type = (int) FilterType::Bell; e.freq = 300; e.q = 0.8f; e.gainDb = -2.0f;
            e.channel = 2; e.reason = ru ("Side: размытая низкая середина"); r.eq.push_back (e);
        }
    }
    if (s.bandWidthPct[2] < 20.0f && s.bandWidthPct[3] < 30.0f && s.widthPct >= 8.0f)
    {
        Problem p;
        p.id = "flat_space"; p.category = ru ("Пространство"); p.module = Module::Stereo; p.measured = false;
        p.title = ru ("Плоская сцена");
        p.what = ru ("В середине и верху мало стерео-информации.");
        p.why = ru ("Микс может звучать «в одной точке», без глубины. Это субъективная оценка по ширине полос.");
        p.fix = ru ("Аккуратно расширь верхние полосы (+15–25%) или добавь пространства в миксе.");
        p.severity = 0.2f;
        add (p);
    }

    const bool harsh = regionDev[4] > tol * 0.8f;
    if (harsh && micro < g.minMicroDyn + 0.5f)
    {
        Problem p;
        p.id = "harsh_dense"; p.category = ru ("Насыщение"); p.module = Module::Eq; p.measured = false;
        p.title = ru ("Возможно, перебор с насыщением");
        p.what = fmt ("Яркая верхняя середина (+{} дБ) вместе с плотной динамикой ({} дБ).", num (regionDev[4]), num (micro));
        p.why = ru ("Так часто звучат перегруженные сатураторы и клипперы: гармоники копятся в 2–6 кГц. По спектру нельзя точно сказать, какой прибор виноват — это оценка.");
        p.fix = ru ("Сравни с обходом сатураторов на шине. На мастере — мягкий вырез 3–5 кГц на 1–2 дБ.");
        p.freqLo = 2000; p.freqHi = 6000;
        p.severity = 0.35f;
        add (p);
    }
    if ((r.genreProfile == 1 || r.genreProfile == 2) && micro > g.minMicroDyn + 5.0f && regionDev[4] < -tol * 0.5f)
    {
        Problem p;
        p.id = "too_clean"; p.category = ru ("Насыщение"); p.module = Module::Maximizer; p.measured = false;
        p.title = ru ("Слишком чисто для такого стиля");
        p.what = fmt ("Большая микродинамика ({} дБ) и спокойная верхняя середина.", num (micro));
        p.why = ru ("Агрессивным стилям обычно помогает немного гармоник — звук становится плотнее и громче на слух без лишнего лимитирования.");
        p.fix = ru ("Попробуй клиппер в Гравитации на 20–35%: он срезает пики мягко и с передискретизацией.");
        p.severity = 0.2f;
        add (p);
    }

    // --- translation --------------------------------------------------------------------
    if (s.phoneLossDb < -8.0f)
    {
        Problem p;
        p.id = "phone"; p.category = ru ("Перенос"); p.module = Module::Eq; p.measured = false;
        p.title = ru ("На телефоне трек «похудеет»");
        p.what = fmt ("Оценка: на маленьком динамике пропадёт около {} дБ громкости — {}% энергии ниже 150 Гц.",
                      num (-s.phoneLossDb, 0), juce::roundToInt (s.lowEndShare * 100.0f));
        p.why = ru ("Телефоны и ноутбуки почти не воспроизводят саб. Если бас живёт только там, на них он исчезает.");
        p.fix = ru ("Дай басу гармоники в 100–300 Гц (сатурация на басу/808 в миксе), чтобы его было слышно и без саба.");
        p.freqLo = 30; p.freqHi = 150;
        p.severity = juce::jlimit (0.15f, 0.5f, (-s.phoneLossDb - 8.0f) / 8.0f + 0.15f);
        add (p);
    }

    std::sort (P.begin(), P.end(), [] (const Problem& a, const Problem& b) { return a.severity > b.severity; });

    // --- scoring -------------------------------------------------------------------------
    const int tonal = scoreFrom (tonalRms, 0.6f, 9.0f);

    int dyn = 100;
    dyn = std::min (dyn, scoreFrom (micro, g.minMicroDyn + 1.0f, g.minMicroDyn - 4.0f));
    dyn = std::min (dyn, scoreFrom (s.plr, g.minPlr + 1.0f, g.minPlr - 4.0f));
    if (s.lra > 14.0f && r.genreProfile != 6) dyn -= juce::roundToInt ((s.lra - 14.0f) * 2.0f);
    dyn = juce::jlimit (0, 100, dyn);

    int peaks = 100;
    if (s.truePeakDb > -0.1f) peaks -= juce::roundToInt (25.0f + (s.truePeakDb + 0.1f) * 12.0f + std::min (25.0f, s.truePeakOvers * 0.2f));
    peaks -= juce::roundToInt (std::min (40.0f, s.clipEventsPerMinute * 3.0f));
    if (s.dcOffsetDb > -50.0f) peaks -= 10;
    const float loudDist = std::abs (s.integrated - r.recTargetLufs);
    peaks -= juce::roundToInt (std::max (0.0f, loudDist - 2.0f) * 3.0f);
    peaks = juce::jlimit (0, 100, peaks);

    int stereo = 100;
    {
        const float ranges[4][2] = { { 0, 25 }, { 15, 85 }, { 30, 115 }, { 40, 140 } };
        for (int b = 0; b < 4; ++b)
        {
            const float w = s.bandWidthPct[(size_t) b];
            const float out = w < ranges[b][0] ? ranges[b][0] - w : (w > ranges[b][1] ? w - ranges[b][1] : 0.0f);
            stereo -= juce::roundToInt (out * (b == 0 ? 1.0f : 0.5f));
        }
        if (s.widthPct < 8.0f) stereo -= 25;
        stereo -= juce::roundToInt (std::max (0.0f, std::abs (s.balanceDb) - 0.5f) * 12.0f);
        stereo = juce::jlimit (0, 100, stereo);
    }

    int phase = 100;
    phase -= juce::roundToInt (std::max (0.0f, 0.5f - s.correlation) * 80.0f);
    phase -= juce::roundToInt (s.negativeCorrFraction * 150.0f);
    phase -= juce::roundToInt (std::max (0.0f, 0.75f - s.bandCorr[0]) * 80.0f);
    phase -= juce::roundToInt (std::max (0.0f, -s.monoLossDb - 3.5f) * 8.0f);
    phase = juce::jlimit (0, 100, phase);

    int clean = 100;
    clean -= juce::roundToInt (std::min (60.0f, s.clipEventsPerMinute * 4.0f));
    if (s.flatTopRuns > 20) clean -= 8;
    if (harsh) clean -= juce::roundToInt (std::min (25.0f, regionDev[4] * 5.0f));
    if (squashed) clean -= 10;
    clean = juce::jlimit (0, 100, clean);

    r.subscores = {
        { ru ("Тональный баланс"), tonal, 0.25f, fmt ("Отклонение от ориентира: {} дБ (СКО)", num (tonalRms, 1)) },
        { ru ("Динамика"),         dyn,   0.17f, fmt ("PLR {} дБ, микродинамика {} дБ", num (s.plr), num (micro)) },
        { ru ("Пики и громкость"), peaks, 0.18f, fmt ("{} LUFS, {} дБTP", num (s.integrated, 1), num (s.truePeakDb, 1)) },
        { ru ("Стерео-образ"),     stereo, 0.15f, fmt ("Ширина {}%, баланс {} дБ", juce::roundToInt (s.widthPct), signedNum (s.balanceDb, 1)) },
        { ru ("Фаза и моно"),      phase, 0.13f, fmt ("Корреляция {}, низ {}", num (s.correlation, 2), num (s.bandCorr[0], 2)) },
        { ru ("Чистота"),          clean, 0.12f, fmt ("Клип-событий: {}", s.clipEvents) },
    };
    float total = 0, wsumScore = 0;
    for (auto& sc : r.subscores) { total += sc.value * sc.weight; wsumScore += sc.weight; }
    int score = juce::roundToInt (total / wsumScore);
    const float worst = P.empty() ? 0.0f : P.front().severity;
    if (worst >= 0.85f) score = std::min (score, 72);
    else if (worst >= 0.6f) score = std::min (score, 86);
    if (s.clipEventsPerMinute > 10.0f) score = std::min (score, 60);
    r.score = juce::jlimit (1, 100, score);

    if (r.score >= 92)      r.verdict = ru ("Это уже мастер. Я… кажется, у меня взорвалась голова.");
    else if (r.score >= 82) r.verdict = ru ("Очень достойно. Пара штрихов — и будет огонь.");
    else if (r.score >= 68) r.verdict = ru ("Ну, пойдёт… но можно было гораздо лучше.");
    else if (r.score >= 50) r.verdict = ru ("Хм. Тут есть над чем поработать. Серьёзно.");
    else                    r.verdict = ru ("Что. Это. Было?! Давай чинить, пока я не улетел с этой луны.");
    return r;
}
} // namespace mooni::diagnostics
