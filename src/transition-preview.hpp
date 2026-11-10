#pragma once

#include <QWidget>
#include <mutex>

#include <obs.hpp>

// Native-surface widget that renders a PRIVATE COPY of the selected
// transition between two sources (A -> B). Playing or scrubbing the copy
// never touches the transition that's driving program output.
class TransitionPreview : public QWidget {
	Q_OBJECT

public:
	enum class SourceMode {
		Colors, // two solid test colors
		Scenes, // program scene -> preview scene (or the next scene outside studio mode)
	};

	explicit TransitionPreview(QWidget *parent = nullptr);
	~TransitionPreview() override;

	void setTransition(obs_source_t *source); // nullptr clears the preview
	void syncSettings(obs_source_t *source);  // push edited settings into the copy

	void setSourceMode(SourceMode mode);
	SourceMode sourceMode() const { return mode_; }
	void refreshSources(); // re-read program/preview scenes in Scenes mode
	void releaseScenes();  // drop scene references (scene collection unloading)

	void play(int durationMs);
	void beginScrub(int durationMs);
	void scrub(float t); // 0.0 - 1.0
	void reset();

	QPaintEngine *paintEngine() const override { return nullptr; }
	QSize sizeHint() const override { return QSize(320, 180); }

protected:
	void showEvent(QShowEvent *event) override;
	void hideEvent(QHideEvent *event) override;
	void resizeEvent(QResizeEvent *event) override;
	void paintEvent(QPaintEvent *) override {}
	bool event(QEvent *event) override;

private:
	void createDisplay();
	void destroyDisplay();
	void createTestSources();
	void updateEndpoints();
	static void draw(void *data, uint32_t cx, uint32_t cy);

	obs_display_t *display_ = nullptr;
	OBSSourceAutoRelease colorA_;
	OBSSourceAutoRelease colorB_;

	SourceMode mode_ = SourceMode::Colors;
	OBSSource sourceA_; // current "from" endpoint
	OBSSource sourceB_; // current "to" endpoint

	std::mutex mutex_; // guards copy_ against the graphics thread
	OBSSource copy_;
};
