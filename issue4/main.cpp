#include <QtGui/QGuiApplication>
#include <QtQml/QQmlApplicationEngine>
#include "../src-gui/eventdispatcher_libevent_qpa.h"

int main(int argc, char *argv[])
{
	QCoreApplication::setEventDispatcher(new EventDispatcherLibEventQPA());
	QGuiApplication app(argc, argv);
	QQmlApplicationEngine engine;
	engine.load("main.qml");
	return app.exec();
}
