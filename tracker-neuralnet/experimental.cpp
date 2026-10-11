#include "ftnoir_tracker_neuralnet.h"
#include "enhanced/widgets.hpp"
#include <QPainter>
#include <QPainterPath>
#include <QFileDialog>
#include <QSaveFile>
#include <QTextStream>
#include <QPushButton>

namespace neuralnet_tracker_ns {
void NeuralNetTracker::process_flow(const cv::Mat& image, enhanced::timestamp time)
{
    flow_anchor anchor;
    { std::lock_guard lock(flow_mutex_); anchor=flow_anchor_; anchor.generation=flow_generation_.load(); }
    enhanced::flow_observation result;
    try { result=optical_flow_.update(image,time,anchor,*experimental_.flow_quality,*experimental_.flow_mode==1); }
    catch (const cv::Exception& e) { optical_flow_.reset(); qWarning()<<"Experimental optical flow:"<<e.what(); }
    { std::lock_guard lock(flow_mutex_); if (!flow_anchor_.valid || anchor.generation!=flow_generation_.load()) result.valid=false; }
    telemetry_.flow(result);
}

class AnalyzerWidget final : public QWidget
{
public:
    enhanced::analysis_snapshot data;
    explicit AnalyzerWidget(QWidget* parent=nullptr) : QWidget(parent) { setMinimumSize(440,1080); }
    void paintEvent(QPaintEvent*) override
    {
        QPainter p(this); p.setRenderHint(QPainter::Antialiasing);
        p.fillRect(rect(),palette().base());
        const QStringList titles={"Raw NeuralNet yaw / pitch / roll (degrees, absolute)",
            "Validated yaw / pitch / roll (degrees, absolute)","Filtered yaw / pitch / roll (degrees, centered before mapping)",
            "Predicted yaw / pitch / roll (degrees, centered before mapping)","Estimated tracking reliability (0–1)",
            "Flow correction to prediction yaw / pitch / roll (degrees)","Capture interval (ms)","ONNX inference duration (ms)","Host capture-to-pose processing latency (ms)"};
        const QColor colors[]={QColor(40,120,210),QColor(220,105,25),QColor(130,65,170)};
        auto value=[](const enhanced::analysis_sample& s,int chart,int axis)->std::optional<double> {
            switch(chart) {
            case 0: if(s.raw_valid) return s.raw[axis+3]; break;
            case 1: if(s.valid) return s.accepted[axis+3]; break;
            case 2: if(s.output_available) return s.filtered[axis+3]; break;
            case 3: if(s.output_available) return s.predicted[axis+3]; break;
            case 4: return s.reliability;
            case 5: if(s.output_available) return s.flow_correction[axis+3]; break;
            case 6: if(s.interval_ms>0) return s.interval_ms; break;
            case 7: return s.inference_ms;
            case 8: return s.processing_ms;
            } return {};
        };
        for(int chart=0;chart<9;++chart) {
            const int top=chart*120;
            p.setPen(palette().text().color()); p.drawText(8,top+16,titles[chart]);
            QRectF plot(50,top+28,width()-66,70); p.setPen(palette().mid().color()); p.drawRect(plot);
            if(data.samples.empty()) { p.drawText(plot,Qt::AlignCenter,"Enable analyzer and start tracking"); continue; }
            double low=chart==4?0:1e9, high=chart==4?1:-1e9;
            const int axes=chart<4 || chart==5?3:1;
            for(const auto& s:data.samples) for(int axis=0;axis<axes;++axis) if(auto v=value(s,chart,axis);v && std::isfinite(*v)) { low=std::min(low,*v);high=std::max(high,*v); }
            if(high<low) { p.setPen(palette().text().color());p.drawText(plot,Qt::AlignCenter,"No valid samples at this stage");continue; }
            if(high-low<.01) {high+=.5;low-=.5;}
            p.setPen(palette().text().color()); p.drawText(2,top+38,QString::number(high,'f',1));p.drawText(2,top+98,QString::number(low,'f',1));
            const auto begin=data.samples.front().captured, end=data.samples.back().captured;
            const double duration=std::max(1.,enhanced::milliseconds(begin,end));
            p.drawText(50,top+114,QString("Time since first visible sample (s): 0 … %1").arg(duration*.001,0,'f',2));
            for(int axis=0;axis<axes;++axis) {
                QPainterPath path; bool connected=false; double prev=0; std::uint64_t epoch=0;
                for(const auto& s:data.samples) {
                    auto v=value(s,chart,axis);
                    if(!v || !std::isfinite(*v)) {connected=false;continue;}
                    const QPointF point(plot.left()+plot.width()*enhanced::milliseconds(begin,s.captured)/duration,
                        plot.bottom()-plot.height()*(*v-low)/(high-low));
                    if(!connected || (chart<4 && (std::abs(*v-prev)>180 || epoch!=s.center_epoch))) path.moveTo(point); else path.lineTo(point);
                    connected=true;prev=*v;epoch=s.center_epoch;
                }
                p.setPen(QPen(colors[axis],1.4));p.drawPath(path);
            }
        }
    }
};

void NeuralNetDialog::refresh_analyzer()
{
    if(!tracker_ || !analyzer_widget_) return;
    tracker_->configure_analyzer(*experimental_.analyzer,*experimental_.recording,*experimental_.recording_seconds);
    if(!*experimental_.analyzer && !*experimental_.recording) {analyzer_widget_->hide();analyzer_status_->setText(tr("Analyzer disabled; a stopped recording remains available for export."));return;}
    auto data=tracker_->analysis();
    if(*experimental_.recording && !data.recording) experimental_.recording=false;
    analyzer_status_->setText(tr("Blue: yaw; orange: pitch; purple: roll. Dropped telemetry updates: %1; live ring replacements: %2. %3")
        .arg(qulonglong(data.dropped)).arg(qulonglong(data.overwritten)).arg(data.recording?tr("Recording"):tr("Recording stopped")));
    auto* widget=static_cast<AnalyzerWidget*>(analyzer_widget_);
    if(*experimental_.analyzer && widget->isVisible()) { widget->data=std::move(data);widget->update(); }
    widget->setVisible(*experimental_.analyzer);
}

// Called from the existing settings form; all consumers stay on the Qt thread.
void NeuralNetDialog::make_analyzer_controls(QFormLayout* form)
{
    enhanced_ui::check(form,tr("Live Tracking Analyzer"),experimental_.analyzer)->setObjectName("liveTrackingAnalyzer");
    enhanced_ui::check(form,tr("Record time series"),experimental_.recording)->setObjectName("recordTrackingAnalysis");
    enhanced_ui::number(form,tr("Recording duration (maximum 4096 samples)"),experimental_.recording_seconds,1,50,tr(" s"));
    analyzer_status_=new QLabel;analyzer_status_->setWordWrap(true);form->addRow(analyzer_status_);
    auto* export_button=new QPushButton(tr("Export Recording (CSV)"));export_button->setObjectName("exportTrackingRecording");form->addRow(export_button);
    connect(export_button,&QPushButton::clicked,this,[this] {
        if(!tracker_) return;
        const auto data=tracker_->analysis(true);
        const auto filename=QFileDialog::getSaveFileName(this,tr("Export tracking recording"),"tracking-recording.csv",tr("CSV files (*.csv)"));
        if(filename.isEmpty()) return;
        QSaveFile file(filename);if(!file.open(QIODevice::WriteOnly|QIODevice::Text)) return;
        QTextStream stream(&file);stream.setRealNumberPrecision(10);
        stream<<"capture_monotonic_ms,validated_monotonic_ms,published_monotonic_ms,output_monotonic_ms,raw_valid,pose_valid,holding,output_available,estimated_reliability,capture_interval_ms,inference_ms,processing_ms,flow_valid,flow_timestamp_ms,flow_anchor_ms,flow_dx_px,flow_dy_px,flow_speed_px_s,flow_reliability,flow_drift_px,flow_cpu_ms,flow_points,flow_rejected,center_epoch,dropped_telemetry_updates,live_ring_replacements";
        for(const auto* stage:{"raw","validated","filtered","predicted","flow_correction"}) for(const auto* axis:{"x_cm","y_cm","z_cm","yaw_deg","pitch_deg","roll_deg"}) stream<<','<<stage<<'_'<<axis;
        stream<<'\n';
        auto stamp=[](enhanced::timestamp t){return t==enhanced::timestamp{}?QString():QString::number(enhanced::milliseconds({},t),'f',4);};
        for(const auto& s:data.samples) {
            stream<<stamp(s.captured)<<','<<stamp(s.validated)<<','<<stamp(s.published)<<','<<stamp(s.output)<<','<<s.raw_valid<<','<<s.valid<<','<<s.holding<<','<<s.output_available<<','
                <<s.reliability<<','<<s.interval_ms<<','<<s.inference_ms<<','<<s.processing_ms<<','<<s.flow.valid<<','<<stamp(s.flow.captured)<<','<<stamp(s.flow.anchor)<<','
                <<s.flow.dx<<','<<s.flow.dy<<','<<s.flow.speed_px_s<<','<<s.flow.reliability<<','<<s.flow.drift_px<<','<<s.flow.cpu_ms<<','<<s.flow.points<<','<<s.flow.rejected<<','
                <<qulonglong(s.center_epoch)<<','<<qulonglong(data.dropped)<<','<<qulonglong(data.overwritten);
            int stage=0;
            for(const auto& pose:{s.raw,s.accepted,s.filtered,s.predicted,s.flow_correction}) {
                const bool valid=stage==0?s.raw_valid:stage==1?s.valid:s.output_available;
                for(double v:pose) {stream<<',';if(valid && std::isfinite(v)) stream<<v;} ++stage;
            } stream<<'\n';
        }
        stream.flush();
        if(!file.commit()) qWarning()<<"Could not export tracking recording:"<<file.errorString();
    });
    analyzer_widget_=new AnalyzerWidget;form->addRow(analyzer_widget_);analyzer_widget_->setVisible(false);
}
}
