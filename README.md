qt_eventdispatcher_libevent
===========================

An experimental libevent based event dispatcher for Qt

**Usage (Qt 4):**

Simply include the header file and instantiate the dispatcher in `main()`
before creating the Qt application object.

    #include "eventdispatcher_libevent.h"
    
    int main(int argc, char** argv)
    {
        EventDispatcherLibEvent dispatcher;
        QCoreApplication app(argc, argv);
        
        // ...
        
        return app.exec();
    }

And add these lines to the .pro file:

    CONFIG    += link_pkgconfig
    PKGCONFIG += libevent


**Usage (Qt 5):**

Simply include the header file and instantiate the dispatcher in `main()`
before creating the Qt application object.

    #include "eventdispatcher_libevent.h"
    
    int main(int argc, char** argv)
    {
        QCoreApplication::setEventDispatcher(new EventDispatcherLibEvent);
        QCoreApplication app(argc, argv);
        
        // ...
        
        return app.exec();
    }

And add these lines to the .pro file:

    CONFIG    += link_pkgconfig
    PKGCONFIG += libevent

Qt 5 allows to specify a custom event dispatcher for a thread:

    QThread* thread = new QThread;
    thread->setEventDispatcher(new EventDispatcherLibEvent);

