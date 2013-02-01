LIBS        += -L$$OUT_PWD/$$DESTDIR/../lib -leventdispatcher_libevent
INCLUDEPATH += $$PWD/../src
DEPENDPATH  += $$PWD/../src

unix:PRE_TARGETDEPS += $$DESTDIR/../lib/libeventdispatcher_libevent.a

CONFIG += console link_prl
CONFIG -= app_bundle
