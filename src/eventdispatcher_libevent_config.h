#ifndef EVENTDISPATCHER_LIBEVENT_CONFIG_H
#define EVENTDISPATCHER_LIBEVENT_CONFIG_H

#include <QtCore/QObject>
#include <QtCore/QExplicitlySharedDataPointer>

class EventDispatcherLibEventConfigPrivate;

class EventDispatcherLibEventConfig {
	Q_GADGET
	Q_FLAGS(Feature Features)
	Q_FLAGS(Config Configuration)
public:
	EventDispatcherLibEventConfig(void);
	~EventDispatcherLibEventConfig(void);

	enum Feature {
		ev_ET  = 0x01,
		ev_O1  = 0x02,
		ev_FDs = 0x04
	};

	enum Config {
		cfg_NoLock            = 0x01,
		cfg_IgnoreEnvironment = 0x02,
		cfg_StartupIOCP       = 0x04,
		cfg_NoCacheTime       = 0x08,
		cfg_EPollChangelist   = 0x10
	};

	Q_DECLARE_FLAGS(Features, Feature)
	Q_DECLARE_FLAGS(Configuration, Config)

	bool avoidMethod(const QLatin1String& method);
	bool requireFeatures(Features f);
	bool setConfiguration(Configuration cfg);

private:
	Q_DECLARE_PRIVATE(EventDispatcherLibEventConfig)
	QExplicitlySharedDataPointer<EventDispatcherLibEventConfigPrivate> d_ptr;

	friend class EventDispatcherLibEventPrivate;
};

Q_DECLARE_OPERATORS_FOR_FLAGS(EventDispatcherLibEventConfig::Features)
Q_DECLARE_OPERATORS_FOR_FLAGS(EventDispatcherLibEventConfig::Configuration)

#endif // EVENTDISPATCHER_LIBEVENT_CONFIG_H
