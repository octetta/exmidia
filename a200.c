#include <stdint.h>
#include <stdbool.h>

// Fixed point configuration
#define FP_BITS 15
#define FP_SCALE (1 << FP_BITS)
#define FP_MAX INT32_MAX

typedef enum {
    ENV_IDLE,
    ENV_ATTACK,
    ENV_DECAY,
    ENV_SUSTAIN,
    ENV_RELEASE
} EnvelopeStage;

typedef struct {
    // Configuration (all in fixed point)
    int32_t attack_rate;   
    int32_t decay_rate;    
    int32_t release_rate;  
    int32_t attack_level;  
    int32_t sustain_level;
    
    // State
    EnvelopeStage stage;
    int32_t current_level;
    bool note_on;
} Envelope;

void envelope_init(Envelope* env, 
                  uint32_t attack_ms,
                  uint32_t decay_ms,
                  uint32_t release_ms,
                  uint32_t sample_rate) {
    // Set target levels in fixed point
    env->attack_level = FP_SCALE;  // 1.0 in fixed point
    env->sustain_level = (FP_SCALE * 7) / 10;  // 0.7 in fixed point
    
    // Calculate number of samples for each phase
    uint32_t attack_samples = (attack_ms * sample_rate) / 1000;
    uint32_t decay_samples = (decay_ms * sample_rate) / 1000;
    uint32_t release_samples = (release_ms * sample_rate) / 1000;
    
    // Calculate rates ensuring we don't get zero due to fixed point math
    // Rate = target_change_in_level / num_samples
    env->attack_rate = env->attack_level / attack_samples;
    if (env->attack_rate == 0) env->attack_rate = 1;  // Ensure minimum rate
    
    env->decay_rate = (env->attack_level - env->sustain_level) / decay_samples;
    if (env->decay_rate == 0) env->decay_rate = 1;
    
    env->release_rate = env->sustain_level / release_samples;
    if (env->release_rate == 0) env->release_rate = 1;
    
    env->stage = ENV_IDLE;
    env->current_level = 0;
    env->note_on = false;
}

void envelope_note_on(Envelope* env) {
    env->note_on = true;
    env->stage = ENV_ATTACK;
}

void envelope_note_off(Envelope* env) {
    env->note_on = false;
    env->stage = ENV_RELEASE;
}

int16_t envelope_process(Envelope* env) {
    switch (env->stage) {
        case ENV_IDLE:
            env->current_level = 0;
            break;
            
        case ENV_ATTACK:
            env->current_level += env->attack_rate;
            if (env->current_level >= env->attack_level) {
                env->current_level = env->attack_level;
                env->stage = ENV_DECAY;
            }
            break;
            
        case ENV_DECAY:
            env->current_level -= env->decay_rate;
            if (env->current_level <= env->sustain_level) {
                env->current_level = env->sustain_level;
                env->stage = ENV_SUSTAIN;
            }
            break;
            
        case ENV_SUSTAIN:
            if (!env->note_on) {
                env->stage = ENV_RELEASE;
            }
            break;
            
        case ENV_RELEASE:
            env->current_level -= env->release_rate;
            if (env->current_level <= 0) {
                env->current_level = 0;
                env->stage = ENV_IDLE;
            }
            break;
    }
    
    // Convert to 16-bit signed integer range
    return (int16_t)((env->current_level * 32767) >> FP_BITS);
}

// Debug function to print envelope state
void envelope_print_debug(Envelope* env) {
    printf("Stage: %d\n", env->stage);
    printf("Current Level: %ld\n", env->current_level);
    printf("Attack Rate: %ld\n", env->attack_rate);
    printf("Attack Level: %ld\n", env->attack_level);
    printf("Sustain Level: %ld\n", env->sustain_level);
}

#include <stdio.h>

int main() {
    Envelope env;
    envelope_init(&env, 5, 1000, 1000, 44100);
    
    // Print initial state
    printf("Initial state:\n");
    envelope_print_debug(&env);
    
    // Start note
    envelope_note_on(&env);
    
    int count = 44100;
    // Print first few samples
    printf("\nFirst %d samples:\n", count);
    for (int i = 0; i < count; i++) {
        int16_t value = envelope_process(&env);
        printf("Sample %d: %d\n", i, value);
    }
    puts("off");
    envelope_note_off(&env);
    for (int i = 0; i < count; i++) {
        int16_t value = envelope_process(&env);
        printf("Sample %d: %d\n", i, value);
    }
    
    // Print state after 10 samples
    printf("\nAfter 10 samples:\n");
    envelope_print_debug(&env);
    
    return 0;
}
