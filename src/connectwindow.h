#pragma once

#include <QtCore/QObject>
#include <QtWidgets/QCheckBox>
#include <QtWidgets/QDialog>
#include <QtWidgets/QLabel>
#include <QtWidgets/QLineEdit>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QSpinBox>

// ── ConnectWindow ──────────────────────────────────────────────────────────────
//
// Modal dialog shown on demand via File → Connect.  Persists connection
// settings in QSettings under the "connection/" group, including an
// "Automatically connect on startup" checkbox ("connection/autoConnect").
// When enabled, MainWindow uses these persisted settings to reconnect
// without showing this dialog on the next application launch.
// Emits connectionRequested() when the user confirms.
class ConnectWindow : public QDialog
{
    Q_OBJECT

public:
    explicit ConnectWindow(QWidget *parent = nullptr);

    // Pre-fill the form with specific values (used by --host CLI argument).
    void setValues(const QString &host, int port, int slaveId, int intervalSeconds);

signals:
    void connectionRequested(const QString &host, int port, int slaveId, int intervalSeconds);

private slots:
    void onAccepted();

private:
    void loadSettings();
    void saveSettings();

    QLineEdit *m_hostEdit        = nullptr;
    QSpinBox  *m_portSpin        = nullptr;
    QSpinBox  *m_slaveIdSpin     = nullptr;
    QSpinBox  *m_intervalSpin    = nullptr;
    QCheckBox *m_autoConnectCheck = nullptr;
    QLabel    *m_errorLabel      = nullptr;
    QPushButton *m_connectButton = nullptr;
};
