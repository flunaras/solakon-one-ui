#include "mainwindow.h"

#include "alarmwidget.h"
#include "batterywidget.h"
#include "connectwindow.h"
#include "dashboardwidget.h"
#include "energywidget.h"
#include "gridwidget.h"
#include "infowidget.h"
#include "modbusapi.h"
#include "pvchartwidget.h"
#include "pvwidget.h"
#include "remotecontrolwidget.h"
#include "settingswidget.h"

#include <QtGui/QCloseEvent>
#include <QtCore/QSettings>
#include <QtCore/QTimer>
#include <QtGui/QAction>
#include <QtWidgets/QDockWidget>
#include <QtWidgets/QLabel>
#include <QtWidgets/QMenu>
#include <QtWidgets/QMenuBar>
#include <QtWidgets/QMessageBox>
#include <QtWidgets/QStatusBar>

// ── Constructor / Destructor ────────────────────────────────────────────────────

MainWindow::MainWindow(ModbusApi *api, QWidget *parent)
    : QMainWindow(parent)
    , m_api(api)
{
    setWindowTitle(QStringLiteral("Solakon One UI"));
    setMinimumSize(900, 600);
    setDockNestingEnabled(true);

    createCentralWidget();
    createDockWidgets();
    createMenus();
    createStatusBar();
    wireSignals();
    restoreWindowState();
}

MainWindow::~MainWindow() = default;

// ── Public ─────────────────────────────────────────────────────────────────────

void MainWindow::setInitialConnection(const QString &host, int port,
                                      int slaveId, int intervalSeconds)
{
    m_pendingHost     = host;
    m_pendingPort     = port;
    m_pendingSlaveId  = slaveId;
    m_pendingInterval = intervalSeconds;
    scheduleAutoConnect();
}

// ── Protected ──────────────────────────────────────────────────────────────────

void MainWindow::closeEvent(QCloseEvent *event)
{
    saveWindowState();
    m_api->disconnectFromInverter();
    event->accept();
}

// ── Slots ──────────────────────────────────────────────────────────────────────

void MainWindow::onConnectAction()
{
    auto *dlg = new ConnectWindow(this);
    connect(dlg, &ConnectWindow::connectionRequested,
            this, [this](const QString &host, int port, int slaveId, int interval) {
                m_api->startPolling(interval);
                m_api->connectToInverter(host, port, slaveId);
            });
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    dlg->exec();
}

void MainWindow::onDisconnectAction()
{
    m_api->disconnectFromInverter();
}

void MainWindow::onConnectionStateChanged(QModbusDevice::State state)
{
    const bool connected = (state == QModbusDevice::ConnectedState);
    m_connectAction->setEnabled(!connected);
    m_disconnectAction->setEnabled(connected);

    switch (state) {
    case QModbusDevice::ConnectedState:
        m_statusLabel->setText(QStringLiteral("Connected"));
        m_statusLabel->setStyleSheet(QStringLiteral("color: green; font-weight: bold;"));
        break;
    case QModbusDevice::ConnectingState:
        m_statusLabel->setText(QStringLiteral("Connecting..."));
        m_statusLabel->setStyleSheet(QString());
        break;
    case QModbusDevice::ClosingState:
        m_statusLabel->setText(QStringLiteral("Disconnecting..."));
        m_statusLabel->setStyleSheet(QString());
        break;
    default:
        m_statusLabel->setText(QStringLiteral("Disconnected"));
        m_statusLabel->setStyleSheet(QStringLiteral("color: red;"));
        break;
    }
}

void MainWindow::onModbusError(const QString &message)
{
    statusBar()->showMessage(QStringLiteral("Error: ") + message, 8000);
}

void MainWindow::onShowSettings()
{
    m_settingsWidget->show();
    m_settingsWidget->raise();
    m_settingsWidget->activateWindow();
}

void MainWindow::onShowRemoteControl()
{
    m_remoteWidget->show();
    m_remoteWidget->raise();
    m_remoteWidget->activateWindow();
}

// ── Private helpers ────────────────────────────────────────────────────────────

void MainWindow::createCentralWidget()
{
    m_dashboard = new DashboardWidget(this);
    setCentralWidget(m_dashboard);
}

void MainWindow::createDockWidgets()
{
    // ── Left column (nested — each split is local, not full-width) ──────────
    //
    // Battery anchors the column; splitDockWidget() creates a sub-splitter
    // scoped to this group so it does not span the right-side panels.

    m_batteryWidget = new BatteryWidget(this);
    auto *battDock = new QDockWidget(QStringLiteral("Battery"), this);
    battDock->setObjectName(QStringLiteral("battDock"));
    battDock->setWidget(m_batteryWidget);
    addDockWidget(Qt::LeftDockWidgetArea, battDock);

    // PV Input below Battery — local left-column splitter only
    m_pvWidget = new PvWidget(this);
    auto *pvDock = new QDockWidget(QStringLiteral("PV Input"), this);
    pvDock->setObjectName(QStringLiteral("pvDock"));
    pvDock->setWidget(m_pvWidget);
    splitDockWidget(battDock, pvDock, Qt::Vertical);

    // ── Bottom docks (tabbed) ──────────────────────────────────────────────
    //
    // Keeping Device Info and Alarms in the bottom area preserves a visible
    // drop target there; users can drag them into the left column if preferred
    // and the nested layout will create a local split (not a full-width one).

    m_infoWidget = new InfoWidget(this);
    auto *infoDock = new QDockWidget(QStringLiteral("Device Info"), this);
    infoDock->setObjectName(QStringLiteral("infoDock"));
    infoDock->setWidget(m_infoWidget);
    addDockWidget(Qt::BottomDockWidgetArea, infoDock);

    m_alarmWidget = new AlarmWidget(this);
    auto *alarmDock = new QDockWidget(QStringLiteral("Alarms & Status"), this);
    alarmDock->setObjectName(QStringLiteral("alarmDock"));
    alarmDock->setWidget(m_alarmWidget);
    tabifyDockWidget(infoDock, alarmDock);

    // ── Right column (PV Chart + Grid + Energy tabbed) ──────────────────────

    m_pvChartWidget = new PvChartWidget(this);
    auto *pvChartDock = new QDockWidget(QStringLiteral("PV Chart"), this);
    pvChartDock->setObjectName(QStringLiteral("pvChartDock"));
    pvChartDock->setWidget(m_pvChartWidget);
    addDockWidget(Qt::RightDockWidgetArea, pvChartDock);

    m_gridWidget = new GridWidget(this);
    auto *gridDock = new QDockWidget(QStringLiteral("Grid"), this);
    gridDock->setObjectName(QStringLiteral("gridDock"));
    gridDock->setWidget(m_gridWidget);
    tabifyDockWidget(pvChartDock, gridDock);

    m_energyWidget = new EnergyWidget(this);
    auto *energyDock = new QDockWidget(QStringLiteral("Energy"), this);
    energyDock->setObjectName(QStringLiteral("energyDock"));
    energyDock->setWidget(m_energyWidget);
    tabifyDockWidget(pvChartDock, energyDock);

    // Raise PV Chart tab so it is visible on first launch
    pvChartDock->raise();
}

void MainWindow::createMenus()
{
    // ── File menu ──────────────────────────────────────────────────────────
    auto *fileMenu = menuBar()->addMenu(QStringLiteral("&File"));

    m_connectAction = new QAction(QStringLiteral("&Connect..."), this);
    m_connectAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+K")));
    connect(m_connectAction, &QAction::triggered, this, &MainWindow::onConnectAction);
    fileMenu->addAction(m_connectAction);

    m_disconnectAction = new QAction(QStringLiteral("&Disconnect"), this);
    m_disconnectAction->setEnabled(false);
    connect(m_disconnectAction, &QAction::triggered, this, &MainWindow::onDisconnectAction);
    fileMenu->addAction(m_disconnectAction);

    fileMenu->addSeparator();
    auto *quitAction = new QAction(QStringLiteral("&Quit"), this);
    quitAction->setShortcut(QKeySequence::Quit);
    connect(quitAction, &QAction::triggered, this, &MainWindow::close);
    fileMenu->addAction(quitAction);

    // ── Inverter menu ──────────────────────────────────────────────────────
    auto *inverterMenu = menuBar()->addMenu(QStringLiteral("&Inverter"));

    auto *settingsAction = new QAction(QStringLiteral("&Settings..."), this);
    settingsAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+,")));
    connect(settingsAction, &QAction::triggered, this, &MainWindow::onShowSettings);
    inverterMenu->addAction(settingsAction);

    auto *remoteAction = new QAction(QStringLiteral("&Remote Control..."), this);
    connect(remoteAction, &QAction::triggered, this, &MainWindow::onShowRemoteControl);
    inverterMenu->addAction(remoteAction);

    // ── Dialogs (created here, shown on demand) ────────────────────────────
    m_settingsWidget = new SettingsWidget(m_api, this);
    m_settingsWidget->hide();

    m_remoteWidget = new RemoteControlWidget(m_api, this);
    m_remoteWidget->hide();
}

void MainWindow::createStatusBar()
{
    m_statusLabel = new QLabel(QStringLiteral("Disconnected"), this);
    m_statusLabel->setStyleSheet(QStringLiteral("color: red;"));
    statusBar()->addPermanentWidget(m_statusLabel);
}

void MainWindow::wireSignals()
{
    // ── Connection state ───────────────────────────────────────────────────
    connect(m_api, &ModbusApi::connectionStateChanged,
            this,  &MainWindow::onConnectionStateChanged);
    connect(m_api, &ModbusApi::errorOccurred,
            this,  &MainWindow::onModbusError);

    // ── Data → dashboard ───────────────────────────────────────────────────
    connect(m_api, &ModbusApi::pvDataUpdated,
            m_dashboard, &DashboardWidget::updatePvData);
    connect(m_api, &ModbusApi::gridDataUpdated,
            m_dashboard, &DashboardWidget::updateGridData);
    connect(m_api, &ModbusApi::batteryDataUpdated,
            m_dashboard, &DashboardWidget::updateBatteryData);
    connect(m_api, &ModbusApi::temperatureUpdated,
            m_dashboard, &DashboardWidget::updateTemperature);

    // ── Data → panel widgets ───────────────────────────────────────────────
    connect(m_api, &ModbusApi::pvDataUpdated,
            m_pvWidget, &PvWidget::updateData);
    connect(m_api, &ModbusApi::pvDataUpdated,
            m_pvChartWidget, &PvChartWidget::onPvDataUpdated);
    connect(m_api, &ModbusApi::gridDataUpdated,
            m_pvChartWidget, &PvChartWidget::onGridDataUpdated);
    connect(m_api, &ModbusApi::gridDataUpdated,
            m_gridWidget, &GridWidget::updateData);
    connect(m_api, &ModbusApi::batteryDataUpdated,
            m_batteryWidget, &BatteryWidget::updateData);
    connect(m_api, &ModbusApi::energyDataUpdated,
            m_energyWidget, &EnergyWidget::updateData);
    connect(m_api, &ModbusApi::infoUpdated,
            m_infoWidget, &InfoWidget::updateData);
    connect(m_api, &ModbusApi::statusUpdated,
            m_alarmWidget, &AlarmWidget::updateStatus);
    connect(m_api, &ModbusApi::alarmStateUpdated,
            m_alarmWidget, &AlarmWidget::updateAlarms);

    // ── Settings read-back ─────────────────────────────────────────────────
    connect(m_api, &ModbusApi::settingsRead,
            m_settingsWidget, &SettingsWidget::loadSettings);
    connect(m_api, &ModbusApi::writeSucceeded,
            m_settingsWidget, [this](const QString &name) {
                statusBar()->showMessage(
                    QStringLiteral("Written: %1").arg(name), 4000);
                // Re-read settings to confirm the new value
                m_api->readSettingsRegisters();
            });
    connect(m_api, &ModbusApi::writeFailed,
            m_settingsWidget, [this](const QString &name, const QString &err) {
                statusBar()->showMessage(
                    QStringLiteral("Write failed (%1): %2").arg(name, err), 8000);
            });
}

void MainWindow::saveWindowState()
{
    QSettings s;
    s.setValue(QStringLiteral("window/geometry"), saveGeometry());
    s.setValue(QStringLiteral("window/state"),    saveState(1));
}

void MainWindow::restoreWindowState()
{
    QSettings s;
    if (s.contains(QStringLiteral("window/geometry"))) {
        restoreGeometry(s.value(QStringLiteral("window/geometry")).toByteArray());
    }
    if (s.contains(QStringLiteral("window/state"))) {
        restoreState(s.value(QStringLiteral("window/state")).toByteArray(), 1);
    }
}

void MainWindow::scheduleAutoConnect()
{
    QTimer::singleShot(0, this, [this]() {
        m_api->startPolling(m_pendingInterval);
        m_api->connectToInverter(m_pendingHost, m_pendingPort, m_pendingSlaveId);
    });
}
