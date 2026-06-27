#include "mainwindow.h"
#include "modbusapi.h"

#include <QtCore/QCommandLineOption>
#include <QtCore/QCommandLineParser>
#include <QtWidgets/QApplication>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    app.setOrganizationName(QStringLiteral("flunaras"));
    app.setApplicationName(QStringLiteral("solakon-one-ui"));
    app.setApplicationVersion(QStringLiteral("1.0.0"));

    // ── CLI parsing ────────────────────────────────────────────────────────
    QCommandLineParser parser;
    parser.setApplicationDescription(
        QStringLiteral("FoxESS Solakon One inverter monitor / control"));
    parser.addHelpOption();
    parser.addVersionOption();

    QCommandLineOption hostOpt(
        QStringList{QStringLiteral("H"), QStringLiteral("host")},
        QStringLiteral("Inverter IP address or hostname"),
        QStringLiteral("host"));
    QCommandLineOption portOpt(
        QStringList{QStringLiteral("p"), QStringLiteral("port")},
        QStringLiteral("Modbus TCP port (default: 502)"),
        QStringLiteral("port"),
        QStringLiteral("502"));
    QCommandLineOption slaveOpt(
        QStringList{QStringLiteral("s"), QStringLiteral("slave-id")},
        QStringLiteral("Modbus slave/unit ID (default: 1)"),
        QStringLiteral("id"),
        QStringLiteral("1"));
    QCommandLineOption intervalOpt(
        QStringList{QStringLiteral("i"), QStringLiteral("interval")},
        QStringLiteral("Poll interval in seconds (default: 10, range 2-300)"),
        QStringLiteral("seconds"),
        QStringLiteral("10"));

    parser.addOption(hostOpt);
    parser.addOption(portOpt);
    parser.addOption(slaveOpt);
    parser.addOption(intervalOpt);
    parser.process(app);

    // ── Create core objects ────────────────────────────────────────────────
    auto *modbusApi = new ModbusApi();
    auto *mainWindow = new MainWindow(modbusApi);

    // ── Apply CLI arguments ────────────────────────────────────────────────
    const QString host     = parser.value(hostOpt);
    const int     port     = qBound(1, parser.value(portOpt).toInt(),    65535);
    const int     slaveId  = qBound(1, parser.value(slaveOpt).toInt(),   247);
    const int     interval = qBound(2, parser.value(intervalOpt).toInt(), 300);

    if (!host.isEmpty()) {
        mainWindow->setInitialConnection(host, port, slaveId, interval);
    }

    mainWindow->show();
    return app.exec();
}
