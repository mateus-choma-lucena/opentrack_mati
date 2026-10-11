#pragma once
#include "enhanced/calibration-profile.hpp"
#include "export.hpp"
#include <QWizard>
#include <QTimer>
#include <functional>
#include <map>
class QLabel;
class QPushButton;
class QCheckBox;
class QComboBox;
class QTableWidget;
class QProgressBar;
class calibration_page;
class smart_comparison;
using calibration_input=std::function<bool(bool,std::vector<enhanced::calibration::observation>&)>;
class OTR_GUI_EXPORT calibration_dialog final : public QWizard {
    Q_OBJECT
public:
    enum page_id {welcome,center_head,noise,left,right,up,down,optional,roll_left,roll_right,x_left,x_right,y_up,y_down,z_forward,z_backward,game_profile,compare,review,completion};
    calibration_dialog(Mappings&,calibration_input,std::function<bool()> stop,std::function<bool()> center,QWidget* parent);
    ~calibration_dialog() override;
    std::function<QString()> new_profile_path;
    std::function<bool(const enhanced::calibration::axis_selection&)> save_applied;
    bool applied_to_current() const {return applied;}
    bool persisted_to_current() const {return saved;}
    enhanced::calibration::axis_selection selected_axes() const {return selection;}
    const enhanced::calibration::record& draft_record() const {return data;}
    int nextId() const override;
    void reject() override;
protected:
    void initializePage(int) override;
    bool validateCurrentPage() override;
    void done(int) override;
private:
    struct recording_controls {QLabel* status{};QCheckBox* comfort{};QPushButton* record{};QProgressBar* progress{};};
    Mappings& mappings;
    calibration_input input;
    std::function<bool()> stop_tracking,center_tracking;
    enhanced::calibration::record data;
    enhanced::calibration::measurement measurement;
    std::map<int,enhanced::calibration::record> checkpoints;
    std::map<int,recording_controls> recordings;
    enhanced::calibration::axis_selection selection{};
    QTimer timer;
    QWidget* live_banner;
    QLabel *readiness,*live_pose,*stage,*review_stats,*review_validity,*finished,*optional_status;
    QProgressBar* overall;
    QCheckBox *roll,*translation,*overwrite;
    QComboBox *game,*preview_axis,*style;
    QTableWidget* table;
    smart_comparison* comparison;
    std::array<QCheckBox*,6> selected{};
    std::optional<std::uint64_t> baseline_epoch;
    enhanced::timestamp accept_after{},retry_since{},last_good{};
    bool recording=false,waiting_retry=false,ready_camera=false,applied=false,saved=false,updating=false,preview_valid=false;
    int skip_to=-1;
    QString reset_message;
    void tick();
    void enter_page(int);
    void begin_recording(bool retry=false);
    void update_recording();
    void build_review();
    void regenerate();
    void choose_template();
    bool apply_result();
    void skip_optional();
    static int recording_axis(int);
    static int stage_number(int);
};
