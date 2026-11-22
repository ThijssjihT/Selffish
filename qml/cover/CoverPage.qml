import QtQuick 2.0
import Sailfish.Silica 1.0

CoverBackground {
    property bool   configured: false
    property string status:     "idle"
    signal compose()

    Label {
        id:                         header
        anchors.top:                parent.top
        anchors.topMargin:          Theme.paddingLarge
        anchors.horizontalCenter:   parent.horizontalCenter
        text:                       qsTr("Selffish")
        font.pixelSize:             Theme.fontSizeSmall
        font.bold:                  true
        color:                      Theme.highlightColor
    }

    Label {
        anchors.centerIn:               parent
        anchors.verticalCenterOffset:   Theme.paddingLarge
        width:                          parent.width - 2 * Theme.paddingLarge
        horizontalAlignment:            Text.AlignHCenter
        wrapMode:                       Text.Wrap
        color:                          Theme.primaryColor
        font.pixelSize:                 Theme.fontSizeLarge
        text:                           !configured             ? qsTr("Open the app to set up Selffish")
                                      : status === "sending"    ? qsTr("Sending…")
                                      : status === "sent"       ? qsTr("Message sent")
                                      : status === "failed"     ? qsTr("Could not send. Open to retry")
                                      :                           qsTr("Compose a new message")
    }

    CoverActionList {
        enabled:    configured && status !== "sending"
        CoverAction {
            iconSource:     "image://theme/icon-cover-message"
            onTriggered:    compose()
        }
    }
}
