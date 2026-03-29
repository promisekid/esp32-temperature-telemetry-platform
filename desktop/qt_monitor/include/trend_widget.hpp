#pragma once

#include "project_types.hpp"

#include <QWidget>

namespace telemetry_platform::qt_monitor {

class TrendWidget : public QWidget {
    Q_OBJECT

  public:
    explicit TrendWidget(QWidget *parent = nullptr);

    void set_state(common::DeviceState state);

  protected:
    void paintEvent(QPaintEvent *event) override;

  private:
    common::DeviceState state_;
};

}  // namespace telemetry_platform::qt_monitor
