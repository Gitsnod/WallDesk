#include "Chevron.h"

#include <QColor>
#include <QPainter>
#include <QPainterPath>
#include <QRectF>

namespace Chevron {

void drawDown(QPainter& painter, const QRectF& box, const QColor& color, qreal strokeWidth,
              qreal rotationDeg)
{
    if (box.width() < 6.0 || box.height() < 6.0) {
        return;
    }
    // 半宽 3.8 / 半高 2.0：宽高比约 1.9:1，与 M3 的 arrow_drop_down 接近，
    // 太小像点、太大像叉。圆头圆角是为了 30px 高度下不发毛刺。
    const qreal cx = box.center().x();
    const qreal cy = box.center().y();
    const qreal halfW = 3.8;
    const qreal halfH = 2.0;

    QPainterPath path;
    path.moveTo(cx - halfW, cy - halfH);
    path.lineTo(cx, cy + halfH);
    path.lineTo(cx + halfW, cy - halfH);

    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);
    if (rotationDeg != 0.0) {
        painter.translate(cx, cy);
        painter.rotate(rotationDeg);
        painter.translate(-cx, -cy);
    }
    painter.setBrush(Qt::NoBrush);
    painter.setPen(QPen(color, strokeWidth, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter.drawPath(path);
    painter.restore();
}

} // namespace Chevron
