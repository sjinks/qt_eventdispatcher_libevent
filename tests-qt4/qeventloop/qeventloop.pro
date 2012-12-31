CONFIG += testcase
TARGET  = tst_qeventloop
QT      = core network testlib
HEADERS = util.h
SOURCES = tst_qeventloop.cpp
DESTDIR = ../

include(../common.pri)
