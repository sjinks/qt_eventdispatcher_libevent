#ifndef EVENT2_EVENT_H
#define EVENT2_EVENT_H

#define SJ_LIBEVENT_EMULATION 1

#include <event.h>
#include "qt4compat.h"

typedef int evutil_socket_t;
typedef void(*event_callback_fn)(evutil_socket_t, short, void*);

Q_DECL_HIDDEN inline struct event* event_new(struct event_base* base, evutil_socket_t fd, short int events, event_callback_fn callback, void* callback_arg)
{
	struct event* e = new struct event;
	event_set(e, fd, events, callback, callback_arg);
	event_base_set(base, e);
	return e;
}

Q_DECL_HIDDEN inline void event_free(struct event* e)
{
	delete e;
}

Q_DECL_HIDDEN inline void event_reinit(struct event_base* base)
{
	qWarning("%s: not implemented", Q_FUNC_INFO);
}

#endif
