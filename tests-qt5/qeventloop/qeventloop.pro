CONFIG += testcase
TARGET  = tst_qeventloop
QT      = core network testlib core-private
SOURCES = tst_qeventloop.cpp
DESTDIR = ..

include(../common.pri)
