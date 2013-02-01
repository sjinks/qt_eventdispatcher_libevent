#include <QtCore/QtGlobal>

#ifdef Q_OS_WIN
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#endif

#include <qplatformdefs.h>
#include <QtCore/QCoreApplication>
#include <QtCore/QEventLoop>
#include <QtCore/QSocketNotifier>
#include <QtCore/QTimer>
#include <QtNetwork/QTcpServer>
#include <QtNetwork/QTcpSocket>
#include <QtTest/QtTest>
#include "eventdispatcher_libevent.h"

class tst_Epoll3 : public QObject {
	Q_OBJECT
public:
	explicit tst_Epoll3(QObject* parent = 0) : QObject(parent) {}

private Q_SLOTS:
	void issue3(void);
};

class Issue3Tester : public QObject {
	Q_OBJECT
public:
	explicit Issue3Tester(QObject* parent = 0)
		: QObject(parent), m_server(new QTcpServer(this)), m_sn1(0), m_sn2(0), m_in1(false), m_in2(false)
	{
		this->m_server->listen(QHostAddress::LocalHost);
		QObject::connect(this->m_server, SIGNAL(newConnection()), this, SLOT(handleNewConnection()));

		qintptr s1 = ::socket(AF_INET, SOCK_STREAM, 0);

		this->m_sn1 = new QSocketNotifier(s1, QSocketNotifier::Write, this);
		this->m_sn2 = new QSocketNotifier(s1, QSocketNotifier::Read, this);

		QObject::connect(this->m_sn1, SIGNAL(activated(int)), this, SLOT(activated1(int)));
		QObject::connect(this->m_sn2, SIGNAL(activated(int)), this, SLOT(activated2(int)));

		struct sockaddr_in sa;
		memset(&sa, 0, sizeof(sa));

		sa.sin_family      = AF_INET;
		sa.sin_port        = htons(this->m_server->serverPort());
		sa.sin_addr.s_addr = htonl(this->m_server->serverAddress().toIPv4Address());

		::connect(s1, reinterpret_cast<struct sockaddr*>(&sa), sizeof(sa));

		this->m_sn1->setEnabled(false);
		this->m_sn2->setEnabled(false);
		QCoreApplication::processEvents();
		this->m_sn1->setEnabled(true);
		this->m_sn2->setEnabled(true);
		QCoreApplication::processEvents();
	}

private:
	QTcpServer* m_server;
	QSocketNotifier* m_sn1;
	QSocketNotifier* m_sn2;
	bool m_in1;
	bool m_in2;

private Q_SLOTS:
	void handleNewConnection(void)
	{
		QTcpServer* server = qobject_cast<QTcpServer*>(this->sender());
		QTcpSocket* sock = server->nextPendingConnection();
		sock->write("Hello!");
		sock->flush(); // Very important!
	}

	void activated1(int) // write
	{
		this->m_sn1->setEnabled(false);
		delete this->m_sn2;
	}

	void activated2(int) // read
	{
		this->m_sn2->setEnabled(false);
		delete this->m_sn1;
	}
};

void tst_Epoll3::issue3(void)
{
	Issue3Tester t;
	QEventLoop l;
	QTimer::singleShot(3000, &l, SLOT(quit()));
	l.exec();
}

#include "tst_epoll3.moc"

int main(int argc, char** argv)
{
#if QT_VERSION < 0x050000
	EventDispatcherLibEvent d;
#else
	QCoreApplication::setEventDispatcher(new EventDispatcherLibEvent);
#endif
	QCoreApplication app(argc, argv);
	tst_Epoll3 t;
	return QTest::qExec(&t, argc, argv);
}
