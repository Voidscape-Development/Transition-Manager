#pragma once

#include <QObject>
#include <QString>
#include <QTimer>

#include <obs.hpp>

#include <atomic>
#include <mutex>
#include <utility>
#include <vector>

struct gs_texture_render;
struct gs_stage_surface;

// Plays a Stinger's video privately and measures how much of the screen
// each frame covers: its alpha, or with a track matte, the matte's
// brightness. Suggests the transition point (the middle of the stretch
// where it covers the most) and reports whether the video has sound.
class StingerAnalyzer : public QObject {
	Q_OBJECT

public:
	struct Result {
		QString error; // set when nothing could be measured
		QString note;  // a caveat about the suggestion
		qint64 pointMs = -1;
		qint64 pointFrame = -1;
		double peak = 0.0; // highest coverage, 0..1
		bool hasAudio = false;
		bool audioChecked = true; // false when a separate matte file was played
	};

	explicit StingerAnalyzer(QObject *parent = nullptr);
	~StingerAnalyzer() override;

	void analyze(obs_source_t *stinger);
	void cancel();
	bool running() const { return timer_.isActive(); }

signals:
	void finished(const StingerAnalyzer::Result &result);

private:
	enum class Mode { Alpha, MatteRight, MatteBottom, MatteFull };

	static void render(void *data, uint32_t cx, uint32_t cy);
	static void audio(void *data, obs_source_t *, const struct audio_data *audio, bool muted);
	void sample();
	void poll();
	void finish();

	OBSSource media_;
	Mode mode_ = Mode::Alpha;
	bool invert_ = false;
	bool separateMatte_ = false;
	int ticks_ = 0;
	QTimer timer_;

	std::mutex lock_;                               // guards samples_
	std::vector<std::pair<qint64, float>> samples_; // ms, coverage
	std::atomic<bool> audio_{false};
	gs_texture_render *texrender_ = nullptr; // graphics thread only
	gs_stage_surface *stage_ = nullptr;
	qint64 stagedMs_ = -1;
};
