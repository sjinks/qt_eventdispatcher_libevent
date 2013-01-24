QT      += gui-private
CONFIG  += staticlib create_prl link_prl
TEMPLATE = lib
TARGET   = eventdispatcher_libevent_qpa
SOURCES  = eventdispatcher_libevent_qpa.cpp
HEADERS  = eventdispatcher_libevent_qpa.h
DESTDIR  = ../lib

LIBS           += -L$$PWD/../lib -leventdispatcher_libevent
INCLUDEPATH    += $$PWD/../src
DEPENDPATH     += $$PWD/../src
PRE_TARGETDEPS += $$PWD/../lib/libeventdispatcher_libevent.a
