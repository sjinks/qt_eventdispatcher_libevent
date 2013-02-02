#ifndef EVENTDISPATCHER_H
#define EVENTDISPATCHER_H

#include "eventdispatcher_libevent.h"

class EventDispatcher : public EventDispatcherLibEvent {
	Q_OBJECT
public:
	explicit EventDispatcher(QObject* parent = 0) : EventDispatcherLibEvent(parent) {}
};

#endif // EVENTDISPATCHER_H
