#pragma once

#include <QWidget>
#include <QString>
#include <QPixmap>
#include <QPaintEvent>

class HeaderWidget : public QWidget
{
    Q_OBJECT
public:
    explicit HeaderWidget(QWidget *parent = nullptr);

    void setTitle(const QString &title);
    void setLogo(const QPixmap &pix);

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    QString m_title;
    QPixmap m_logo;
};
