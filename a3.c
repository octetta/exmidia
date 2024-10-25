#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>

#define SAMPLE_RATE 44100

typedef struct {
    int16_t levels[8];
    uint32_t rates[8];
    int sustain_stage;
    int release_stage;
    int current_stage;
    int32_t current_level;
    int32_t delta;
    uint32_t step_count;
    bool triggered;
    bool released;
} Envelope;

void init_envelope(Envelope *env, int16_t *levels, uint32_t *rates, int sustain_stage, int release_stage) {
    for (int i = 0; i < 8; i++) {
        env->levels[i] = levels[i];
        env->rates[i] = rates[i];
    }
    env->sustain_stage = sustain_stage;
    env->release_stage = release_stage;
    env->current_stage = 0;
    env->step_count = 0;
    env->current_level = levels[0];
    env->triggered = false;
    env->released = false;
    env->delta = 0;
}

void trigger_envelope(Envelope *env) {
    env->current_stage = 0;
    env->step_count = 0;
    env->current_level = env->levels[0];
    env->released = false;
    env->triggered = true;

    if (env->rates[0] != 0) {
        env->delta = (env->levels[1] - env->levels[0]) / (int32_t)env->rates[0];
    }
}

void release_envelope(Envelope *env) {
    env->released = true;
    env->current_stage = env->release_stage;
    env->step_count = 0;

    if (env->release_stage < 7) {
        env->delta = (env->levels[env->release_stage + 1] - env->levels[env->release_stage]) / (int32_t)env->rates[env->release_stage];
    }
}

int16_t next_envelope_value(Envelope *env) {
    if (!env->triggered) {
        return 0;
    }

    env->step_count++;

    if (env->step_count >= env->rates[env->current_stage]) {
        env->step_count = 0;
        env->current_stage++;

        if (env->current_stage >= 8) {
            env->current_level = env->levels[7];
            return env->current_level;
        }

        // Update delta for the next stage
        if (env->current_stage < 7) {
            env->delta = (env->levels[env->current_stage + 1] - env->levels[env->current_stage]) / (int32_t)env->rates[env->current_stage];
        }
    }

    env->current_level += env->delta;

    return (int16_t)env->current_level;
}

int main() {
    // Example level and rate arrays
    int16_t levels[8] = {0, 16384, 32767, 16384, 8192, 4096, 2048, 0};
    uint32_t rates[8] = {22050, 110250, 44100, 88200, 132300, 176400, 220500, 44100};

    // Initialize the envelope
    Envelope env;
    init_envelope(&env, levels, rates, 2, 6);  // Sustain at stage 2, release at stage 6

    // Trigger the envelope
    trigger_envelope(&env);

    // Simulate 10 seconds of envelope output
    for (int i = 0; i < SAMPLE_RATE * 10; i++) {  // Simulate 10 seconds of audio
        int16_t envelope_value = next_envelope_value(&env);

        // Print every 1/10th second to track envelope progress
        if (i % (SAMPLE_RATE / 10) == 0) {
            printf("Time %.1f sec: Envelope Value = %d\n", (float)i / SAMPLE_RATE, envelope_value);
        }

        // Release after 5 seconds
        if (i == SAMPLE_RATE * 5) {
            release_envelope(&env);
        }
    }

    return 0;
}
