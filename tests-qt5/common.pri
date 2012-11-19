LIBS           += -L$$PWD/../lib -leventdispatcher_libevent
INCLUDEPATH    += $$PWD/../lib
DEPENDPATH     += $$PWD/../lib
PRE_TARGETDEPS += $$PWD/../lib/libeventdispatcher_libevent.a
