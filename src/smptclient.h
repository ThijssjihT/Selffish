#ifndef SMTPCLIENT_H
#define SMTPCLIENT_H

#include <QObject>
#include <QString>
#include <QStringList>
#include <QByteArray>
#include <QTimer>

class QSslSocket;
class QSslError;

// Minimal SMTP submission client.
//
//  - Implicit TLS (port 465) or STARTTLS (port 587). Plain-text SMTP is
//    deliberately not supported, because credentials would cross the network
//    unprotected. Certificates are always verified.
//  - AUTH PLAIN, falling back to AUTH LOGIN.
//  - Sends one text/plain UTF-8 message per send() call.
//
// Written against Qt 5.6 (Sailfish OS) and C++11.
class SmtpClient : public QObject
{
    Q_OBJECT

public:
    enum Security { Ssl, StartTls };
    Q_ENUM(Security)

    Q_PROPERTY(QString host         MEMBER m_host        NOTIFY configChanged)
    Q_PROPERTY(int port             MEMBER m_port        NOTIFY configChanged)
    Q_PROPERTY(Security security    MEMBER m_security    NOTIFY configChanged)
    Q_PROPERTY(QString username     MEMBER m_username    NOTIFY configChanged)
    Q_PROPERTY(QString password     MEMBER m_password    NOTIFY configChanged)
    Q_PROPERTY(QString fromAddress  MEMBER m_fromAddress NOTIFY configChanged)
    Q_PROPERTY(bool busy            READ busy            NOTIFY busyChanged)

    explicit SmtpClient(QObject *parent = 0);
    ~SmtpClient();

    bool busy() const { return m_busy; }

    // Asynchronous. Emits sent() or failed(error) exactly once.
    // fromAddress defaults to username when empty.
    Q_INVOKABLE void send(const QString &recipient,
                          const QString &subject,
                          const QString &body);

signals:
    void sent();
    void failed(const QString &error);
    void configChanged();
    void busyChanged();

private slots:
    void onReadyRead();
    void onEncrypted();
    void onSocketError();
    void onSslErrors(const QList<QSslError> &errors);
    void onDisconnected();
    void onTimeout();

private:
    enum State {
        Idle,
        Greeting,
        Ehlo,
        StartTlsCmd,
        TlsHandshake,
        AuthPlain,
        AuthLoginUser,
        AuthLoginPass,
        AuthLoginDone,
        MailFrom,
        RcptTo,
        Data,
        DataDone
    };

    void handleReply(int code, const QStringList &lines);
    void parseCapabilities(const QStringList &lines);
    void authenticate();
    void startEnvelope();
    void sendCommand(const QByteArray &command, bool redact = false);
    void fail(const QString &message);
    void failReply(const QString &context, int code, const QStringList &lines);
    void succeed();
    void teardown();
    void setBusy(bool busy);

    static QByteArray buildMessage(const QString &from, const QString &to,
                                   const QString &subject, const QString &body);

    // configuration
    QString m_host;
    int m_port;
    Security m_security;
    QString m_username;
    QString m_password;
    QString m_fromAddress;

    // per-send state
    bool m_busy;
    State m_state;
    QSslSocket *m_socket;
    QTimer m_timer;
    QByteArray m_buffer;
    QStringList m_replyLines;
    QStringList m_authMechanisms;
    bool m_hasStartTls;
    bool m_tlsDone;
    QString m_envelopeFrom;
    QString m_envelopeTo;
    QByteArray m_message;
    bool m_debug;
};

#endif // SMTPCLIENT_H
