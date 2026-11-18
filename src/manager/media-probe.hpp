#pragma once

#include <QHash>
#include <QObject>
#include <QString>
#include <QTimer>

#include <obs.hpp>

// Reads a video file's length and frame count the same way the Stinger
// transition does: through a private media source's "get_duration" and
// "get_nb_frames" procs. The file opens asynchronously, so this polls
// briefly and reports back. Results are cached per path.
class MediaProbe : public QObject {
	Q_OBJECT

public:
	explicit MediaProbe(QObject *parent = nullptr);
	~MediaProbe() override;

	void probe(const QString &path);
	void cancel();

signals:
	void finished(const QString &path, qint64 durationMs, qint64 frames);
	void failed(const QString &path);

private:
	void poll();

	struct Info {
		qint64 durationMs;
		qint64 frames;
	};

	OBSSourceAutoRelease source_;
	QString path_;
	QTimer timer_;
	int attempts_ = 0;
	QHash<QString, Info> cache_;
};
