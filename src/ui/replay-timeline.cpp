/*
Instant Replay for OBS
Copyright (C) 2026 Trinity AEM

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License along
with this program. If not, see <https://www.gnu.org/licenses/>
*/

#include "replay-timeline.hpp"

#include <QMouseEvent>
#include <QPainter>
#include <QPalette>

#include <algorithm>
#include <cmath>

namespace {

constexpr int kTrackHeight = 18;
constexpr int kSideMargin = 8;
constexpr int kHandleGrab = 7;
constexpr double kMinSelection = 0.2;

} // namespace

ReplayTimeline::ReplayTimeline(QWidget *parent) : QWidget(parent)
{
	setMinimumHeight(44);
	setMouseTracking(true);
}

QSize ReplayTimeline::sizeHint() const
{
	return QSize(280, 44);
}

void ReplayTimeline::setBuffer(double span, double filled)
{
	span_sec = std::max(1.0, span);
	filled_sec = std::clamp(filled, 0.0, span_sec);
	update();
}

void ReplayTimeline::setSelection(double in_value, double out_value)
{
	in_sec = std::max(in_value, out_value + kMinSelection);
	out_sec = std::max(0.0, out_value);
	has_selection = true;
	update();
}

void ReplayTimeline::clearSelection()
{
	has_selection = false;
	playhead_sec = -1.0;
	update();
}

void ReplayTimeline::setPlayhead(double position_sec)
{
	playhead_sec = position_sec;
	update();
}

double ReplayTimeline::secondsAt(int x) const
{
	const int usable = std::max(1, width() - 2 * kSideMargin);
	const double ratio = std::clamp(static_cast<double>(x - kSideMargin) / usable, 0.0, 1.0);
	/* Left edge is the oldest footage, right edge is the live edge. */
	return span_sec * (1.0 - ratio);
}

int ReplayTimeline::xForSeconds(double seconds) const
{
	const int usable = std::max(1, width() - 2 * kSideMargin);
	const double ratio = std::clamp(1.0 - seconds / span_sec, 0.0, 1.0);
	return kSideMargin + static_cast<int>(std::lround(ratio * usable));
}

void ReplayTimeline::paintEvent(QPaintEvent *)
{
	QPainter painter(this);
	painter.setRenderHint(QPainter::Antialiasing, false);

	const QPalette &colours = palette();
	const int track_top = (height() - kTrackHeight) / 2;
	const QRect track(kSideMargin, track_top, width() - 2 * kSideMargin, kTrackHeight);

	painter.fillRect(track, colours.color(QPalette::Base));

	/* Footage actually held by the ring. */
	const QRect filled(xForSeconds(filled_sec), track.top(), track.right() - xForSeconds(filled_sec) + 1,
			   track.height());
	painter.fillRect(filled, colours.color(QPalette::AlternateBase));

	if (has_selection) {
		const int left = xForSeconds(in_sec);
		const int right = xForSeconds(out_sec);
		QRect selection(left, track.top(), std::max(1, right - left), track.height());

		QColor highlight = colours.color(QPalette::Highlight);
		highlight.setAlpha(140);
		painter.fillRect(selection, highlight);

		painter.setPen(colours.color(QPalette::Highlight));
		painter.drawRect(selection.adjusted(0, 0, -1, -1));

		/* IN and OUT handles. */
		painter.setBrush(colours.color(QPalette::Highlight));
		painter.drawRect(QRect(left - 2, track.top() - 3, 4, track.height() + 6));
		painter.drawRect(QRect(right - 2, track.top() - 3, 4, track.height() + 6));

		if (playhead_sec >= 0.0) {
			const double position = in_sec - playhead_sec;
			if (position >= out_sec - kMinSelection) {
				painter.setPen(QPen(colours.color(QPalette::BrightText), 2));
				const int x = xForSeconds(position);
				painter.drawLine(x, track.top() - 4, x, track.bottom() + 4);
			}
		}
	}

	painter.setPen(colours.color(QPalette::WindowText));
	painter.setBrush(Qt::NoBrush);
	painter.drawRect(track.adjusted(0, 0, -1, -1));

	QFont small = font();
	small.setPointSizeF(std::max(7.0, small.pointSizeF() - 1.5));
	painter.setFont(small);
	painter.setPen(colours.color(QPalette::PlaceholderText));
	painter.drawText(QRect(kSideMargin, 0, 60, track_top), Qt::AlignLeft | Qt::AlignVCenter,
			 QStringLiteral("-%1 s").arg(span_sec, 0, 'f', 0));
	painter.drawText(QRect(width() - kSideMargin - 60, 0, 60, track_top), Qt::AlignRight | Qt::AlignVCenter,
			 QStringLiteral("live"));
}

void ReplayTimeline::mousePressEvent(QMouseEvent *event)
{
	if (!has_selection || event->button() != Qt::LeftButton) {
		QWidget::mousePressEvent(event);
		return;
	}

	const int x = static_cast<int>(event->position().x());
	if (std::abs(x - xForSeconds(in_sec)) <= kHandleGrab)
		dragging = Handle::In;
	else if (std::abs(x - xForSeconds(out_sec)) <= kHandleGrab)
		dragging = Handle::Out;
	else
		dragging = Handle::None;
}

void ReplayTimeline::mouseMoveEvent(QMouseEvent *event)
{
	if (!has_selection) {
		QWidget::mouseMoveEvent(event);
		return;
	}

	const int x = static_cast<int>(event->position().x());
	if (dragging == Handle::None) {
		const bool over_handle = std::abs(x - xForSeconds(in_sec)) <= kHandleGrab ||
					 std::abs(x - xForSeconds(out_sec)) <= kHandleGrab;
		setCursor(over_handle ? Qt::SizeHorCursor : Qt::ArrowCursor);
		return;
	}

	const double seconds = secondsAt(x);
	if (dragging == Handle::In)
		in_sec = std::clamp(seconds, out_sec + kMinSelection, filled_sec);
	else
		out_sec = std::clamp(seconds, 0.0, in_sec - kMinSelection);

	update();
	emit selectionChanged(in_sec, out_sec);
}

void ReplayTimeline::mouseReleaseEvent(QMouseEvent *event)
{
	if (dragging != Handle::None) {
		dragging = Handle::None;
		emit selectionChanged(in_sec, out_sec);
	}

	QWidget::mouseReleaseEvent(event);
}
