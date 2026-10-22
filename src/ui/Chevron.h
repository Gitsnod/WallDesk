#pragma once

#include <QtGlobal>

class QColor;
class QPainter;
class QRectF;

/** 自绘小图标。QSS 没有内置箭头图像，样式表里只能给 ::drop-down 宽度，
 *  箭头本身要么引资源文件、要么自绘。两处（下拉框、带菜单的按钮）要一致，
 *  所以收到这里，避免各画一份、粗细角度走形。 */
namespace Chevron {

/** 在 box 内居中画一个向下的 chevron（V 形）。 */
void drawDown(QPainter& painter, const QRectF& box, const QColor& color,
              qreal strokeWidth = 1.6);

} // namespace Chevron
