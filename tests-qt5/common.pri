LIBS           += -L$$PWD/../lib -leventdispatcher_libevent
INCLUDEPATH    += $$PWD/../src
DEPENDPATH     += $$PWD/../src
PRE_TARGETDEPS += $$PWD/../lib/libeventdispatcher_libevent.a
