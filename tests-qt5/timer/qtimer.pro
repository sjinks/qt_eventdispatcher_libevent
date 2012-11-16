CONFIG  += testcase
TARGET   = tst_qtimer
QT       = core testlib
SOURCES  = tst_qtimer.cpp
DEFINES += QT_DISABLE_DEPRECATED_BEFORE=0

DESTDIR  = ../
include(../common.pri)
