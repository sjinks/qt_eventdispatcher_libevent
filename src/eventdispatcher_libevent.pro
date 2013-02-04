QT      -= gui
TARGET   = eventdispatcher_libevent
TEMPLATE = lib
DESTDIR  = ../lib
CONFIG  += staticlib create_prl precompile_header

HEADERS += \
	eventdispatcher_libevent.h \
	eventdispatcher_libevent_p.h \
	eventdispatcher_libevent_config.h \
	eventdispatcher_libevent_config_p.h \
	libevent2-emul.h \
	qt4compat.h \
	tco.h \
	tco_impl.h \
	common.h

SOURCES += \
	eventdispatcher_libevent.cpp \
	eventdispatcher_libevent_p.cpp \
	timers_p.cpp \
	socknot_p.cpp \
	eventdispatcher_libevent_config.cpp

PRECOMPILED_HEADER = common.h

headers.files = eventdispatcher_libevent.h eventdispatcher_libevent_config.h

unix {
	CONFIG += create_pc

	system('cc -E $$PWD/conftests/eventfd.h -o /dev/null 2> /dev/null') {
		SOURCES += tco_eventfd.cpp
	}
	else {
		SOURCES += tco_pipe.cpp
	}

	system('pkg-config --exists libevent') {
		CONFIG    += link_pkgconfig
		PKGCONFIG += libevent
	}
	else {
		system('cc -E $$PWD/conftests/libevent2.h -o /dev/null 2> /dev/null') {
			DEFINES += SJ_LIBEVENT_MAJOR=2
		}
		else:system('cc -E $$PWD/conftests/libevent1.h -o /dev/null 2> /dev/null') {
			DEFINES += SJ_LIBEVENT_MAJOR=1
		}
		else {
			warning("Assuming libevent 1.x")
			DEFINES += SJ_LIBEVENT_MAJOR=1
		}

		LIBS += -levent_core
	}

	target.path  = /usr/lib
	headers.path = /usr/include

	QMAKE_PKGCONFIG_NAME        = eventdispatcher_libevent
	QMAKE_PKGCONFIG_DESCRIPTION = "Libevent-based event dispatcher for Qt"
	QMAKE_PKGCONFIG_LIBDIR      = $$target.path
	QMAKE_PKGCONFIG_INCDIR      = $$headers.path
	QMAKE_PKGCONFIG_DESTDIR     = pkgconfig
}
else {
	LIBS        += -levent
	headers.path = $$DESTDIR
	target.path  = $$DESTDIR
}

win32 {
	SOURCES += tco_win32_libevent.cpp
	HEADERS += wsainit.h
	LIBS    += $$QMAKE_LIBS_NETWORK
	CONFIG  -= staticlib
	CONFIG  += dll
}

INSTALLS += target headers
