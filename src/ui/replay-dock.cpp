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

#include "replay-dock.hpp"

#include <obs-module.h>
#include <plugin-support.h>

#include <QButtonGroup>
#include <QCheckBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QFont>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QProgressBar>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>

namespace {

constexpr int kStatusIntervalMs = 100; /* 10 Hz is enough for a buffer gauge */
constexpr int kSpeeds[] = {25, 50, 75, 100};

QLabel *makeBadge(const QString &text)
{
	auto *label = new QLabel(text);
	QFont font = label->font();
	font.setBold(true);
	label->setFont(font);
	label->setAlignment(Qt::AlignCenter);
	/* Colours come from the palette so the badge stays readable in every OBS theme. */
	label->setFrameShape(QFrame::StyledPanel);
	label->setMinimumWidth(70);
	return label;
}

} // namespace

ReplayDock::ReplayDock(QWidget *parent) : QWidget(parent)
{
	auto *layout = new QVBoxLayout(this);
	layout->setContentsMargins(8, 8, 8, 8);
	layout->setSpacing(8);

	layout->addWidget(buildStatusRow());
	layout->addWidget(buildCaptureRow());
	layout->addWidget(buildSpeedRow());
	layout->addWidget(buildEventsBox(), 1);
	layout->addWidget(buildTransportRow());

	setMinimumWidth(320);

	status_timer = new QTimer(this);
	connect(status_timer, &QTimer::timeout, this, &ReplayDock::refreshStatus);
	status_timer->start(kStatusIntervalMs);
}

ReplayDock::~ReplayDock() = default;

QWidget *ReplayDock::buildStatusRow()
{
	auto *box = new QWidget(this);
	auto *outer = new QVBoxLayout(box);
	outer->setContentsMargins(0, 0, 0, 0);
	outer->setSpacing(4);

	auto *top = new QHBoxLayout();
	buffer_bar = new QProgressBar(box);
	buffer_bar->setRange(0, 1000);
	buffer_bar->setValue(0);
	buffer_bar->setTextVisible(false);
	buffer_bar->setFixedHeight(12);

	buffer_label = new QLabel(QStringLiteral("0.0 / 0.0 s"), box);
	onair_label = makeBadge(obs_module_text("Replay.OnAir.Live"));

	top->addWidget(buffer_bar, 1);
	top->addWidget(buffer_label);
	top->addWidget(onair_label);

	format_label = new QLabel(obs_module_text("Replay.Status.NoBuffer"), box);
	format_label->setEnabled(false);

	outer->addLayout(top);
	outer->addWidget(format_label);
	return box;
}

QWidget *ReplayDock::buildCaptureRow()
{
	auto *box = new QWidget(this);
	auto *row = new QHBoxLayout(box);
	row->setContentsMargins(0, 0, 0, 0);

	mark_button = new QPushButton(obs_module_text("Replay.Mark"), box);
	mark_button->setMinimumHeight(56);
	QFont mark_font = mark_button->font();
	mark_font.setBold(true);
	mark_font.setPointSize(mark_font.pointSize() + 2);
	mark_button->setFont(mark_font);
	connect(mark_button, &QPushButton::clicked, this, &ReplayDock::onMark);

	auto *form = new QFormLayout();
	form->setContentsMargins(0, 0, 0, 0);

	/* A goal is roughly "4 s before the whistle, 2 s after", hence two numbers, not a range. */
	length_spin = new QDoubleSpinBox(box);
	length_spin->setRange(1.0, 30.0);
	length_spin->setSingleStep(0.5);
	length_spin->setValue(6.0);
	length_spin->setSuffix(QStringLiteral(" s"));

	offset_spin = new QDoubleSpinBox(box);
	offset_spin->setRange(0.0, 30.0);
	offset_spin->setSingleStep(0.5);
	offset_spin->setValue(4.0);
	offset_spin->setSuffix(QStringLiteral(" s"));

	form->addRow(obs_module_text("Replay.Length"), length_spin);
	form->addRow(obs_module_text("Replay.Offset"), offset_spin);

	row->addWidget(mark_button, 1);
	row->addLayout(form, 1);
	return box;
}

QWidget *ReplayDock::buildSpeedRow()
{
	auto *box = new QWidget(this);
	auto *row = new QHBoxLayout(box);
	row->setContentsMargins(0, 0, 0, 0);
	row->addWidget(new QLabel(obs_module_text("Replay.Speed"), box));

	speed_group = new QButtonGroup(this);
	speed_group->setExclusive(true);

	for (int speed : kSpeeds) {
		auto *button = new QPushButton(QStringLiteral("%1%").arg(speed), box);
		button->setCheckable(true);
		button->setChecked(speed == 100);
		speed_group->addButton(button, speed);
		row->addWidget(button, 1);
	}

	connect(speed_group, &QButtonGroup::idClicked, this, &ReplayDock::onSpeedChanged);
	return box;
}

QWidget *ReplayDock::buildEventsBox()
{
	auto *box = new QGroupBox(obs_module_text("Replay.Events"), this);
	auto *layout = new QVBoxLayout(box);
	layout->setContentsMargins(6, 6, 6, 6);

	events_list = new QListWidget(box);
	events_list->setAlternatingRowColors(true);
	layout->addWidget(events_list);

	auto *hint = new QLabel(obs_module_text("Replay.Events.Hint"), box);
	hint->setWordWrap(true);
	hint->setEnabled(false);
	layout->addWidget(hint);
	return box;
}

QWidget *ReplayDock::buildTransportRow()
{
	auto *box = new QWidget(this);
	auto *outer = new QVBoxLayout(box);
	outer->setContentsMargins(0, 0, 0, 0);
	outer->setSpacing(6);

	auto *row = new QHBoxLayout();
	play_button = new QPushButton(obs_module_text("Replay.Play"), box);
	play_button->setMinimumHeight(40);
	stop_button = new QPushButton(obs_module_text("Replay.Stop"), box);
	stop_button->setMinimumHeight(40);
	connect(play_button, &QPushButton::clicked, this, &ReplayDock::onPlay);
	connect(stop_button, &QPushButton::clicked, this, &ReplayDock::onStop);

	row->addWidget(play_button, 3);
	row->addWidget(stop_button, 1);

	auto_return_check = new QCheckBox(obs_module_text("Replay.AutoReturn"), box);
	auto_return_check->setChecked(true);

	outer->addLayout(row);
	outer->addWidget(auto_return_check);
	return box;
}

void ReplayDock::onMark()
{
	/* M3 wires this to the ring buffer; for now it only proves the UI plumbing works. */
	obs_log(LOG_INFO, "MARK requested (length %.1f s, offset %.1f s)", length_spin->value(),
		offset_spin->value());
}

void ReplayDock::onPlay()
{
	obs_log(LOG_INFO, "PLAY requested at %d%%", speed_percent);
}

void ReplayDock::onStop()
{
	obs_log(LOG_INFO, "STOP requested");
}

void ReplayDock::onSpeedChanged(int percent)
{
	speed_percent = percent;
	obs_log(LOG_INFO, "speed set to %d%%", percent);
}

void ReplayDock::refreshStatus()
{
	/* Placeholder until the capture engine lands in M1: keeps the timer path exercised. */
}
