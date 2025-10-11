#ifndef OVERLAYLABEL_H
#define OVERLAYLABEL_H

#include <QLabel>
#include <QPainter>
#include <QPaintEvent> // You should also include this here

class OverlayLabel : public QLabel {
    Q_OBJECT
public:
    explicit OverlayLabel(QWidget *parent = nullptr) : QLabel(parent) {}
    void setAngles(float pitch, float roll, float yaw) {
        m_pitch = pitch;
        m_roll = roll;
        m_yaw = yaw;
        update(); // Request a repaint
    }

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    float m_pitch = 0.0f;
    float m_roll = 0.0f;
    float m_yaw = 0.0f;
};

#endif // OVERLAYLABEL_H