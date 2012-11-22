HEADERS += $$PWD/eventdispatcher_libevent.h

unix {
	CONFIG    += link_pkgconfig
	PKGCONFIG += eventdispatcher_libevent
}
else:win32 {
	LIBS        += -L$$PWD -leventdispatcher_libevent
	INCLUDEPATH += $$PWD
	DEPENDPATH  += $$PWD
}
