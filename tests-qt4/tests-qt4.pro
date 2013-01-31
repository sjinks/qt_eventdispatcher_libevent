TEMPLATE = subdirs

SUBDIRS = \
	qeventdispatcher \
	qeventloop \
	qtimer \
	issues

# Benchmarks appeared in Qt 4.6
greaterThan(QT_MINOR_VERSION, 5) {
	SUBDIRS += events
}

# qsocketnotifier depends on Qt 4.8's privates
greaterThan(QT_MINOR_VERSION, 7) {
	SUBDIRS += qsocketnotifier
}
