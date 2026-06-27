#pragma once

#include <QtCore/QObject>
#include <QtWidgets/QDialog>
#include <QtWidgets/QLabel>
#include <QtWidgets/QLineEdit>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QSpinBox>

// ── ConnectWindow ──────────────────────────────────────────────────────────────
//
// Modal dialog shown on startup (when no --host was given) or via
// File → Connect.  Persists connection settings in QSettings under the
// "connection/" group.  Emits connectionRequested() when the user confirms.
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
    QLabel    *m_errorLabel      = nullptr;
    QPushButton *m_connectButton = nullptr;
};
