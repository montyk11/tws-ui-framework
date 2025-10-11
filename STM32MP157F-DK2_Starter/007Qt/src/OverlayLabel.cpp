#include "OverlayLabel.h"
#include <QPaintEvent>

void OverlayLabel::paintEvent(QPaintEvent *event) {
    // Let the base class handle painting the pixmap (video frame)
    QLabel::paintEvent(event);

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    // Get the dimensions for positioning
    int centerX = rect().center().x();
    int centerY = rect().center().y();
    int screenWidth = rect().width();
    int screenHeight = rect().height();

    // 1. Draw Roll Indicator (Left Side)
    // The roll indicator is a vertical line that rotates.
    painter.save();
    painter.setPen(QPen(Qt::yellow, 2));
    
    // Translate to the center of the left side
    painter.translate(screenWidth * 0.15, centerY);
    // Rotate the canvas by the roll angle
    painter.rotate(m_roll);
    
    // Draw the vertical line and small tick marks
    painter.drawLine(0, -50, 0, 50); // The main vertical line
    painter.drawLine(-10, 0, 10, 0); // A small horizontal tick at the center
    
    painter.restore();

    // 2. Draw Pitch Indicator (Right Side)
    // The pitch indicator is a horizontal line that moves up and down.
    painter.save();
    painter.setPen(QPen(Qt::green, 2));
    
    // Scale the pitch value to control its movement range
    int pitchOffset = static_cast<int>(-m_pitch * 5); // Negative to make positive pitch move up
    
    // Translate to the center of the right side and apply the pitch offset
    painter.translate(screenWidth * 0.85, centerY + pitchOffset);

    // Draw the horizontal line and a small center dot
    painter.drawLine(0, -50, 0, 50); // The horizontal line
    painter.drawEllipse(-2, -2, 4, 4); // A small dot in the center
    
    painter.restore();

    // 3. Draw Heading (Yaw) Indicator (Top Center)
    // The yaw indicator is a compass or a simple line pointing directionally.
    // For simplicity, let's draw an arrow that rotates at the top center.
    painter.save();
    painter.setPen(QPen(Qt::cyan, 2));
    
    // Translate to the top center of the screen
    painter.translate(centerX, screenHeight * 0.1);
    
    // Rotate the canvas by the yaw angle
    painter.rotate(m_yaw);
    
    // Draw an arrow shape
    painter.drawLine(0, 0, 0, -30);
    painter.drawLine(0, -30, -5, -25);
    painter.drawLine(0, -30, 5, -25);
    
    painter.restore();
}