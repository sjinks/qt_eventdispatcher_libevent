#ifndef TCO_H
#define TCO_H

#include <QtCore/QObject>
#include <QtCore/QScopedPointer>
#include "qt4compat.h"

class ThreadCommunicationObjectPrivate;

class Q_DECL_HIDDEN ThreadCommunicationObject {
public:
	ThreadCommunicationObject(void);
	~ThreadCommunicationObject(void);

	bool valid(void) const;

	bool wakeUp(void);
	bool awaken(void);

	qintptr fd(void) const;
private:
	Q_DISABLE_COPY(ThreadCommunicationObject)
	Q_DECLARE_PRIVATE(ThreadCommunicationObject)

#if QT_VERSION >= 0x040600
	QScopedPointer<ThreadCommunicationObjectPrivate> d_ptr;
#else
	ThreadCommunicationObjectPrivate* d_ptr;
#endif

};

#endif // TCO_H
