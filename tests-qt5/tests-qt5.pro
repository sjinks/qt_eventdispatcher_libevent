TEMPLATE = subdirs

SUBDIRS = \
	dispatcher \
	eventloop \
	socketnotifier \
	timer \
	events

dispatcher.file     = dispatcher/qeventdispatcher.pro
eventloop.file      = eventloop/qeventloop.pro
socketnotifier.file = socketnotifier/qsocketnotifier.pro
timer.file          = timer/qtimer.pro
events.file         = events/events.pro
