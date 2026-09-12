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

#include <media-io/video-io.h>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <vector>

/*
 * Per-frame bookkeeping. Kept in an array of its own so that writing metadata does not evict the
 * pixel data of neighbouring slots from cache.
 */
struct FrameMeta {
	uint64_t timestamp = 0; /* ns, OBS video clock */
	uint64_t seq = 0;
	uint32_t width = 0;
	uint32_t height = 0;
	uint32_t linesize[MAX_AV_PLANES] = {};
	bool starts_gap = false; /* first frame recorded after a pause */
};

struct RingConfig {
	uint32_t width = 0;
	uint32_t height = 0;
	video_format format = VIDEO_FORMAT_NONE;
	uint32_t linesize[MAX_AV_PLANES] = {};
	double duration_sec = 10.0;
	double fps = 0.0;       /* effective frame rate, after the frame rate divisor */
	uint64_t max_bytes = 0; /* hard ceiling; capacity is trimmed to fit */
};

/*
 * Single-producer / single-consumer ring of raw video frames.
 *
 * The producer is the libobs video-io thread (the raw video callback), the consumers are the
 * graphics thread (playback) and the Qt thread (status). Publication happens through a release
 * store of the head sequence, so a consumer that observes head == N is guaranteed to see the
 * full contents of slot N-1.
 *
 * All memory is allocated up front: the write path must not allocate, lock or log.
 */
class FrameRing {
public:
	FrameRing() = default;
	~FrameRing();

	FrameRing(const FrameRing &) = delete;
	FrameRing &operator=(const FrameRing &) = delete;

	/* Allocates the ring. Returns false if even a single frame does not fit the budget. */
	bool reconfigure(const RingConfig &config);
	void release();

	/* Producer side: called from the video-io thread only. */
	void write(const uint8_t *const source[MAX_AV_PLANES], const uint32_t source_linesize[MAX_AV_PLANES],
		   uint64_t timestamp, bool starts_gap);

	/* Consumer side. */
	uint64_t head() const { return head_.load(std::memory_order_acquire); }
	uint64_t oldest() const;
	bool read(uint64_t seq, FrameMeta &meta, const uint8_t *planes[MAX_AV_PLANES]) const;

	uint64_t capacity() const { return capacity_; }
	uint64_t bytes_allocated() const { return bytes_allocated_; }
	const RingConfig &config() const { return config_; }

	/* Seconds of footage currently held, derived from frame timestamps. */
	double buffered_seconds() const;

private:
	uint8_t *slot(uint64_t seq) const;

	RingConfig config_;
	std::vector<uint8_t *> chunks_;
	std::vector<FrameMeta> meta_;

	size_t slot_size_ = 0;
	size_t slots_per_chunk_ = 0;
	size_t plane_rows_[MAX_AV_PLANES] = {};
	size_t plane_offset_[MAX_AV_PLANES] = {};
	size_t plane_count_ = 0;

	uint64_t capacity_ = 0;
	uint64_t bytes_allocated_ = 0;

	std::atomic<uint64_t> head_{0};
};

/* Number of planes and their row counts for the formats the plugin supports. */
bool frame_plane_layout(video_format format, uint32_t height, size_t &plane_count, size_t rows[MAX_AV_PLANES]);
