TEMPLATE = subdirs
CONFIG  += ordered

SUBDIRS = src src-gui tests

src.file     = src/eventdispatcher_libevent.pro
tests.file   = tests/qt_eventdispatcher_tests/build.pro
src-gui.file = src-gui/eventdispatcher_libevent_qpa.pro
