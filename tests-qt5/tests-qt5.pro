TEMPLATE = subdirs

SUBDIRS = \
	dispatcher \
	eventloop \
	timer

dispatcher.file = dispatcher/qeventdispatcher.pro
eventloop.file  = eventloop/qeventloop.pro
timer.file      = timer/qtimer.pro
