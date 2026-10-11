#pragma once

#include "export.hpp"

#include "logic/mappings.hpp"
#include "gui/ui_mapping-dialog.h"
#include "spline/spline-widget.hpp"
#include "enhanced/options.hpp"
#include "enhanced/telemetry.hpp"
#include <functional>
#include <QTimer>

#include <QWidget>
#include <QDialog>
#include <QtEvents>

class OTR_GUI_EXPORT mapping_dialog final : public QDialog
{
    Q_OBJECT
public:
    mapping_dialog(Mappings& m);
    void refresh_tab();
    inline bool embeddable() noexcept { return false; }
    std::function<std::optional<enhanced::head_controls_snapshot>()> head_diagnostics;
private:
    Ui::mapping_dialog ui;
    Mappings& m;
    main_settings s;
    enhanced_settings enhanced_opts;
    QTimer preview_timer;

    spline_widget* widgets[6][2];

    void closeEvent(QCloseEvent*) override;

    void load();

    void save_dialog();
    void invalidate_dialog();
    void refresh_mapping_controls();

private slots:
    void doOK();
    void doCancel();
    void doAccept();
    void doReject();
};
