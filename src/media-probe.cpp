#include "media-probe.hpp"

#include <QFileInfo>

static constexpr int kPollMs = 100;
static constexpr int kMaxAttempts = 50; // 5 s

MediaProbe::MediaProbe(QObject *parent) : QObject(parent)
{
	timer_.setInterval(kPollMs);
	connect(&timer_, &QTimer::timeout, this, &MediaProbe::poll);
}

MediaProbe::~MediaProbe()
{
	cancel();
}

void MediaProbe::cancel()
{
	timer_.stop();
	source_ = nullptr;
	path_.clear();
}

void MediaProbe::probe(const QString &path)
{
	if (path == path_ && timer_.isActive())
		return; // already working on it
	cancel();

	if (path.isEmpty() || !QFileInfo::exists(path)) {
		emit failed(path);
		return;
	}

	auto cached = cache_.constFind(path);
	if (cached != cache_.constEnd()) {
		emit finished(path, cached->durationMs, cached->frames);
		return;
	}

	// Same settings the Stinger uses for its own media source.
	OBSDataAutoRelease settings = obs_data_create();
	obs_data_set_string(settings, "local_file", path.toUtf8().constData());
	obs_data_set_bool(settings, "looping", false);
	obs_data_set_bool(settings, "is_stinger", true);
	source_ = obs_source_create_private("ffmpeg_source", "tm-stinger-probe", settings);
	if (!source_) {
		emit failed(path);
		return;
	}

	path_ = path;
	attempts_ = 0;
	timer_.start();
}

void MediaProbe::poll()
{
	if (!source_) {
		timer_.stop();
		return;
	}

	proc_handler_t *ph = obs_source_get_proc_handler(source_);
	calldata_t cd = {};
	proc_handler_call(ph, "get_duration", &cd);
	const qint64 durationNs = calldata_int(&cd, "duration");

	qint64 frames = 0;
	if (durationNs > 0) {
		// Only ask for frames once the media is open; before that the
		// handler has nothing to read from.
		proc_handler_call(ph, "get_nb_frames", &cd);
		frames = calldata_int(&cd, "num_frames");
	}
	calldata_free(&cd);

	if (durationNs > 0) {
		const QString path = path_;
		const Info info{durationNs / 1000000, frames};
		cache_.insert(path, info);
		cancel();
		emit finished(path, info.durationMs, info.frames);
		return;
	}

	if (++attempts_ >= kMaxAttempts) {
		const QString path = path_;
		cancel();
		emit failed(path);
	}
}
