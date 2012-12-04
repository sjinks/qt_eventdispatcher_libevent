QT       -= gui
TARGET    = eventdispatcher_libevent
TEMPLATE  = lib
DESTDIR   = ../lib
CONFIG   += staticlib create_prl release
HEADERS  += eventdispatcher_libevent.h eventdispatcher_libevent_p.h eventdispatcher_libevent_config.h eventdispatcher_libevent_config_p.h
SOURCES  += eventdispatcher_libevent.cpp eventdispatcher_libevent_p.cpp timers_p.cpp socknot_p.cpp eventdispatcher_libevent_config.cpp

headers.files = eventdispatcher_libevent.h eventdispatcher_libevent_config.h

unix {
	CONFIG    += create_pc link_pkgconfig
	PKGCONFIG += libevent

	target.path  = /usr/lib
	headers.path = /usr/include

	QMAKE_PKGCONFIG_NAME        = eventdispatcher_libevent
	QMAKE_PKGCONFIG_DESCRIPTION = "Libevent-based event dispatcher for Qt"
	QMAKE_PKGCONFIG_LIBDIR      = $$target.path
	QMAKE_PKGCONFIG_INCDIR      = $$headers.path
	QMAKE_PKGCONFIG_DESTDIR     = pkgconfig
}
else {
	LIBS        += -levent_core
	headers.path = $$DESTDIR
}

INSTALLS += target headers

