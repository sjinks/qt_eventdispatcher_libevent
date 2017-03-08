import QtQuick 2.0
import QtQuick.Controls 1.2
import QtTest 1.1

Item {
    id: root

    visible: true
    focus: true
    width: 800
    height: 600

    property bool buttonClicked: false
    property bool testHung: false

    Button {
        id: button
        text: qsTr("Button 1")
        onClicked: buttonClicked = true
    }

    Timer {
        interval: 10000
        running: true
        onTriggered: testHung = true
    }

    TestCase {
        when: windowShown || testHung
        name: "guiTest"

        function test_click_button()
        {
            if (testHung) {
                fail("Test hung");
            }

            mouseClick(button);
            compare(buttonClicked, true);
        }
    }
}
