#ifndef EVENTDISPATCHER_LIBEVENT_P_H
#define EVENTDISPATCHER_LIBEVENT_P_H

#include <qplatformdefs.h>
#include <QtCore/QAbstractEventDispatcher>
#include <QtCore/QAtomicInt>
#include <QtCore/QHash>
#include <QtCore/QMultiHash>
#include <QtCore/QSet>
#include <event2/event.h>

#if !defined(LIBEVENT_VERSION_NUMBER) || LIBEVENT_VERSION_NUMBER < 0x02000400
#	error "libevent >= 2.0.4 is required"
#endif

#if QT_VERSION < 0x050000
namespace Qt { // Sorry
	enum TimerType {
		PreciseTimer,
		CoarseTimer,
		VeryCoarseTimer
	};
}
#endif

class EventDispatcherLibEvent;

class Q_DECL_HIDDEN EventDispatcherLibEventPrivate {
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
	struct event_base* m_base;
	struct event* m_wakeup;
	QAtomicInt m_wakeups;
	SocketNotifierHash m_notifiers;
	TimerHash m_timers;
	QSet<int> m_timers_to_reactivate;
	bool m_seen_event;

	static void calculateCoarseTimerTimeout(EventDispatcherLibEventPrivate::TimerInfo* info, const struct timeval& now, struct timeval& when);
	static void calculateNextTimeout(EventDispatcherLibEventPrivate::TimerInfo* info, const struct timeval& now, struct timeval& delta);

	static void socket_notifier_callback(evutil_socket_t fd, short int events, void* arg);
	static void timer_callback(evutil_socket_t fd, short int events, void* arg);
	static void wake_up_handler(evutil_socket_t fd, short int events, void* arg);

	void disableSocketNotifiers(bool disable);
	void killSocketNotifiers(void);
	void disableTimers(bool disable);
	void killTimers(void);
};

#endif // EVENTDISPATCHER_LIBEVENT_P_H
