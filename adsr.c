#include <stdio.h>

typedef enum {
    ATTACK,
    DECAY,
    SUSTAIN,
    RELEASE,
    IDLE
} ADSRState;

typedef struct {
    ADSRState state;
    float attackTime;  // Attack time in seconds
    float decayTime;   // Decay time in seconds
    float sustainLevel; // Sustain level (0.0 to 1.0)
    float releaseTime; // Release time in seconds
    float sampleRate;  // Samples per second (e.g., 44100)
    float envelope;    // Current envelope value
    float attackRate;
    float decayRate;
    float releaseRate;
} ADSR;

void ADSR_init(ADSR* env, float attackTime, float decayTime, float sustainLevel, float releaseTime, float sampleRate) {
    env->state = IDLE;
    env->attackTime = attackTime;
    env->decayTime = decayTime;
    env->sustainLevel = sustainLevel;
    env->releaseTime = releaseTime;
    env->sampleRate = sampleRate;
    env->envelope = 0.0f;

    env->attackRate = 1.0f / (attackTime * sampleRate);
    env->decayRate = (1.0f - sustainLevel) / (decayTime * sampleRate);
    env->releaseRate = sustainLevel / (releaseTime * sampleRate);
}

void ADSR_noteOn(ADSR* env) {
    env->state = ATTACK;
}

void ADSR_noteOff(ADSR* env) {
    env->state = RELEASE;
}

float ADSR_process(ADSR* env) {
    switch (env->state) {
        case ATTACK:
            env->envelope += env->attackRate;
            if (env->envelope >= 1.0f) {
                env->envelope = 1.0f;
                env->state = DECAY;
            }
            break;
        case DECAY:
            env->envelope -= env->decayRate;
            if (env->envelope <= env->sustainLevel) {
                env->envelope = env->sustainLevel;
                env->state = SUSTAIN;
            }
            break;
        case SUSTAIN:
            // Stay at sustain level
            break;
        case RELEASE:
            env->envelope -= env->releaseRate;
            if (env->envelope <= 0.0f) {
                env->envelope = 0.0f;
                env->state = IDLE;
            }
            break;
        case IDLE:
            // Do nothing, envelope is at 0
            break;
    }
    return env->envelope;
}

int main() {
    // Initialize an ADSR envelope with 0.1s attack, 0.2s decay, 0.7 sustain level, and 0.3s release
    ADSR adsr;
    ADSR_init(&adsr, 0.1f, 0.2f, 0.7f, 0.3f, 44100.0f);

    // Trigger note on
    ADSR_noteOn(&adsr);

    // Simulate processing 100 samples
    for (int i = 0; i < 100; i++) {
        float envValue = ADSR_process(&adsr);
        printf("Sample %d: %f\n", i, envValue);
    }

    // Trigger note off after some time
    ADSR_noteOff(&adsr);

    // Simulate more samples
    for (int i = 0; i < 100; i++) {
        float envValue = ADSR_process(&adsr);
        printf("Sample %d: %f\n", i + 100, envValue);
    }

    return 0;
}
