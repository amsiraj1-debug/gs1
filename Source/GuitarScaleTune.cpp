// Guitar Scale Tune: monophonic pitch detection (YIN) + scale snapping + pitch shifting.
#include <juce_audio_processors/juce_audio_processors.h>
#include <cmath>
#include <vector>
using namespace juce;

namespace
{
struct Scale { const char* name; std::vector<int> steps; };

const std::vector<Scale>& scales()
{
    static const std::vector<Scale> s = {
        { "Chromatic", {0,1,2,3,4,5,6,7,8,9,10,11} },
        { "Major (Ionian)", {0,2,4,5,7,9,11} },
        { "Natural Minor (Aeolian)", {0,2,3,5,7,8,10} },
        { "Major Pentatonic", {0,2,4,7,9} },
        { "Minor Pentatonic", {0,3,5,7,10} },
        { "Blues (Minor)", {0,3,5,6,7,10} },
        { "Blues (Major)", {0,2,3,4,7,9} },
        { "Dorian", {0,2,3,5,7,9,10} },
        { "Phrygian", {0,1,3,5,7,8,10} },
        { "Lydian", {0,2,4,6,7,9,11} },
        { "Mixolydian", {0,2,4,5,7,9,10} },
        { "Locrian", {0,1,3,5,6,8,10} },
        { "Harmonic Minor", {0,2,3,5,7,8,11} },
        { "Melodic Minor", {0,2,3,5,7,9,11} },
        { "Phrygian Dominant", {0,1,4,5,7,8,10} },
        { "Hungarian Minor", {0,2,3,6,7,8,11} },
        { "Double Harmonic", {0,1,4,5,7,8,11} },
        { "Lydian Dominant", {0,2,4,6,7,9,10} },
        { "Altered", {0,1,3,4,6,8,10} },
        { "Whole Tone", {0,2,4,6,8,10} },
        { "Diminished (W-H)", {0,2,3,5,6,8,9,11} },
        { "Diminished (H-W)", {0,1,3,4,6,7,9,10} },
        { "Hirajoshi", {0,2,3,7,8} } };
    return s;
}

const char* const kNotes[12] = { "C","C#","D","D#","E","F","F#","G","G#","A","A#","B" };

// YIN pitch detector, early-exits at the first clear dip so it stays cheap.
class Yin
{
public:
    void prepare (double fs)
    {
        sr = fs; tauMax = (int) (fs / 70.0); W = tauMax;           // lowest note ~70 Hz
        ring.assign ((size_t) (W + tauMax + 1), 0.f); lin = ring;
        dn.assign ((size_t) tauMax + 2, 1.f); pos = 0;
    }
    void push (float x) { ring[(size_t) pos] = x; pos = (pos + 1) % (int) ring.size(); }

    float detect()                                                  // Hz, or 0 if no clear pitch
    {
        const int n = (int) ring.size();
        for (int i = 0; i < n; ++i) lin[(size_t) i] = ring[(size_t) ((pos + i) % n)];
        const float* x = lin.data() + n - 1;                        // x[-k] = k samples ago
        float e = 0; for (int j = 0; j < W; ++j) e += x[-j] * x[-j];
        if (e / (float) W < 1e-6f) return 0.f;                      // below about -60 dB

        float run = 0;
        auto step = [&] (int t)
        {
            float d = 0;
            for (int j = 0; j < W; ++j) { float v = x[-j] - x[-j - t]; d += v * v; }
            run += d;
            return dn[(size_t) t] = run > 0 ? d * (float) t / run : 1.f;
        };
        step (1);
        for (int t = 2; t < tauMax; ++t)
            if (step (t) < 0.15f)
            {
                while (t + 1 < tauMax && step (t + 1) < dn[(size_t) t]) ++t;
                float tf = (float) t;
                if (t + 1 < tauMax)                                  // parabolic refinement
                {
                    float a = dn[(size_t) t - 1], b = dn[(size_t) t], c = dn[(size_t) t + 1];
                    float den = a - 2 * b + c;
                    if (den != 0) tf += 0.5f * (a - c) / den;
                }
                float hz = (float) sr / tf;
                return hz < 1500.f ? hz : 0.f;
            }
        return 0.f;
    }
private:
    double sr = 48000; int tauMax = 600, W = 600, pos = 0;
    std::vector<float> ring, lin, dn;
};

// Two crossfading delay taps give a simple real-time pitch shifter.
struct Shifter
{
    std::vector<float> buf; int w = 0, n = 1; float phase = 0, win = 1;
    void prepare (int winSamples) { win = (float) winSamples; n = winSamples * 2 + 8; buf.assign ((size_t) n, 0.f); w = 0; phase = 0; }
    float tap (float delay) const
    {
        float r = (float) w - delay; if (r < 0) r += (float) n;
        int i0 = (int) r; float f = r - (float) i0;
        return buf[(size_t) i0] * (1 - f) + buf[(size_t) ((i0 + 1) % n)] * f;
    }
    void process (float in, float ratio, float& wet, float& dryDelayed)
    {
        buf[(size_t) w] = in;
        phase += (1.f - ratio) / win; phase -= std::floor (phase);
        float p2 = phase + 0.5f; p2 -= std::floor (p2);
        auto g = [] (float p) { return 0.5f - 0.5f * std::cos (MathConstants<float>::twoPi * p); };
        wet = tap (phase * win) * g (phase) + tap (p2 * win) * g (p2);
        dryDelayed = tap (win * 0.5f);                              // latency-matched dry signal
        w = (w + 1) % n;
    }
};
} // namespace

class GuitarScaleTune : public AudioProcessor
{
public:
    GuitarScaleTune()
        : AudioProcessor (BusesProperties().withInput ("In", AudioChannelSet::stereo(), true)
                                           .withOutput ("Out", AudioChannelSet::stereo(), true)),
          apvts (*this, nullptr, "STATE", layout()) {}

    static AudioProcessorValueTreeState::ParameterLayout layout()
    {
        AudioProcessorValueTreeState::ParameterLayout l;
        StringArray keys, names;
        for (auto* k : kNotes) keys.add (k);
        for (auto& s : scales()) names.add (s.name);
        l.add (std::make_unique<AudioParameterChoice> (ParameterID { "key", 1 }, "Key", keys, 9));
        l.add (std::make_unique<AudioParameterChoice> (ParameterID { "scale", 1 }, "Scale", names, 4));
        l.add (std::make_unique<AudioParameterFloat> (ParameterID { "speed", 1 }, "Retune Speed (ms)",
                                                      NormalisableRange<float> (0.f, 300.f, 1.f, 0.5f), 20.f));
        l.add (std::make_unique<AudioParameterFloat> (ParameterID { "amount", 1 }, "Correction Amount (%)",
                                                      NormalisableRange<float> (0.f, 100.f, 1.f), 100.f));
        l.add (std::make_unique<AudioParameterFloat> (ParameterID { "mix", 1 }, "Mix (%)",
                                                      NormalisableRange<float> (0.f, 100.f, 1.f), 100.f));
        return l;
    }

    void prepareToPlay (double fs, int) override
    {
        sr = fs; yin.prepare (fs);
        const int win = (int) (0.030 * fs);                         // 30 ms shifter window
        for (auto& s : shift) s.prepare (win);
        hop = jmax (1, (int) (fs / 200.0)); hopCount = 0; offset = 0; target = -1;
        ratio.reset (fs, 0.005); ratio.setCurrentAndTargetValue (1.f);
        engage.reset (fs, 0.010); engage.setCurrentAndTargetValue (0.f);
        setLatencySamples (win / 2);
    }
    void releaseResources() override {}

    bool isBusesLayoutSupported (const BusesLayout& l) const override
    {
        auto o = l.getMainOutputChannelSet();
        return (o == AudioChannelSet::mono() || o == AudioChannelSet::stereo()) && o == l.getMainInputChannelSet();
    }

    void processBlock (AudioBuffer<float>& b, MidiBuffer&) override
    {
        ScopedNoDenormals noDenormals;
        const int nCh = jmin (b.getNumChannels(), 2), ns = b.getNumSamples();
        const int key = (int) *apvts.getRawParameterValue ("key");
        const int sc = (int) *apvts.getRawParameterValue ("scale");
        speedMs = *apvts.getRawParameterValue ("speed");
        amount = *apvts.getRawParameterValue ("amount") / 100.f;
        const float mix = *apvts.getRawParameterValue ("mix") / 100.f;
        for (int i = 0; i < 12; ++i) inScale[i] = false;
        for (int s : scales()[(size_t) sc].steps) inScale[(key + s) % 12] = true;

        for (int i = 0; i < ns; ++i)
        {
            float mono = 0;
            for (int c = 0; c < nCh; ++c) mono += b.getSample (c, i);
            yin.push (mono / (float) jmax (1, nCh));
            if (++hopCount >= hop) { hopCount = 0; analyse(); }

            const float r = ratio.getNextValue(), e = engage.getNextValue();
            for (int c = 0; c < nCh; ++c)
            {
                float wet, dry;
                shift[c].process (b.getSample (c, i), r, wet, dry);
                const float proc = dry + e * (wet - dry);           // bypass shifter when already in tune
                b.setSample (c, i, dry + mix * (proc - dry));
            }
        }
    }

    // Find the note being played, pick the nearest scale note, glide toward it.
    void analyse()
    {
        const float hz = yin.detect();
        float wanted = 0.f;
        if (hz > 0.f)
        {
            const float midi = 69.f + 12.f * std::log2 (hz / 440.f);
            int best = -1; float bd = 99.f;
            const int centre = (int) std::round (midi);
            for (int n = centre - 6; n <= centre + 6; ++n)
                if (inScale[((n % 12) + 12) % 12] && std::abs (midi - (float) n) < bd) { bd = std::abs (midi - (float) n); best = n; }
            if (target >= 0 && target != best && inScale[target % 12] && std::abs (midi - (float) target) < bd + 0.25f)
                best = target;                                      // hysteresis: don't flutter between notes
            target = best;
            wanted = ((float) best - midi) * amount;
        }
        else target = -1;

        const float dtMs = 1000.f * (float) hop / (float) sr;
        const float a = speedMs < 0.5f ? 1.f : 1.f - std::exp (-dtMs / speedMs);
        offset += a * (wanted - offset);
        ratio.setTargetValue (std::pow (2.f, offset / 12.f));
        engage.setTargetValue (std::abs (offset) > 0.03f ? 1.f : 0.f);
    }

    const String getName() const override { return "Guitar Scale Tune"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const String getProgramName (int) override { return {}; }
    void changeProgramName (int, const String&) override {}
    bool hasEditor() const override { return true; }
    AudioProcessorEditor* createEditor() override { return new GenericAudioProcessorEditor (*this); }
    void getStateInformation (MemoryBlock& d) override { if (auto x = apvts.copyState().createXml()) copyXmlToBinary (*x, d); }
    void setStateInformation (const void* d, int n) override { if (auto x = getXmlFromBinary (d, n)) apvts.replaceState (ValueTree::fromXml (*x)); }

private:
    AudioProcessorValueTreeState apvts;
    Yin yin; Shifter shift[2];
    SmoothedValue<float> ratio { 1.f }, engage { 0.f };
    double sr = 48000; int hop = 240, hopCount = 0, target = -1;
    float offset = 0, speedMs = 20, amount = 1; bool inScale[12] {};
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GuitarScaleTune)
};

AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new GuitarScaleTune(); }
