#pragma once

#include <QScrollArea>
#include <memory>

#include <obs.hpp>

class QFormLayout;
class QListWidget;

// Builds a custom Qt form from a source's obs_properties_t. OBS's own
// properties view isn't exported to plugins, so each property type maps to
// a widget here. Extend addProperty() to restyle or add types.
class PropertiesPanel : public QScrollArea {
	Q_OBJECT

public:
	explicit PropertiesPanel(QWidget *parent = nullptr);
	~PropertiesPanel() override;

	void setSource(obs_source_t *source);
	obs_source_t *source() const { return source_; }

	// Rebuild from the source's current settings (e.g. after a rename or an
	// edit made elsewhere).
	void refresh();

	// Clears the source's settings back to its defaults.
	void resetToDefaults();

signals:
	void settingsChanged();

private:
	void rebuild();
	void scheduleRebuild();
	void clearWidgets();
	void addProperty(obs_property_t *prop, QFormLayout *layout);
	void commit(obs_property_t *prop);
	static QFormLayout *makeForm();

	QWidget *makeFontField(obs_property_t *prop, const QByteArray &key, const QString &label);
	QWidget *makeEditableListField(obs_property_t *prop, const QByteArray &key, const QString &label);
	QWidget *makeFrameRateField(obs_property_t *prop, const QByteArray &key);
	QWidget *makeLumaPicker(obs_property_t *prop, const QByteArray &key);
	void commitEditableList(obs_property_t *prop, const QByteArray &key, QListWidget *list);

	using PropertiesPtr = std::unique_ptr<obs_properties_t, decltype(&obs_properties_destroy)>;

	OBSSource source_;
	OBSDataAutoRelease settings_;
	PropertiesPtr props_{nullptr, obs_properties_destroy};
	bool rebuildPending_ = false;
};
