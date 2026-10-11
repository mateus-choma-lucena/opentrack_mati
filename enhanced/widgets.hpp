#pragma once
#include "options.hpp"
#include <QCheckBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QLabel>

namespace enhanced_ui {
inline QCheckBox* check(QFormLayout* form, const QString& title, options::value<bool>& setting)
{
    auto* widget = new QCheckBox(title);
    form->addRow(widget); options::tie_setting(setting, widget);
    return widget;
}
inline QDoubleSpinBox* number(QFormLayout* form, const QString& title, options::value<double>& setting,
                              double low, double high, const QString& suffix = {})
{
    auto* widget = new QDoubleSpinBox;
    widget->setRange(low, high); widget->setSingleStep(.1); widget->setSuffix(suffix);
    form->addRow(title, widget); options::tie_setting(setting, widget);
    return widget;
}
inline void adaptive(QFormLayout* form, enhanced_settings& s)
{
    check(form, QObject::tr("Enable Enhanced Adaptive Filter"), s.adaptive_enabled);
    number(form, QObject::tr("Rotation smoothing strength"), s.rotation_smoothing, 0, 5);
    number(form, QObject::tr("Translation smoothing strength"), s.translation_smoothing, 0, 5);
    number(form, QObject::tr("Responsiveness"), s.responsiveness, 0, 5);
    number(form, QObject::tr("Jitter protection"), s.jitter, 0, 5);
    check(form, QObject::tr("Use confidence when supplied by tracker"), s.confidence_aware);
    check(form, QObject::tr("Show filter debug values"), s.debug);
    auto* note = new QLabel(QObject::tr("Select Enhanced Adaptive Filter in the main Filter list. Disable NeuralNet's internal filter when comparing smoothness."));
    note->setWordWrap(true); form->addRow(note);
}
} // namespace enhanced_ui
