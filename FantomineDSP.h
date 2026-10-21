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
// Détecteur de hauteur YIN monophonique, travaillant sur un signal décimé.
class PitchTracker
{
public:
    void prepare (double sampleRate)
    {
        decim = std::max (1, (int) std::lround (sampleRate / 12000.0));
        dsr = sampleRate / decim;

        hp.setHighpass (sampleRate, 55.0, 0.707);
        lp1.setLowpass (sampleRate, 1800.0, 0.54);
        lp2.setLowpass (sampleRate, 1800.0, 1.31);
        hp.reset(); lp1.reset(); lp2.reset();

        windowSize = (int) std::lround (dsr * 0.040);          // 40 ms
        maxTau     = std::min (windowSize - 2, (int) (dsr / 62.0)); // jusqu'à ~62 Hz (Ré grave)
        minTau     = std::max (2, (int) (dsr / 1400.0));
        hop        = std::max (16, (int) std::lround (dsr * 0.005)); // 5 ms

        ring.assign ((size_t) (windowSize + maxTau + 8), 0.0f);
        diff.assign ((size_t) maxTau + 2, 0.0f);
        frame.assign (ring.size(), 0.0f);
        writePos = 0; counter = 0; hopCounter = 0;
        hist[0] = hist[1] = hist[2] = 0.0;
        histCount = 0;
        detected = false; midi = 0.0; confidence = 0.0;
    }

    // Renvoie true quand une nouvelle analyse vient d'être faite.
    bool push (float x)
    {
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

private:
    void analyse()
    {
        const int N = (int) ring.size();
        const int len = windowSize + maxTau;
        int start = writePos - len; while (start < 0) start += N;
        double energy = 0.0;
        for (int i = 0; i < len; ++i)
        {
            const float v = ring[(size_t) ((start + i) % N)];
            frame[(size_t) i] = v;
            energy += (double) v * v;
        }

        if (energy / len < 1.0e-7)           // silence : on oublie la note
        {
            detected = false; confidence = 0.0; histCount = 0;
            return;
        }

        // fonction de différence (YIN étape 2) + normalisation cumulée (étape 3)
        diff[0] = 1.0f;
        double running = 0.0;
        const float* f = frame.data();
        for (int tau = 1; tau <= maxTau; ++tau)
        {
            double s = 0.0;
            for (int i = 0; i < windowSize; ++i)
            {
                const double d = (double) f[i] - (double) f[i + tau];
                s += d * d;
            }
            running += s;
            diff[(size_t) tau] = (float) (running > 0.0 ? s * tau / running : 1.0);
        }

        // seuil absolu (étape 4)
        int best = -1;
        const float threshold = 0.15f;
        for (int tau = minTau; tau < maxTau; ++tau)
        {
            if (diff[(size_t) tau] < threshold)
            {
                while (tau + 1 < maxTau && diff[(size_t) tau + 1] < diff[(size_t) tau]) ++tau;
                best = tau;
                break;
            }
        }
        if (best < 0)
        {
            // pas de minimum franc : on garde la note précédente mais la confiance baisse
            confidence *= 0.6;
            if (confidence < 0.2) detected = false;
            return;
        }

        // interpolation parabolique (étape 5)
        double betterTau = best;
        if (best > 1 && best < maxTau - 1)
        {
            const double s0 = diff[(size_t) best - 1], s1 = diff[(size_t) best], s2 = diff[(size_t) best + 1];
            const double den = 2.0 * (2.0 * s1 - s2 - s0);
            if (std::abs (den) > 1e-12) betterTau = best + (s2 - s0) / den;
        }

        const double hz = dsr / betterTau;
        const double m  = hzToMidi (hz);
        const double c  = 1.0 - diff[(size_t) best];

        // médiane sur 3 analyses pour rejeter les sauts d'octave isolés
        hist[2] = hist[1]; hist[1] = hist[0]; hist[0] = m;
        histCount = std::min (3, histCount + 1);
        double med = m;
        if (histCount == 3)
        {
            double a = hist[0], b = hist[1], d = hist[2];
            med = std::max (std::min (a, b), std::min (std::max (a, b), d));
            if (std::abs (m - med) < 0.5) med = m;   // note stable : on garde la plus récente
        }

        midi = med;
        confidence = c;
        detected = true;
    }

    Biquad hp, lp1, lp2;
    int decim = 4, windowSize = 480, maxTau = 190, minTau = 8, hop = 60;
    double dsr = 12000.0;
    std::vector<float> ring, diff, frame;
    int writePos = 0, counter = 0, hopCounter = 0;
    double hist[3] {};
    int histCount = 0;
    bool detected = false;
    double midi = 0.0, confidence = 0.0;
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
    int   octave     = 0;      // -1, 0, +1
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

        voiceHP.setHighpass (sr, 70.0, 0.707);
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
            if (tracker.push ((float) x) && ! p.hold)
            {
                if (tracker.hasPitch() && tracker.getConfidence() > 0.5 && env > 0.0025)
                {
                    const double m = tracker.getMidi();
                    if (! haveNote || std::abs (m - targetMidi) > 0.7)
                        vibDepthRamp = 0.0;                         // nouvelle note : le vibrato repart doucement
                    if (! haveNote) curMidi = m;                    // première note : pas de glissando depuis nulle part
                    targetMidi = m;
                    haveNote = true;
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
            }
            wasHold = p.hold;
            voiceAmp = ampTarget > voiceAmp ? ampAtt * voiceAmp + (1.0 - ampAtt) * ampTarget
                                            : ampRel * voiceAmp + (1.0 - ampRel) * ampTarget;

            // --- hauteur : note + octave + pédale, puis glide ---
            expSmooth = expCoef * expSmooth + (1.0 - expCoef) * bendTarget;
            const double goal = targetMidi + 12.0 * p.octave;
            curMidi = glideCoef * curMidi + (1.0 - glideCoef) * goal;

            // --- vibrato humanisé (vitesse et profondeur qui respirent) ---
            rateJit  = 0.9995 * rateJit  + 0.025 * rng.next();
            depthJit = 0.9997 * depthJit + 0.02  * rng.next();
            vibDepthRamp = std::min (1.0, vibDepthRamp + 1.0 / (0.35 * sr));
            vibPhase += vibRate * (1.0 + 0.08 * rateJit) / sr;
            if (vibPhase >= 1.0) vibPhase -= 1.0;
            const double vib = vibDepth * vibDepthRamp * (1.0 + 0.25 * depthJit) * std::sin (kTwoPi * vibPhase);

            // dérive lente type oscillateurs hétérodynes (quelques cents)
            driftA = 0.99995 * driftA + 0.00005 * rng.next();
            driftB = 0.9995  * driftB + 0.0005  * driftA;
            const double drift = driftB * 10.0;   // ~±3 cents

            const double m  = curMidi + expSmooth + vib + drift;
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

        displayMidi  = curMidi + expSmooth;
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
    double env = 0.0, voiceAmp = 0.0, heldAmp = 0.0;
    double envAtt = 0, envRel = 0, ampAtt = 0, ampRel = 0;
    double expSmooth = 0.0, expCoef = 0.0;
    double vibPhase = 0.0, vibDepthRamp = 0.0, rateJit = 0.0, depthJit = 0.0;
    double driftA = 0.0, driftB = 0.0;
    double blendSm = 0.7, blendCoef = 0.0;
    Rng rng;
};

} // namespace fantomine
