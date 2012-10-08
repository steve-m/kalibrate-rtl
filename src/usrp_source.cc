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
#include <unistd.h>
#include <string.h>
#include <pthread.h>
#include <math.h>
#include <complex>

#include <usrp/usrp_standard.h>
#include <usrp/usrp_subdev_spec.h>
#include <usrp/usrp_dbid.h>

#include "usrp_source.h"

extern int g_verbosity;


usrp_source::usrp_source(float sample_rate, long int fpga_master_clock_freq) {

	m_fpga_master_clock_freq = fpga_master_clock_freq;
	m_desired_sample_rate = sample_rate;
	m_sample_rate = 0.0;
	m_decimation = 0;
	m_u_rx.reset();
	m_db_rx.reset();
	m_cb = new circular_buffer(CB_LEN, sizeof(complex), 0);

	pthread_mutex_init(&m_u_mutex, 0);
}


usrp_source::usrp_source(unsigned int decimation, long int fpga_master_clock_freq) {

	m_fpga_master_clock_freq = fpga_master_clock_freq;
	m_sample_rate = 0.0;
	m_u_rx.reset();
	m_db_rx.reset();
	m_cb = new circular_buffer(CB_LEN, sizeof(complex), 0);

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
	pthread_mutex_destroy(&m_u_mutex);
}


void usrp_source::stop() {

	pthread_mutex_lock(&m_u_mutex);
	if(m_db_rx)
		m_db_rx->set_enable(0);
	if(m_u_rx)
		m_u_rx->stop();
	pthread_mutex_unlock(&m_u_mutex);
}


void usrp_source::start() {

	pthread_mutex_lock(&m_u_mutex);
	if(m_db_rx)
		m_db_rx->set_enable(1);
	if(m_u_rx)
		m_u_rx->start();
	pthread_mutex_unlock(&m_u_mutex);
}


void usrp_source::calculate_decimation() {

	float decimation_f;

	decimation_f = (float)m_u_rx->fpga_master_clock_freq() / m_desired_sample_rate;
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

	int r;
	usrp_tune_result tr;

	pthread_mutex_lock(&m_u_mutex);
	r = m_u_rx->tune(0, m_db_rx, freq, &tr);
	pthread_mutex_unlock(&m_u_mutex);

	return r;
}


bool usrp_source::set_antenna(int antenna) {

	return m_db_rx->select_rx_antenna(antenna);
}


bool usrp_source::set_gain(float gain) {

	float min = m_db_rx->gain_min(), max = m_db_rx->gain_max();

	if((gain < 0.0) || (1.0 < gain))
		return false;

	return m_db_rx->set_gain(min + gain * (max - min));
}


/*
 * open() should be called before multiple threads access usrp_source.
 */
int usrp_source::open(unsigned int subdev) {

	int do_set_decim = 0;
	usrp_subdev_spec ss(subdev, 0);

	if(!m_u_rx) {
		if(!m_decimation) {
			do_set_decim = 1;
			m_decimation = 4;
		}
		if(!(m_u_rx = usrp_standard_rx::make(0, m_decimation,
		   NCHAN, INITIAL_MUX, usrp_standard_rx::FPGA_MODE_NORMAL,
		   FUSB_BLOCK_SIZE, FUSB_NBLOCKS, FPGA_FILENAME()))) {
			fprintf(stderr, "error: usrp_standard_rx::make: "
			   "failed!\n");
			return -1;
		}
		m_u_rx->set_fpga_master_clock_freq(m_fpga_master_clock_freq);
		m_u_rx->stop();

		if(do_set_decim) {
			calculate_decimation();
		}

		m_u_rx->set_decim_rate(m_decimation);
		m_sample_rate = (double)m_u_rx->fpga_master_clock_freq() / m_decimation;

		if(g_verbosity > 1) {
			fprintf(stderr, "FPGA clock : %ld\n", m_u_rx->fpga_master_clock_freq());
			fprintf(stderr, "Decimation : %u\n", m_decimation);
			fprintf(stderr, "Sample rate: %f\n", m_sample_rate);
		}
	}
	if(!m_u_rx->is_valid(ss)) {
		fprintf(stderr, "error: invalid daughterboard\n");
		return -1;
	}
	if(!(m_db_rx = m_u_rx->selected_subdev(ss))) {
		fprintf(stderr, "error: no daughterboard\n");
		return -1;
	}

	m_u_rx->set_mux(m_u_rx->determine_rx_mux_value(ss));

	set_gain(0.45);
	m_db_rx->select_rx_antenna(1); // this is a nop for most db

	return 0;
}


#define USB_PACKET_SIZE 512

int usrp_source::fill(unsigned int num_samples, unsigned int *overrun_i) {

	bool overrun;
	unsigned char ubuf[USB_PACKET_SIZE];
	short *s = (short *)ubuf;
	unsigned int i, j, space, overruns = 0;
	complex *c;

	while((m_cb->data_available() < num_samples) && (m_cb->space_available() > 0)) {

		// read one usb packet from the usrp
		pthread_mutex_lock(&m_u_mutex);
		if(m_u_rx->read(ubuf, sizeof(ubuf), &overrun) != sizeof(ubuf)) {
			pthread_mutex_unlock(&m_u_mutex);
			fprintf(stderr, "error: usrp_standard_rx::read\n");
			return -1;
		}
		pthread_mutex_unlock(&m_u_mutex);
		if(overrun)
			overruns++;

		// write complex<short> input to complex<float> output
		c = (complex *)m_cb->poke(&space);

		// set space to number of complex items to copy
		if(space > (USB_PACKET_SIZE >> 2))
			space = USB_PACKET_SIZE >> 2;

		// write data
		for(i = 0, j = 0; i < space; i += 1, j += 2)
			c[i] = complex(s[j], s[j + 1]);

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
	fill(flush_count * USB_PACKET_SIZE, 0);
	m_cb->flush();

	return 0;
}
