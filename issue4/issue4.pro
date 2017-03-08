TEMPLATE = app
QT      += qml quick
CONFIG  += qmltestcase
SOURCES += main.cpp
TARGET   = tst_issue4

INCLUDEPATH += ../src

HEADERS += ../src/eventdispatcher_libevent.h ../src-gui/eventdispatcher_libevent_qpa.h
LIBS    += ../lib/libeventdispatcher_libevent_qpa.a ../lib/libeventdispatcher_libevent.a
