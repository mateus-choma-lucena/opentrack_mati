#pragma once
#include "api/plugin-api.hpp"
#include "enhanced/motion.hpp"
#include "enhanced/options.hpp"
#include <QLabel>
#include <QTimer>
#include <atomic>

class enhanced_adaptive final : public IFilter
{
    enhanced_settings settings;
    enhanced::adaptive_filter state;
    mutable std::mutex mutex;
    std::atomic<double> debug_delta{0};
public:
    module_status initialize() override { return status_ok(); }
    void filter(const double* input, double* output) override;
    void filter_sample(const double* input, double* output, const tracking_sample_info& sample) override;
    bool enhanced_active() const override { return *settings.adaptive_enabled; }
    bool supports_head_response() const override { return enhanced_active(); }
    void filter_sample_with_response(const double* input, double* output, const tracking_sample_info& sample, double multiplier) override;
    std::optional<double> estimated_response_lag_ms() const override { std::lock_guard lock(mutex); return state.estimated_lag_ms(); }
    void center() override { std::lock_guard lock(mutex); state.reset(); }
    void tracking_lost() override { std::lock_guard lock(mutex); state.tracking_lost(); }
    double activity() const { return debug_delta.load(); }
};

class enhanced_adaptive_dialog final : public IFilterDialog
{
    Q_OBJECT
    enhanced_settings settings;
    enhanced_adaptive* filter = nullptr;
    QLabel* debug;
    QTimer timer;
public:
    enhanced_adaptive_dialog();
    void register_filter(IFilter* f) override { filter = static_cast<enhanced_adaptive*>(f); }
    void unregister_filter() override { filter = nullptr; }
    void save() override { settings.b->save(); }
    void reload() override { settings.b->reload(); }
};

class enhanced_adaptive_metadata final : public Metadata
{
    Q_OBJECT
    QString name() override { return tr("Enhanced Adaptive Filter"); }
    QIcon icon() override { return QIcon(":/images/filter-16.png"); }
};
