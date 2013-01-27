#include <qplatformdefs.h>
#include <QtCore/QCoreApplication>
#include "eventdispatcher_libevent.h"
#include "eventdispatcher_libevent_p.h"
#include "eventdispatcher_libevent_config.h"
#include "eventdispatcher_libevent_config_p.h"

static void event_log_callback(int severity, const char* msg)
{
	switch (severity) {
		case _EVENT_LOG_WARN: qWarning("%s", msg); break;
		case _EVENT_LOG_ERR:  qCritical("%s", msg); break;
		default:              qDebug("%s", msg); break;
	}
}


EventDispatcherLibEventPrivate::EventDispatcherLibEventPrivate(EventDispatcherLibEvent* const q)
	: q_ptr(q), m_interrupt(false), m_base(0), m_wakeup(0), m_wakeups(),
	  m_notifiers(), m_timers(), m_timers_to_reactivate(), m_seen_event(false)
{
	this->initialize(0);
}

EventDispatcherLibEventPrivate::EventDispatcherLibEventPrivate(EventDispatcherLibEvent* const q, const EventDispatcherLibEventConfig& cfg)
	: q_ptr(q), m_interrupt(false), m_base(0), m_wakeup(0), m_wakeups(),
	  m_notifiers(), m_timers(), m_timers_to_reactivate(), m_seen_event(false)
{
#ifdef SJ_LIBEVENT_EMULATION
	Q_UNUSED(cfg)
	qWarning("LibEvent 1.x does not support custom configurations");
	this->initialize(0);
#else
	this->initialize(&cfg);
#endif
}

void EventDispatcherLibEventPrivate::initialize(const EventDispatcherLibEventConfig* cfg)
{
	static bool init = false;
	if (!init) {
		init = true;
		event_set_log_callback(event_log_callback);

#if defined(LIBEVENT_VERSION_NUMBER) && LIBEVENT_VERSION_NUMBER > 0x02010100
		qAddPostRoutine(libevent_global_shutdown);
#endif
	}

#ifndef SJ_LIBEVENT_EMULATION
	if (cfg) {
		this->m_base = event_base_new_with_config(cfg->d_func()->m_cfg);
		if (!this->m_base) {
			qWarning("%s: Cannot create the event base with the specified configuration", Q_FUNC_INFO);
		}
	}
#else
	Q_UNUSED(cfg)
#endif

	if (!this->m_base) {
		this->m_base = event_base_new();
		Q_CHECK_PTR(this->m_base);
	}

	this->m_wakeup = event_new(this->m_base, 0, EV_PERSIST, EventDispatcherLibEventPrivate::wake_up_handler, this);
	Q_CHECK_PTR(this->m_wakeup);
	event_add(this->m_wakeup, 0);
}

EventDispatcherLibEventPrivate::~EventDispatcherLibEventPrivate(void)
{
	if (this->m_wakeup) {
		event_del(this->m_wakeup);
		event_free(this->m_wakeup);
		this->m_wakeup = 0;
	}

	this->killTimers();
	this->killSocketNotifiers();

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
					EventDispatcherLibEventPrivate::calculateNextTimeout(info, now, delta);
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

void EventDispatcherLibEventPrivate::wake_up_handler(int fd, short int events, void* arg)
{
	Q_UNUSED(fd)
	Q_UNUSED(events)

	EventDispatcherLibEventPrivate* disp = reinterpret_cast<EventDispatcherLibEventPrivate*>(arg);
	Q_ASSERT(disp != 0);

	if (!disp->m_wakeups.testAndSetRelease(1, 0)) {
		qCritical("%s: internal error, wakeUps.testAndSetRelease(1, 0) failed!", Q_FUNC_INFO);
	}
}
