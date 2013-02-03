INCLUDEPATH += $$PWD $$PWD/../src
DEPENDPATH  += $$PWD $$PWD/../src

HEADERS += $$PWD/eventdispatcher.h

CONFIG  *= link_prl
LIBS    += -L$$OUT_PWD/$$DESTDIR/../lib -leventdispatcher_libevent

unix|*-g++* {
	PRE_TARGETDEPS *= $$OUT_PWD/$$DESTDIR/../lib/$${QMAKE_PREFIX_STATICLIB}eventdispatcher_libevent$${LIB_SUFFIX}.$${QMAKE_EXTENSION_STATICLIB}
}
else:win32 {
	PRE_TARGETDEPS *= $$OUT_PWD/$$DESTDIR/../lib/eventdispatcher_libevent$${LIB_SUFFIX}.lib
}
