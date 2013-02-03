#include "common.h"
#include "eventdispatcher_libevent.h"
#include "eventdispatcher_libevent_p.h"
#include "eventdispatcher_libevent_config.h"
#include "eventdispatcher_libevent_config_p.h"

#ifdef Q_OS_WIN
Q_GLOBAL_STATIC(WSAInitializer, wsa_initializer)
#endif

static void event_log_callback(int severity, const char* msg)
{
	switch (severity) {
		case _EVENT_LOG_WARN: qWarning("%s", msg); break;
		case _EVENT_LOG_ERR:  qCritical("%s", msg); break;
		default:              qDebug("%s", msg); break;
	}
}


EventDispatcherLibEventPrivate::EventDispatcherLibEventPrivate(EventDispatcherLibEvent* const q)
	: q_ptr(q), m_interrupt(false), m_base(0), m_wakeup(0), m_tco(0),
	  m_notifiers(), m_timers(), m_event_list(), m_seen_event(false)
{
	this->initialize(0);
}

EventDispatcherLibEventPrivate::EventDispatcherLibEventPrivate(EventDispatcherLibEvent* const q, const EventDispatcherLibEventConfig& cfg)
	: q_ptr(q), m_interrupt(false), m_base(0), m_wakeup(0), m_tco(0),
	  m_notifiers(), m_timers(), m_event_list(), m_seen_event(false)
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

#ifdef Q_OS_WIN
		if (!WSAInitialized()) {
			wsa_initializer();
		}
#endif

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

	this->m_tco = new ThreadCommunicationObject();
	if (!this->m_tco->valid()) {
		qFatal("%s: failed to create a thread communication object", Q_FUNC_INFO);
	}

	this->m_wakeup = event_new(this->m_base, this->m_tco->fd(), EV_READ | EV_PERSIST, EventDispatcherLibEventPrivate::wake_up_handler, this);
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

	delete this->m_tco;
}

bool EventDispatcherLibEventPrivate::processEvents(QEventLoop::ProcessEventsFlags flags)
{
	Q_Q(EventDispatcherLibEvent);

	const bool exclude_notifiers = (flags & QEventLoop::ExcludeSocketNotifiers);
	const bool exclude_timers    = (flags & QEventLoop::X11ExcludeTimers);

	exclude_notifiers && this->disableSocketNotifiers(true);
	exclude_timers    && this->disableTimers(true);

	this->m_interrupt  = false;
	this->m_seen_event = false;

	Q_EMIT q->awake();
#if QT_VERSION < 0x040500
	QCoreApplication::sendPostedEvents(0, (flags & QEventLoop::DeferredDeletion) ? -1 : 0);
#else
	QCoreApplication::sendPostedEvents();
#endif

	const bool can_wait = !this->m_interrupt && (flags & QEventLoop::WaitForMoreEvents);
	if (can_wait) {
		Q_EMIT q->aboutToBlock();
	}

	bool result = false;

	if (!this->m_interrupt) {
		event_base_loop(this->m_base, EVLOOP_ONCE | (can_wait ? 0 : EVLOOP_NONBLOCK));

#if QT_VERSION >= 0x040800
		EventList list;
		this->m_event_list.swap(list);
#else
		EventList list(this->m_event_list);
		this->m_event_list.clear();
#endif

		result = (list.size() > 0) | this->m_seen_event;

		for (int i=0; i<list.size(); ++i) {
			const PendingEvent& e = list.at(i);
			if (!e.first.isNull()) {
				QCoreApplication::sendEvent(e.first, e.second);
			}
		}

		struct timeval now;
		struct timeval delta;
		evutil_gettimeofday(&now, 0);

		// Now that all event handlers have finished (and we returned from the recusrion), reactivate all pending timers
		for (int i=0; i<list.size(); ++i) {
			const PendingEvent& e = list.at(i);
			if (!e.first.isNull() && e.second->type() == QEvent::Timer) {
				QTimerEvent* te = static_cast<QTimerEvent*>(e.second);
				if (te) {
					TimerHash::Iterator tit = this->m_timers.find(te->timerId());
					if (tit != this->m_timers.end()) {
						TimerInfo* info = tit.value();

						if (!event_pending(info->ev, EV_TIMEOUT, 0)) { // false in tst_QTimer::restartedTimerFiresTooSoon()
							EventDispatcherLibEventPrivate::calculateNextTimeout(info, now, delta);
							event_add(info->ev, &delta);
						}
					}
				}
			}

			delete e.second;
		}
	}

	exclude_notifiers && this->disableSocketNotifiers(false);
	exclude_timers    && this->disableTimers(false);

	return result;
}

void EventDispatcherLibEventPrivate::wake_up_handler(int fd, short int events, void* arg)
{
	Q_UNUSED(fd)
	Q_UNUSED(events)

	EventDispatcherLibEventPrivate* disp = reinterpret_cast<EventDispatcherLibEventPrivate*>(arg);
	Q_ASSERT(disp != 0);

	disp->m_seen_event = true;
	disp->m_tco->awaken();
}
