import QtQuick 2.0
import Sailfish.Silica 1.0
import Nemo.Configuration 1.0
import "pages"
import "cover"

ApplicationWindow {
    id:                     app
    initialPage:            Component{
                                SetUp {
                                    config:     settings
                                    onCompose:  app.openCompose()
                                }
                            }
    cover:                  Component{
                                CoverPage {
                                    configured: settings.email !== ""
                                    onCompose:  app.openCompose()
                                }
                            }
    allowedOrientations:    defaultAllowedOrientations

    ConfigurationGroup {
        id:     settings
        path:   "/apps/harbour-selffish"

        property string email
        property string subject:    "Note to self"
    }

    function openCompose() {
        pageStack.pop(null, PageStackAction.Immediate)
        pageStack.push(Qt.resolvedUrl("pages/Compose.qml"),
                       {
                           recipient:   settings.email,
                           subject:     settings.subject
                       },
                       PageStackAction.Immediate)
        activate()
    }

    Component.onCompleted: {

    }
}
