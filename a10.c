#include <math.h>

void ADSR(float *sample, float attack, float decay, float sustain, float release) {
    static float envelope = 0.0f;
    static float phase = 0.0f;

    // Attack phase
    if (phase < attack) {
        envelope += 1.0f / attack;
        phase += 1.0f;
    }
    // Decay phase
    else if (phase < attack + decay) {
        envelope -= (envelope - sustain) / decay;
        phase += 1.0f;
    }
    // Sustain phase
    else if (phase < attack + decay + sustain) {
        phase += 1.0f;
    }
    // Release phase
    else {
        envelope -= envelope / release;
        phase += 1.0f;
    }

    *sample *= envelope;

    // Reset envelope and phase if necessary
    if (envelope <= 0.0f) {
        envelope = 0.0f;
        phase = 0.0f;
    }
}
