// Fantomine (Studio Wiga) — moteur DSP de la voix thérémine.
// Indépendant de JUCE pour pouvoir être testé hors ligne.
//
// Chaîne : entrée guitare mono -> passe-haut -> passe-bas + décimation ->
// détection de hauteur YIN -> note cible (+ octave + courbure pédale)
// -> glide (portamento en domaine logarithmique) -> vibrato humanisé
// -> oscillateur additif sans repliement (timbre PURE..REED)
// -> formant "voix" -> enveloppe suivie sur la guitare -> mélange.
#pragma once

#include <array>
#include <cmath>
#include <cstdint>
#include <vector>
#include <algorithm>

namespace fantomine
{

constexpr double kPi  = 3.14159265358979323846;
constexpr double kTwoPi = 2.0 * kPi;

inline double midiToHz (double m) { return 440.0 * std::pow (2.0, (m - 69.0) / 12.0); }
inline double hzToMidi (double f) { return 69.0 + 12.0 * std::log2 (f / 440.0); }

//==============================================================================
// Biquad (RBJ), simple précision suffisante ici mais calculs en double.
struct Biquad
{
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
    double z1 = 0, z2 = 0;

    void reset() { z1 = z2 = 0; }

    void setLowpass (double sr, double f, double q)
    {
        const double w = kTwoPi * f / sr, c = std::cos (w), al = std::sin (w) / (2.0 * q);
        const double a0 = 1.0 + al;
        b0 = (1.0 - c) * 0.5 / a0; b1 = (1.0 - c) / a0; b2 = b0;
        a1 = -2.0 * c / a0; a2 = (1.0 - al) / a0;
    }
    void setHighpass (double sr, double f, double q)
    {
        const double w = kTwoPi * f / sr, c = std::cos (w), al = std::sin (w) / (2.0 * q);
        const double a0 = 1.0 + al;
        b0 = (1.0 + c) * 0.5 / a0; b1 = -(1.0 + c) / a0; b2 = b0;
        a1 = -2.0 * c / a0; a2 = (1.0 - al) / a0;
    }
    void setPeak (double sr, double f, double q, double gainDb)
    {
        const double A = std::pow (10.0, gainDb / 40.0);
        const double w = kTwoPi * f / sr, c = std::cos (w), al = std::sin (w) / (2.0 * q);
        const double a0 = 1.0 + al / A;
        b0 = (1.0 + al * A) / a0; b1 = -2.0 * c / a0; b2 = (1.0 - al * A) / a0;
        a1 = -2.0 * c / a0; a2 = (1.0 - al / A) / a0;
    }
    inline double process (double x)
    {
        const double y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return y;
    }
};

//==============================================================================
// Détecteur de hauteur YIN monophonique, travaillant sur un signal décimé (~12 kHz).
//  - détection d'attaque : après chaque nouvelle note, l'analyse ne regarde que ce qui suit
//    l'attaque (la note précédente qui sonne encore ne « colle » plus), puis la fenêtre s'agrandit ;
//  - repli sur le minimum global quand aucun creux franc n'existe (cordes qui sonnent ensemble) ;
//  - correction d'octave dans les deux sens (fondamental faible au micro chevalet, sous-multiples).
class PitchTracker
{
public:
    void prepare (double sampleRate)
    {
        decim = std::max (1, (int) std::lround (sampleRate / 12000.0));
        dsr = sampleRate / decim;

        hp.setHighpass (sampleRate, 28.0, 0.707);   // garde le fondamental d'une 7 cordes en G (et plus bas)
        lp1.setLowpass (sampleRate, 1800.0, 0.54);
        lp2.setLowpass (sampleRate, 1800.0, 1.31);
        hp.reset(); lp1.reset(); lp2.reset();

        // note la plus grave suivie : 33 Hz (Do1) -> une 7 cordes en G (Sol1, 49 Hz) et ~4 tons plus bas
        maxTau     = (int) (dsr / 33.0);
        minTau     = std::max (2, (int) (dsr / 1400.0));
        windowSize = (int) std::lround (dsr * 0.032);          // 32 ms en régime établi
        minWindow  = (int) std::lround (dsr * 0.010);          // 10 ms minimum juste après l'attaque
        hop        = std::max (8, (int) std::lround (dsr * 0.003)); // une analyse toutes les 3 ms

        // + 2 périodes max d'historique : les peignes qui retirent les notes précédentes lisent avant la fenêtre
        ring.assign ((size_t) (windowSize + 4 * maxTau + 32), 0.0f);
        diff.assign ((size_t) maxTau + 2, 0.0f);
        raw.assign ((size_t) maxTau + 2, 0.0f);
        frame.assign (ring.size(), 0.0f);
        cframe.assign (ring.size(), 0.0f);
        for (int r = 0; r < kRefs; ++r) { refTau[r] = 0.0; refAge[r] = 0; }
        candTau = 0.0; candCount = 0;
        refLife = (int) (3.0 * dsr);
        writePos = 0; counter = 0; hopCounter = 0;
        hist[0] = hist[1] = hist[2] = 0.0;
        histCount = 0;
        detected = false; midi = 0.0; confidence = 0.0;
        fastEnv = slowEnv = 0.0;
        onsetHP.setHighpass (sampleRate, 1500.0, 0.707);
        onsetHP2.setHighpass (sampleRate, 1500.0, 0.707);
        onsetHP.reset(); onsetHP2.reset();
        fastC = std::exp (-1.0 / (0.004 * sampleRate));
        broadEnv = 0.0; broadC = std::exp (-1.0 / (0.150 * sampleRate));
        blockLen = std::max (1, (int) std::lround (0.001 * sampleRate));
        for (auto& b : blockHist) b = 0.0f;
        blockPos = 0; blockCount = 0; blockMax = 0.0; prevPeak = 0.0;
        slowC = std::exp (-1.0 / (0.060 * sampleRate));
        sinceOnset = 1 << 30; refractory = (int) (0.050 * sampleRate);   // compté en échantillons pleine fréquence
        onsetFlag = false;
    }

    // Renvoie true quand une nouvelle analyse vient d'être faite.
    bool push (float x)
    {
        // --- détection d'attaque sur les aigus (le médiator), pleine fréquence ---
        // les aigus ne « pulsent » pas avec les notes graves, contrairement à l'enveloppe large bande
        const double h = std::abs (onsetHP2.process (onsetHP.process (x)));
        fastEnv = h > fastEnv ? h : fastC * fastEnv + (1.0 - fastC) * h;
        // niveau large bande : sert de plancher (les aigus résiduels des cordes qui sonnent sont minuscules)
        const double ax = std::abs (x);
        broadEnv = ax > broadEnv ? ax : broadC * broadEnv + (1.0 - broadC) * ax;
        // référence = crête des aigus sur les 5..45 ms précédentes. Une corde grave a des pointes d'aigus
        // à chaque période et ses battements avec une autre corde font monter/descendre ces pointes :
        // comparer à la crête récente (et non à une moyenne) évite les fausses attaques à répétition.
        blockMax = std::max (blockMax, fastEnv);
        if (++blockCount >= blockLen)
        {
            blockHist[(size_t) blockPos] = (float) blockMax;
            blockPos = (blockPos + 1) % kBlocks;
            blockCount = 0; blockMax = 0.0;
            float m = 0.0f;
            for (int b = kRecentBlocks; b < kBlocks; ++b) m = std::max (m, blockHist[(size_t) ((blockPos - 1 - b + 2 * kBlocks) % kBlocks)]);
            prevPeak = m;
        }
        if (sinceOnset < (1 << 30)) ++sinceOnset;
        if (sinceOnset > refractory && fastEnv > 1.8 * prevPeak + std::max (2.0e-4, 0.01 * broadEnv))
        {
           #ifdef FANTO_DEBUG
            if (fantoDebug) printf ("   ONSET fast=%.5f prev=%.5f broad=%.4f\n", fastEnv, prevPeak, broadEnv);
           #endif
            sinceOnset = 0;
            histCount = 0;            // l'ancienne note ne vote plus dans la médiane
            onsetFlag = true;
        }

        double y = lp2.process (lp1.process (hp.process (x)));
        if (++counter < decim) return false;
        counter = 0;

        ring[(size_t) writePos] = (float) y;
        writePos = (writePos + 1) % (int) ring.size();
        for (int r = 0; r < kRefs; ++r)
            if (refTau[r] > 0.0 && ++refAge[r] > refLife) refTau[r] = 0.0;   // corde éteinte depuis longtemps

        if (++hopCounter < hop) return false;
        hopCounter = 0;
        analyse();
        return true;
    }

    bool   hasPitch() const     { return detected; }
    double getMidi() const      { return midi; }
    double getConfidence() const{ return confidence; }
    bool   takeOnset()          { const bool o = onsetFlag; onsetFlag = false; return o; }

private:
    // lecture du tampon circulaire à une position fractionnaire (Lagrange 4 points)
    inline float ringAt (double pos) const
    {
        const int N = (int) ring.size();
        const int i = (int) std::floor (pos);
        const float t = (float) (pos - i);
        auto at = [this, N] (int k) { k %= N; if (k < 0) k += N; return ring[(size_t) k]; };
        const float ym1 = at (i - 1), y0 = at (i), y1 = at (i + 1), y2 = at (i + 2);
        const float c1 = y1 - ym1 * (1.0f / 3.0f) - y0 * 0.5f - y2 * (1.0f / 6.0f);
        const float c2 = 0.5f * (ym1 + y1) - y0;
        const float c3 = (y2 - ym1) * (1.0f / 6.0f) + 0.5f * (y0 - y1);
        return ((c3 * t + c2) * t + c1) * t + y0;
    }

    // période exacte de la note qui sonnait juste avant l'attaque : minimise le résidu du peigne
    // (une erreur d'un quart d'échantillon laisse passer les harmoniques aigus de l'ancienne note)
    double residual (double tau, int len) const
    {
        double e = 0.0;
        for (int i = 0; i < len; ++i)
        {
            const double p = (double) (writePos - 1 - i);
            const float d = ringAt (p) - ringAt (p - tau);
            e += (double) d * d;
        }
        return e;
    }
    double refinePeriod (double tau) const
    {
        const int len = std::min (windowSize, (int) ring.size() - (int) (3.0 * tau) - 8);
        if (len < 32) return tau;
        double step = 0.4;
        for (int it = 0; it < 4; ++it)
        {
            const double a = residual (tau - step, len), b = residual (tau, len), c = residual (tau + step, len);
            const double den = a - 2.0 * b + c;
            double off = den > 1e-12 ? 0.5 * (a - c) / den : (a < c ? -1.0 : 1.0);
            off = std::clamp (off, -1.0, 1.0);
            tau += off * step;
            step *= 0.35;
        }
        return tau;
    }

    struct YinResult { int status = 0; double tau = 0.0, d = 1.0; };   // 0 trop tôt, 1 pas de hauteur, 2 trouvée

    void analyse()
    {
        const int N = (int) ring.size();

        // longueur utilisable : tout le tampon, ou seulement ce qui suit la dernière attaque
        // (on laisse ~4 ms à l'attaque du médiator pour passer)
        const int skip = (int) (0.004 * dsr);
        const int full = windowSize + maxTau;
        const int since = sinceOnset / decim;                 // en échantillons décimés
        int usable = full;
        if (since < full + skip)
            usable = since - skip;
        if (usable < minWindow + minTau * 2) { return; }   // trop tôt : on garde l'estimation précédente

        // découpage fenêtre / décalage maximal
        int tauLim = std::min (maxTau, usable - minWindow);
        int W = std::min (windowSize, usable - tauLim);
        if (tauLim < minTau + 2 || W < minWindow) return;

        int start = writePos - (W + tauLim); while (start < 0) start += N;
        double energy = 0.0;
        for (int i = 0; i < W + tauLim; ++i)
        {
            const float v = ring[(size_t) ((start + i) % N)];
            frame[(size_t) i] = v;
            energy += (double) v * v;
        }

        if (energy / (W + tauLim) < 1.0e-7)          // silence : on oublie la note
        {
            detected = false; confidence = 0.0; histCount = 0;
            for (auto& r : refTau) r = 0.0;
            candCount = 0;
            return;
        }

        YinResult res = yin (frame.data(), W, tauLim, true);

        // Les cordes jouées avant sonnent souvent encore : le mélange donne l'ancienne note ou une
        // « note fantôme » plus grave (période commune). Un peigne x[n] - x[n - T] (T = période de
        // l'ancienne note) l'efface sans changer la période de la nouvelle note : on analyse aussi ce signal.
        if (refTau[0] > 0.0)
        {
            // peigne en cascade : (1 - z^-T0)(1 - z^-T1) développé en 4 lectures signées
            double offs[1 << kRefs]; float sgn[1 << kRefs]; int nOff = 0;
            offs[nOff] = 0.0; sgn[nOff++] = 1.0f;
            for (int r = 0; r < kRefs; ++r)
                if (refTau[r] > 0.0)
                    for (int j = 0, n0 = nOff; j < n0; ++j) { offs[nOff] = offs[j] + refTau[r]; sgn[nOff++] = -sgn[j]; }

            // juste après l'attaque, la nouvelle note n'a pas encore d'« écho » dans le peigne :
            // on n'analyse que ce qui suit l'attaque d'au moins le plus long retard du peigne
            const int combLag = (int) std::ceil (offs[nOff - 1]) + 1;
            int usableC = usable;
            if (since < full + skip + combLag) usableC = since - skip - combLag;
            const int tauLimC = std::min (tauLim, usableC - minWindow);
            const int WC = std::min (W, usableC - tauLimC);
            const bool combReady = tauLimC >= minTau + 2 && WC >= minWindow;
            const int startC = (W + tauLim) - (WC + tauLimC);           // décalage dans la trame

            double cEnergy = 0.0, eSub = 0.0;
            if (combReady)
                for (int i = startC; i < W + tauLim; ++i)
                {
                    const double p = (double) (start + i);
                    float v = frame[(size_t) i];
                    for (int j = 1; j < nOff; ++j) v += sgn[j] * ringAt (p - offs[j]);
                    cframe[(size_t) (i - startC)] = v;
                    cEnergy += (double) v * v;
                    eSub += (double) frame[(size_t) i] * frame[(size_t) i];
                }
            // énergie de référence : celle que le peigne laisse passer en moyenne (gain ~ 2 par étage)
            const double cRef = eSub * (double) (nOff);

           #ifdef FANTO_DEBUG
            if (fantoDebug) printf ("   since=%d usableC=%d ready=%d raw st=%d m=%.2f E=%.4f\n", since, usableC, (int) combReady, res.status, res.tau>0?hzToMidi(dsr/res.tau):0, combReady ? cEnergy/cRef : -1.0);
           #endif
            if (! combReady)
            {
                if (res.status == 2 && (isRef (res.tau) || isMultipleOfRef (res.tau))) return;   // on attend de pouvoir trancher
            }
            else
            {
                // pas de correction « octave en dessous » ici : le peigne creuse justement certains harmoniques
                YinResult c = cEnergy > 1.0e-4 * cRef ? yin (cframe.data(), WC, tauLimC, false) : YinResult { 1, 0.0, 1.0 };
               #ifdef FANTO_DEBUG
                if (fantoDebug) printf ("   raw st=%d m=%.2f d=%.2f | comb(%.2f %.2f) st=%d m=%.2f d=%.2f E=%.4f\n", res.status, res.tau>0?hzToMidi(dsr/res.tau):0, res.d,
                                        hzToMidi(dsr/refTau[0]), refTau[1]>0?hzToMidi(dsr/refTau[1]):0, c.status, c.tau>0?hzToMidi(dsr/c.tau):0, c.d, cEnergy/cRef);
               #endif
                const bool cNew   = c.status == 2 && c.d < 0.22 && ! isRef (c.tau) && ! isMultipleOfRef (c.tau);
                const bool rawNew = res.status == 2 && res.d < 0.08 && ! isRef (res.tau) && ! isMultipleOfRef (res.tau);
                if (cNew)
                    res = c;                                     // une note nouvelle se dégage sous les cordes qui sonnent
                else if (rawNew)
                    ;                                            // note seule bien nette (ex. octave d'une note déjà jouée)
                else if (c.status == 0)
                    return;                                      // la nouvelle note n'est pas encore mesurable
                else
                {
                    // rien de nouveau : seules les notes déjà jouées sonnent (note rejouée, attaque parasite)
                    // -> la note en cours, ou la corde qu'on vient de rejouer ; jamais la période commune des cordes
                    double pick = refTau[0];
                    if (since < (int) (0.15 * dsr))
                        pick = replucked (start + startC, (W + tauLim) - startC, start + (W + tauLim) - since);
                    res.status = 2; res.tau = pick; res.d = 0.1;
                }
            }
        }

        if (res.status == 0) return;
        if (res.status == 1) { confidence *= 0.6; if (confidence < 0.2) detected = false; return; }

        const double m = hzToMidi (dsr / res.tau);
        const double c = 1.0 - res.d;
        trackStable (res.tau);
       #ifdef FANTO_DEBUG
        if (fantoDebug) printf ("      [W=%d lim=%d tau=%.2f d'=%.3f m=%.2f path=%d]\n", W, tauLim, res.tau, res.d, m, dbgPath);
       #endif

        // médiane sur 3 analyses pour rejeter les erreurs isolées
        hist[2] = hist[1]; hist[1] = hist[0]; hist[0] = m;
        histCount = std::min (3, histCount + 1);
        double med = m;
        if (histCount == 3)
        {
            const double a = hist[0], b = hist[1], d = hist[2];
            med = std::max (std::min (a, b), std::min (std::max (a, b), d));
            if (std::abs (m - med) < 0.5) med = m;   // note stable : on garde la plus récente
        }

        midi = med;
        confidence = c;
        detected = true;
    }

    // note en cours = période stable 12 ms ; quand elle change, l'ancienne devient une note « qui sonne encore »
    void trackStable (double tau)
    {
        if (candTau > 0.0 && std::abs (tau - candTau) < 0.03 * candTau) ++candCount;
        else { candTau = tau; candCount = 1; }
        if (candCount < 4) return;

        if (refTau[0] > 0.0 && std::abs (candTau - refTau[0]) < 0.03 * refTau[0])
        {
            refAge[0] = 0;                                                   // elle sonne toujours
            if (candCount % 8 == 0) refTau[0] = refinePeriod (tau);          // suit le bend / la justesse qui bouge
            return;
        }
        if (candCount != 4) return;
        double nt[kRefs + 1]; int na[kRefs + 1]; int n = 0;
        nt[n] = refinePeriod (candTau); na[n++] = 0;
        for (int r = 0; r < kRefs; ++r)
            if (refTau[r] > 0.0 && std::abs (refTau[r] - nt[0]) > 0.03 * nt[0]) { nt[n] = refTau[r]; na[n++] = refAge[r]; }
        for (int r = 0; r < kRefs; ++r) { refTau[r] = r < n ? nt[r] : 0.0; refAge[r] = r < n ? na[r] : 0; }
       #ifdef FANTO_DEBUG
        if (fantoDebug) printf ("   NOTE %.2f (avant %.2f)\n", hzToMidi (dsr / refTau[0]), refTau[1] > 0 ? hzToMidi (dsr / refTau[1]) : 0.0);
       #endif
    }

    // Juste après une attaque, sans note nouvelle : quelle corde déjà connue vient d'être rejouée ?
    // Celle dont l'énergie augmente le plus entre avant et après l'attaque (les autres ne font que s'éteindre).
    // Énergie d'une composante r ~ E(x) - E(x - x[n - Tr]) / 2.
    double compEnergy (int from, int len, double tau) const
    {
        double ex = 0.0, ec = 0.0;
        for (int i = 0; i < len; ++i)
        {
            const double p = (double) (from + i);
            const float x = ringAt (p);
            const float c = x - ringAt (p - tau);
            ex += (double) x * x; ec += (double) c * c;
        }
        return std::max (0.0, ex - 0.5 * ec) / std::max (1, len);
    }
    double replucked (int postStart, int postLen, int onsetPos) const
    {
        const int preLen = std::min (postLen, std::max (32, windowSize / 2));
        auto total = [this] (int from, int len)
        {
            double e = 0.0;
            for (int i = 0; i < len; ++i) { const float x = ringAt ((double) (from + i)); e += (double) x * x; }
            return e / std::max (1, len);
        };
        const double postTot = total (postStart, postLen), preTot = total (onsetPos - preLen, preLen);
        double best = refTau[0], bestGain = -1.0;
        for (int r = 0; r < kRefs; ++r)
        {
            if (refTau[r] <= 0.0) continue;
            const double post = compEnergy (postStart, postLen, refTau[r]);
            if (post < 0.15 * postTot) continue;                          // corde étouffée / éteinte
            const double gain = post / (compEnergy (onsetPos - preLen, preLen, refTau[r]) + 0.1 * preTot + 1.0e-12);
            if (gain > bestGain) { bestGain = gain; best = refTau[r]; }
        }
        return best;
    }

    bool isMultipleOfRef (double tau) const
    {
        for (double r : refTau)
            if (r > 0.0)
                for (int k = 2; k <= 6; ++k)
                    if (std::abs (tau - k * r) < 0.02 * k * r) return true;
        return false;
    }

    bool isRef (double tau) const
    {
        for (double r : refTau)
            if (r > 0.0 && std::abs (tau - r) < 0.03 * r) return true;
        return false;
    }

    // YIN sur une trame (W + tauLim échantillons)
    YinResult yin (const float* f, int W, int tauLim, bool allowLower)
    {
        YinResult out;
        diff[0] = 1.0f;
        double running = 0.0;
        for (int tau = 1; tau <= tauLim; ++tau)
        {
            // 4 accumulateurs indépendants : le compilateur peut vectoriser sans -ffast-math
            float a0 = 0.0f, a1 = 0.0f, a2 = 0.0f, a3 = 0.0f;
            const float* g = f + tau;
            int i = 0;
            for (; i + 3 < W; i += 4)
            {
                const float d0 = f[i] - g[i], d1 = f[i + 1] - g[i + 1], d2 = f[i + 2] - g[i + 2], d3 = f[i + 3] - g[i + 3];
                a0 += d0 * d0; a1 += d1 * d1; a2 += d2 * d2; a3 += d3 * d3;
            }
            for (; i < W; ++i) { const float d = f[i] - g[i]; a0 += d * d; }
            const double s = (double) ((a0 + a1) + (a2 + a3));
            running += s;
            raw[(size_t) tau] = (float) s;
            diff[(size_t) tau] = (float) (running > 0.0 ? s * tau / running : 1.0);
        }

        auto localMin = [this, tauLim] (int tau)
        {
            while (tau + 1 < tauLim && diff[(size_t) tau + 1] < diff[(size_t) tau]) ++tau;
            while (tau - 1 > minTau && diff[(size_t) tau - 1] < diff[(size_t) tau]) --tau;
            return tau;
        };

        // valeur du creux interpolée (une période non entière tombe entre deux échantillons)
        auto rawMin = [this, tauLim] (int t)
        {
            if (t <= 1 || t >= tauLim - 1) return (double) raw[(size_t) t];
            const double a = raw[(size_t) t - 1], b = raw[(size_t) t], c = raw[(size_t) t + 1];
            const double den = a - 2.0 * b + c;
            return den > 1e-12 ? std::max (0.0, b - (c - a) * (c - a) / (8.0 * den)) : b;
        };

        // seuil absolu (étape 4)
        int best = -1;
        const float threshold = 0.12f;
        for (int tau = minTau; tau < tauLim; ++tau)
            if (diff[(size_t) tau] < threshold) { best = localMin (tau); break; }

       #ifdef FANTO_DEBUG
        dbgPath = best < 0 ? 2 : 1;
       #endif
        if (best < 0)
        {
            // repli : minimum global (plusieurs cordes qui sonnent, note qui s'éteint)
            int g = minTau;
            for (int tau = minTau + 1; tau < tauLim; ++tau)
                if (diff[(size_t) tau] < diff[(size_t) g]) g = tau;
            if (diff[(size_t) g] > 0.40f || W < windowSize / 2) { out.status = 1; return out; }
            // préfère le plus petit sous-multiple presque aussi bon (évite l'octave en dessous)
            best = g;
            for (int k = 4; k >= 2; --k)
            {
                if (g / k <= minTau) continue;
                const int t = localMin (std::max (minTau, (int) std::lround ((double) g / k)));
                if (std::abs (t * k - g) <= 2 + g / 40 && diff[(size_t) t] < diff[(size_t) g] + 0.06f) { best = t; break; }
            }
        }
        else if (allowLower)
        {
            // fondamental faible (micro chevalet) : si 2τ est nettement meilleur, la vraie période est 2τ
            for (int k = 2; k <= 3; ++k)
            {
                const int t2 = best * k;
                if (t2 + 1 >= tauLim) break;
                // comparaison sur la différence brute (sans la normalisation cumulée, qui favorise les grands τ)
                int t = t2;
                for (int j = std::max (minTau, t2 - 3 - t2 / 40); j <= std::min (tauLim - 1, t2 + 3 + t2 / 40); ++j)
                    if (raw[(size_t) j] < raw[(size_t) t]) t = j;
                if (diff[(size_t) best] > 0.08f && rawMin (t) < 0.25 * rawMin (best))
                {
                   #ifdef FANTO_DEBUG
                    dbgPath = 10 + k;
                   #endif
                    best = t; break; }
            }
        }

        // signal « peigné » : les restes des anciennes cordes créent des périodes communes très graves ;
        // on garde le plus petit sous-multiple encore bien périodique (la vraie nouvelle note)
        if (! allowLower)
            for (int k = 6; k >= 2; --k)
            {
                if (best / k <= minTau) continue;
                const int t = localMin (std::max (minTau, (int) std::lround ((double) best / k)));
                if (std::abs (t * k - best) <= 2 + best / 40 && diff[(size_t) t] < std::max (0.30f, diff[(size_t) best] + 0.15f)) { best = t; break; }
            }

        // trop tôt pour exclure une période deux fois plus longue : on attend un peu plus de signal
        if (best * 2 > tauLim && tauLim < maxTau && diff[(size_t) best] > 0.05f) { out.status = 0; return out; }

        // interpolation parabolique (étape 5)
        // (sur la différence brute : la normalisation cumulée décale le creux de quelques cents)
        double betterTau = best;
        if (best > 1 && best < tauLim - 1)
        {
            const double s0 = raw[(size_t) best - 1], s1 = raw[(size_t) best], s2 = raw[(size_t) best + 1];
            const double den = 2.0 * (2.0 * s1 - s2 - s0);
            if (std::abs (den) > 1e-12) betterTau = best + std::clamp ((s2 - s0) / den, -1.0, 1.0);
        }
        out.status = 2; out.tau = betterTau; out.d = diff[(size_t) best];
        return out;
    }

    Biquad hp, lp1, lp2, onsetHP, onsetHP2;
    int decim = 4, windowSize = 384, minWindow = 72, maxTau = 363, minTau = 8, hop = 36;
    double dsr = 12000.0;
    std::vector<float> ring, diff, raw, frame, cframe;
    static constexpr int kRefs = 3;          // note en cours + 2 notes précédentes qui sonnent encore
    double refTau[kRefs] {};                 // périodes en échantillons décimés (0 = aucune)
    int refAge[kRefs] {}, refLife = 36000;
    double candTau = 0.0; int candCount = 0;
    int writePos = 0, counter = 0, hopCounter = 0;
    double hist[3] {};
    int histCount = 0;
    bool detected = false;
    double midi = 0.0, confidence = 0.0;
    double fastEnv = 0.0, slowEnv = 0.0, fastC = 0.0, slowC = 0.0, broadEnv = 0.0, broadC = 0.0;
    static constexpr int kBlocks = 45, kRecentBlocks = 5;    // blocs de 1 ms
    std::array<float, kBlocks> blockHist {};
    int blockPos = 0, blockCount = 0, blockLen = 44;
    double blockMax = 0.0, prevPeak = 0.0;
    int sinceOnset = 1 << 30, refractory = 540;
    bool onsetFlag = false;
   #ifdef FANTO_DEBUG
 public: bool fantoDebug = false; int dbgPath = 0;
   #endif
};

//==============================================================================
// Petit générateur pseudo-aléatoire (xorshift), sans allocation.
struct Rng
{
    uint32_t s = 0x9E3779B9u;
    inline double next()   // [-1, 1]
    {
        s ^= s << 13; s ^= s >> 17; s ^= s << 5;
        return (double) s / 2147483648.0 - 1.0;
    }
};

//==============================================================================
struct Params
{
    float glide      = 0.35f;  // 0 = collé, 1 = très lent
    float rangeSemis = 12.0f;  // amplitude de la pédale (1..24 demi-tons)
    float tone       = 0.35f;  // 0 = sinus pur, 1 = anche / nasal
    float vibrato    = 0.25f;  // profondeur
    float speed      = 0.45f;  // vitesse du vibrato
    float blend      = 0.70f;  // 0 = guitare seule, 1 = voix seule
    float octaveSemis = 0.0f;  // décalage d'octave en demi-tons, continu (-12..+12, whammy)
    int   pedalMode  = 0;      // 0 = UP, 1 = DOWN, 2 = BIPOLAR (centre = neutre)
    float expression = 0.0f;   // position pédale 0..1
    bool  hold       = false;  // fige la note et le niveau
};

//==============================================================================
class TheremEngine
{
public:
    void prepare (double sampleRate)
    {
        sr = sampleRate;
        tracker.prepare (sampleRate);

        voiceHP.setHighpass (sr, 26.0, 0.707);
        voiceLP.setLowpass (sr, std::min (9000.0, sr * 0.45), 0.707);
        formant.setPeak (sr, 1150.0, 1.1, 0.0);
        voiceHP.reset(); voiceLP.reset(); formant.reset();

        phase = 0.0;
        curMidi = 57.0; targetMidi = 57.0; haveNote = false;
        env = 0.0; voiceAmp = 0.0; heldAmp = 0.0;
        expSmooth = 0.0; vibPhase = 0.0; vibDepthRamp = 0.0;
        driftA = driftB = 0.0; rateJit = 0.0; depthJit = 0.0;
        lastTone = -1.0f;
        for (auto& h : harmAmp) h = 0.0;
        updateTone (0.35f);

        envAtt = std::exp (-1.0 / (0.003 * sr));
        envRel = std::exp (-1.0 / (0.180 * sr));
        ampAtt = std::exp (-1.0 / (0.012 * sr));
        ampRel = std::exp (-1.0 / (0.140 * sr));
        expCoef = std::exp (-1.0 / (0.012 * sr));   // lissage pédale : supprime l'escalier des CC 7 bits
        blendSm = 0.7; blendCoef = std::exp (-1.0 / (0.02 * sr));
        octSm = 0.0; octCoef = std::exp (-1.0 / (0.015 * sr));     // whammy : réponse rapide, sans escalier
        vibSm = 0.0; vibCoef = std::exp (-1.0 / (0.03 * sr));
        sinceOnsetS = 1 << 30; notePeak = 0.0; noiseFloor = 1.0e-4;
        floorC = std::exp (-1.0 / (2.0 * sr));
        envLag = 0.0; lagC = std::exp (-1.0 / (0.25 * sr));
        candSince = 1 << 30; candPeak = 0.0;
        warmup = (int) (1.0 * sr);                 // 1 s pour mesurer le bruit de fond avant d'accepter un swell
        peakWindow = (int) (0.050 * sr);           // crête mesurée sur les 50 ms après l'attaque
        onsetValid = (int) (0.600 * sr);           // une attaque autorise un saut de note pendant 0,6 s
        stableHops = 14;                           // ~40 ms de hauteur stable (analyses toutes les 3 ms)
        pendingCount = 0;
    }

    // Traite un bloc mono (entrée guitare -> sortie mélangée).
    void process (const float* in, float* out, int numSamples, const Params& p)
    {
        if (std::abs (p.tone - lastTone) > 1.0e-6f) updateTone (p.tone);

        // glide : constante de temps de 2 ms à 700 ms (courbe exponentielle)
        const double glideTime = 0.002 * std::pow (350.0, (double) p.glide);
        const double glideCoef = std::exp (-1.0 / (glideTime * sr));

        const double vibRate   = 3.0 * std::pow (3.0, (double) p.speed);       // 3..9 Hz
        const double vibDepth  = 0.65 * std::pow ((double) p.vibrato, 1.4);     // jusqu'à ±0.65 demi-ton
        const double blendTgt  = p.blend;

        double bendTarget;
        switch (p.pedalMode)
        {
            case 1:  bendTarget = -p.expression * p.rangeSemis; break;               // DOWN
            case 2:  bendTarget = (p.expression * 2.0 - 1.0) * p.rangeSemis; break;  // BIPOLAR
            default: bendTarget =  p.expression * p.rangeSemis; break;               // UP
        }

        for (int i = 0; i < numSamples; ++i)
        {
            const double x = in[i];

            // --- suivi d'enveloppe de la guitare (antenne de volume) ---
            const double ax = std::abs (x);
            env = ax > env ? envAtt * env + (1.0 - envAtt) * ax
                           : envRel * env + (1.0 - envRel) * ax;

            // --- détection de hauteur ---
            // --- crête de la note en cours (sert à ignorer la fin de note et le ronflement) ---
            if (sinceOnsetS < (1 << 30)) ++sinceOnsetS;
            if (warmup > 0) --warmup;
            // attaque candidate : on mesure sa crête pendant 50 ms, puis on la valide seulement
            // si elle dépasse franchement le bruit de fond (le clic d'une corde étouffée ne compte pas)
            if (candSince < peakWindow)
            {
                candPeak = std::max (candPeak, env);
                if (++candSince == peakWindow && candPeak > 8.0 * noiseFloor + 0.002)
                {
                    notePeak = candPeak;
                    sinceOnsetS = peakWindow;
                }
            }
            // plancher de bruit (ronflement, souffle) : suit l'enveloppe vers le bas tout de suite,
            // remonte lentement seulement quand aucune note ne sonne vraiment
            const bool noteActive = haveNote && env > 0.03 * notePeak;
            if (env < noiseFloor) noiseFloor = env;
            else if (! noteActive) noiseFloor = floorC * noiseFloor + (1.0 - floorC) * env;
            envLag = lagC * envLag + (1.0 - lagC) * env;
            if (haveNote && candSince >= peakWindow && env > notePeak) notePeak = env;   // crête réelle de la note (swell compris)      // enveloppe « en retard » : sert à repérer un swell qui monte

            if (tracker.push ((float) x) && ! p.hold)
            {
                if (tracker.takeOnset()) { pendingCount = 0; candSince = 0; candPeak = env; }
                if (tracker.hasPitch() && tracker.getConfidence() > 0.5 && env > 0.0025)
                {
                    const double m = tracker.getMidi();
                    const double dist = std::abs (m - targetMidi);
                    const bool jump = ! haveNote || dist > 0.6;
                    bool accept = true;
                    if (jump)
                    {
                        // un changement de note doit être confirmé par 2 analyses d'accord (3 ms d'écart) :
                        // évite les « pépins » au moment de l'attaque du médiator
                        if (pendingCount > 0 && std::abs (m - pendingMidi) < 0.5) ++pendingCount;
                        else { pendingMidi = m; pendingCount = 1; }
                        accept = pendingCount >= 2;

                        // une vraie nouvelle note vient d'une attaque (médiator), d'un bend / slide / hammer-on
                        // proche, ou d'une hauteur stable 40 ms pendant que la corde sonne encore fort.
                        // Le ronflement secteur et la fin de note qui s'éteint ne remplacent jamais la note jouée.
                        // une attaque ne compte que si la note qui suit dépasse franchement le bruit de fond
                        // (le clic d'une corde qu'on étouffe ne doit pas « lancer » le ronflement)
                        const bool candLoud    = candSince < peakWindow && candPeak > 8.0 * noiseFloor + 0.002
                                                 && env > 0.25 * candPeak;
                        const bool recentOnset = candLoud || sinceOnsetS < onsetValid;
                        const bool nearMove    = haveNote && dist <= 2.6 && env > 0.05 * notePeak;
                        const bool stillStrong = notePeak > 0.0 && env > 0.15 * notePeak && pendingCount >= stableHops;
                        // attaque douce (swell au volume, doigts) : niveau qui MONTE, bien au-dessus du bruit de fond
                        const bool softStart   = warmup == 0 && env > 8.0 * noiseFloor + 0.002 && env > 1.4 * envLag
                                                 && pendingCount >= stableHops;
                        if (! (recentOnset || nearMove || stillStrong || softStart)) accept = false;
                        else if (candLoud) notePeak = candPeak;   // nouvelle note : nouvelle crête
                        else if (softStart && ! recentOnset && ! stillStrong) { sinceOnsetS = 0; candSince = peakWindow; notePeak = env; }
                    }
                    else pendingCount = 0;

                    if (accept)
                    {
                        if (jump) vibDepthRamp = 0.0;               // nouvelle note : le vibrato repart doucement
                        if (! haveNote) curMidi = m;                // première note : pas de glissando depuis nulle part
                        targetMidi = m;
                        haveNote = true;
                    }
                }
            }

            // --- niveau de la voix ---
            double ampTarget;
            if (p.hold)
            {
                if (! wasHold) heldAmp = std::max (voiceAmp, 0.25);
                ampTarget = heldAmp;
            }
            else
            {
                // compression douce de l'enveloppe : jeu dynamique mais voix chantante
                const double e = std::max (0.0, env - 0.0015);
                ampTarget = haveNote ? std::min (1.0, std::pow (e * 6.0, 0.6)) : 0.0;
                // la voix s'éteint quand la note est retombée à -34 dB sous son attaque
                // (sinon le bruit / ronflement de la guitare la maintient en vie)
                const double rel = env / std::max (notePeak, 1.0e-6);
                ampTarget *= std::clamp ((rel - 0.02) / 0.03, 0.0, 1.0);
                // et jamais au niveau du bruit de fond
                ampTarget *= std::clamp ((env / std::max (noiseFloor, 1.0e-7) - 2.0) / 2.0, 0.0, 1.0);
            }
            wasHold = p.hold;
            voiceAmp = ampTarget > voiceAmp ? ampAtt * voiceAmp + (1.0 - ampAtt) * ampTarget
                                            : ampRel * voiceAmp + (1.0 - ampRel) * ampTarget;

            // --- hauteur : note + octave + pédale, puis glide ---
            expSmooth = expCoef * expSmooth + (1.0 - expCoef) * bendTarget;
            // le glide suit la note jouée ; l'octave (whammy) a son propre lissage rapide
            curMidi = glideCoef * curMidi + (1.0 - glideCoef) * targetMidi;
            octSm   = octCoef * octSm + (1.0 - octCoef) * (double) p.octaveSemis;

            // --- vibrato humanisé (vitesse et profondeur qui respirent) ---
            rateJit  = 0.9995 * rateJit  + 0.025 * rng.next();
            depthJit = 0.9997 * depthJit + 0.02  * rng.next();
            vibDepthRamp = std::min (1.0, vibDepthRamp + 1.0 / (0.35 * sr));
            vibPhase += vibRate * (1.0 + 0.08 * rateJit) / sr;
            if (vibPhase >= 1.0) vibPhase -= 1.0;
            vibSm = vibCoef * vibSm + (1.0 - vibCoef) * vibDepth;
            const double vib = vibSm * vibDepthRamp * (1.0 + 0.25 * depthJit) * std::sin (kTwoPi * vibPhase);

            // dérive lente type oscillateurs hétérodynes (quelques cents)
            driftA = 0.99995 * driftA + 0.00005 * rng.next();
            driftB = 0.9995  * driftB + 0.0005  * driftA;
            const double drift = driftB * 10.0;   // ~±3 cents

            const double m  = curMidi + octSm + expSmooth + vib + drift;
            const double hz = std::clamp (midiToHz (m), 20.0, sr * 0.45);

            // --- oscillateur additif sans repliement ---
            phase += hz / sr;
            if (phase >= 1.0) phase -= std::floor (phase);
            const double th = kTwoPi * phase;
            const double s1 = std::sin (th), c1 = std::cos (th);
            double sk = s1, skm1 = 0.0, osc = harmAmp[0] * s1;
            const double twoC = 2.0 * c1;
            const int maxH = std::min (kNumHarm, (int) ((sr * 0.45) / hz));
            for (int k = 1; k < maxH; ++k)
            {
                const double sn = twoC * sk - skm1;   // sin((k+1)θ)
                skm1 = sk; sk = sn;
                osc += harmAmp[(size_t) k] * sn;
            }
            osc *= harmNorm;

            double v = formant.process (osc);
            v = voiceLP.process (voiceHP.process (v));
            v *= voiceAmp;
            lastVoice = (float) v;

            // --- mélange à puissance constante ---
            blendSm = blendCoef * blendSm + (1.0 - blendCoef) * blendTgt;
            const double gd = std::cos (blendSm * kPi * 0.5);
            const double gv = std::sin (blendSm * kPi * 0.5) * 0.75;
            out[i] = (float) (x * gd + v * gv);
        }

        displayMidi  = curMidi + octSm + expSmooth;
        displayAmp   = voiceAmp;
        noteDetected = haveNote && voiceAmp > 0.01;
    }

   #ifdef FANTO_DEBUG
    void tracker_dbg (bool on) { tracker.fantoDebug = on; }
   #endif
    // infos pour l'interface
    double displayMidi = 57.0, displayAmp = 0.0;
    bool   noteDetected = false;
    float  lastVoice = 0.0f;
    const std::array<double, 12>& getHarmonics() const { return harmAmp; }

private:
    static constexpr int kNumHarm = 12;

    void updateTone (float t)
    {
        lastTone = t;
        // pente spectrale : 1/k^p, p de 3.6 (quasi sinus) à 1.15 (anche)
        const double p = 3.6 - 2.45 * t;
        double sum = 0.0;
        for (int k = 0; k < kNumHarm; ++k)
        {
            const double n = k + 1.0;
            double a = 1.0 / std::pow (n, p);
            // léger déséquilibre pair/impair : caractère "hétérodyne"
            if ((k + 1) % 2 == 0) a *= 0.75 + 0.35 * t;
            if (k > 0) a *= std::min (1.0, 0.15 + 1.6 * t);   // en position PURE, presque rien au-delà du fondamental
            harmAmp[(size_t) k] = a;
            sum += a;
        }
        harmNorm = 1.0 / std::max (1.0, sum * 0.85);
        formant.setPeak (sr, 1150.0, 1.1, 9.0 * t);            // formant "voix" croissant avec TONE
    }

    double sr = 48000.0;
    PitchTracker tracker;
    Biquad voiceHP, voiceLP, formant;
    std::array<double, kNumHarm> harmAmp {};
    double harmNorm = 1.0;
    float lastTone = -1.0f;

    double phase = 0.0, curMidi = 57.0, targetMidi = 57.0;
    bool haveNote = false, wasHold = false;
    double pendingMidi = 0.0; int pendingCount = 0;
    int sinceOnsetS = 1 << 30, peakWindow = 2400, onsetValid = 28800, stableHops = 14;
    double notePeak = 0.0, noiseFloor = 1.0, floorC = 0.0, envLag = 0.0, lagC = 0.0, candPeak = 0.0;
    int candSince = 1 << 30, warmup = 0;
    double env = 0.0, voiceAmp = 0.0, heldAmp = 0.0;
    double envAtt = 0, envRel = 0, ampAtt = 0, ampRel = 0;
    double expSmooth = 0.0, expCoef = 0.0, octSm = 0.0, octCoef = 0.0, vibSm = 0.0, vibCoef = 0.0;
    double vibPhase = 0.0, vibDepthRamp = 0.0, rateJit = 0.0, depthJit = 0.0;
    double driftA = 0.0, driftB = 0.0;
    double blendSm = 0.7, blendCoef = 0.0;
    Rng rng;
};

} // namespace fantomine
