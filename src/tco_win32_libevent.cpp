#include <QtCore/QtGlobal>

#ifdef Q_OS_WIN
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#endif

#include <qplatformdefs.h>
#include "tco.h"

#if defined(SJ_LIBEVENT_MAJOR) && SJ_LIBEVENT_MAJOR == 1
#	include "libevent2-emul.h"
#else
#	include <event2/event.h>
#endif

#if QT_VERSION >= 0x040400
#	include <QtCore/QAtomicInt>
#endif

namespace {

static bool WSAInitialized(void)
{
	SOCKET s = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (s == INVALID_SOCKET && WSAGetLastError() == WSANOTINITIALISED) {
		return false;
	}

	::closesocket(s);
	return true;
}

class Q_DECL_HIDDEN WSAInitializer {
public:
	WSAInitializer(void)
		: m_success(false)
	{
		WORD wVersionRequested = MAKEWORD(2, 2);
		WSADATA wsaData;
		int err = WSAStartup(wVersionRequested, &wsaData);
		if (Q_LIKELY(!err)) {
			this->m_success = true;
		}
	}

	~WSAInitializer(void)
	{
		if (Q_LIKELY(this->m_success)) {
			WSACleanup();
		}
	}
private:
	bool m_success;
};

Q_GLOBAL_STATIC(WSAInitializer, wsa_initializer)

}

class Q_DECL_HIDDEN ThreadCommunicationObjectPrivate {
public:
	ThreadCommunicationObjectPrivate(void)
		: fd(-1), isvalid(false)
#if QT_VERSION >= 0x040400
		  , wakeups()
#endif
	{
		static bool wsa_inited = false;
		if (!wsa_inited) {
			wsa_inited = WSAInitialized();
			if (!wsa_inited) {
				wsa_initializer();
			}

			wsa_inited = WSAInitialized();
		}

		this->fd[0] = INVALID_SOCKET;
		this->fd[1] = INVALID_SOCKET;
		if (0 != evutil_socketpair(AF_INET, SOCK_STREAM, IPPROTO_TCP, this->fd)) {
			qFatal("%s: evutil_socketpair() failed: %d", Q_FUNC_INFO, WSAGetLastError());
		}

		evutil_make_socket_nonblocking(this->fd[0]);
		evutil_make_socket_nonblocking(this->fd[1]);

		this->isvalid = true;
	}

	~ThreadCommunicationObjectPrivate(void)
	{
		if (this->fd[0] != INVALID_SOCKET) {
			::closesocket(this->fd[0]);
			::closesocket(this->fd[1]);
		}
	}

	bool wakeUp(void)
	{
		Q_ASSERT(this->isvalid);

#if QT_VERSION >= 0x040400
		if (this->wakeups.testAndSetAcquire(0, 1))
#endif
		{
			const char c = '.';
			int res      = ::send(this->fd[1], &c, 1, 0);

			if (Q_UNLIKELY(1 != res)) {
				qWarning("%s: send() failed: %d", Q_FUNC_INFO, WSAGetLastError());
				return false;
			}
		}

		return true;
	}

	bool awaken(void)
	{
		Q_ASSERT(this->isvalid);

		char buf[16];
		int res;
		do {
			res = ::recv(this->fd[0], buf, sizeof(buf), 0);
		} while (res == sizeof(buf));

		if (Q_UNLIKELY(SOCKET_ERROR == res)) {
			qErrnoWarning("%s: recv() failed: %d", Q_FUNC_INFO, WSAGetLastError());
		}

#if QT_VERSION >= 0x040400
		if (Q_UNLIKELY(!this->wakeups.testAndSetRelease(1, 0))) {
			qCritical("%s: internal error, testAndSetRelease(1, 0) failed!", Q_FUNC_INFO);
			res = -1;
		}
#endif

		return res != SOCKET_ERROR;
	}

	bool valid(void) const { return this->isvalid; }
	qintptr readfd(void) const { return this->fd[0]; }

private:
	SOCKET fd[2];
	bool isvalid;
#if QT_VERSION >= 0x040400
	QAtomicInt wakeups;
#endif
};

#include "tco_impl.h"
