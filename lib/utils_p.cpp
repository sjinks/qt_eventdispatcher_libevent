#include <qplatformdefs.h>
#include "eventdispatcher_libevent_p.h"
#include "utils_p.h"
#if defined(HAVE_SYS_EVENTFD_H) || (defined(__GLIBC_PREREQ) && __GLIBC_PREREQ(2, 8))
#	include <sys/eventfd.h>
#	if !defined(HAVE_SYS_EVENTFD_H)
#		define HAVE_SYS_EVENTFD_H
#	endif
#endif

#if defined(EFD_CLOEXEC) && defined(EFD_NONBLOCK)
#	define MY_EFD_CLOEXEC  EFD_CLOEXEC
#	define MY_EFD_NONBLOCK EFD_NONBLOCK
#else
#	define MY_EFD_CLOEXEC  0
#	define MY_EFD_NONBLOCK 0
#endif

#if defined(Q_OS_LINUX) && defined(O_CLOEXEC)
#	define THREADSAFE_CLOEXEC_SUPPORTED 1
namespace libcsupplement {
	inline int pipe2(int[], int) { errno = ENOSYS; return -1; }
}

using namespace libcsupplement;
#else
#	define THREADSAFE_CLOEXEC_SUPPORTED 0
#endif

void event_log_callback(int severity, const char* msg)
{
	switch (severity) {
		case _EVENT_LOG_WARN: qWarning("%s", msg); break;
		case _EVENT_LOG_ERR:  qCritical("%s", msg); break;
		default:              qDebug("%s", msg); break;
	}
}

int make_tco(int* readfd, int* writefd)
{
	Q_ASSERT(readfd != 0 && writefd != 0);

#ifdef HAVE_SYS_EVENTFD_H
	int flags = MY_EFD_CLOEXEC | MY_EFD_NONBLOCK;
	int res   = eventfd(0, flags);

	if (-1 == res) {
		if (EINVAL == errno && flags) {
			res = eventfd(0, 0);
			if (res != -1) {
				evutil_make_socket_closeonexec(res);
				evutil_make_socket_nonblocking(res);
			}
		}
	}

	*readfd  = res;
	*writefd = res;
	return res;
#else
	int pipefd[2];
	int res;

	*readfd  = -1;
	*writefd = -1;
#if THREADSAFE_CLOEXEC_SUPPORTED
	res = ::pipe2(pipefd, O_CLOEXEC | O_NONBLOCK);
	if (res != -1 || errno != ENOSYS) {
		if (res != -1) {
			*readfd  = pipefd[0];
			*writefd = pipefd[1];
		}

		return res;
	}
#endif
	res = ::pipe(pipefd);

	if (-1 == res) {
		return -1;
	}

	evutil_make_socket_closeonexec(pipefd[0]);
	evutil_make_socket_nonblocking(pipefd[0]);

	evutil_make_socket_closeonexec(pipefd[1]);
	evutil_make_socket_nonblocking(pipefd[1]);

	*readfd  = pipefd[0];
	*writefd = pipefd[1];
	return res;
#endif // HAVE_SYS_EVENTFD_H
}

#undef MY_EFD_CLOEXEC
#undef MY_EFD_NONBLOCK
#undef THREADSAFE_CLOEXEC_SUPPORTED
