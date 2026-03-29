#include "main_window.hpp"

#include <QApplication>
#include <QFontDatabase>
#include <QFont>
#include <QMetaObject>
#include <QTimer>
#include <QStyleFactory>

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>

namespace {

struct LaunchOptions {
    telemetry_platform::qt_monitor::MonitorSessionConfig session;
    std::string screenshot_path;
    std::string tab_name;
    std::uint32_t quit_after_ms {0};
    std::uint32_t load_device_config_after_ms {0};
    bool query_config_on_start {false};
    bool show_help {false};
};

void print_usage() {
    std::cout
        << "usage: qt_monitor [--port COM3 | --replay PATH] [--baud 115200] "
        << "[--mode auto|jsonl|binary] [--replay-interval 250] "
        << "[--screenshot PATH] [--tab overview|trend|faults|config] [--quit-after-ms 2200] "
        << "[--query-config-on-start] [--load-device-config-after-ms 1500]\n";
}

LaunchOptions parse_args(int argc, char **argv) {
    LaunchOptions options {};
    options.session.mode = telemetry_platform::common::ProtocolMode::auto_detect;

    for (int index = 1; index < argc; ++index) {
        const std::string arg = argv[index];
        if (arg == "--port" && (index + 1) < argc) {
            options.session.port_name = argv[++index];
        } else if (arg == "--replay" && (index + 1) < argc) {
            options.session.replay_path = argv[++index];
        } else if (arg == "--baud" && (index + 1) < argc) {
            options.session.baud_rate = static_cast<std::uint32_t>(std::strtoul(argv[++index], nullptr, 10));
        } else if (arg == "--replay-interval" && (index + 1) < argc) {
            options.session.replay_interval_ms = static_cast<std::uint32_t>(std::strtoul(argv[++index], nullptr, 10));
        } else if (arg == "--mode" && (index + 1) < argc) {
            options.session.mode = telemetry_platform::common::protocol_mode_from_string(argv[++index])
                                       .value_or(telemetry_platform::common::ProtocolMode::auto_detect);
        } else if (arg == "--screenshot" && (index + 1) < argc) {
            options.screenshot_path = argv[++index];
        } else if (arg == "--tab" && (index + 1) < argc) {
            options.tab_name = argv[++index];
        } else if (arg == "--quit-after-ms" && (index + 1) < argc) {
            options.quit_after_ms = static_cast<std::uint32_t>(std::strtoul(argv[++index], nullptr, 10));
        } else if (arg == "--query-config-on-start") {
            options.query_config_on_start = true;
        } else if (arg == "--load-device-config-after-ms" && (index + 1) < argc) {
            options.load_device_config_after_ms =
                static_cast<std::uint32_t>(std::strtoul(argv[++index], nullptr, 10));
        } else if (arg == "--help" || arg == "-h") {
            options.show_help = true;
        }
    }

    if (options.session.port_name.empty() && options.session.replay_path.empty()) {
        options.session.port_name = "COM3";
    }
    return options;
}

void save_window_screenshot(QWidget &window, const std::string &path) {
    if (path.empty()) {
        return;
    }

    std::filesystem::path screenshot_path(path);
    if (screenshot_path.has_parent_path()) {
        std::filesystem::create_directories(screenshot_path.parent_path());
    }

    window.grab().save(QString::fromStdString(screenshot_path.string()));
}

void configure_application_font(QApplication &app) {
    QStringList preferred_families;

    const QString font_paths[] = {
        QStringLiteral("C:/Windows/Fonts/segoeui.ttf"),
        QStringLiteral("C:/Windows/Fonts/arial.ttf"),
        QStringLiteral("C:/Windows/Fonts/consola.ttf"),
    };

    for (const auto &font_path : font_paths) {
        const auto font_id = QFontDatabase::addApplicationFont(font_path);
        if (font_id >= 0) {
            const auto families = QFontDatabase::applicationFontFamilies(font_id);
            for (const auto &family : families) {
                if (!preferred_families.contains(family)) {
                    preferred_families.push_back(family);
                }
            }
        }
    }

    if (preferred_families.isEmpty()) {
        preferred_families = {QStringLiteral("Segoe UI"), QStringLiteral("Arial"), QStringLiteral("Consolas")};
    }

    QFont app_font;
    app_font.setFamilies(preferred_families);
    app_font.setPointSize(10);
    app_font.setHintingPreference(QFont::PreferFullHinting);
    app_font.setStyleStrategy(QFont::PreferAntialias);
    app.setFont(app_font);
}

}  // namespace

int main(int argc, char **argv) {
    QApplication app(argc, argv);
    const auto options = parse_args(argc, argv);

    if (options.show_help) {
        print_usage();
        return 0;
    }

    app.setStyle(QStyleFactory::create("Fusion"));
    configure_application_font(app);

    telemetry_platform::qt_monitor::MainWindow window(options.session);
    if (!options.tab_name.empty()) {
        window.set_active_tab(QString::fromStdString(options.tab_name));
    }
    window.show();

    if (options.query_config_on_start) {
        QTimer::singleShot(250, [&window]() {
            QMetaObject::invokeMethod(&window, "request_device_config", Qt::QueuedConnection);
        });
    }

    if (options.load_device_config_after_ms > 0) {
        QTimer::singleShot(static_cast<int>(options.load_device_config_after_ms), [&window]() {
            QMetaObject::invokeMethod(&window, "load_device_config_into_form", Qt::QueuedConnection);
        });
    }

    if (!options.screenshot_path.empty()) {
        const auto screenshot_delay_ms = static_cast<int>(std::max<std::uint32_t>(options.quit_after_ms > 0 ? options.quit_after_ms / 2 : 1200, 800));
        QTimer::singleShot(screenshot_delay_ms, [&window, screenshot_path = options.screenshot_path]() {
            save_window_screenshot(window, screenshot_path);
        });
    }

    if (options.quit_after_ms > 0) {
        QTimer::singleShot(static_cast<int>(options.quit_after_ms), &app, &QApplication::quit);
    }

    return app.exec();
}
