#pragma once
#include "export.hpp"
#include "enhanced/calibration-profile.hpp"
#include "enhanced/head-controls.hpp"
#include <QWidget>
#include <QPainter>
#include <QPainterPath>

class OTR_GUI_EXPORT smart_comparison final : public QWidget {
    Q_DECLARE_TR_FUNCTIONS(smart_comparison)
    using axis_result=enhanced::calibration::axis_result;
    using response_style=enhanced::calibration::response_style;
public:
    std::array<spline,3> positive,negative;
    axis_result data;
    bool valid=false;
    double limit=180;
    QString units="degrees";
    explicit smart_comparison(QWidget* parent) : QWidget(parent) {setMinimumHeight(210);for(auto* a:{&positive,&negative})for(auto& s:*a)enhanced::calibration::private_spline(s);}
    void set_axis(const axis_result& a,double output_limit,const QString& unit) {
        data=a;limit=output_limit;units=unit;valid=a.calibrated;
        for(int i=0;i<3 && valid;++i) {
            auto variant=a;variant.style=response_style(i+1);
            valid=generate(variant);
            if(valid) {positive[i].set_points(enhanced::calibration::qt_points(variant.positive_points));negative[i].set_points(enhanced::calibration::qt_points(variant.negative_points));}
        }
        update();
    }
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);p.fillRect(rect(),palette().base());p.setRenderHint(QPainter::Antialiasing);
        const QRectF box(56,28,width()-73,height()-90);p.setPen(palette().text().color());p.drawRect(box);
        if(!valid)return;
        const double xmin=-data.negative.magnitude*1.05,xmax=data.positive.magnitude*1.05;
        const double ymax=std::max(data.target_positive,data.target_negative)*1.08;
        const auto screen=[&](double x,double y){return QPointF(box.left()+(x-xmin)/(xmax-xmin)*box.width(),box.center().y()-y/ymax*box.height()/2);};
        p.setPen(palette().mid().color());p.drawLine(screen(xmin,0),screen(xmax,0));p.drawLine(screen(0,-ymax),screen(0,ymax));
        const QColor colors[]={QColor(30,120,70),QColor(40,100,210),QColor(190,65,155)};
        for(int i=0;i<3;++i){QPainterPath path;for(int k=0;k<=1024;++k){const double x=xmin+(xmax-xmin)*k/1024;
            const double y=enhanced::adjusted_mapping_output((x<0?negative[i]:positive[i]).get_value_no_save(x),data.output_gain,limit);
            if(!k)path.moveTo(screen(x,y));else path.lineTo(screen(x,y));}
            p.setPen(QPen(colors[i],2));p.drawPath(path);
            p.drawText(QRectF(i*width()/3.,0,width()/3.,25),Qt::AlignCenter,QString::fromLatin1(enhanced::calibration::style_name(response_style(i+1))));}
        p.setPen(palette().text().color());
        for(double x:{-data.negative.magnitude,0.,data.positive.magnitude})p.drawText(QRectF(screen(x,0).x()-35,box.bottom()+3,70,18),Qt::AlignCenter,QString::number(x,'f',1));
        for(double y:{-data.target_negative,0.,data.target_positive})p.drawText(QRectF(0,screen(0,y).y()-9,52,18),Qt::AlignRight,QString::number(y,'f',1));
        p.drawText(QRectF(8,height()-39,width()-16,38),Qt::AlignCenter|Qt::TextWordWrap,tr("Physical input (%1) → effective target (%1)\nSame ranges, targets and gain").arg(units));
    }
};

