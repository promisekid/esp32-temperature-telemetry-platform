#include "trend_widget.hpp"

#include <QPainter>
#include <QPainterPath>

#include <algorithm>
#include <cmath>
#include <utility>

namespace telemetry_platform::qt_monitor {

namespace {

constexpr QColor kBackground(14, 20, 27);
constexpr QColor kPanel(18, 26, 35);
constexpr QColor kBorder(35, 51, 66);
constexpr QColor kGrid(44, 62, 79);
constexpr QColor kAxisText(145, 166, 188);
constexpr QColor kRealColor(65, 196, 255);
constexpr QColor kSimColor(255, 154, 92);
constexpr QColor kOfflineMask(10, 15, 19, 140);

QColor series_color(std::size_t channel_index) {
    return channel_index == 0 ? kRealColor : kSimColor;
}

QString series_name(std::size_t channel_index) {
    return channel_index == 0 ? QStringLiteral("channel_0 real") : QStringLiteral("channel_1 simulated");
}

}  // namespace

TrendWidget::TrendWidget(QWidget *parent) : QWidget(parent) {
    setMinimumHeight(320);
}

void TrendWidget::set_state(common::DeviceState state) {
    state_ = std::move(state);
    update();
}

void TrendWidget::paintEvent(QPaintEvent *event) {
    QWidget::paintEvent(event);

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.fillRect(rect(), kBackground);

    const QRect panel_rect = rect().adjusted(10, 10, -10, -10);
    painter.setPen(QPen(kBorder, 1));
    painter.setBrush(kPanel);
    painter.drawRoundedRect(panel_rect, 14, 14);

    const QRect plot_rect = panel_rect.adjusted(56, 34, -20, -48);

    painter.setPen(QPen(kGrid, 1));
    for (int index = 0; index <= 4; ++index) {
        const auto y = plot_rect.top() + (plot_rect.height() * index) / 4;
        painter.drawLine(plot_rect.left(), y, plot_rect.right(), y);
    }
    for (int index = 0; index <= 5; ++index) {
        const auto x = plot_rect.left() + (plot_rect.width() * index) / 5;
        painter.drawLine(x, plot_rect.top(), x, plot_rect.bottom());
    }

    painter.setPen(QPen(kBorder, 1));
    painter.drawRect(plot_rect);

    if (state_.trend_history.empty()) {
        painter.setPen(Qt::white);
        painter.drawText(plot_rect, Qt::AlignCenter, QStringLiteral("Waiting for telemetry history"));
        return;
    }

    double min_temp = 9999.0;
    double max_temp = -9999.0;
    std::uint64_t min_host_ms = 0;
    std::uint64_t max_host_ms = 0;
    bool has_point = false;

    for (const auto &channel_history : state_.trend_history) {
        if (channel_history.empty()) {
            continue;
        }

        if (!has_point) {
            min_host_ms = channel_history.front().host_timestamp_ms;
            max_host_ms = channel_history.back().host_timestamp_ms;
            has_point = true;
        } else {
            min_host_ms = std::min(min_host_ms, channel_history.front().host_timestamp_ms);
            max_host_ms = std::max(max_host_ms, channel_history.back().host_timestamp_ms);
        }

        for (const auto &point : channel_history) {
            min_temp = std::min(min_temp, point.temperature_c);
            max_temp = std::max(max_temp, point.temperature_c);
        }
    }

    if (!has_point) {
        painter.setPen(Qt::white);
        painter.drawText(plot_rect, Qt::AlignCenter, QStringLiteral("Trend buffer is empty"));
        return;
    }

    if (std::abs(max_temp - min_temp) < 0.5) {
        min_temp -= 1.0;
        max_temp += 1.0;
    } else {
        min_temp -= 0.3;
        max_temp += 0.3;
    }

    const auto time_span = std::max<std::uint64_t>(1, max_host_ms - min_host_ms);
    const auto temp_span = std::max(0.1, max_temp - min_temp);

    for (std::size_t channel_index = 0; channel_index < state_.trend_history.size() && channel_index < 2; ++channel_index) {
        const auto &channel_history = state_.trend_history[channel_index];
        if (channel_history.size() < 2) {
            continue;
        }

        QPainterPath path;
        bool first_point = true;
        for (const auto &point : channel_history) {
            const double x_ratio = static_cast<double>(point.host_timestamp_ms - min_host_ms) / static_cast<double>(time_span);
            const double y_ratio = (point.temperature_c - min_temp) / temp_span;
            const QPointF plot_point(
                plot_rect.left() + x_ratio * plot_rect.width(),
                plot_rect.bottom() - y_ratio * plot_rect.height()
            );

            if (first_point) {
                path.moveTo(plot_point);
                first_point = false;
            } else {
                path.lineTo(plot_point);
            }
        }

        painter.setPen(QPen(series_color(channel_index), 2.5));
        painter.drawPath(path);
    }

    painter.setPen(kAxisText);
    painter.drawText(
        QRect(plot_rect.left(), panel_rect.top() + 8, plot_rect.width(), 18),
        Qt::AlignLeft | Qt::AlignVCenter,
        QStringLiteral("Temperature trend")
    );
    painter.drawText(
        QRect(plot_rect.left(), plot_rect.bottom() + 10, plot_rect.width(), 18),
        Qt::AlignLeft | Qt::AlignVCenter,
        QStringLiteral("window: last 120 s")
    );
    painter.drawText(
        QRect(plot_rect.left(), plot_rect.bottom() + 10, plot_rect.width(), 18),
        Qt::AlignRight | Qt::AlignVCenter,
        QStringLiteral("range: %1C .. %2C").arg(min_temp, 0, 'f', 1).arg(max_temp, 0, 'f', 1)
    );

    for (int tick = 0; tick <= 4; ++tick) {
        const double value = max_temp - (temp_span * tick) / 4.0;
        const auto y = plot_rect.top() + (plot_rect.height() * tick) / 4;
        painter.drawText(QRect(panel_rect.left() + 8, y - 8, 40, 16), Qt::AlignRight | Qt::AlignVCenter, QStringLiteral("%1").arg(value, 0, 'f', 1));
    }

    const QRect legend_rect(panel_rect.right() - 280, panel_rect.top() + 8, 260, 22);
    for (int index = 0; index < 2; ++index) {
        const auto item_rect = QRect(legend_rect.left() + index * 128, legend_rect.top(), 120, legend_rect.height());
        painter.setBrush(series_color(static_cast<std::size_t>(index)));
        painter.setPen(Qt::NoPen);
        painter.drawRoundedRect(QRect(item_rect.left(), item_rect.top() + 5, 14, 10), 3, 3);
        painter.setPen(kAxisText);
        painter.drawText(item_rect.adjusted(22, 0, 0, 0), Qt::AlignLeft | Qt::AlignVCenter, series_name(static_cast<std::size_t>(index)));
    }

    if (!state_.online) {
        painter.fillRect(plot_rect, kOfflineMask);
        painter.setPen(QColor(255, 220, 130));
        painter.drawText(plot_rect, Qt::AlignCenter, QStringLiteral("device offline"));
    }
}

}  // namespace telemetry_platform::qt_monitor
