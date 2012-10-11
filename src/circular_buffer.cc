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
#endif /* HAVE_CONFIG_H */

#include <stdio.h>
#include <stdlib.h>
#ifndef _WIN32
#include <unistd.h>
#include <sys/ipc.h>
#endif
#include <string.h>
#include <pthread.h>
#include <stdexcept>
#include <sys/types.h>
#include <sys/stat.h>
#ifndef D_HOST_OSX
#ifndef _WIN32
#include <sys/shm.h>
#endif
#else
#include <sys/mman.h>
#include <fcntl.h>
#endif /* !D_HOST_OSX */

#include "circular_buffer.h"
//#include <cstdio>

#ifndef D_HOST_OSX
#ifndef _WIN32
circular_buffer::circular_buffer(const unsigned int buf_len,
   const unsigned int item_size, const unsigned int overwrite) {

	int shm_id_temp, shm_id_guard, shm_id_buf;
	void *base;

	if(!buf_len)
		throw std::runtime_error("circular_buffer: buffer len is 0");

	if(!item_size)
		throw std::runtime_error("circular_buffer: item size is 0");

	// calculate buffer size
	m_item_size = item_size;
	m_buf_size = item_size * buf_len;

	m_pagesize = getpagesize();
	if(m_buf_size % m_pagesize)
		m_buf_size = (m_buf_size + m_pagesize) & ~(m_pagesize - 1);
	m_buf_len = m_buf_size / item_size;
	
	// create an address-range that can contain everything
	if((shm_id_temp = shmget(IPC_PRIVATE, 2 * m_pagesize + 2 * m_buf_size,
	   IPC_CREAT | S_IRUSR | S_IWUSR)) == -1) {
		perror("shmget");
		throw std::runtime_error("circular_buffer: shmget");
	}

	// create a read-only guard page
	if((shm_id_guard = shmget(IPC_PRIVATE, m_pagesize,
	   IPC_CREAT | S_IRUSR)) == -1) {
		shmctl(shm_id_temp, IPC_RMID, 0);
		perror("shmget");
		throw std::runtime_error("circular_buffer: shmget");
	}

	// create the data buffer
	if((shm_id_buf = shmget(IPC_PRIVATE, m_buf_size, IPC_CREAT | S_IRUSR |
	   S_IWUSR)) == -1) {
		perror("shmget");
		shmctl(shm_id_temp, IPC_RMID, 0);
		shmctl(shm_id_guard, IPC_RMID, 0);
		throw std::runtime_error("circular_buffer: shmget");
	}

	// attach temporary memory to get an address-range
	if((base = shmat(shm_id_temp, 0, 0)) == (void *)(-1)) {
		perror("shmat");
		shmctl(shm_id_temp, IPC_RMID, 0);
		shmctl(shm_id_guard, IPC_RMID, 0);
		shmctl(shm_id_buf, IPC_RMID, 0);
		throw std::runtime_error("circular_buffer: shmat");
	}

	// remove the temporary memory id
	shmctl(shm_id_temp, IPC_RMID, 0);

	// detach and free the temporary memory
	shmdt(base);

	// race condition here
	
	// map first copy of guard page with previous address
	if(shmat(shm_id_guard, base, SHM_RDONLY) == (void *)(-1)) {
		perror("shmat");
		shmctl(shm_id_guard, IPC_RMID, 0);
		shmctl(shm_id_buf, IPC_RMID, 0);
		throw std::runtime_error("circular_buffer: shmat");
	}

	// map first copy of the buffer
	if(shmat(shm_id_buf, (char *)base + m_pagesize, 0) == (void *)(-1)) {
		perror("shmat");
		shmctl(shm_id_guard, IPC_RMID, 0);
		shmctl(shm_id_buf, IPC_RMID, 0);
		shmdt(base);
		throw std::runtime_error("circular_buffer: shmat");
	}

	// map second copy of the buffer
	if(shmat(shm_id_buf, (char *)base + m_pagesize + m_buf_size, 0) ==
	   (void *)(-1)) {
		perror("shmat");
		shmctl(shm_id_guard, IPC_RMID, 0);
		shmctl(shm_id_buf, IPC_RMID, 0);
		shmdt((char *)base + m_pagesize);
		shmdt(base);
		throw std::runtime_error("circular_buffer: shmat");
	}

	// map second copy of guard page
	if(shmat(shm_id_guard, (char *)base + m_pagesize + 2 * m_buf_size,
	   SHM_RDONLY) == (void *)(-1)) {
		perror("shmat");
		shmctl(shm_id_guard, IPC_RMID, 0);
		shmctl(shm_id_buf, IPC_RMID, 0);
		shmdt((char *)base + m_pagesize + m_buf_size);
		shmdt((char *)base + m_pagesize);
		shmdt((char *)base);
		throw std::runtime_error("circular_buffer: shmat");
	}

	// remove the id for the guard and buffer, we don't need them anymore
	shmctl(shm_id_guard, IPC_RMID, 0);
	shmctl(shm_id_buf, IPC_RMID, 0);

	// save the base address for detach later
	m_base = base;

	// save a pointer to the data
	m_buf = (char *)base + m_pagesize;

	m_r = m_w = 0;
	m_read = m_written = 0;

	m_item_size = item_size;

	m_overwrite = overwrite;

	pthread_mutex_init(&m_mutex, 0);
}

circular_buffer::~circular_buffer() {

	shmdt((char *)m_base + m_pagesize + 2 * m_buf_size);
	shmdt((char *)m_base + m_pagesize + m_buf_size);
	shmdt((char *)m_base + m_pagesize);
	shmdt((char *)m_base);
}

#else
circular_buffer::circular_buffer(const unsigned int buf_len,
   const unsigned int item_size, const unsigned int overwrite) {

	if(!buf_len)
		throw std::runtime_error("circular_buffer: buffer len is 0");

	if(!item_size)
		throw std::runtime_error("circular_buffer: item size is 0");

	// calculate buffer size
	m_item_size = item_size;
	m_buf_size = item_size * buf_len;
	m_buf_len = m_buf_size / item_size;


  d_handle = CreateFileMapping(INVALID_HANDLE_VALUE,    // use paging file
			       NULL,                    // default security
			       PAGE_READWRITE,          // read/write access
			       0,                       // max. object size
			       m_buf_size,                    // buffer size
			       NULL);       // name of mapping object


  if (d_handle == NULL || d_handle == INVALID_HANDLE_VALUE){
    throw std::runtime_error ("gr_vmcircbuf_mmap_createfilemapping");
  }

  // Allocate virtual memory of the needed size, then free it so we can use it
  LPVOID first_tmp;
  first_tmp = VirtualAlloc( NULL, 2*m_buf_size, MEM_RESERVE, PAGE_NOACCESS );
  if (first_tmp == NULL){
    CloseHandle(d_handle);         // cleanup
    throw std::runtime_error ("gr_vmcircbuf_mmap_createfilemapping");
  }

  if (VirtualFree(first_tmp, 0, MEM_RELEASE) == 0){
    CloseHandle(d_handle);         // cleanup
    throw std::runtime_error ("gr_vmcircbuf_mmap_createfilemapping");
  }

  d_first_copy =  MapViewOfFileEx((HANDLE)d_handle,   // handle to map object
				   FILE_MAP_WRITE,    // read/write permission
				   0,
				   0,
				   m_buf_size,
				   first_tmp);
  if (d_first_copy != first_tmp){
    CloseHandle(d_handle);         // cleanup
    throw std::runtime_error ("gr_vmcircbuf_mmap_createfilemapping");
  }

  d_second_copy =  MapViewOfFileEx((HANDLE)d_handle,   // handle to map object
				   FILE_MAP_WRITE,     // read/write permission
				   0,
				   0,
				   m_buf_size,
				   (char *)first_tmp + m_buf_size);//(LPVOID) ((char *)d_first_copy + size));

  if (d_second_copy != (char *)first_tmp + m_buf_size){
    UnmapViewOfFile(d_first_copy);
    CloseHandle(d_handle);                      // cleanup
    throw std::runtime_error ("gr_vmcircbuf_mmap_createfilemapping");
  }

	// save a pointer to the data
	m_buf = d_first_copy;// (char *)base + m_pagesize;

	m_r = m_w = 0;
	m_read = m_written = 0;

	m_item_size = item_size;

	m_overwrite = overwrite;

	pthread_mutex_init(&m_mutex, 0);

  }

circular_buffer::~circular_buffer() {
	UnmapViewOfFile(d_first_copy);
	UnmapViewOfFile(d_second_copy);
	CloseHandle(d_handle);
}
#endif
#else /* !D_HOST_OSX */


/*
 * OSX doesn't support System V shared memory.  Using GNU Radio as an example,
 * we'll implement this for OSX using Posix shared memory.  I'm not exactly
 * sure why GNU Radio prefers the System V usage, but I seem to recall there
 * was a reason.
 */
circular_buffer::circular_buffer(const unsigned int buf_len,
   const unsigned int item_size, const unsigned int overwrite) {

	int shm_fd;
	char shm_name[255]; // XXX should be NAME_MAX
	void *base;

	if(!buf_len)
		throw std::runtime_error("circular_buffer: buffer len is 0");

	if(!item_size)
		throw std::runtime_error("circular_buffer: item size is 0");

	// calculate buffer size
	m_item_size = item_size;
	m_buf_size = item_size * buf_len;

	m_pagesize = getpagesize();
	if(m_buf_size % m_pagesize)
		m_buf_size = (m_buf_size + m_pagesize) & ~(m_pagesize - 1);
	m_buf_len = m_buf_size / item_size;

	// create unique-ish name
	snprintf(shm_name, sizeof(shm_name), "/kalibrate-%d", getpid());

	// create a Posix shared memory object
	if((shm_fd = shm_open(shm_name, O_RDWR | O_CREAT | O_EXCL, S_IRUSR | S_IWUSR)) == -1) {
		perror("shm_open");
		throw std::runtime_error("circular_buffer: shm_open");
	}

	// create enough space to hold everything
	if(ftruncate(shm_fd, 2 * m_pagesize + 2 * m_buf_size) == -1) {
		perror("ftruncate");
		close(shm_fd);
		shm_unlink(shm_name);
		throw std::runtime_error("circular_buffer: ftruncate");
	}

	// get an address for the buffer
	if((base = mmap(0, 2 * m_pagesize + 2 * m_buf_size, PROT_NONE, MAP_SHARED, shm_fd, 0)) == MAP_FAILED) {
		perror("mmap");
		close(shm_fd);
		shm_unlink(shm_name);
		throw std::runtime_error("circular_buffer: mmap (base)");
	}

	// unmap everything but the first guard page
	if(munmap((char *)base + m_pagesize, m_pagesize + 2 * m_buf_size) == -1) {
		perror("munmap");
		close(shm_fd);
		shm_unlink(shm_name);
		throw std::runtime_error("circular_buffer: munmap");
	}

	// race condition

	// map first copy of the buffer
	if(mmap((char *)base + m_pagesize, m_buf_size, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_FIXED, shm_fd, m_pagesize) == MAP_FAILED) {
		perror("mmap");
		munmap(base, 2 * m_pagesize + 2 * m_buf_size);
		close(shm_fd);
		shm_unlink(shm_name);
		throw std::runtime_error("circular_buffer: mmap (buf 1)");
	}

	// map second copy of the buffer
	if(mmap((char *)base + m_pagesize + m_buf_size, m_buf_size, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_FIXED, shm_fd, m_pagesize) == MAP_FAILED) {
		perror("mmap");
		munmap(base, 2 * m_pagesize + 2 * m_buf_size);
		close(shm_fd);
		shm_unlink(shm_name);
		throw std::runtime_error("circular_buffer: mmap (buf 2)");
	}

	// map second copy of the guard page
	if(mmap((char *)base + m_pagesize + 2 * m_buf_size, m_pagesize, PROT_NONE, MAP_SHARED | MAP_FIXED, shm_fd, 0) == MAP_FAILED) {
		perror("mmap");
		munmap(base, 2 * m_pagesize + 2 * m_buf_size);
		close(shm_fd);
		shm_unlink(shm_name);
		throw std::runtime_error("circular_buffer: mmap (guard)");
	}

	// both the file and name are unnecessary now
	close(shm_fd);
	shm_unlink(shm_name);

	// save the base address for unmap later
	m_base = base;

	// save a pointer to the data
	m_buf = (char *)base + m_pagesize;

	m_r = m_w = 0;
	m_read = m_written = 0;

	m_item_size = item_size;

	m_overwrite = overwrite;

	pthread_mutex_init(&m_mutex, 0);
}


circular_buffer::~circular_buffer() {

	munmap(m_base, 2 * m_pagesize + 2 * m_buf_size);
}
#endif /* !D_HOST_OSX */


/*
 * The amount to read can only grow unless someone calls read after this is
 * called.  No real good way to tie the two together.
 */
unsigned int circular_buffer::data_available() {

	unsigned int amt;

	pthread_mutex_lock(&m_mutex);
	amt = m_written - m_read;	// item_size
	pthread_mutex_unlock(&m_mutex);

	return amt;
}


unsigned int circular_buffer::space_available() {

	unsigned int amt;

	pthread_mutex_lock(&m_mutex);
	amt = m_buf_len - (m_written - m_read);
	pthread_mutex_unlock(&m_mutex);

	return amt;
}


#ifndef MIN
#define MIN(a, b) ((a)<(b)?(a):(b))
#endif /* !MIN */

/*
 * m_buf_size is in terms of bytes
 * m_r and m_w are offsets in bytes
 * m_buf_len is in terms of m_item_size
 * buf_len is in terms of m_item_size
 * len, m_written, and m_read are all in terms of m_item_size
 */
unsigned int circular_buffer::read(void *buf, const unsigned int buf_len) {

	unsigned int len;

	pthread_mutex_lock(&m_mutex);
	len = MIN(buf_len, m_written - m_read);
	memcpy(buf, (char *)m_buf + m_r, len * m_item_size);
	m_read += len;
	if(m_read == m_written) {
		m_r = m_w = 0;
		m_read = m_written = 0;
	} else
		m_r = (m_r + len * m_item_size) % m_buf_size;
	pthread_mutex_unlock(&m_mutex);

	return len;
}


/*
 * warning:
 *
 *	Don't use read() while you are peek()'ing.  write() should be
 *	okay unless you have an overwrite buffer.
 */
void *circular_buffer::peek(unsigned int *buf_len) {

	unsigned int len;
	void *p;

	pthread_mutex_lock(&m_mutex);
	len = m_written - m_read;
	p = (char *)m_buf + m_r;
	pthread_mutex_unlock(&m_mutex);

	if(buf_len)
		*buf_len = len;

	return p;
}


void *circular_buffer::poke(unsigned int *buf_len) {

	unsigned int len;
	void *p;

	pthread_mutex_lock(&m_mutex);
	len = m_buf_len - (m_written - m_read);
	p = (char *)m_buf + m_w;
	pthread_mutex_unlock(&m_mutex);

	if(buf_len)
		*buf_len = len;

	return p;
}


unsigned int circular_buffer::purge(const unsigned int buf_len) {

	unsigned int len;

	pthread_mutex_lock(&m_mutex);
	len = MIN(buf_len, m_written - m_read);
	m_read += len;
	if(m_read == m_written) {
		m_r = m_w = 0;
		m_read = m_written = 0;
	} else
		m_r = (m_r + len * m_item_size) % m_buf_size;
	pthread_mutex_unlock(&m_mutex);

	return len;
}


unsigned int circular_buffer::write(const void *buf,
   const unsigned int buf_len) {

	unsigned int len, buf_off = 0;

	pthread_mutex_lock(&m_mutex);
	if(m_overwrite) {
		if(buf_len > m_buf_len) {
			buf_off = buf_len - m_buf_len;
			len = m_buf_len;
		} else
			len = buf_len;
	} else
		len = MIN(buf_len, m_buf_len - (m_written - m_read));
	memcpy((char *)m_buf + m_w, (char *)buf + buf_off * m_item_size,
	   len * m_item_size);
	m_written += len;
	m_w = (m_w + len * m_item_size) % m_buf_size;
	if(m_written > m_buf_len + m_read) {
		m_read = m_written - m_buf_len;
		m_r = m_w;
	}
	pthread_mutex_unlock(&m_mutex);

	return len;
}


void circular_buffer::wrote(unsigned int len) {

	pthread_mutex_lock(&m_mutex);
	m_written += len;
	m_w = (m_w + len * m_item_size) % m_buf_size;
	pthread_mutex_unlock(&m_mutex);
}


void circular_buffer::flush() {

	pthread_mutex_lock(&m_mutex);
	m_read = m_written = 0;
	m_r = m_w = 0;
	pthread_mutex_unlock(&m_mutex);
}


void circular_buffer::flush_nolock() {

	m_read = m_written = 0;
	m_r = m_w = 0;
}


void circular_buffer::lock() {

	pthread_mutex_lock(&m_mutex);
}


void circular_buffer::unlock() {

	pthread_mutex_unlock(&m_mutex);
}


unsigned int circular_buffer::buf_len() {

	return m_buf_len;
}
