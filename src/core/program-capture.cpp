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

#include "program-capture.hpp"

#include <plugin-support.h>
#include <util/platform.h>

#include <algorithm>

#ifdef _WIN32
#include <windows.h>
#elif defined(__APPLE__)
#include <mach/mach.h>
#include <mach/mach_host.h>
#endif

namespace {

/*
 * The video-io thread walks every raw callback in sequence before handing the frame to the
 * encoders, so anything slower than this shows up as encoder lag for the user.
 */
constexpr uint64_t kCopyBudgetNs = 1500000; /* 1.5 ms */

/* Destination stride per plane for tightly packed storage. */
bool plane_linesizes(video_format format, uint32_t width, uint32_t linesize[MAX_AV_PLANES])
{
	switch (format) {
	case VIDEO_FORMAT_NV12:
		linesize[0] = width;
		linesize[1] = width; /* interleaved UV, half the rows, full width */
		return true;
	case VIDEO_FORMAT_I420:
		linesize[0] = width;
		linesize[1] = (width + 1) / 2;
		linesize[2] = (width + 1) / 2;
		return true;
	case VIDEO_FORMAT_I444:
		linesize[0] = width;
		linesize[1] = width;
		linesize[2] = width;
		return true;
	case VIDEO_FORMAT_BGRA:
	case VIDEO_FORMAT_BGRX:
	case VIDEO_FORMAT_RGBA:
		linesize[0] = width * 4;
		return true;
	default:
		return false;
	}
}

const char *format_name(video_format format)
{
	switch (format) {
	case VIDEO_FORMAT_NV12:
		return "NV12";
	case VIDEO_FORMAT_I420:
		return "I420";
	case VIDEO_FORMAT_I444:
		return "I444";
	case VIDEO_FORMAT_BGRA:
		return "BGRA";
	case VIDEO_FORMAT_BGRX:
		return "BGRX";
	case VIDEO_FORMAT_RGBA:
		return "RGBA";
	default:
		return "unsupported";
	}
}

uint64_t available_physical_memory()
{
#ifdef _WIN32
	MEMORYSTATUSEX status = {};
	status.dwLength = sizeof(status);
	if (GlobalMemoryStatusEx(&status))
		return status.ullAvailPhys;
	return 0;
#elif defined(__APPLE__)
	vm_statistics64_data_t stats = {};
	mach_msg_type_number_t count = HOST_VM_INFO64_COUNT;
	if (host_statistics64(mach_host_self(), HOST_VM_INFO64, reinterpret_cast<host_info64_t>(&stats), &count) !=
	    KERN_SUCCESS)
		return 0;

	vm_size_t page_size = 0;
	if (host_page_size(mach_host_self(), &page_size) != KERN_SUCCESS)
		return 0;

	return static_cast<uint64_t>(stats.free_count + stats.inactive_count) * page_size;
#else
	return 0;
#endif
}

} // namespace

ProgramCapture &ProgramCapture::instance()
{
	static ProgramCapture capture;
	return capture;
}

uint64_t ProgramCapture::default_memory_budget()
{
	const uint64_t available = available_physical_memory();
	if (available == 0)
		return 1024ull * 1024ull * 1024ull; /* unknown: stay conservative at 1 GiB */

	/* Never take more than half of what is free: OBS, the browser sources and Windows need room. */
	return available / 2;
}

bool ProgramCapture::start(const CaptureSettings &settings)
{
	if (running())
		stop();

	error_.clear();

	obs_video_info ovi = {};
	if (!obs_get_video_info(&ovi)) {
		error_ = "video output is not initialised";
		obs_log(LOG_ERROR, "capture: %s", error_.c_str());
		return false;
	}

	RingConfig config;
	config.width = ovi.output_width;
	config.height = ovi.output_height;
	config.format = ovi.output_format;

	if (!plane_linesizes(config.format, config.width, config.linesize)) {
		error_ = std::string("unsupported colour format ") + format_name(config.format);
		obs_log(LOG_ERROR, "capture: %s (HDR and packed 4:2:2 formats are not supported yet)", error_.c_str());
		return false;
	}

	const uint32_t divisor = std::max<uint32_t>(1, settings.frame_rate_divisor);
	const double source_fps = ovi.fps_den ? static_cast<double>(ovi.fps_num) / ovi.fps_den : 0.0;
	if (source_fps <= 0.0) {
		error_ = "video output reports no frame rate";
		obs_log(LOG_ERROR, "capture: %s", error_.c_str());
		return false;
	}

	config.fps = source_fps / divisor;
	config.duration_sec = settings.duration_sec;
	config.max_bytes = settings.max_bytes ? settings.max_bytes : default_memory_budget();

	if (!ring_.reconfigure(config)) {
		error_ = "not enough memory for the requested buffer";
		obs_log(LOG_ERROR, "capture: %s (%.1f s at %ux%u %s)", error_.c_str(), config.duration_sec,
			config.width, config.height, format_name(config.format));
		return false;
	}

	settings_ = settings;
	settings_.frame_rate_divisor = divisor;
	width_ = config.width;
	height_ = config.height;
	fps_ = config.fps;

	frames_written_.store(0, std::memory_order_relaxed);
	frames_dropped_.store(0, std::memory_order_relaxed);
	copy_ns_total_.store(0, std::memory_order_relaxed);
	copy_ns_max_.store(0, std::memory_order_relaxed);
	slow_frames_.store(0, std::memory_order_relaxed);
	paused_.store(false, std::memory_order_release);
	pending_gap_.store(false, std::memory_order_release);
	running_.store(true, std::memory_order_release);

	/*
	 * Asking for exactly what the mix already produces keeps libobs from spinning up a swscale
	 * converter, which would run on this very thread for every subscriber.
	 */
	video_scale_info conversion = {};
	conversion.format = ovi.output_format;
	conversion.width = ovi.output_width;
	conversion.height = ovi.output_height;
	conversion.range = ovi.range;
	conversion.colorspace = ovi.colorspace;

	obs_add_raw_video_callback2(&conversion, divisor, raw_video_callback, this);

	const double capacity_sec = fps_ > 0.0 ? static_cast<double>(ring_.capacity()) / fps_ : 0.0;
	obs_log(LOG_INFO, "capture started: %ux%u %s, %.2f fps (divisor %u)", width_, height_,
		format_name(config.format), fps_, divisor);
	obs_log(LOG_INFO, "capture buffer: %llu frames, %.1f s, %.2f GiB",
		static_cast<unsigned long long>(ring_.capacity()), capacity_sec,
		static_cast<double>(ring_.bytes_allocated()) / (1024.0 * 1024.0 * 1024.0));
	if (capacity_sec + 0.05 < settings.duration_sec)
		obs_log(LOG_WARNING, "capture buffer trimmed to %.1f s (asked for %.1f s) by the memory budget",
			capacity_sec, settings.duration_sec);
	return true;
}

void ProgramCapture::stop()
{
	if (!running())
		return;

	/* Remove the callback before releasing the ring it writes into. */
	obs_remove_raw_video_callback(raw_video_callback, this);
	running_.store(false, std::memory_order_release);
	ring_.release();

	obs_log(LOG_INFO, "capture stopped");
}

void ProgramCapture::set_paused(bool paused)
{
	const bool was_paused = paused_.exchange(paused, std::memory_order_acq_rel);
	if (was_paused && !paused)
		pending_gap_.store(true, std::memory_order_release);
}

void ProgramCapture::raw_video_callback(void *param, video_data *frame)
{
	static_cast<ProgramCapture *>(param)->on_frame(frame);
}

void ProgramCapture::on_frame(video_data *frame)
{
	if (!frame || paused_.load(std::memory_order_acquire))
		return;

	const RingConfig &config = ring_.config();
	if (frame->data[0] == nullptr)
		return;

	/*
	 * A profile switch (obs_reset_video) changes frame geometry underneath us. Detecting it is
	 * two comparisons; rebuilding the ring happens outside this callback.
	 */
	if (config.width == 0 || config.height == 0) {
		frames_dropped_.fetch_add(1, std::memory_order_relaxed);
		return;
	}

	const bool starts_gap = pending_gap_.exchange(false, std::memory_order_acq_rel);

	const uint64_t started = os_gettime_ns();
	ring_.write(frame->data, frame->linesize, frame->timestamp, starts_gap);
	const uint64_t elapsed = os_gettime_ns() - started;

	frames_written_.fetch_add(1, std::memory_order_relaxed);
	copy_ns_total_.fetch_add(elapsed, std::memory_order_relaxed);

	uint64_t previous_max = copy_ns_max_.load(std::memory_order_relaxed);
	while (elapsed > previous_max &&
	       !copy_ns_max_.compare_exchange_weak(previous_max, elapsed, std::memory_order_relaxed))
		;

	if (elapsed > kCopyBudgetNs)
		slow_frames_.fetch_add(1, std::memory_order_relaxed);
}

void ProgramCapture::poll_video_settings()
{
	if (!running())
		return;

	obs_video_info ovi = {};
	if (!obs_get_video_info(&ovi))
		return;

	const RingConfig &config = ring_.config();
	if (ovi.output_width == config.width && ovi.output_height == config.height &&
	    ovi.output_format == config.format)
		return;

	obs_log(LOG_INFO, "video settings changed (%ux%u -> %ux%u), restarting capture", config.width, config.height,
		ovi.output_width, ovi.output_height);

	const CaptureSettings previous = settings_;
	stop();
	start(previous);
}

CaptureStatus ProgramCapture::status() const
{
	CaptureStatus status;
	status.running = running();
	status.paused = paused();
	status.width = width_;
	status.height = height_;
	status.fps = fps_;
	status.error = error_;

	if (!status.running)
		return status;

	status.buffered_sec = ring_.buffered_seconds();
	status.capacity_sec = fps_ > 0.0 ? static_cast<double>(ring_.capacity()) / fps_ : 0.0;
	status.bytes = ring_.bytes_allocated();
	status.frames_written = frames_written_.load(std::memory_order_relaxed);
	status.frames_dropped = frames_dropped_.load(std::memory_order_relaxed);
	status.slow_frames = slow_frames_.load(std::memory_order_relaxed);

	const uint64_t written = status.frames_written;
	if (written > 0) {
		status.copy_ms_avg =
			static_cast<double>(copy_ns_total_.load(std::memory_order_relaxed)) / written / 1000000.0;
	}
	status.copy_ms_max = static_cast<double>(copy_ns_max_.load(std::memory_order_relaxed)) / 1000000.0;
	return status;
}
