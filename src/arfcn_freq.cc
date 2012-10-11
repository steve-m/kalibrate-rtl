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
#ifndef _WIN32
#include <unistd.h>
#endif
#include <string.h>
#include "arfcn_freq.h"


const char *bi_to_str(int bi) {

	switch(bi) {
		case GSM_850:
			return "GSM-850";

		case GSM_R_900:
			return "GSM-R-900";

		case GSM_900:
			return "GSM-900";

		case GSM_E_900:
			return "E-GSM-900";

		case DCS_1800:
			return "DCS-1800";

		case PCS_1900:
			return "PCS-1900";

		default:
			return "unknown band indicator";
	}
}


int str_to_bi(char *s) {

	if(!strcmp(s, "GSM850") || !strcmp(s, "GSM-850") || !strcmp(s, "850"))
		return GSM_850;

	if(!strcmp(s, "GSM-R") || !strcmp(s, "R-GSM"))
		return GSM_R_900;

	if(!strcmp(s, "GSM900") || !strcmp(s, "GSM-900") || !strcmp(s, "900"))
		return GSM_900;

	if(!strcmp(s, "EGSM") || !strcmp(s, "E-GSM") || !strcmp(s, "EGSM900") ||
	   !strcmp(s, "E-GSM900") || !strcmp(s, "E-GSM-900"))
		return GSM_E_900;

	if(!strcmp(s, "DCS") || !strcmp(s, "DCS1800") ||
	   !strcmp(s, "DCS-1800") || !strcmp(s, "1800"))
		return DCS_1800;
	
	if(!strcmp(s, "PCS") || !strcmp(s, "PCS1900") ||
	   !strcmp(s, "PCS-1900") || !strcmp(s, "1900"))
		return PCS_1900;

	return -1;
}


double arfcn_to_freq(int n, int *bi) {

	if((128 <= n) && (n <= 251)) {
		if(bi)
			*bi = GSM_850;
		return 824.2e6 + 0.2e6 * (n - 128) + 45.0e6;
	}

	if((1 <= n) && (n <= 124)) {
		if(bi && (*bi != GSM_E_900))
			*bi = GSM_900;
		return 890.0e6 + 0.2e6 * n + 45.0e6;
	}

	if(n == 0) {
		if(bi)
			*bi = GSM_E_900;
		return 935e6;
	}
	if((955 <= n) && (n <= 1023)) {
		if(bi) {
			if (975 <= n)
				*bi = GSM_E_900;
			else
				*bi = GSM_R_900;
		}
		return 890.0e6 + 0.2e6 * (n - 1024) + 45.0e6;
	}

	if((512 <= n) && (n <= 810)) {
		if(!bi) {
			fprintf(stderr, "error: ambiguous arfcn: %d\n", n);
			return -1.0;
		}

		if(*bi == DCS_1800)
			return 1710.2e6 + 0.2e6 * (n - 512) + 95.0e6;

		if(*bi == PCS_1900)
			return 1850.2e6 + 0.2e6 * (n - 512) + 80.0e6;

		fprintf(stderr, "error: bad (arfcn, band indicator) pair: "
		   "(%d, %s)\n", n, bi_to_str(*bi));
		return -1.0;
	}

	if((811 <= n) && (n <= 885)) {
		if(bi)
			*bi = DCS_1800;
		return 1710.2e6 + 0.2e6 * (n - 512) + 95.0e6;
	}

	fprintf(stderr, "error: bad arfcn: %d\n", n);
	return -1.0;
}


int freq_to_arfcn(double freq, int *bi) {

	if((869.2e6 <= freq) && (freq <= 893.8e6)) {
		if(bi)
			*bi = GSM_850;
		return (int)((freq - 869.2e6) / 0.2e6) + 128;
	}

	if((921.2e6 <= freq) && (freq <= 925.0e6)) {
		if(bi)
			*bi = GSM_R_900;
		return (int)((freq - 935e6) / 0.2e6) + 1024;
	}

	if((935.2e6 <= freq) && (freq <= 959.8e6)) {
		if(bi)
			*bi = GSM_900;
		return (int)((freq - 935e6) / 0.2e6);
	}

	if(935.0e6 == freq) {
		if(bi)
			*bi = GSM_E_900;
		return 0;
	}
	if((925.2e6 <= freq) && (freq <= 934.8e6)) {
		if(bi)
			*bi = GSM_E_900;
		return (int)((freq - 935e6) / 0.2e6) + 1024;
	}

	if((1805.2e6 <= freq) && (freq <= 1879.8e6)) {
		if(bi)
			*bi = DCS_1800;
		return (int)((freq - 1805.2e6) / 0.2e6) + 512;
	}

	if((1930.2e6 <= freq) && (freq <= 1989.8e6)) {
		if(bi)
			*bi = PCS_1900;
		return (int)((freq - 1930.2e6) / 0.2e6) + 512;
	}

	fprintf(stderr, "error: bad frequency: %lf\n", freq);
	return -1;
}


int first_chan(int bi) {

	switch(bi) {
		case GSM_850:
			return 128;

		case GSM_R_900:
			return 955;

		case GSM_900:
			return 1;

		case GSM_E_900:
			return 0;

		case DCS_1800:
			return 512;

		case PCS_1900:
			return 512;

		default:
			return -1;
	}

	return -1;
}


int next_chan_loop(int chan, int bi) {

	switch(bi) {
		case GSM_850:
			if((128 <= chan) && (chan < 251))
				return chan + 1;
			if(chan == 251)
				return 128;
			return -1;

		case GSM_R_900:
			if((955 <= chan) && (chan < 974))
				return chan + 1;
			if(chan == 974)
				return 955;
			return -1;

		case GSM_900:
			if((1 <= chan) && (chan < 124))
				return chan + 1;
			if(chan == 124)
				return 1;
			return -1;

		case GSM_E_900:
			if((0 <= chan) && (chan < 124))
				return chan + 1;
			if(chan == 124)
				return 975;
			if((975 <= chan) && (chan < 1023))
				return chan + 1;
			if(chan == 1023)
				return 0;
			return -1;

		case DCS_1800:
			if((512 <= chan) && (chan < 885))
				return chan + 1;
			if(chan == 885)
				return 512;
			return -1;

		case PCS_1900:
			if((512 <= chan) && (chan < 810))
				return chan + 1;
			if(chan == 810)
				return 512;
			return -1;

		default:
			return -1;
	}

	return -1;
}


int next_chan(int chan, int bi) {

	switch(bi) {
		case GSM_850:
			if((128 <= chan) && (chan < 251))
				return chan + 1;
			return -1;

		case GSM_R_900:
			if((955 <= chan) && (chan < 974))
				return chan + 1;
			return -1;

		case GSM_900:
			if((1 <= chan) && (chan < 124))
				return chan + 1;
			return -1;

		case GSM_E_900:
			if((0 <= chan) && (chan < 124))
				return chan + 1;
			if(chan == 124)
				return 975;
			if((975 <= chan) && (chan < 1023))
				return chan + 1;
			return -1;

		case DCS_1800:
			if((512 <= chan) && (chan < 885))
				return chan + 1;
			return -1;

		case PCS_1900:
			if((512 <= chan) && (chan < 810))
				return chan + 1;
			return -1;

		default:
			return -1;
	}

	return -1;
}
