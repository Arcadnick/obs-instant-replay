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

#pragma once

#include <QWidget>

class QButtonGroup;
class QCheckBox;
class QDoubleSpinBox;
class QLabel;
class QListWidget;
class QProgressBar;
class QPushButton;
class QTimer;

/*
 * Operator panel. Every widget here lives on the Qt thread only: the capture/playback core
 * runs on the OBS video thread and must reach this class through queued invocations.
 */
class ReplayDock : public QWidget {
	Q_OBJECT

public:
	explicit ReplayDock(QWidget *parent = nullptr);
	~ReplayDock() override;

private slots:
	void onMark();
	void onPlay();
	void onStop();
	void onSpeedChanged(int speed_percent);
	void refreshStatus();

private:
	QWidget *buildStatusRow();
	QWidget *buildCaptureRow();
	QWidget *buildSpeedRow();
	QWidget *buildEventsBox();
	QWidget *buildTransportRow();

	/* status row */
	QProgressBar *buffer_bar = nullptr;
	QLabel *buffer_label = nullptr;
	QLabel *format_label = nullptr;
	QLabel *onair_label = nullptr;

	/* capture settings */
	QDoubleSpinBox *length_spin = nullptr;
	QDoubleSpinBox *offset_spin = nullptr;

	/* transport */
	QButtonGroup *speed_group = nullptr;
	QPushButton *mark_button = nullptr;
	QPushButton *play_button = nullptr;
	QPushButton *stop_button = nullptr;
	QCheckBox *auto_return_check = nullptr;
	QListWidget *events_list = nullptr;

	QTimer *status_timer = nullptr;

	int speed_percent = 100;
};
