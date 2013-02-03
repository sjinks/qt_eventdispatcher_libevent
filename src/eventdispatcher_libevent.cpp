#include "common.h"
#include "eventdispatcher_libevent.h"
#include "eventdispatcher_libevent_p.h"

EventDispatcherLibEvent::EventDispatcherLibEvent(QObject* parent)
	: QAbstractEventDispatcher(parent), d_ptr(new EventDispatcherLibEventPrivate(this))
{
}

EventDispatcherLibEvent::EventDispatcherLibEvent(const EventDispatcherLibEventConfig& config, QObject* parent)
	: QAbstractEventDispatcher(parent), d_ptr(new EventDispatcherLibEventPrivate(this, config))
{
}

EventDispatcherLibEvent::~EventDispatcherLibEvent(void)
{
#if QT_VERSION < 0x040600
	delete this->d_ptr;
	this->d_ptr = 0;
#endif
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

	if (notifier->thread() != thread() || thread() != QThread::currentThread()) {
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

#if defined(Q_OS_WIN) && QT_VERSION >= 0x050000
bool EventDispatcherLibEvent::registerEventNotifier(QWinEventNotifier* notifier)
{
	Q_UNUSED(notifier)
	Q_UNIMPLEMENTED();
	return false;
}

void EventDispatcherLibEvent::unregisterEventNotifier(QWinEventNotifier* notifier)
{
	Q_UNUSED(notifier)
	Q_UNIMPLEMENTED();
}
#endif

void EventDispatcherLibEvent::wakeUp(void)
{
	Q_D(EventDispatcherLibEvent);
	d->m_tco->wakeUp();
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

EventDispatcherLibEvent::EventDispatcherLibEvent(EventDispatcherLibEventPrivate& dd, QObject* parent)
	: QAbstractEventDispatcher(parent), d_ptr(&dd)
{
}

void EventDispatcherLibEvent::reinitialize(void)
{
	Q_D(EventDispatcherLibEvent);
	event_reinit(d->m_base);
}
