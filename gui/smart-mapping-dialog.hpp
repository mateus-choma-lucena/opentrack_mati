#pragma once
#include "export.hpp"
#include "enhanced/smart-mapping.hpp"
#include "spline/spline-widget.hpp"
#include <QDialog>
#include <functional>

class QComboBox;
class QCheckBox;
class QDoubleSpinBox;
class QLabel;
class QPushButton;
class smart_comparison;

// Draft splines use the native editor and evaluator, with private bundles.
// Only an explicit Apply or Save as action crosses the persistence boundary.
class OTR_GUI_EXPORT smart_mapping_dialog final : public QDialog {
    Q_OBJECT
    Mappings& mappings;
    enhanced::calibration::record draft;
    std::array<enhanced::calibration::axis_result,6> previous;
    std::array<spline,6> positive,negative,original_positive,original_negative;
    enhanced_settings controls;
    main_settings settings;
    enhanced::calibration::profile stored;
    std::function<bool()> stop_tracking;
    std::function<QString()> new_profile_path;
    QComboBox *axis,*style,*game;
    std::array<QCheckBox*,6> selected{};
    QDoubleSpinBox *target_positive,*target_negative,*sensitivity_positive,*sensitivity_negative,*shape,*deadzone,*probe;
    QLabel *readout,*timing,*advice,*status,*template_reason;
    QPushButton *apply_button,*save_button;
    QCheckBox* confirmation;
    spline_widget *positive_editor,*negative_editor;
    smart_comparison* comparison;
    bool updating=false,applied=false;
    void load_axis();
    void regenerate();
    void point_edited(int index);
    void refresh_preview();
    bool valid_selection(QString* reason=nullptr) const;
public:
    smart_mapping_dialog(Mappings& mappings,std::function<bool()> stop_tracking,std::function<QString()> new_profile_path,
                         QWidget* parent=nullptr,std::optional<enhanced::calibration::record> measurements={});
    bool applied_to_current() const {return applied;}
    const enhanced::calibration::record& draft_record() const {return draft;}
    spline& draft_curve(int index,bool negative_branch=false) {return negative_branch?negative[index]:positive[index];}
    enhanced::calibration::axis_selection selected_axes() const;
};
