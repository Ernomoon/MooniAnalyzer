// Headless UI verification: runs real audio through the processor and renders every room to PNG.
#include "core/PluginProcessor.h"
#include "gui/PluginEditor.h"
#include <random>

using namespace mooni;

static void pump (int ms) { juce::MessageManager::getInstance()->runDispatchLoopUntil (ms); }

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;
    const juce::File outDir (argc > 1 ? juce::String (argv[1]) : juce::String ("/tmp/shots"));
    outDir.createDirectory();

    auto proc = std::make_unique<MooniProcessor>();
    proc->setPlayConfigDetails (2, 2, 48000, 512);
    proc->prepareToPlay (48000, 512);
    {
        auto s = proc->settings; s.mode = AnalysisMode::Smart; proc->setSettings (s);
    }
    std::unique_ptr<juce::AudioProcessorEditor> ed (proc->createEditor());
    auto* editor = dynamic_cast<MooniEditor*> (ed.get());
    ed->setSize (MooniEditor::kW, MooniEditor::kH);

    auto snap = [&] (const juce::String& name)
    {
        for (int i = 0; i < 6; ++i) { editor->forceTick (1.0f / 30.0f); pump (5); }
        auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, 1.0f);
        juce::PNGImageFormat png;
        auto f = outDir.getChildFile (name + ".png");
        f.deleteFile();
        juce::FileOutputStream os (f);
        png.writeImageToStream (img, os);
        std::printf ("wrote %s\n", f.getFullPathName().toRawUTF8());
    };

    snap ("00_empty");

    // a deliberately imperfect "trap-ish" mix: heavy 808, muddy low-mids, wide low end, harsh hats
    std::mt19937 rng (5);
    std::normal_distribution<float> nd (0, 1);
    const double sr = 48000;
    juce::AudioBuffer<float> buf (2, 512);
    juce::MidiBuffer midi;
    double t = 0, phase808 = 0, phaseMud = 0;
    float pinkL = 0, pinkR = 0;
    const int total = (int) (sr * 24 / 512);
    for (int b = 0; b < total; ++b)
    {
        for (int i = 0; i < 512; ++i, t += 1.0 / sr)
        {
            const double beat = std::fmod (t, 0.5);
            const double env = std::exp (-beat * 6.0);
            phase808 += 2 * juce::MathConstants<double>::pi * (48 + 30 * std::exp (-beat * 30)) / sr;
            phaseMud += 2 * juce::MathConstants<double>::pi * 300 / sr;
            const float kick = (float) (0.55 * env * std::sin (phase808));
            const float mud = (float) (0.06 * std::sin (phaseMud) * (0.6 + 0.4 * std::sin (t * 3)));
            pinkL = 0.97f * pinkL + 0.03f * nd (rng); pinkR = 0.97f * pinkR + 0.03f * nd (rng);
            const double hatEnv = std::exp (-std::fmod (t, 0.125) * 60.0);
            const float hatL = (float) (0.08 * hatEnv * nd (rng)), hatR = (float) (0.08 * hatEnv * nd (rng));
            const float wideLow = (float) (0.05 * std::sin (2 * juce::MathConstants<double>::pi * 70 * t));
            float l = kick + mud + pinkL * 0.5f + hatL + wideLow;
            float r = kick + mud + pinkR * 0.5f + hatR - wideLow;
            l = std::tanh (l * 1.4f); r = std::tanh (r * 1.4f);
            buf.setSample (0, i, l); buf.setSample (1, i, r);
        }
        proc->processBlock (buf, midi);
        pump (2); if (b % 4 == 0) editor->forceTick (0.01f);
        if (b == total / 3) snap ("01_listening");
    }
    // stop: silence so the analyser finalises
    for (int b = 0; b < (int) (sr * 3 / 512); ++b) { buf.clear(); proc->processBlock (buf, midi); pump (2); }
    pump (800);
    editor->forceTick (0.03f);
    for (int i = 0; i < 40; ++i) { editor->forceTick (1.0f / 30.0f); pump (10); }
    std::printf ("history entries: %d, latest score: %d\n", proc->history.size(), proc->engine.getLatestReport() ? proc->engine.getLatestReport()->score : -1);
    if (auto r = proc->engine.getLatestReport())
        for (auto& p : r->problems) std::printf ("  problem: %s  (%.2f)\n", p.title.toRawUTF8(), p.severity);
    snap ("02_observatory");

    const char* names[] = { "03_eq", "04_tides", "05_gravity", "06_reference", "07_logbook" };
    for (int room = 1; room <= 5; ++room)
    {
        editor->setRoom (room);
        for (int i = 0; i < 30; ++i) { editor->forceTick (1.0f / 30.0f); pump (5); }
        snap (names[room - 1]);
    }

    // apply recommendations and check the EQ room
    editor->setRoom (1);
    if (auto r = proc->engine.getLatestReport())
    {
        for (auto& e : r->eq) std::printf ("  eq suggestion: type %d  %.0f Hz  %+.1f dB  Q %.2f  ch %d  (%s)\n", e.type, e.freq, e.gainDb, e.q, e.channel, e.reason.toRawUTF8());
        proc->applyEqRecommendations (*r);
        auto bands = proc->currentEqBands();
        for (int i = 0; i < 8; ++i) if (bands[(size_t) i].enabled) std::printf ("  band %d: type %d  %.0f Hz  %+.1f dB  Q %.2f\n", i + 1, bands[(size_t) i].type, bands[(size_t) i].freq, bands[(size_t) i].gainDb, bands[(size_t) i].q);
    }
    for (int i = 0; i < 20; ++i) { editor->forceTick (1.0f / 30.0f); pump (5); }
    snap ("08_eq_applied");

    // eclipse (bypass)
    editor->setRoom (0);
    proc->setValue (params::bypass, 1.0f);
    for (int i = 0; i < 60; ++i) { editor->forceTick (1.0f / 30.0f); pump (5); }
    snap ("09_eclipse");
    proc->setValue (params::bypass, 0.0f);

    editor->showSettings (true); snap ("10_settings"); editor->showSettings (false);
    editor->showHelp (true); snap ("10b_help"); editor->showHelp (false);
    editor->showAbout (true); for (int i = 0; i < 10; ++i) { editor->forceTick (1.0f / 30.0f); pump (5); } snap ("11_about"); editor->showAbout (false);

    // explosion animation frames
    editor->getMooni().play ("explode", true);
    for (int i = 0; i < 40; ++i) { editor->forceTick (1.0f / 30.0f); pump (2); if (i == 20) snap ("12_explode_boom"); }

    // state round trip
    juce::MemoryBlock mb; proc->getStateInformation (mb);
    auto proc2 = std::make_unique<MooniProcessor>();
    proc2->setStateInformation (mb.getData(), (int) mb.getSize());
    std::printf ("state: %d bytes, restored history %d entries, eqOn %.0f\n", (int) mb.getSize(), proc2->history.size(), proc2->value (params::eqOn));

    ed.reset();
    proc->releaseResources();
    return 0;
}
