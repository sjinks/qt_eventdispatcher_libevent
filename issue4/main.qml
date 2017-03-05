import QtQuick 2.0
import QtQuick.Controls 1.2
import QtQuick.Layouts 1.1

ApplicationWindow {
    id: root

    visible: true
    width: 800
    height: 600
    title: qsTr("Issue #4")

    RowLayout {
        Button {
            text: qsTr("Button 1")
            onClicked: console.log(this.text)
        }
        Button {
            text: qsTr("Button 2")
            onClicked: console.log(this.text)
        }
        Button {
            text: qsTr("Button 3")
            onClicked: console.log(this.text)
        }
    }
}
