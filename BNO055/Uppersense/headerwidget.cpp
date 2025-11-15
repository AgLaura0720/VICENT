#include "headerwidget.h"
#include <QPainter>
#include <QLinearGradient>

HeaderWidget::HeaderWidget(QWidget *parent)
    : QWidget(parent)
{
    setMinimumHeight(90);
    m_title = QString::fromUtf8("Panel de Control — Funcionalidad de muñeca y codo");
}

void HeaderWidget::setTitle(const QString &title)
{
    m_title = title;
    update();
}

void HeaderWidget::setLogo(const QPixmap &pix)
{
    m_logo = pix;
    update();
}

void HeaderWidget::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);

    QLinearGradient grad(0, 0, width(), 0);
    grad.setColorAt(0.0, QColor("#002b6f"));
    grad.setColorAt(1.0, QColor("#4ea1ff"));
    p.fillRect(rect(), grad);

    int h = height();
    if (!m_logo.isNull()) {
        int size = qMin(h - 20, 60);
        QPixmap scaled = m_logo.scaled(size, size, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        int x = 40;
        int y = (h - scaled.height()) / 2;
        p.drawPixmap(x, y, scaled);
    }

    QFont f("Segoe UI", 18, QFont::Bold);
    p.setFont(f);
    p.setPen(Qt::white);
    p.drawText(rect(), Qt::AlignCenter, m_title);
}
