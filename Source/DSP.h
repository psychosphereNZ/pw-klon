#pragma once
// PW-Klon DSP: Kompressor mit FF/FB/INT-Detektion, M/S, DPR, Saettigung und Parallel-Mix.
// 1:1-Port des getesteten JSFX-Skripts (presswerk_klon.jsfx).

#include <algorithm>
#include <cmath>
#include <vector>

namespace pw
{
constexpr double kPi = 3.14159265358979323846;
constexpr double kDbToLin = 0.11512925464970228; // ln(10)/20

inline double dbToGain (double db) { return std::exp (db * kDbToLin); }
inline double gainToDb (double g)  { return 20.0 * std::log10 (g); }

struct OnePole
{
    double a = 0, z = 0;
    void set (double f, double sr) { a = std::exp (-2.0 * kPi * std::min (f, 0.49 * sr) / sr); }
    double lp (double x) { z = x + (z - x) * a; return z; }
    double hp (double x) { z = x + (z - x) * a; return x - z; }
    void reset() { z = 0; }
};

struct Allpass
{
    double b0 = 1, b1 = 0, a1 = 0, a2 = 0, x1 = 0, x2 = 0, y1 = 0, y2 = 0;
    void set (double f, double q, double sr)
    {
        const double w = 2.0 * kPi * f / sr, cs = std::cos (w), al = std::sin (w) / (2.0 * q), a0 = 1.0 + al;
        b0 = (1.0 - al) / a0; b1 = -2.0 * cs / a0; a1 = b1; a2 = b0;
    }
    double process (double x)
    {
        const double y = b0 * x + b1 * x1 + x2 - a1 * y1 - a2 * y2;
        x2 = x1; x1 = x; y2 = y1; y1 = y;
        return y;
    }
    void reset() { x1 = x2 = y1 = y2 = 0; }
};

// Low-Shelf 1. Ordnung (DC-Gain g, HF-Gain 1) bzw. seine exakte Inverse
struct Shelf
{
    double b0 = 1, b1 = 0, a1 = 0, x1 = 0, y1 = 0;
    void set (double fc, double g, bool inverse, double sr)
    {
        const double K = std::tan (kPi * fc / sr);
        if (inverse) { const double n = K * g + 1; b0 = (K + 1) / n; b1 = (K - 1) / n; a1 = (K * g - 1) / n; }
        else         { const double n = K + 1;     b0 = (K * g + 1) / n; b1 = (K * g - 1) / n; a1 = (K - 1) / n; }
    }
    double process (double x) { const double y = b0 * x + b1 * x1 - a1 * y1; x1 = x; y1 = y; return y; }
    void reset() { x1 = y1 = 0; }
};

inline double satur (double x, double d)
{
    if (d <= 1.0001) return x;
    const double k = std::min ((d - 1.0) * 4.0, 1.0);
    return x + (std::tanh (x * d) / d - x) * k;
}

inline double softClip (double x)
{
    constexpr double t = 0.7;
    const double a = std::abs (x);
    return a > t ? std::copysign (t + (1.0 - t) * std::tanh ((a - t) / (1.0 - t)), x) : x;
}

// Gain Computer: Pegel (dB) -> Gain-Reduktion (dB, <= 0)
inline double gainComputer (double L, double T, double W, double s, double nl)
{
    const double ov = L - T;
    double gr;
    if (W < 0.01)            gr = ov > 0 ? s * ov : 0.0;
    else if (2 * ov < -W)    gr = 0.0;
    else if (2 * ov > W)     gr = s * ov;
    else { const double x = ov + W * 0.5; gr = s * x * x / (2 * W); }
    const double x = std::max (ov + W * 0.5, 0.0);
    gr -= nl * x * x / (48.0 + 2.0 * x);
    return std::min (gr, 0.0);
}

struct Params
{
    double inputDb = 0, outputDb = 0, threshold = -18, ratio = 4, knee = 6, nonlin = 0;
    bool autoMakeup = false;
    double attackMs = 10, releaseMs = 150, adapt = 0, rmsMs = 2;
    int detection = 0; // 0 FF, 1 FB, 2 INT
    double link = 100;
    bool midSide = false, dpr = false;
    double scMix = -100, scDelayMs = 0, scLow = 20, scHigh = 20000;
    bool sc12dB = false;
    int scMonitor = 0; // 0 aus, 1 CH1, 2 CH2, 3 beide
    double scMonDb = 0;
    bool satPre = false;
    double satAmount = 0, satDynamics = 0, warmth = 0;
    double mix = 100, wetDb = 0, dryHp = 20;
    bool expand = false, softClip = true;
};

class Dsp
{
public:
    void prepare (double sampleRate)
    {
        sr = sampleRate;
        D = (int) std::ceil (0.005 * sr);
        int n = 1;
        while (n < 2 * D + 8) n <<= 1;
        mask = n - 1;
        for (auto& b : bufM) b.assign ((size_t) n, 0.0);
        for (auto& b : bufS) b.assign ((size_t) n, 0.0);
        wpos = 0;

        pkR    = std::exp (-1.0 / (0.05 * sr));
        crestC = 1.0 - std::exp (-1.0 / (0.1 * sr));
        wC     = 1.0 - std::exp (-1.0 / (0.03 * sr));
        mDec   = std::exp (-1.0 / (0.3 * sr));
        smC    = 1.0 - std::exp (-1.0 / (0.02 * sr));
        reset();
        setParams (p, true);
    }

    void reset()
    {
        G[0] = G[1] = ms[0] = ms[1] = pk[0] = pk[1] = cr[0] = cr[1] = w[0] = w[1] = 0;
        for (auto& b : bufM) std::fill (b.begin(), b.end(), 0.0);
        for (auto& b : bufS) std::fill (b.begin(), b.end(), 0.0);
        for (auto* f : { &hpA1, &hpA2, &hpB1, &hpB2, &lpA1, &lpA2, &lpB1, &lpB2, &dhpL, &dhpR }) f->reset();
        for (auto* f : { &apiL, &apiR, &apoL, &apoR }) f->reset();
        for (auto* f : { &shp[0], &shp[1], &shq[0], &shq[1] }) f->reset();
        inPk = outPk = 0;
        first = true;
    }

    int latency() const { return D; }

    void setParams (const Params& np, bool force = false)
    {
        const Params old = p;
        p = np;

        tIn  = dbToGain (p.inputDb);
        tOut = dbToGain (p.outputDb);
        thr = p.threshold;
        ratio = std::max (p.ratio, 1.0);
        makeup = p.autoMakeup ? -thr * (1.0 - 1.0 / ratio) / 6.0 : 0.0;

        sFF = 1.0 / ratio - 1.0;
        const double se = 1.0 / std::min (ratio, 2.5) - 1.0;
        sFB = se / (1.0 + se);
        sINT = 1.0 / std::min (ratio, 5.0) - 1.0;

        attC = 1.0 - std::exp (-2.3 / (std::max (p.attackMs, 0.01) * 0.001 * sr));
        if (p.detection > 0) attC = std::min (attC, 0.5);
        relS = p.releaseMs * 0.001 * sr;
        rmsC = p.rmsMs > 0.01 ? 1.0 - std::exp (-1.0 / (p.rmsMs * 0.001 * sr)) : 1.0;

        ext = (p.scMix + 100.0) / 200.0;
        scd = std::clamp (D + (int) std::lround (p.scDelayMs * 0.001 * sr), 0, mask);

        if (force || old.scLow != p.scLow)
            for (auto* f : { &hpA1, &hpA2, &hpB1, &hpB2 }) f->set (p.scLow, sr);
        if (force || old.scHigh != p.scHigh)
            for (auto* f : { &lpA1, &lpA2, &lpB1, &lpB2 }) f->set (p.scHigh, sr);
        if (force || old.warmth != p.warmth)
        {
            const double wg = dbToGain (-p.warmth);
            for (int c = 0; c < 2; ++c) { shp[c].set (600, wg, false, sr); shq[c].set (600, wg, true, sr); }
        }
        if (force || old.dryHp != p.dryHp) { dhpL.set (p.dryHp, sr); dhpR.set (p.dryHp, sr); }
        if (force)
        {
            apiL.set (200, 0.7, sr); apiR.set (200, 0.7, sr);
            apoL.set (70, 0.7, sr);  apoR.set (70, 0.7, sr);
        }

        tWet = p.mix / 100.0;
        tWetLvl = dbToGain (p.wetDb + makeup);
        monLvl = dbToGain (p.scMonDb);
        if (force || first) { sIn = tIn; sOut = tOut; sWet = tWet; sWetLvl = tWetLvl; first = false; }
    }

    // L/R: Haupt-Audio (in-place). eL/eR: externe Sidechain (darf nullptr sein).
    void process (float* L, float* R, const float* eL, const float* eR, int n)
    {
        const double link = p.link / 100.0;
        const double adapt = p.adapt / 100.0;
        double grMin1 = 0, grMin2 = 0;

        for (int i = 0; i < n; ++i)
        {
            sIn += (tIn - sIn) * smC; sOut += (tOut - sOut) * smC;
            sWet += (tWet - sWet) * smC; sWetLvl += (tWetLvl - sWetLvl) * smC;

            double xl = L[i] * sIn, xr = R[i] * sIn;
            const double el = eL ? eL[i] * sIn : 0.0;
            const double er = eR ? eR[i] * sIn : 0.0;
            inPk = std::max ({ std::abs (xl), std::abs (xr), inPk * mDec });
            if (p.dpr) { xl = apiL.process (xl); xr = apiR.process (xr); }

            bufM[0][(size_t) wpos] = xl; bufM[1][(size_t) wpos] = xr;
            bufS[0][(size_t) wpos] = xl; bufS[1][(size_t) wpos] = xr;
            bufS[2][(size_t) wpos] = el; bufS[3][(size_t) wpos] = er;
            const size_t rp = (size_t) ((wpos - D) & mask);
            const size_t rs = (size_t) ((wpos - scd) & mask);
            const double dl = bufM[0][rp], dr = bufM[1][rp];
            const double il = bufS[0][rs], ir = bufS[1][rs], xel = bufS[2][rs], xer = bufS[3][rs];
            wpos = (wpos + 1) & mask;

            double c1, c2, s1, s2, e1, e2;
            if (p.midSide)
            {
                c1 = (dl + dr) * 0.5;  c2 = (dl - dr) * 0.5;
                s1 = (il + ir) * 0.5;  s2 = (il - ir) * 0.5;
                e1 = (xel + xer) * 0.5; e2 = (xel - xer) * 0.5;
            }
            else { c1 = dl; c2 = dr; s1 = il; s2 = ir; e1 = xel; e2 = xer; }

            // Sidechain
            double d1 = s1 * (1 - ext) + e1 * ext;
            double d2 = s2 * (1 - ext) + e2 * ext;
            d1 = hpA1.hp (d1); d2 = hpB1.hp (d2);
            if (p.sc12dB) { d1 = hpA2.hp (d1); d2 = hpB2.hp (d2); }
            d1 = lpA1.lp (d1); d2 = lpB1.lp (d2);
            if (p.sc12dB) { d1 = lpA2.lp (d1); d2 = lpB2.lp (d2); }

            // RMS + Crest
            const double dd[2] = { d1, d2 };
            double Lv[2];
            for (int c = 0; c < 2; ++c)
            {
                ms[c] += (dd[c] * dd[c] - ms[c]) * rmsC;
                Lv[c] = 10.0 * std::log10 (ms[c] + 1e-12);
                pk[c] = std::max (std::abs (dd[c]), pk[c] * pkR);
                cr[c] += (dd[c] * dd[c] - cr[c]) * crestC;
                const double q = gainToDb ((pk[c] + 1e-9) / (std::sqrt (cr[c]) + 1e-9));
                w[c] += (std::clamp ((q - 4.0) / 10.0, 0.0, 1.0) - w[c]) * wC;
            }

            // Link
            const double Lm = std::max (Lv[0], Lv[1]), wm = std::max (w[0], w[1]);
            const double Ln[2] = { Lv[0] + (Lm - Lv[0]) * link, Lv[1] + (Lm - Lv[1]) * link };
            const double wn[2] = { w[0] + (wm - w[0]) * link, w[1] + (wm - w[1]) * link };

            double g[2];
            for (int c = 0; c < 2; ++c)
            {
                double T;
                if (p.detection == 0)      T = gainComputer (Ln[c], thr, p.knee, sFF, p.nonlin);
                else if (p.detection == 1) T = gainComputer (Ln[c] + G[c], thr, p.knee, sFB, p.nonlin);
                else T = wn[c] * gainComputer (Ln[c], thr, p.knee, sINT, p.nonlin)
                       + (1 - wn[c]) * gainComputer (Ln[c] + G[c], thr, p.knee, sFB, p.nonlin);

                if (T < G[c]) G[c] += (T - G[c]) * attC;
                else          G[c] += (T - G[c]) * (1.0 - std::exp (-1.0 / (relS * std::pow (4.0, adapt * (1.0 - 2.0 * wn[c])))));
                g[c] = dbToGain (G[c]);
            }
            grMin1 = std::min (grMin1, G[0]);
            grMin2 = std::min (grMin2, G[1]);

            // Saettigung + VCA
            const double cc[2] = { c1, c2 };
            double y[2];
            for (int c = 0; c < 2; ++c)
            {
                if (p.satAmount > 0)
                {
                    const double dv = dbToGain (p.satAmount * ((1 - p.satDynamics / 100.0)
                                                + p.satDynamics / 100.0 * std::min (-G[c] / 6.0, 1.0)));
                    y[c] = p.satPre ? shq[c].process (satur (shp[c].process (cc[c]), dv)) * g[c]
                                    : shq[c].process (satur (shp[c].process (cc[c] * g[c]), dv));
                }
                else y[c] = cc[c] * g[c];
            }

            double wl, wr;
            if (p.midSide) { wl = y[0] + y[1]; wr = y[0] - y[1]; }
            else           { wl = y[0]; wr = y[1]; }
            if (p.dpr) { wl = apoL.process (wl); wr = apoR.process (wr); }
            wl *= sWetLvl; wr *= sWetLvl;

            // Dry
            double dryl = dhpL.hp (dl), dryr = dhpR.hp (dr);
            if (p.expand)
            {
                const double x1 = std::min (1.0 / g[0], 15.85);
                const double x2 = p.midSide ? x1 : std::min (1.0 / g[1], 15.85);
                dryl *= x1; dryr *= x2;
            }

            double ol = dryl * (1 - sWet) + wl * sWet;
            double orr = dryr * (1 - sWet) + wr * sWet;

            if (p.scMonitor == 1) ol = orr = d1 * monLvl;
            else if (p.scMonitor == 2) ol = orr = d2 * monLvl;
            else if (p.scMonitor == 3)
            {
                if (p.midSide) { ol = (d1 + d2) * monLvl; orr = (d1 - d2) * monLvl; }
                else           { ol = d1 * monLvl; orr = d2 * monLvl; }
            }

            ol *= sOut; orr *= sOut;
            if (p.softClip) { ol = softClip (ol); orr = softClip (orr); }
            outPk = std::max ({ std::abs (ol), std::abs (orr), outPk * mDec });
            L[i] = (float) ol; R[i] = (float) orr;
        }

        meterGR1 = (float) -grMin1;
        meterGR2 = (float) -grMin2;
        meterIn  = (float) inPk;
        meterOut = (float) outPk;
        meterFF  = (float) (p.detection == 0 ? 1.0 : p.detection == 1 ? 0.0 : w[0]);
    }

    float meterGR1 = 0, meterGR2 = 0, meterIn = 0, meterOut = 0, meterFF = 0;

private:
    double sr = 44100;
    Params p;
    int D = 221, mask = 1023, wpos = 0, scd = 221;
    std::vector<double> bufM[2], bufS[4];

    double tIn = 1, tOut = 1, tWet = 1, tWetLvl = 1, sIn = 1, sOut = 1, sWet = 1, sWetLvl = 1, monLvl = 1;
    double thr = -18, ratio = 4, makeup = 0, sFF = -0.75, sFB = 0, sINT = 0;
    double attC = 0.1, relS = 1000, rmsC = 1, ext = 0;
    double pkR = 0, crestC = 0, wC = 0, mDec = 0, smC = 0;
    double G[2] {}, ms[2] {}, pk[2] {}, cr[2] {}, w[2] {};
    double inPk = 0, outPk = 0;
    bool first = true;

    OnePole hpA1, hpA2, hpB1, hpB2, lpA1, lpA2, lpB1, lpB2, dhpL, dhpR;
    Allpass apiL, apiR, apoL, apoR;
    Shelf shp[2], shq[2];
};
} // namespace pw
