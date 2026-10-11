#pragma once
#include "calibration-profile.hpp"
#include "options.hpp"
#include "head-controls.hpp"
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QTemporaryDir>
#include <QMap>

namespace enhanced::calibration {
inline double current_gain(const enhanced_settings& s,int axis) {
    const std::array<int,6> values{*s.amplification_x,*s.amplification_y,*s.amplification_z,*s.amplification_yaw,*s.amplification_pitch,*s.amplification_roll};
    return *s.head_enabled?amplification_gain(*s.per_axis?values[axis]:*s.amplification):1.;
}
struct target_readout { double physical=0, curve_input=0, curve=0, gain=1, amplified=0, bounded=0, final=0; bool saturated=false; };
inline target_readout preview_target(double physical,const spline& positive,const spline& negative,bool alt_enabled,
                                     bool invert_pre,bool invert_post,double zero,double gain,double limit,bool curves_enabled=true,bool controls_enabled=true) {
    target_readout r; r.physical=physical; r.curve_input=physical*(invert_pre?-1:1);
    r.curve=curves_enabled?(r.curve_input<0 && alt_enabled?negative:positive).get_value_no_save(r.curve_input):r.curve_input;
    r.gain=gain; r.amplified=r.curve*gain; r.bounded=controls_enabled?adjusted_mapping_output(r.curve,gain,limit):r.curve;
    r.saturated=std::abs(r.amplified-r.bounded)>1e-6;
    r.final=(r.bounded+zero*(invert_pre?-1:1))*(invert_post?-1:1);
    return r;
}
inline QString recommendations(const record& r,int axis,double target_positive,double target_negative,double gain,int speed) {
    const auto& a=r.axis[axis]; const auto& j=r.jitter; const QString unit=axis<3?"cm":"degrees";
    const double smaller=std::min(a.positive.magnitude,a.negative.magnitude);
    const double noise_ratio=smaller>0?j.p95[axis]/smaller:0;
    const auto style=noise_ratio>.015?response_style::balanced:response_style::natural;
    return QObject::tr("Deadzone: %1 %2, from 1.2 × max(3 × MAD, P95) and limited to 20% of your smaller comfortable range. "
        "Center sensitivity: start at 1.00 on each branch; your P95 deviation is %3% of the smaller range. "
        "Suggested style: %4 because %5. This is a starting preference, not a universal improvement.\n"
        "Targets: your requested −%6 / +%7 %2 over measured −%8 / +%9 %2 imply average sensitivities %10 / %11. "
        "Racing templates suggest targets; they do not measure game FOV or monitor placement.\n"
        "Amplification: 42% (1×) makes requested targets easy to interpret. Current gain %12× is compensated once in generated points. "
        "Speed: 55% preserves the existing filter response; the measured valid rate %13 Hz and reliability %14% do not establish optimal timing. "
        "Current Speed %15% affects timing separately, never the static curves.\n"
        "Optional Enhanced Adaptive Filter: jitter protection %16, rotation smoothing %17, translation smoothing %18. "
        "These conservative starting values use measured P95 rotation/translation deviation and the existing filter's stationary bands. "
        "Adjust them manually only if that filter is selected; deliberate movement cannot be diagnosed as sensor noise here.")
        .arg(std::min(j.deadzone[axis],smaller*.2),0,'f',3).arg(unit).arg(noise_ratio*100,0,'f',2)
        .arg(style_name(style)).arg(noise_ratio>.015?QObject::tr("a softer center can protect small motion relative to the measured noise envelope"):QObject::tr("the measured noise envelope is small relative to your range"))
        .arg(target_negative,0,'f',1).arg(target_positive,0,'f',1).arg(a.negative.magnitude,0,'f',2).arg(a.positive.magnitude,0,'f',2)
        .arg(target_negative/std::max(.001,a.negative.magnitude),0,'f',2).arg(target_positive/std::max(.001,a.positive.magnitude),0,'f',2)
        .arg(gain,0,'f',3).arg(j.valid_hz,0,'f',1).arg(j.reliability*100,0,'f',1).arg(speed)
        .arg([&] { double n=0;for(int i=0;i<6;++i)n=std::max(n,j.p95[i]/(i<3?.01875:.1));return std::clamp(n,0.,5.); }(),0,'f',2)
        .arg(std::clamp(1.+*std::max_element(j.p95.begin()+3,j.p95.end())*.5,1.,3.),0,'f',2)
        .arg(std::clamp(1.+*std::max_element(j.p95.begin(),j.p95.begin()+3),1.,3.),0,'f',2);
}
inline bool same_file(const QString& source,const QString& destination) {
    return QFileInfo(source).absoluteFilePath().compare(QFileInfo(destination).absoluteFilePath(),Qt::CaseInsensitive)==0;
}
inline void write_head_controls(QSettings& destination,const enhanced_settings& controls) {
    destination.setValue("enhanced-tracking/head-controls-enabled",*controls.head_enabled);
    destination.setValue("enhanced-tracking/head-amplification",*controls.amplification);
    destination.setValue("enhanced-tracking/head-speed",*controls.speed);
    destination.setValue("enhanced-tracking/head-per-axis",*controls.per_axis);
    const std::array<int,6> amplification{*controls.amplification_x,*controls.amplification_y,*controls.amplification_z,*controls.amplification_yaw,*controls.amplification_pitch,*controls.amplification_roll};
    const char* names[]={"x","y","z","yaw","pitch","roll"};
    for(int i=0;i<6;++i)destination.setValue(QString("enhanced-tracking/head-amplification-%1").arg(names[i]),amplification[i]);
}
inline bool save_selected_mapping(Mappings& mappings,const profile& calibration,const axis_selection& selected,const enhanced_settings& controls) {
    options::globals::detail::mark_ini_modified();
    const bool saved=options::globals::with_settings_object([&](QSettings& ini) {
        QMap<QString,QVariant> before;
        for(const auto& key:ini.allKeys())before.insert(key,ini.value(key));
        for(int i=0;i<6;++i)if(selected[i]) {
            const auto& map=mappings(i);const auto prefix=QString("opentrack-mappings/%1-").arg(map.opts.prefix());
            ini.setValue(map.name+"/points",QVariant::fromValue(map.spline_main.get_points()));
            ini.setValue(map.alt_name+"/points",QVariant::fromValue(map.spline_alt.get_points()));
            ini.setValue(prefix+"alt-axis-sign",*map.opts.altp);
            ini.setValue(prefix+"max-value",int(*map.opts.clamp_x_));ini.setValue(prefix+"max-output-value",int(*map.opts.clamp_y_));
        }
        ini.setValue("automatic-calibration/record",*calibration.data);ini.setValue("automatic-calibration/rollback",*calibration.rollback);ini.setValue("automatic-calibration/original",*calibration.original);
        write_head_controls(ini,controls);
        // QSettings instances for one file share their pending changes. Use a
        // fresh status for each attempt: an earlier I/O error is sticky.
        QSettings writer(ini.fileName(),ini.format());writer.sync();
        if(writer.status()==QSettings::NoError)return true;
        // Failed writes must not leave calibrated keys queued for a later sync.
        for(const auto& key:ini.allKeys())if(!before.contains(key))ini.remove(key);
        for(auto it=before.cbegin();it!=before.cend();++it)ini.setValue(it.key(),it.value());
        return false;
    });
    if(saved){auto& context=options::globals::detail::cur_settings();
        if(context.qsettings->status()!=QSettings::NoError){const auto path=context.qsettings->fileName();const auto format=context.qsettings->format();context.qsettings.emplace(path,format);}
        context.mtx.unlock();}
    return saved;
}
inline axis_selection snapshot_selection(const QString& snapshot) {
    axis_selection selected{};const auto axes=QJsonDocument::fromJson(snapshot.toUtf8()).object()["axes"].toArray();
    if(axes.size()==6)for(int i=0;i<6;++i)selected[i]=axes[i].toObject()["affected"].toBool();return selected;
}
// Full INI bytes use the same files/directory as OpenTrack's existing copied profiles.
// Atomic copying preserves unknown legacy plugin keys and never switches the active profile.
inline QString copy_profile_file(const QString& source,const QString& destination,bool overwrite=false) {
    if(same_file(source,destination)) return QObject::tr("Choose a different profile file.");
    if(QFileInfo(destination).suffix().compare("ini",Qt::CaseInsensitive)!=0) return QObject::tr("Profile files must use .ini.");
    if(QFile::exists(destination) && !overwrite) return QObject::tr("That profile already exists. Choose a new name.");
    QFile input(source); if(!input.open(QIODevice::ReadOnly)) return input.errorString();
    const auto bytes=input.readAll(); QSaveFile output(destination);
    if(!output.open(QIODevice::WriteOnly) || output.write(bytes)!=bytes.size() || !output.commit()) return output.errorString();
    return {};
}
inline QString save_new_mapping_profile(const QString& source,const QString& destination,Mappings& mappings,const record& r,const axis_selection& selected) {
    if(QFile::exists(destination) || same_file(source,destination)) return QObject::tr("Choose an unused profile name. The current profile is preserved.");
    record checked=r;for(int i=0;i<6;++i)checked.axis[i].calibrated &= selected[i];
    if(!decode(encode(checked))) return QObject::tr("The draft calibration record is invalid.");
    bool any=false;
    for(int i=0;i<6;++i) if(selected[i] && r.axis[i].calibrated) {
        const auto& a=r.axis[i]; spline p,n;
        if(!input_clamp(i,std::max(a.positive.magnitude,a.negative.magnitude)) || a.positive_points.empty() || a.negative_points.empty() || !verify_branch(p,a.positive_points) || !verify_branch(n,a.negative_points) ||
            std::max({a.target_positive,a.target_negative,a.positive_points.back().output,a.negative_points.back().output})>(i<3?600.:180.)) return QObject::tr("A selected curve is invalid or exceeds the output limit.");
        any=true;
    }
    if(!any) return QObject::tr("Select at least one calibrated axis.");
    QFile input(source); if(!input.open(QIODevice::ReadOnly)) return input.errorString();
    // QTemporaryFile retains its handle after close on Windows; QSettings cannot
    // atomically replace that locked file. A private temporary directory lets the
    // ordinary QFile close fully before QSettings writes the reviewed INI.
    QTemporaryDir temporary(QFileInfo(destination).absolutePath()+"/.opentrack-mapping-XXXXXX");
    if(!temporary.isValid()) return QObject::tr("Could not create a temporary profile directory.");
    const auto temporary_path=temporary.filePath("draft.ini");
    QFile staging(temporary_path);if(!staging.open(QIODevice::WriteOnly))return staging.errorString();
    const auto bytes=input.readAll();if(staging.write(bytes)!=bytes.size())return staging.errorString();staging.close();
    profile existing;
    const auto merged=merge_selected(r,decode(*existing.data),selected);
    record affected=r;for(int i=0;i<6;++i) affected.axis[i].calibrated &= selected[i];
    const auto rollback=snapshot(mappings,affected,existing);
    enhanced_settings controls;controls.set_raii_dtor_state(false);
    {
        QSettings destination_settings(temporary_path,QSettings::IniFormat);
        for(int i=0;i<6;++i) if(selected[i] && r.axis[i].calibrated) {
            const auto& a=r.axis[i]; const auto& map=mappings(i); const auto prefix=map.opts.prefix();
            destination_settings.setValue(map.name+"/points",QVariant::fromValue(qt_points(a.positive_points)));
            destination_settings.setValue(map.alt_name+"/points",QVariant::fromValue(qt_points(a.negative_points)));
            destination_settings.setValue("opentrack-mappings/"+prefix+"-alt-axis-sign",true);
            destination_settings.setValue("opentrack-mappings/"+prefix+"-max-value",input_clamp(i,std::max(a.positive.magnitude,a.negative.magnitude)));
            const double target=std::max({a.target_positive,a.target_negative,a.positive_points.back().output,a.negative_points.back().output});
            int out=180; if(i<3) {out=600;for(int v:{75,100,150,300,600}) if(target<=v){out=v;break;}}
            destination_settings.setValue("opentrack-mappings/"+prefix+"-max-output-value",-out);
        }
        destination_settings.setValue("automatic-calibration/record",encode(merged));
        destination_settings.setValue("automatic-calibration/rollback",rollback);
        destination_settings.setValue("automatic-calibration/original",original_with_axes(*existing.original,(*existing.rollback).isEmpty()?rollback:*existing.rollback,affected));
        // Pending Head Movement Controls are part of this reviewed mapping draft.
        // Copy only those controls so the saved curves use the previewed gain/timing;
        // preserve camera, inference, prediction and filter keys from the source INI.
        write_head_controls(destination_settings,controls);
        destination_settings.sync(); if(destination_settings.status()!=QSettings::NoError) return QObject::tr("Could not write the new INI profile.");
    }
    return copy_profile_file(temporary_path,destination);
}
} // namespace enhanced::calibration
