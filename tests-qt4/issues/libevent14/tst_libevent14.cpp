#include <QtCore/QCoreApplication>
#include <QtCore/QThread>
#include <QtCore/QTimer>
#include <QtTest/QTest>
#include "eventdispatcher_libevent.h"

class LibEventIssue14Test : public QObject {
	Q_OBJECT

public:
	LibEventIssue14Test(void)
		: m_success(false), m_loop()
	{
	}

private:
	bool m_success;
	QEventLoop m_loop;

protected Q_SLOTS:
	void testSignalHandler(void)
	{
		this->m_success = true;
		this->m_loop.quit();
	}

private Q_SLOTS:
	void testCase1(void);
};

class MyThread : public QThread {
	Q_OBJECT
public:
	explicit MyThread(QObject* parent = 0) : QThread(parent) {}

	virtual void run(void)
	{
		QTimer::singleShot(1000, this, SIGNAL(testSignal()));
		this->exec();
	}

Q_SIGNALS:
	void testSignal(void);
};

void LibEventIssue14Test::testCase1(void)
{
	while (this->m_loop.processEvents(QEventLoop::AllEvents)) {
		;
	}

	this->m_loop.processEvents(QEventLoop::AllEvents);
	QVERIFY(!this->m_loop.processEvents(QEventLoop::AllEvents));

	this->m_success = false;

	MyThread thread;
	QObject::connect(&thread, SIGNAL(testSignal()), this, SLOT(testSignalHandler()), Qt::QueuedConnection);
	thread.start();

	QTimer::singleShot(10000, &this->m_loop, SLOT(quit()));
	this->m_loop.exec();
	thread.quit();
	thread.wait();
	QVERIFY(this->m_success);
}

int main(int argc, char** argv)
{
#if QT_VERSION < 0x050000
	EventDispatcherLibEvent d;
#else
	QCoreApplication::setEventDispatcher(new EventDispatcherLibEvent);
#endif
	QCoreApplication app(argc, argv);
	LibEventIssue14Test t;
	return QTest::qExec(&t, argc, argv);
}

#include "tst_libevent14.moc"
