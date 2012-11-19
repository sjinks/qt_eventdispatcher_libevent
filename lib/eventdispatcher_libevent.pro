QT        -= gui
TARGET     = eventdispatcher_libevent
TEMPLATE   = lib
CONFIG    += staticlib create_prl create_pc link_pkgconfig
PKGCONFIG += libevent
HEADERS   += eventdispatcher_libevent.h eventdispatcher_libevent_p.h utils_p.h
SOURCES   += eventdispatcher_libevent.cpp eventdispatcher_libevent_p.cpp utils_p.cpp timers_p.cpp socknot_p.cpp

greaterThan(QT_MAJOR_VERSION, 4) {
	PKGCONFIG += libevent_pthreads
}

target.path   = /usr/lib
headers.path  = /usr/include
headers.files = eventdispatcher_libevent.h

INSTALLS += target headers

QMAKE_PKGCONFIG_NAME        = eventdispatcher_libevent
QMAKE_PKGCONFIG_DESCRIPTION = "Libevent-based event dispatcher for Qt"
QMAKE_PKGCONFIG_LIBDIR      = $$target.path
QMAKE_PKGCONFIG_INCDIR      = $$headers.path
QMAKE_PKGCONFIG_DESTDIR     = pkgconfig
