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
 * This is based on the algorithm found in the paper,
 *
 *	Varma, G. Narendra, Usha Sahu, and G. Prabhu Charan.  "Robust
 *	Frequency Burst Detection Algorithm for GSM / GPRS."
 *
 * The algorithm uses an adaptive filter to calculate the error difference from
 * a pure tone.  When the error goes low, the tone is detected.  When it goes
 * back high, the scan function returns and indicates the number of samples the
 * error was low.
 *
 * The following code is an original work and the above BSD-license should
 * apply.  However, the algorithm itself may be patented and any use of this
 * code should take that into consideration.
 */

#include <fftw3.h>

#include "circular_buffer.h"
#include "usrp_complex.h"

class fcch_detector {

public:
	fcch_detector(const float sample_rate, const unsigned int D = 8, const float p = 1.0 / 32.0, const float G = 1.0 / 12.5);
	~fcch_detector();
	unsigned int scan(const complex *s, const unsigned int s_len, float *offset, unsigned int *consumed);
	float freq_detect(const complex *s, const unsigned int s_len, float *pm);
	unsigned int update(const complex *s, unsigned int s_len);
	int next_norm_error(float *error);
	complex *dump_x(unsigned int *);
	complex *dump_y(unsigned int *);
	unsigned int filter_delay() { return m_filter_delay; };
	unsigned int get_delay();
	unsigned int filter_len();
	unsigned int x_buf_len();
	unsigned int y_buf_len();
	unsigned int x_purge(unsigned int);

private:
#define GSM_RATE (1625000.0 / 6.0)
#define FFT_SIZE 1024

	unsigned int	m_w_len,
			m_D,
			m_check_G,
			m_filter_delay,
			m_lpf_len,
			m_fcch_burst_len;
	float		m_sample_rate,
			m_p,
			m_G,
			m_e;
	complex 	*m_w;
	circular_buffer *m_x_cb,
			*m_y_cb,
			*m_e_cb;

	fftw_complex	*m_in, *m_out;
	fftw_plan	m_plan;
};
