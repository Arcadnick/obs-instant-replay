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

#include "playback-engine.hpp"

#include <obs.h>

#include <cstdint>

/*
 * Puts a replay on program and brings the live scene back afterwards.
 *
 * Everything here runs on the Qt thread: obs_frontend_set_current_scene blocks waiting for the UI
 * thread, so calling it from the graphics thread or from a raw callback deadlocks OBS.
 */
class ReplayDirector {
public:
	static ReplayDirector &instance();

	bool play_to_program(const Clip &clip, double speed, bool auto_return);

	/* Stops playback and returns to the live scene immediately. */
	void stop();

	/* Called from the dock's timer: finishes playback, returns to live, resumes recording. */
	void poll();

	bool on_air() const { return active_; }

	/* Releases scene references; call before the scene collection goes away. */
	void reset();

private:
	ReplayDirector() = default;

	obs_source_t *ensure_replay_scene();
	void finish(bool return_to_live);

	obs_weak_source_t *return_scene_ = nullptr;
	obs_weak_source_t *replay_scene_ = nullptr;
	bool active_ = false;
	bool auto_return_ = true;
	uint64_t watchdog_deadline_ns_ = 0;
	uint64_t resume_capture_at_ns_ = 0;
};
