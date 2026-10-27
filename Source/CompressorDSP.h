#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace aurora
{
//==============================================================================
// Статическая кривая: возвращает величину снижения уровня в dB (>= 0).
inline float computeReductionDb (float levelDb, float thresholdDb, float ratio, float kneeDb)
{
    const float over  = levelDb - thresholdDb;
    const float w     = std::max (kneeDb, 0.0f);
    const float slope = 1.0f - 1.0f / std::max (ratio, 1.0f);

    if (over <= -0.5f * w) return 0.0f;
    if (over >=  0.5f * w) return slope * over;

    const float t = over + 0.5f * w;
    return slope * t * t / (2.0f * w);
}

//==============================================================================
// Биквад 12 дБ/окт для фильтра сайдчейна (без аллокаций).
struct Biquad
{
    float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;

    void setHighPass (double sr, float freq)
    {
        const double w0    = 2.0 * juce::MathConstants<double>::pi * freq / sr;
        const double cosw  = std::cos (w0);
        const double alpha = std::sin (w0) / (2.0 * 0.70710678);
        const double a0    = 1.0 + alpha;
        b0 = (float) (((1.0 + cosw) * 0.5) / a0);
        b1 = (float) (-(1.0 + cosw) / a0);
        b2 = b0;
        a1 = (float) (-2.0 * cosw / a0);
        a2 = (float) ((1.0 - alpha) / a0);
    }

    float process (float x) noexcept
    {
        const float y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return y;
    }

    void reset() noexcept { z1 = z2 = 0.0f; }
};

//==============================================================================
class CompressorDSP
{
public:
    static constexpr float maxLookaheadMs = 10.0f;
    static constexpr int   maxOversample  = 4;

    struct Params
    {
        float thresholdDb = -18.0f;
        float ratio       = 4.0f;
        float attackMs    = 10.0f;
        float releaseMs   = 120.0f;
        float kneeDb      = 6.0f;
        float makeupDb    = 0.0f;
        float mix         = 1.0f;     // 0..1 (параллельная компрессия)
        float scHpfHz     = 20.0f;    // <= 20.5 Гц = выключен
        bool  rms         = false;
        bool  autoMakeup  = false;
        int   lookaheadSamples = 0;   // в текущей (оверсэмплированной) частоте
    };

    // hostSampleRate — частота хоста; линия задержки считается на максимальный оверсэмплинг.
    void prepare (double hostSampleRate)
    {
        sr = hostSampleRate;
        const int maxL = (int) std::ceil (0.001 * (double) maxLookaheadMs * hostSampleRate * maxOversample) + 4;
        const int size = juce::nextPowerOfTwo (maxL);
        for (auto& d : dl) d.assign ((size_t) size, 0.0f);
        dlMask = size - 1;
        mix.reset (hostSampleRate * maxOversample, 0.02);
        reset();
    }

    // Меняет рабочую частоту (при смене оверсэмплинга). Без аллокаций.
    void setSampleRate (double newRate)
    {
        sr = newRate;
        mix.reset (newRate, 0.02);
        lastHpf = -1.0f;
    }

    void reset()
    {
        for (auto& f : hp) f.reset();
        for (auto& d : dl) std::fill (d.begin(), d.end(), 0.0f);
        wp = 0;
        curL = 0;
        grState = 0.0f;
        ms = 0.0f;
        lastHpf = -1.0f;
        needsMixInit = true;
    }

    // Обрабатывает буфер на месте. key — внешний сайдчейн (nullptr = внутренний).
    // Возвращает максимальное снижение уровня за блок (dB, >= 0).
    float process (juce::AudioBuffer<float>& buf, const juce::AudioBuffer<float>* key, const Params& p)
    {
        const int n   = buf.getNumSamples();
        const int nch = std::min (buf.getNumChannels(), 2);
        if (n == 0 || nch == 0) return 0.0f;

        const bool ext = key != nullptr && key->getNumChannels() > 0 && key->getNumSamples() >= n;

        float* w[2] = { nullptr, nullptr };
        const float* k[2] = { nullptr, nullptr };
        for (int c = 0; c < nch; ++c)
        {
            w[c] = buf.getWritePointer (c);
            if (ext) k[c] = key->getReadPointer (std::min (c, key->getNumChannels() - 1));
        }

        const bool useHpf = p.scHpfHz > 20.5f;
        if (useHpf && p.scHpfHz != lastHpf)
        {
            for (auto& f : hp) f.setHighPass (sr, p.scHpfHz);
            lastHpf = p.scHpfHz;
        }

        const int L = juce::jlimit (0, dlMask, p.lookaheadSamples);
        if (L != curL)
        {
            curL = L;
            for (auto& d : dl) std::fill (d.begin(), d.end(), 0.0f);
        }

        const float fs      = (float) sr;
        const float atk     = std::exp (-1.0f / (0.001f * std::max (p.attackMs,  0.01f) * fs));
        const float rel     = std::exp (-1.0f / (0.001f * std::max (p.releaseMs, 1.0f)  * fs));
        const float rmsCoef = std::exp (-1.0f / (0.010f * fs));   // окно RMS ~10 мс
        const float makeup  = p.makeupDb + (p.autoMakeup ? -0.5f * p.thresholdDb * (1.0f - 1.0f / p.ratio) : 0.0f);

        if (needsMixInit) { mix.setCurrentAndTargetValue (p.mix); needsMixInit = false; }
        mix.setTargetValue (p.mix);

        float maxGr = 0.0f;

        for (int i = 0; i < n; ++i)
        {
            float xin[2] = { 0.0f, 0.0f };
            for (int c = 0; c < nch; ++c) xin[c] = w[c][i];

            // --- детектор (стерео-линк); видит сигнал БЕЗ задержки ---
            float peak = 0.0f, sq = 0.0f;
            for (int c = 0; c < nch; ++c)
            {
                float s = ext ? k[c][i] : xin[c];
                if (useHpf) s = hp[(size_t) c].process (s);
                peak = std::max (peak, std::abs (s));
                sq  += s * s;
            }

            float keyLevel = peak;
            if (p.rms)
            {
                ms = rmsCoef * ms + (1.0f - rmsCoef) * (sq / (float) nch);
                keyLevel = std::sqrt (ms);
            }

            const float levelDb = juce::Decibels::gainToDecibels (keyLevel, -120.0f);
            const float target  = computeReductionDb (levelDb, p.thresholdDb, p.ratio, p.kneeDb);

            // --- сглаживание attack/release в dB ---
            const float coef = target > grState ? atk : rel;
            grState = target + coef * (grState - target);
            maxGr = std::max (maxGr, grState);

            const float gain = juce::Decibels::decibelsToGain (makeup - grState);
            const float m    = mix.getNextValue();
            const float g    = 1.0f + m * (gain - 1.0f);   // dry*(1-m) + dry*gain*m

            // --- звук идёт через линию задержки (lookahead) ---
            for (int c = 0; c < nch; ++c)
            {
                auto& line = dl[(size_t) c];
                line[(size_t) wp] = xin[c];
                w[c][i] = line[(size_t) ((wp + dlMask + 1 - curL) & dlMask)] * g;
            }
            wp = (wp + 1) & dlMask;
        }

        return maxGr;
    }

private:
    double sr = 44100.0;
    std::array<Biquad, 2> hp;
    std::array<std::vector<float>, 2> dl { std::vector<float> (1, 0.0f), std::vector<float> (1, 0.0f) };
    int dlMask = 0, wp = 0, curL = 0;
    juce::SmoothedValue<float> mix { 1.0f };
    float grState = 0.0f, ms = 0.0f, lastHpf = -1.0f;
    bool needsMixInit = true;
};

} // namespace aurora
