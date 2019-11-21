/*
 * Copyright (c) 2010, Joshua Lackey
 * All rights reserved.
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 * 
 *     *  Redistributions of source code must retain the above copyright
 *        notice, this list of conditions and the following disclaimer.
 *
 *     *  Redistributions in binary form must reproduce the above copyright
 *        notice, this list of conditions and the following disclaimer in the
 *        documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "usrp_source.h"
#include "circular_buffer.h"
#include "fcch_detector.h"
#include "arfcn_freq.h"
#include "util.h"

extern int g_verbosity;

static const float ERROR_DETECT_OFFSET_MAX = 40e3;

#ifdef _WIN32
#define BUFSIZ 1024
#endif

static double vectornorm2(const complex *v, const unsigned int len) {

	unsigned int i;
	double e = 0.0;

	for(i = 0; i < len; i++)
		e += norm(v[i]);

	return e;
}


int c0_detect(usrp_source *u, int bi) {

#define GSM_RATE (1625000.0 / 6.0)
#define  NOTFOUND_MAX 10

	int i, chan_count;
	unsigned int overruns, b_len, frames_len, found_count, notfound_count, r;
	float offset, spower[BUFSIZ], effective_offset, min_offset, max_offset;
	double freq, sps, n, power[BUFSIZ], sum = 0, a;
	complex *b;
	circular_buffer *ub;
	fcch_detector *detector = new fcch_detector(u->sample_rate());

	if(bi == BI_NOT_DEFINED) {
		fprintf(stderr, "error: c0_detect: band not defined\n");
		return -1;
	}

	sps = u->sample_rate() / GSM_RATE;
	frames_len = (unsigned int)ceil((12 * 8 * 156.25 + 156.25) * sps);
	ub = u->get_buffer();

	// first, we calculate the power in each channel
	if(g_verbosity > 2) {
		fprintf(stderr, "calculate power in each channel:\n");
	}
	u->start();
	u->flush();
	for(i = first_chan(bi); i >= 0; i = next_chan(i, bi)) {
		freq = arfcn_to_freq(i, &bi);
		if(!u->tune(freq)) {
			fprintf(stderr, "error: usrp_source::tune\n");
			return -1;
		}

		do {
			u->flush();
			if(u->fill(frames_len, &overruns)) {
				fprintf(stderr, "error: usrp_source::fill\n");
				return -1;
			}
		} while(overruns);

		b = (complex *)ub->peek(&b_len);
		n = sqrt(vectornorm2(b, frames_len));
		power[i] = n;
		if(g_verbosity > 2) {
			fprintf(stderr, "\tchan %d (%.1fMHz):\tpower: %lf\n",
			   i, freq / 1e6, n);
		}
	}

	/*
	 * We want to use the average to determine which channels have
	 * power, and hence a possibility of being channel 0 on a BTS.
	 * However, some channels in the band can be extremely noisy.  (E.g.,
	 * CDMA traffic in GSM-850.)  Hence we won't consider the noisiest
	 * channels when we construct the average.
	 */
	chan_count = 0;
	for(i = first_chan(bi); i >= 0; i = next_chan(i, bi)) {
		spower[chan_count++] = power[i];
	}
	sort(spower, chan_count);

	// average the lowest %60
	a = avg(spower, chan_count - 4 * chan_count / 10, 0);

	if(g_verbosity > 0) {
		fprintf(stderr, "channel detect threshold: %lf\n", a);
	}

	// then we look for fcch bursts
	printf("%s:\n", bi_to_str(bi));
	found_count = 0;
	notfound_count = 0;
	sum = 0;
	i = first_chan(bi);
	do {
		if(power[i] <= a) {
			i = next_chan(i, bi);
			continue;
		}
		if (isatty(1)) {
			printf("...chan %i\r", i);
			fflush(stdout);
		}

		freq = arfcn_to_freq(i, &bi);
		if(!u->tune(freq)) {
			fprintf(stderr, "error: usrp_source::tune\n");
			return -1;
		}

		do {
			u->flush();
			if(u->fill(frames_len, &overruns)) {
				fprintf(stderr, "error: usrp_source::fill\n");
				return -1;
			}
		} while(overruns);

		b = (complex *)ub->peek(&b_len);
		r = detector->scan(b, b_len, &offset, 0);
		effective_offset = offset - GSM_RATE / 4;
		if(r && (fabsf(effective_offset) < ERROR_DETECT_OFFSET_MAX)) {
			// found
			if (found_count) {
				min_offset = fmin(min_offset, effective_offset);
				max_offset = fmax(max_offset, effective_offset);
			} else {
				min_offset = max_offset = effective_offset;
			}
			found_count++;
			printf("    chan: %4d (%.1fMHz ", i, freq / 1e6);
			display_freq(effective_offset);
			printf(")    power: %10.2f\n", power[i]);
			notfound_count = 0;
			i = next_chan(i, bi);
		} else {
			// not found
			notfound_count += 1;
			if(notfound_count >= NOTFOUND_MAX) {
				notfound_count = 0;
				i = next_chan(i, bi);
			}
		}
	} while(i >= 0);

	if (found_count == 1) {
		printf("\n");
		printf("Only one channel was found. This is unlikely and may "
			"indicate you need to provide a rough estimate of the initial "
			"PPM. It can be provided with the '-e' option. Try tuning against "
			"a local FM radio or other known frequency first.\n");
	}
	/*
	 * If the difference in offsets found is strangely large
	 */
	if (found_count > 1 && max_offset - min_offset > 1000) {
		printf("\n");
		printf("Difference of offsets between channels is >1kHz. This likely "
			"means that the correct PPM is too far away and you need to provide "
			"a rough estimate using the '-e' option. Try tuning against "
			"a local FM radio or other known frequency first.\n");
	}
	return 0;
}
