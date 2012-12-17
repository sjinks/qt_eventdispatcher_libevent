#include <QtCore/QCoreApplication>
#include <QtCore/QEvent>
#include "eventdispatcher_libev_p.h"

#ifdef WIN32
#	include "win32_utils.h"
#endif

void EventDispatcherLibEvPrivate::calculateCoarseTimerTimeout(EventDispatcherLibEvPrivate::TimerInfo* info, const struct timeval& now, struct timeval& when)
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
	uint msec         = static_cast<uint>(info->when.tv_usec / 1000);
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

	if (timercmp(&when, &now, <)) {
		when.tv_sec  += interval / 1000;
		when.tv_usec += (interval % 1000) * 1000;
		if (when.tv_usec > 999999) {
			++when.tv_sec;
			when.tv_usec -= 1000000;
		}
	}

	Q_ASSERT(timercmp(&now, &when, <=));
}

ev_tstamp EventDispatcherLibEvPrivate::calculateNextTimeout(EventDispatcherLibEvPrivate::TimerInfo* info, const struct timeval& now)
{
	struct timeval tv_interval;
	struct timeval when;
	tv_interval.tv_sec  = info->interval / 1000;
	tv_interval.tv_usec = (info->interval % 1000) * 1000;

	if (info->interval) {
		qlonglong tnow  = (qlonglong(now.tv_sec)        * 1000) + (now.tv_usec        / 1000);
		qlonglong twhen = (qlonglong(info->when.tv_sec) * 1000) + (info->when.tv_usec / 1000);

		if ((info->interval < 1000 && twhen - tnow > 1500) || (info->interval >= 1000 && twhen - tnow > 1.2*info->interval)) {
			info->when = now;
		}
	}

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
			timeradd(&info->when, &tv_interval, &info->when);
			if (timercmp(&info->when, &now, <)) {
				timeradd(&now, &tv_interval, &info->when);
			}

			when = info->when;
		}
		else {
			when = now;
		}
	}
	else {
		timeradd(&info->when, &tv_interval, &info->when);
		if (timercmp(&info->when, &now, <)) {
			timeradd(&now, &tv_interval, &info->when);
		}

		EventDispatcherLibEvPrivate::calculateCoarseTimerTimeout(info, now, when);
	}

	return (when.tv_sec - now.tv_sec) + (when.tv_usec - now.tv_usec) * 1.0E-6;
}

void EventDispatcherLibEvPrivate::registerTimer(int timerId, int interval, Qt::TimerType type, QObject* object)
{
	struct timeval now;
	gettimeofday(&now, 0);

	EventDispatcherLibEvPrivate::TimerInfo* info = new EventDispatcherLibEvPrivate::TimerInfo;
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

	ev_tstamp delta = EventDispatcherLibEvPrivate::calculateNextTimeout(info, now);

	struct ev_timer* tmp = &info->ev;
	ev_timer_init(tmp, EventDispatcherLibEvPrivate::timer_callback, delta, 0);
	info->ev.data = info;
	ev_timer_start(this->m_base, tmp);
	this->m_timers.insert(timerId, info);
}

bool EventDispatcherLibEvPrivate::unregisterTimer(int timerId)
{
	TimerHash::Iterator it = this->m_timers.find(timerId);
	if (it != this->m_timers.end()) {
		EventDispatcherLibEvPrivate::TimerInfo* info = it.value();
		ev_timer_stop(this->m_base, &info->ev);
		delete info;
		this->m_timers.erase(it);
		return true;
	}

	return false;
}

bool EventDispatcherLibEvPrivate::unregisterTimers(QObject* object)
{
	TimerHash::Iterator it = this->m_timers.begin();
	while (it != this->m_timers.end()) {
		EventDispatcherLibEvPrivate::TimerInfo* info = it.value();
		if (object == info->object) {
			ev_timer_stop(this->m_base, &info->ev);
			delete info;
			it = this->m_timers.erase(it);
		}
		else {
			++it;
		}
	}

	return true;
}

QList<QAbstractEventDispatcher::TimerInfo> EventDispatcherLibEvPrivate::registeredTimers(QObject* object) const
{
	QList<QAbstractEventDispatcher::TimerInfo> res;

	TimerHash::ConstIterator it = this->m_timers.constBegin();
	while (it != this->m_timers.constEnd()) {
		EventDispatcherLibEvPrivate::TimerInfo* info = it.value();
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

int EventDispatcherLibEvPrivate::remainingTime(int timerId) const
{
	TimerHash::ConstIterator it = this->m_timers.find(timerId);
	if (it != this->m_timers.end()) {
		const EventDispatcherLibEvPrivate::TimerInfo* info = it.value();

		const struct ev_timer* tmp = &info->ev;
		if (ev_is_pending(tmp) || ev_is_active(tmp)) {
			ev_tstamp now  = ev_now(this->m_base);
			ev_tstamp when = info->when.tv_sec + info->when.tv_usec * 1.0E-6;

			if (now > when) {
				return 0;
			}

			return static_cast<int>((when - now) * 1000);
		}
	}

	return -1;
}

void EventDispatcherLibEvPrivate::timer_callback(struct ev_loop* loop, struct ev_timer* w, int revents)
{
	Q_UNUSED(revents)

	EventDispatcherLibEvPrivate* self            = reinterpret_cast<EventDispatcherLibEvPrivate*>(ev_userdata(loop));
	EventDispatcherLibEvPrivate::TimerInfo* info = reinterpret_cast<EventDispatcherLibEvPrivate::TimerInfo*>(w->data);
	self->m_seen_event = true;

	// Timer can be reactivated only after its callback finishes; processEvents() will take care of this
	self->m_timers_to_reactivate.insert(info->timerId);

	QTimerEvent* event = new QTimerEvent(info->timerId);
	QCoreApplication::postEvent(info->object, event);
}

void EventDispatcherLibEvPrivate::disableTimers(bool disable)
{
	struct timeval now;
	if (!disable) {
		gettimeofday(&now, 0);
	}

	TimerHash::Iterator it = this->m_timers.begin();
	while (it != this->m_timers.end()) {
		EventDispatcherLibEvPrivate::TimerInfo* info = it.value();
		if (disable) {
			ev_timer_stop(this->m_base, &info->ev);
		}
		else {
			ev_tstamp delta = EventDispatcherLibEvPrivate::calculateNextTimeout(info, now);
			struct ev_timer* tmp = &info->ev;
			ev_timer_set(tmp, delta, 0);
			ev_timer_start(this->m_base, tmp);
		}

		++it;
	}
}

void EventDispatcherLibEvPrivate::killTimers(void)
{
	if (!this->m_timers.isEmpty()) {
		TimerHash::Iterator it = this->m_timers.begin();
		while (it != this->m_timers.end()) {
			EventDispatcherLibEvPrivate::TimerInfo* info = it.value();
			ev_timer_stop(this->m_base, &info->ev);
			delete info;
			++it;
		}

		this->m_timers.clear();
	}
}
