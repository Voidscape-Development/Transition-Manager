#include "transition-preview.hpp"

#include <plugin-support.h>

#include <obs-frontend-api.h>

#include <QEvent>
#include <QHideEvent>
#include <QResizeEvent>
#include <QShowEvent>
#include <QWindow>
#include <algorithm>

#if !defined(_WIN32) && !defined(__APPLE__)
#include <obs-nix-platform.h>
#endif

// OBS colors are 0xAABBGGRR.
static constexpr uint32_t kColorA = 0xFF5A3A1E; // dark blue
static constexpr uint32_t kColorB = 0xFF2A8CE6; // orange
static constexpr uint32_t kBackground = 0x1E1E1E;

TransitionPreview::TransitionPreview(QWidget *parent) : QWidget(parent)
{
	setAttribute(Qt::WA_PaintOnScreen);
	setAttribute(Qt::WA_StaticContents);
	setAttribute(Qt::WA_NoSystemBackground);
	setAttribute(Qt::WA_OpaquePaintEvent);
	setAttribute(Qt::WA_DontCreateNativeAncestors);
	setAttribute(Qt::WA_NativeWindow);
	setMinimumHeight(140);
	QSizePolicy policy(QSizePolicy::Expanding, QSizePolicy::Preferred);
	policy.setHeightForWidth(true);
	setSizePolicy(policy);

	createTestSources();
	updateEndpoints();
}

TransitionPreview::~TransitionPreview()
{
	destroyDisplay();
	setTransition(nullptr);
}

void TransitionPreview::createTestSources()
{
	obs_video_info ovi = {};
	obs_get_video_info(&ovi);

	auto makeColor = [&](uint32_t color, const char *name) {
		OBSDataAutoRelease s = obs_data_create();
		obs_data_set_int(s, "color", color);
		obs_data_set_int(s, "width", ovi.base_width ? ovi.base_width : 1920);
		obs_data_set_int(s, "height", ovi.base_height ? ovi.base_height : 1080);
		return obs_source_create_private("color_source_v3", name, s);
	};

	colorA_ = makeColor(kColorA, "tm-preview-a");
	colorB_ = makeColor(kColorB, "tm-preview-b");
}

// Picks the A/B endpoints for the current mode. Falls back to the test colors
// for any endpoint that can't be resolved (no scenes, single scene, ...).
void TransitionPreview::updateEndpoints()
{
	OBSSource a(colorA_.Get());
	OBSSource b(colorB_.Get());

	if (mode_ == SourceMode::Scenes) {
		OBSSourceAutoRelease program = obs_frontend_get_current_scene();
		OBSSource next;

		if (obs_frontend_preview_program_mode_active()) {
			OBSSourceAutoRelease preview = obs_frontend_get_current_preview_scene();
			next = preview.Get();
		}

		// Outside studio mode (or if preview == program), use the scene after
		// the program scene in the scene list.
		if (program && (!next || next.Get() == program.Get())) {
			next = nullptr;
			struct obs_frontend_source_list scenes = {};
			obs_frontend_get_scenes(&scenes);
			for (size_t i = 0; i < scenes.sources.num; ++i) {
				if (scenes.sources.array[i] == program.Get() && scenes.sources.num > 1) {
					next = scenes.sources.array[(i + 1) % scenes.sources.num];
					break;
				}
			}
			obs_frontend_source_list_free(&scenes);
		}

		if (program)
			a = program.Get();
		if (next && next.Get() != a.Get())
			b = next;
	}

	sourceA_ = a;
	sourceB_ = b;
}

static QString endpointName(obs_source_t *source, obs_source_t *colorA, obs_source_t *colorB)
{
	if (!source || source == colorA || source == colorB)
		return QObject::tr("Test color");
	return QString::fromUtf8(obs_source_get_name(source));
}

QString TransitionPreview::fromName() const
{
	return endpointName(sourceA_, colorA_, colorB_);
}

QString TransitionPreview::toName() const
{
	return endpointName(sourceB_, colorA_, colorB_);
}

void TransitionPreview::setSourceMode(SourceMode mode)
{
	if (mode_ == mode)
		return;
	mode_ = mode;
	refreshSources();
}

void TransitionPreview::refreshSources()
{
	updateEndpoints();
	reset();
}

void TransitionPreview::releaseScenes()
{
	if (copy_) {
		obs_transition_force_stop(copy_);
		obs_transition_clear(copy_);
	}
	sourceA_ = colorA_.Get();
	sourceB_ = colorB_.Get();
	if (copy_)
		obs_transition_set(copy_, sourceA_);
}

void TransitionPreview::setTransition(obs_source_t *source)
{
	OBSSource next;

	if (source) {
		OBSDataAutoRelease settings = obs_source_get_settings(source);
		OBSDataAutoRelease copySettings = obs_data_create_from_json(obs_data_get_json(settings));
		OBSSourceAutoRelease copy =
			obs_source_create_private(obs_source_get_id(source), "tm-preview", copySettings);
		if (copy) {
			// Active (not just showing) so media-driven transitions such as
			// Stinger actually play, matching OBS's own properties preview.
			obs_source_inc_active(copy);
			updateEndpoints();
			obs_transition_set(copy, sourceA_);
			next = copy.Get();
		}
	}

	OBSSource previous;
	{
		std::lock_guard<std::mutex> lock(mutex_);
		previous = copy_;
		copy_ = next;
	}

	// Released on the UI thread, outside the lock.
	if (previous) {
		obs_transition_force_stop(previous);
		obs_transition_clear(previous);
		obs_source_dec_active(previous);
	}
}

void TransitionPreview::syncSettings(obs_source_t *source)
{
	if (!copy_ || !source)
		return;
	OBSDataAutoRelease settings = obs_source_get_settings(source);
	obs_source_update(copy_, settings);
}

void TransitionPreview::play(int durationMs)
{
	if (!copy_)
		return;
	updateEndpoints();
	obs_transition_force_stop(copy_);
	obs_transition_set(copy_, sourceA_);
	obs_transition_start(copy_, OBS_TRANSITION_MODE_AUTO, (uint32_t)durationMs, sourceB_);
}

void TransitionPreview::beginScrub(int durationMs)
{
	if (!copy_)
		return;
	updateEndpoints();
	obs_transition_force_stop(copy_);
	obs_transition_set(copy_, sourceA_);
	obs_transition_start(copy_, OBS_TRANSITION_MODE_MANUAL, (uint32_t)durationMs, sourceB_);
	obs_transition_set_manual_time(copy_, 0.0f);
}

void TransitionPreview::scrub(float t)
{
	if (!copy_)
		return;
	// Reaching 1.0 in manual mode completes the transition; stay just short
	// so the slider can keep moving back and forth.
	obs_transition_set_manual_time(copy_, std::clamp(t, 0.0f, 0.999f));
}

void TransitionPreview::reset()
{
	if (!copy_)
		return;
	obs_transition_force_stop(copy_);
	obs_transition_set(copy_, sourceA_);
}

void TransitionPreview::createDisplay()
{
	if (display_ || !windowHandle() || !isVisible())
		return;

	const QSize size = this->size() * devicePixelRatioF();

	gs_init_data info = {};
	info.cx = (uint32_t)size.width();
	info.cy = (uint32_t)size.height();
	info.format = GS_BGRA;
	info.zsformat = GS_ZS_NONE;

#if defined(_WIN32)
	info.window.hwnd = reinterpret_cast<void *>(winId());
#elif defined(__APPLE__)
	info.window.view = reinterpret_cast<id>(winId());
#else
	if (obs_get_nix_platform() != OBS_NIX_PLATFORM_X11_EGL) {
		// TODO: Wayland needs the wl_surface from Qt's native interface.
		static bool warned = false;
		if (!warned)
			obs_log(LOG_WARNING, "preview currently supports X11 only on Linux");
		warned = true;
		return;
	}
	info.window.id = (uint32_t)winId();
	info.window.display = obs_get_nix_platform_display();
#endif

	display_ = obs_display_create(&info, kBackground);
	if (display_)
		obs_display_add_draw_callback(display_, draw, this);
}

void TransitionPreview::destroyDisplay()
{
	if (!display_)
		return;
	obs_display_remove_draw_callback(display_, draw, this);
	obs_display_destroy(display_);
	display_ = nullptr;
}

void TransitionPreview::showEvent(QShowEvent *event)
{
	QWidget::showEvent(event);
	winId(); // force native window creation
	createDisplay();
}

void TransitionPreview::hideEvent(QHideEvent *event)
{
	QWidget::hideEvent(event);
	destroyDisplay(); // no point rendering into a hidden surface
}

void TransitionPreview::resizeEvent(QResizeEvent *event)
{
	QWidget::resizeEvent(event);
	if (!display_) {
		createDisplay();
		return;
	}
	const QSize size = event->size() * devicePixelRatioF();
	obs_display_resize(display_, (uint32_t)size.width(), (uint32_t)size.height());
}

bool TransitionPreview::event(QEvent *event)
{
	// Docking/undocking recreates the native window; rebuild the display.
	if (event->type() == QEvent::WinIdChange) {
		destroyDisplay();
		createDisplay();
	}
	return QWidget::event(event);
}

void TransitionPreview::draw(void *data, uint32_t cx, uint32_t cy)
{
	auto *self = static_cast<TransitionPreview *>(data);

	obs_video_info ovi = {};
	if (!obs_get_video_info(&ovi) || !ovi.base_width || !ovi.base_height)
		return;

	// Letterbox the canvas into the widget.
	const float scale = std::min((float)cx / ovi.base_width, (float)cy / ovi.base_height);
	const int w = (int)(ovi.base_width * scale);
	const int h = (int)(ovi.base_height * scale);
	const int x = ((int)cx - w) / 2;
	const int y = ((int)cy - h) / 2;

	std::lock_guard<std::mutex> lock(self->mutex_);
	if (!self->copy_)
		return;

	gs_viewport_push();
	gs_projection_push();
	gs_ortho(0.0f, (float)ovi.base_width, 0.0f, (float)ovi.base_height, -100.0f, 100.0f);
	gs_set_viewport(x, y, w, h);

	obs_source_video_render(self->copy_);

	gs_projection_pop();
	gs_viewport_pop();
}
