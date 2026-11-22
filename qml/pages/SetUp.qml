import Sailfish.Silica 1.0
import QtQuick 2.0

Page {
    id: setUp

    property var config
    signal compose()

    SilicaFlickable {
        anchors.fill:   parent
        contentHeight:  column.height + Theme.paddingLarge

        Column {
            id:         column
            width:      parent.width
            spacing:    Theme.paddingLarge

            PageHeader {
                title:      qsTr("Selffish")
                description:qsTr("Set up")
            }

            TextField {
                id:                 emailField
                width:              parent.width
                label:              qsTr("E-mail address")
                placeholderText:    qsTr("Where should your notes go?")
                text:               setUp.config.email
                inputMethodHints:   Qt.ImhEmailCharactersOnly | Qt.ImhNoPredictiveText
                validator:          RegExpValidator { regExp: /^[^@\s]+@[^@\s]+\.[^@\s]+$/ }
                errorHighlight:     text !== "" && !acceptableInput

                EnterKey.iconSource:"image://theme/icon-m-enter-next"
                EnterKey.onClicked: subjectField.focus = true

                onTextChanged: {
                    if (acceptableInput)
                        setUp.config.email = text
                    else if (text === "")
                        setUp.config.email = ""
                }
            }

            TextField {
                id:                 subjectField
                width:              parent.width
                label:              qsTr("Subject")
                placeholderText:    qsTr("Subject")
                text:               setUp.config.subject

                EnterKey.iconSource:"image://theme/icon-m-enter-close"
                EnterKey.onClicked: focus = false

                onTextChanged:      setUp.config.subject = text
            }

            Button {
                anchors.horizontalCenter:   parent.horizontalCenter
                visible:                    setUp.config.email !== ""
                text:                       qsTr("New message")
                onClicked:                  setUp.compose()
            }
        }
    }
}
