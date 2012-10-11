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
#define _USE_MATH_DEFINES
#include <math.h>


void display_freq(float f) {

	if(f >= 0) {
		printf("+ ");
	} else {
		printf("- ");
		f = -f;
	}
	if(fabs(f) >= 1e9) {
		printf("%.3fGHz", f / 1e9);
		return;
	}
	if(fabs(f) >= 1e6) {
		printf("%.1fMHz", f / 1e6);
		return;
	}
	if(fabs(f) >= 1e3) {
		printf("%.3fkHz", f / 1e3);
		return;
	}
	if(fabs(f) >= 1e2) {
		printf("%.0fHz", f);
		return;
	}
	if(fabs(f) >= 1e1) {
		printf(" %.0fHz", f);
		return;
	}
	printf("  %.0fHz", f);
}


void sort(float *b, unsigned int len) {

	for(unsigned int i = 0; i < len; i++) {
		for(unsigned int j = i + 1; j < len; j++) {
			if(b[j] < b[i]) {
				float t = b[i];
				b[i] = b[j];
				b[j] = t;
			}
		}
	}
}


double avg(float *b, unsigned int len, float *stddev) {

	unsigned int i;
	double t = 0.0, a = 0.0, s = 0.0;

	for(i = 0; i < len; i++)
		t += b[i];
	a = t / len;
	if(stddev) {
		for(i = 0; i < len; i++)
			s += (b[i] - a) * (b[i] - a);
		s /= len;
		s = sqrt(s);
		*stddev = s;
	}

	return a;
}
