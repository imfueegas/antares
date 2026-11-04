// TunerCore.h - motor de corrección de tono en tiempo real (sin dependencias de JUCE).
// Detección de tono YIN + lógica de corrección + resíntesis PSOLA, todo en streaming.
// Uso: Engine e; e.prepare(sr); por cada muestra: out = e.process(in); latencia = e.latency() muestras.
#pragma once
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <limits>
#include <vector>

namespace entonador
{
struct Params
{
    float refA = 440.f;
    // Notas permitidas por clase de nota absoluta (0 = Do ... 11 = Si). Por defecto: Do mayor.
    bool mask[12] = { true, false, true, false, true, true, false, true, false, true, false, true };
    float retuneMs = 25.f;   // 0..400
    float humanize = 30.f;   // 0..100
    float vibAmt = 1.f;      // 0..2 (1 = vibrato natural)
    int cvShape = 0;         // 0 seno, 1 triangular, 2 cuadrada
    float cvRate = 5.5f;     // Hz
    float cvDepth = 0.f;     // cents
    float cvDelay = 0.3f;    // s
    float transpose = 0.f;   // semitonos
    float detune = 0.f;      // cents
    float formant = 0.f;     // semitonos (-6..6)
    float mix = 1.f;         // 0..1
};

class Engine
{
public:
    static constexpr int kRing = 1 << 15, kMask = kRing - 1;
    static constexpr int kFrames = 512, kMarks = 2048, kHist = 1024;

    void prepare (double sampleRate)
    {
        sr = sampleRate;
        D = std::max (1, (int) std::lround (sr / 11000.0));
        const double sr2 = sr / D;
        tauMax = (int) std::floor (sr2 / 80.0);
        tauMin = std::max (2, (int) std::floor (sr2 / 900.0));
        W = (int) std::lround (tauMax * 1.5);
        N = W + tauMax;
        hop = std::max (1, (int) std::lround (sr * 0.005));
        Cdelay = (N * D + D) / 2;
        const int maxP = (int) std::ceil (sr / 75.0);
        maxL = maxP + 8;
        HO = std::max (Cdelay, (int) std::ceil (1.95 * maxP) + 8);
        U = (int) std::lround (0.008 * sr);
        LAT = HO + maxL + hop + 32;
        in.assign (kRing, 0.f);
        outb.assign (kRing, 0.f);
        wsb.assign (kRing, 0.f);
        xs.assign ((size_t) N, 0.f);
        dd.assign ((size_t) tauMax + 2, 0.f);
        cm.assign ((size_t) tauMax + 2, 0.f);
        frames.assign (kFrames, Frame {});
        marks.assign (kMarks, Mark {});
        reset();
    }

    int latency() const { return LAT; }

    void reset()
    {
        std::fill (in.begin(), in.end(), 0.f);
        std::fill (outb.begin(), outb.end(), 0.f);
        std::fill (wsb.begin(), wsb.end(), 0.f);
        n = 0; nFr = 0; nMk = 0;
        nextPos = 0; runActive = false; pol = 1; lastVoicedMark = 0; lastMarkPos = -1;
        synthPos = 0.0; kPtr = 0;
        peakRms = 0.f; rawN = 0; streak = 0; hang = 0; lastF0 = 0.f;
        run = false; cNum = 0; cSum = 0.f; cIdx = 0; prevT = -1000; age = 0.f; shiftState = 0.f;
        histW.store (0);
        for (int i = 0; i < kHist; ++i) { histIn[i].store (nanf ("")); histOut[i].store (nanf ("")); histTgt[i].store (nanf ("")); }
    }

    void setParams (const Params& p) { P = p; }

    // Entra una muestra, sale la muestra corregida con retardo latency().
    float process (float x)
    {
        in[(size_t) (n & kMask)] = x;
        ++n;
        if (n % hop == 0) runFrame();
        const int64_t j = n - 1 - LAT;
        if (j < 0) return 0.f;
        const size_t k = (size_t) (j & kMask);
        const float wet = outb[k] / std::max (wsb[k], 0.2f);
        outb[k] = 0.f; wsb[k] = 0.f;
        const float dry = in[k];
        float y = dry * (1.f - P.mix) + wet * P.mix;
        return std::max (-1.f, std::min (1.f, y));
    }

    // Historial para la interfaz (cuadros de 5 ms): nota MIDI de entrada, salida y objetivo (NaN = sin voz).
    int readHistory (float* hin, float* hout, float* htgt, int count) const
    {
        const uint32_t w = histW.load (std::memory_order_acquire);
        count = std::min<int> (count, (int) std::min<uint32_t> (w, (uint32_t) kHist - 4));
        for (int i = 0; i < count; ++i)
        {
            const uint32_t idx = (w - (uint32_t) count + (uint32_t) i) % (uint32_t) kHist;
            hin[i] = histIn[idx].load (std::memory_order_relaxed);
            hout[i] = histOut[idx].load (std::memory_order_relaxed);
            htgt[i] = histTgt[idx].load (std::memory_order_relaxed);
        }
        return count;
    }
    double frameSeconds() const { return (double) hop / sr; }

private:
    struct Frame { int64_t pos = 0; float f0 = 0.f; float shift = 0.f; };
    struct Mark { int64_t pos = 0; float per = 0.f; uint8_t vo = 0; };

    double sr = 44100.0;
    int D = 4, tauMax = 137, tauMin = 12, W = 205, N = 342, hop = 220, Cdelay = 700, HO = 1100, maxL = 560, U = 352, LAT = 1900;
    std::vector<float> in, outb, wsb, xs, dd, cm;
    int64_t n = 0;
    Params P;

    std::vector<Frame> frames; int64_t nFr = 0;
    std::vector<Mark> marks; int64_t nMk = 0;
    int64_t nextPos = 0, lastVoicedMark = 0, lastMarkPos = -1;
    bool runActive = false; int pol = 1;
    double synthPos = 0.0; int64_t kPtr = 0;

    float peakRms = 0.f; float rawH[5] {}; int rawN = 0; int streak = 0; int hang = 0; float lastF0 = 0.f;
    bool run = false; float cr[16] {}; int cNum = 0, cIdx = 0; float cSum = 0.f;
    int prevT = -1000; float age = 0.f, shiftState = 0.f;

    std::array<std::atomic<float>, kHist> histIn, histOut, histTgt;
    std::atomic<uint32_t> histW { 0 };

    float inAt (int64_t i) const { return (i < 0 || i >= n) ? 0.f : in[(size_t) (i & kMask)]; }
    const Frame& frame (int64_t i) const { return frames[(size_t) (i % kFrames)]; }
    const Mark& mark (int64_t i) const { return marks[(size_t) (i & (kMarks - 1))]; }

    // ---------- detección YIN sobre la ventana decimada ----------
    float yin()
    {
        for (int tau = 1; tau <= tauMax; ++tau)
        {
            float s = 0.f;
            for (int j = 0; j < W; ++j) { const float dl = xs[(size_t) j] - xs[(size_t) (j + tau)]; s += dl * dl; }
            dd[(size_t) tau] = s;
        }
        cm[0] = 1.f;
        float run_ = 0.f;
        for (int tau = 1; tau <= tauMax; ++tau)
        {
            run_ += dd[(size_t) tau];
            cm[(size_t) tau] = dd[(size_t) tau] * (float) tau / (run_ > 1e-12f ? run_ : 1e-12f);
        }
        int est = -1;
        for (int t = tauMin; t < tauMax; ++t)
        {
            if (cm[(size_t) t] < 0.15f)
            {
                while (t + 1 < tauMax && cm[(size_t) (t + 1)] < cm[(size_t) t]) ++t;
                est = t; break;
            }
        }
        if (est < 0)
        {
            int best = tauMin;
            for (int t = tauMin; t < tauMax; ++t) if (cm[(size_t) t] < cm[(size_t) best]) best = t;
            if (cm[(size_t) best] < 0.3f) est = best;
        }
        if (est > 1 && est < tauMax)
        {
            const float s0 = cm[(size_t) est - 1], s1 = cm[(size_t) est], s2 = cm[(size_t) est + 1];
            const float den = s0 + s2 - 2.f * s1;
            float sh = den != 0.f ? 0.5f * (s0 - s2) / den : 0.f;
            sh = std::max (-1.f, std::min (1.f, sh));
            return (float) (sr / D) / ((float) est + sh);
        }
        return 0.f;
    }

    int nearestAllowed (float m) const
    {
        const int r = (int) std::lround (m);
        int best = -1000; float bd = 1e9f;
        for (int k = r - 3; k <= r + 3; ++k)
        {
            if (P.mask[((k % 12) + 12) % 12])
            {
                const float d = std::fabs ((float) k - m);
                if (d < bd) { bd = d; best = k; }
            }
        }
        return best;
    }
    static float lfo (int shape, float ph)
    {
        const float f = ph - std::floor (ph);
        if (shape == 1) return f < 0.5f ? 4.f * f - 1.f : 3.f - 4.f * f;
        if (shape == 2) return f < 0.5f ? 1.f : -1.f;
        return std::sin (6.28318530718f * ph);
    }

    // ---------- un cuadro de análisis (cada ~5 ms) ----------
    void runFrame()
    {
        const float norm = 1.f / (float) (D * D);
        float e = 0.f;
        for (int i = 0; i < N; ++i)
        {
            const int64_t c = n - (int64_t) (N - 1 - i) * D - D;
            float s = 0.f;
            if (D == 1) s = inAt (c);
            else for (int j = -D + 1; j < D; ++j) s += (float) (D - std::abs (j)) * inAt (c + j);
            s *= norm;
            xs[(size_t) i] = s;
            e += s * s;
        }
        const float rms = std::sqrt (e / (float) N);
        peakRms = std::max (rms, peakRms * 0.999f);
        const float thr = std::max (0.002f, 0.05f * peakRms);
        const float raw = rms > thr ? yin() : 0.f;

        // mediana causal de 5 y requisito de 2 cuadros seguidos
        for (int i = 4; i > 0; --i) rawH[i] = rawH[i - 1];
        rawH[0] = raw; rawN = std::min (5, rawN + 1);
        streak = raw > 0.f ? streak + 1 : 0;
        float f0 = 0.f;
        if (raw > 0.f && streak >= 2)
        {
            float w[5] = { 0.f, 0.f, 0.f, 0.f, 0.f }; int cnt = 0;
            for (int i = 0; i < rawN && i < 5; ++i) if (rawH[i] > 0.f) w[cnt++] = rawH[i];
            if (cnt >= 3)
            {
                for (int a = 1; a < cnt; ++a)   // ordenación por inserción (cnt <= 5)
                    for (int b = a; b > 0 && w[b - 1] > w[b]; --b) std::swap (w[b - 1], w[b]);
                f0 = w[(cnt - 1) / 2];
            }
            else f0 = raw;
            hang = 3; lastF0 = f0;
        }
        else if (hang > 0 && lastF0 > 0.f) { --hang; f0 = lastF0; }

        const float hopSec = (float) hop / (float) sr;
        float shiftC = 0.f, inM = std::numeric_limits<float>::quiet_NaN(), outM = inM, tgtM = inM;
        if (f0 > 0.f)
        {
            if (! run) { run = true; cNum = 0; cSum = 0.f; cIdx = 0; prevT = -1000; age = 0.f; shiftState = 0.f; }
            const float c = 100.f * (69.f + 12.f * std::log2 (f0 / P.refA));
            if (cNum == 16) cSum -= cr[cIdx]; else ++cNum;
            cr[cIdx] = c; cSum += c; cIdx = (cIdx + 1) & 15;
            const float center = cSum / (float) cNum;
            const float m = center / 100.f;
            const int best = nearestAllowed (m);
            float T;
            if (best == -1000) T = m;
            else
            {
                T = (float) best;
                if (prevT != -1000 && P.mask[((prevT % 12) + 12) % 12] && std::fabs (m - (float) prevT) <= std::fabs (m - (float) best) + 0.25f)
                    T = (float) prevT;
            }
            if ((int) std::lround (T) != prevT || (best == -1000 && prevT != -1000)) { age = 0.f; prevT = best == -1000 ? -1000 : (int) std::lround (T); }
            else age += hopSec;
            const float desired = (T - m) * 100.f;
            const float tau = (P.retuneMs / 1000.f) * (1.f + (P.humanize / 100.f) * 6.f * std::min (age, 1.5f));
            const float a = tau < 1e-4f ? 1.f : 1.f - std::exp (-hopSec / tau);
            shiftState += (desired - shiftState) * a;
            const float vib = c - center;
            float s = shiftState + vib * (P.vibAmt - 1.f);
            if (P.cvDepth > 0.f)
            {
                const float env = std::min (1.f, std::max (0.f, (age - P.cvDelay) / 0.25f));
                s += P.cvDepth * env * lfo (P.cvShape, age * P.cvRate);
            }
            s += P.transpose * 100.f + P.detune;
            shiftC = std::max (-2400.f, std::min (2400.f, s));
            inM = c / 100.f; outM = inM + shiftC / 100.f; tgtM = T;
        }
        else run = false;

        Frame& fr = frames[(size_t) (nFr % kFrames)];
        fr.pos = n - Cdelay; fr.f0 = f0; fr.shift = shiftC;
        ++nFr;
        const uint32_t w = histW.load (std::memory_order_relaxed);
        histIn[w % kHist].store (inM, std::memory_order_relaxed);
        histOut[w % kHist].store (outM, std::memory_order_relaxed);
        histTgt[w % kHist].store (tgtM, std::memory_order_relaxed);
        histW.store (w + 1, std::memory_order_release);

        const int64_t Hs = n - HO;
        createMarks (Hs);
        synthesize (Hs);
    }

    float f0At (int64_t pos) const
    {
        if (nFr == 0) return 0.f;
        const Frame& last = frame (nFr - 1);
        if (pos >= last.pos) return last.f0;
        const int64_t lo = std::max<int64_t> (0, nFr - kFrames + 1);
        int64_t j = nFr - 1;
        while (j > lo && frame (j).pos > pos) --j;
        if (frame (j).pos > pos) return frame (j).f0;
        const Frame& a = frame (j); const Frame& b = frame (j + 1);
        if (a.f0 > 0.f && b.f0 > 0.f) return a.f0 + (b.f0 - a.f0) * (float) (pos - a.pos) / (float) (b.pos - a.pos);
        return (pos - a.pos < b.pos - pos) ? a.f0 : b.f0;
    }
    float shiftAt (int64_t pos) const
    {
        if (nFr == 0) return 0.f;
        const Frame& last = frame (nFr - 1);
        if (pos >= last.pos) return last.shift;
        const int64_t lo = std::max<int64_t> (0, nFr - kFrames + 1);
        int64_t j = nFr - 1;
        while (j > lo && frame (j).pos > pos) --j;
        if (frame (j).pos > pos) return frame (j).shift;
        const Frame& a = frame (j); const Frame& b = frame (j + 1);
        if (a.f0 > 0.f && b.f0 > 0.f) return a.shift + (b.shift - a.shift) * (float) (pos - a.pos) / (float) (b.pos - a.pos);
        return (pos - a.pos < b.pos - pos) ? a.shift : b.shift;
    }

    void pushMark (int64_t pos, float per, uint8_t vo)
    {
        Mark& m = marks[(size_t) (nMk & (kMarks - 1))];
        m.pos = pos; m.per = per; m.vo = vo;
        ++nMk; lastMarkPos = pos;
    }

    // ---------- marcas de análisis ----------
    void createMarks (int64_t limit)
    {
        while (lastMarkPos < limit)
        {
            const float f = f0At (nextPos);
            if (! (f > 0.f))
            {
                pushMark (nextPos, (float) U, 0);
                nextPos += U; runActive = false;
                continue;
            }
            const float ff = std::max (75.f, std::min (1000.f, f));
            const float Pd = (float) sr / ff;
            int64_t m;
            if (! runActive)
            {
                const int64_t lo = nextPos, hi = std::min<int64_t> (n - 1, nextPos + (int64_t) std::lround (Pd));
                float mp = -1e30f, mn = -1e30f; int64_t ip = lo, im = lo;
                for (int64_t i = lo; i <= hi; ++i)
                {
                    const float v = inAt (i);
                    if (v > mp) { mp = v; ip = i; }
                    if (-v > mn) { mn = -v; im = i; }
                }
                if (mp >= mn) { pol = 1; m = ip; } else { pol = -1; m = im; }
                runActive = true;
            }
            else
            {
                const int64_t ex = lastVoicedMark + (int64_t) std::lround (Pd);
                const int64_t lo = std::max<int64_t> (lastVoicedMark + (int64_t) std::lround (0.5f * Pd), ex - (int64_t) std::lround (0.3f * Pd));
                const int64_t hi = std::min<int64_t> (n - 1, ex + (int64_t) std::lround (0.3f * Pd));
                if (lo > hi) m = std::min<int64_t> (n - 1, ex);
                else
                {
                    float bv = -1e30f; m = lo;
                    for (int64_t i = lo; i <= hi; ++i)
                    {
                        const float v = (float) pol * inAt (i);
                        if (v > bv) { bv = v; m = i; }
                    }
                }
            }
            pushMark (m, Pd, 1);
            lastVoicedMark = m;
            nextPos = m + std::max<int64_t> (1, (int64_t) std::lround (Pd));
        }
    }

    // ---------- resíntesis PSOLA ----------
    void synthesize (int64_t Hs)
    {
        const float fform = std::pow (2.f, std::max (-6.f, std::min (6.f, P.formant)) / 12.f);
        while (synthPos <= (double) Hs && nMk > 0)
        {
            const int64_t first = std::max<int64_t> (0, nMk - kMarks + 8);
            if (kPtr < first) kPtr = first;
            while (kPtr + 1 < nMk && std::fabs ((double) mark (kPtr + 1).pos - synthPos) < std::fabs ((double) mark (kPtr).pos - synthPos)) ++kPtr;
            const Mark mk = mark (kPtr);
            const bool voiced = mk.vo == 1;
            double step;
            double ts = synthPos;
            float fs = 1.f;
            if (voiced)
            {
                const float r = std::max (0.25f, std::min (4.f, std::pow (2.f, shiftAt ((int64_t) ts) / 1200.f)));
                step = (double) mk.per / (double) r;
                fs = fform;
            }
            else { ts = (double) mk.pos; step = mk.per; }
            const int L = std::max (8, (int) std::lround (mk.per));
            const int64_t o = (int64_t) std::llround (ts);
            for (int i = -L; i <= L; ++i)
            {
                const int64_t j = o + i;
                if (j < 0) continue;
                const double sp = (double) mk.pos + (double) i * fs;
                const int64_t s0 = (int64_t) std::floor (sp);
                const float fr = (float) (sp - (double) s0);
                const float a = inAt (s0), b = inAt (s0 + 1);
                const float w = 0.5f * (1.f + std::cos (3.14159265359f * (float) i / (float) L));
                const size_t k = (size_t) (j & kMask);
                outb[k] += (a + (b - a) * fr) * w;
                wsb[k] += w;
            }
            synthPos = ts + std::max (4.0, step);
        }
    }
};
} // namespace entonador
