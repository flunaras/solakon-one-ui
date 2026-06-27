#include "mainwindow.h"

#include "alarmwidget.h"
#include "batterywidget.h"
#include "connectwindow.h"
#include "dashboardwidget.h"
#include "energywidget.h"
#include "gridwidget.h"
#include "infowidget.h"
#include "imodbusapi.h"
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

MainWindow::MainWindow(IModbusApi *api, QWidget *parent)
    : QMainWindow(parent)
    , m_api(api)
{
    setWindowTitle(QStringLiteral("Solakon One UI"));
    setMinimumSize(900, 600);
    // No central widget — every panel is a QDockWidget.
    // Nesting must be enabled before createDockWidgets() because splitDockWidget()
    // relies on it to create column-local (non-full-width) splitters.
    setDockNestingEnabled(true);

    createMenus();
    createDockWidgets();
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

void MainWindow::createDockWidgets()
{
    // Helper lambda: wraps a widget in a named QDockWidget.
    auto makeDock = [this](const QString &title, const QString &name,
                           QWidget *widget) -> QDockWidget * {
        auto *dock = new QDockWidget(title, this);
        dock->setObjectName(name);
        dock->setWidget(widget);
        return dock;
    };

    // ── Panel widgets ──────────────────────────────────────────────────────
    m_dashboard     = new DashboardWidget(this);
    m_batteryWidget = new BatteryWidget(this);
    m_pvWidget      = new PvWidget(this);
    m_gridWidget    = new GridWidget(this);
    m_energyWidget  = new EnergyWidget(this);
    m_infoWidget    = new InfoWidget(this);
    m_alarmWidget   = new AlarmWidget(this);
    m_pvChartWidget = new PvChartWidget(this);

    auto *battDock    = makeDock(QStringLiteral("Battery"),        QStringLiteral("battDock"),    m_batteryWidget);
    auto *pvDock      = makeDock(QStringLiteral("PV Input"),       QStringLiteral("pvDock"),      m_pvWidget);
    auto *dashDock    = makeDock(QStringLiteral("Dashboard"),      QStringLiteral("dashDock"),    m_dashboard);
    auto *pvChartDock = makeDock(QStringLiteral("PV Chart"),       QStringLiteral("pvChartDock"), m_pvChartWidget);
    auto *gridDock    = makeDock(QStringLiteral("Grid"),           QStringLiteral("gridDock"),    m_gridWidget);
    auto *energyDock  = makeDock(QStringLiteral("Energy"),         QStringLiteral("energyDock"),  m_energyWidget);
    auto *infoDock    = makeDock(QStringLiteral("Device Info"),    QStringLiteral("infoDock"),    m_infoWidget);
    auto *alarmDock   = makeDock(QStringLiteral("Alarms & Status"),QStringLiteral("alarmDock"),   m_alarmWidget);

    // ── Default nested layout ──────────────────────────────────────────────
    //
    // The sequence below uses splitDockWidget() to build three independent
    // columns.  Each splitDockWidget(A, B, Vertical) call creates a splitter
    // that is LOCAL to A's column cell — it does not span the other columns.
    //
    //   [Battery ] [Dashboard        ] [PV Chart / Grid / Energy]
    //   [PV Input] [Device Info/Alarms]
    //
    // Step 1 — Battery anchors the left dock area.
    addDockWidget(Qt::LeftDockWidgetArea, battDock);

    // Step 2 — Dashboard goes to the right of Battery (creates centre column).
    splitDockWidget(battDock, dashDock, Qt::Horizontal);

    // Step 3 — PV Chart goes to the right of Dashboard (creates right column).
    splitDockWidget(dashDock, pvChartDock, Qt::Horizontal);

    // Step 4 — PV Input below Battery: LOCAL to the left column.
    splitDockWidget(battDock, pvDock, Qt::Vertical);

    // Step 5 — Device Info below Dashboard: LOCAL to the centre column.
    splitDockWidget(dashDock, infoDock, Qt::Vertical);

    // Step 6 — Tab Alarms with Device Info; tab Grid and Energy with PV Chart.
    tabifyDockWidget(infoDock,    alarmDock);
    tabifyDockWidget(pvChartDock, gridDock);
    tabifyDockWidget(pvChartDock, energyDock);

    // Raise the preferred front tabs on first launch.
    infoDock->raise();
    pvChartDock->raise();

    // ── View menu: one toggle action per dock ──────────────────────────────
    // Built here (rather than in createMenus()) so the docks are in scope and
    // we can use QDockWidget::toggleViewAction() — these auto-managed actions
    // stay in sync with dock visibility, including the user closing the dock
    // via the title-bar X button.  Dock order matches the layout left→right.
    createViewMenu({ battDock, pvDock, dashDock, infoDock, alarmDock,
                     pvChartDock, gridDock, energyDock });
}

void MainWindow::createViewMenu(const QList<QDockWidget *> &docks)
{
    auto *viewMenu = menuBar()->addMenu(QStringLiteral("&View"));

    for (QDockWidget *dock : docks) {
        // toggleViewAction() returns a checkable QAction owned by the dock
        // whose checked state is bound to the dock's visibility — no manual
        // wiring needed.
        viewMenu->addAction(dock->toggleViewAction());
    }
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
    connect(m_api, &IModbusApi::connectionStateChanged,
            this,  &MainWindow::onConnectionStateChanged);
    connect(m_api, &IModbusApi::errorOccurred,
            this,  &MainWindow::onModbusError);

    // ── Data → dashboard ───────────────────────────────────────────────────
    connect(m_api, &IModbusApi::pvDataUpdated,
            m_dashboard, &DashboardWidget::updatePvData);
    connect(m_api, &IModbusApi::gridDataUpdated,
            m_dashboard, &DashboardWidget::updateGridData);
    connect(m_api, &IModbusApi::batteryDataUpdated,
            m_dashboard, &DashboardWidget::updateBatteryData);
    connect(m_api, &IModbusApi::temperatureUpdated,
            m_dashboard, &DashboardWidget::updateTemperature);
    // Live work-mode updates come from both the per-cycle poll and the
    // settings read-back (after connect / after writes). The dashboard
    // only stores the latest value, so duplicate notifications are harmless.
    connect(m_api, &IModbusApi::workModeUpdated,
            m_dashboard, &DashboardWidget::updateWorkMode);
    connect(m_api, &IModbusApi::settingsRead,
            m_dashboard, [this](const InverterSettings &s) {
                m_dashboard->updateWorkMode(s.workMode);
            });

    // ── Data → panel widgets ───────────────────────────────────────────────
    connect(m_api, &IModbusApi::pvDataUpdated,
            m_pvWidget, &PvWidget::updateData);
    connect(m_api, &IModbusApi::pvDataUpdated,
            m_pvChartWidget, &PvChartWidget::onPvDataUpdated);
    connect(m_api, &IModbusApi::gridDataUpdated,
            m_pvChartWidget, &PvChartWidget::onGridDataUpdated);
    connect(m_api, &IModbusApi::gridDataUpdated,
            m_gridWidget, &GridWidget::updateData);
    connect(m_api, &IModbusApi::batteryDataUpdated,
            m_pvChartWidget, &PvChartWidget::onBatteryDataUpdated);
    connect(m_api, &IModbusApi::batteryDataUpdated,
            m_batteryWidget, &BatteryWidget::updateData);
    connect(m_api, &IModbusApi::energyDataUpdated,
            m_energyWidget, &EnergyWidget::updateData);
    connect(m_api, &IModbusApi::infoUpdated,
            m_infoWidget, &InfoWidget::updateData);
    connect(m_api, &IModbusApi::statusUpdated,
            m_alarmWidget, &AlarmWidget::updateStatus);
    connect(m_api, &IModbusApi::gridStatusUpdated,
            m_alarmWidget, &AlarmWidget::updateGridStatus);
    connect(m_api, &IModbusApi::alarmStateUpdated,
            m_alarmWidget, &AlarmWidget::updateAlarms);

    // ── Remote control read-back → dialog + dashboard ──────────────────────
    // The widget shows the live read-back of register 46001 so the operator
    // can detect external sessions (e.g. FoxESS app strategy periods) before
    // applying a new command.  The dashboard also caches the state so it can
    // annotate the Work Mode tile when an active remote-control session is
    // responsible for the inverter being in Force Charge / Force Discharge.
    connect(m_api, &IModbusApi::remoteControlStateUpdated,
            m_remoteWidget, &RemoteControlWidget::updateState);
    connect(m_api, &IModbusApi::remoteControlStateUpdated,
            m_dashboard, &DashboardWidget::updateRemoteControlState);

    // ── Settings read-back ─────────────────────────────────────────────────
    connect(m_api, &IModbusApi::settingsRead,
            m_settingsWidget, &SettingsWidget::loadSettings);
    connect(m_api, &IModbusApi::writeSucceeded,
            m_settingsWidget, [this](const QString &name) {
                statusBar()->showMessage(
                    QStringLiteral("Written: %1").arg(name), 4000);
                // Re-read settings to confirm the new value
                m_api->readSettingsRegisters();
            });
    connect(m_api, &IModbusApi::writeFailed,
            m_settingsWidget, [this](const QString &name, const QString &err) {
                statusBar()->showMessage(
                    QStringLiteral("Write failed (%1): %2").arg(name, err), 8000);
            });
}

void MainWindow::saveWindowState()
{
    QSettings s;
    s.setValue(QStringLiteral("window/geometry"), saveGeometry());
    s.setValue(QStringLiteral("window/state"),    saveState(2));
}

void MainWindow::restoreWindowState()
{
    QSettings s;
    if (s.contains(QStringLiteral("window/geometry")))
        restoreGeometry(s.value(QStringLiteral("window/geometry")).toByteArray());
    if (s.contains(QStringLiteral("window/state")))
        restoreState(s.value(QStringLiteral("window/state")).toByteArray(), 2);
}

void MainWindow::scheduleAutoConnect()
{
    QTimer::singleShot(0, this, [this]() {
        m_api->startPolling(m_pendingInterval);
        m_api->connectToInverter(m_pendingHost, m_pendingPort, m_pendingSlaveId);
    });
}
