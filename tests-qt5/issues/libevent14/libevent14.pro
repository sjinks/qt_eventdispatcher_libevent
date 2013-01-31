QT       = core testlib
TEMPLATE = app
TARGET   = tst_libevent14
CONFIG  += console
CONFIG  -= app_bundle
SOURCES += tst_libevent14.cpp
DESTDIR  = ../../

include(../../common.pri)
