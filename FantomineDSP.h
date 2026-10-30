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

        ring.assign ((size_t) (windowSize + maxTau + 16), 0.0f);
        diff.assign ((size_t) maxTau + 2, 0.0f);
        raw.assign ((size_t) maxTau + 2, 0.0f);
        frame.assign (ring.size(), 0.0f);
        writePos = 0; counter = 0; hopCounter = 0;
        hist[0] = hist[1] = hist[2] = 0.0;
        histCount = 0;
        detected = false; midi = 0.0; confidence = 0.0;
        fastEnv = slowEnv = 0.0;
        onsetHP.setHighpass (sampleRate, 1500.0, 0.707);
        onsetHP2.setHighpass (sampleRate, 1500.0, 0.707);
        onsetHP.reset(); onsetHP2.reset();
        fastC = std::exp (-1.0 / (0.004 * sampleRate));
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
        slowEnv = slowC * slowEnv + (1.0 - slowC) * h;
        if (sinceOnset < (1 << 30)) ++sinceOnset;
        if (sinceOnset > refractory && fastEnv > 3.0 * slowEnv + 2.0e-4)
        {
            sinceOnset = 0;
            histCount = 0;            // l'ancienne note ne vote plus dans la médiane
            onsetFlag = true;
        }

        double y = lp2.process (lp1.process (hp.process (x)));
        if (++counter < decim) return false;
        counter = 0;

        ring[(size_t) writePos] = (float) y;
        writePos = (writePos + 1) % (int) ring.size();

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
            return;
        }

        // fonction de différence (YIN étape 2) + normalisation cumulée (étape 3)
        diff[0] = 1.0f;
        double running = 0.0;
        const float* f = frame.data();
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
            if (diff[(size_t) g] > 0.40f || W < windowSize / 2) { confidence *= 0.6; if (confidence < 0.2) detected = false; return; }
            // préfère le plus petit sous-multiple presque aussi bon (évite l'octave en dessous)
            best = g;
            for (int k = 4; k >= 2; --k)
            {
                const int t = localMin (std::max (minTau, (int) std::lround ((double) g / k)));
                if (std::abs (t * k - g) <= 2 + g / 40 && diff[(size_t) t] < diff[(size_t) g] + 0.06f) { best = t; break; }
            }
        }
        else
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

        // trop tôt pour exclure une période deux fois plus longue : on attend un peu plus de signal
        if (best * 2 > tauLim && tauLim < maxTau && diff[(size_t) best] > 0.05f) return;

        // interpolation parabolique (étape 5)
        double betterTau = best;
        if (best > 1 && best < tauLim - 1)
        {
            const double s0 = diff[(size_t) best - 1], s1 = diff[(size_t) best], s2 = diff[(size_t) best + 1];
            const double den = 2.0 * (2.0 * s1 - s2 - s0);
            if (std::abs (den) > 1e-12) betterTau = best + (s2 - s0) / den;
        }

        const double m = hzToMidi (dsr / betterTau);
        const double c = 1.0 - diff[(size_t) best];
       #ifdef FANTO_DEBUG
        if (fantoDebug) printf ("      [W=%d lim=%d best=%d d'=%.3f m=%.2f path=%d]\n", W, tauLim, best, diff[(size_t) best], m, dbgPath);
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

    Biquad hp, lp1, lp2, onsetHP, onsetHP2;
    int decim = 4, windowSize = 384, minWindow = 72, maxTau = 363, minTau = 8, hop = 36;
    double dsr = 12000.0;
    std::vector<float> ring, diff, raw, frame;
    int writePos = 0, counter = 0, hopCounter = 0;
    double hist[3] {};
    int histCount = 0;
    bool detected = false;
    double midi = 0.0, confidence = 0.0;
    double fastEnv = 0.0, slowEnv = 0.0, fastC = 0.0, slowC = 0.0;
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
