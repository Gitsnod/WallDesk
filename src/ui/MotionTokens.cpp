#include "MotionTokens.h"

#include <QPointF>

QEasingCurve MotionTokens::standard()
{
    QEasingCurve curve(QEasingCurve::BezierSpline);
    curve.addCubicBezierSegment(QPointF(0.2, 0.0), QPointF(0.0, 1.0), QPointF(1.0, 1.0));
    return curve;
}

QEasingCurve MotionTokens::standardDecelerate()
{
    QEasingCurve curve(QEasingCurve::BezierSpline);
    curve.addCubicBezierSegment(QPointF(0.0, 0.0), QPointF(0.0, 1.0), QPointF(1.0, 1.0));
    return curve;
}

QEasingCurve MotionTokens::standardAccelerate()
{
    QEasingCurve curve(QEasingCurve::BezierSpline);
    curve.addCubicBezierSegment(QPointF(0.3, 0.0), QPointF(1.0, 1.0), QPointF(1.0, 1.0));
    return curve;
}

QEasingCurve MotionTokens::rippleCurve()
{
    QEasingCurve curve(QEasingCurve::BezierSpline);
    curve.addCubicBezierSegment(QPointF(0.0, 0.0), QPointF(0.2, 1.0), QPointF(1.0, 1.0));
    return curve;
}

bool MotionTokens::animationsEnabled()
{
    BOOL systemAnimations = TRUE;
    if (SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION, 0, &systemAnimations, 0)
        && !systemAnimations) {
        return false;
    }
    return true;
}

int MotionTokens::duration(int nominalMs)
{
    return animationsEnabled() ? nominalMs : 0;
}
