#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <unistd.h>

#include "modulate.h"
const char *char_to_varcode(char c);

void modulate_init(struct modulate_state *state, uint32_t carrier,
                   uint32_t rate, float baud,
                   void (*write)(void *arg, void *data, unsigned int count),
                   void *arg) {
    state->cfg.rate = rate;
    state->cfg.carrier = carrier;
    state->cfg.baud = baud;
    state->cfg.pll_incr = (float)carrier / (float)rate;
    state->cfg.write = write;
    state->cfg.write_arg = arg;
    state->cfg.omega = (2.0 * M_PI * carrier) / rate;
    state->bit_pll = 0.0;
    state->baud_pll = 0.0;
    state->polarity = 0;
    return;
}

static void send_bit(struct modulate_state *state, int bit) {
    int16_t low = -32768/4;
    int16_t high = 32767/4;
    int16_t zero = 0;
    int16_t w_low;
    int16_t w_high;
    int sign = 1;
    float amplitude;
    int sign_change;

    printf("%d", bit);

    if (bit == 0)
      sign_change = (1==1); 
    else
      sign_change = (1==0);
    state->polarity ^= !bit;
    if (state->polarity) {
        sign = -1;
        low = 32767/4;
        high = -32768/4;
    }

    int bit_count = 0;

    while (bit_count < 32) {
	if(!sign_change)
	  amplitude = -1.0;
	else{
	  if(bit_count == 16)
		  state->baud_pll += 0.5;
	  if(bit_count < 16)
	    amplitude =  sqrtf( ( cosf(M_PI * bit_count / 16) + 1) / 2);
	  else
	    amplitude = -sqrtf( ( cosf(M_PI * bit_count / 16) + 1) / 2);
	}
	w_low = low * amplitude;
	w_high = high * amplitude;
        if (state->baud_pll > ((float)11/12)) {
            state->cfg.write(state->cfg.write_arg, &zero, sizeof(zero));
        } else if (state->baud_pll > ((float)7/12)) {
            state->cfg.write(state->cfg.write_arg, &w_low, sizeof(low));
        } else if (state->baud_pll > ((float)5/12)) {
            state->cfg.write(state->cfg.write_arg, &zero, sizeof(zero));
        } else if (state->baud_pll > ((float)1/12)) {
            state->cfg.write(state->cfg.write_arg, &w_high, sizeof(high));
	} else {
            state->cfg.write(state->cfg.write_arg, &zero, sizeof(zero));
        }
        state->baud_pll += state->cfg.pll_incr;
        if (state->baud_pll > 1.0) {
            bit_count++;
            state->baud_pll -= 1.0;
        }
    }
//    if(sign_change)
//      state->baud_pll += 0.5;
}

void modulate_string(struct modulate_state *state, const char *string) {
    char c;

    printf("Sending: ");
    for (unsigned int i = 0; i < 8; i++) {
        send_bit(state, 0);
    }

    while ((c = *string++) != '\0') {
        const char *bit_pattern = char_to_varcode(c);

        int bit = 0;
        while ((bit = *bit_pattern++) != '\0') {
            send_bit(state, bit != '0');
        }
        send_bit(state, 0);
        send_bit(state, 0);
    }
    printf("\n");
}
