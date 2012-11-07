qt_eventdispatcher_libevent
===========================

An experimental libevent based event dispatcher for Qt

**Usage:**

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

