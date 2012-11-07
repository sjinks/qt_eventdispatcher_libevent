#include <QtCore/QCoreApplication>
#include <QtCore/QEvent>
#include <QtCore/QHash>
#include <QtCore/QMultiHash>
#include <QtCore/QSocketNotifier>
#include <QtCore/QThread>
#include <event2/event.h>
#include <event2/thread.h>
#include <unistd.h>
#include "eventdispatcher_libevent.h"

static int make_pipe(int pipefd[2])
{
	int res = ::pipe(pipefd);
	if (-1 == res) {
		return res;
	}

	evutil_make_socket_closeonexec(pipefd[0]);
	evutil_make_socket_nonblocking(pipefd[0]);

	evutil_make_socket_closeonexec(pipefd[1]);
	evutil_make_socket_nonblocking(pipefd[1]);

	return 0;
}

class EventDispatcherLibEventPrivate {
public:
	EventDispatcherLibEventPrivate(EventDispatcherLibEvent* const q);
	~EventDispatcherLibEventPrivate(void);
	bool processEvents(QEventLoop::ProcessEventsFlags flags);
	void registerSocketNotifier(QSocketNotifier* notifier);
	void unregisterSocketNotifier(QSocketNotifier* notifier);
	void registerTimer(int timerId, int interval, QObject* object);
	bool unregisterTimer(int timerId);
	bool unregisterTimers(QObject* object);
	QList<QAbstractEventDispatcher::TimerInfo> registeredTimers(QObject* object) const;


	struct SocketNotifierInfo {
		QSocketNotifier* sn;
		struct event* ev;
	};

	struct TimerInfo {
		EventDispatcherLibEventPrivate* self;
		struct event* ev;
		int timerId;
		int interval;
		QObject* object;
	};

	typedef QMultiHash<evutil_socket_t, SocketNotifierInfo> SocketNotifierHash;
	typedef QHash<int, TimerInfo*> TimerHash;

private:
	Q_DISABLE_COPY(EventDispatcherLibEventPrivate)
	Q_DECLARE_PUBLIC(EventDispatcherLibEvent)
	EventDispatcherLibEvent* const q_ptr;

	bool m_interrupt;
	int m_thread_pipe[2];
	struct event_base* m_base;
	struct event* m_wakeup;
	SocketNotifierHash m_notifiers;
	TimerHash m_timers;

	static void socket_notifier_callback(evutil_socket_t fd, short int events, void* arg);
	static void timer_callback(evutil_socket_t fd, short int events, void* arg);
	static void wake_up_handler(evutil_socket_t fd, short int events, void* arg);
};

EventDispatcherLibEventPrivate::EventDispatcherLibEventPrivate(EventDispatcherLibEvent* const q)
	: q_ptr(q), m_interrupt(false), m_thread_pipe(), m_base(0), m_wakeup(0),
	  m_notifiers(), m_timers()
{
	this->m_base = event_base_new();

	if (-1 == make_pipe(this->m_thread_pipe)) {
#ifndef QT_NO_DEBUG
		qFatal("%s: Unable to create pipe", Q_FUNC_INFO);
#else
		qErrnoWarning("%s: Unable to create pipe", Q_FUNC_INFO);
#endif

		m_thread_pipe[0] = -1;
		m_thread_pipe[1] = -1;
	}
	else {
		this->m_wakeup = event_new(this->m_base, this->m_thread_pipe[0], EV_READ | EV_PERSIST, EventDispatcherLibEventPrivate::wake_up_handler, 0);
		event_add(this->m_wakeup, 0);
	}
}

EventDispatcherLibEventPrivate::~EventDispatcherLibEventPrivate(void)
{
	if (this->m_thread_pipe[0] != -1) {
		evutil_closesocket(this->m_thread_pipe[0]);
	}

	if (this->m_thread_pipe[1] != -1) {
		evutil_closesocket(this->m_thread_pipe[1]);
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

	Q_EMIT q->awake();
	QCoreApplication::sendPostedEvents();

	const bool canWait = (!this->m_interrupt && (flags & QEventLoop::WaitForMoreEvents));

	do {
		if (canWait) {
			Q_EMIT q->aboutToBlock();
			event_base_loop(this->m_base, EVLOOP_ONCE);
		}
		else {
			event_base_loop(this->m_base, EVLOOP_NONBLOCK | EVLOOP_ONCE);
		}

		QCoreApplication::sendPostedEvents();
	} while (!this->m_interrupt && !canWait);

	this->m_interrupt = false;
	return true;
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

void EventDispatcherLibEventPrivate::registerTimer(int timerId, int interval, QObject* object)
{
	EventDispatcherLibEventPrivate::TimerInfo* info = new EventDispatcherLibEventPrivate::TimerInfo;
	info->self     = this;
	info->ev       = event_new(this->m_base, -1, EV_PERSIST, EventDispatcherLibEventPrivate::timer_callback, info);
	info->timerId  = timerId;
	info->interval = interval;
	info->object   = object;

	struct timeval tv;
	tv.tv_sec  = interval / 1000;
	tv.tv_usec = interval % 1000;
	event_add(info->ev, &tv);

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
			QAbstractEventDispatcher::TimerInfo ti(it.key(), info->interval);
			res.append(ti);
			++it;
		}
	}

	return res;
}


void EventDispatcherLibEventPrivate::socket_notifier_callback(int fd, short int events, void* arg)
{
	EventDispatcherLibEventPrivate* disp = reinterpret_cast<EventDispatcherLibEventPrivate*>(arg);
	SocketNotifierHash::Iterator it = disp->m_notifiers.find(fd);
	while (it != disp->m_notifiers.end() && it.key() == fd) {
		SocketNotifierInfo& data = it.value();
		QSocketNotifier::Type type = data.sn->type();

		if ((QSocketNotifier::Read == type && (events & EV_READ)) || (QSocketNotifier::Write == type && (events & EV_WRITE))) {
			QEvent e(QEvent::SockAct);
			QCoreApplication::sendEvent(data.sn, &e);
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
	QTimerEvent event(info->timerId);
	QCoreApplication::sendEvent(info->object, &event);
}

void EventDispatcherLibEventPrivate::wake_up_handler(int fd, short int events, void* arg)
{
	Q_UNUSED(events)
	Q_UNUSED(arg)

	char buf[256];
	while (::read(fd, buf, 256) > 0) {
		// Do nothing
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
	extern Q_CORE_EXPORT uint qGlobalPostedEventsCount();
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

void EventDispatcherLibEvent::registerTimer(int timerId, int interval, QObject* object)
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

	Q_D(EventDispatcherLibEvent);
	d->registerTimer(timerId, interval, object);
}

bool EventDispatcherLibEvent::unregisterTimer(int timerId)
{
#ifndef QT_NO_DEBUG
	if (timerId < 1) {
		qWarning("%s: invalid arguments", Q_FUNC_INFO);
		return;
	}

	if (this->thread() != QThread::currentThread()) {
		qWarning("%s: timers cannot be stopped from another thread", Q_FUNC_INFO);
		return;
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
		return;
	}

	if (object->thread() != this->thread() && this->thread() != QThread::currentThread()) {
		qWarning("%s: timers cannot be stopped from another thread", Q_FUNC_INFO);
		return;
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

void EventDispatcherLibEvent::wakeUp(void)
{
	Q_D(EventDispatcherLibEvent);

	if (d->m_thread_pipe[1] != -1) {
		if (::write(d->m_thread_pipe[1], "w", 1) != 1) {
			// Avoid compiler's warning
		}
	}
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
