#include <QtQuick>
#include <sailfishapp.h>
#include "smtpclient.h"

int main(int argc, char *argv[])
{
    qmlRegisterType<SmtpClient>("harbour.selffish", 1, 0, "SmtpClient");

    return SailfishApp::main(argc, argv);
}
