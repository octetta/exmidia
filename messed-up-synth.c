#include <stdio.h>
#include <stdint.h>
#include <math.h>
#include <alsa/asoundlib.h>
#include <pthread.h>

typedef int32_t qfp;

#if 0
#define QSTR "Q16.16"
#define QMUL (65536.0)
#define QSFT (16)
#define QMSK (0xffff)
#endif

#if 1
#define QSTR "Q17.15"
#define QMUL (32768.0)
#define QSFT (15)
#define QMSK (0x7fff)
#endif

#if 0
#define QSTR "Q18.14"
#define QMUL (16384.0)
#define QSFT (14)
#define QMSK (0x3fff)
#endif

#if 0
#define QSTR "Q19.13"
#define QMUL (8192.0)
#define QSFT (13)
#define QMSK (0x1fff)
#endif

#if 0
#define QSTR "Q20.12"
#define QMUL (4096.0)
#define QSFT (12)
#define QMSK (0xfff)
#endif

#if 0
#define QSTR "Q21.11"
#define QMUL (2048.0)
#define QSFT (11)
#define QMSK (0x7ff)
#endif

#if 0
#define QSTR "Q22.10"
#define QMUL (1024.0)
#define QSFT (10)
#define QMSK (0x3ff)
#endif

#if 0
#define QSTR "Q23.9"
#define QMUL (512.0)
#define QSFT (9)
#define QMSK (0x1ff)
#endif

#if 0
#define QSTR "Q24.8"
#define QMUL (256.0)
#define QSFT (8)
#define QMSK (0xff)
#endif

#if 1
qfp dtoqfp(double f) {
 return f * QMUL;
}
double qfptod(qfp q) {
  return (double)q / QMUL;
}
int32_t qfpwhole(qfp q) {
  return q >> QSFT;
}
uint32_t qfpfrac(qfp q) {
  return q & QMSK;
}
#endif

#include <sys/time.h>

#define SAMPLE_RATE 44100
#define WAVE_SIZE 4096 // must be a power of 2 for the mask below to work
#define PHASE_MASK (WAVE_SIZE - 1)
#define PERIOD_SIZE 1024  // Number of samples per ALSA period

#define VOICES (8)

qfp vol[VOICES];

struct timeval rtns0;
struct timeval rtns1;

typedef int16_t sample_t;

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
int16_t sine[WAVE_SIZE];
int16_t cosine[WAVE_SIZE];
int16_t sqr[WAVE_SIZE];
int16_t tri[WAVE_SIZE];
int16_t sawup[WAVE_SIZE];
int16_t sawdown[WAVE_SIZE];
int16_t noise[WAVE_SIZE];
int16_t none[WAVE_SIZE];
int16_t usr0[WAVE_SIZE];
int16_t usr1[WAVE_SIZE];
int16_t usr2[WAVE_SIZE];
int16_t usr3[WAVE_SIZE];
int16_t usr4[WAVE_SIZE];

#define MAX_VALUE 32767
#define MIN_VALUE -32767

void make_sine(int16_t *table, int size) {
    for (int i = 0; i < size; i++) {
        table[i] = (int16_t)(MAX_VALUE * sinf(2.0f * M_PI * i / size));
    }
}

void make_cosine(int16_t *table, int size) {
    for (int i = 0; i < size; i++) {
        table[i] = (int16_t)(MAX_VALUE * cosf(2.0f * M_PI * i / size));
    }
}

void make_sqr(int16_t *table, int size) {
    for (int i = 0; i < size; i++) {
        if (i < (size/2)) {
            table[i] = MAX_VALUE;
        } else {
            table[i] = MIN_VALUE;
        }
    }
}

void make_tri(int16_t *table, int size) {
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

void make_sawup(int16_t *table, int size) {
    int acc = 0;
    int rate = MAX_VALUE/size;
    for (int i = 0; i < size; i++) {
        table[i] = acc;
        acc += rate;
    }
}

void make_sawdown(int16_t *table, int size) {
    int acc = MAX_VALUE;
    int rate = MAX_VALUE/size;
    for (int i = 0; i < size; i++) {
        table[i] = acc;
        acc -= rate;
    }
}

void make_noise(int16_t *table, int size) {
    for (int i = 0; i < size; i++) {
        table[i] = ((double)rand()/(double)RAND_MAX - 0.5) * MAX_VALUE;
    }
}

void make_none(int16_t *table, int size) {
    for (int i = 0; i < size; i++) {
        table[i] = 0;
    }
}

#if 1
// DDS initialization
void dds_init(DDS *dds, double output_frequency) {
    dds->phase_accumulator = 0;
    dds->phase_increment =
      (uint32_t)(
          (output_frequency * WAVE_SIZE) / SAMPLE_RATE * (1 << 8));
}

void dds_mod(DDS *dds, double new_frequency) {
    dds->phase_increment = (uint32_t)((new_frequency * WAVE_SIZE) / SAMPLE_RATE * (1 << 8));
}

// DDS step to get the next sample
int16_t dds_step(DDS *dds, int16_t *wavetable) {
    uint16_t index = dds->phase_accumulator >> 8;
    int16_t sample = wavetable[index & PHASE_MASK];
    dds->phase_accumulator += dds->phase_increment;
    return sample;
}
#endif

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

double params[1024];
char input[1024];

int running = 1;

//#define WAVE_MAX (12)

int oe[VOICES];
int ofm[VOICES]; // choose which oscillator is a frequency modulator
int oam[VOICES]; // choose which oscillator is a amplitude modulator

#if 0
double of[VOICES];
double on[VOICES];
double oa[VOICES];

// LFO-ey stuff
// TODO
int oam[VOICES]; // choose which oscillator is a amplitude modulator
int opm[VOICES]; // choose which oscillator is a panning modulator

// amplitude ratio... this influences the oa
int top[VOICES];
int bot[VOICES];
#endif

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

#if 0
void calc_ratio(int index) {
    int precision = 10000;
    int ip = oa[index] * precision;
    int gcd = agcd(abs(ip), precision);
    top[index] = ip / gcd;
    bot[index] = precision / gcd;
}
#endif

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
enum {
  SINE,
  SQR,
  SAWD,
  SAWU,
  TRI ,
  NOIZ,
  USR0,
  USR1,
  USR2,
  USR3,
  USR4,
  NONE,
  WAVE_MAX,
};

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
    for (int i=0; i<WAVE_SIZE; i++) {
        sprintf(buf, "%d\n", wave[i]);
        write(fd, buf, strlen(buf));
    }
    close(fd);
}




#include <stdint.h>
#include <stddef.h>

// #define SAMPLE_RATE 44100
#define FIXED_POINT_SHIFT 16

typedef struct {
    double freq;
    double modf;
    int w;
    sample_t *wave;
    sample_t raw;
    sample_t processed;
    size_t len;
    int32_t inc;
    int32_t acc;
    int fps;
    int rate;
    //
    int32_t scale;
    int32_t wrap;
    char ismod;
    int fmo;
    int amo;
} osc_t;

void osc_wave(osc_t *osc, sample_t *wave, size_t len) {
    osc->wave = wave;
    osc->len = len;
    osc->wrap = len << osc->fps;

    // Calculate the phase increment (fixed-point),
    // scale for both the frequency and the wave table size
    osc->inc = (int32_t)((osc->freq * len * osc->scale) / osc->rate);
}

void osc_init(osc_t *osc, sample_t *wave, size_t len, double freq) {
  osc->freq = freq;
  osc->fps = QSFT;
  osc->scale = 1 << QSFT;
  osc->rate = SAMPLE_RATE;

  osc_wave(osc, wave, len);

  osc->acc = 0;

  osc->ismod = 0;
  osc->fmo = -1;
}

void osc_freq(osc_t *osc, double freq) {
  osc->freq = freq;
  osc->inc = (int32_t)((freq * osc->len * osc->scale) / osc->rate);
}

// Generate the next sample from the wave table
sample_t osc_sample(osc_t *osc) {
    // Extract the integer part of the phase accumulator to get the current table index
    uint32_t index = osc->acc >> osc->fps;

    // Get the next table index, wrapping around if necessary
    uint32_t next_index = (index + 1) % osc->len;

    // Get the fractional part of the phase accumulator for interpolation
    uint32_t fractional = osc->acc & (osc->scale - 1);

    // Perform linear interpolation between the current sample and the next sample
#if 1
    int16_t current = osc->wave[index % osc->len];
    // keep value internally for modulator use
    osc->raw = current;
    // increment the phase accumulator and wrap if it exceeds the wave size
    osc->acc += osc->inc;
    osc->acc %= osc->wrap;
    return (int16_t)current;
#else
    int16_t current = osc->wave[index % osc->len];
    int16_t next = osc->wave[next_index];
    int32_t interpolate = current + ((next - current) * fractional) / osc->scale;
    // keep value internally for modulator use
    osc->raw = interpolate;
    // increment the phase accumulator and wrap if it exceeds the wave size
    osc->acc += osc->inc;
    osc->acc %= osc->wrap;
    return (int16_t)interpolate;
#endif
}


osc_t osc[VOICES];

int ow[VOICES];
int top[VOICES];
int bot[VOICES];
double oa[VOICES];
double of[VOICES];

///// working but wonky envelope stuff below

// simple ADSR

#include <stdint.h>
#include <stdbool.h>

// Fixed point configuration
#define FP_BITS 15
#define FP_SCALE (1 << FP_BITS)
#define FP_MAX INT32_MAX

enum {
    ENV_IDLE,
    ENV_ATTACK,
    ENV_DECAY,
    ENV_SUSTAIN,
    ENV_RELEASE
};

typedef struct {
    // Configuration (all in fixed point)
    int32_t attack_rate;   
    int32_t decay_rate;    
    int32_t release_rate;  
    int32_t attack_level;  
    int32_t sustain_level;
    double dattack_level;  
    double dsustain_level;
    
    // keep for display
    int attack_ms;
    int decay_ms;
    int release_ms;

    int stage;
    int32_t current_level;
    bool note_on;
} env_t;

void env_init(env_t *env,
  uint32_t attack_ms, uint32_t decay_ms, uint32_t release_ms,
  double attack_level, double sustain_level) {
    
    if (attack_ms == 0) attack_ms = 1;
    if (decay_ms == 0) decay_ms = 1;
    if (release_ms == 0) release_ms = 1;
    
    env->attack_ms = attack_ms;
    env->decay_ms = decay_ms;
    env->release_ms = release_ms;

    env->dattack_level = attack_level;
    env->dsustain_level = sustain_level;

    // Set target levels in fixed point
    env->attack_level = FP_SCALE * attack_level;  // 1.0 in fixed point
    env->sustain_level = (FP_SCALE * sustain_level) / 10;  // 0.7 in fixed point
    
    // Calculate number of samples for each phase
    uint32_t attack_samples = (attack_ms * SAMPLE_RATE) / 1000;
    uint32_t decay_samples = (decay_ms * SAMPLE_RATE) / 1000;
    uint32_t release_samples = (release_ms * SAMPLE_RATE) / 1000;
    
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

void env_on(env_t *env) {
    env->note_on = true;
    env->stage = ENV_ATTACK;
}

void env_off(env_t *env) {
    env->note_on = false;
    env->stage = ENV_RELEASE;
}

int16_t env_next(env_t *env) {
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


///

sample_t *waves[WAVE_MAX] = {
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

int wave_size[WAVE_MAX] = {
  WAVE_SIZE,
  WAVE_SIZE,
  WAVE_SIZE,
  WAVE_SIZE,
  WAVE_SIZE,
  WAVE_SIZE,
  WAVE_SIZE,
  WAVE_SIZE,
  WAVE_SIZE,
  WAVE_SIZE,
  WAVE_SIZE,
  WAVE_SIZE,
};

////

env_t env[VOICES];

#define SAMPLE_RATE 44100

void show_voice(char flag, int i) {
    double f = osc[i].freq;
    double v = qfptod(vol[i]);
    printf("%c v%d w%d f%g a%g", flag, i, osc[i].w, f, v);
    printf(" M%d F%d", osc[i].ismod, osc[i].fmo);
    printf(" e%d B%d,%d,%d,%g,%g",
        oe[i],
        env[i].attack_ms,
        env[i].decay_ms,
        env[i].release_ms,
        env[i].dattack_level,
        env[i].dsustain_level
        );
    puts("");
}

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
        } else if (c == '=') {
            double d = mytod(&line[p], &valid, &next);
            if (!valid) break; else p += next-1;
            qfp q = dtoqfp(d);
            printf("%f -> %d %d:%d -> %f err:%f\n",
              d,
              q,
              qfpwhole(q),
              qfpfrac(q),
              qfptod(q),
              d-qfptod(q));
        } else if (c == '~') {
            // sleep n ms
            int ms = mytol(&line[p], &valid, &next);
            if (!valid) break; else p += next-1;
            usleep(ms * 1000);
        } else if (c == '?') {
            char peek = line[p];
            if (peek == '?') {
                p++;
                for (int i=0; i<VOICES; i++) {
                    char flag = ' ';
                    if (i == voice) flag = '*';
                    show_voice(flag, i);
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
                show_voice(flag, i);
            }
            continue;
        } else if (c == 'M') {
            int m = mytol(&line[p], &valid, &next);
            if (!valid) break; else p += next-1;
            osc[voice].ismod = m;
        } else if (c == 'F') {
            int f = mytol(&line[p], &valid, &next);
            if (!valid) break; else p += next-1;
            osc[voice].fmo = f;
        } else if (c == 'B') {
            // breakpoint aka ADR ... poor copy of AMY's
            // b#,#,#,#,#
            // attack-ms, decay-ms, release-ms, attack-level, sustain-level

            int a = mytol(&line[p], &valid, &next);
            if (!valid) break; else p += next-1;
            if (line[p] == ',') p++; else break;

            int d = mytol(&line[p], &valid, &next);
            if (!valid) break; else p += next-1;
            if (line[p] == ',') p++; else break;

            int r = mytol(&line[p], &valid, &next);
            if (!valid) break; else p += next-1;
            if (line[p] == ',') p++; else break;

            double al = mytod(&line[p], &valid, &next);
            if (!valid) break; else p += next-1;
            if (line[p] == ',') p++; else break;

            double sl = mytod(&line[p], &valid, &next);
            if (!valid) break; else p += next-1;

            // use the values
            env_init(&env[voice], a, d, r, al, sl);
        } else if (c == 'e') {
            char peek = line[p];
            if (peek == '0') {
                p++;
                oe[voice] = 0;
            } else if (peek == '1') {
                p++;
                oe[voice] = 1;
            } else {
                continue;
            }
        } else if (c == 'f') {
            double f = mytod(&line[p], &valid, &next);
             printf("freq :: p:%d :: f:%f valid:%d next:%d\n", p, f, valid, next);
            if (!valid) break; else p += next-1;
            //if (f >= 0.0) of[voice] = f;
            //dds_mod(&dds[voice], f);
            osc_freq(&osc[voice], f);
        } else if (c == 'v') {
            int n = mytol(&line[p], &valid, &next);
            // printf("voice :: p:%d :: n:%d valid:%d next:%d\n", p, n, valid, next);
            if (!valid) break; else p += next-1;
            if (n >= 0 && n < VOICES) voice = n;
        } else if (c == 'I') {
            int inc = mytol(&line[p], &valid, &next);
            if (!valid) break; else p += next-1;
            osc[voice].inc = inc;
        } else if (c == 'a') {
            double a = mytod(&line[p], &valid, &next);
            // printf("amp :: p:%d :: a:%f valid:%d next:%d\n", p, a, valid, next);
            if (!valid) break; else p += next-1;
            if (a >= 0.0) {
                //oa[voice] = a;
                //calc_ratio(voice);
                vol[voice] = dtoqfp(a);
            }
        } else if (c == 'w') {
            int w = mytol(&line[p], &valid, &next);
            if (!valid) break; else p += next-1;
            if (w >= 0 && w < WAVE_MAX) {
                //ow[voice] = w;
                sample_t *ptr = waves[w];
                int len = wave_size[w];
                osc[voice].w = w;
                osc_wave(&osc[voice], ptr, len);
            }
        } else if (c == 'n') {
            double note = mytod(&line[p], &valid, &next);
            // printf("note :: p:%d :: note:%f valid:%d next:%d\n", p, note, valid, next);
            if (!valid) break; else p += next-1;
            if (note >= 0.0 && note <= 127.0) {
                //on[voice] = note;
                //of[voice] = 440.0 * pow(2.0, (note - 69.0) / 12.0);
                //dds_mod(&dds[voice], of[voice]);
                double f = 440.0 * pow(2.0, (note - 69.0) / 12.0);
                osc_freq(&osc[voice], f);
            }
#if 0
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
#endif
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
        } else if (c == 'l') {
            double velocity = mytod(&line[p], &valid, &next);
            if (!valid) break; else p += next-1;
            if (velocity == 0.0) {
                env_off(&env[voice]);
            } else if (velocity > 0.0) {
                env_on(&env[voice]);
            }
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

void altsynth(sample_t *buffer, int period_size) {
    for (int i=0; i<PERIOD_SIZE; i++) {
        buffer[i] = 0;
        for (int v=0; v<VOICES; v++) {
            if (vol[v] == 0) continue;
            int s = osc_sample(&osc[v]);
            s = s * vol[v] >> QSFT;
            int n = 0;
            // envelope?
            if (oe[v]) {
              int16_t e = env_next(&env[v]);
              int32_t sample = ((int32_t)s * e) >> FP_BITS;
              sample_t f = (sample_t)sample;
              if (!osc[v].ismod) n = f;
              osc[v].processed = f;
            } else {
              if (!osc[v].ismod) n = s;
              osc[v].processed = s;
            }
            // frequency modulation?
            int fmo = osc[v].fmo;
            if (fmo >= 0) {
              double modf = (double)osc[fmo].processed / (double)MAX_VALUE;
              if (osc[v].modf != modf) {
                double f = osc[v].freq + modf;
                osc[v].inc += (int32_t)((f * osc[v].len * osc[v].scale) / osc->rate);
                osc[v].modf = modf;
              }
            }
            
            buffer[i] += n;
        }
    }
}

#if 1
void synth(int16_t *buffer, int period_size) {
    int a = 0;
    int b = 0;
    for (int n = 0; n < PERIOD_SIZE; n++) {
        buffer[n] = 0;
        int c = 0;
        for (int i=0; i<VOICES; i+=2) {
            int mod = i+1;
            if (ow[i] == NONE) continue;
            if (oa[i] == 0.0) continue;
            if (top[i] == 0) continue;
            c++;
            a = (dds_step(&dds[i], waves[ow[i]])) * top[i] / bot[i];
            if (ow[mod] == NONE) {
                b = 0;
            } else {
                b = (dds_step(&dds[mod], waves[ow[mod]])) * top[mod] / bot[mod];
                if (oe[mod]) {
                    int16_t envelope_value = env_next(&env[mod]);
                    int32_t sample = ((int32_t)b * envelope_value) >> FP_BITS;
                    int16_t final_output = (int16_t)(sample);
                    b = final_output;
                }
            }
            dds_mod(&dds[i], of[i] + (double)b);
            if (oe[i]) {
                int16_t envelope_value = env_next(&env[i]);
                int32_t sample = ((int32_t)a * envelope_value) >> FP_BITS;
                int16_t final_output = (int16_t)(sample);
                buffer[n] += final_output;
            } else {
                buffer[n] += a;
            }
        }
        // buffer[n] /= c;
    }
}
#endif

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

#define HISTORY_FILE ".synth_history"

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

    printf("QFP %s\n", QSTR);

    linenoiseHistoryLoad(HISTORY_FILE);

    make_sine(sine, WAVE_SIZE);
    make_cosine(cosine, WAVE_SIZE);
    make_sqr(sqr, WAVE_SIZE);
    make_tri(tri, WAVE_SIZE);
    make_sawup(sawup, WAVE_SIZE);
    make_sawdown(sawdown, WAVE_SIZE);
    make_noise(noise, WAVE_SIZE);
    make_none(none, WAVE_SIZE);

    for (int i=0; i<VOICES; i++) {
        osc_init(&osc[i], sine, WAVE_SIZE, 440.0);
        osc[i].w = SINE;
        vol[i] = dtoqfp(0);
        env_init(&env[i], 2000, 1000, 1000, 1.0, 0.7);
    }

    
    vol[1] = dtoqfp(0.1);
    osc_freq(&osc[1], 1);
    osc[1].ismod = 1;

    pthread_t user_thread;
    pthread_create(&user_thread, NULL, user, NULL);
    pthread_detach(user_thread);

    pthread_t midi_thread;
    pthread_create(&midi_thread, NULL, midi, NULL);
    pthread_detach(midi_thread);

    gettimeofday(&rtns0, NULL);

    while (running) {
        // Fill the buffer with DDS-generated sine wave samples

        #if 1
        synth(buffer, PERIOD_SIZE);
        #else
        altsynth(buffer, PERIOD_SIZE);
        #endif

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

    linenoiseHistorySave(HISTORY_FILE);

    return 0;
}
