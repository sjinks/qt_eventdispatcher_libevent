SOURCES     += $$PWD/../eventdispatcher_libevent.cpp
HEADERS     += $$PWD/../eventdispatcher_libevent.h
INCLUDEPATH += $$PWD/../
DEPENDPATH  += $$PWD/../
CONFIG      += link_pkgconfig
PKGCONFIG   += libevent libevent_pthreads
