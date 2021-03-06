
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <time.h>
#include <math.h>

#include "arm_math.h"
#include "modulate.h"

#define SAMPLE_RATE 62500
#define CARRIER_TONE 20833.33f
#define BAUD_RATE (651.0417f) // 31.25
#define PLL_INCR (BAUD_RATE / (float)(SAMPLE_RATE))

#define SAMPLES_PER_PERIOD 20 // Must evenly divide CARRIER_TONE

#define clear() printf("\033[H\033[J")
#define gotoxy(x, y) printf("\033[%d;%dH", (y), (x))

// The threshold at which a bit is determined to be a reversal
const float detection_threshold = 1800000.0f; // 0.18f

#include "fir_coefficients.h"
#include "lpf_coefficients.h"

static float fir_state[SAMPLES_PER_PERIOD + FIR_STAGES - 1];
static float i_lpf_state[SAMPLES_PER_PERIOD + LPF_STAGES - 1];
static float q_lpf_state[SAMPLES_PER_PERIOD + LPF_STAGES - 1];

uint8_t wave_header[44];

extern char varcode_to_char(uint32_t c);
static void print_char(uint32_t c) {
    printf("%c", varcode_to_char(c >> 2));
}


void write_wav(int16_t *data, size_t len, const char *name) {
    int out_fd = open(name, O_WRONLY | O_CREAT | O_TRUNC, 0777);
    if (out_fd == -1) {
        perror("unable to open filtered file");
        return;
    }
    if (write(out_fd, wave_header, sizeof(wave_header)) == -1) {
        perror("error");
    }
    if (write(out_fd, data, len) == -1) {
        perror("error");
    }
    close(out_fd);
}

void write_wav_stereo(int16_t *left, int16_t *right, unsigned int len,
                      const char *name) {
    FILE *output = fopen(name, "w+b");
    if (!output) {
        perror("unable to open filtered file");
        return;
    }
    if (fwrite(wave_header, sizeof(wave_header), 1, output) <= 0) {
        perror("error");
        fclose(output);
        return;
    }
    for (unsigned int i = 0; i < len; i++) {
        if (fwrite(&(left[i]), 2, 1, output) != 1) {
            perror("error");
            fclose(output);
            return;
        }
        if (fwrite(&(right[i]), 2, 1, output) != 1) {
            perror("error");
            fclose(output);
            return;
        }
    }
    fclose(output);
}

struct nco_state {
    float32_t samplerate; // Hz
    float32_t freq;       // Hz

    float32_t phase; // rad
} nco_st;

void nco(float32_t control, uint32_t timestep, float32_t *i, float32_t *q) {
    // control is a number from +1 to -1, which translates to -pi to +pi additional phase per time step
    // timestep is the current time step, expressed in terms of samples since t=0
    // i is a pointer to the in-phase return value
    // q is a pointer to the quadrature return value

    nco_st.phase += (control / PI);

    *i =
        cos((((float32_t)timestep) * nco_st.freq * 2 * PI) / nco_st.samplerate +
            nco_st.phase);
    *q =
        sin((((float32_t)timestep) * nco_st.freq * 2 * PI) / nco_st.samplerate +
            nco_st.phase);
}

static int byte_count = 0;
static void append_wav(void *arg, void *data, unsigned int count) {
    // printf("%p %p %ud\n", )
    // printf("write %d count %u\n", ((int16_t *)data)[0], count);
    byte_count += count;
    fwrite(data, count, 1, arg);
}

static void modulate(const char *src_txt, const char *dest_wav) {
    struct stat statbuf;
    struct modulate_state state;
    FILE *wav = fopen(dest_wav, "wb+");

    int fd = open(src_txt, O_RDONLY);
    if (-1 == fd) {
        perror("unable to open source text file");
        exit(1);
    }
    if (-1 == fstat(fd, &statbuf)) {
        perror("unable to determine size of buffer");
        exit(1);
    }
    char *txt_file = malloc(statbuf.st_size + 1);
    memset(txt_file, 0, statbuf.st_size);

    if (read(fd, txt_file, statbuf.st_size) != statbuf.st_size) {
        fprintf(stderr, "couldn't read source text file");
    }
    close(fd);

    static uint8_t wav_header[44] = {
        0x52, 0x49, 0x46, 0x46, 0x1c, 0x12, 0x05, 0x00, 0x57, 0x41, 0x56,
        0x45, 0x66, 0x6d, 0x74, 0x20, 0x10, 0x00, 0x00, 0x00, 0x01, 0x00,
        0x01, 0x00, 0x11, 0x2b, 0x00, 0x00, 0x22, 0x56, 0x00, 0x00, 0x02,
        0x00, 0x10, 0x00, 0x64, 0x61, 0x74, 0x61, 0x00, 0x00, 0x00, 0x00,
    };
    uint32_t rate = SAMPLE_RATE;
    uint32_t tone = CARRIER_TONE; // rate / 3;
    memcpy(&wav_header[24], &rate, sizeof(rate));
    *((uint32_t *)&(wav_header[40])) = 841939;
    fwrite(wav_header, sizeof(wav_header), 1, wav);

    modulate_init(&state, tone, rate, 1.0 /* unimplemented */, append_wav, wav);
    modulate_string(&state, txt_file);
    fseek(wav, 40, SEEK_SET);
    fwrite(&byte_count, 4, 1, wav); // Update the chunk byte count
    fclose(wav);
    free(txt_file);
}

int main(int argc, char **argv) {
    char *wave_file_name = "samples/PSK31_sample.wav";
    char *output_file_name = "filtered.wav";
    char *source_txt_file_name = "to-be-modulated.txt";
    struct stat statbuf;
    int16_t *wave_buffer;
    int16_t *wave_filtered;

    if (1) {
        wave_file_name = "modulated.wav";
        modulate(source_txt_file_name, wave_file_name);
    }

    if (argc > 1) {
        wave_file_name = argv[1];
    }

    printf("Opening %s for demodulation...\n", wave_file_name);
    int fd = open(wave_file_name, O_RDONLY);
    if (fd == -1) {
        perror("unable to open wave file");
        return 1;
    }

    int out_fd = open(output_file_name, O_WRONLY | O_CREAT | O_TRUNC, 0777);
    if (out_fd == -1) {
        perror("unable to open filtered file");
        return 1;
    }

    if (-1 == fstat(fd, &statbuf)) {
        perror("unable to determine size of buffer");
        return 1;
    }

    if (read(fd, wave_header, sizeof(wave_header)) != sizeof(wave_header)) {
        fprintf(stderr, "couldn't read wave header");
    }

    int baselen = statbuf.st_size - sizeof(wave_header);
    wave_buffer = malloc(baselen * sizeof(int16_t));
    if ((unsigned int)read(fd, wave_buffer, baselen) !=
        statbuf.st_size - sizeof(wave_header)) {
        fprintf(stderr, "short read of wave file\n");
        return 1;
    }

    wave_filtered = malloc(baselen * sizeof(int16_t));
    memset(wave_filtered, 0, baselen * sizeof(int16_t));

    int samplerate = *((unsigned int *)&(wave_header[24]));
    printf("samplerate: %d\n", samplerate);

#if 0    
    // double the sampling rate through interpolation and filtering
    baselen = baselen * 2;
    wave_upsampled = malloc(baselen * sizeof(int16_t));
    wave_upsampled_filtered = malloc(baselen * sizeof(int16_t));

    memset(wave_upsampled, 0, baselen * sizeof(int16_t));
    memset(wave_upsampled_filtered, 0, baselen * sizeof(int16_t));
    samplerate = samplerate * 2;
    *((unsigned int *)&(wave_header[24])) = samplerate;

    for (int i = 0; i < baselen / 2; i++) {
        wave_upsampled[i * 2] = wave_buffer[i];
        wave_upsampled[i * 2 + 1] = 0;
    }

    arm_fir_instance_f32 fir;
    arm_fir_init_f32(&fir, FIR_STAGES, fir_coefficients, fir_state,
                     SAMPLES_PER_PERIOD);
    for (int sample_offset = 0; sample_offset < baselen;
         sample_offset += SAMPLES_PER_PERIOD) {
        // Apply our bandpass filter
        float32_t firwindow[SAMPLES_PER_PERIOD];
        arm_q15_to_float(&(wave_upsampled[sample_offset]), firwindow,
                         SAMPLES_PER_PERIOD);
        static float32_t filtered_samples[SAMPLES_PER_PERIOD];
        arm_fir_f32(&fir, firwindow, filtered_samples, SAMPLES_PER_PERIOD);
        arm_float_to_q15(filtered_samples,
                         &(wave_upsampled_filtered[sample_offset]),
                         SAMPLES_PER_PERIOD);
    }

    write_wav(wave_upsampled_filtered, baselen, "upsampled_filtered.wav");
#else
    // apply the BPF
    wave_filtered = malloc(baselen * sizeof(int16_t));

    memset(wave_filtered, 0, baselen * sizeof(int16_t));
    //for(int i = 0; i < FIR_STAGES; i++) {
    //  printf("%0.003f\n", fir_coefficients[i]);
    //}

    arm_fir_instance_f32 fir;
    arm_fir_init_f32(&fir, FIR_STAGES, fir_coefficients, fir_state,
                     SAMPLES_PER_PERIOD);
    for (int sample_offset = 0; sample_offset < baselen;
         sample_offset += SAMPLES_PER_PERIOD) {
        // Apply our bandpass filter
        float32_t firwindow[SAMPLES_PER_PERIOD];
        arm_q15_to_float(&(wave_buffer[sample_offset]), firwindow,
                         SAMPLES_PER_PERIOD);
        static float32_t filtered_samples[SAMPLES_PER_PERIOD];
        arm_fir_f32(&fir, firwindow, filtered_samples, SAMPLES_PER_PERIOD);
        arm_float_to_q15(filtered_samples,
                         &(wave_filtered[sample_offset]),
                         SAMPLES_PER_PERIOD);
    }

    write_wav(wave_filtered, baselen, "bpsk_filtered.wav");
#endif
    
    // now try to run a PLL to "lock" onto the signal
    nco_st.samplerate = (float32_t)samplerate;
    nco_st.freq = (float32_t)CARRIER_TONE;
    nco_st.phase = 0.0;

#if 0    
    /////// test the NCO
    int16_t *i_int;
    int16_t *q_int;
    i_int = malloc(baselen * sizeof(int16_t));
    q_int = malloc(baselen * sizeof(int16_t));
    memset(i_int, 0, baselen);
    memset(q_int, 0, baselen);

    for (int i = 0; i < baselen; i++) {
        float32_t i_flt;
        float32_t q_flt;

        nco(0.0, (uint32_t)i, &i_flt, &q_flt);
        i_int[i] = (int16_t)(i_flt * 8000);
        q_int[i] = (int16_t)(q_flt * 8000);
    }
    wave_header[22] = 2; // stereo
    uint32_t byterate = *((uint32_t *)&(wave_header[28]));
    *((uint32_t *)&(wave_header[28])) = byterate * 2;
    uint32_t length = *((uint32_t *)&(wave_header[40]));
    *((uint32_t *)&(wave_header[40])) =
        length *
        4; // *4 because we forgot to expand it when we doubled sample rate
    write_wav_stereo(i_int, q_int, baselen, "quadrature.wav");
    ///////
#else
    wave_header[22] = 2; // stereo
    uint32_t byterate = *((uint32_t *)&(wave_header[28]));
    *((uint32_t *)&(wave_header[28])) = byterate * 2;
    uint32_t length = *((uint32_t *)&(wave_header[40]));
    *((uint32_t *)&(wave_header[40])) =
        length * 2;
    printf("wave length: %d\n", length * 2);
#endif

    // multiply the NCO output times input signal
    int16_t *input;
    input = wave_filtered;  // wave_buffer if not filtered, wave_filtered if filtered
    int16_t *i_loop;
    int16_t *q_loop;
    i_loop = malloc(baselen * sizeof(int16_t));
    q_loop = malloc(baselen * sizeof(int16_t));
    memset(i_loop, 0, baselen);
    memset(q_loop, 0, baselen);

    float32_t agc = 1.0;
    const float32_t agc_step = 0.05;
    const float32_t agc_target_hi = 0.5;
    const float32_t agc_target_low = 0.25;

    arm_fir_instance_f32 i_lpf;
    arm_fir_instance_f32 q_lpf;
    arm_fir_init_f32(&i_lpf, LPF_STAGES, lpf_coefficients, i_lpf_state,
                     SAMPLES_PER_PERIOD);
    arm_fir_init_f32(&q_lpf, LPF_STAGES, lpf_coefficients, q_lpf_state,
                     SAMPLES_PER_PERIOD);
    float32_t nco_error = 0.0;
    for (int sample_offset = 0; sample_offset < baselen;
         sample_offset += SAMPLES_PER_PERIOD) {

        float32_t loopwindow[SAMPLES_PER_PERIOD];
        arm_q15_to_float(&(input[sample_offset]), loopwindow,
                         SAMPLES_PER_PERIOD);

	// scan for agc value. note q15_to_float normalizes an int16_t to +1.0/-1.0.
	int above_hi = 0;
	int above_low = 0;
	for( int i = 0; i < SAMPLES_PER_PERIOD; i++ ) {
	  loopwindow[i] = loopwindow[i] * agc; // compute the agc
	  
	  // then check if we're out of bounds
	  if( loopwindow[i] > agc_target_low ) {
	    above_low = 1;
	  }
	  if( loopwindow[i] > agc_target_hi ) {
	    above_hi = 1;
	  }
	}
	if( above_hi ) {
	  agc = agc * (1.0-agc_step);
	} else if( !above_low ) {
	  agc = agc * (1.0+agc_step);
	}
	
        float32_t i_samps[SAMPLES_PER_PERIOD];
        float32_t q_samps[SAMPLES_PER_PERIOD];
        for (int i = 0; i < SAMPLES_PER_PERIOD; i++) {
            nco(nco_error, (uint32_t)(i + sample_offset), &(i_samps[i]),
                &(q_samps[i]));
        }
        //arm_q15_to_float(&(i_int[sample_offset]), i_samps, SAMPLES_PER_PERIOD);
        //arm_q15_to_float(&(q_int[sample_offset]), q_samps, SAMPLES_PER_PERIOD);

        static float32_t i_mult_samps[SAMPLES_PER_PERIOD];
        static float32_t q_mult_samps[SAMPLES_PER_PERIOD];
        arm_mult_f32(loopwindow, i_samps, i_mult_samps, SAMPLES_PER_PERIOD);
        arm_mult_f32(loopwindow, q_samps, q_mult_samps, SAMPLES_PER_PERIOD);

        static float32_t i_lpf_samples[SAMPLES_PER_PERIOD];
        static float32_t q_lpf_samples[SAMPLES_PER_PERIOD];
        arm_fir_f32(&i_lpf, i_mult_samps, i_lpf_samples, SAMPLES_PER_PERIOD);
        arm_fir_f32(&q_lpf, q_mult_samps, q_lpf_samples, SAMPLES_PER_PERIOD);

        arm_float_to_q15(i_lpf_samples, &(i_loop[sample_offset]),
                         SAMPLES_PER_PERIOD);
        arm_float_to_q15(q_lpf_samples, &(q_loop[sample_offset]),
                         SAMPLES_PER_PERIOD);

        float32_t errorwindow[SAMPLES_PER_PERIOD];
        arm_mult_f32(i_lpf_samples, q_lpf_samples, errorwindow,
                     SAMPLES_PER_PERIOD);
        float32_t avg = 0;
        for (int i = 0; i < SAMPLES_PER_PERIOD; i++) {
            avg += errorwindow[i];
        }
        avg /= ((float32_t)SAMPLES_PER_PERIOD);
        nco_error = -(avg);
        // printf("err: %0.04f\n", nco_error);

        static float bit_pll = 0;
        static const float pll_incr = (BAUD_RATE / (float)SAMPLE_RATE);
        for (unsigned int j = 0; j < SAMPLES_PER_PERIOD; j++) {
            if (bit_pll < 0.5 && (bit_pll + pll_incr) >= 0.5) {
                static int bit_acc = 0;
                static int last_state = 0;
                int state = i_lpf_samples[j] > 0.0;
                int bit = !(state ^ last_state);
                last_state = state;

                bit_acc = (bit_acc << 1) | bit;
                if ((bit_acc & 3) == 0) {
                    print_char(bit_acc);
                    bit_acc = 0;
                }
            }
            bit_pll += pll_incr;
            if (bit_pll >= 1) {
                bit_pll -= 1;
            }
        }
    }
    printf("\n");
    write_wav_stereo(i_loop, q_loop, baselen, "quadrature_loop.wav");

    /*
    filter_u16(wave_buffer, wave_filtered,
               (statbuf.st_size - sizeof(wave_header)) / sizeof(*wave_buffer));
    
    if (write(out_fd, wave_header, sizeof(wave_header)) == -1) {
        perror("error");
    }
    if (write(out_fd, wave_filtered, statbuf.st_size - sizeof(wave_header)) ==
        -1) {
        perror("error");
    }

    demodulate_u16((int16_t *)(wave_buffer + 44), (statbuf.st_size - 44) / 2);

    printf("Done demodulating\n");
    */
    return 0;
}
