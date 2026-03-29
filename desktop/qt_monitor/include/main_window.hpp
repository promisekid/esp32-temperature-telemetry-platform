#pragma once

#include "monitor_session.hpp"
#include "trend_widget.hpp"

#include <QFrame>
#include <QLabel>
#include <QMainWindow>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QLineEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QTabWidget>
#include <QTableWidget>
#include <QTimer>

namespace telemetry_platform::qt_monitor {

class MainWindow : public QMainWindow {
    Q_OBJECT

  public:
    explicit MainWindow(const MonitorSessionConfig &config, QWidget *parent = nullptr);
    void set_active_tab(const QString &tab_name);

  private slots:
    void poll_device();
    void refresh_view();
    void request_device_config();
    void load_device_config_into_form();
    void push_config_to_device();
    void import_config_json();
    void export_config_json();
    void load_config_preset();
    void save_config_preset();
    void save_config_draft();
    void reload_config_draft();
    void reset_config_defaults();
    void mark_config_dirty();

  private:
    void setup_ui();
    void setup_config_page(QTabWidget *tabs);
    void update_overview(const common::DeviceState &state);
    void update_faults(const common::DeviceState &state);
    void update_config_status(const common::DeviceState &state);
    void connect_config_inputs();
    void populate_config_form(const common::DeviceConfigProfile &profile);
    common::DeviceConfigProfile collect_config_form() const;
    QString config_storage_path() const;
    QString config_preset_directory() const;
    void load_config_draft(bool use_defaults_when_missing = true);
    bool confirm_discard_dirty_form(const QString &next_action);
    bool confirm_push_to_device(const common::DeviceConfigProfile &profile, const common::DeviceState &state);
    bool load_profile_from_json(const QString &path, common::DeviceConfigProfile &profile, QString *error_message = nullptr) const;
    bool save_profile_to_json(const QString &path, const common::DeviceConfigProfile &profile, QString *error_message = nullptr) const;

    MonitorSessionConfig config_;
    MonitorSession session_;
    common::DeviceConfigProfile config_profile_ {};
    QTimer poll_timer_;
    QTimer refresh_timer_;

    QLabel *connection_label_ {nullptr};
    QLabel *session_label_ {nullptr};
    QLabel *protocol_label_ {nullptr};
    QLabel *heartbeat_label_ {nullptr};
    QLabel *device_status_label_ {nullptr};
    QLabel *overview_hint_label_ {nullptr};
    QFrame *channel0_card_ {nullptr};
    QFrame *channel1_card_ {nullptr};
    QLabel *channel0_value_label_ {nullptr};
    QLabel *channel0_meta_label_ {nullptr};
    QLabel *channel1_value_label_ {nullptr};
    QLabel *channel1_meta_label_ {nullptr};
    QLabel *last_fault_label_ {nullptr};
    QLabel *config_status_label_ {nullptr};
    QLabel *device_config_status_label_ {nullptr};
    QLabel *config_compare_status_label_ {nullptr};
    QLabel *config_path_label_ {nullptr};
    TrendWidget *trend_widget_ {nullptr};
    QTableWidget *fault_table_ {nullptr};
    QTabWidget *tab_widget_ {nullptr};
    QComboBox *config_protocol_combo_ {nullptr};
    QSpinBox *sensor_gpio_spin_ {nullptr};
    QSpinBox *sample_period_spin_ {nullptr};
    QSpinBox *heartbeat_period_spin_ {nullptr};
    QLineEdit *sensor_id_edit_ {nullptr};
    QLineEdit *fw_version_edit_ {nullptr};
    QDoubleSpinBox *valid_min_spin_ {nullptr};
    QDoubleSpinBox *valid_max_spin_ {nullptr};
    QDoubleSpinBox *real_overtemp_spin_ {nullptr};
    QDoubleSpinBox *sim_min_spin_ {nullptr};
    QDoubleSpinBox *sim_max_spin_ {nullptr};
    QDoubleSpinBox *sim_step_spin_ {nullptr};
    QDoubleSpinBox *sim_overtemp_spin_ {nullptr};
    QPushButton *query_device_config_button_ {nullptr};
    QPushButton *load_device_config_button_ {nullptr};
    QPushButton *push_config_button_ {nullptr};
    QPushButton *import_config_button_ {nullptr};
    QPushButton *export_config_button_ {nullptr};
    QPushButton *load_preset_button_ {nullptr};
    QPushButton *save_preset_button_ {nullptr};
    QPushButton *save_config_button_ {nullptr};
    QPushButton *reload_config_button_ {nullptr};
    QPushButton *reset_config_button_ {nullptr};
    QString config_action_status_ {QStringLiteral("defaults loaded")};
    bool config_form_dirty_ {false};
    bool suppress_config_tracking_ {false};
};

}  // namespace telemetry_platform::qt_monitor
