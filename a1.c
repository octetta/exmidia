#include <stdint.h>
#include <stdbool.h>

#define SAMPLE_RATE 44100

typedef struct {
    uint16_t levels[8];   // 16-bit levels for each stage (0-65535)
    uint16_t rates[8];    // 16-bit rates for each stage (rate per sample)
    int sustain_stage;    // Which stage is the sustain (0-7, -1 if none)
    int release_stage;    // Which stage is the release (0-7)
    int current_stage;    // Current stage in the envelope
    uint32_t step_count;  // Number of steps in the current stage
    uint32_t current_level; // Current level of the envelope
    bool released;        // Has the release been triggered
} Envelope;

// Initialize the envelope
void init_envelope(Envelope *env, uint16_t *levels, uint16_t *rates, int sustain_stage, int release_stage) {
    for (int i = 0; i < 8; i++) {
        env->levels[i] = levels[i];
        env->rates[i] = rates[i];
    }
    env->sustain_stage = sustain_stage;
    env->release_stage = release_stage;
    env->current_stage = 0;
    env->step_count = 0;
    env->current_level = levels[0]; // Start at the level of stage 0
    env->released = false;
}

// Trigger the envelope (starts the attack phase)
void trigger_envelope(Envelope *env) {
    env->current_stage = 0;
    env->step_count = 0;
    env->current_level = env->levels[0];
    env->released = false;
}

// Trigger the release phase of the envelope
void release_envelope(Envelope *env) {
    env->released = true;
    if (env->release_stage >= 0 && env->release_stage < 8) {
        env->current_stage = env->release_stage;
        env->step_count = 0;
    }
}

// Get the next value in the envelope
uint16_t next_envelope_value(Envelope *env) {
    if (env->current_stage >= 8) {
        return env->current_level; // If we're done, just return the current level
    }

    // Advance the envelope by one step
    uint32_t target_level = env->levels[env->current_stage];
    if (env->current_level != target_level) {
        if (env->current_level < target_level) {
            env->current_level += env->rates[env->current_stage];
            if (env->current_level > target_level) {
                env->current_level = target_level;
            }
        } else {
            env->current_level -= env->rates[env->current_stage];
            if (env->current_level < target_level) {
                env->current_level = target_level;
            }
        }
    }

    env->step_count++;

    // Check if the stage is complete
    if (env->current_level == target_level) {
        if (!env->released && env->current_stage == env->sustain_stage) {
            return env->current_level; // Hold at sustain level
        }

        // Move to the next stage
        env->current_stage++;
        env->step_count = 0;

        // If we're done with all stages, return the final level
        if (env->current_stage >= 8) {
            return env->levels[7];
        }
    }

    return env->current_level;
}

#include <stdio.h>

int16_t levels[8] = {
    0,     // Stage 0: Initial level (silence before keypress)
    32767, // Stage 1: Attack peak (maximum volume)
    16384, // Stage 2: Decay to sustain level
    8192,  // Stage 3: Lower sustain level
    8192,  // Stage 4: Sustained level while the key is held
    8192,  // Stage 5: Sustained level (duplicate of stage 4)
    8192,  // Stage 6: Sustained level (optional duplicate)
    0      // Stage 7: Release to silence
};

uint16_t rates[8] = {
    5000,  // Stage 0: Fast attack (sharp rise in volume)
    5000,  // Stage 1: Moderate attack rate
    1000,  // Stage 2: Slower decay rate to sustain level
    0,     // Stage 3: Hold at sustain level (no change in level)
    0,     // Stage 4: Sustain while key is held (no change in level)
    0,     // Stage 5: Sustain (duplicate)
    0,     // Stage 6: Sustain (duplicate)
    1000   // Stage 7: Fast release rate (fade out)
};


int main() {
  Envelope env;
  init_envelope(&env, levels, rates, 4, 7);  // Sustain on stage 4, release starts at stage 7
  trigger_envelope(&env);  // Start the envelope when the key is pressed

  // On key release:
  release_envelope(&env);  // Trigger the release phase

  return 0;
}
