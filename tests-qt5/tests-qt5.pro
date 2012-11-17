TEMPLATE = subdirs

SUBDIRS = \
	dispatcher \
	eventloop \
	socketnotifier \
	timer

dispatcher.file     = dispatcher/qeventdispatcher.pro
eventloop.file      = eventloop/qeventloop.pro
socketnotifier.file = socketnotifier/qsocketnotifier.pro
timer.file          = timer/qtimer.pro
