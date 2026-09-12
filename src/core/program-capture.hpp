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

#include "frame-ring.hpp"

#include <obs.h>

#include <atomic>
#include <cstdint>
#include <string>

struct CaptureSettings {
	double duration_sec = 10.0;
	uint32_t frame_rate_divisor = 1; /* 1 = every frame, 2 = every other frame, ... */
	uint64_t max_bytes = 0;          /* 0 = derive from available physical memory */
};

struct CaptureStatus {
	bool running = false;
	bool paused = false;
	uint32_t width = 0;
	uint32_t height = 0;
	double fps = 0.0;
	double buffered_sec = 0.0;
	double capacity_sec = 0.0;
	uint64_t bytes = 0;
	uint64_t frames_written = 0;
	uint64_t frames_dropped = 0;
	double copy_ms_avg = 0.0;
	double copy_ms_max = 0.0;
	uint64_t slow_frames = 0; /* callbacks that took longer than the budget */
	std::string error;
};

/*
 * Owns the raw video callback on the OBS program mix and the ring it feeds.
 *
 * The callback runs on the libobs video-io thread, which also hands frames to the encoders: it
 * must never allocate, lock or log, or the user gets "skipped frames due to encoding lag" live
 * on air.
 */
class ProgramCapture {
public:
	static ProgramCapture &instance();

	bool start(const CaptureSettings &settings);
	void stop();
	bool running() const { return running_.load(std::memory_order_acquire); }

	/*
	 * Recording is suspended while a replay is on program, otherwise the replay itself would be
	 * captured back into the ring. The first frame recorded afterwards is flagged as starting a
	 * gap so clip selection can clamp to it.
	 */
	void set_paused(bool paused);
	bool paused() const { return paused_.load(std::memory_order_acquire); }

	/*
	 * Applying new video settings (obs_reset_video) raises no frontend event, so the geometry is
	 * polled from the UI thread; a mismatch means libobs has quietly inserted a scaler for us and
	 * the ring has to be rebuilt. Must be called from the Qt thread.
	 */
	void poll_video_settings();

	CaptureStatus status() const;
	FrameRing &ring() { return ring_; }
	const CaptureSettings &settings() const { return settings_; }

	/* Bytes of physical memory the ring is allowed to take when max_bytes is left at 0. */
	static uint64_t default_memory_budget();

private:
	ProgramCapture() = default;

	static void raw_video_callback(void *param, video_data *frame);
	void on_frame(video_data *frame);

	FrameRing ring_;
	CaptureSettings settings_;
	std::string error_;

	uint32_t width_ = 0;
	uint32_t height_ = 0;
	double fps_ = 0.0;

	std::atomic<bool> running_{false};
	std::atomic<bool> paused_{false};
	std::atomic<bool> pending_gap_{false};

	std::atomic<uint64_t> frames_written_{0};
	std::atomic<uint64_t> frames_dropped_{0};
	std::atomic<uint64_t> copy_ns_total_{0};
	std::atomic<uint64_t> copy_ns_max_{0};
	std::atomic<uint64_t> slow_frames_{0};
};
