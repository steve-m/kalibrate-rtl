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

#include <stdio.h>	// for debug
#include <stdlib.h>

#include <stdexcept>
#include <string.h>
#include "fcch_detector.h"

extern int g_debug;

static const char * const fftw_plan_name = ".kal_fftw_plan";


fcch_detector::fcch_detector(const float sample_rate, const unsigned int D,
   const float p, const float G) {

	FILE *plan_fp;
	char plan_name[BUFSIZ];
	const char *home;


	m_D = D;
	m_p = p;
	m_G = G;
	m_e = 0.0;

	m_sample_rate = sample_rate;
	m_fcch_burst_len =
	   (unsigned int)(148.0 * (m_sample_rate / GSM_RATE));

	m_filter_delay = 8;
	m_w_len = 2 * m_filter_delay + 1;
	m_w = new complex[m_w_len];
	memset(m_w, 0, sizeof(complex) * m_w_len);

	m_x_cb = new circular_buffer(8192, sizeof(complex), 0);
	m_y_cb = new circular_buffer(8192, sizeof(complex), 1);
	m_e_cb = new circular_buffer(1015808, sizeof(float), 0);

	m_in = (fftw_complex *)fftw_malloc(sizeof(fftw_complex) * FFT_SIZE);
	m_out = (fftw_complex *)fftw_malloc(sizeof(fftw_complex) * FFT_SIZE);
	if((!m_in) || (!m_out))
		throw std::runtime_error("fcch_detector: fftw_malloc failed!");
#ifndef _WIN32
	home = getenv("HOME");
	if(strlen(home) + strlen(fftw_plan_name) + 2 < sizeof(plan_name)) {
		strcpy(plan_name, home);
		strcat(plan_name, "/");
		strcat(plan_name, fftw_plan_name);
		if((plan_fp = fopen(plan_name, "r"))) {
			fftw_import_wisdom_from_file(plan_fp);
			fclose(plan_fp);
		}
		m_plan = fftw_plan_dft_1d(FFT_SIZE, m_in, m_out, FFTW_FORWARD,
		   FFTW_MEASURE);
		if((plan_fp = fopen(plan_name, "w"))) {
			fftw_export_wisdom_to_file(plan_fp);
			fclose(plan_fp);
		}
	} else
#endif
		m_plan = fftw_plan_dft_1d(FFT_SIZE, m_in, m_out, FFTW_FORWARD,
		   FFTW_ESTIMATE);
	if(!m_plan)
		throw std::runtime_error("fcch_detector: fftw plan failed!");
}


fcch_detector::~fcch_detector() {

	if(m_w) {
		delete[] m_w;
		m_w = 0;
	}
	if(m_x_cb) {
		delete m_x_cb;
		m_x_cb = 0;
	}
	if(m_y_cb) {
		delete m_y_cb;
		m_y_cb = 0;
	}
	if(m_e_cb) {
		delete m_e_cb;
		m_e_cb = 0;
	}
}


enum {
	LOW	= 0,
	HIGH	= 1
};

static unsigned int g_count = 0,
		    g_block_s = HIGH;


static inline void low_to_high_init() {

	g_count = 0;
	g_block_s = HIGH;
}


static inline unsigned int low_to_high(float e, float a) {

	unsigned int r = 0;

	if(e > a) {
		if(g_block_s == LOW) {
			r = g_count;
			g_block_s = HIGH;
			g_count = 0;
		}
		g_count += 1;
	} else {
		if(g_block_s == HIGH) {
			g_block_s = LOW;
			g_count = 0;
		}
		g_count += 1;
	}

	return r;
}


static inline int peak_valley(complex *c, unsigned int c_len, complex peak, unsigned int peak_i, unsigned int width, float *p2m) {

	float valley = 0.0;
	unsigned int i, valley_count = 0;

	// these constants aren't the best for all burst types
	for(i = 2; i < 2 + width; i++) {
		if(i <= peak_i) {
			valley += norm(c[peak_i - i]);
			valley_count += 1;
		}
		if(peak_i + i < c_len) {
			valley += norm(c[peak_i + i]);
			valley_count += 1;
		}
	}

	if(valley_count < 2) {
		fprintf(stderr, "error: bad valley_count\n");
		return -1;
	}
	valley = sqrtf(valley / (float)valley_count) + 0.00001;

	if(p2m)
		*p2m = sqrtf(norm(peak)) / valley;

	return 0;
}


static inline float sinc(const float x) {

	if((x <= -0.0001) || (0.0001 <= x))
		return sinf(x) / x;
	return 1.0;
}


static inline complex interpolate_point(const complex *s, const unsigned int s_len, const float s_i) {

	static const unsigned int filter_len = 21;

	int start, end, i;
	unsigned int d;
	complex point;

	d = (filter_len - 1) / 2;
	start = (int)(floor(s_i) - d);
	end = (int)(floor(s_i) + d + 1);
	if(start < 0)
		start = 0;
	if(end > (int)(s_len - 1))
		end = s_len - 1;
	for(point = 0.0, i = start; i <= end; i++)
		point += s[i] * sinc(M_PI * (i - s_i));
	return point;
}


static inline float peak_detect(const complex *s, const unsigned int s_len, complex *peak, float *avg_power) {

	unsigned int i;
	float max = -1.0, max_i = -1.0, sample_power, sum_power, early_i, late_i, incr;
	complex early_p, late_p, cmax;

	sum_power = 0;
	for(i = 0; i < s_len; i++) {
		sample_power = norm(s[i]);
		sum_power += sample_power;
		if(sample_power > max) {
			max = sample_power;
			max_i = i;
		}
	}
	early_i = (1 <= max_i)? (max_i - 1) : 0;
	late_i = (max_i + 1 < s_len)? (max_i + 1) : s_len - 1;

	incr = 0.5;
	while(incr > 1.0 / 1024.0) {
		early_p = interpolate_point(s, s_len, early_i);
		late_p = interpolate_point(s, s_len, late_i);
		if(norm(early_p) < norm(late_p))
			early_i += incr;
		else if(norm(early_p) > norm(late_p))
			early_i -= incr;
		else
			break;
		incr /= 2.0;
		late_i = early_i + 2.0;
	}
	max_i = early_i + 1.0;
	cmax = interpolate_point(s, s_len, max_i);

	if(peak)
		*peak = cmax;

	if(avg_power)
		*avg_power = (sum_power - norm(cmax)) / (s_len - 1);

	return max_i;
}


static inline float itof(float index, float sample_rate, unsigned int fft_size) {

	double r = index * (sample_rate / (double)fft_size);

	/*
	if(index > (double)fft_size / 2.0)
		return r - sample_rate;
	else
		return r;
	 */
	return r;
}


static inline unsigned int ftoi(float frequency, float sample_rate, unsigned int fft_size) {

	unsigned int r = (frequency / sample_rate) * fft_size;

	return r;
}


#ifndef MIN
#define MIN(a, b) (a)<(b)?(a):(b)
#endif /* !MIN */


float fcch_detector::freq_detect(const complex *s, const unsigned int s_len, float *pm) {

	unsigned int i, len;
	float max_i, avg_power;
	complex fft[FFT_SIZE], peak;

	len = MIN(s_len, FFT_SIZE);
	for(i = 0; i < len; i++) {
		m_in[i][0] = s[i].real();
		m_in[i][1] = s[i].imag();
	}
	for(i = len; i < FFT_SIZE; i++) {
		m_in[i][0] = 0;
		m_in[i][1] = 0;
	}

	fftw_execute(m_plan);

	for(i = 0; i < FFT_SIZE; i++) {
		fft[i] = complex(m_out[i][0], m_out[i][1]);
	}

	max_i = peak_detect(fft, FFT_SIZE, &peak, &avg_power);
	if(pm)
		*pm = norm(peak) / avg_power;
	return itof(max_i, m_sample_rate, FFT_SIZE);
}


static inline void display_complex(const complex *s, unsigned int s_len) {

	for(unsigned int i = 0; i < s_len; i++) {
		printf("%f\n", s[i].real());
		fprintf(stderr, "%f\n", s[i].imag());
	}
}


/*
 * scan:
 * 	1.  calculate average error
 * 	2.  find neighborhoods with low error that satisfy minimum length
 * 	3.  for each such neighborhood, take fft and calculate peak/mean
 * 	4.  if peak/mean > 50, then this is a valid finding.
 */
unsigned int fcch_detector::scan(const complex *s, const unsigned int s_len, float *offset, unsigned int *consumed) {

	static const float sps = m_sample_rate / (1625000.0 / 6.0);
	static const unsigned int MIN_FB_LEN = 100 * sps;
	static const unsigned int MIN_PM = 50; // XXX arbitrary, depends on decimation

	unsigned int len = 0, t, e_count, i, l_count, y_offset, y_len;
	float e, *a, loff = 0, pm;
	double sum = 0.0, avg, limit;
	const complex *y;

	// calculate the error for each sample
	while(len < s_len) {
		t = m_x_cb->write(s + len, 1);
		len += t;
		if(!next_norm_error(&e)) {
			m_e_cb->write(&e, 1);
			sum += e;
		}
	}
	if(consumed)
		*consumed = len;

	// calculate average error over entire buffer
	a = (float *)m_e_cb->peek(&e_count);
	avg = sum / (double)e_count;
	limit = 0.7 * avg;

	if(g_debug) {
		printf("debug: error limit: %.1lf\n", limit);
	}

	// find neighborhoods where the error is smaller than the limit
	low_to_high_init();
	for(i = 0; i < e_count; i++) {
		l_count = low_to_high(a[i], limit);

		// see if p/m indicates a pure tone
		pm = 0;
		if(l_count >= MIN_FB_LEN) {
			y_offset = i - l_count;
			y_len = (l_count < m_fcch_burst_len)? l_count : m_fcch_burst_len;
			y = s + y_offset;
			loff = freq_detect(y, y_len, &pm);
			if(g_debug)
				printf("debug: %.0f\t%f\t%f\n", (double)l_count / sps, pm, loff);
			if(pm > MIN_PM)
				break;
		}
	}
	// empty buffers for next call
	m_e_cb->flush();
	m_x_cb->flush();
	m_y_cb->flush();

	if(pm <= MIN_PM)
		return 0;

	if(offset)
		*offset = loff;

	if(g_debug) {
		printf("debug: fcch_detector finished -----------------------------\n");
	}

	return 1;
}


unsigned int fcch_detector::update(const complex *s, const unsigned int s_len) {

	return m_x_cb->write(s, s_len);
}


unsigned int fcch_detector::get_delay() {

	return m_w_len - 1 + m_D;
}


unsigned int fcch_detector::filter_len() {

	return m_w_len;
}


static float vectornorm2(const complex *v, const unsigned int len) {

	unsigned int i;
	float e = 0.0;

	for(i = 0; i < len; i++)
		e += norm(v[i]);

	return e;
}


/*
 * First y value comes out at sample x[n + m_D] = x[w_len - 1 + m_D].
 *
 * 	y[0] = X(x[0], ..., x[w_len - 1 + m_D])
 *
 * So y and e are delayed by w_len - 1 + m_D.
 */
int fcch_detector::next_norm_error(float *error) {

	unsigned int i, n, max;
	float E;
	complex *x, y, e;

	// n is "current" sample
	n = m_w_len - 1;

	// ensure there are enough samples in the buffer
	x = (complex *)m_x_cb->peek(&max);
	if(n + m_D >= max)
		return n + m_D - max + 1;

	// update G
	E = vectornorm2(x, m_w_len);
	if(m_G >= 2.0 / E)
		m_G = 1.0 / E;

	// calculate filtered value
	y = 0.0;
	for(i = 0; i < m_w_len; i++)
		y += std::conj(m_w[i]) * x[n - i];
	// m_y_cb->write(&y, 1);
	m_y_cb->write(x + n + m_D, 1); // XXX save filtered value?

	// calculate error from desired signal
	e = x[n + m_D] - y;

	// update filters with opposite gradient
	for(i = 0; i < m_w_len; i++)
		m_w[i] += m_G * std::conj(e) * x[n - i];

	// update error average power
	E /= m_w_len;
	m_e = (1.0 - m_p) * m_e + m_p * norm(e);

	// return error ratio
	if(error)
		*error = m_e / E;

	// remove the processed sample from the buffer
	m_x_cb->purge(1);

	return 0;
}


complex *fcch_detector::dump_x(unsigned int *x_len) {

	return (complex *)m_x_cb->peek(x_len);
}


complex *fcch_detector::dump_y(unsigned int *y_len) {

	return (complex *)m_y_cb->peek(y_len);
}


unsigned int fcch_detector::y_buf_len() {

	return m_y_cb->buf_len();
}


unsigned int fcch_detector::x_buf_len() {

	return m_x_cb->buf_len();
}


unsigned int fcch_detector::x_purge(unsigned int len) {

	return m_x_cb->purge(len);
}
