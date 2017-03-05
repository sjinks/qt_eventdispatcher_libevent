TEMPLATE = subdirs
CONFIG  += ordered
SUBDIRS  = src tests

greaterThan(QT_MAJOR_VERSION, 4) {
	SUBDIRS     += src-gui
	src-gui.file = src-gui/eventdispatcher_libevent_qpa.pro
}

src.file   = src/eventdispatcher_libevent.pro
tests.file = tests/qt_eventdispatcher_tests/build.pro
