#pragma once
#include "enhanced/calibration-profile.hpp"
#include "export.hpp"
#include <QDialog>
#include <QTimer>
#include <functional>

class QLabel;
class QPushButton;
class QCheckBox;
class QComboBox;
class QTableWidget;
class QStackedWidget;
class calibration_preview;

using calibration_input = std::function<bool(bool,std::vector<enhanced::calibration::observation>&)>;

class OTR_GUI_EXPORT calibration_dialog final : public QDialog {
    Q_OBJECT
    Mappings& mappings;
    calibration_input input;
    std::function<bool()> stop_tracking;
    std::function<bool()> center_tracking;
    enhanced::calibration::record result;
    enhanced::calibration::measurement measurement;
    std::vector<std::pair<int,QString>> steps;
    std::array<int,6> first_sign{};
    QTimer timer;
    QStackedWidget* pages;
    QLabel *instructions,*status,*review_stats,*review_validity;
    QPushButton *record_button,*next_button,*apply_button;
    QCheckBox *comfortable,*roll,*translation,*overwrite;
    QComboBox *use_case,*preview_axis;
    QTableWidget* table;
    calibration_preview* preview;
    int step=0;
    std::optional<std::uint64_t> baseline_epoch;
    bool reviewing=false, updating=false, preview_valid=false;
    bool applied=false;
    enhanced::calibration::axis_selection applied_selection{};
    enhanced::timestamp accept_after{};
    void tick();
    void begin();
    void advance();
    void show_step();
    void review();
    void regenerate();
public:
    std::function<QString()> new_profile_path;
    bool applied_to_current() const {return applied;}
    enhanced::calibration::axis_selection selected_axes() const {return applied_selection;}
    calibration_dialog(Mappings& mappings,calibration_input input,std::function<bool()> stop_tracking,std::function<bool()> center_tracking,QWidget* parent);
    ~calibration_dialog() override;
};
