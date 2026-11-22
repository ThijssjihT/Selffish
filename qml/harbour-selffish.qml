import QtQuick 2.0
import Sailfish.Silica 1.0
import Nemo.Configuration 1.0
import harbour.selffish 1.0
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

    property string sendState:  "idle"
    property string draftSubject
    property string draftBody
    property string sendError

    ConfigurationGroup {
        id:     settings
        path:   "/apps/harbour-selffish"

        property string email
        property string subject:        "Note to self"

        property string smtpHost
        property int    smtpPort:       465
        property int    smtpSecurity:   0
        property string smtpUser
        property string smtpPassword
    }

    SmtpClient {
        id:         smtp
        host:       settings.smtpHost
        port:       settings.smtpPort
        security:   settings.smtpSecurity
        username:   settings.smtpUser
        password:   settings.smtpPassword
    }

    Connections {
        target: smtp
        onSent: {
            app.draftSubject = ""
            app.draftBody = ""
            app.sendError = ""
            app.sendState = "sent"
            resetTimer.restart()
        }
        onFailed: {
            app.sendError = error
            app.sendState = "failed"
        }
    }

    Timer {
        id:         resetTimer
        interval:   8000
        onTriggered:if (app.sendState === "sent") app.sendState = "idle"
    }

    function openCompose() {
        activate()
        if (sendState === "sending") return
        if (pageStack.currentPage && pageStack.currentPage.objectName == "compose") return

        var dlg = pageStack.push(Qt.resolvedUrl("pages/Compose.qml"), {
           mailer:      smtp,
           recipient:   settings.email,
           subject:     settings.subject
        }, PageStackAction.Immediate)

        dlg.accepted.connect(function() { app.send(dlg.subjectText, dlg.bodyText) })
        dlg.rejected.connect(function() { app.discard() })
    }

    function send(subject, body) {
        draftSubject = subject
        draftBody = body
        sendError = ""
        sendState = "sending"
        smtp.send(settings.email, subject, body)
        deactivate()                            // back to the home screen
    }

    function discard() {
        draftSubject = ""
        draftBody = ""
        sendError = ""
        if (sendState === "failed") sendState = "idle"
        deactivate()
    }

    Component.onCompleted: {

    }
}
