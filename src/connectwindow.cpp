#include "connectwindow.h"

#include <QtCore/QSettings>
#include <QtWidgets/QDialogButtonBox>
#include <QtWidgets/QFormLayout>
#include <QtWidgets/QGroupBox>
#include <QtWidgets/QVBoxLayout>

// ── Constructor ────────────────────────────────────────────────────────────────

ConnectWindow::ConnectWindow(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(QStringLiteral("Connect to Inverter"));
    setModal(true);
    setMinimumWidth(360);

    // ── Input form ─────────────────────────────────────────────────────────
    auto *groupBox  = new QGroupBox(QStringLiteral("Modbus TCP Connection"), this);
    auto *formLayout = new QFormLayout(groupBox);
    formLayout->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);

    m_hostEdit = new QLineEdit(this);
    m_hostEdit->setPlaceholderText(QStringLiteral("e.g. 192.168.1.148"));
    formLayout->addRow(QStringLiteral("Host / IP:"), m_hostEdit);

    m_portSpin = new QSpinBox(this);
    m_portSpin->setRange(1, 65535);
    m_portSpin->setValue(502);
    formLayout->addRow(QStringLiteral("Port:"), m_portSpin);

    m_slaveIdSpin = new QSpinBox(this);
    m_slaveIdSpin->setRange(1, 247);
    m_slaveIdSpin->setValue(1);
    formLayout->addRow(QStringLiteral("Slave ID:"), m_slaveIdSpin);

    m_intervalSpin = new QSpinBox(this);
    m_intervalSpin->setRange(2, 300);
    m_intervalSpin->setValue(10);
    m_intervalSpin->setSuffix(QStringLiteral(" s"));
    formLayout->addRow(QStringLiteral("Poll interval:"), m_intervalSpin);

    m_autoConnectCheck = new QCheckBox(
        QStringLiteral("Automatically connect on startup"), this);
    formLayout->addRow(QString(), m_autoConnectCheck);

    // ── Error label (hidden until needed) ─────────────────────────────────
    m_errorLabel = new QLabel(this);
    m_errorLabel->setStyleSheet(QStringLiteral("color: red;"));
    m_errorLabel->hide();

    // ── Button box ─────────────────────────────────────────────────────────
    auto *buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    m_connectButton = buttons->button(QDialogButtonBox::Ok);
    m_connectButton->setText(QStringLiteral("Connect"));

    // ── Top-level layout ───────────────────────────────────────────────────
    auto *topLayout = new QVBoxLayout(this);
    topLayout->addWidget(groupBox);
    topLayout->addWidget(m_errorLabel);
    topLayout->addWidget(buttons);

    connect(buttons, &QDialogButtonBox::accepted, this, &ConnectWindow::onAccepted);
    connect(buttons, &QDialogButtonBox::rejected, this, &ConnectWindow::reject);

    loadSettings();
}

// ── Public ─────────────────────────────────────────────────────────────────────

void ConnectWindow::setValues(const QString &host, int port, int slaveId, int intervalSeconds)
{
    m_hostEdit->setText(host);
    m_portSpin->setValue(port);
    m_slaveIdSpin->setValue(slaveId);
    m_intervalSpin->setValue(intervalSeconds);
}

// ── Slots ──────────────────────────────────────────────────────────────────────

void ConnectWindow::onAccepted()
{
    const QString host = m_hostEdit->text().trimmed();
    if (host.isEmpty()) {
        m_errorLabel->setText(QStringLiteral("Please enter a host address."));
        m_errorLabel->show();
        return;
    }
    m_errorLabel->hide();
    saveSettings();
    emit connectionRequested(host,
                             m_portSpin->value(),
                             m_slaveIdSpin->value(),
                             m_intervalSpin->value());
    accept();
}

// ── Private helpers ────────────────────────────────────────────────────────────

void ConnectWindow::loadSettings()
{
    QSettings s;
    m_hostEdit->setText(s.value(QStringLiteral("connection/host")).toString());
    m_portSpin->setValue(s.value(QStringLiteral("connection/port"), 502).toInt());
    m_slaveIdSpin->setValue(s.value(QStringLiteral("connection/slaveId"), 1).toInt());
    m_intervalSpin->setValue(s.value(QStringLiteral("connection/intervalSeconds"), 10).toInt());
    m_autoConnectCheck->setChecked(s.value(QStringLiteral("connection/autoConnect"), false).toBool());
}

void ConnectWindow::saveSettings()
{
    QSettings s;
    s.setValue(QStringLiteral("connection/host"),            m_hostEdit->text().trimmed());
    s.setValue(QStringLiteral("connection/port"),            m_portSpin->value());
    s.setValue(QStringLiteral("connection/slaveId"),         m_slaveIdSpin->value());
    s.setValue(QStringLiteral("connection/intervalSeconds"), m_intervalSpin->value());
    s.setValue(QStringLiteral("connection/autoConnect"),     m_autoConnectCheck->isChecked());
}
