#include <stdio.h>
#include <stdint.h>
#include <math.h>
#include <alsa/asoundlib.h>
#include <pthread.h>

pid_t audio_pid;
pid_t user_pid;
pid_t midi_pid;

#include <sys/time.h>

#define SAMPLE_RATE (44100)
#define CYCLE_SIZE (4096)
#define ALSA_BUFFER (1024)  // Number of samples per ALSA period

#define VOICES (8)

struct timeval rtns0;
struct timeval rtns1;

typedef int16_t sample_t;

// DDS structure
typedef struct {
    uint32_t phase_accumulator;
    int32_t phase_increment;
    uint32_t size;
    int32_t phase_increment_divisor;
} DDS;

DDS dds[VOICES];

// Q17.15
#define DDS_FRAC_BITS (15)
#define DDS_SCALE (1 << DDS_FRAC_BITS)

void dds_freq(DDS *dds, double f) {
    dds->phase_increment = (int32_t)((f * dds->size) / SAMPLE_RATE * DDS_SCALE);
}

void dds_init(DDS *dds, uint32_t size, double f) {
    dds->phase_accumulator = 0;
    dds->size = size;
    dds->phase_increment_divisor = SAMPLE_RATE * DDS_SCALE;
    dds_freq(dds, f);
}

sample_t dds_step(DDS *dds, sample_t *wavetable) {
    if (dds->size == 0) return 0;
    uint32_t index = dds->phase_accumulator >> DDS_FRAC_BITS;
    sample_t sample = wavetable[index % dds->size];
    dds->phase_accumulator += dds->phase_increment;
    return sample;
}

// ALSA variables
snd_pcm_t *pcm_handle;
snd_pcm_hw_params_t *hw_params;

sample_t sine[CYCLE_SIZE];
sample_t cosine[CYCLE_SIZE];
sample_t sqr[CYCLE_SIZE];
sample_t tri[CYCLE_SIZE];
sample_t sawup[CYCLE_SIZE];
sample_t sawdown[CYCLE_SIZE];
sample_t noise[CYCLE_SIZE];
sample_t none[CYCLE_SIZE];
sample_t usr0[CYCLE_SIZE];
sample_t usr1[CYCLE_SIZE];
sample_t usr2[CYCLE_SIZE];
sample_t usr3[CYCLE_SIZE];
sample_t usr4[CYCLE_SIZE];

#define MAX_VALUE 32767
#define MIN_VALUE -32767

void make_sine(sample_t *table, int size) {
    for (int i = 0; i < size; i++) {
        table[i] = (sample_t)(MAX_VALUE * sinf(2.0f * M_PI * i / size));
    }
}

void make_cosine(sample_t *table, int size) {
    for (int i = 0; i < size; i++) {
        table[i] = (sample_t)(MAX_VALUE * cosf(2.0f * M_PI * i / size));
    }
}

void make_sqr(sample_t *table, int size) {
    for (int i = 0; i < size; i++) {
        if (i < (size/2)) {
            table[i] = MAX_VALUE;
        } else {
            table[i] = MIN_VALUE;
        }
    }
}

void make_tri(sample_t *table, int size) {
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

void make_sawup(sample_t *table, int size) {
    int acc = 0;
    int rate = MAX_VALUE/size;
    for (int i = 0; i < size; i++) {
        table[i] = acc;
        acc += rate;
    }
}

void make_sawdown(sample_t *table, int size) {
    int acc = MAX_VALUE;
    int rate = MAX_VALUE/size;
    for (int i = 0; i < size; i++) {
        table[i] = acc;
        acc -= rate;
    }
}

void make_noise(sample_t *table, int size) {
    for (int i = 0; i < size; i++) {
        table[i] = ((double)rand()/(double)RAND_MAX - 0.5) * MAX_VALUE;
    }
}

void make_none(sample_t *table, int size) {
    for (int i = 0; i < size; i++) {
        table[i] = 0;
    }
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
    snd_pcm_hw_params_set_period_size(pcm_handle, hw_params, ALSA_BUFFER, 0);

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

#if 0
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

#endif


int running = 1;

#define WAVE_MAX (12)

double of[VOICES];
double oft[VOICES];
int ofg[VOICES];
double ofgd[VOICES];
double on[VOICES];
double oa[VOICES];
int oe[VOICES];
int ow[VOICES];

// LFO-ey stuff
// TODO
int ismod[VOICES];
int cachemod[VOICES];
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
    midi_pid = getpid();
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

void dump(sample_t *wave) {
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
    for (int i=0; i<CYCLE_SIZE; i++) {
        sprintf(buf, "%d\n", wave[i]);
        write(fd, buf, strlen(buf));
    }
    close(fd);
}

// simple ADSR

#include <stdint.h>
#include <stdbool.h>

// Fixed point configuration
// Q17.15
#define ENV_FRAC_BITS 14
#define ENV_SCALE (1 << ENV_FRAC_BITS)
#define ENV_MAX INT32_MAX

enum {
    ENV_IDLE,
    ENV_ATTACK,
    ENV_DECAY,
    ENV_SUSTAIN,
    ENV_RELEASE
};

typedef struct {
    int32_t attack_rate;   
    int32_t decay_rate;    
    int32_t release_rate;  
    int32_t attack_level;  
    int32_t sustain_level;
    
    uint32_t attack_ms;
    uint32_t decay_ms;
    uint32_t release_ms;

    int stage;
    int last_stage;

    int32_t current_level;
    int32_t last_level;
    bool note_on;
} env_t;

void env_init(env_t* env,
    uint32_t attack_ms,
    uint32_t decay_ms,
    uint32_t release_ms,
    uint32_t attack_level,
    uint32_t sustain_level) {
    env->last_level = -1;
    env->attack_ms = attack_ms;
    env->decay_ms = decay_ms;
    env->release_ms = release_ms;

    env->attack_level = attack_level;
    env->sustain_level = sustain_level;
    
    // Calculate number of samples for each phase
    uint32_t attack_samples = (attack_ms * SAMPLE_RATE) / 1000;
    uint32_t decay_samples = (decay_ms * SAMPLE_RATE) / 1000;
    uint32_t release_samples = (release_ms * SAMPLE_RATE) / 1000;

    if (attack_samples == 0) attack_samples = 1;
    if (decay_samples == 0) decay_samples = 1;
    if (release_samples == 0) release_samples = 1;
    
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

void env_on(env_t* env) {
    env->last_stage = ENV_IDLE;
    env->note_on = true;
    env->stage = ENV_ATTACK;
}

void env_off(env_t* env) {
    env->note_on = false;
    env->stage = ENV_RELEASE;
}

sample_t env_next(env_t* env) {
    if (env->last_stage != env->stage) {
        printf("ENV %d -> %d (%d)\n", env->last_stage, env->stage, env->current_level);
        env->last_stage = env->stage;
    }
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
    if (env->current_level != env->last_level) {
        printf("%d\n", env->current_level);
        env->last_level = env->current_level;
    }
    return ((env->current_level * ENV_MAX) >> ENV_FRAC_BITS);
}

env_t env[VOICES];

long long int total_cpu_usage(void) {
    long long int t;
    FILE *f = fopen("/proc/stat", "rt");
    if (f) {
        char buf[1024];
        char *line = fgets(buf, sizeof(buf), f);
        if (line) {
            long long int user;
            long long int nice;
            long long int system;
            long long int idle;
            int n = sscanf(line, "%*s %llu %llu %llu %llu", &user, &nice, &system, &idle);
            // printf("n:%d %llu %llu %llu %llu\n", n, user, nice, system, idle);
            t = user + nice + system + idle;
        }
        fclose(f);
        return t;
    }
    return t;
}

long int pid_times(pid_t pid) {
    char file[1024];
    sprintf(file, "/proc/%d/stat", (int)pid);
    long long int t;
    FILE *f = fopen(file, "rt");
    if (f) {
        char buf[1024];
        char *line = fgets(buf, sizeof(buf), f);
        if (line) {
            long int usertime;
            long int systemtime;
            int n = sscanf(line,
                "%*s %*s %*s %*s" //pid,command,state,ppid
                "%*s %*s %*s %*s %*s %*s %*s %*s %*s"
                "%lu %lu" //usertime,systemtime
                "%*s %*s %*s %*s %*s %*s %*s"
                "%*s", //virtual memory size in bytes
                
                &usertime, &systemtime);
            // printf("n:%d %lu %lu\n", n, usertime, systemtime);
            t = usertime + systemtime;
        }
        fclose(f);
        return t;
    }
    return t;
}

void show_voice(char flag, int i) {
    printf("%c v%d w%d f%.4f e%d a%.4f",
        flag, i, ow[i], of[i], oe[i], oa[i]);
    // printf(" t%d b%d", top[i], bot[i]);
    if (ismod[i]) printf(" M%d", ismod[i]);
    if (ofm[i] >= 0) printf(" F%d", ofm[i]);
    if (oe[i]) printf(" B%d,%d,%d,%d,%d",
        env[i].attack_ms,
        env[i].decay_ms,
        env[i].release_ms,
        env[i].attack_level,
        env[i].sustain_level);
    if (ofg[i]) printf(" G%d (%f/%f)", ofg[i], ofgd[i], oft[i]);
    puts("");
}

void cpu_usage(char *name, pid_t pid) {
    int ncpu = sysconf(_SC_NPROCESSORS_ONLN);
    // printf("ncpu=%d\n", ncpu);
    long long t1 = total_cpu_usage();
    // printf("t1 = %llu\n", t1);
    long p1 = pid_times(pid);
    // printf("p1=%ld\n", p1);
    sleep(1);
    long long t2 = total_cpu_usage();
    // printf("t2 = %llu\n", t2);
    long p2 = pid_times(pid);
    // printf("p2=%ld\n", p2);
    double usage = (ncpu * (p2-p1)) * 100 / (double)(t2-t1);
    printf("%s cpu-usage=%g\n", name, usage);
}

int wire(char *line) {
    int p = 0;
    int valid;
    while (line[p] != '\0') {
        valid = 1;
        char c = line[p++];
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
                printf("rtms %ldms\n", rtms);
                printf("btms %ldms\n", btms);
                printf("diff %ldms\n", btms-rtms);
                printf("L%d\n", latency_hack_ms);
                printf("D%s\n", device);
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
            ismod[voice] = m;
        } else if (c == 'G') {
            int g = mytol(&line[p], &valid, &next);
            if (!valid) break; else p += next-1;
            ofg[voice] = g;
        } else if (c == 'S') {
            cpu_usage("audio", audio_pid);
            cpu_usage("user", user_pid);
            cpu_usage("midi", midi_pid);
        } else if (c == 'F') {
            int f = mytol(&line[p], &valid, &next);
            if (!valid) break; else p += next-1;
            if (f < VOICES) {
                ofm[voice] = f;
                ismod[f] = 1;
            }
        } else if (c == 'B') {
            // breakpoint aka ADR ... poor copy of AMY's
            // b#,#,#
            // attack-ms, decay-ms, release-ms

            int a = mytol(&line[p], &valid, &next);
            // printf("attack :: p:%d :: n:%d valid:%d next:%d\n", p, a, valid, next);
            if (!valid) break; else p += next-1;
            if (line[p] == ',') p++; else break;

            int d = mytol(&line[p], &valid, &next);
            // printf("decay :: p:%d :: n:%d valid:%d next:%d\n", p, d, valid, next);
            if (!valid) break; else p += next-1;
            if (line[p] == ',') p++; else break;

            int r = mytol(&line[p], &valid, &next);
            // printf("release :: p:%d :: n:%d valid:%d next:%d\n", p, r, valid, next);
            if (!valid) break; else p += next-1;
            if (line[p] == ',') p++; else break;

            int al = mytol(&line[p], &valid, &next);
            // printf("attack-level :: p:%d :: n:%d valid:%d next:%d\n", p, al, valid, next);
            if (!valid) break; else p += next-1;
            if (line[p] == ',') p++; else break;

            int sl = mytol(&line[p], &valid, &next);
            // printf("sustain-level :: p:%d :: n:%d valid:%d next:%d\n", p, sl, valid, next);
            if (!valid) break; else p += next-1;

            // use the values
            env_init(&env[voice],a,d,r, al, sl);
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
            // printf("freq :: p:%d :: f:%f valid:%d next:%d\n", p, f, valid, next);
            if (!valid) break; else p += next-1;
            if (f >= 0.0) {
                if (ofg[voice] > 0) {
                    double d = f - of[voice];
                    ofgd[voice] = d / (double)ofg[voice];
                    oft[voice] = f;
                    f += d;
                    of[voice] = f;
                    dds_freq(&dds[voice], f);
                } else {
                    of[voice] = f;
                    dds_freq(&dds[voice], f);
                }
            }
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
            if (w >= 0 && w < WAVE_MAX) {
                ow[voice] = w;
                sample_t *ptr = none;
                switch (w) {
                    case SINE: ptr = sine; break;
                    case SQR: ptr = sqr; break;
                    case SAWD: ptr = sawdown; break;
                    case SAWU: ptr = sawup; break;
                    case TRI: ptr = tri; break;
                    case NOIZ: ptr = noise; break;
                }
            }
        } else if (c == 'n') {
            double note = mytod(&line[p], &valid, &next);
            // printf("note :: p:%d :: note:%f valid:%d next:%d\n", p, note, valid, next);
            if (!valid) break; else p += next-1;
            if (note >= 0.0 && note <= 127.0) {
                on[voice] = note;
                of[voice] = 440.0 * pow(2.0, (note - 69.0) / 12.0);
                dds_freq(&dds[voice], of[voice]);
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
        } else if (c == 'l') {
            double velocity = mytod(&line[p], &valid, &next);
            if (!valid) break; else p += next-1;
            if (velocity <= 0.0) {
                if (oe[voice]) {
                    env_off(&env[voice]);
                } else {
                    oa[voice] = 0.0;
                    calc_ratio(voice);
                }
            } else if (velocity > 0.0) {
                oa[voice] = velocity;
                calc_ratio(voice);
                env_on(&env[voice]);
            }
        } else {
            valid = 0;
            break;
        }
    }
    if (!valid) {
        printf("trouble -> %s\n", &line[p-1]);
    }
}

void *user(void *arg) {
    // int voice = 0;
    user_pid = getpid();
    while (1) {
        char *line = linenoise("> ");
        if (line == NULL) break;
        linenoiseHistoryAdd(line);
        int n = wire(line);
        linenoiseFree(line);
    }
    running = 0;
}

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

void synth(int16_t *buffer, int period_size) {
    int32_t a = 0;
    int32_t b = 0;
    for (int n = 0; n < period_size; n++) {
        buffer[n] = 0;
        int c = 0;
        // process modulators first
        for (int i=0; i<VOICES; i++) {
            cachemod[i] = 0;
            if (ow[i] == NONE) continue;
            if (oa[i] == 0.0) continue;
            if (top[i] == 0) continue;
            if (ismod[i]) {
                b = (dds_step(&dds[i], waves[ow[i]])) * top[i] / bot[i];
                if (ofm[i] >= 0) {
                    dds_freq(&dds[i], of[i] + (double)cachemod[ofm[i]]);
                }
                if (oe[i]) {
                    int32_t envelope_value = env_next(&env[i]);
                    int32_t sample = (b * envelope_value) >> ENV_FRAC_BITS;
                    b = sample;
                }
                cachemod[i] = b;
            }
        }
        // process things that are not modulators
        for (int i=0; i<VOICES; i++) {
            if (ismod[i]) continue;
            if (ow[i] == NONE) continue;
            if (oa[i] == 0.0) continue;
            if (top[i] == 0) continue;
            c++;
            a = (dds_step(&dds[i], waves[ow[i]])) * top[i] / bot[i];
            if (ofm[i] >= 0) {
                dds_freq(&dds[i], of[i] + (double)cachemod[ofm[i]]);
            }
            if (oe[i]) {
                int32_t envelope_value = env_next(&env[i]);
                int32_t sample = (a * envelope_value) >> 2;
                // int64_t sample = (a * envelope_value) >> ENV_FRAC_BITS;
                buffer[n] += sample;
            } else {
                buffer[n] += a;
            }
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

#define HISTORY_FILE ".synth_history"

int main(int argc, char *argv[]) {
    int err;
    int16_t buffer[ALSA_BUFFER];

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

    linenoiseHistoryLoad(HISTORY_FILE);

    printf("DDS Q%d.%d\n", 32-DDS_FRAC_BITS, DDS_FRAC_BITS);
    printf("ENV Q%d.%d\n", 32-ENV_FRAC_BITS, ENV_FRAC_BITS);

    make_sine(sine, CYCLE_SIZE);
    make_cosine(cosine, CYCLE_SIZE);
    make_sqr(sqr, CYCLE_SIZE);
    make_tri(tri, CYCLE_SIZE);
    make_sawup(sawup, CYCLE_SIZE);
    make_sawdown(sawdown, CYCLE_SIZE);
    make_noise(noise, CYCLE_SIZE);
    make_none(none, CYCLE_SIZE);

    for (int i=0; i<VOICES; i++) {
        int mod = i+1;
        of[i] = 440.0;
        // of[mod] = 0.25;
        ofm[i] = -1;
        ismod[i] = 0;
        dds_init(&dds[i], CYCLE_SIZE, of[i]);
        ow[i] = SINE;
        oa[i] = 0;
        // dds_init(&dds[mod], CYCLE_SIZE, of[mod]);
        // if (i < 4) {
        //     ow[i] = SINE;
        //     ow[mod] = SINE;
        //     oa[i] = .01;
        //     oa[mod] = 0;
        // } else {
        //     ow[i] = NONE;
        //     ow[mod] = NONE;
        //     oa[i] = 0;
        //     oa[mod] = 0;
        // }
        calc_ratio(i);
        calc_ratio(mod);
    }


    for (int i=0; i<VOICES; i+=1) {
        // simple
        env_init(&env[i], 
            2000,    // 2 second attack
            3000,    // 3 second decay
            4000,    // 4 second release
            ENV_SCALE, (ENV_SCALE * 7) / 10);
    }

    pthread_t user_thread;
    pthread_create(&user_thread, NULL, user, NULL);
    pthread_detach(user_thread);

    pthread_t midi_thread;
    pthread_create(&midi_thread, NULL, midi, NULL);
    pthread_detach(midi_thread);

    gettimeofday(&rtns0, NULL);

    audio_pid = getpid();

    while (running) {
        synth(buffer, ALSA_BUFFER);

        if ((err = snd_pcm_wait(pcm_handle, 1000)) < 0) {
            check_alsa_error(err, "PCM wait failed");
        }
        if ((err = snd_pcm_writei(pcm_handle, buffer, ALSA_BUFFER)) < 0) {
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
            sent+=ALSA_BUFFER;
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
