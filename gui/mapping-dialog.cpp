/* Copyright (c) 2014-2015, Stanislaw Halik <sthalik@misaki.pl>

 * Permission to use, copy, modify, and/or distribute this
 * software for any purpose with or without fee is hereby granted,
 * provided that the above copyright notice and this permission
 * notice appear in all copies.
 */

#include "mapping-dialog.hpp"
#include "logic/main-settings.hpp"
#include "spline/spline-widget.hpp"
#include "enhanced/head-controls-ui.hpp"
#include "smart-mapping-dialog.hpp"
#include <QGroupBox>
#include <QVBoxLayout>
#include <QPushButton>
#include <QMessageBox>

#include <QtEvents>

using namespace options;

mapping_dialog::mapping_dialog(Mappings& m) : m(m), widgets{}
{
    ui.setupUi(this);
    ui.verticalLayout->setContentsMargins(6,6,6,6);
    ui.verticalLayout->setSpacing(4);

    QWidget* pages[] = {
        ui.tabWidgetPage1,
        ui.tabWidgetPage2,
        ui.tabWidgetPage3,
        ui.tabWidgetPage4,
        ui.tabWidgetPage5,
        ui.tabWidgetPage6,
    };

    {
        QColor bg = palette().window().color();

        QString tmp;
        tmp.reserve(32);

        tmp += QStringLiteral(".QWidget { background-color: rgb(%1, %2, %3); } ")
            .arg(bg.red()).arg(bg.green()).arg(bg.blue());

        for (QWidget* w : pages)
        {
            w->setStyleSheet(tmp);
            w->layout()->setContentsMargins(6,6,6,6);
            w->layout()->setSpacing(4);
        }
    }

    load();

    auto* controls=new QGroupBox(tr("Head Movement Controls"),this);
    controls->setObjectName("mappingHeadControls");
    auto* form=new QFormLayout(controls);
    form->setContentsMargins(6,6,6,6); form->setSpacing(3);
    enhanced_ui::head_movement(form,enhanced_opts,[this] { return head_diagnostics ? head_diagnostics() : std::optional<enhanced::head_controls_snapshot>{}; },
        [this] { const int tab=ui.tabWidget->currentIndex(); return tab<3?tab+3:tab-3; },
        [this](int axis) { return this->m(axis).opts.max_clamp_y(); });
    ui.verticalLayout->insertWidget(0,controls);
    auto* calibration_row=new QHBoxLayout;
    auto* calibrate=new QPushButton(tr("Automatic calibration…"),this); calibrate->setObjectName("automaticCalibration");
    auto* smart=new QPushButton(tr("Smart Mapping…"),this);smart->setObjectName("smartMapping");
    auto* rollback=new QPushButton(tr("Undo calibration / previous mapping"),this); rollback->setObjectName("calibrationRollback");
    auto* original=new QPushButton(tr("Restore original mapping"),this);original->setObjectName("mappingRestoreOriginal");
    auto* reset=new QPushButton(tr("Reset this axis to previous state"),this);reset->setObjectName("mappingResetAxis");
    calibration_row->addWidget(calibrate);calibration_row->addWidget(smart); calibration_row->addWidget(rollback); ui.verticalLayout->insertLayout(1,calibration_row);
    auto* restore_row=new QHBoxLayout;restore_row->addWidget(reset);restore_row->addWidget(original);ui.verticalLayout->insertLayout(2,restore_row);
    connect(calibrate,&QPushButton::clicked,this,[this] {
        // Commit any pending manual edits only after the user accepts the generated curves.
        calibration_dialog dialog(this->m,calibration_samples,calibration_stop_tracking,calibration_center_tracking,this);
        dialog.new_profile_path=new_mapping_profile_path;
        dialog.save_applied=[this](const enhanced::calibration::axis_selection& axes){return enhanced::calibration::save_selected_mapping(this->m,calibration_profile,axes,enhanced_opts);};
        if (dialog.exec()==QDialog::Accepted) {if(dialog.applied_to_current() && !dialog.persisted_to_current() && !enhanced::calibration::save_selected_mapping(this->m,calibration_profile,dialog.selected_axes(),enhanced_opts))QMessageBox::warning(this,tr("Profile"),tr("The profile could not be saved. Retry saving from Mapping."));if(profiles_changed)profiles_changed();}
        refresh_tab();
    });
    connect(smart,&QPushButton::clicked,this,[this] {
        smart_mapping_dialog dialog(this->m,calibration_stop_tracking,new_mapping_profile_path,this);
        if(dialog.exec()==QDialog::Accepted){if(dialog.applied_to_current() && !enhanced::calibration::save_selected_mapping(this->m,calibration_profile,dialog.selected_axes(),enhanced_opts))QMessageBox::warning(this,tr("Profile"),tr("The profile could not be saved. Retry saving from Mapping."));if(profiles_changed)profiles_changed();}refresh_tab();
    });
    connect(reset,&QPushButton::clicked,this,[this] {
        const int tab=ui.tabWidget->currentIndex(),i=tab<3?tab+3:tab-3;
        const auto axes=QJsonDocument::fromJson(previous_mapping.toUtf8()).object()["axes"].toArray();if(axes.size()!=6)return;
        if(calibration_stop_tracking && !calibration_stop_tracking())return;
        const auto a=axes[i].toObject();auto& map=this->m(i);
        map.opts.clamp_x_=axis_opts::max_clamp(a["clamp-x"].toInt());map.opts.clamp_y_=axis_opts::max_clamp(a["clamp-y"].toInt());map.opts.altp=a["alt-enabled"].toBool();
        map.spline_main.set_points(*enhanced::calibration::read_points(a["main"]));map.spline_alt.set_points(*enhanced::calibration::read_points(a["alt"]));refresh_tab();
    });
    connect(original,&QPushButton::clicked,this,[this] {
        if((*calibration_profile.original).isEmpty()){QMessageBox::information(this,tr("Mapping"),tr("This profile has no original mapping snapshot."));return;}
        if(QMessageBox::question(this,tr("Restore original mapping"),tr("Restore all mapping axes captured before calibration? Tracking will stop and this profile will be saved."),QMessageBox::Yes|QMessageBox::No,QMessageBox::No)!=QMessageBox::Yes)return;
        if(!calibration_stop_tracking || !calibration_stop_tracking())return;
        const auto selected=enhanced::calibration::snapshot_selection(*calibration_profile.original);
        if(enhanced::calibration::restore_original(this->m,calibration_profile,true)) {
            if(!enhanced::calibration::save_selected_mapping(this->m,calibration_profile,selected,enhanced_opts))QMessageBox::warning(this,tr("Profile"),tr("The restored profile could not be saved. Retry saving from Mapping."));
        }else QMessageBox::warning(this,tr("Mapping"),tr("The original snapshot is invalid. Mappings were not changed."));refresh_tab();
    });
    connect(rollback,&QPushButton::clicked,this,[this] {
        if ((*calibration_profile.rollback).isEmpty()) { QMessageBox::information(this,tr("Calibration"),tr("This profile has no calibration rollback snapshot.")); return; }
        if (QMessageBox::question(this,tr("Restore calibration"),tr("Restore the previous calibrated mapping branches and their limits? Tracking will stop and the restored profile will be saved."),QMessageBox::Yes|QMessageBox::No,QMessageBox::No)!=QMessageBox::Yes) return;
        if (!calibration_stop_tracking || !calibration_stop_tracking()) return;
        const auto selected=enhanced::calibration::snapshot_selection(*calibration_profile.rollback);
        if (enhanced::calibration::restore(this->m,calibration_profile,true)) {
            if(!enhanced::calibration::save_selected_mapping(this->m,calibration_profile,selected,enhanced_opts))QMessageBox::warning(this,tr("Profile"),tr("The restored profile could not be saved. Retry saving from Mapping."));
        }
        else QMessageBox::warning(this,tr("Calibration"),tr("The rollback snapshot is invalid. Mappings were not changed."));
        refresh_tab();
    });
    auto* legend=new QLabel(tr("Dashed: original curve. Orange: adjusted target, capped at Max output. Points edit the original."));
    legend->setToolTip(tr("Speed controls how quickly output reaches the adjusted target."));
    legend->setWordWrap(true); ui.verticalLayout->insertWidget(1,legend);
    const auto changed=[this](auto) { refresh_mapping_controls(); };
    options::tie_setting(enhanced_opts.head_enabled,this,changed);
    options::tie_setting(enhanced_opts.amplification,this,changed);
    options::tie_setting(enhanced_opts.per_axis,this,changed);
    for (auto* value : { &enhanced_opts.amplification_x,&enhanced_opts.amplification_y,&enhanced_opts.amplification_z,
                        &enhanced_opts.amplification_yaw,&enhanced_opts.amplification_pitch,&enhanced_opts.amplification_roll })
        options::tie_setting(*value,this,changed);
    connect(&preview_timer,&QTimer::timeout,this,&mapping_dialog::refresh_tab); preview_timer.start(100);
    refresh_mapping_controls();

    tie_setting(s.a_yaw.altp, ui.rx_altp);
    tie_setting(s.a_pitch.altp, ui.ry_altp);
    tie_setting(s.a_roll.altp, ui.rz_altp);
    tie_setting(s.a_x.altp, ui.tx_altp);
    tie_setting(s.a_y.altp, ui.ty_altp);
    tie_setting(s.a_z.altp, ui.tz_altp);

    tie_setting(s.a_yaw.clamp_x_, ui.max_yaw_rotation);
    tie_setting(s.a_pitch.clamp_x_, ui.max_pitch_rotation);
    tie_setting(s.a_roll.clamp_x_, ui.max_roll_rotation);
    tie_setting(s.a_x.clamp_x_, ui.max_x_translation);
    tie_setting(s.a_y.clamp_x_, ui.max_y_translation);
    tie_setting(s.a_z.clamp_x_, ui.max_z_translation);

    tie_setting(s.a_x.clamp_y_, ui.max_x_out);
    tie_setting(s.a_y.clamp_y_, ui.max_y_out);
    tie_setting(s.a_z.clamp_y_, ui.max_z_out);

    tie_setting(s.a_pitch.clamp_y_, ui.max_pitch_output);

    connect(ui.buttonBox, &QDialogButtonBox::accepted, this, &mapping_dialog::accept);
    connect(ui.buttonBox, &QDialogButtonBox::rejected, this, &mapping_dialog::reject);
    connect(this, &mapping_dialog::accepted, this, &mapping_dialog::doAccept);
    connect(this, &mapping_dialog::rejected, this, &mapping_dialog::doReject);
}

void mapping_dialog::refresh_mapping_controls()
{
    const std::array<int,6> percentages{*enhanced_opts.amplification_yaw,*enhanced_opts.amplification_pitch,*enhanced_opts.amplification_roll,
        *enhanced_opts.amplification_x,*enhanced_opts.amplification_y,*enhanced_opts.amplification_z};
    for (int tab=0;tab<6;++tab) for (auto* widget:widgets[tab])
        if (widget) widget->set_mapping_overlay(*enhanced_opts.head_enabled,
            enhanced::amplification_gain(*enhanced_opts.per_axis ? percentages[tab] : *enhanced_opts.amplification));
}

void mapping_dialog::load()
{
    struct {
        spline_widget* qfc;
        Axis axis;
        QCheckBox* checkbox;
        bool altp;
    } qfcs[] =
    {
        { ui.rxconfig, Yaw,   nullptr, false, },
        { ui.ryconfig, Pitch, nullptr, false, },
        { ui.rzconfig, Roll,  nullptr, false, },
        { ui.txconfig, TX,    nullptr, false, },
        { ui.tyconfig, TY,    nullptr, false, },
        { ui.tzconfig, TZ,    nullptr, false, },
        { ui.rxconfig_alt, Yaw,   ui.rx_altp, true, },
        { ui.ryconfig_alt, Pitch, ui.ry_altp, true, },
        { ui.rzconfig_alt, Roll,  ui.rz_altp, true, },
        { ui.txconfig_alt, TX,    ui.tx_altp, true, },
        { ui.tyconfig_alt, TY,    ui.ty_altp, true, },
        { ui.tzconfig_alt, TZ,    ui.tz_altp, true, },
        { nullptr, Yaw, nullptr, false }
    };

    using a = axis_opts::max_clamp;

    ui.max_pitch_output->setItemData(0, int(a::o_r180));
    ui.max_pitch_output->setItemData(1, int(a::o_r90));

    for (QComboBox* x : { ui.max_yaw_rotation, ui.max_pitch_rotation, ui.max_roll_rotation })
        for (a y : { a::r180, a::r90, a::r60, a::r45, a::r30, a::r25, a::r20, a::r15, a::r10 })
            x->addItem(tr("%1°").arg(y), y);

    for (QComboBox* x : { ui.max_x_translation, ui.max_y_translation, ui.max_z_translation })
        for (a y : { a::t30, a::t20, a::t15, a::t10, a::t75, a::t100, a::t150, a::t300, a::t600 })
            x->addItem(tr("%1 cm").arg(int(y)), y);

    for (QComboBox* x : { ui.max_x_out, ui.max_y_out, ui.max_z_out })
        for (a y : { a::o_t75, a::o_t100, a::o_t150, a::o_t300, a::o_t600 })
            x->addItem(tr("%1 cm").arg(abs(int(y))), y);

    for (int i = 0; qfcs[i].qfc; i++)
    {
        const bool altp = qfcs[i].altp;

        Map& axis = m(qfcs[i].axis);
        spline& conf = altp ? axis.spline_alt : axis.spline_main;
        spline_widget& qfc = *qfcs[i].qfc;
        // Smaller minima here keep other spline users' sizing unchanged. Qt
        // scales these logical pixel dimensions with the desktop's DPI setting.
        qfc.setMinimumSize(560,140);

        if (altp)
        {
            connect(&axis.opts.altp,
                    value_::value_changed<bool>(),
                    this, [&](bool f) { qfc.setEnabled(f); });
            qfc.setEnabled(axis.opts.altp);
        }

        auto update_xstep = [&qfc](int clamp_x) {
            int value;

            if (clamp_x <= a::r30)
                value = 1;
            else if (clamp_x <= a::r45)
                value = 5;
            else if (clamp_x <= a::t150)
                value = 10;
            else if (clamp_x <= a::t300)
                value = 25;
            else
                value = 50;

            qfc.set_x_step(value);
        };

        auto update_ystep = [&qfc](int clamp_y) {
            int value;
            switch (clamp_y)
            {
            default:
            case a::o_r180:
                value = 20; break;
            case a::o_r90:
                value = 10; break;
            case a::o_t75:
                value = 5; break;
            case a::o_t100:
            case a::o_t150:
                value = 10; break;
            case a::o_t300:
                value = 50; break;
            case a::o_t600:
                value = 100; break;
            }
            qfc.set_y_step(value);
        };

        qfc.set_snap(.5, 1);

        connect(&axis.opts.clamp_x_, value_::value_changed<int>(), &qfc, update_xstep);
        connect(&axis.opts.clamp_y_, value_::value_changed<int>(), &qfc, update_ystep);

        // force signal to avoid duplicating the slot's logic
        qfc.set_config(&conf);

        update_xstep(axis.opts.clamp_x_);
        update_ystep(axis.opts.clamp_y_);

        widgets[i % 6][altp ? 1 : 0] = &qfc;
    }
}

void mapping_dialog::refresh_tab()
{
    if (!isVisible())
        return;

    const int idx = ui.tabWidget->currentIndex();

    if (idx >= 0 && idx < 6) [[likely]]
    {
        widgets[idx][0]->repaint();
        widgets[idx][1]->repaint();
    }
    else
        qDebug() << "mapping_dialog: bad index" << idx;
}

void mapping_dialog::save_dialog()
{
    calibration_profile.b->save();
    enhanced_opts.b->save();
    s.b_map->save();

    m.forall([](Map& axis)
    {
        axis.spline_main.save();
        axis.spline_alt.save();
        axis.opts.b_mapping_window->save();
    });
}

void mapping_dialog::invalidate_dialog()
{
    calibration_profile.b->reload();
    enhanced_opts.b->reload();
    s.b_map->reload();

    m.forall([](Map& s)
    {
        s.spline_main.reload();
        s.spline_alt.reload();
        s.opts.b_mapping_window->reload();
    });
}

void mapping_dialog::closeEvent(QCloseEvent *)
{
    qDebug() << "mapping_dialog: closeEvent";
    reject();
}
void mapping_dialog::showEvent(QShowEvent* event)
{
    enhanced::calibration::record affected;for(auto& a:affected.axis)a.calibrated=true;
    previous_mapping=enhanced::calibration::snapshot(m,affected,calibration_profile);
    QDialog::showEvent(event);
}

void mapping_dialog::doOK()
{
    qDebug() << "mapping_dialog: doOK";
    accept();
}

void mapping_dialog::doCancel()
{
    qDebug() << "mapping_dialog: doCancel";
    reject();
}

void mapping_dialog::doAccept()
{
    qDebug() << "mapping_dialog: doAccept";
    save_dialog();
}

void mapping_dialog::doReject()
{
    qDebug() << "mapping_dialog: doReject";
    invalidate_dialog();
}
