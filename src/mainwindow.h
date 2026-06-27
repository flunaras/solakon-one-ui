#pragma once

#include <QtGui/QAction>
#include <QtSerialBus/QModbusDevice>
#include <QtWidgets/QLabel>
#include <QtWidgets/QMainWindow>

class ModbusApi;
class DashboardWidget;
class PvWidget;
class GridWidget;
class BatteryWidget;
class EnergyWidget;
class InfoWidget;
class AlarmWidget;
class SettingsWidget;
class RemoteControlWidget;
class PvChartWidget;

// ── MainWindow ────────────────────────────────────────────────────────────────
//
// Top-level QMainWindow.  Owns all panel dock widgets, routes ModbusApi
// signals to the appropriate widget update slots, manages the connection
// dialog, and persists window/dock geometry in QSettings.
class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    // Takes ownership of ModbusApi (via parent QObject tree).
    explicit MainWindow(ModbusApi *api, QWidget *parent = nullptr);
    ~MainWindow() override;

    // Called by main() when --host was given on the CLI; auto-connects
    // after the window is shown (deferred via QTimer::singleShot).
    void setInitialConnection(const QString &host, int port,
                              int slaveId, int intervalSeconds);

protected:
    void closeEvent(QCloseEvent *event) override;

private slots:
    void onConnectAction();
    void onDisconnectAction();
    void onConnectionStateChanged(QModbusDevice::State state);
    void onModbusError(const QString &message);
    void onShowSettings();
    void onShowRemoteControl();

private:
    // ── Setup helpers ──────────────────────────────────────────────────────
    void createCentralWidget();
    void createDockWidgets();
    void createMenus();
    void createStatusBar();
    void wireSignals();
    void saveWindowState();
    void restoreWindowState();
    void scheduleAutoConnect();

    // ── Core objects ───────────────────────────────────────────────────────
    ModbusApi *m_api;

    // ── Panel widgets ──────────────────────────────────────────────────────
    DashboardWidget     *m_dashboard      = nullptr;
    PvWidget            *m_pvWidget       = nullptr;
    GridWidget          *m_gridWidget     = nullptr;
    BatteryWidget       *m_batteryWidget  = nullptr;
    EnergyWidget        *m_energyWidget   = nullptr;
    InfoWidget          *m_infoWidget     = nullptr;
    AlarmWidget         *m_alarmWidget    = nullptr;
    SettingsWidget      *m_settingsWidget = nullptr;
    RemoteControlWidget *m_remoteWidget   = nullptr;
    PvChartWidget       *m_pvChartWidget  = nullptr;

    // ── Toolbar / status-bar ───────────────────────────────────────────────
    QAction *m_connectAction    = nullptr;
    QAction *m_disconnectAction = nullptr;
    QLabel  *m_statusLabel      = nullptr;

    // ── Pending auto-connect parameters ───────────────────────────────────
    QString m_pendingHost;
    int     m_pendingPort     = 502;
    int     m_pendingSlaveId  = 1;
    int     m_pendingInterval = 10;
};
