CONFIG  += testcase
TARGET   = tst_qsocketnotifier
QT       = core-private network-private testlib network
SOURCES  = tst_qsocketnotifier.cpp qabstractsocketengine.cpp qnativesocketengine.cpp qnativesocketengine_unix.cpp
HEADERS  = qabstractsocketengine_p.h qnativesocketengine_p.h
DEFINES += QT_DISABLE_DEPRECATED_BEFORE=0

QNETWORK_SRC = $$QT_SOURCE_TREE/src/network
INCLUDEPATH += $$QNETWORK_SRC

DESTDIR  = ../
include(../common.pri)
