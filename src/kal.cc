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

/*
 * kal
 *
 *    Two functions:
 *
 * 	1.  Calculates the frequency offset between a local GSM tower and the
 * 	    USRP clock.
 *
 *	2.  Identifies the frequency of all GSM base stations in a given band.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#else
#define PACKAGE_VERSION "custom build"
#endif /* HAVE_CONFIG_H */

#include <stdio.h>
#include <stdlib.h>
#ifndef _WIN32
#include <unistd.h>
#include <sys/time.h>
#endif
#ifdef HAVE_LIBGEN_H
#include <libgen.h>
#endif
#include <string.h>

#include <errno.h>

#include "usrp_source.h"
#include "fcch_detector.h"
#include "arfcn_freq.h"
#include "offset.h"
#include "c0_detect.h"
#include "version.h"
#ifdef _WIN32
#include <getopt.h>
#define basename(x) "meh"
#define strtof strtod
#endif

#define GSM_RATE (1625000.0 / 6.0)


int g_verbosity = 0;
int g_debug = 0;

void usage(char *prog) {

	printf("kalibrate v%s-rtl, Copyright (c) 2010, Joshua Lackey\n", kal_version_string);
	printf("modified for use with rtl-sdr devices, Copyright (c) 2012, Steve Markgraf");
	printf("\nUsage:\n");
	printf("\tGSM Base Station Scan:\n");
	printf("\t\t%s <-s band indicator> [options]\n", basename(prog));
	printf("\n");
	printf("\tClock Offset Calculation:\n");
	printf("\t\t%s <-f frequency | -c channel> [options]\n", basename(prog));
	printf("\n");
	printf("Where options are:\n");
	printf("\t-s\tband to scan (GSM850, GSM-R, GSM900, EGSM, DCS, PCS)\n");
	printf("\t-f\tfrequency of nearby GSM base station\n");
	printf("\t-c\tchannel of nearby GSM base station\n");
	printf("\t-b\tband indicator (GSM850, GSM-R, GSM900, EGSM, DCS, PCS)\n");
	printf("\t-g\tgain in dB\n");
	printf("\t-d\trtl-sdr device index\n");
	printf("\t-e\tinitial frequency error in ppm\n");
#if HAVE_DITHERING == 1
	printf("\t-N\tdisable dithering (default: dithering enabled)\n");
#endif
	printf("\t-E\tmanual frequency offset in hz\n");
	printf("\t-v\tverbose\n");
	printf("\t-D\tenable debug messages\n");
	printf("\t-h\thelp\n");
	exit(-1);
}


int main(int argc, char **argv) {

	char *endptr;
	int c, antenna = 1, bi = BI_NOT_DEFINED, chan = -1, bts_scan = 0;
	int ppm_error = 0, hz_adjust = 0;
	int dithering = true;
	unsigned int subdev = 0, decimation = 192;
	long int fpga_master_clock_freq = 52000000;
	float gain = 0;
	double freq = -1.0, fd;
	usrp_source *u;

	while((c = getopt(argc, argv, "f:c:s:b:R:A:g:e:E:Nd:vDh?")) != EOF) {
		switch(c) {
			case 'f':
				freq = strtod(optarg, 0);
				break;

			case 'c':
				chan = strtoul(optarg, 0, 0);
				break;

			case 's':
				if((bi = str_to_bi(optarg)) == -1) {
					fprintf(stderr, "error: bad band "
					   "indicator: ``%s''\n", optarg);
					usage(argv[0]);
				}
				bts_scan = 1;
				break;

			case 'b':
				if((bi = str_to_bi(optarg)) == -1) {
					fprintf(stderr, "error: bad band "
					   "indicator: ``%s''\n", optarg);
					usage(argv[0]);
				}
				break;

			case 'R':
				errno = 0;
				subdev = strtoul(optarg, &endptr, 0);
				if((!errno) && (endptr != optarg))
					break;
				if(tolower(*optarg) == 'a') {
					subdev = 0;
				} else if(tolower(*optarg) == 'b') {
					subdev = 1;
				} else {
					fprintf(stderr, "error: bad side: "
					   "``%s''\n",
					   optarg);
					usage(argv[0]);
				}
				break;

			case 'A':
				if(!strcmp(optarg, "RX2")) {
					antenna = 1;
				} else if(!strcmp(optarg, "TX/RX")) {
					antenna = 0;
				} else {
					errno = 0;
					antenna = strtoul(optarg, &endptr, 0);
					if(errno || (endptr == optarg)) {
						fprintf(stderr, "error: bad "
						   "antenna: ``%s''\n",
						   optarg);
						usage(argv[0]);
					}
				}
				break;

			case 'g':
				gain = strtof(optarg, 0) * 10;
				break;

			case 'F':
				fpga_master_clock_freq = strtol(optarg, 0, 0);
				if(!fpga_master_clock_freq)
					fpga_master_clock_freq = (long int)strtod(optarg, 0); 

				// was answer in MHz?
				if(fpga_master_clock_freq < 1000) {
					fpga_master_clock_freq *= 1000000;
				}
				break;

			case 'e':
				ppm_error = strtol(optarg, 0, 0);
				break;

			case 'N':
				dithering = false;
				break;

			case 'E':
				hz_adjust = strtol(optarg, 0, 0);
				break;

			case 'd':
				subdev = strtol(optarg, 0, 0);
				break;

			case 'v':
				g_verbosity++;
				break;

			case 'D':
				g_debug = 1;
				break;

			case 'h':
			case '?':
			default:
				usage(argv[0]);
				break;
		}

	}

	// sanity check frequency / channel
	if(bts_scan) {
		if(bi == BI_NOT_DEFINED) {
			fprintf(stderr, "error: scaning requires band\n");
			usage(argv[0]);
		}
	} else {
		if(freq < 0.0) {
			if(chan < 0) {
				fprintf(stderr, "error: must enter channel or "
				   "frequency\n");
				usage(argv[0]);
			}
			if((freq = arfcn_to_freq(chan, &bi)) < 869e6)
				usage(argv[0]);
		}
		if((freq < 869e6) || (2e9 < freq)) {
			fprintf(stderr, "error: bad frequency: %lf\n", freq);
			usage(argv[0]);
		}
		chan = freq_to_arfcn(freq, &bi);
	}

#if 0
	// sanity check clock
	if(fpga_master_clock_freq < 48000000) {
		fprintf(stderr, "error: FPGA master clock too slow: %li\n", fpga_master_clock_freq);
		usage(argv[0]);
	}

	// calculate decimation -- get as close to GSM rate as we can
	fd = (double)fpga_master_clock_freq / GSM_RATE;
	decimation = (unsigned int)fd;
#endif
	if(g_debug) {
#ifdef D_HOST_OSX
		printf("debug: Mac OS X version\n");
#endif
		printf("debug: FPGA Master Clock Freq:\t%li\n", fpga_master_clock_freq);
		printf("debug: decimation            :\t%u\n", decimation);
		printf("debug: RX Subdev Spec        :\t%s\n", subdev? "B" : "A");
		printf("debug: Antenna               :\t%s\n", antenna? "RX2" : "TX/RX");
		printf("debug: Gain                  :\t%f\n", gain);
	}

	u = new usrp_source(decimation, fpga_master_clock_freq);
	if(!u) {
		fprintf(stderr, "error: usrp_source\n");
		return -1;
	}
	if(u->open(subdev) == -1) {
		fprintf(stderr, "error: usrp_source::open\n");
		return -1;
	}

	/* Enable/disable dithering */
	if (!u->set_dithering(dithering)) {
		fprintf(stderr, "error: usrp_source::set_dithering\n");
	}

//	u->set_antenna(antenna);
	if (gain != 0) {
		if(!u->set_gain(gain)) {
			fprintf(stderr, "error: usrp_source::set_gain\n");
			return -1;
		}
	}

	if (ppm_error != 0) {
		if(u->set_freq_correction(ppm_error) < 0) {
			fprintf(stderr, "error: usrp_source::set_freq_correction\n");
			return -1;
		}
	}

	if(!bts_scan) {
		if(!u->tune(freq+hz_adjust)) {
			fprintf(stderr, "error: usrp_source::tune\n");
			return -1;
		}

		double tuner_error = u->m_center_freq - freq;

		fprintf(stderr, "%s: Calculating clock frequency offset.\n",
		   basename(argv[0]));
		fprintf(stderr, "Using %s channel %d (%.1fMHz)\n",
		   bi_to_str(bi), chan, freq / 1e6);
		fprintf(stderr, "Tuned to %.6fMHz (reported tuner error: %.0fHz)\n",
		   u->m_center_freq / 1e6, tuner_error);

		return offset_detect(u, hz_adjust, tuner_error);
	}

	fprintf(stderr, "%s: Scanning for %s base stations.\n",
	   basename(argv[0]), bi_to_str(bi));

	return c0_detect(u, bi);
}
