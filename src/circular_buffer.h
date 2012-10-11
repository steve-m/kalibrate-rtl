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
 * circular_buffer
 *
 * This class is based heavily on the GNU Radio circular buffer.  While this
 * class was written from scratch and contains ideas not present in the GNU
 * Radio implementation, the GNU Radio circular buffers were used as a
 * reference while developing this class.
 *
 * This is more a warning that the above BSD-style license may not be the only
 * copyright that applies.
 */

#pragma once

/*
 * XXX If read doesn't catch up with write before 2**64 bytes are written, this
 * will break.
 */

#include <pthread.h>
#ifdef _WIN32
#include <Windows.h>
#endif

class circular_buffer {
public:
	circular_buffer(const unsigned int buf_len, const unsigned int item_size = 1, const unsigned int overwrite = 0);
	~circular_buffer();

	unsigned int read(void *buf, const unsigned int buf_len);
	void *peek(unsigned int *buf_len);
	unsigned int purge(const unsigned int buf_len);
	void *poke(unsigned int *buf_len);
	void wrote(unsigned int len);
	unsigned int write(const void *buf, const unsigned int buf_len);
	unsigned int data_available();
	unsigned int space_available();
	void flush();
	void flush_nolock();
	void lock();
	void unlock();
	unsigned int buf_len();

private:
#ifdef _WIN32
	HANDLE d_handle;
	LPVOID d_first_copy;
	LPVOID d_second_copy;
#endif
	void *m_buf;
	unsigned int m_buf_len, m_buf_size, m_r, m_w, m_item_size;
	unsigned long long m_read, m_written;

	unsigned int m_overwrite;

	void *m_base;
	unsigned int m_pagesize;

	pthread_mutex_t	m_mutex;
};
