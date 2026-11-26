#include "stinger-analyzer.hpp"

#include <graphics/graphics.h>
#include <graphics/vec4.h>

#include <algorithm>
#include <cmath>

static constexpr uint32_t kW = 64, kH = 36; // measured at this size
static constexpr int kPollMs = 50;
static constexpr int kStartTicks = 5000 / kPollMs; // to open the file
static constexpr int kMaxTicks = 60000 / kPollMs;  // give up after a minute

StingerAnalyzer::StingerAnalyzer(QObject *parent) : QObject(parent)
{
	timer_.setInterval(kPollMs);
	connect(&timer_, &QTimer::timeout, this, &StingerAnalyzer::poll);
}

StingerAnalyzer::~StingerAnalyzer()
{
	cancel();
}

void StingerAnalyzer::analyze(obs_source_t *stinger)
{
	cancel();
	OBSDataAutoRelease s = obs_source_get_settings(stinger);
	const bool matte = obs_data_get_bool(s, "track_matte_enabled");
	const int layout = (int)obs_data_get_int(s, "track_matte_layout"); // horizontal, vertical, separate, mask
	const char *path = obs_data_get_string(s, matte && layout == 2 ? "track_matte_path" : "path");
	mode_ = !matte        ? Mode::Alpha
		: layout == 0 ? Mode::MatteRight
		: layout == 1 ? Mode::MatteBottom
			      : Mode::MatteFull;
	invert_ = matte && obs_data_get_bool(s, "invert_matte");
	separateMatte_ = matte && layout == 2;

	// Plays as soon as it opens; never shown anywhere.
	OBSDataAutoRelease settings = obs_data_create();
	obs_data_set_string(settings, "local_file", path);
	obs_data_set_bool(settings, "restart_on_activate", false);
	obs_data_set_bool(settings, "looping", false);
	OBSSourceAutoRelease media = obs_source_create_private("ffmpeg_source", "tm-stinger-analyzer", settings);
	if (!media || !*path) {
		Result r;
		r.error = tr("Choose a video file first.");
		emit finished(r);
		return;
	}

	media_ = media.Get();
	samples_.clear();
	audio_ = false;
	ticks_ = 0;
	obs_source_inc_active(media_); // as a Stinger's own video is
	obs_source_add_audio_capture_callback(media_, audio, this);
	obs_add_main_render_callback(render, this);
	timer_.start();
}

void StingerAnalyzer::cancel()
{
	timer_.stop();
	if (!media_)
		return;
	// Once these return, neither callback is running or will run again.
	obs_remove_main_render_callback(render, this);
	obs_source_remove_audio_capture_callback(media_, audio, this);
	obs_source_dec_active(media_);
	media_ = nullptr;
	if (texrender_) {
		obs_enter_graphics();
		gs_texrender_destroy(texrender_);
		gs_stagesurface_destroy(stage_);
		obs_leave_graphics();
	}
	texrender_ = nullptr;
	stage_ = nullptr;
	stagedMs_ = -1;
}

void StingerAnalyzer::audio(void *data, obs_source_t *, const struct audio_data *audio, bool)
{
	auto *self = static_cast<StingerAnalyzer *>(data);
	const auto *samples = reinterpret_cast<const float *>(audio->data[0]);
	for (uint32_t i = 0; samples && i < audio->frames && !self->audio_; ++i)
		if (std::fabs(samples[i]) > 1e-4f)
			self->audio_ = true;
}

void StingerAnalyzer::render(void *data, uint32_t, uint32_t)
{
	static_cast<StingerAnalyzer *>(data)->sample();
}

// Graphics thread. Reads back the frame staged on the previous call, then
// stages the current one, so the GPU never stalls waiting for it.
void StingerAnalyzer::sample()
{
	uint8_t *px = nullptr;
	uint32_t linesize = 0;
	if (stagedMs_ >= 0 && gs_stagesurface_map(stage_, &px, &linesize)) {
		const uint32_t x0 = mode_ == Mode::MatteRight ? kW / 2 : 0;
		const uint32_t y0 = mode_ == Mode::MatteBottom ? kH / 2 : 0;
		double sum = 0.0;
		for (uint32_t y = y0; y < kH; ++y) {
			for (uint32_t x = x0; x < kW; ++x) {
				const uint8_t *p = px + y * linesize + x * 4;
				sum += mode_ == Mode::Alpha ? p[3] : 0.2126 * p[0] + 0.7152 * p[1] + 0.0722 * p[2];
			}
		}
		float coverage = (float)(sum / (255.0 * (kW - x0) * (kH - y0)));
		if (invert_)
			coverage = 1.0f - coverage;
		gs_stagesurface_unmap(stage_);
		std::lock_guard<std::mutex> guard(lock_);
		samples_.emplace_back(stagedMs_, coverage);
	}
	stagedMs_ = -1;

	const uint32_t cx = obs_source_get_width(media_), cy = obs_source_get_height(media_);
	if (!cx || !cy || obs_source_media_get_state(media_) != OBS_MEDIA_STATE_PLAYING)
		return;
	if (!texrender_) {
		texrender_ = gs_texrender_create(GS_RGBA, GS_ZS_NONE);
		stage_ = gs_stagesurface_create(kW, kH, GS_RGBA);
	}
	gs_texrender_reset(texrender_);
	if (!gs_texrender_begin(texrender_, kW, kH))
		return;
	vec4 clear;
	vec4_zero(&clear);
	gs_clear(GS_CLEAR_COLOR, &clear, 0.0f, 0);
	gs_ortho(0.0f, (float)cx, 0.0f, (float)cy, -100.0f, 100.0f);
	gs_blend_state_push();
	gs_blend_function(GS_BLEND_ONE, GS_BLEND_ZERO); // keep alpha as is
	obs_source_video_render(media_);
	gs_blend_state_pop();
	gs_texrender_end(texrender_);
	gs_stage_texture(stage_, gs_texrender_get_texture(texrender_));
	stagedMs_ = obs_source_media_get_time(media_);
}

void StingerAnalyzer::poll()
{
	const auto state = obs_source_media_get_state(media_);
	const bool neverStarted = ++ticks_ > kStartTicks && state != OBS_MEDIA_STATE_PLAYING;
	if (state == OBS_MEDIA_STATE_ENDED || state == OBS_MEDIA_STATE_ERROR || neverStarted || ticks_ > kMaxTicks)
		finish();
}

void StingerAnalyzer::finish()
{
	proc_handler_t *ph = obs_source_get_proc_handler(media_);
	calldata_t cd = {};
	proc_handler_call(ph, "get_duration", &cd);
	const qint64 durationMs = calldata_int(&cd, "duration") / 1000000;
	proc_handler_call(ph, "get_nb_frames", &cd);
	const qint64 frames = calldata_int(&cd, "num_frames");
	calldata_free(&cd);
	cancel();

	Result r;
	r.hasAudio = audio_;
	r.audioChecked = !separateMatte_;
	std::vector<std::pair<qint64, float>> samples;
	{
		std::lock_guard<std::mutex> guard(lock_);
		samples.swap(samples_);
	}
	if (samples.empty()) {
		r.error = tr("The video couldn't be played.");
		emit finished(r);
		return;
	}

	auto byCoverage = [](const auto &a, const auto &b) {
		return a.second < b.second;
	};
	r.peak = std::max_element(samples.begin(), samples.end(), byCoverage)->second;
	const float low = std::min_element(samples.begin(), samples.end(), byCoverage)->second;
	if (r.peak - low < 0.05f) {
		r.error = mode_ == Mode::Alpha ? tr("The video has no transparency, so there's nothing to measure. "
						    "Use a track matte, or set the point by eye.")
					       : tr("The matte doesn't change, so there's nothing to measure.");
		emit finished(r);
		return;
	}

	// The middle of the first stretch at (nearly) full coverage.
	const float threshold = (float)r.peak * 0.98f;
	auto first = std::find_if(samples.begin(), samples.end(),
				  [threshold](const auto &s) { return s.second >= threshold; });
	auto last = std::find_if(first, samples.end(), [threshold](const auto &s) { return s.second < threshold; });
	r.pointMs = (first->first + std::prev(last)->first) / 2;
	if (durationMs > 0 && frames > 0)
		r.pointFrame = std::llround((double)r.pointMs * frames / durationMs);
	if (r.peak < 0.9)
		r.note = tr("It never covers the whole screen (at most %1%), so the cut may show.")
				 .arg(std::lround(r.peak * 100));
	emit finished(r);
}
