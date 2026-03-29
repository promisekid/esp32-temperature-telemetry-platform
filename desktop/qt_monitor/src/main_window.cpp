#include "main_window.hpp"

#include <QAbstractItemView>
#include <QApplication>
#include <QCoreApplication>
#include <QDateTime>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QFrame>
#include <QGridLayout>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollArea>
#include <QSettings>
#include <QSignalBlocker>
#include <QStatusBar>
#include <QTabWidget>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVBoxLayout>
#include <QWidget>

#include <chrono>
#include <cmath>
#include <cstddef>
#include <filesystem>

namespace telemetry_platform::qt_monitor {

namespace {

constexpr double kFloatTolerance = 0.0001;

std::uint64_t now_ms() {
    const auto now = std::chrono::steady_clock::now().time_since_epoch();
    return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(now).count());
}

QString connection_badge_text(const common::DeviceState &state) {
    return state.online ? QStringLiteral("ONLINE") : QStringLiteral("OFFLINE");
}

QString connection_badge_style(const common::DeviceState &state) {
    if (state.online) {
        return QStringLiteral(
            "QLabel { background: #16352a; color: #7dffbc; border: 1px solid #2f7f5a; "
            "border-radius: 14px; padding: 6px 14px; font-weight: 700; }"
        );
    }

    return QStringLiteral(
        "QLabel { background: #3a1d1d; color: #ffb3b3; border: 1px solid #8c4343; "
        "border-radius: 14px; padding: 6px 14px; font-weight: 700; }"
    );
}

QString channel_card_style(common::ChannelStatus status) {
    switch (status) {
        case common::ChannelStatus::ok:
            return QStringLiteral(
                "QFrame { background: #141c25; border: 1px solid #274255; border-radius: 14px; }"
            );
        case common::ChannelStatus::overtemp:
            return QStringLiteral(
                "QFrame { background: #291717; border: 1px solid #b35b3d; border-radius: 14px; }"
            );
        default:
            return QStringLiteral(
                "QFrame { background: #241d12; border: 1px solid #8d6a2c; border-radius: 14px; }"
            );
    }
}

QString channel_value_color(common::ChannelStatus status) {
    switch (status) {
        case common::ChannelStatus::ok:
            return QStringLiteral("#f5fbff");
        case common::ChannelStatus::overtemp:
            return QStringLiteral("#ffb089");
        default:
            return QStringLiteral("#ffd66e");
    }
}

QString source_label(common::ChannelSource source) {
    return source == common::ChannelSource::real ? QStringLiteral("Real Sensor") : QStringLiteral("Simulated Channel");
}

QString heartbeat_age_text(const common::DeviceState &state) {
    if (state.last_heartbeat_host_ms == 0) {
        return QStringLiteral("Heartbeat: n/a");
    }

    const auto age_ms = now_ms() > state.last_heartbeat_host_ms ? now_ms() - state.last_heartbeat_host_ms : 0;
    return QStringLiteral("Heartbeat age: %1 s").arg(static_cast<double>(age_ms) / 1000.0, 0, 'f', 1);
}

QString protocol_text(common::ProtocolMode configured_mode, common::ProtocolMode active_mode) {
    return QStringLiteral("Protocol: config=%1 / active=%2")
        .arg(QString::fromUtf8(common::to_string(configured_mode)))
        .arg(QString::fromUtf8(common::to_string(active_mode)));
}

QString protocol_label(common::ProtocolMode mode) {
    return QString::fromUtf8(common::to_string(mode));
}

bool nearly_equal(double lhs, double rhs) {
    return std::abs(lhs - rhs) <= kFloatTolerance;
}

int profile_diff_count(
    const common::DeviceConfigProfile &lhs,
    const common::DeviceConfigProfile &rhs
) {
    int diff_count = 0;

    if (lhs.preferred_protocol != rhs.preferred_protocol) {
        ++diff_count;
    }
    if (lhs.sensor_gpio != rhs.sensor_gpio) {
        ++diff_count;
    }
    if (lhs.sample_period_ms != rhs.sample_period_ms) {
        ++diff_count;
    }
    if (lhs.heartbeat_period_ms != rhs.heartbeat_period_ms) {
        ++diff_count;
    }
    if (lhs.sensor_id != rhs.sensor_id) {
        ++diff_count;
    }
    if (lhs.firmware_version != rhs.firmware_version) {
        ++diff_count;
    }
    if (!nearly_equal(lhs.valid_min_temp_c, rhs.valid_min_temp_c)) {
        ++diff_count;
    }
    if (!nearly_equal(lhs.valid_max_temp_c, rhs.valid_max_temp_c)) {
        ++diff_count;
    }
    if (!nearly_equal(lhs.real_overtemp_threshold_c, rhs.real_overtemp_threshold_c)) {
        ++diff_count;
    }
    if (!nearly_equal(lhs.simulated_min_temp_c, rhs.simulated_min_temp_c)) {
        ++diff_count;
    }
    if (!nearly_equal(lhs.simulated_max_temp_c, rhs.simulated_max_temp_c)) {
        ++diff_count;
    }
    if (!nearly_equal(lhs.simulated_step_c, rhs.simulated_step_c)) {
        ++diff_count;
    }
    if (!nearly_equal(lhs.simulated_overtemp_threshold_c, rhs.simulated_overtemp_threshold_c)) {
        ++diff_count;
    }

    return diff_count;
}

bool profiles_equal(
    const common::DeviceConfigProfile &lhs,
    const common::DeviceConfigProfile &rhs
) {
    return profile_diff_count(lhs, rhs) == 0;
}

QJsonObject profile_to_json_object(const common::DeviceConfigProfile &profile) {
    QJsonObject object;
    object.insert(QStringLiteral("preferred_protocol"), QString::fromUtf8(common::to_string(profile.preferred_protocol)));
    object.insert(QStringLiteral("sensor_gpio"), static_cast<int>(profile.sensor_gpio));
    object.insert(QStringLiteral("sample_period_ms"), static_cast<int>(profile.sample_period_ms));
    object.insert(QStringLiteral("heartbeat_period_ms"), static_cast<int>(profile.heartbeat_period_ms));
    object.insert(QStringLiteral("sensor_id"), QString::fromStdString(profile.sensor_id));
    object.insert(QStringLiteral("firmware_version"), QString::fromStdString(profile.firmware_version));
    object.insert(QStringLiteral("valid_min_temp_c"), profile.valid_min_temp_c);
    object.insert(QStringLiteral("valid_max_temp_c"), profile.valid_max_temp_c);
    object.insert(QStringLiteral("real_overtemp_threshold_c"), profile.real_overtemp_threshold_c);
    object.insert(QStringLiteral("simulated_min_temp_c"), profile.simulated_min_temp_c);
    object.insert(QStringLiteral("simulated_max_temp_c"), profile.simulated_max_temp_c);
    object.insert(QStringLiteral("simulated_step_c"), profile.simulated_step_c);
    object.insert(QStringLiteral("simulated_overtemp_threshold_c"), profile.simulated_overtemp_threshold_c);
    return object;
}

std::filesystem::path detect_repo_root() {
    auto current = std::filesystem::path(QCoreApplication::applicationDirPath().toStdString());
    for (int depth = 0; depth < 8; ++depth) {
        if (std::filesystem::exists(current / "desktop") && std::filesystem::exists(current / "data")) {
            return current;
        }
        if (!current.has_parent_path()) {
            break;
        }
        current = current.parent_path();
    }
    return std::filesystem::current_path();
}

QFrame *make_channel_card(
    QWidget *parent,
    const QString &title,
    QLabel **value_label,
    QLabel **meta_label
) {
    auto *card = new QFrame(parent);
    card->setStyleSheet(channel_card_style(common::ChannelStatus::not_found));
    auto *layout = new QVBoxLayout(card);
    layout->setContentsMargins(18, 16, 18, 16);
    layout->setSpacing(8);

    auto *title_label = new QLabel(title, card);
    title_label->setStyleSheet("QLabel { color: #8ea7bf; font-size: 12px; letter-spacing: 0.5px; }");

    *value_label = new QLabel(QStringLiteral("--.-- C"), card);
    (*value_label)->setStyleSheet("QLabel { color: #dfe7ef; font-size: 28px; font-weight: 700; }");

    *meta_label = new QLabel(QStringLiteral("waiting for telemetry"), card);
    (*meta_label)->setStyleSheet("QLabel { color: #b9c7d6; font-size: 13px; }");
    (*meta_label)->setWordWrap(true);

    layout->addWidget(title_label);
    layout->addWidget(*value_label);
    layout->addWidget(*meta_label);
    layout->addStretch(1);
    return card;
}

void update_channel_card(
    QFrame *card,
    QLabel *value_label,
    QLabel *meta_label,
    const common::DeviceState &state,
    std::size_t channel_index,
    const QString &fallback_title
) {
    if (channel_index >= state.latest_snapshot.channels.size()) {
        card->setStyleSheet(channel_card_style(common::ChannelStatus::not_found));
        value_label->setStyleSheet(QStringLiteral("QLabel { color: #dfe7ef; font-size: 28px; font-weight: 700; }"));
        value_label->setText(QStringLiteral("--.-- C"));
        meta_label->setText(QStringLiteral("%1\nstatus: unavailable").arg(fallback_title));
        return;
    }

    const auto &channel = state.latest_snapshot.channels[channel_index];
    card->setStyleSheet(channel_card_style(channel.status));
    value_label->setStyleSheet(
        QStringLiteral("QLabel { color: %1; font-size: 28px; font-weight: 700; }").arg(channel_value_color(channel.status))
    );
    value_label->setText(QStringLiteral("%1 C").arg(channel.temperature_c, 0, 'f', 2));
    meta_label->setText(
        QStringLiteral("%1\nname: %2\nstatus: %3")
            .arg(source_label(channel.source))
            .arg(QString::fromStdString(channel.name))
            .arg(QString::fromUtf8(common::to_string(channel.status)))
    );
}

}  // namespace

MainWindow::MainWindow(const MonitorSessionConfig &config, QWidget *parent)
    : QMainWindow(parent), config_(config) {
    setup_ui();
    load_config_draft(true);

    std::string error_message;
    if (!session_.start(config_, &error_message)) {
        QMessageBox::warning(this, QStringLiteral("Session Start Failed"), QString::fromStdString(error_message));
    }

    connect(&poll_timer_, &QTimer::timeout, this, &MainWindow::poll_device);
    connect(&refresh_timer_, &QTimer::timeout, this, &MainWindow::refresh_view);
    poll_timer_.start(100);
    refresh_timer_.start(300);
    refresh_view();
}

void MainWindow::set_active_tab(const QString &tab_name) {
    if (tab_widget_ == nullptr) {
        return;
    }

    const auto normalized = tab_name.trimmed().toLower();
    if (normalized == QStringLiteral("overview")) {
        tab_widget_->setCurrentIndex(0);
    } else if (normalized == QStringLiteral("trend")) {
        tab_widget_->setCurrentIndex(1);
    } else if (normalized == QStringLiteral("faults")) {
        tab_widget_->setCurrentIndex(2);
    } else if (normalized == QStringLiteral("config")) {
        tab_widget_->setCurrentIndex(3);
    }
}

void MainWindow::poll_device() {
    std::string error_message;
    if (!session_.poll_once(&error_message) && !error_message.empty()) {
        statusBar()->showMessage(QString::fromStdString(error_message), 1800);
    }
}

void MainWindow::refresh_view() {
    const auto state = session_.snapshot();
    update_overview(state);
    update_faults(state);
    update_config_status(state);
    trend_widget_->set_state(state);
}

QString MainWindow::config_storage_path() const {
    const auto repo_root = detect_repo_root();
    const auto config_dir = repo_root / "data" / "config";
    std::filesystem::create_directories(config_dir);
    return QString::fromStdString((config_dir / "qt_monitor_config.ini").string());
}

QString MainWindow::config_preset_directory() const {
    const auto repo_root = detect_repo_root();
    const auto preset_dir = repo_root / "data" / "config" / "presets";
    std::filesystem::create_directories(preset_dir);
    return QString::fromStdString(preset_dir.string());
}

void MainWindow::setup_config_page(QTabWidget *tabs) {
    auto *config_page = new QWidget(this);
    auto *page_layout = new QVBoxLayout(config_page);
    page_layout->setContentsMargins(18, 18, 18, 18);
    page_layout->setSpacing(12);

    auto *intro_frame = new QFrame(config_page);
    intro_frame->setStyleSheet("QFrame { background: #131c25; border: 1px solid #213243; border-radius: 14px; }");
    auto *intro_layout = new QVBoxLayout(intro_frame);
    intro_layout->setContentsMargins(18, 16, 18, 16);
    intro_layout->setSpacing(6);

    auto *title = new QLabel(QStringLiteral("Local configuration draft"), intro_frame);
    title->setStyleSheet("QLabel { color: #eef6ff; font-size: 18px; font-weight: 600; }");
    config_status_label_ = new QLabel(QStringLiteral("Status: defaults loaded"), intro_frame);
    config_status_label_->setStyleSheet("QLabel { color: #8ea7bf; font-size: 13px; }");
    device_config_status_label_ = new QLabel(QStringLiteral("Device config: not queried"), intro_frame);
    device_config_status_label_->setStyleSheet("QLabel { color: #8ea7bf; font-size: 13px; }");
    config_compare_status_label_ = new QLabel(QStringLiteral("Compare: local draft only"), intro_frame);
    config_compare_status_label_->setStyleSheet("QLabel { color: #8ea7bf; font-size: 13px; }");
    config_path_label_ = new QLabel(QStringLiteral("Draft path: %1").arg(config_storage_path()), intro_frame);
    config_path_label_->setStyleSheet("QLabel { color: #8ea7bf; font-size: 13px; }");

    auto *hint = new QLabel(
        QStringLiteral("This page edits a desktop-side draft that mirrors current firmware defaults. "
                       "You can query the latest device config, load it into the form, or push the current draft to the device."),
        intro_frame
    );
    hint->setWordWrap(true);
    hint->setStyleSheet("QLabel { color: #b9c7d6; font-size: 13px; }");

    intro_layout->addWidget(title);
    intro_layout->addWidget(config_status_label_);
    intro_layout->addWidget(device_config_status_label_);
    intro_layout->addWidget(config_compare_status_label_);
    intro_layout->addWidget(config_path_label_);
    intro_layout->addWidget(hint);

    auto *scroll = new QScrollArea(config_page);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setStyleSheet(
        "QScrollArea { background: #0f1720; border: 1px solid #213243; border-radius: 14px; }"
        "QScrollArea QWidget { background: #0f1720; }"
    );
    scroll->viewport()->setStyleSheet("background: #0f1720;");

    auto *form_host = new QWidget(scroll);
    form_host->setObjectName(QStringLiteral("configFormHost"));
    form_host->setAttribute(Qt::WA_StyledBackground, true);
    form_host->setStyleSheet(
        "#configFormHost { background-color: #0f1720; }"
        "#configFormHost QLabel { color: #dce8f4; font-size: 13px; }"
    );
    auto *form_layout = new QFormLayout(form_host);
    form_layout->setContentsMargins(6, 6, 6, 6);
    form_layout->setHorizontalSpacing(18);
    form_layout->setVerticalSpacing(10);
    form_layout->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);
    form_layout->setFormAlignment(Qt::AlignTop | Qt::AlignLeft);

    config_protocol_combo_ = new QComboBox(form_host);
    config_protocol_combo_->addItem(protocol_label(common::ProtocolMode::jsonl_v2), static_cast<int>(common::ProtocolMode::jsonl_v2));
    config_protocol_combo_->addItem(protocol_label(common::ProtocolMode::binary_v1), static_cast<int>(common::ProtocolMode::binary_v1));

    sensor_gpio_spin_ = new QSpinBox(form_host);
    sensor_gpio_spin_->setRange(0, 48);
    sample_period_spin_ = new QSpinBox(form_host);
    sample_period_spin_->setRange(500, 5000);
    heartbeat_period_spin_ = new QSpinBox(form_host);
    heartbeat_period_spin_->setRange(1000, 30000);

    sensor_id_edit_ = new QLineEdit(form_host);
    fw_version_edit_ = new QLineEdit(form_host);

    auto make_double_spin = [form_host](double min, double max, int decimals) {
        auto *spin = new QDoubleSpinBox(form_host);
        spin->setRange(min, max);
        spin->setDecimals(decimals);
        spin->setSingleStep(0.1);
        return spin;
    };

    valid_min_spin_ = make_double_spin(-100.0, 150.0, 1);
    valid_max_spin_ = make_double_spin(-100.0, 200.0, 1);
    real_overtemp_spin_ = make_double_spin(-50.0, 200.0, 1);
    sim_min_spin_ = make_double_spin(-50.0, 200.0, 1);
    sim_max_spin_ = make_double_spin(-50.0, 200.0, 1);
    sim_step_spin_ = make_double_spin(0.1, 20.0, 2);
    sim_overtemp_spin_ = make_double_spin(-50.0, 200.0, 1);

    form_layout->addRow(QStringLiteral("Preferred protocol"), config_protocol_combo_);
    form_layout->addRow(QStringLiteral("Sensor GPIO"), sensor_gpio_spin_);
    form_layout->addRow(QStringLiteral("Sample period (ms)"), sample_period_spin_);
    form_layout->addRow(QStringLiteral("Heartbeat period (ms)"), heartbeat_period_spin_);
    form_layout->addRow(QStringLiteral("Sensor ID"), sensor_id_edit_);
    form_layout->addRow(QStringLiteral("Firmware version"), fw_version_edit_);
    form_layout->addRow(QStringLiteral("Valid min temperature"), valid_min_spin_);
    form_layout->addRow(QStringLiteral("Valid max temperature"), valid_max_spin_);
    form_layout->addRow(QStringLiteral("Real channel overtemp"), real_overtemp_spin_);
    form_layout->addRow(QStringLiteral("Simulated min temperature"), sim_min_spin_);
    form_layout->addRow(QStringLiteral("Simulated max temperature"), sim_max_spin_);
    form_layout->addRow(QStringLiteral("Simulated step"), sim_step_spin_);
    form_layout->addRow(QStringLiteral("Simulated overtemp"), sim_overtemp_spin_);

    auto *device_button_row = new QHBoxLayout();
    query_device_config_button_ = new QPushButton(QStringLiteral("Query Device"), form_host);
    load_device_config_button_ = new QPushButton(QStringLiteral("Load Device Config"), form_host);
    push_config_button_ = new QPushButton(QStringLiteral("Push Draft To Device"), form_host);
    device_button_row->addWidget(query_device_config_button_);
    device_button_row->addWidget(load_device_config_button_);
    device_button_row->addWidget(push_config_button_);
    device_button_row->addStretch(1);
    form_layout->addRow(QStringLiteral("Device actions"), device_button_row);

    auto *button_row = new QHBoxLayout();
    import_config_button_ = new QPushButton(QStringLiteral("Import JSON"), form_host);
    export_config_button_ = new QPushButton(QStringLiteral("Export JSON"), form_host);
    load_preset_button_ = new QPushButton(QStringLiteral("Load Preset"), form_host);
    save_preset_button_ = new QPushButton(QStringLiteral("Save Preset"), form_host);
    save_config_button_ = new QPushButton(QStringLiteral("Save Draft"), form_host);
    reload_config_button_ = new QPushButton(QStringLiteral("Reload Draft"), form_host);
    reset_config_button_ = new QPushButton(QStringLiteral("Reset Defaults"), form_host);
    button_row->addWidget(import_config_button_);
    button_row->addWidget(export_config_button_);
    button_row->addWidget(load_preset_button_);
    button_row->addWidget(save_preset_button_);
    button_row->addWidget(save_config_button_);
    button_row->addWidget(reload_config_button_);
    button_row->addWidget(reset_config_button_);
    button_row->addStretch(1);
    form_layout->addRow(QString(), button_row);

    connect(query_device_config_button_, &QPushButton::clicked, this, &MainWindow::request_device_config);
    connect(load_device_config_button_, &QPushButton::clicked, this, &MainWindow::load_device_config_into_form);
    connect(push_config_button_, &QPushButton::clicked, this, &MainWindow::push_config_to_device);
    connect(import_config_button_, &QPushButton::clicked, this, &MainWindow::import_config_json);
    connect(export_config_button_, &QPushButton::clicked, this, &MainWindow::export_config_json);
    connect(load_preset_button_, &QPushButton::clicked, this, &MainWindow::load_config_preset);
    connect(save_preset_button_, &QPushButton::clicked, this, &MainWindow::save_config_preset);
    connect(save_config_button_, &QPushButton::clicked, this, &MainWindow::save_config_draft);
    connect(reload_config_button_, &QPushButton::clicked, this, &MainWindow::reload_config_draft);
    connect(reset_config_button_, &QPushButton::clicked, this, &MainWindow::reset_config_defaults);
    connect_config_inputs();

    scroll->setWidget(form_host);

    page_layout->addWidget(intro_frame);
    page_layout->addWidget(scroll, 1);
    tabs->addTab(config_page, QStringLiteral("Config"));
}

void MainWindow::setup_ui() {
    setWindowTitle(QStringLiteral("ESP32 Temperature Telemetry Monitor"));
    resize(1180, 760);
    setStyleSheet(
        "QMainWindow { background: #0d131a; }"
        "QTabWidget::pane { border: 1px solid #1f2d3a; background: #0f1720; }"
        "QTabBar::tab { background: #121b24; color: #b9c6d3; padding: 8px 14px; }"
        "QTabBar::tab:selected { background: #1a2833; color: #eef6ff; }"
        "QLabel { color: #dce8f4; }"
        "QLineEdit, QComboBox, QSpinBox, QDoubleSpinBox { background: #111922; color: #eef6ff; border: 1px solid #223140; border-radius: 8px; padding: 6px 8px; }"
        "QPushButton { background: #173047; color: #eef6ff; border: 1px solid #2d587d; border-radius: 8px; padding: 6px 12px; }"
        "QPushButton:hover { background: #1d3c58; }"
        "QTableWidget { background: #111922; color: #dce8f4; border: 1px solid #223140; border-radius: 12px; gridline-color: #223140; }"
        "QHeaderView::section { background: #16212c; color: #cfe0ef; padding: 6px; border: none; border-right: 1px solid #223140; }"
        "QStatusBar { background: #111922; color: #b8c8d8; }"
    );

    tab_widget_ = new QTabWidget(this);
    setCentralWidget(tab_widget_);

    auto *overview_page = new QWidget(this);
    auto *overview_layout = new QVBoxLayout(overview_page);
    overview_layout->setContentsMargins(18, 18, 18, 18);
    overview_layout->setSpacing(14);

    auto *header_frame = new QFrame(overview_page);
    header_frame->setStyleSheet("QFrame { background: #131c25; border: 1px solid #213243; border-radius: 14px; }");
    auto *header_layout = new QGridLayout(header_frame);
    header_layout->setContentsMargins(18, 16, 18, 16);
    header_layout->setHorizontalSpacing(16);
    header_layout->setVerticalSpacing(10);

    connection_label_ = new QLabel(QStringLiteral("OFFLINE"), header_frame);
    session_label_ = new QLabel(header_frame);
    protocol_label_ = new QLabel(header_frame);
    heartbeat_label_ = new QLabel(header_frame);
    device_status_label_ = new QLabel(header_frame);
    overview_hint_label_ = new QLabel(header_frame);
    connection_label_->setStyleSheet(connection_badge_style(common::DeviceState {}));
    session_label_->setStyleSheet("QLabel { color: #8ea7bf; font-size: 13px; }");
    protocol_label_->setStyleSheet("QLabel { color: #8ea7bf; font-size: 13px; }");
    heartbeat_label_->setStyleSheet("QLabel { color: #8ea7bf; font-size: 13px; }");
    device_status_label_->setStyleSheet("QLabel { color: #eef6ff; font-size: 20px; font-weight: 600; }");
    overview_hint_label_->setStyleSheet("QLabel { color: #8ea7bf; font-size: 13px; }");

    header_layout->addWidget(connection_label_, 0, 0, Qt::AlignLeft | Qt::AlignTop);
    header_layout->addWidget(device_status_label_, 0, 1, 1, 3);
    header_layout->addWidget(session_label_, 1, 0, 1, 2);
    header_layout->addWidget(protocol_label_, 1, 2, 1, 1);
    header_layout->addWidget(heartbeat_label_, 1, 3, 1, 1);
    header_layout->addWidget(overview_hint_label_, 2, 0, 1, 4);

    auto *cards_layout = new QHBoxLayout();
    cards_layout->setSpacing(14);
    channel0_card_ = make_channel_card(overview_page, QStringLiteral("CHANNEL_0"), &channel0_value_label_, &channel0_meta_label_);
    channel1_card_ = make_channel_card(overview_page, QStringLiteral("CHANNEL_1"), &channel1_value_label_, &channel1_meta_label_);
    cards_layout->addWidget(channel0_card_, 1);
    cards_layout->addWidget(channel1_card_, 1);

    auto *fault_frame = new QFrame(overview_page);
    fault_frame->setStyleSheet("QFrame { background: #131c25; border: 1px solid #213243; border-radius: 14px; }");
    auto *fault_layout = new QVBoxLayout(fault_frame);
    fault_layout->setContentsMargins(18, 16, 18, 16);
    fault_layout->setSpacing(8);

    auto *fault_title = new QLabel(QStringLiteral("LATEST FAULT"), fault_frame);
    fault_title->setStyleSheet("QLabel { color: #8ea7bf; font-size: 12px; }");
    last_fault_label_ = new QLabel(QStringLiteral("No active fault"), fault_frame);
    last_fault_label_->setStyleSheet("QLabel { color: #eef6ff; font-size: 15px; font-weight: 600; }");
    last_fault_label_->setWordWrap(true);
    fault_layout->addWidget(fault_title);
    fault_layout->addWidget(last_fault_label_);

    overview_layout->addWidget(header_frame);
    overview_layout->addLayout(cards_layout);
    overview_layout->addWidget(fault_frame);
    overview_layout->addStretch(1);

    auto *trend_page = new QWidget(this);
    auto *trend_layout = new QVBoxLayout(trend_page);
    trend_layout->setContentsMargins(18, 18, 18, 18);
    trend_layout->setSpacing(12);
    trend_widget_ = new TrendWidget(trend_page);
    trend_layout->addWidget(trend_widget_);

    auto *fault_page = new QWidget(this);
    auto *fault_page_layout = new QVBoxLayout(fault_page);
    fault_page_layout->setContentsMargins(18, 18, 18, 18);
    fault_page_layout->setSpacing(12);
    fault_table_ = new QTableWidget(0, 4, fault_page);
    fault_table_->setHorizontalHeaderLabels({QStringLiteral("Channel"), QStringLiteral("Source"), QStringLiteral("Code"), QStringLiteral("Message")});
    fault_table_->horizontalHeader()->setStretchLastSection(true);
    fault_table_->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    fault_table_->verticalHeader()->setVisible(false);
    fault_table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    fault_table_->setSelectionMode(QAbstractItemView::NoSelection);
    fault_table_->setAlternatingRowColors(true);
    fault_page_layout->addWidget(fault_table_);

    tab_widget_->addTab(overview_page, QStringLiteral("Overview"));
    tab_widget_->addTab(trend_page, QStringLiteral("Trend"));
    tab_widget_->addTab(fault_page, QStringLiteral("Faults"));
    setup_config_page(tab_widget_);

    const auto session_text = config_.replay_path.empty()
                                  ? QStringLiteral("Transport: serial %1").arg(QString::fromStdString(config_.port_name))
                                  : QStringLiteral("Transport: replay %1").arg(QString::fromStdString(config_.replay_path));
    session_label_->setText(session_text);
    protocol_label_->setText(protocol_text(config_.mode, common::ProtocolMode::auto_detect));
    heartbeat_label_->setText(QStringLiteral("Heartbeat: n/a"));
    device_status_label_->setText(QStringLiteral("Device health: -    Firmware: -"));
    overview_hint_label_->setText(QStringLiteral("Latest sample: -    Uptime: -    Faults cached: 0"));
    statusBar()->showMessage(
        config_.replay_path.empty() ? QStringLiteral("Waiting for serial telemetry...") : QStringLiteral("Replay source loaded")
    );
}

void MainWindow::update_overview(const common::DeviceState &state) {
    connection_label_->setText(connection_badge_text(state));
    connection_label_->setStyleSheet(connection_badge_style(state));
    protocol_label_->setText(protocol_text(config_.mode, session_.active_mode()));
    heartbeat_label_->setText(heartbeat_age_text(state));
    device_status_label_->setText(
        QStringLiteral("Device health: %1    Firmware: %2")
            .arg(QString::fromUtf8(common::to_string(state.latest_snapshot.device_status)))
            .arg(state.firmware_version.empty() ? QStringLiteral("-") : QString::fromStdString(state.firmware_version))
    );
    overview_hint_label_->setText(
        QStringLiteral("Latest sample: %1    Uptime: %2 s    Faults cached: %3")
            .arg(state.latest_snapshot.sample_index)
            .arg(static_cast<double>(state.latest_snapshot.uptime_ms) / 1000.0, 0, 'f', 1)
            .arg(static_cast<int>(state.recent_faults.size()))
    );

    update_channel_card(channel0_card_, channel0_value_label_, channel0_meta_label_, state, 0, QStringLiteral("Real Sensor"));
    update_channel_card(channel1_card_, channel1_value_label_, channel1_meta_label_, state, 1, QStringLiteral("Simulated Channel"));

    if (!state.recent_faults.empty()) {
        const auto &fault = state.recent_faults.front();
        last_fault_label_->setText(
            QStringLiteral("channel_%1  %2\n%3")
                .arg(static_cast<int>(fault.channel_id))
                .arg(QString::fromStdString(fault.code))
                .arg(QString::fromStdString(fault.message))
        );
        last_fault_label_->setStyleSheet("QLabel { color: #ffb089; font-size: 15px; font-weight: 600; }");
    } else {
        last_fault_label_->setText(QStringLiteral("No active fault"));
        last_fault_label_->setStyleSheet("QLabel { color: #eef6ff; font-size: 15px; font-weight: 600; }");
    }
}

void MainWindow::update_faults(const common::DeviceState &state) {
    fault_table_->clearSpans();
    fault_table_->setRowCount(0);

    if (state.recent_faults.empty()) {
        fault_table_->setRowCount(1);
        auto *empty_item = new QTableWidgetItem(QStringLiteral("No fault event recorded"));
        fault_table_->setItem(0, 0, empty_item);
        fault_table_->setSpan(0, 0, 1, 4);
        return;
    }

    fault_table_->setRowCount(static_cast<int>(state.recent_faults.size()));

    for (int row = 0; row < static_cast<int>(state.recent_faults.size()); ++row) {
        const auto &fault = state.recent_faults[static_cast<std::size_t>(row)];
        fault_table_->setItem(row, 0, new QTableWidgetItem(QStringLiteral("channel_%1").arg(static_cast<int>(fault.channel_id))));
        fault_table_->setItem(row, 1, new QTableWidgetItem(source_label(fault.source)));
        fault_table_->setItem(row, 2, new QTableWidgetItem(QString::fromStdString(fault.code)));
        fault_table_->setItem(row, 3, new QTableWidgetItem(QString::fromStdString(fault.message)));
    }
}

void MainWindow::update_config_status(const common::DeviceState &state) {
    const bool replay_mode = session_.is_replay_mode();
    const bool form_ready = config_protocol_combo_ != nullptr && sensor_id_edit_ != nullptr;
    const auto current_profile = form_ready ? collect_config_form() : config_profile_;
    config_form_dirty_ = !profiles_equal(current_profile, config_profile_);

    if (query_device_config_button_ != nullptr) {
        query_device_config_button_->setEnabled(!replay_mode);
    }
    if (push_config_button_ != nullptr) {
        push_config_button_->setEnabled(!replay_mode);
    }
    if (load_device_config_button_ != nullptr) {
        load_device_config_button_->setEnabled(state.device_config.has_value());
    }
    if (export_config_button_ != nullptr) {
        export_config_button_->setEnabled(form_ready);
    }
    if (load_preset_button_ != nullptr) {
        load_preset_button_->setEnabled(form_ready);
    }
    if (save_preset_button_ != nullptr) {
        save_preset_button_->setEnabled(form_ready);
    }
    if (save_config_button_ != nullptr) {
        save_config_button_->setEnabled(form_ready);
    }

    if (config_status_label_ != nullptr) {
        config_status_label_->setText(
            QStringLiteral("Draft: %1 | %2")
                .arg(config_form_dirty_ ? QStringLiteral("dirty") : QStringLiteral("clean"))
                .arg(config_action_status_)
        );
    }

    if (device_config_status_label_ == nullptr) {
        return;
    }

    if (replay_mode) {
        device_config_status_label_->setText(QStringLiteral("Device config: replay mode does not support device commands"));
        return;
    }

    QString message = state.device_config.has_value()
                          ? QStringLiteral("Device config: available from device")
                          : QStringLiteral("Device config: not queried");

    if (!state.last_command.empty()) {
        message += QStringLiteral(" | last command: %1 (%2)")
                       .arg(QString::fromStdString(state.last_command))
                       .arg(state.last_command_success ? QStringLiteral("ok") : QStringLiteral("failed"));
        if (!state.last_command_message.empty()) {
            message += QStringLiteral(" - %1").arg(QString::fromStdString(state.last_command_message));
        }
    }

    device_config_status_label_->setText(message);

    if (config_compare_status_label_ == nullptr) {
        return;
    }

    if (replay_mode) {
        config_compare_status_label_->setText(QStringLiteral("Compare: replay mode, device config unavailable"));
        return;
    }

    if (!state.device_config.has_value()) {
        config_compare_status_label_->setText(
            config_form_dirty_
                ? QStringLiteral("Compare: local draft changed, waiting for device config snapshot")
                : QStringLiteral("Compare: waiting for device config snapshot")
        );
        return;
    }

    const auto diff_count = profile_diff_count(current_profile, *state.device_config);
    if (diff_count == 0) {
        config_compare_status_label_->setText(
            config_form_dirty_
                ? QStringLiteral("Compare: local draft changed but still matches current device config")
                : QStringLiteral("Compare: form matches device config")
        );
    } else {
        config_compare_status_label_->setText(
            config_form_dirty_
                ? QStringLiteral("Compare: conflict, local draft differs from device config in %1 field(s)").arg(diff_count)
                : QStringLiteral("Compare: form differs from device config in %1 field(s)").arg(diff_count)
        );
    }
}

bool MainWindow::confirm_discard_dirty_form(const QString &next_action) {
    if (!config_form_dirty_) {
        return true;
    }

    const auto result = QMessageBox::question(
        this,
        QStringLiteral("Discard Local Draft Changes"),
        QStringLiteral("The current form has unsaved local changes.\n\nContinue and %1?").arg(next_action),
        QMessageBox::Yes | QMessageBox::No,
        QMessageBox::No
    );
    return result == QMessageBox::Yes;
}

bool MainWindow::confirm_push_to_device(
    const common::DeviceConfigProfile &profile,
    const common::DeviceState &state
) {
    QString message;
    if (state.device_config.has_value()) {
        const auto diff_count = profile_diff_count(profile, *state.device_config);
        message = diff_count == 0
                      ? QStringLiteral("The current form matches the last known device config.\n\nPush it to the device anyway?")
                      : QStringLiteral("The current form differs from the last known device config in %1 field(s).\n\nPush the draft to the device?")
                            .arg(diff_count);
    } else {
        message = QStringLiteral("No device config snapshot is available yet.\n\nPush the current draft to the device?");
    }

    const auto result = QMessageBox::question(
        this,
        QStringLiteral("Push Draft To Device"),
        message,
        QMessageBox::Yes | QMessageBox::No,
        QMessageBox::No
    );
    return result == QMessageBox::Yes;
}

void MainWindow::populate_config_form(const common::DeviceConfigProfile &profile) {
    suppress_config_tracking_ = true;
    const QSignalBlocker protocol_blocker(config_protocol_combo_);
    const QSignalBlocker sensor_gpio_blocker(sensor_gpio_spin_);
    const QSignalBlocker sample_period_blocker(sample_period_spin_);
    const QSignalBlocker heartbeat_period_blocker(heartbeat_period_spin_);
    const QSignalBlocker sensor_id_blocker(sensor_id_edit_);
    const QSignalBlocker fw_version_blocker(fw_version_edit_);
    const QSignalBlocker valid_min_blocker(valid_min_spin_);
    const QSignalBlocker valid_max_blocker(valid_max_spin_);
    const QSignalBlocker real_overtemp_blocker(real_overtemp_spin_);
    const QSignalBlocker sim_min_blocker(sim_min_spin_);
    const QSignalBlocker sim_max_blocker(sim_max_spin_);
    const QSignalBlocker sim_step_blocker(sim_step_spin_);
    const QSignalBlocker sim_overtemp_blocker(sim_overtemp_spin_);

    const auto protocol_index = config_protocol_combo_->findData(static_cast<int>(profile.preferred_protocol));
    config_protocol_combo_->setCurrentIndex(protocol_index >= 0 ? protocol_index : 0);
    sensor_gpio_spin_->setValue(static_cast<int>(profile.sensor_gpio));
    sample_period_spin_->setValue(static_cast<int>(profile.sample_period_ms));
    heartbeat_period_spin_->setValue(static_cast<int>(profile.heartbeat_period_ms));
    sensor_id_edit_->setText(QString::fromStdString(profile.sensor_id));
    fw_version_edit_->setText(QString::fromStdString(profile.firmware_version));
    valid_min_spin_->setValue(profile.valid_min_temp_c);
    valid_max_spin_->setValue(profile.valid_max_temp_c);
    real_overtemp_spin_->setValue(profile.real_overtemp_threshold_c);
    sim_min_spin_->setValue(profile.simulated_min_temp_c);
    sim_max_spin_->setValue(profile.simulated_max_temp_c);
    sim_step_spin_->setValue(profile.simulated_step_c);
    sim_overtemp_spin_->setValue(profile.simulated_overtemp_threshold_c);
    suppress_config_tracking_ = false;
    config_form_dirty_ = false;
}

common::DeviceConfigProfile MainWindow::collect_config_form() const {
    common::DeviceConfigProfile profile {};
    profile.preferred_protocol = static_cast<common::ProtocolMode>(config_protocol_combo_->currentData().toInt());
    profile.sensor_gpio = static_cast<std::uint32_t>(sensor_gpio_spin_->value());
    profile.sample_period_ms = static_cast<std::uint32_t>(sample_period_spin_->value());
    profile.heartbeat_period_ms = static_cast<std::uint32_t>(heartbeat_period_spin_->value());
    profile.sensor_id = sensor_id_edit_->text().trimmed().toStdString();
    profile.firmware_version = fw_version_edit_->text().trimmed().toStdString();
    profile.valid_min_temp_c = valid_min_spin_->value();
    profile.valid_max_temp_c = valid_max_spin_->value();
    profile.real_overtemp_threshold_c = real_overtemp_spin_->value();
    profile.simulated_min_temp_c = sim_min_spin_->value();
    profile.simulated_max_temp_c = sim_max_spin_->value();
    profile.simulated_step_c = sim_step_spin_->value();
    profile.simulated_overtemp_threshold_c = sim_overtemp_spin_->value();
    return profile;
}

void MainWindow::load_config_draft(bool use_defaults_when_missing) {
    config_profile_ = common::default_device_config_profile();
    const auto path = config_storage_path();
    const QFileInfo draft_info(path);
    if (!draft_info.exists() && !use_defaults_when_missing) {
        return;
    }

    if (draft_info.exists()) {
        QSettings settings(path, QSettings::IniFormat);
        config_profile_.preferred_protocol = common::protocol_mode_from_string(settings.value("protocol/preferred", QString::fromUtf8(common::to_string(config_profile_.preferred_protocol))).toString().toStdString()).value_or(config_profile_.preferred_protocol);
        config_profile_.sensor_gpio = settings.value("device/sensor_gpio", static_cast<int>(config_profile_.sensor_gpio)).toUInt();
        config_profile_.sample_period_ms = settings.value("device/sample_period_ms", static_cast<int>(config_profile_.sample_period_ms)).toUInt();
        config_profile_.heartbeat_period_ms = settings.value("device/heartbeat_period_ms", static_cast<int>(config_profile_.heartbeat_period_ms)).toUInt();
        config_profile_.sensor_id = settings.value("device/sensor_id", QString::fromStdString(config_profile_.sensor_id)).toString().toStdString();
        config_profile_.firmware_version = settings.value("device/firmware_version", QString::fromStdString(config_profile_.firmware_version)).toString().toStdString();
        config_profile_.valid_min_temp_c = settings.value("device/valid_min_temp_c", config_profile_.valid_min_temp_c).toDouble();
        config_profile_.valid_max_temp_c = settings.value("device/valid_max_temp_c", config_profile_.valid_max_temp_c).toDouble();
        config_profile_.real_overtemp_threshold_c = settings.value("thresholds/real_overtemp_c", config_profile_.real_overtemp_threshold_c).toDouble();
        config_profile_.simulated_min_temp_c = settings.value("simulated/min_temp_c", config_profile_.simulated_min_temp_c).toDouble();
        config_profile_.simulated_max_temp_c = settings.value("simulated/max_temp_c", config_profile_.simulated_max_temp_c).toDouble();
        config_profile_.simulated_step_c = settings.value("simulated/step_c", config_profile_.simulated_step_c).toDouble();
        config_profile_.simulated_overtemp_threshold_c = settings.value("simulated/overtemp_c", config_profile_.simulated_overtemp_threshold_c).toDouble();
        config_action_status_ = QStringLiteral("loaded draft from disk");
    } else {
        config_action_status_ = QStringLiteral("using firmware-aligned defaults");
    }

    config_path_label_->setText(QStringLiteral("Draft path: %1").arg(path));
    populate_config_form(config_profile_);
    update_config_status(session_.snapshot());
}

void MainWindow::save_config_draft() {
    config_profile_ = collect_config_form();
    const auto path = config_storage_path();
    QSettings settings(path, QSettings::IniFormat);
    settings.setValue("protocol/preferred", QString::fromUtf8(common::to_string(config_profile_.preferred_protocol)));
    settings.setValue("device/sensor_gpio", static_cast<int>(config_profile_.sensor_gpio));
    settings.setValue("device/sample_period_ms", static_cast<int>(config_profile_.sample_period_ms));
    settings.setValue("device/heartbeat_period_ms", static_cast<int>(config_profile_.heartbeat_period_ms));
    settings.setValue("device/sensor_id", QString::fromStdString(config_profile_.sensor_id));
    settings.setValue("device/firmware_version", QString::fromStdString(config_profile_.firmware_version));
    settings.setValue("device/valid_min_temp_c", config_profile_.valid_min_temp_c);
    settings.setValue("device/valid_max_temp_c", config_profile_.valid_max_temp_c);
    settings.setValue("thresholds/real_overtemp_c", config_profile_.real_overtemp_threshold_c);
    settings.setValue("simulated/min_temp_c", config_profile_.simulated_min_temp_c);
    settings.setValue("simulated/max_temp_c", config_profile_.simulated_max_temp_c);
    settings.setValue("simulated/step_c", config_profile_.simulated_step_c);
    settings.setValue("simulated/overtemp_c", config_profile_.simulated_overtemp_threshold_c);
    settings.sync();

    config_action_status_ = QStringLiteral("draft saved to disk");
    config_form_dirty_ = false;
    update_config_status(session_.snapshot());
    statusBar()->showMessage(QStringLiteral("Configuration draft saved"), 2000);
}

void MainWindow::request_device_config() {
    if (session_.is_replay_mode()) {
        statusBar()->showMessage(QStringLiteral("Replay mode does not support device commands"), 2500);
        return;
    }

    std::string error_message;
    if (!session_.request_device_config(&error_message)) {
        const auto message = error_message.empty() ? QStringLiteral("Request device config failed") : QString::fromStdString(error_message);
        device_config_status_label_->setText(QStringLiteral("Device config: request failed"));
        statusBar()->showMessage(message, 3000);
        QMessageBox::warning(this, QStringLiteral("Query Device Config Failed"), message);
        return;
    }

    device_config_status_label_->setText(QStringLiteral("Device config: request sent, waiting for response"));
    statusBar()->showMessage(QStringLiteral("Device config request sent"), 2000);
}

void MainWindow::load_device_config_into_form() {
    const auto state = session_.snapshot();
    if (!state.device_config.has_value()) {
        statusBar()->showMessage(QStringLiteral("No device config has been received yet"), 2500);
        return;
    }
    if (!confirm_discard_dirty_form(QStringLiteral("load the latest device config into the form"))) {
        statusBar()->showMessage(QStringLiteral("Load device config canceled"), 2000);
        return;
    }

    config_profile_ = *state.device_config;
    populate_config_form(config_profile_);
    config_action_status_ = QStringLiteral("loaded latest device config into draft");
    update_config_status(state);
    statusBar()->showMessage(QStringLiteral("Loaded device config into form"), 2000);
}

void MainWindow::push_config_to_device() {
    if (session_.is_replay_mode()) {
        statusBar()->showMessage(QStringLiteral("Replay mode does not support device commands"), 2500);
        return;
    }

    const auto profile = collect_config_form();
    if (!confirm_push_to_device(profile, session_.snapshot())) {
        statusBar()->showMessage(QStringLiteral("Push config canceled"), 2000);
        return;
    }
    std::string error_message;
    if (!session_.send_config_profile(profile, &error_message)) {
        const auto message = error_message.empty() ? QStringLiteral("Push config failed") : QString::fromStdString(error_message);
        device_config_status_label_->setText(QStringLiteral("Device config: push failed"));
        statusBar()->showMessage(message, 3000);
        QMessageBox::warning(this, QStringLiteral("Push Config Failed"), message);
        return;
    }

    config_action_status_ = QStringLiteral("draft pushed to device (local draft not auto-saved)");
    update_config_status(session_.snapshot());
    device_config_status_label_->setText(QStringLiteral("Device config: push sent, waiting for ack"));
    statusBar()->showMessage(QStringLiteral("Config push sent to device"), 2000);
}

void MainWindow::import_config_json() {
    if (!confirm_discard_dirty_form(QStringLiteral("replace the current form with an imported JSON profile"))) {
        statusBar()->showMessage(QStringLiteral("Import config canceled"), 2000);
        return;
    }

    const auto default_path = QFileInfo(config_storage_path()).absolutePath();
    const auto path = QFileDialog::getOpenFileName(
        this,
        QStringLiteral("Import Device Config JSON"),
        default_path,
        QStringLiteral("JSON Files (*.json)")
    );
    if (path.isEmpty()) {
        return;
    }

    auto profile = common::default_device_config_profile();
    QString error_message;
    if (!load_profile_from_json(path, profile, &error_message)) {
        QMessageBox::warning(this, QStringLiteral("Import Config Failed"), error_message);
        statusBar()->showMessage(error_message, 3000);
        return;
    }

    config_profile_ = profile;
    populate_config_form(config_profile_);
    config_action_status_ = QStringLiteral("imported profile from JSON (not yet saved to draft file)");
    update_config_status(session_.snapshot());
    statusBar()->showMessage(QStringLiteral("Imported config profile from JSON"), 2000);
}

void MainWindow::export_config_json() {
    const auto default_dir = QFileInfo(config_storage_path()).absolutePath();
    const auto default_file = QFileInfo(config_storage_path()).baseName() + QStringLiteral(".json");
    const auto path = QFileDialog::getSaveFileName(
        this,
        QStringLiteral("Export Device Config JSON"),
        QFileInfo(default_dir, default_file).absoluteFilePath(),
        QStringLiteral("JSON Files (*.json)")
    );
    if (path.isEmpty()) {
        return;
    }

    QString error_message;
    if (!save_profile_to_json(path, collect_config_form(), &error_message)) {
        QMessageBox::warning(this, QStringLiteral("Export Config Failed"), error_message);
        statusBar()->showMessage(error_message, 3000);
        return;
    }

    config_action_status_ = QStringLiteral("exported current form to JSON");
    update_config_status(session_.snapshot());
    statusBar()->showMessage(QStringLiteral("Exported config profile to JSON"), 2000);
}

void MainWindow::load_config_preset() {
    if (!confirm_discard_dirty_form(QStringLiteral("replace the current form with a saved preset"))) {
        statusBar()->showMessage(QStringLiteral("Load preset canceled"), 2000);
        return;
    }

    const auto path = QFileDialog::getOpenFileName(
        this,
        QStringLiteral("Load Config Preset"),
        config_preset_directory(),
        QStringLiteral("JSON Files (*.json)")
    );
    if (path.isEmpty()) {
        return;
    }

    auto profile = common::default_device_config_profile();
    QString error_message;
    if (!load_profile_from_json(path, profile, &error_message)) {
        QMessageBox::warning(this, QStringLiteral("Load Preset Failed"), error_message);
        statusBar()->showMessage(error_message, 3000);
        return;
    }

    config_profile_ = profile;
    populate_config_form(config_profile_);
    config_action_status_ = QStringLiteral("loaded preset from %1").arg(QFileInfo(path).fileName());
    update_config_status(session_.snapshot());
    statusBar()->showMessage(QStringLiteral("Loaded preset into config form"), 2000);
}

void MainWindow::save_config_preset() {
    const auto default_name =
        QStringLiteral("preset_%1.json").arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd_HHmmss")));
    const auto path = QFileDialog::getSaveFileName(
        this,
        QStringLiteral("Save Config Preset"),
        QFileInfo(config_preset_directory(), default_name).absoluteFilePath(),
        QStringLiteral("JSON Files (*.json)")
    );
    if (path.isEmpty()) {
        return;
    }

    QString error_message;
    if (!save_profile_to_json(path, collect_config_form(), &error_message)) {
        QMessageBox::warning(this, QStringLiteral("Save Preset Failed"), error_message);
        statusBar()->showMessage(error_message, 3000);
        return;
    }

    config_action_status_ = QStringLiteral("saved preset to %1").arg(QFileInfo(path).fileName());
    update_config_status(session_.snapshot());
    statusBar()->showMessage(QStringLiteral("Saved current form as preset"), 2000);
}

void MainWindow::reload_config_draft() {
    if (!confirm_discard_dirty_form(QStringLiteral("reload the saved local draft"))) {
        statusBar()->showMessage(QStringLiteral("Reload draft canceled"), 2000);
        return;
    }

    load_config_draft(true);
    statusBar()->showMessage(QStringLiteral("Configuration draft reloaded"), 2000);
}

void MainWindow::reset_config_defaults() {
    if (!confirm_discard_dirty_form(QStringLiteral("reset the form to firmware-aligned defaults"))) {
        statusBar()->showMessage(QStringLiteral("Reset defaults canceled"), 2000);
        return;
    }

    config_profile_ = common::default_device_config_profile();
    populate_config_form(config_profile_);
    config_action_status_ = QStringLiteral("reset to firmware-aligned defaults (not yet saved)");
    update_config_status(session_.snapshot());
    statusBar()->showMessage(QStringLiteral("Configuration form reset to defaults"), 2000);
}

void MainWindow::mark_config_dirty() {
    if (suppress_config_tracking_) {
        return;
    }

    config_form_dirty_ = !profiles_equal(collect_config_form(), config_profile_);
    update_config_status(session_.snapshot());
}

void MainWindow::connect_config_inputs() {
    const auto mark_dirty = [this]() { mark_config_dirty(); };

    connect(config_protocol_combo_, qOverload<int>(&QComboBox::currentIndexChanged), this, [mark_dirty](int) { mark_dirty(); });
    connect(sensor_gpio_spin_, qOverload<int>(&QSpinBox::valueChanged), this, [mark_dirty](int) { mark_dirty(); });
    connect(sample_period_spin_, qOverload<int>(&QSpinBox::valueChanged), this, [mark_dirty](int) { mark_dirty(); });
    connect(heartbeat_period_spin_, qOverload<int>(&QSpinBox::valueChanged), this, [mark_dirty](int) { mark_dirty(); });
    connect(sensor_id_edit_, &QLineEdit::textChanged, this, [mark_dirty](const QString &) { mark_dirty(); });
    connect(fw_version_edit_, &QLineEdit::textChanged, this, [mark_dirty](const QString &) { mark_dirty(); });
    connect(valid_min_spin_, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [mark_dirty](double) { mark_dirty(); });
    connect(valid_max_spin_, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [mark_dirty](double) { mark_dirty(); });
    connect(real_overtemp_spin_, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [mark_dirty](double) { mark_dirty(); });
    connect(sim_min_spin_, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [mark_dirty](double) { mark_dirty(); });
    connect(sim_max_spin_, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [mark_dirty](double) { mark_dirty(); });
    connect(sim_step_spin_, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [mark_dirty](double) { mark_dirty(); });
    connect(sim_overtemp_spin_, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [mark_dirty](double) { mark_dirty(); });
}

bool MainWindow::load_profile_from_json(
    const QString &path,
    common::DeviceConfigProfile &profile,
    QString *error_message
) const {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        if (error_message != nullptr) {
            *error_message = QStringLiteral("Unable to open file: %1").arg(path);
        }
        return false;
    }

    QJsonParseError parse_error;
    const auto document = QJsonDocument::fromJson(file.readAll(), &parse_error);
    if (document.isNull() || !document.isObject()) {
        if (error_message != nullptr) {
            *error_message = QStringLiteral("Invalid JSON document: %1").arg(parse_error.errorString());
        }
        return false;
    }

    const auto object = document.object();
    if (object.contains(QStringLiteral("preferred_protocol"))) {
        const auto mode = common::protocol_mode_from_string(
            object.value(QStringLiteral("preferred_protocol")).toString().toStdString()
        );
        if (!mode.has_value()) {
            if (error_message != nullptr) {
                *error_message = QStringLiteral("Invalid preferred_protocol value");
            }
            return false;
        }
        profile.preferred_protocol = *mode;
    }

    profile.sensor_gpio = static_cast<std::uint32_t>(object.value(QStringLiteral("sensor_gpio")).toInt(profile.sensor_gpio));
    profile.sample_period_ms = static_cast<std::uint32_t>(object.value(QStringLiteral("sample_period_ms")).toInt(profile.sample_period_ms));
    profile.heartbeat_period_ms = static_cast<std::uint32_t>(object.value(QStringLiteral("heartbeat_period_ms")).toInt(profile.heartbeat_period_ms));
    profile.sensor_id = object.value(QStringLiteral("sensor_id")).toString(QString::fromStdString(profile.sensor_id)).trimmed().toStdString();
    profile.firmware_version =
        object.value(QStringLiteral("firmware_version")).toString(QString::fromStdString(profile.firmware_version)).trimmed().toStdString();
    profile.valid_min_temp_c = object.value(QStringLiteral("valid_min_temp_c")).toDouble(profile.valid_min_temp_c);
    profile.valid_max_temp_c = object.value(QStringLiteral("valid_max_temp_c")).toDouble(profile.valid_max_temp_c);
    profile.real_overtemp_threshold_c =
        object.value(QStringLiteral("real_overtemp_threshold_c")).toDouble(profile.real_overtemp_threshold_c);
    profile.simulated_min_temp_c =
        object.value(QStringLiteral("simulated_min_temp_c")).toDouble(profile.simulated_min_temp_c);
    profile.simulated_max_temp_c =
        object.value(QStringLiteral("simulated_max_temp_c")).toDouble(profile.simulated_max_temp_c);
    profile.simulated_step_c = object.value(QStringLiteral("simulated_step_c")).toDouble(profile.simulated_step_c);
    profile.simulated_overtemp_threshold_c =
        object.value(QStringLiteral("simulated_overtemp_threshold_c")).toDouble(profile.simulated_overtemp_threshold_c);

    return true;
}

bool MainWindow::save_profile_to_json(
    const QString &path,
    const common::DeviceConfigProfile &profile,
    QString *error_message
) const {
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        if (error_message != nullptr) {
            *error_message = QStringLiteral("Unable to write file: %1").arg(path);
        }
        return false;
    }

    const auto payload = QJsonDocument(profile_to_json_object(profile)).toJson(QJsonDocument::Indented);
    if (file.write(payload) < 0) {
        if (error_message != nullptr) {
            *error_message = QStringLiteral("Failed to write JSON payload to: %1").arg(path);
        }
        return false;
    }
    return true;
}

}  // namespace telemetry_platform::qt_monitor
