TEMPLATE = subdirs
CONFIG  += ordered

SUBDIRS = src tests

src.file   = src/eventdispatcher_libevent.pro
tests.file = tests/qt_eventdispatcher_tests/build.pro
