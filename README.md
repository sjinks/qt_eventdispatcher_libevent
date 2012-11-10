qt_eventdispatcher_libevent
===========================

An experimental libevent based event dispatcher for Qt

**Features**
* very fast :-)
* `eventfd()` can be used instead of `pipe()` for internal communication (define `HAVE_SYS_EVENTFD_H` to use this feature)
* compatibility with Qt4 and Qt 5
* does not use any private Qt headers

**Unsupported features**
* `QSocketNotifier::Exception` (libevent offers no support for this)
* `processEvents()` does not support any flags other than `QEventLoop::WaitForMoreEvents`
* undocumented `QCoreApplication::watchUnixSignal()` is not supported (because it is planned to be removed from Qt 5 and GLib dispatcher does not support it either)

**Usage (Qt 4):**

Simply include the header file and instantiate the dispatcher in `main()`
before creating the Qt application object.

```c++
#include "eventdispatcher_libevent.h"
    
int main(int argc, char** argv)
{
    EventDispatcherLibEvent dispatcher;
    QCoreApplication app(argc, argv);

    // ...

    return app.exec();
}
```

And add these lines to the .pro file:

```
CONFIG    += link_pkgconfig
PKGCONFIG += libevent
```

To use `eventfd()` instead of `pipe()` (Linux kernel 2.6.22 / glibc 2.8) add this line to the .pro file:

```
DEFINES += HAVE_SYS_EVENTFD_H
```

**Usage (Qt 5):**

Simply include the header file and instantiate the dispatcher in `main()`
before creating the Qt application object.

```c++
#include "eventdispatcher_libevent.h"
    
int main(int argc, char** argv)
{
    QCoreApplication::setEventDispatcher(new EventDispatcherLibEvent);
    QCoreApplication app(argc, argv);

    // ...

    return app.exec();
}
```

And add these lines to the .pro file:

```
CONFIG    += link_pkgconfig
PKGCONFIG += libevent
```

To use `eventfd()` instead of `pipe()` (Linux kernel 2.6.22 / glibc 2.8) add this line to the .pro file:

```
DEFINES += HAVE_SYS_EVENTFD_H
```

Qt 5 allows to specify a custom event dispatcher for a thread:

```c++
QThread* thread = new QThread;
thread->setEventDispatcher(new EventDispatcherLibEvent);
```
