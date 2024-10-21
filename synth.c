#include <stdio.h>
#include <stdint.h>
#include <math.h>
#include <alsa/asoundlib.h>
#include <pthread.h>

#include <sys/time.h>

#define SAMPLE_RATE 44100
#define CHUNK_SIZE 4096 // must be a power of 2 for the mask below to work
#define PHASE_MASK (CHUNK_SIZE - 1)
#define PERIOD_SIZE 1024  // Number of samples per ALSA period

#define VOICES (24)

struct timeval rtns0;
struct timeval rtns1;

// DDS structure
typedef struct {
    uint32_t phase_accumulator;
    uint32_t phase_increment;
} DDS;

// DDS instance
DDS dds[VOICES];

// ALSA variables
snd_pcm_t *pcm_handle;
snd_pcm_hw_params_t *hw_params;

// Wavetable for sine wave
int16_t sine[CHUNK_SIZE];
int16_t cosine[CHUNK_SIZE];
int16_t sqr[CHUNK_SIZE];
int16_t tri[CHUNK_SIZE];
int16_t sawup[CHUNK_SIZE];
int16_t sawdown[CHUNK_SIZE];
int16_t noise[CHUNK_SIZE];
int16_t none[CHUNK_SIZE];
int16_t usr0[CHUNK_SIZE];
int16_t usr1[CHUNK_SIZE];
int16_t usr2[CHUNK_SIZE];
int16_t usr3[CHUNK_SIZE];
int16_t usr4[CHUNK_SIZE];

#define MAX_VALUE 32767
#define MIN_VALUE -32767

void generate_sine(int16_t *table, int size) {
    for (int i = 0; i < size; i++) {
        table[i] = (int16_t)(MAX_VALUE * sinf(2.0f * M_PI * i / size));
    }
}

void generate_cosine(int16_t *table, int size) {
    for (int i = 0; i < size; i++) {
        table[i] = (int16_t)(MAX_VALUE * cosf(2.0f * M_PI * i / size));
    }
}

void generate_sqr(int16_t *table, int size) {
    for (int i = 0; i < size; i++) {
        if (i < (size/2)) {
            table[i] = MAX_VALUE;
        } else {
            table[i] = MIN_VALUE;
        }
    }
}

void generate_tri(int16_t *table, int size) {
    int quarter = size / 4;
    for (int i = 0; i < size; i++) {
        if (i < quarter) {
            table[i] = (4 * MAX_VALUE * i) / size;
        } else if (i < 3 * quarter) {
            table[i] = MAX_VALUE - (4 * MAX_VALUE * (i - quarter)) / size;
        } else {
            table[i] = MIN_VALUE + (4 * MAX_VALUE * (i - 3 * quarter)) / size;
        }
    }
}

void generate_sawup(int16_t *table, int size) {
    int acc = 0;
    int rate = MAX_VALUE/size;
    for (int i = 0; i < size; i++) {
        table[i] = acc;
        acc += rate;
    }
}

void generate_sawdown(int16_t *table, int size) {
    int acc = MAX_VALUE;
    int rate = MAX_VALUE/size;
    for (int i = 0; i < size; i++) {
        table[i] = acc;
        acc -= rate;
    }
}

void generate_noise(int16_t *table, int size) {
    for (int i = 0; i < size; i++) {
        table[i] = ((double)rand()/(double)RAND_MAX - 0.5) * MAX_VALUE;
    }
}

void generate_none(int16_t *table, int size) {
    for (int i = 0; i < size; i++) {
        table[i] = 0;
    }
}

typedef struct {
    int id;
    int16_t *wavetable;
    int samples;
    int samplerate;
} wave_t;

typedef struct {
    int id;
    int waveindex;
    int16_t *wavetable;
    int samples;
    int nextwaveindex; // pointer to next or self or -1 for stop
    double ifreq;
    int phase_accumulator;
    int phase_increment;
    double iamp;
    int ratio_top;
    int ratio_bot;
} voice_t;

// DDS initialization
void dds_init(DDS *dds, double output_frequency) {
    dds->phase_accumulator = 0;
    dds->phase_increment = (uint32_t)((output_frequency * CHUNK_SIZE) / SAMPLE_RATE * (1 << 8));
}

void dds_mod(DDS *dds, double new_frequency) {
    dds->phase_increment = (uint32_t)((new_frequency * CHUNK_SIZE) / SAMPLE_RATE * (1 << 8));
}

// DDS step to get the next sample
int16_t dds_step(DDS *dds, int16_t *wavetable) {
    uint16_t index = dds->phase_accumulator >> 8;
    int16_t sample = wavetable[index & PHASE_MASK];
    dds->phase_accumulator += dds->phase_increment;
    return sample;
}

// ALSA error handler
void check_alsa_error(int err, const char *msg) {
    if (err < 0) {
        fprintf(stderr, "%s: %s\n", msg, snd_strerror(err));
        exit(EXIT_FAILURE);
    }
}

// ALSA setup
int setup_alsa(char *device) {
    int err;

    // Open ALSA device for playback
    if ((err = snd_pcm_open(&pcm_handle, device, SND_PCM_STREAM_PLAYBACK, 0)) < 0) {
        check_alsa_error(err, "Cannot open PCM device");
        return -1;
    }

    // Allocate hardware parameters
    snd_pcm_hw_params_malloc(&hw_params);
    snd_pcm_hw_params_any(pcm_handle, hw_params);

    // Set hardware parameters
    snd_pcm_hw_params_set_access(pcm_handle, hw_params, SND_PCM_ACCESS_RW_INTERLEAVED);
    snd_pcm_hw_params_set_format(pcm_handle, hw_params, SND_PCM_FORMAT_S16_LE);
    snd_pcm_hw_params_set_channels(pcm_handle, hw_params, 1);  // Mono output
    snd_pcm_hw_params_set_rate(pcm_handle, hw_params, SAMPLE_RATE, 0);
    snd_pcm_hw_params_set_period_size(pcm_handle, hw_params, PERIOD_SIZE, 0);

    // Apply hardware parameters
    if ((err = snd_pcm_hw_params(pcm_handle, hw_params)) < 0) {
        check_alsa_error(err, "Cannot set hardware parameters");
        return -1;
    }

    // Free hardware parameters structure
    snd_pcm_hw_params_free(hw_params);

    // Prepare PCM for playback
    snd_pcm_prepare(pcm_handle);
    return 0;
}

// TODO
int note[VOICES];
int gate[VOICES];
int note_active[VOICES];

double envelope(
    int *note_active, int gate, double *env_level, double t,
    double attack, double decay, double sustain, double release) {
    if (gate)  {
        if (t > attack + decay) return(*env_level = sustain);
        if (t > attack) return(*env_level = 1.0 - (1.0 - sustain) * (t - attack) / decay);
        return *env_level = t / attack;
    } else {
        if (t > release) {
            if (note_active) *note_active = 0;
            return *env_level = 0;
        }
        return *env_level * (1.0 - t / release);
    }
}

#define GAIN_TOP 1
#define GAIN_BOT 2

double params[1024];
char input[1024];

int running = 1;

#define WAVE_MAX (12)

double of[VOICES];
double on[VOICES];
double oa[VOICES];
int ow[VOICES];

// LFO-ey stuff
// TODO
int ofm[VOICES]; // choose which oscillator is a frequency modulator
int oam[VOICES]; // choose which oscillator is a amplitude modulator
int opm[VOICES]; // choose which oscillator is a panning modulator

// amplitude ratio... this influences the oa
int top[VOICES];
int bot[VOICES];

#include "linenoise.h"

unsigned long long sent = 0;

int agcd(int a, int b) {
    while (b != 0) {
        int temp = a % b;
        a = b;
        b = temp;
    }
    return a;
}

void calc_ratio(int index) {
    int precision = 10000;
    int ip = oa[index] * precision;
    int gcd = agcd(abs(ip), precision);
    top[index] = ip / gcd;
    bot[index] = precision / gcd;
}

long mytol(char *str, int *valid, int *next) {
    long val;
    char *endptr;
    val = strtol(str, &endptr, 10);
    if (endptr == str) {
        if (valid) *valid = 0;
        if (next) *next = 0;
        return 0;
    }
    if (valid) *valid = 1;
    if (next) *next = endptr - str + 1;
    return val;
}

double mytod(char *str, int *valid, int *next) {
    double val;
    char *endptr;
    val = strtod(str, &endptr);
    if (endptr == str) {
        if (valid) *valid = 0;
        if (next) *next = 0;
        return 0;
    }
    if (valid) *valid = 1;
    if (next) *next = endptr - str + 1;
    return val;
}

char *mytok(char *str, char tok, int *next) {
    int n = 0;
    while (1) {
        if (str[n] == tok) {
            str[n] = '\0';
            if (next) *next = n+1;
            return str;
        }
    }
}

long int rtms = 0;
long int btms = 0;
long int diff = 0;

#define LATENCY_HACK_MS (100)

int latency_hack_ms = LATENCY_HACK_MS;
char *device = "default";
int voice = 0;

void *midi(void *arg) {
    while (running) {
        sleep(5);
    }
}

// inspired by AMY :)
#define SINE 0
#define SQR  1
#define SAWD 2
#define SAWU 3
#define TRI  4
#define NOIZ 5
#define USR0 6
#define USR1 7
#define USR2 8
#define USR3 9
#define USR4 10
#define NONE 11

void dump(int16_t *wave) {
    int c = 0;
    char template[] = "waveXXXXXX";
    int fd = mkstemp(template);
    if (fd < 0) {
        puts("FAIL");
        perror("mkstemp");
        return;
    }
    printf("created %s\n", template);
    char buf[80];
    for (int i=0; i<CHUNK_SIZE; i++) {
        sprintf(buf, "%d\n", wave[i]);
        write(fd, buf, strlen(buf));
    }
    close(fd);
}

/////

#include <stdint.h>
#include <stddef.h>

#define SAMPLE_RATE 44100
#define FIXED_POINT_SHIFT 16

typedef struct {
    const int16_t *wave_table;  // Pointer to the wave table array
    size_t table_size;          // Size of the wave table (number of samples in one cycle)
    uint32_t phase_increment;   // Fixed-point phase increment per sample
    uint32_t phase_accumulator; // Fixed-point phase accumulator (current position in the table)
} WaveTableOscillator;

// Initialize the wave table oscillator with the given wave table and frequency
void init_wave_table_oscillator(WaveTableOscillator *osc, const int16_t *wave_table, size_t table_size, float frequency) {
    osc->wave_table = wave_table;
    osc->table_size = table_size;

    // Calculate the phase increment (fixed-point), scaling for both the frequency and the wave table size
    osc->phase_increment = (uint32_t)((frequency * table_size * (1 << FIXED_POINT_SHIFT)) / SAMPLE_RATE);
    
    // Initialize the phase accumulator to 0
    osc->phase_accumulator = 0;
}

// Change the playback frequency of the wave table oscillator
void set_wave_table_frequency(WaveTableOscillator *osc, float frequency) {
    // Recalculate the phase increment based on the new frequency
    osc->phase_increment = (uint32_t)((frequency * osc->table_size * (1 << FIXED_POINT_SHIFT)) / SAMPLE_RATE);
}


// Generate the next sample from the wave table
int16_t next_wave_table_sample(WaveTableOscillator *osc) {
    // Extract the integer part of the phase accumulator to get the current table index
    uint32_t index = osc->phase_accumulator >> FIXED_POINT_SHIFT;

    // Get the next table index, wrapping around if necessary
    uint32_t next_index = (index + 1) % osc->table_size;

    // Get the fractional part of the phase accumulator for interpolation
    uint32_t fractional = osc->phase_accumulator & ((1 << FIXED_POINT_SHIFT) - 1);

    // Perform linear interpolation between the current sample and the next sample
    int16_t current_sample = osc->wave_table[index];
    // int16_t next_sample = osc->wave_table[next_index];

    // int32_t interpolated_sample = current_sample + ((next_sample - current_sample) * fractional) / (1 << FIXED_POINT_SHIFT);

    // Increment the phase accumulator by the phase increment
    osc->phase_accumulator += osc->phase_increment;
    
    // Wrap the phase accumulator around if it exceeds the wave table size
    osc->phase_accumulator %= (osc->table_size << FIXED_POINT_SHIFT);

    // return (int16_t)interpolated_sample;

    return current_sample;
}


/////
#if 1

#include <stdint.h>
#include <stdbool.h>

#define SAMPLE_RATE 44100

typedef struct {
    int16_t levels[8];      // 16-bit signed levels for each stage (-32768 to 32767)
    uint32_t rates[8];      // 32-bit rate values for each stage (affects speed of transition)
    int sustain_stage;      // Which stage is the sustain (0-7, -1 if none)
    int release_stage;      // Which stage is the release (0-7)
    int current_stage;      // Current stage in the envelope
    int last_stage;
    uint32_t step_count;    // Current step within the stage
    int32_t current_level;  // Current level of the envelope (int32_t for intermediate calculation)
    bool released;          // Has the release been triggered
    bool triggered;         // Has the envelope been triggered
    bool gate;
    int32_t delta;          // The amount the level should change per sample
} Envelope;

// Initialize the envelope with levels and rates
void init_envelope(Envelope *env, int16_t *levels, uint32_t *rates, int sustain_stage, int release_stage) {
    for (int i = 0; i < 8; i++) {
        env->levels[i] = levels[i];
        env->rates[i] = rates[i];  // Now using 32-bit rates for longer transitions
    }
    env->sustain_stage = sustain_stage;
    env->release_stage = release_stage;
    env->current_stage = 0;
    env->step_count = 0;
    env->current_level = levels[0];  // Start at the level of stage 0
    env->released = false;
    env->triggered = false;
    env->delta = 0;  // No change until triggered
}

// Trigger the envelope (starts the attack phase)
void trigger_envelope(Envelope *env) {
    env->last_stage = -1;
    env->current_stage = 0;
    env->step_count = 0;
    env->current_level = env->levels[0];
    env->released = false;
    env->triggered = true;
    env->gate = true;

    // Calculate the change per sample for the first stage
    if (env->rates[0] != 0) {
        env->delta = (env->levels[1] - env->levels[0]) / (int32_t)env->rates[0];
    }
}

// Trigger the release phase of the envelope
void release_envelope(Envelope *env) {
    env->released = true;

    // Transition to the release stage if it's valid
    if (env->release_stage >= 0 && env->release_stage < 8) {
        env->current_stage = env->release_stage;
        env->step_count = 0;

        // Calculate the change per sample for the release stage
        if (env->rates[env->release_stage] != 0) {
            env->delta = (env->levels[env->release_stage + 1] - env->levels[env->release_stage]) / (int32_t)env->rates[env->release_stage];
        } else {
            env->delta = 0;  // No change if rate is zero
        }
    }
}

#define STAGE_MAX (8)

// Get the next value in the envelope
int16_t next_envelope_value(Envelope *env) {
    // Return 0 if the envelope hasn't been triggered
    if (!env->triggered) {
        return 0;
    }

    if (env->current_stage != env->last_stage) {
        printf("%d -> %d\r\n", env->last_stage, env->current_stage);
        env->last_stage = env->current_stage;
    }
    // Advance the envelope by one step (1/44100 second)
    env->current_level += env->delta;
    env->step_count++;

    // Check if the current stage is complete
    if (env->step_count >= env->rates[env->current_stage]) {
        env->current_stage++;
        if (env->current_stage >= STAGE_MAX) env->current_stage = STAGE_MAX-1;
        env->step_count = 0;

        // If we're done with all stages, return the final level
        if (env->current_stage >= STAGE_MAX) {
            return env->levels[STAGE_MAX-1];
        }

        int32_t rate = (int32_t)env->rates[env->current_stage];

        // If we're at the sustain stage, hold at that level
        if (!env->released && env->current_stage == env->sustain_stage) {
            env->delta = 0; // Hold at sustain level
            return (int16_t)env->current_level; // Return current sustain level
        } else if (env->released && env->current_stage == env->release_stage) {
            // Calculate delta for the release stage
            if (env->current_stage + 1 < STAGE_MAX) {
                if (rate == 0) env->delta = 0;
                else env->delta = (env->levels[env->current_stage + 1] - env->levels[env->current_stage]) / rate;
            } else {
                env->delta = 0; // End of envelope, stop changes
            }
        } else {
            // Update delta for the next stage if we are not in the sustain stage
            if (env->current_stage < (STAGE_MAX-1)) {
                if (rate == 0) env->delta = 0;
                else env->delta = (env->levels[env->current_stage + 1] - env->levels[env->current_stage]) / rate;
            }
        }
    }

    return (int16_t)env->current_level;
}


#else

#include <stdint.h>
#include <stdbool.h>

#define SAMPLE_RATE 44100

typedef struct {
    int16_t levels[8];      // 16-bit signed levels for each stage (-32768 to 32767)
    uint32_t rates[8];      // 32-bit rate values for each stage (affects speed of transition)
    int sustain_stage;      // Which stage is the sustain (0-7, -1 if none)
    int release_stage;      // Which stage is the release (0-7)
    int current_stage;      // Current stage in the envelope
    uint32_t step_count;    // Current step within the stage
    int32_t current_level;  // Current level of the envelope (int32_t for intermediate calculation)
    bool released;          // Has the release been triggered
    bool triggered;         // Has the envelope been triggered
    int32_t delta;          // The amount the level should change per sample
} Envelope;

// Initialize the envelope with levels and rates
void init_envelope(Envelope *env, int16_t *levels, uint32_t *rates, int sustain_stage, int release_stage) {
    for (int i = 0; i < 8; i++) {
        env->levels[i] = levels[i];
        env->rates[i] = rates[i];  // Now using 32-bit rates for longer transitions
    }
    env->sustain_stage = sustain_stage;
    env->release_stage = release_stage;
    env->current_stage = 0;
    env->step_count = 0;
    env->current_level = levels[0];  // Start at the level of stage 0
    env->released = false;
    env->triggered = false;
    env->delta = 0;  // No change until triggered
}

// Trigger the envelope (starts the attack phase)
void trigger_envelope(Envelope *env) {
    env->current_stage = 0;
    env->step_count = 0;
    env->current_level = env->levels[0];
    env->released = false;
    env->triggered = true;

    // Calculate the change per sample for the first stage
    if (env->rates[0] != 0) {
        env->delta = (env->levels[1] - env->levels[0]) / (int32_t)env->rates[0];
    }
}

// Trigger the release phase of the envelope
void release_envelope(Envelope *env) {
    env->released = true;
    if (env->release_stage >= 0 && env->release_stage < 8) {
        env->current_stage = env->release_stage;
        env->step_count = 0;

        // Calculate the change per sample for the release stage
        if (env->rates[env->release_stage] != 0) {
            env->delta = (env->levels[env->release_stage + 1] - env->levels[env->release_stage]) / (int32_t)env->rates[env->release_stage];
        }
    }
}

// Get the next value in the envelope
int16_t next_envelope_value(Envelope *env) {
    // Return 0 if the envelope hasn't been triggered
    if (!env->triggered) {
        return 0;
    }

    // Ensure we're within a valid stage
    if (env->current_stage >= 8) {
        return (int16_t)env->current_level;
    }

    // Advance the envelope by one step (1/44100 second)
    env->current_level += env->delta;
    env->step_count++;

    // Check if the current stage is complete
    if (env->step_count >= env->rates[env->current_stage]) {
        env->current_stage++;
        env->step_count = 0;

        // If we're done with all stages, return the final level
        if (env->current_stage >= 8) {
            puts("R");
            return env->levels[7];
        }

        // Update delta for the next stage if we are not at the sustain stage
        if (!env->released && env->current_stage == env->sustain_stage) {
            return (int16_t)env->current_level; // Hold at sustain level
        } else if (env->current_stage < 7) {
            // Calculate delta for the next stage
            if (env->rates[env->current_stage] == 0) return 0;
            env->delta = (env->levels[env->current_stage + 1] - env->levels[env->current_stage]) / (int32_t)env->rates[env->current_stage];
        }
    }

    return (int16_t)env->current_level;
}

#endif


/////


#if 1
int16_t levels[8] = {
    0,     // Stage 0: Initial level (silence before keypress)
    32767, // Stage 1: Attack peak (maximum volume)
    16384, // Stage 2: Decay to sustain level
    8192,  // Stage 3: Lower sustain level
    8192,  // Stage 4: Sustained level while the key is held
    8192,  // Stage 5: Sustained level (duplicate of stage 4)
    64,  // Stage 6: Sustained level (optional duplicate)
    0      // Stage 7: Release to silence
};

uint32_t rates[8] = {
    5000,  // Stage 0: Fast attack (sharp rise in volume)
    5000,  // Stage 1: Moderate attack rate
    1000,  // Stage 2: Slower decay rate to sustain level
    0,     // Stage 3: Hold at sustain level (no change in level)
    0,     // Stage 4: Sustain while key is held (no change in level)
    0,     // Stage 5: Sustain (duplicate)
    500,   // Stage 6: Sustain (duplicate)
    1000   // Stage 7: Fast release rate (fade out)
};
#else
int16_t levels[8] = {0, 16384, 32767, 16384, 8192, 4096, 2048, 0}; // Example envelope levels
uint32_t rates[8] = {1000, 5000, 3000, 2000, 1500, 1500, 1500, 1000}; // Example rates (progression speed)

// int16_t levels[8] = {0, 16384, 32767, 16384, 8192, 4096, 2048, 0}; // Example envelope levels
// uint32_t rates[8] = {2050, 110250, 44100, 88200, 132300, 176400, 220500, 44100}; // Example rates (now supporting several-second stages)


// int16_t levels[8] = {0, 6000, 9000, 5000, 400, 4096, 2048, 0}; // Example envelope levels
// uint32_t rates[8] = {5000, 5000, 4410, 8820, 50000, 1000, 500, 1}; // Example rates (now supporting several-second stages)
#endif

Envelope env;

/////


int wire(char *line) {
    int p = 0;
    while (line[p] != '\0') {
        char c = line[p++];
        int valid;
        int next;
        if (c == ' ' || c == '\t' || c == '\r' || c == ';') continue;
        if (c == '#') break;
        if (c == ':') {
            char peek = line[p];
            if (peek == 'c') {
                p++;
                printf("%c[2J%c[H\n", 27, 27);
            } else if (peek == 'q') {
                p++;
                puts("");
                running = 0;
                return -1;
            }
        } else if (c == '?') {
            char peek = line[p];
            if (peek == '?') {
                p++;
                for (int i=0; i<VOICES; i++) {
                    char flag = ' ';
                    if (i == voice) flag = '*';
                    printf("%c v%d w%d f%g a%g t%d b%d\n", flag, i, ow[i], of[i], oa[i], top[i], bot[i]);
                }
                // printf("%u, %u\n", (dds[voice].phase_accumulator>>8)&PHASE_MASK, dds[voice].phase_increment);
                // printf("sent=%lld\n", sent);
                printf("rtms %ldms\n", rtms);
                printf("btms %ldms\n", btms);
                printf("diff %ldms\n", btms-rtms);
                printf("L%d\n", latency_hack_ms);
                printf("D%s\n", device);
                //
            } else {
                int i = voice;
                char flag = ' ';
                if (i == voice) flag = '*';
                printf("%c v%d w%d f%g a%g t%d b%d\n", flag, i, ow[i], of[i], oa[i], top[i], bot[i]);
            }
            continue;
        } else if (c == 'f') {
            double f = mytod(&line[p], &valid, &next);
            // printf("freq :: p:%d :: f:%f valid:%d next:%d\n", p, f, valid, next);
            if (!valid) break; else p += next-1;
            if (f >= 0.0) of[voice] = f;
            dds_mod(&dds[voice], f);
        } else if (c == 'v') {
            int n = mytol(&line[p], &valid, &next);
            // printf("voice :: p:%d :: n:%d valid:%d next:%d\n", p, n, valid, next);
            if (!valid) break; else p += next-1;
            if (n >= 0 && n < VOICES) voice = n;
        } else if (c == 'a') {
            double a = mytod(&line[p], &valid, &next);
            // printf("amp :: p:%d :: a:%f valid:%d next:%d\n", p, a, valid, next);
            if (!valid) break; else p += next-1;
            if (a >= 0.0) {
                oa[voice] = a;
                calc_ratio(voice);
            }
        } else if (c == 'w') {
            int w = mytol(&line[p], &valid, &next);
            // printf("wave :: p:%d :: n:%d valid:%d next:%d\n", p, w, valid, next);
            if (!valid) break; else p += next-1;
            if (w >= 0 && w < WAVE_MAX) ow[voice] = w;
        } else if (c == 'n') {
            double note = mytod(&line[p], &valid, &next);
            // printf("note :: p:%d :: note:%f valid:%d next:%d\n", p, note, valid, next);
            if (!valid) break; else p += next-1;
            if (note >= 0.0 && note <= 127.0) {
                on[voice] = note;
                of[voice] = 440.0 * pow(2.0, (note - 69.0) / 12.0);
                dds_mod(&dds[voice], of[voice]);
            }
        } else if (c == 't') {
            int n = mytol(&line[p], &valid, &next);
            // printf("top :: p:%d :: n:%d valid:%d next:%d\n", p, n, valid, next);
            if (!valid) break; else p += next-1;
            if (n >= 0) {
                top[voice] = n;
                if (bot[voice] > 0) oa[voice] = (double)top[voice]/(double)bot[voice];
            }
        } else if (c == 'b') {
            int n = mytol(&line[p], &valid, &next);
            // printf("bot :: p:%d :: n:%d valid:%d next:%d\n", p, n, valid, next);
            if (!valid) break; else p += next-1;
            if (n > 0) {
                bot[voice] = n;
                if (bot[voice] > 0) oa[voice] = (double)top[voice]/(double)bot[voice];
            }
        } else if (c == 'L') {
            int n = mytol(&line[p], &valid, &next);
            // printf("LAT :: p:%d :: n:%d valid:%d next:%d\n", p, n, valid, next);
            if (!valid) break; else p += next-1;
            if (n > 0) {
                latency_hack_ms = n;
            }
        } else if (c == 'W') {
            char peek = line[p];
            if (peek >= '0' && peek <= '9') {
                p++;
                int n = peek - '0';
                switch (n) {
                    case SINE: dump(sine); break;
                    case SQR: dump(sqr); break;
                    case TRI: dump(tri); break;
                    case SAWU: dump(sawup); break;
                    case SAWD: dump(sawdown); break;
                    case NOIZ: dump(noise); break;
                }
            } else {
                printf("%d sine\n", SINE);
                printf("%d sqr\n", SQR);
                printf("%d sawd\n", SAWD);
                printf("%d sawu\n", SAWU);
                printf("%d tri\n", TRI);
                printf("%d noiz\n", NOIZ);
            }
        } else if (c == 'T') {
            puts("TRIGGER");
            trigger_envelope(&env);
        } else if (c == 'R') {
            puts("RELEASE");
            release_envelope(&env);
        }
    }
}

void *user(void *arg) {
    // int voice = 0;
    while (1) {
        char *line = linenoise("> ");
        if (line == NULL) break;
        linenoiseHistoryAdd(line);
        int n = wire(line);
        linenoiseFree(line);
    }
    running = 0;
}

int16_t *waves[WAVE_MAX] = {
    sine,
    sqr,
    sawdown,
    sawup,
    tri,
    noise,
    none,
    usr0,
    usr1,
    usr2,
    usr3,
    usr4,
};

void synth(int16_t *buffer, int period_size) {
    int a = 0;
    int b = 0;
    for (int n = 0; n < PERIOD_SIZE; n++) {
        buffer[n] = 0;
        int c = 0;
        for (int i=0; i<VOICES; i+=2) {
            if (ow[i] == NONE) continue;
            if (oa[i] == 0.0) continue;
            c++;
            a = (dds_step(&dds[i+0], waves[ow[i+0]])) * top[i+0] / bot[i+0];
            if (ow[i+1] == NONE) {
                b = 0;
            } else {
                b = (dds_step(&dds[i+1], waves[ow[i+1]])) * top[i+1] / bot[i+1];
            }
            dds_mod(&dds[i+0], of[i+0] + (double)b);
            buffer[n] += a;
        }
        // buffer[n] /= c;
    }
}

void listalsa(char *what) {
    int status;
    char *kind = strdup(what);
    char **hints;
    status = snd_device_name_hint(-1, kind, (void ***)&hints);
    if (status < 0) {
        puts("NOPE");
    } else {
        for (char **n = hints; *n != NULL; n++) {
            char *name = snd_device_name_get_hint(*n, "NAME");
            if (name != NULL) {
                puts(name);
                free(name);
            }
        }
        snd_device_name_free_hint((void **)hints);
    }
}




///


int main(int argc, char *argv[]) {
    int err;
    int16_t buffer[PERIOD_SIZE];

    if (argc > 1) {
        if (argv[1][0] == '-') {
            if (argv[1][1] == 'a') {
                listalsa("pcm");
                return 0;
            }
            if (argv[1][1] == 'm') {
                listalsa("rawmidi");
                return 0;
            }
        } else {
            device = argv[1];
        }
    }

    if (setup_alsa(device) != 0) {
    }

    generate_sine(sine, CHUNK_SIZE);
    generate_cosine(cosine, CHUNK_SIZE);
    generate_sqr(sqr, CHUNK_SIZE);
    generate_tri(tri, CHUNK_SIZE);
    generate_sawup(sawup, CHUNK_SIZE);
    generate_sawdown(sawdown, CHUNK_SIZE);
    generate_noise(noise, CHUNK_SIZE);
    generate_none(none, CHUNK_SIZE);

    // init_envelope(&env, levels, rates, 2, 6);
    init_envelope(&env, levels, rates, 4, 7);  // Sustain on stage 4, release starts at stage 7

    WaveTableOscillator wtsine;
    init_wave_table_oscillator(&wtsine, sine, CHUNK_SIZE, 440.0);

    for (int i=0; i<VOICES; i+=2) {
        of[i+0] = 440.0;
        of[i+1] = 0.25;
        dds_init(&dds[i+0], of[i+0]);
        dds_init(&dds[i+1], of[i+1]);
        if (i < 4) {
            ow[i+0] = SINE;
            ow[i+1] = SINE;
            oa[i+0] = .01;
            oa[i+1] = 0;
        } else {
            ow[i+0] = NONE;
            ow[i+1] = NONE;            
            oa[i+0] = 0;
            oa[i+1] = 0;
        }
        calc_ratio(i+0);
        calc_ratio(i+1);
    }

    pthread_t user_thread;
    pthread_create(&user_thread, NULL, user, NULL);
    pthread_detach(user_thread);

    pthread_t midi_thread;
    pthread_create(&midi_thread, NULL, midi, NULL);
    pthread_detach(midi_thread);

    gettimeofday(&rtns0, NULL);

    while (running) {
        // Fill the buffer with DDS-generated sine wave samples

        //synth(buffer, PERIOD_SIZE);
        for (int i=0; i<PERIOD_SIZE; i++) {
            int s = next_wave_table_sample(&wtsine);
            int e = next_envelope_value(&env);
            int n = s * e / MAX_VALUE;
            buffer[i] = n;
        }

        // Wait until ALSA is ready to accept new data
        if ((err = snd_pcm_wait(pcm_handle, 1000)) < 0) {
            check_alsa_error(err, "PCM wait failed");
        }

        // Write the buffer to ALSA device
        if ((err = snd_pcm_writei(pcm_handle, buffer, PERIOD_SIZE)) < 0) {
            if (err == -EPIPE) {
                // Recover from buffer underrun
                snd_pcm_prepare(pcm_handle);
            } else {
                check_alsa_error(err, "Failed to write to PCM device");
            }
        } else {
            // try to not get too far ahead of realtime...
            // without this, we get about 24 seconds ahead of realtime!
            #define TV2MS(t) ((t.tv_sec*1000)+(t.tv_usec/1000))
            sent+=PERIOD_SIZE;
            gettimeofday(&rtns1, NULL);
            rtms = TV2MS(rtns1)-TV2MS(rtns0);
            btms = sent * 1000 / SAMPLE_RATE;
            diff = btms - rtms;
            if (diff > latency_hack_ms) {
                diff -= latency_hack_ms;
                usleep(diff * 1000);
            }
        }
    }

    // Cleanup and close
    snd_pcm_close(pcm_handle);

    pthread_join(user_thread, NULL);
    pthread_join(midi_thread, NULL);

    return 0;
}
