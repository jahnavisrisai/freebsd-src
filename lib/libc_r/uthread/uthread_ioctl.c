/*
 * Copyright (c) 1995 John Birrell <jb@cimlogic.com.au>.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by John Birrell.
 * 4. Neither the name of the author nor the names of any co-contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY JOHN BIRRELL AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 */
#include <stdarg.h>
#include <sys/ioctl.h>
#ifdef _THREAD_SAFE
#include <sys/fcntl.h>	/* O_NONBLOCK*/
#include <pthread.h>
#include "pthread_private.h"

int
ioctl(int fd, unsigned long request,...)
{
	int             ret;
	int		*op;
	int             status;
	va_list         ap;

	/* Block signals: */
	_thread_kern_sig_block(&status);

	/* Lock the file descriptor: */
	if ((ret = _thread_fd_lock(fd, FD_RDWR, NULL, __FILE__, __LINE__)) == 0) {
		/* Initialise the variable argument list: */
	va_start(ap, request);

		switch( request) {
		case FIONBIO:
			/*
			 * descriptors must be non-blocking; we are only
			 * twiddling the flag based on the request
			 */
			op = va_arg(ap, int *);
			_thread_fd_table[fd]->flags &= ~O_NONBLOCK;
			_thread_fd_table[fd]->flags |= ((*op) ? O_NONBLOCK : 0);
			ret = 0;
			break;
		default:
		ret = _thread_sys_ioctl(fd, request, va_arg(ap, char *));
			break;
		}

		/* Free variable arguments: */
	va_end(ap);

		/* Unlock the file descriptor: */
		_thread_fd_unlock(fd, FD_RDWR);
	}
	/* Unblock signals: */
	_thread_kern_sig_unblock(status);

	/* Return the completion status: */
	return (ret);
}
#endif
