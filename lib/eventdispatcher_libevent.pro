QT        -= gui
TARGET     = eventdispatcher_libevent
TEMPLATE   = lib
CONFIG    += staticlib create_prl create_pc link_pkgconfig
PKGCONFIG += libevent
HEADERS   += eventdispatcher_libevent.h
SOURCES   += eventdispatcher_libevent.cpp

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
