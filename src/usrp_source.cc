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


#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#ifndef _WIN32
#include <unistd.h>
#endif
#include <string.h>
#include <pthread.h>
#define _USE_MATH_DEFINES
#include <math.h>
#include <complex>

#include "usrp_source.h"

extern int g_verbosity;


#ifdef _WIN32
inline double round(double x) { return floor(x + 0.5); }
#endif

usrp_source::usrp_source(float sample_rate, long int fpga_master_clock_freq) {

	m_fpga_master_clock_freq = fpga_master_clock_freq;
	m_desired_sample_rate = sample_rate;
	m_center_freq = 0.0;
	m_sample_rate = 0.0;
	m_decimation = 0;
	m_cb = new circular_buffer(CB_LEN, sizeof(complex), 0);
	m_freq_corr = 0;

	pthread_mutex_init(&m_u_mutex, 0);
}


usrp_source::usrp_source(unsigned int decimation, long int fpga_master_clock_freq) {

	m_fpga_master_clock_freq = fpga_master_clock_freq;
	m_center_freq = 0.0;
	m_sample_rate = 0.0;
	m_cb = new circular_buffer(CB_LEN, sizeof(complex), 0);
	m_freq_corr = 0;

	pthread_mutex_init(&m_u_mutex, 0);

	m_decimation = decimation & ~1;
	if(m_decimation < 4)
		m_decimation = 4;
	if(m_decimation > 256)
		m_decimation = 256;
}


usrp_source::~usrp_source() {

	stop();
	delete m_cb;
	rtlsdr_close(dev);
	pthread_mutex_destroy(&m_u_mutex);
}


void usrp_source::stop() {

	pthread_mutex_lock(&m_u_mutex);
	pthread_mutex_unlock(&m_u_mutex);
}


void usrp_source::start() {

	pthread_mutex_lock(&m_u_mutex);
	pthread_mutex_unlock(&m_u_mutex);
}


void usrp_source::calculate_decimation() {

	float decimation_f;

//	decimation_f = (float)m_u_rx->fpga_master_clock_freq() / m_desired_sample_rate;
	m_decimation = (unsigned int)round(decimation_f) & ~1;

	if(m_decimation < 4)
		m_decimation = 4;
	if(m_decimation > 256)
		m_decimation = 256;
}


float usrp_source::sample_rate() {

	return m_sample_rate;

}


int usrp_source::tune(double freq) {

	int r = 0;

	pthread_mutex_lock(&m_u_mutex);
	if (freq != m_center_freq) {
		r = rtlsdr_set_center_freq(dev, (uint32_t)freq);

		if (r < 0)
			fprintf(stderr, "Tuning to %u Hz failed!\n", (uint32_t)freq);
		else
			m_center_freq = rtlsdr_get_center_freq(dev);
	}

	pthread_mutex_unlock(&m_u_mutex);

	return 1; //(r < 0) ? 0 : 1;
}

int usrp_source::set_freq_correction(int ppm) {
	m_freq_corr = ppm;
	return rtlsdr_set_freq_correction(dev, ppm);
}

bool usrp_source::set_antenna(int antenna) {

	return 0;
}

bool usrp_source::set_dithering(bool enable) {
#if HAVE_DITHERING == 1
	return (bool)(!rtlsdr_set_dithering(dev, (int)enable));
#else
	return true;
#endif
}

bool usrp_source::set_gain(float gain) {
	int r, g = gain * 10;

	/* Enable manual gain */
	r = rtlsdr_set_tuner_gain_mode(dev, 1);
	if (r < 0)
		fprintf(stderr, "WARNING: Failed to enable manual gain.\n");

	fprintf(stderr, "Setting gain: %.1f dB\n", gain/10);
	r = rtlsdr_set_tuner_gain(dev, g);

	return (r < 0) ? 0 : 1;
}


/*
 * open() should be called before multiple threads access usrp_source.
 */
int usrp_source::open(unsigned int subdev) {
	int i, r, device_count, count;
	uint32_t dev_index = subdev;
	uint32_t samp_rate = 270833;

	m_sample_rate = 270833.002142;

	device_count = rtlsdr_get_device_count();
	if (!device_count) {
		fprintf(stderr, "No supported devices found.\n");
		exit(1);
	}

	fprintf(stderr, "Found %d device(s):\n", device_count);
	for (i = 0; i < device_count; i++)
		fprintf(stderr, "  %d:  %s\n", i, rtlsdr_get_device_name(i));
	fprintf(stderr, "\n");

	fprintf(stderr, "Using device %d: %s\n",
		dev_index,
		rtlsdr_get_device_name(dev_index));

	r = rtlsdr_open(&dev, dev_index);
	if (r < 0) {
		fprintf(stderr, "Failed to open rtlsdr device #%d.\n", dev_index);
		exit(1);
	}

	/* Set the sample rate */
	r = rtlsdr_set_sample_rate(dev, samp_rate);
	if (r < 0)
		fprintf(stderr, "WARNING: Failed to set sample rate.\n");

	/* Reset endpoint before we start reading from it (mandatory) */
	r = rtlsdr_reset_buffer(dev);
	if (r < 0)
		fprintf(stderr, "WARNING: Failed to reset buffers.\n");

//	r = rtlsdr_set_offset_tuning(dev, 1);
//	if (r < 0)
//		fprintf(stderr, "WARNING: Failed to enable offset tuning\n");

	return 0;
}

#define USB_PACKET_SIZE		(2 * 16384)
#define FLUSH_SIZE		512


int usrp_source::fill(unsigned int num_samples, unsigned int *overrun_i) {

	unsigned char ubuf[USB_PACKET_SIZE];
	unsigned int i, j, space, overruns = 0;
	complex *c;
	int n_read;

	while((m_cb->data_available() < num_samples) && (m_cb->space_available() > 0)) {

		// read one usb packet from the usrp
		pthread_mutex_lock(&m_u_mutex);

		if (rtlsdr_read_sync(dev, ubuf, sizeof(ubuf), &n_read) < 0) {
			pthread_mutex_unlock(&m_u_mutex);
			fprintf(stderr, "error: usrp_standard_rx::read\n");
			return -1;
		}

		pthread_mutex_unlock(&m_u_mutex);

		// write complex<short> input to complex<float> output
		c = (complex *)m_cb->poke(&space);

		// set space to number of complex items to copy
		space = n_read / 2;

		// write data
		for(i = 0, j = 0; i < space; i += 1, j += 2)
			c[i] = complex((ubuf[j] - 127) * 256, (ubuf[j + 1] - 127) * 256);

		// update cb
		m_cb->wrote(i);
	}

	// if the cb is full, we left behind data from the usb packet
	if(m_cb->space_available() == 0) {
		fprintf(stderr, "warning: local overrun\n");
		overruns++;
	}

	if(overrun_i)
		*overrun_i = overruns;

	return 0;
}


int usrp_source::read(complex *buf, unsigned int num_samples,
   unsigned int *samples_read) {

	unsigned int n;

	if(fill(num_samples, 0))
		return -1;

	n = m_cb->read(buf, num_samples);

	if(samples_read)
		*samples_read = n;

	return 0;
}


/*
 * Don't hold a lock on this and use the usrp at the same time.
 */
circular_buffer *usrp_source::get_buffer() {

	return m_cb;
}


int usrp_source::flush(unsigned int flush_count) {

	m_cb->flush();
	fill(flush_count * FLUSH_SIZE, 0);
	m_cb->flush();

	return 0;
}
