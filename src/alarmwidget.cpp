#include "alarmwidget.h"

#include <QtWidgets/QFormLayout>
#include <QtWidgets/QGroupBox>
#include <QtWidgets/QVBoxLayout>

AlarmWidget::AlarmWidget(QWidget *parent)
    : QWidget(parent)
{
    auto *group = new QGroupBox(QStringLiteral("Alarms && Status"), this);
    auto *form  = new QFormLayout(group);

    m_statusLine = new QLabel(QStringLiteral("--"), this);
    m_statusLine->setAlignment(Qt::AlignCenter);
    m_statusLine->setStyleSheet(QStringLiteral("font-weight: bold;"));

    m_alarm1Label = new QLabel(QStringLiteral("--"), this);
    m_alarm2Label = new QLabel(QStringLiteral("--"), this);
    m_alarm3Label = new QLabel(QStringLiteral("--"), this);

    form->addRow(QStringLiteral("Status:"),  m_statusLine);
    form->addRow(QStringLiteral("ALARM_1:"), m_alarm1Label);
    form->addRow(QStringLiteral("ALARM_2:"), m_alarm2Label);
    form->addRow(QStringLiteral("ALARM_3:"), m_alarm3Label);

    auto *top = new QVBoxLayout(this);
    top->addWidget(group);
    top->addStretch();
}

void AlarmWidget::updateStatus(const InverterStatus &status)
{
    QStringList states;
    if (status.standby)   states << QStringLiteral("Standby");
    if (status.operation) states << QStringLiteral("Operation");
    if (status.fault)     states << QStringLiteral("FAULT");

    const QString text = states.isEmpty()
        ? QStringLiteral("Unknown (0x%1)").arg(status.raw, 4, 16, QLatin1Char('0'))
        : states.join(QStringLiteral(" | "));

    m_statusLine->setText(text);

    if (status.fault) {
        m_statusLine->setStyleSheet(QStringLiteral("color: red; font-weight: bold;"));
    } else if (status.operation) {
        m_statusLine->setStyleSheet(QStringLiteral("color: green; font-weight: bold;"));
    } else {
        m_statusLine->setStyleSheet(QStringLiteral("font-weight: bold;"));
    }
}

void AlarmWidget::updateAlarms(const AlarmState &alarms)
{
    auto fmtAlarm = [](quint16 v) -> QString {
        if (v == 0) return QStringLiteral("OK (0x0000)");
        return QStringLiteral("0x%1").arg(v, 4, 16, QLatin1Char('0')).toUpper();
    };

    m_alarm1Label->setText(fmtAlarm(alarms.alarm1));
    m_alarm2Label->setText(fmtAlarm(alarms.alarm2));
    m_alarm3Label->setText(fmtAlarm(alarms.alarm3));

    const QString alarmStyle = QStringLiteral("color: red;");
    const QString okStyle    = QString();
    m_alarm1Label->setStyleSheet(alarms.alarm1 ? alarmStyle : okStyle);
    m_alarm2Label->setStyleSheet(alarms.alarm2 ? alarmStyle : okStyle);
    m_alarm3Label->setStyleSheet(alarms.alarm3 ? alarmStyle : okStyle);
}
