#include <QtCore/QCoreApplication>
#include <QtCore/QEvent>
#include <QtCore/QHash>
#include <QtCore/QMultiHash>
#include <QtCore/QSocketNotifier>
#include <QtCore/QThread>
#include <event2/event.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <sys/time.h>
#if defined(HAVE_SYS_EVENTFD_H) || (defined(__GLIBC_PREREQ) && __GLIBC_PREREQ(2, 8))
#	include <sys/eventfd.h>
#endif
#include "eventdispatcher_libevent.h"

#if QT_VERSION < 0x050000
namespace Qt { // Sorry
	enum TimerType {
		PreciseTimer,
		CoarseTimer,
		VeryCoarseTimer
	};
}
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

static int make_tco(int* readfd, int* writefd)
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

class EventDispatcherLibEventPrivate {
public:
	EventDispatcherLibEventPrivate(EventDispatcherLibEvent* const q);
	~EventDispatcherLibEventPrivate(void);
	bool processEvents(QEventLoop::ProcessEventsFlags flags);
	void registerSocketNotifier(QSocketNotifier* notifier);
	void unregisterSocketNotifier(QSocketNotifier* notifier);
	void registerTimer(int timerId, int interval, Qt::TimerType type, QObject* object);
	bool unregisterTimer(int timerId);
	bool unregisterTimers(QObject* object);
	QList<QAbstractEventDispatcher::TimerInfo> registeredTimers(QObject* object) const;
	int remainingTime(int timerId) const;

	struct SocketNotifierInfo {
		QSocketNotifier* sn;
		struct event* ev;
	};

	struct TimerInfo {
		EventDispatcherLibEventPrivate* self;
		QObject* object;
		struct event* ev;
		struct timeval when;
		int timerId;
		int interval;
		Qt::TimerType type;
	};

	typedef QMultiHash<evutil_socket_t, SocketNotifierInfo> SocketNotifierHash;
	typedef QHash<int, TimerInfo*> TimerHash;

private:
	Q_DISABLE_COPY(EventDispatcherLibEventPrivate)
	Q_DECLARE_PUBLIC(EventDispatcherLibEvent)
	EventDispatcherLibEvent* const q_ptr;

	bool m_interrupt;
	int m_pipe_read;
	int m_pipe_write;
	struct event_base* m_base;
	struct event* m_wakeup;
	SocketNotifierHash m_notifiers;
	TimerHash m_timers;
	bool m_seen_event;

	static void socket_notifier_callback(evutil_socket_t fd, short int events, void* arg);
	static void timer_callback(evutil_socket_t fd, short int events, void* arg);
	static void wake_up_handler(evutil_socket_t fd, short int events, void* arg);

	void disableSocketNotifiers(bool disable);
};

static void calculateCoarseTimerTimeout(EventDispatcherLibEventPrivate::TimerInfo* info, const struct timeval& now, struct timeval& when)
{
	Q_ASSERT(info->interval > 20);
	// The coarse timer works like this:
	//  - interval under 40 ms: round to even
	//  - between 40 and 99 ms: round to multiple of 4
	//  - otherwise: try to wake up at a multiple of 25 ms, with a maximum error of 5%
	//
	// We try to wake up at the following second-fraction, in order of preference:
	//    0 ms
	//  500 ms
	//  250 ms or 750 ms
	//  200, 400, 600, 800 ms
	//  other multiples of 100
	//  other multiples of 50
	//  other multiples of 25
	//
	// The objective is to make most timers wake up at the same time, thereby reducing CPU wakeups.

	uint interval     = uint(info->interval);
	uint msec         = info->when.tv_usec / 1000;
	uint max_rounding = interval / 20; // 5%
	when              = info->when;

	if (interval < 100 && (interval % 25) != 0) {
		if (interval < 50) {
			uint round_up = ((msec % 50) >= 25) ? 1 : 0;
			msec = ((msec >> 1) | round_up) << 1;
		}
		else {
			uint round_up = ((msec % 100) >= 50) ? 1 : 0;
			msec = ((msec >> 2) | round_up) << 2;
		}
	}
	else {
		uint min = uint(qMax<int>(0, msec - max_rounding));
		uint max = qMin(1000u, msec + max_rounding);

		bool done = false;

		// take any round-to-the-second timeout
		if (min == 0) {
			msec = 0;
			done = true;
		}
		else if (max == 1000) {
			msec = 1000;
			done = true;
		}

		if (!done) {
			uint boundary;

			// if the interval is a multiple of 500 ms and > 5000 ms, always round towards a round-to-the-second
			// if the interval is a multiple of 500 ms, round towards the nearest multiple of 500 ms
			if ((interval % 500) == 0) {
				if (interval >= 5000) {
					msec = msec >= 500 ? max : min;
					done = true;
				}
				else {
					boundary = 500;
				}
			}
			else if ((interval % 50) == 0) {
				// same for multiples of 250, 200, 100, 50
				uint tmp = interval / 50;
				if ((tmp % 4) == 0) {
					boundary = 200;
				}
				else if ((tmp % 2) == 0) {
					boundary = 100;
				}
				else if ((tmp % 5) == 0) {
					boundary = 250;
				}
				else {
					boundary = 50;
				}
			}
			else {
				boundary = 25;
			}

			if (!done) {
				uint base   = uint(msec / boundary) * boundary;
				uint middle = base + boundary / 2;
				msec        = (msec < middle) ? qMax(base, min) : qMin(base + boundary, max);
			}
		}
	}

	if (msec == 1000) {
		++when.tv_sec;
		when.tv_usec = 0;
	}
	else {
		when.tv_usec = msec * 1000;
	}

	if (evutil_timercmp(&when, &now, <)) {
		when.tv_sec  += interval / 1000;
		when.tv_usec += (interval % 1000) * 1000;
		if (when.tv_usec > 999999) {
			++when.tv_sec;
			when.tv_usec -= 1000000;
		}
	}

	Q_ASSERT(evutil_timercmp(&now, &when, <=));
}

static void calculateNextTimeout(EventDispatcherLibEventPrivate::TimerInfo* info, const struct timeval& now, struct timeval& delta)
{
	struct timeval tv_interval;
	struct timeval when;
	tv_interval.tv_sec  = info->interval / 1000;
	tv_interval.tv_usec = (info->interval % 1000) * 1000;

	if (Qt::VeryCoarseTimer == info->type) {
		if (info->when.tv_usec >= 500000) {
			++info->when.tv_sec;
		}

		info->when.tv_usec = 0;
		info->when.tv_sec += info->interval / 1000;
		if (info->when.tv_sec <= now.tv_sec) {
			info->when.tv_sec = now.tv_sec + info->interval / 1000;
		}

		when = info->when;
	}
	else if (Qt::PreciseTimer == info->type) {
		if (info->interval) {
			do {
				evutil_timeradd(&info->when, &tv_interval, &info->when);
			} while (evutil_timercmp(&info->when, &now, <));

			when = info->when;
		}
		else {
			when = now;
		}
	}
	else {
		evutil_timeradd(&info->when, &tv_interval, &info->when);
		if (evutil_timercmp(&info->when, &now, <)) {
			evutil_timeradd(&now, &tv_interval, &info->when);
		}

		calculateCoarseTimerTimeout(info, now, when);
	}

	evutil_timersub(&when, &now, &delta);
}

EventDispatcherLibEventPrivate::EventDispatcherLibEventPrivate(EventDispatcherLibEvent* const q)
	: q_ptr(q), m_interrupt(false), m_pipe_read(), m_pipe_write(), m_base(0), m_wakeup(0),
	  m_notifiers(), m_timers(), m_seen_event(false)
{
	this->m_base = event_base_new();

	if (-1 == make_tco(&this->m_pipe_read, &this->m_pipe_write)) {
		qFatal("%s: Fatal: Unable to create thread communication object", Q_FUNC_INFO);
	}
	else {
		this->m_wakeup = event_new(this->m_base, this->m_pipe_read, EV_READ | EV_PERSIST, EventDispatcherLibEventPrivate::wake_up_handler, 0);
		event_add(this->m_wakeup, 0);
	}
}

EventDispatcherLibEventPrivate::~EventDispatcherLibEventPrivate(void)
{
#ifdef HAVE_SYS_EVENTFD_H
	Q_ASSERT(this->m_pipe_read == this->m_pipe_write);
#else
	if (-1 != this->m_pipe_write) {
		evutil_closesocket(this->m_pipe_write);
	}
#endif

	if (-1 != this->m_pipe_read) {
		evutil_closesocket(this->m_pipe_read);
	}

	if (this->m_wakeup) {
		event_del(this->m_wakeup);
		event_free(this->m_wakeup);
		this->m_wakeup = 0;
	}

	if (this->m_base) {
		event_base_free(this->m_base);
		this->m_base = 0;
	}
}

bool EventDispatcherLibEventPrivate::processEvents(QEventLoop::ProcessEventsFlags flags)
{
	Q_Q(EventDispatcherLibEvent);

	bool exclude_notifiers = (flags & QEventLoop::ExcludeSocketNotifiers);

	if (exclude_notifiers) {
		this->disableSocketNotifiers(true);
	}

	int evflags  = EVLOOP_ONCE;
	bool canWait = (!this->m_interrupt && (flags & QEventLoop::WaitForMoreEvents) && !q->hasPendingEvents());
	if (canWait) {
		Q_EMIT q->aboutToBlock();
	}
	else {
		Q_EMIT q->awake();
		evflags |= EVLOOP_NONBLOCK;
	}

	this->m_seen_event = false;
	if (q->hasPendingEvents()) {
		QCoreApplication::sendPostedEvents();
		this->m_seen_event = true;
	}

	do {
		event_base_loop(this->m_base, evflags);
		QCoreApplication::sendPostedEvents();
	} while (!this->m_interrupt && canWait && !this->m_seen_event);

	if (canWait) {
		Q_EMIT q->awake();
	}

	if (exclude_notifiers) {
		this->disableSocketNotifiers(false);
	}

	this->m_interrupt = false;
	return this->m_seen_event;
}

void EventDispatcherLibEventPrivate::registerSocketNotifier(QSocketNotifier* notifier)
{
	evutil_socket_t sockfd = notifier->socket();
	short int what;
	switch (notifier->type()) {
		case QSocketNotifier::Read:  what = EV_READ; break;
		case QSocketNotifier::Write: what = EV_WRITE; break;
		case QSocketNotifier::Exception: /// FIXME
			return;

		default:
			Q_ASSERT(false);
			return;
	}

	what |= EV_PERSIST;
	struct event* ev = event_new(this->m_base, sockfd, what, EventDispatcherLibEventPrivate::socket_notifier_callback, this);
	event_add(ev, 0);

	SocketNotifierInfo data;
	data.sn = notifier;
	data.ev = ev;
	this->m_notifiers.insertMulti(sockfd, data);
}

void EventDispatcherLibEventPrivate::unregisterSocketNotifier(QSocketNotifier* notifier)
{
	evutil_socket_t sockfd = notifier->socket();
	SocketNotifierHash::Iterator it = this->m_notifiers.find(sockfd);
	while (it != this->m_notifiers.end() && it.key() == sockfd) {
		SocketNotifierInfo& data = it.value();
		if (data.sn == notifier) {
			event_del(data.ev);
			event_free(data.ev);
			it = this->m_notifiers.erase(it);
		}
		else {
			++it;
		}
	}
}

void EventDispatcherLibEventPrivate::registerTimer(int timerId, int interval, Qt::TimerType type, QObject* object)
{
	struct timeval now;
	evutil_gettimeofday(&now, 0);

	EventDispatcherLibEventPrivate::TimerInfo* info = new EventDispatcherLibEventPrivate::TimerInfo;
	info->self     = this;
	info->ev       = event_new(this->m_base, -1, 0, EventDispatcherLibEventPrivate::timer_callback, info);
	info->timerId  = timerId;
	info->interval = interval;
	info->type     = type;
	info->object   = object;
	info->when     = now; // calculateNextTimeout() will take care of info->when

	if (Qt::CoarseTimer == type) {
		if (interval >= 20000) {
			info->type = Qt::VeryCoarseTimer;
		}
		else if (interval <= 20) {
			info->type = Qt::PreciseTimer;
		}
	}

	struct timeval delta;
	calculateNextTimeout(info, now, delta);

	event_add(info->ev, &delta);
	this->m_timers.insert(timerId, info);
}

bool EventDispatcherLibEventPrivate::unregisterTimer(int timerId)
{
	TimerHash::Iterator it = this->m_timers.find(timerId);
	if (it != this->m_timers.end()) {
		EventDispatcherLibEventPrivate::TimerInfo* info = it.value();
		event_del(info->ev);
		event_free(info->ev);
		delete info;
		this->m_timers.erase(it);
		return true;
	}

	return false;
}

bool EventDispatcherLibEventPrivate::unregisterTimers(QObject* object)
{
	TimerHash::Iterator it = this->m_timers.begin();
	while (it != this->m_timers.end()) {
		EventDispatcherLibEventPrivate::TimerInfo* info = it.value();
		if (object == info->object) {
			event_del(info->ev);
			event_free(info->ev);
			delete info;
			it = this->m_timers.erase(it);
		}
		else {
			++it;
		}
	}

	return true;
}

QList<QAbstractEventDispatcher::TimerInfo> EventDispatcherLibEventPrivate::registeredTimers(QObject* object) const
{
	QList<QAbstractEventDispatcher::TimerInfo> res;

	TimerHash::ConstIterator it = this->m_timers.constBegin();
	while (it != this->m_timers.constEnd()) {
		EventDispatcherLibEventPrivate::TimerInfo* info = it.value();
		if (object == info->object) {
#if QT_VERSION < 0x050000
			QAbstractEventDispatcher::TimerInfo ti(it.key(), info->interval);
#else
			QAbstractEventDispatcher::TimerInfo ti(it.key(), info->interval, info->type);
#endif
			res.append(ti);
		}

		++it;
	}

	return res;
}

int EventDispatcherLibEventPrivate::remainingTime(int timerId) const
{
	TimerHash::ConstIterator it = this->m_timers.find(timerId);
	if (it != this->m_timers.end()) {
		const EventDispatcherLibEventPrivate::TimerInfo* info = it.value();
		struct timeval when;

		int r = event_pending(info->ev, EV_TIMEOUT, &when);
		if (r) {
			struct timeval now;
			event_base_gettimeofday_cached(this->m_base, &now);

			qulonglong tnow  = qulonglong(now.tv_sec)  * 1000000 + now.tv_usec;
			qulonglong twhen = qulonglong(when.tv_sec) * 1000000 + when.tv_usec;

			if (tnow > twhen) {
				return 0;
			}

			return (twhen - tnow) / 1000;
		}
	}

	return -1;
}


void EventDispatcherLibEventPrivate::socket_notifier_callback(int fd, short int events, void* arg)
{
	EventDispatcherLibEventPrivate* disp = reinterpret_cast<EventDispatcherLibEventPrivate*>(arg);
	SocketNotifierHash::Iterator it = disp->m_notifiers.find(fd);
	while (it != disp->m_notifiers.end() && it.key() == fd) {
		SocketNotifierInfo& data = it.value();
		QSocketNotifier::Type type = data.sn->type();

		if ((QSocketNotifier::Read == type && (events & EV_READ)) || (QSocketNotifier::Write == type && (events & EV_WRITE))) {
			QEvent* e = new QEvent(QEvent::SockAct);
			QCoreApplication::postEvent(data.sn, e);
		}

		++it;
	}

	disp->m_seen_event = true;
}

void EventDispatcherLibEventPrivate::timer_callback(int fd, short int events, void* arg)
{
	Q_ASSERT(-1 == fd);
	Q_ASSERT(events & EV_TIMEOUT);
	Q_UNUSED(fd)
	Q_UNUSED(events)

	EventDispatcherLibEventPrivate::TimerInfo* info = reinterpret_cast<EventDispatcherLibEventPrivate::TimerInfo*>(arg);

	struct timeval now;
	struct timeval delta;
	evutil_gettimeofday(&now, 0);
	calculateNextTimeout(info, now, delta);

	event_add(info->ev, &delta);
	info->self->m_seen_event = true;

	QTimerEvent* event = new QTimerEvent(info->timerId);
	QCoreApplication::postEvent(info->object, event);
}

void EventDispatcherLibEventPrivate::wake_up_handler(int fd, short int events, void* arg)
{
	Q_UNUSED(events)
	Q_UNUSED(arg)

#ifdef HAVE_SYS_EVENTFD_H
	char buf[8];
	if (::read(fd, buf, 8) != 8) {
		qErrnoWarning("%s: read failed", Q_FUNC_INFO);
	}
#else
	char buf[256];
	while (::read(fd, buf, 256) > 0) {
		// Do nothing
	}
#endif
}

void EventDispatcherLibEventPrivate::disableSocketNotifiers(bool disable)
{
	SocketNotifierHash::Iterator it = this->m_notifiers.begin();
	while (it != this->m_notifiers.end()) {
		SocketNotifierInfo& data = it.value();
		disable ? event_del(data.ev) : event_add(data.ev, 0);
		++it;
	}
}


EventDispatcherLibEvent::EventDispatcherLibEvent(QObject* parent)
	: QAbstractEventDispatcher(parent), d_ptr(new EventDispatcherLibEventPrivate(this))
{
}

EventDispatcherLibEvent::~EventDispatcherLibEvent(void)
{
}

bool EventDispatcherLibEvent::processEvents(QEventLoop::ProcessEventsFlags flags)
{
	Q_D(EventDispatcherLibEvent);
	return d->processEvents(flags);
}

bool EventDispatcherLibEvent::hasPendingEvents(void)
{
	extern uint qGlobalPostedEventsCount();
	return qGlobalPostedEventsCount() > 0;
}

void EventDispatcherLibEvent::registerSocketNotifier(QSocketNotifier* notifier)
{
#ifndef QT_NO_DEBUG
	if (notifier->socket() < 0) {
		qWarning("QSocketNotifier: Internal error: sockfd < 0");
		return;
	}
	else if (notifier->thread() != thread() || thread() != QThread::currentThread()) {
		qWarning("QSocketNotifier: socket notifiers cannot be enabled from another thread");
		return;
	}
#endif

	if (notifier->type() == QSocketNotifier::Exception) {
		return;
	}

	Q_D(EventDispatcherLibEvent);
	d->registerSocketNotifier(notifier);
}

void EventDispatcherLibEvent::unregisterSocketNotifier(QSocketNotifier* notifier)
{
#ifndef QT_NO_DEBUG
	if (notifier->socket() < 0) {
		qWarning("QSocketNotifier: Internal error: sockfd < 0");
		return;
	}
	else if (notifier->thread() != thread() || thread() != QThread::currentThread()) {
		qWarning("QSocketNotifier: socket notifiers cannot be disabled from another thread");
		return;
	}
#endif

	// Short circuit, we do not support QSocketNotifier::Exception
	if (notifier->type() == QSocketNotifier::Exception) {
		return;
	}

	Q_D(EventDispatcherLibEvent);
	d->unregisterSocketNotifier(notifier);
}

void EventDispatcherLibEvent::registerTimer(
	int timerId,
	int interval,
#if QT_VERSION >= 0x050000
	Qt::TimerType timerType,
#endif
	QObject* object
)
{
#ifndef QT_NO_DEBUG
	if (timerId < 1 || interval < 0 || !object) {
		qWarning("%s: invalid arguments", Q_FUNC_INFO);
		return;
	}

	if (object->thread() != this->thread() && this->thread() != QThread::currentThread()) {
		qWarning("%s: timers cannot be started from another thread", Q_FUNC_INFO);
		return;
	}
#endif

	Qt::TimerType type;
#if QT_VERSION >= 0x050000
	type = timerType;
#else
	type = Qt::CoarseTimer;
#endif

	Q_D(EventDispatcherLibEvent);
	d->registerTimer(timerId, interval, type, object);
}

bool EventDispatcherLibEvent::unregisterTimer(int timerId)
{
#ifndef QT_NO_DEBUG
	if (timerId < 1) {
		qWarning("%s: invalid arguments", Q_FUNC_INFO);
		return false;
	}

	if (this->thread() != QThread::currentThread()) {
		qWarning("%s: timers cannot be stopped from another thread", Q_FUNC_INFO);
		return false;
	}
#endif

	Q_D(EventDispatcherLibEvent);
	return d->unregisterTimer(timerId);
}

bool EventDispatcherLibEvent::unregisterTimers(QObject* object)
{
#ifndef QT_NO_DEBUG
	if (!object) {
		qWarning("%s: invalid arguments", Q_FUNC_INFO);
		return false;
	}

	if (object->thread() != this->thread() && this->thread() != QThread::currentThread()) {
		qWarning("%s: timers cannot be stopped from another thread", Q_FUNC_INFO);
		return false;
	}
#endif

	Q_D(EventDispatcherLibEvent);
	return d->unregisterTimers(object);
}

QList<QAbstractEventDispatcher::TimerInfo> EventDispatcherLibEvent::registeredTimers(QObject* object) const
{
	if (!object) {
		qWarning("%s: invalid argument", Q_FUNC_INFO);
		return QList<QAbstractEventDispatcher::TimerInfo>();
	}

	Q_D(const EventDispatcherLibEvent);
	return d->registeredTimers(object);
}

#if QT_VERSION >= 0x050000
int EventDispatcherLibEvent::remainingTime(int timerId)
{
	Q_D(const EventDispatcherLibEvent);
	return d->remainingTime(timerId);
}
#endif

void EventDispatcherLibEvent::wakeUp(void)
{
	Q_D(EventDispatcherLibEvent);

	quint64 x = 1;
	if (::write(d->m_pipe_write, reinterpret_cast<const char*>(&x), sizeof(x)) != sizeof(x)) {
		qErrnoWarning("%s: write failed", Q_FUNC_INFO);
	}

	d->m_seen_event = true;
}

void EventDispatcherLibEvent::interrupt(void)
{
	Q_D(EventDispatcherLibEvent);
	d->m_interrupt = true;
	this->wakeUp();
}

void EventDispatcherLibEvent::flush(void)
{
}
