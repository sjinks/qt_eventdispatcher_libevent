#include <QtCore/QCoreApplication>
#include <QtCore/QEvent>
#include <QtCore/QSocketNotifier>
#include <QtCore/QThread>
#include "eventdispatcher_libevent.h"
#if QT_VERSION >= 0x050000
#	include <event2/thread.h>
#endif
#include "utils_p.h"
#include "eventdispatcher_libevent_p.h"

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
	  m_notifiers(), m_timers(), m_timers_to_reactivate(), m_seen_event(false)
{
	static bool init = false;
	if (!init) {
		init = true;
		event_set_log_callback(event_log_callback);
#if QT_VERSION >= 0x050000
		evthread_use_pthreads();
#endif
	}

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

	Q_ASSERT(this->m_timers_to_reactivate.isEmpty());

	bool exclude_notifiers = (flags & QEventLoop::ExcludeSocketNotifiers);
	bool exclude_timers    = (flags & QEventLoop::X11ExcludeTimers);

	if (exclude_notifiers) {
		this->disableSocketNotifiers(true);
	}

	if (exclude_timers) {
		this->disableTimers(true);
	}

	bool result = false;
	this->m_seen_event = false;

	if (q->hasPendingEvents()) {
		QCoreApplication::sendPostedEvents();
		result = true;
		flags &= ~QEventLoop::WaitForMoreEvents;
	}

	QSet<int> timers;

	if ((flags & QEventLoop::WaitForMoreEvents)) {
		if (!this->m_interrupt) {
			this->m_seen_event = false;
			do {
				Q_EMIT q->aboutToBlock();
				event_base_loop(this->m_base, EVLOOP_ONCE);

				timers.unite(this->m_timers_to_reactivate);
				this->m_timers_to_reactivate.clear();

				if (!this->m_seen_event) {
					// When this->m_seen_event == false, this means we have been woken up by wake().
					// Send all potsed events here or tst_QEventLoop::execAfterExit() freezes
					QCoreApplication::sendPostedEvents(); // an event handler invoked by sendPostedEvents() may invoke processEvents() again
				}

				Q_EMIT q->awake();
			} while (!this->m_interrupt && !this->m_seen_event);
		}
	}
	else {
		event_base_loop(this->m_base, EVLOOP_ONCE | EVLOOP_NONBLOCK);
		Q_EMIT q->awake(); // If removed, tst_QEventLoop::processEvents() fails
		result |= this->m_seen_event;
	}

	timers.unite(this->m_timers_to_reactivate);
	this->m_timers_to_reactivate.clear();

	result |= this->m_seen_event;
	QCoreApplication::sendPostedEvents(); // an event handler invoked by sendPostedEvents() may invoke processEvents() again

	// Now that all event handlers have finished (and we returned from the recusrion), reactivate all pending timers
	if (!timers.isEmpty()) {
		struct timeval now;
		struct timeval delta;
		evutil_gettimeofday(&now, 0);
		QSet<int>::ConstIterator it = timers.constBegin();
		while (it != timers.constEnd()) {
			TimerHash::Iterator tit = this->m_timers.find(*it);
			if (tit != this->m_timers.end()) {
				EventDispatcherLibEventPrivate::TimerInfo* info = tit.value();

				if (!event_pending(info->ev, EV_TIMEOUT, 0)) { // false in tst_QTimer::restartedTimerFiresTooSoon()
					calculateNextTimeout(info, now, delta);
					event_add(info->ev, &delta);
				}
			}

			++it;
		}
	}

	if (exclude_notifiers) {
		this->disableSocketNotifiers(false);
	}

	if (exclude_timers) {
		this->disableTimers(false);
	}

	this->m_interrupt = false;
	return result;
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
	disp->m_seen_event = true;
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
}

void EventDispatcherLibEventPrivate::timer_callback(int fd, short int events, void* arg)
{
	Q_ASSERT(-1 == fd);
	Q_ASSERT(events & EV_TIMEOUT);
	Q_UNUSED(fd)
	Q_UNUSED(events)

	EventDispatcherLibEventPrivate::TimerInfo* info = reinterpret_cast<EventDispatcherLibEventPrivate::TimerInfo*>(arg);
	info->self->m_seen_event = true;

	// Timer can be reactivated only after its callback finishes; processEvents() will take care of this
	info->self->m_timers_to_reactivate.insert(info->timerId);

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

void EventDispatcherLibEventPrivate::disableTimers(bool disable)
{
	struct timeval now;
	if (!disable) {
		evutil_gettimeofday(&now, 0);
	}

	TimerHash::Iterator it = this->m_timers.begin();
	while (it != this->m_timers.end()) {
		EventDispatcherLibEventPrivate::TimerInfo* info = it.value();
		if (disable) {
			event_del(info->ev);
		}
		else {
			struct timeval delta;
			calculateNextTimeout(info, now, delta);
			event_add(info->ev, &delta);
		}

		++it;
	}
}
