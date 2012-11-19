#include <QtCore/QCoreApplication>
#include <QtCore/QEvent>
#include <QtCore/QSocketNotifier>
#include "eventdispatcher_libevent_p.h"

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

void EventDispatcherLibEventPrivate::disableSocketNotifiers(bool disable)
{
	SocketNotifierHash::Iterator it = this->m_notifiers.begin();
	while (it != this->m_notifiers.end()) {
		SocketNotifierInfo& data = it.value();
		disable ? event_del(data.ev) : event_add(data.ev, 0);
		++it;
	}
}
