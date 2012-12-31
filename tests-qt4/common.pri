LIBS           += -L$$PWD/../lib -leventdispatcher_epoll
INCLUDEPATH    += $$PWD/../src
DEPENDPATH     += $$PWD/../src
PRE_TARGETDEPS += $$PWD/../lib/libeventdispatcher_epoll.a
