#pragma once
#include "calibration.hpp"
#include "logic/mappings.hpp"
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

namespace enhanced::calibration {
struct profile : options::opts {
    options::value<QString> data{b,"record",{}};
    options::value<QString> rollback{b,"rollback",{}};
    options::value<QString> original{b,"original",{}};
    // The Mapping dialog owns save/cancel. Helper instances must not perform the
    // plugin-dialog RAII reload when they leave scope after application.
    profile() : opts("automatic-calibration") { set_raii_dtor_state(false); }
};
inline QJsonArray json_axes(const axes& values) { QJsonArray a; for (double v:values) a.append(v); return a; }
inline bool read_axes(const QJsonValue& value, axes& result) {
    const auto a=value.toArray(); if (a.size()!=6) return false;
    for (int i=0;i<6;++i) { if (!a[i].isDouble() || !std::isfinite(a[i].toDouble())) return false; result[i]=a[i].toDouble(); }
    return true;
}
inline QJsonArray json_points(const std::vector<point>& points) {
    QJsonArray a; for (const auto& p:points) a.append(QJsonArray{p.input,p.output}); return a;
}
inline QJsonArray json_points(const QList<QPointF>& points) {
    QJsonArray a; for (const auto& p:points) a.append(QJsonArray{p.x(),p.y()}); return a;
}
inline std::optional<QList<QPointF>> read_points(const QJsonValue& value) {
    if (!value.isArray() || value.toArray().size()>4096) return {};
    QList<QPointF> result;
    for (const auto& entry:value.toArray()) {
        const auto p=entry.toArray(); if (p.size()!=2 || !p[0].isDouble() || !p[1].isDouble()) return {};
        const double x=p[0].toDouble(), y=p[1].toDouble();
        if (!std::isfinite(x) || !std::isfinite(y) || x<0 || x>1000 || y<0 || y>1000) return {};
        result.append({x,y});
    }
    return result;
}
inline QList<QPointF> qt_points(const std::vector<point>& points) {
    QList<QPointF> result; for (const auto& p:points) result.append({p.input,p.output}); return result;
}
inline QJsonObject json_range(const range& r) {
    return {{"magnitude",r.magnitude},{"reliability",r.reliability},{"seconds",r.seconds},{"samples",int(r.samples)},{"comfortable",r.comfort_confirmed}};
}
inline range read_range(const QJsonObject& o) {
    return {o["magnitude"].toDouble(),o["reliability"].toDouble(),o["seconds"].toDouble(),unsigned(std::max(0,o["samples"].toInt())),o["comfortable"].toBool()};
}
inline QString encode(const record& r) {
    QJsonArray a;
    for (const auto& x:r.axis) a.append(QJsonObject{{"calibrated",x.calibrated},{"positive",json_range(x.positive)},{"negative",json_range(x.negative)},
        {"deadzone",x.deadzone},{"shape",x.shape},{"target-positive",x.target_positive},{"target-negative",x.target_negative},
        {"style",int(x.style)},{"sensitivity-positive",x.sensitivity_positive},{"sensitivity-negative",x.sensitivity_negative},
        {"output-gain",x.output_gain},{"manual-points",x.manual_points},
        {"positive-points",json_points(x.positive_points)},{"negative-points",json_points(x.negative_points)}});
    const auto& j=r.jitter;
    const QJsonObject noise{{"median",json_axes(j.median)},{"mad",json_axes(j.mad)},{"p95",json_axes(j.p95)},{"drift",json_axes(j.drift)},
        {"deadzone",json_axes(j.deadzone)},{"interval-p05-ms",j.interval_p05_ms},{"interval-p50-ms",j.interval_p50_ms},{"interval-p95-ms",j.interval_p95_ms},
        {"valid-hz",j.valid_hz},{"reliability",j.reliability},{"seconds",j.valid_seconds},{"samples",int(j.valid_samples)},
        {"rejected",int(j.rejected_samples)},{"ambiguous-motion",j.ambiguous_motion}};
    return QString::fromUtf8(QJsonDocument(QJsonObject{{"schema",record::schema},{"date",QString::fromStdString(r.date)},
        {"use-case",QString::fromStdString(r.use_case)},{"axes",a},{"stationary",noise}}).toJson(QJsonDocument::Compact));
}
inline std::optional<record> decode(const QString& text) {
    const auto o=QJsonDocument::fromJson(text.toUtf8()).object();
    const int version=o["schema"].toInt();
    if ((version!=1 && version!=record::schema) || o["axes"].toArray().size()!=6) return {};
    record r; r.date=o["date"].toString().toStdString(); r.use_case=o["use-case"].toString().toStdString();
    const auto j=o["stationary"].toObject();
    if (!read_axes(j["median"],r.jitter.median) || !read_axes(j["mad"],r.jitter.mad) || !read_axes(j["p95"],r.jitter.p95) ||
        !read_axes(j["drift"],r.jitter.drift) || !read_axes(j["deadzone"],r.jitter.deadzone)) return {};
    r.jitter.interval_p05_ms=j["interval-p05-ms"].toDouble(); r.jitter.interval_p50_ms=j["interval-p50-ms"].toDouble(); r.jitter.interval_p95_ms=j["interval-p95-ms"].toDouble();
    r.jitter.valid_hz=j["valid-hz"].toDouble(); r.jitter.reliability=j["reliability"].toDouble(); r.jitter.valid_seconds=j["seconds"].toDouble();
    r.jitter.valid_samples=unsigned(std::max(0,j["samples"].toInt())); r.jitter.rejected_samples=unsigned(std::max(0,j["rejected"].toInt())); r.jitter.ambiguous_motion=j["ambiguous-motion"].toBool();
    if(r.jitter.valid_hz<0 || r.jitter.reliability<0 || r.jitter.reliability>1 || r.jitter.valid_seconds<0 ||
       r.jitter.interval_p05_ms<0 || r.jitter.interval_p50_ms<r.jitter.interval_p05_ms || r.jitter.interval_p95_ms<r.jitter.interval_p50_ms) return {};
    for(int i=0;i<6;++i)if(r.jitter.mad[i]<0 || r.jitter.p95[i]<0 || r.jitter.deadzone[i]<0)return {};
    for (int i=0;i<6;++i) {
        const auto a=o["axes"].toArray()[i].toObject(); auto& x=r.axis[i];
        x.positive=read_range(a["positive"].toObject()); x.negative=read_range(a["negative"].toObject());
        x.deadzone=a["deadzone"].toDouble(); x.shape=a["shape"].toDouble(); x.target_positive=a["target-positive"].toDouble(); x.target_negative=a["target-negative"].toDouble();
        x.style=response_style(a["style"].toInt()); x.sensitivity_positive=a["sensitivity-positive"].toDouble(1);
        x.sensitivity_negative=a["sensitivity-negative"].toDouble(1); x.output_gain=a["output-gain"].toDouble(1);
        const bool manual=a["manual-points"].toBool();
        const bool calibrated=a["calibrated"].toBool();
        if (calibrated) {
            if (!generate(x)) return {};
            // Validate stored controls and preserve them exactly across schema-compatible loads.
            const auto positive=read_points(a["positive-points"]), negative=read_points(a["negative-points"]);
            if (!positive || !negative || positive->size()<2 || negative->size()<2) return {};
            if (!manual && (positive->size()!=qsizetype(x.positive_points.size()) || negative->size()!=qsizetype(x.negative_points.size()))) return {};
            for (int branch=0;branch<2;++branch) {
                const auto& p=branch?*negative:*positive; auto& expected=branch?x.negative_points:x.positive_points;
                if (manual) {
                    if (p.front()!=QPointF(0,0) || p.back().x()>(branch?x.negative.magnitude:x.positive.magnitude)+1e-6) return {};
                    expected.clear();
                    for (qsizetype k=0;k<p.size();++k) {
                        if (k && (p[k].x()<=p[k-1].x() || p[k].y()<p[k-1].y())) return {};
                        expected.push_back({p[k].x(),p[k].y()});
                    }
                } else for (qsizetype k=0;k<p.size();++k) if (std::abs(p[k].x()-expected[k].input)>1e-8 || std::abs(p[k].y()-expected[k].output)>1e-8) return {};
            }
            x.manual_points=manual;
        }
    }
    return r;
}

// Preview and validation use OpenTrack's actual Catmull-Rom/bucket evaluator.
inline void private_spline(spline& value) { value.set_bundle(std::make_shared<options::bundle_>(QString()),{},NonAxis); }
inline bool verify_branch(spline& value, const std::vector<point>& points) {
    private_spline(value); value.set_points(qt_points(points));
    if (points.empty()) return false;
    double previous=0, previous_negative=0;
    for (int k=0;k<=8192;++k) {
        const double x=points.back().input*k/8192, y=value.get_value_no_save(x);
        const double negative=value.get_value_no_save(-x);
        if (!std::isfinite(y) || y+1e-7<previous || y<0 || y>points.back().output+1e-6 || !std::isfinite(negative) ||
            negative>1e-7 || negative>previous_negative+1e-6 || negative<-points.back().output-.05) return false;
        previous=y; previous_negative=negative;
    }
    // The original evaluator samples into fixed buckets and extends its last
    // interior bucket. Bound the endpoint error by its resolution and max slope.
    const double tolerance=std::max(.05,4*points.back().output/(16383*std::min(1.,points.back().input)));
    return std::abs(value.get_value_no_save(0))<1e-9 && std::abs(previous-points.back().output)<tolerance;
}
inline int input_clamp(int axis, double range_value) {
    for (int value : (axis<3 ? std::vector<int>{10,15,20,30,75,100,150,300,600} : std::vector<int>{10,15,20,25,30,45,60,90,180}))
        if (range_value<=value) return value;
    return 0;
}
inline QString snapshot(Mappings& mappings, const record& r, const profile& p) {
    QJsonArray a;
    for (int i=0;i<6;++i) {
        const auto& m=mappings(i);
        a.append(QJsonObject{{"affected",r.axis[i].calibrated},{"main",json_points(m.spline_main.get_points())},{"alt",json_points(m.spline_alt.get_points())},
            {"alt-enabled",*m.opts.altp},{"clamp-x",int(*m.opts.clamp_x_)},{"clamp-y",int(*m.opts.clamp_y_)}});
    }
    return QString::fromUtf8(QJsonDocument(QJsonObject{{"schema",1},{"axes",a},{"record",*p.data}}).toJson(QJsonDocument::Compact));
}
// Caller stops tracking using its normal lifecycle before mutation. These operations
// stay on the UI thread; no disk I/O or spline rebuild is inserted into tracking.
using axis_selection=std::array<bool,6>;
inline constexpr axis_selection all_axes{true,true,true,true,true,true};
inline record merge_selected(const record& update,const std::optional<record>& prior,const axis_selection& selected) {
    record result=prior.value_or(update);
    for(int i=0;i<6;++i) if(selected[i] && update.axis[i].calibrated) result.axis[i]=update.axis[i];
    if(!prior) for(int i=0;i<6;++i) if(!selected[i]) result.axis[i].calibrated=false;
    result.date=update.date; result.use_case=update.use_case; result.jitter=update.jitter;
    return result;
}
inline QString original_with_axes(QString original,const QString& fallback,const record& affected) {
    if(original.isEmpty())original=fallback;
    auto object=QJsonDocument::fromJson(original.toUtf8()).object();auto axes=object["axes"].toArray();
    if(axes.size()!=6)return original;
    for(int i=0;i<6;++i)if(affected.axis[i].calibrated){auto axis=axes[i].toObject();axis["affected"]=true;axes[i]=axis;}
    object["axes"]=axes;return QString::fromUtf8(QJsonDocument(object).toJson(QJsonDocument::Compact));
}
inline bool apply(Mappings& mappings, const record& r, profile& p, bool confirmed,const axis_selection& selected=all_axes) {
    record checked=r;for(int i=0;i<6;++i)checked.axis[i].calibrated &= selected[i];
    if (!confirmed || !decode(encode(checked))) return false;
    bool any=false;
    for (int i=0;i<6;++i) if (selected[i] && r.axis[i].calibrated) {
        const auto& a=r.axis[i]; spline positive,negative;
        if (!input_clamp(i,std::max(a.positive.magnitude,a.negative.magnitude)) ||
            std::max({a.target_positive,a.target_negative,a.positive_points.back().output,a.negative_points.back().output})>(i<3?600.:180.) ||
            !verify_branch(positive,a.positive_points) || !verify_branch(negative,a.negative_points)) return false;
        any=true;
    }
    if (!any) return false;
    record affected=r; for(int i=0;i<6;++i) affected.axis[i].calibrated &= selected[i];
    const auto previous=snapshot(mappings,affected,p);
    p.original=original_with_axes(*p.original,(*p.rollback).isEmpty()?previous:*p.rollback,affected);
    const auto merged=merge_selected(r,decode(*p.data),selected);
    p.rollback=previous;
    for (int i=0;i<6;++i) if (selected[i] && r.axis[i].calibrated) {
        auto& m=mappings(i); const auto& a=r.axis[i];
        m.opts.clamp_x_=axis_opts::max_clamp(input_clamp(i,std::max(a.positive.magnitude,a.negative.magnitude)));
        const double target=std::max({a.target_positive,a.target_negative,a.positive_points.back().output,a.negative_points.back().output});
        int out=180;
        if (i<3) { out=600; for (int v:{75,100,150,300,600}) if (target<=v) { out=v; break; } }
        m.opts.clamp_y_=axis_opts::max_clamp(-out);
        m.spline_main.set_points(qt_points(a.positive_points)); m.spline_alt.set_points(qt_points(a.negative_points)); m.opts.altp=true;
    }
    p.data=encode(merged);
    return true;
}
inline bool restore(Mappings& mappings, profile& p, bool confirmed) {
    if (!confirmed) return false;
    const auto o=QJsonDocument::fromJson((*p.rollback).toUtf8()).object(); const auto a=o["axes"].toArray();
    if (o["schema"].toInt()!=1 || a.size()!=6) return false;
    // Validate the entire snapshot before changing any setting.
    for (int i=0;i<6;++i) {
        const auto x=a[i].toObject();
        if (!read_points(x["main"]) || !read_points(x["alt"]) || x["clamp-x"].toInt()==0 || x["clamp-y"].toInt()==0) return false;
    }
    for (int i=0;i<6;++i) {
        const auto x=a[i].toObject(); if (!x["affected"].toBool()) continue;
        auto& m=mappings(i);
        m.opts.clamp_x_=axis_opts::max_clamp(x["clamp-x"].toInt()); m.opts.clamp_y_=axis_opts::max_clamp(x["clamp-y"].toInt());
        m.spline_main.set_points(*read_points(x["main"])); m.spline_alt.set_points(*read_points(x["alt"])); m.opts.altp=x["alt-enabled"].toBool();
    }
    p.data=o["record"].toString(); p.rollback=QString{};
    return true;
}
inline bool restore_original(Mappings& mappings,profile& p,bool confirmed) {
    if(!confirmed || (*p.original).isEmpty()) return false;
    const auto original=*p.original, previous_undo=*p.rollback;
    record affected;
    const auto axes=QJsonDocument::fromJson(original.toUtf8()).object()["axes"].toArray();
    if(axes.size()!=6) return false;
    for(int i=0;i<6;++i) affected.axis[i].calibrated=axes[i].toObject()["affected"].toBool();
    const auto undo=snapshot(mappings,affected,p);
    p.rollback=original;
    if(!restore(mappings,p,true)) {p.rollback=previous_undo;return false;}
    p.rollback=undo;
    return true;
}
} // namespace enhanced::calibration
