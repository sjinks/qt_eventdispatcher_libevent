#include <QtQuickTest/quicktest.h>
#include "../src-gui/eventdispatcher_libevent_qpa.h"

int main(int argc, char** argv)
{
	QCoreApplication::setEventDispatcher(new EventDispatcherLibEventQPA());
	QTEST_ADD_GPU_BLACKLIST_SUPPORT
	QTEST_SET_MAIN_SOURCE_PATH
	return quick_test_main(argc, argv, "issue4", 0);
}
