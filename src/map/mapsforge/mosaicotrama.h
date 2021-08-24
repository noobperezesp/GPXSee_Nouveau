#ifndef MAPSFORGE_MOSAICOTRAMA_H
#define MAPSFORGE_MOSAICOTRAMA_H

#include <QPixmap>
#include "map/projection.h"
#include "map/transform.h"
#include "estilo.h"
#include "datomapa.h"

class MapsforgeMap;
class TextItem;
class MosaicoTrama
{
public:
	MosaicoTrama(const Projection &proj, const Transform &transform, int zoom,
	  const QRect &rect, qreal ratio, const QString &key,
	  const QList<DatoMapa::Path> &paths, const QList<DatoMapa::Point> &points)
	  : _proj(proj), _transform(transform), _zoom(zoom), _rect(rect),
	  _ratio(ratio), _key(key), _pixmap(rect.width() * ratio,
	  rect.height() * ratio), _paths(paths), _points(points) {}

	const QString &key() const {return _key;}
	QPoint xy() const {return _rect.topLeft();}
	const QPixmap &pixmap() const {return _pixmap;}

	void render();

private:
	class PathInstruction
	{
	public:
		PathInstruction() : _render(0), _path(0) {}
		PathInstruction(const Estilo::PathRender *render, DatoMapa::Path *path)
		  : _render(render), _path(path) {}

		bool operator<(const PathInstruction &other) const
		{
			if (_path->layer == other._path->layer)
				return _render->zOrder() < other._render->zOrder();
			else
				return (_path->layer < other._path->layer);
		}

		const Estilo::PathRender *render() const {return _render;}
		DatoMapa::Path *path() {return _path;}

	private:
		const Estilo::PathRender *_render;
		DatoMapa::Path *_path;
	};

	struct Key {
		Key(int zoom, bool closed, const QVector<DatoMapa::Tag> &tags)
		  : zoom(zoom), closed(closed), tags(tags) {}
		bool operator==(const Key &other) const
		{
			return zoom == other.zoom && closed == other.closed
			  && tags == other.tags;
		}

		int zoom;
		bool closed;
		const QVector<DatoMapa::Tag> &tags;
	};

	friend HASH_T qHash(const MosaicoTrama::Key &key);
	friend HASH_T qHash(const MosaicoTrama::PathInstruction &pi);

	QVector<PathInstruction> pathInstructions();
	QPointF ll2xy(const Coordinates &c) const
	  {return _transform.proj2img(_proj.ll2xy(c));}
	void processPointLabels(QList<TextItem*> &textItems);
	void processAreaLabels(QList<TextItem*> &textItems);
	void processLineLabels(QList<TextItem*> &textItems);
	QPainterPath painterPath(const Polygon &polygon) const;
	void drawTextItems(QPainter *painter, const QList<TextItem*> &textItems);
	void drawPaths(QPainter *painter);

	Projection _proj;
	Transform _transform;
	int _zoom;
	QRect _rect;
	qreal _ratio;
	QString _key;
	QPixmap _pixmap;
	QList<DatoMapa::Path> _paths;
	QList<DatoMapa::Point> _points;
};

inline HASH_T qHash(const MosaicoTrama::Key &key)
{
	return ::qHash(key.zoom) ^ ::qHash(key.tags);
}

#endif // MAPSFORGE_MOSAICOTRAMA_H
