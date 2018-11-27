#ifndef EVENTDISPATCHERQPA_H
#define EVENTDISPATCHERQPA_H

#include "eventdispatcher_libevent_qpa.h"

class EventDispatcherQPA : public EventDispatcherLibEventQPA {
	Q_OBJECT
public:
	explicit EventDispatcherQPA(QObject* parent = 0) : EventDispatcherLibEventQPA(parent) {}
};

#endif // EVENTDISPATCHERQPA_H
