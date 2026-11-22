import Sailfish.Silica 1.0
import QtQuick 2.0

Dialog {
    id:         compose
    objectName: "compose"

    property string recipient
    property string errorText
    property alias  subjectText:    subjectField.text
    property alias  bodyText:       bodyArea.text

    canAccept: bodyArea.text.trim() !== ""

    SilicaFlickable {
        anchors.fill:   parent
        contentHeight:  column.height + Theme.paddingLarge

        Column {
            id:     column
            width:  parent.width

            DialogHeader {
                title:      compose.recipient
                acceptText: qsTr("Send")
                cancelText: qsTr("Discard")
            }

            Label {
                visible:        compose.errorText !== ""
                x:              Theme.horizontalPageMargin
                width:          parent.width - 2 * Theme.horizontalPageMargin
                wrapMode:       Text.Wrap
                font.pixelSize: Theme.fontSizeSmall
                color:          Theme.highlightColor
                text:           qsTr("Could not send: %1").arg(compose.errorText)
            }

            TextField {
                id:                 subjectField
                width:              parent.width
                label:              qsTr("Subject")
                placeholderText:    qsTr("Subject")

                EnterKey.iconSource:"image://theme/icon-m-enter-next"
                EnterKey.onClicked: bodyArea.forceActiveFocus()
            }

            TextArea {
                id:                 bodyArea
                width:              parent.width
                label:              qsTr("Message")
                placeholderText:    qsTr("Write your note…")
                focus:              true
            }
        }
    }

}
