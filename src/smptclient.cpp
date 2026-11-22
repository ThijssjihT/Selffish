#include "smtpclient.h"

#include <QSslSocket>
#include <QSslError>
#include <QDateTime>
#include <QLocale>
#include <QUuid>
#include <QRegularExpression>
#include <QDebug>

namespace {

const int kTimeoutMs = 30000;

// Deliberately strict: no whitespace, quotes, angle brackets, commas etc.
// This also rules out CR/LF injection into the SMTP dialogue.
bool validAddress(const QString &a)
{
    static const QRegularExpression re(QStringLiteral(
        "^[^\\s<>\"\\\\,;:()@]+@[^\\s<>\"\\\\,;:()@]+\\.[^\\s<>\"\\\\,;:()@]+$"));
    return re.match(a).hasMatch();
}

QByteArray wrapBase64(const QByteArray &raw)
{
    const QByteArray enc = raw.toBase64();
    QByteArray out;
    for (int i = 0; i < enc.size(); i += 76) {
        out += enc.mid(i, 76);
        out += "\r\n";
    }
    return out;
}

// RFC 2047 encoded-words, split so that each word stays under 75 characters
// and never cuts a UTF-8 sequence in half.
QByteArray encodeHeaderText(const QString &input)
{
    QString s = input;
    s.replace(QLatin1Char('\r'), QLatin1Char(' '));
    s.replace(QLatin1Char('\n'), QLatin1Char(' '));
    s.replace(QLatin1Char('\t'), QLatin1Char(' '));

    bool plain = s.size() <= 70;
    for (int i = 0; plain && i < s.size(); ++i) {
        const ushort c = s.at(i).unicode();
        if (c < 32 || c > 126)
            plain = false;
    }
    if (plain)
        return s.toLatin1();

    const int maxChunkBytes = 45;   // 45 bytes -> 60 base64 chars, + 12 framing = 72
    QList<QByteArray> words;
    QByteArray chunk;
    for (int i = 0; i < s.size(); ) {
        const int n = (s.at(i).isHighSurrogate() && i + 1 < s.size()) ? 2 : 1;
        const QByteArray piece = s.mid(i, n).toUtf8();
        if (!chunk.isEmpty() && chunk.size() + piece.size() > maxChunkBytes) {
            words << "=?UTF-8?B?" + chunk.toBase64() + "?=";
            chunk.clear();
        }
        chunk += piece;
        i += n;
    }
    if (!chunk.isEmpty())
        words << "=?UTF-8?B?" + chunk.toBase64() + "?=";

    QByteArray out;
    for (int i = 0; i < words.size(); ++i) {
        if (i > 0)
            out += "\r\n ";          // header folding
        out += words.at(i);
    }
    return out;
}

} // namespace

SmtpClient::SmtpClient(QObject *parent)
    : QObject(parent)
    , m_port(465)
    , m_security(Ssl)
    , m_busy(false)
    , m_state(Idle)
    , m_socket(0)
    , m_hasStartTls(false)
    , m_tlsDone(false)
    , m_debug(qgetenv("SELFFISH_SMTP_DEBUG") == "1")
{
    m_timer.setSingleShot(true);
    m_timer.setInterval(kTimeoutMs);
    connect(&m_timer, SIGNAL(timeout()), this, SLOT(onTimeout()));
}

SmtpClient::~SmtpClient()
{
    teardown();
}

void SmtpClient::setBusy(bool busy)
{
    if (m_busy == busy)
        return;
    m_busy = busy;
    emit busyChanged();
}

QByteArray SmtpClient::buildMessage(const QString &from, const QString &to,
                                    const QString &subject, const QString &body)
{
    const QString domain = from.mid(from.indexOf(QLatin1Char('@')) + 1);
    QString id = QUuid::createUuid().toString();
    id.remove(QLatin1Char('{')).remove(QLatin1Char('}')).remove(QLatin1Char('-'));

    QByteArray msg;
    msg += "From: <" + from.toLatin1() + ">\r\n";
    msg += "To: <" + to.toLatin1() + ">\r\n";
    msg += "Subject: " + encodeHeaderText(subject) + "\r\n";
    msg += "Date: " + QLocale::c().toString(QDateTime::currentDateTimeUtc(),
                          QStringLiteral("ddd, dd MMM yyyy HH:mm:ss")).toLatin1()
           + " +0000\r\n";
    msg += "Message-ID: <" + id.toLatin1() + "@" + domain.toLatin1() + ">\r\n";
    msg += "MIME-Version: 1.0\r\n";
    msg += "Content-Type: text/plain; charset=UTF-8\r\n";
    msg += "Content-Transfer-Encoding: base64\r\n";
    msg += "\r\n";

    // Canonical CRLF line endings inside the text, then base64. Because the body
    // is base64 no line can start with '.', so SMTP dot-stuffing is not needed.
    QString text = body;
    text.replace(QLatin1String("\r\n"), QLatin1String("\n"));
    text.replace(QLatin1Char('\r'), QLatin1Char('\n'));
    text.replace(QLatin1Char('\n'), QLatin1String("\r\n"));
    msg += wrapBase64(text.toUtf8());
    return msg;
}

void SmtpClient::send(const QString &recipient, const QString &subject, const QString &body)
{
    if (m_busy) {
        emit failed(tr("A message is already being sent"));
        return;
    }
    const QString from = m_fromAddress.isEmpty() ? m_username : m_fromAddress;

    if (m_host.isEmpty() || m_port <= 0 || m_port > 65535) {
        emit failed(tr("SMTP server is not configured"));
        return;
    }
    if (!validAddress(recipient)) {
        emit failed(tr("Invalid recipient address"));
        return;
    }
    if (!validAddress(from)) {
        emit failed(tr("Invalid sender address"));
        return;
    }

    m_envelopeFrom = from;
    m_envelopeTo = recipient;
    m_message = buildMessage(from, recipient, subject, body);
    m_buffer.clear();
    m_replyLines.clear();
    m_authMechanisms.clear();
    m_hasStartTls = false;
    m_tlsDone = (m_security == Ssl);

    setBusy(true);
    m_state = Greeting;

    m_socket = new QSslSocket(this);
    connect(m_socket, SIGNAL(readyRead()), this, SLOT(onReadyRead()));
    connect(m_socket, SIGNAL(encrypted()), this, SLOT(onEncrypted()));
    connect(m_socket, SIGNAL(error(QAbstractSocket::SocketError)), this, SLOT(onSocketError()));
    connect(m_socket, SIGNAL(sslErrors(QList<QSslError>)), this, SLOT(onSslErrors(QList<QSslError>)));
    connect(m_socket, SIGNAL(disconnected()), this, SLOT(onDisconnected()));

    m_timer.start();
    if (m_security == Ssl)
        m_socket->connectToHostEncrypted(m_host, m_port);
    else
        m_socket->connectToHost(m_host, m_port);
}

void SmtpClient::sendCommand(const QByteArray &command, bool redact)
{
    if (m_debug)
        qDebug("SMTP C: %s", redact ? "<redacted>" : command.constData());
    m_socket->write(command + "\r\n");
    m_timer.start();
}

void SmtpClient::onReadyRead()
{
    if (!m_socket)
        return;
    m_buffer += m_socket->readAll();

    int idx;
    while (m_state != Idle && (idx = m_buffer.indexOf('\n')) >= 0) {
        QByteArray line = m_buffer.left(idx);
        m_buffer.remove(0, idx + 1);
        if (line.endsWith('\r'))
            line.chop(1);
        if (m_debug)
            qDebug("SMTP S: %s", line.constData());

        bool ok = false;
        const int code = line.left(3).toInt(&ok);
        if (line.size() < 3 || !ok) {
            fail(tr("Unexpected reply from server"));
            return;
        }
        const bool more = line.size() > 3 && line.at(3) == '-';
        m_replyLines << QString::fromUtf8(line.mid(4));
        m_timer.start();

        if (!more) {
            const QStringList lines = m_replyLines;
            m_replyLines.clear();
            handleReply(code, lines);
        }
    }
}

void SmtpClient::handleReply(int code, const QStringList &lines)
{
    switch (m_state) {
    case Greeting:
        if (code != 220) { failReply(tr("Server refused the connection"), code, lines); return; }
        m_state = Ehlo;
        sendCommand("EHLO localhost");
        break;

    case Ehlo:
        if (code != 250) { failReply(tr("EHLO rejected"), code, lines); return; }
        parseCapabilities(lines);
        if (m_security == StartTls && !m_tlsDone) {
            if (!m_hasStartTls) {
                fail(tr("Server does not offer STARTTLS; refusing to send the password unencrypted"));
                return;
            }
            m_state = StartTlsCmd;
            sendCommand("STARTTLS");
        } else {
            authenticate();
        }
        break;

    case StartTlsCmd:
        if (code != 220) { failReply(tr("STARTTLS rejected"), code, lines); return; }
        if (!m_buffer.isEmpty()) {
            // Anything the server sent before the handshake could be injected
            // plaintext (STARTTLS command injection), so refuse it.
            fail(tr("Unexpected data before TLS handshake"));
            return;
        }
        m_state = TlsHandshake;
        m_socket->setPeerVerifyName(m_host);
        m_socket->startClientEncryption();
        break;

    case TlsHandshake:
        fail(tr("Unexpected reply during TLS handshake"));
        break;

    case AuthPlain:
        if (code != 235) { failReply(tr("Login failed"), code, lines); return; }
        startEnvelope();
        break;

    case AuthLoginUser:
        if (code != 334) { failReply(tr("Login failed"), code, lines); return; }
        m_state = AuthLoginPass;
        sendCommand(m_username.toUtf8().toBase64());
        break;

    case AuthLoginPass:
        if (code != 334) { failReply(tr("Login failed"), code, lines); return; }
        m_state = AuthLoginDone;
        sendCommand(m_password.toUtf8().toBase64(), true);
        break;

    case AuthLoginDone:
        if (code != 235) { failReply(tr("Login failed"), code, lines); return; }
        startEnvelope();
        break;

    case MailFrom:
        if (code != 250) { failReply(tr("Sender rejected"), code, lines); return; }
        m_state = RcptTo;
        sendCommand("RCPT TO:<" + m_envelopeTo.toLatin1() + ">");
        break;

    case RcptTo:
        if (code != 250 && code != 251) { failReply(tr("Recipient rejected"), code, lines); return; }
        m_state = Data;
        sendCommand("DATA");
        break;

    case Data:
        if (code != 354) { failReply(tr("Server did not accept the message"), code, lines); return; }
        m_state = DataDone;
        m_socket->write(m_message);
        m_socket->write(".\r\n");
        m_timer.start();
        break;

    case DataDone:
        if (code != 250) { failReply(tr("Message rejected"), code, lines); return; }
        succeed();
        break;

    case Idle:
        break;
    }
}

void SmtpClient::parseCapabilities(const QStringList &lines)
{
    m_hasStartTls = false;
    m_authMechanisms.clear();
    // The first line is the greeting; the rest are capabilities.
    for (int i = 1; i < lines.size(); ++i) {
        const QString cap = lines.at(i).trimmed().toUpper();
        if (cap == QLatin1String("STARTTLS")) {
            m_hasStartTls = true;
        } else if (cap.startsWith(QLatin1String("AUTH ")) || cap.startsWith(QLatin1String("AUTH="))) {
            m_authMechanisms << cap.mid(5).split(QLatin1Char(' '), QString::SkipEmptyParts);
        }
    }
}

void SmtpClient::authenticate()
{
    if (m_username.isEmpty()) {
        startEnvelope();
        return;
    }
    if (m_authMechanisms.contains(QLatin1String("PLAIN"))) {
        QByteArray cred;
        cred.append('\0');
        cred += m_username.toUtf8();
        cred.append('\0');
        cred += m_password.toUtf8();
        m_state = AuthPlain;
        sendCommand("AUTH PLAIN " + cred.toBase64(), true);
    } else if (m_authMechanisms.contains(QLatin1String("LOGIN"))) {
        m_state = AuthLoginUser;
        sendCommand("AUTH LOGIN");
    } else {
        fail(tr("The server offers no supported login method (PLAIN or LOGIN)"));
    }
}

void SmtpClient::startEnvelope()
{
    m_state = MailFrom;
    sendCommand("MAIL FROM:<" + m_envelopeFrom.toLatin1() + ">");
}

void SmtpClient::onEncrypted()
{
    if (m_state != TlsHandshake)
        return;                     // implicit TLS: the greeting follows on its own
    m_tlsDone = true;
    m_state = Ehlo;                 // capabilities must be re-read after STARTTLS
    sendCommand("EHLO localhost");
}

void SmtpClient::onSslErrors(const QList<QSslError> &errors)
{
    if (m_state == Idle)
        return;
    QStringList texts;
    foreach (const QSslError &e, errors)
        texts << e.errorString();
    // Never ignore certificate problems: the password is about to be sent.
    fail(tr("Could not verify the server's certificate: %1").arg(texts.join(QStringLiteral("; "))));
}

void SmtpClient::onSocketError()
{
    if (m_state == Idle || !m_socket)
        return;
    fail(m_socket->errorString());
}

void SmtpClient::onDisconnected()
{
    if (m_state != Idle)
        fail(tr("Connection closed by the server"));
}

void SmtpClient::onTimeout()
{
    if (m_state != Idle)
        fail(tr("The server did not respond in time"));
}

void SmtpClient::failReply(const QString &context, int code, const QStringList &lines)
{
    fail(QStringLiteral("%1: %2 %3").arg(context).arg(code).arg(lines.join(QLatin1Char(' ')).trimmed()));
}

void SmtpClient::fail(const QString &message)
{
    if (m_state == Idle)
        return;
    teardown();
    setBusy(false);
    emit failed(message);
}

void SmtpClient::succeed()
{
    if (m_socket) {
        // Say goodbye politely, let the socket delete itself once it has closed.
        m_socket->disconnect(this);
        connect(m_socket, SIGNAL(disconnected()), m_socket, SLOT(deleteLater()));
        m_socket->write("QUIT\r\n");
        m_socket->disconnectFromHost();
        m_socket = 0;
    }
    m_timer.stop();
    m_state = Idle;
    m_message.clear();
    setBusy(false);
    emit sent();
}

void SmtpClient::teardown()
{
    m_timer.stop();
    m_state = Idle;
    m_message.clear();
    m_buffer.clear();
    if (m_socket) {
        m_socket->disconnect(this);
        m_socket->abort();
        m_socket->deleteLater();
        m_socket = 0;
    }
}
