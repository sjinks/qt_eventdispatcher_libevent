CONFIG  += testcase
TARGET   = tst_epoll3
QT       = core network testlib
SOURCES += tst_epoll3.cpp
DEFINES += QT_DISABLE_DEPRECATED_BEFORE=0
DESTDIR  = ../..

include(../../common.pri)

win32: LIBS += $$QMAKE_LIBS_NETWORK
