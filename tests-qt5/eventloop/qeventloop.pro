CONFIG    += testcase
TARGET     = tst_qeventloop
QT         = core network testlib core-private
SOURCES   += tst_qeventloop.cpp
HEADERS   += eventdispatcher_libevent.h

DESTDIR    = ../
include(../common.pri)
