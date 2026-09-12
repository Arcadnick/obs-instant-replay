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

/*
 * Buffer timeline. The axis is "seconds before the live edge": 0 on the right is now, the left end
 * is as far back as the ring reaches. A QSlider cannot show two draggable handles over a filled
 * region, hence the custom painting.
 */
class ReplayTimeline : public QWidget {
	Q_OBJECT

public:
	explicit ReplayTimeline(QWidget *parent = nullptr);

	/* span: how far back the ring can reach, filled: how much of it actually holds footage. */
	void setBuffer(double span_sec, double filled_sec);

	/* Selection, in seconds before the live edge (in_sec > out_sec >= 0). */
	void setSelection(double in_sec, double out_sec);
	void clearSelection();
	bool hasSelection() const { return has_selection; }

	/* Playhead position inside the selection, in seconds from the in point; negative hides it. */
	void setPlayhead(double position_sec);

	QSize sizeHint() const override;

signals:
	void selectionChanged(double in_sec, double out_sec);

protected:
	void paintEvent(QPaintEvent *event) override;
	void mousePressEvent(QMouseEvent *event) override;
	void mouseMoveEvent(QMouseEvent *event) override;
	void mouseReleaseEvent(QMouseEvent *event) override;

private:
	enum class Handle { None, In, Out };

	double secondsAt(int x) const;
	int xForSeconds(double seconds) const;

	double span_sec = 10.0;
	double filled_sec = 0.0;
	double in_sec = 0.0;
	double out_sec = 0.0;
	double playhead_sec = -1.0;
	bool has_selection = false;

	Handle dragging = Handle::None;
};
