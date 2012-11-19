#ifndef UTILS_P_H
#define UTILS_P_H

#include <QtCore/QtGlobal>
#include <qplatformdefs.h>
#include <errno.h>

Q_DECL_HIDDEN void event_log_callback(int severity, const char* msg);
Q_DECL_HIDDEN int make_tco(int* readfd, int* writefd);

Q_DECL_HIDDEN inline qint64 safe_read(int fd, void* data, qint64 maxlen)
{
	qint64 ret;

	do {
		ret = QT_READ(fd, data, maxlen);
	} while (ret == -1 && errno == EINTR);

	return ret;
}

Q_DECL_HIDDEN inline qint64 safe_write(int fd, const void* data, qint64 len)
{
	qint64 ret;

	do {
		ret = QT_WRITE(fd, data, len);
	} while (ret == -1 && errno == EINTR);

	return ret;
}

#endif // UTILS_P_H
