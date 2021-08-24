#include <QPainter>
#include <QCache>
#include "common/programpaths.h"
#include "map/mapsforgemap.h"
#include "map/textpathitem.h"
#include "map/textpointitem.h"
#include "mosaicotrama.h"

static const Estilo& style(qreal ratio)
{
	static Estilo s(ProgramPaths::renderthemeFile(), ratio);
	return s;
}

static qreal area(const QPainterPath &polygon)
{
	qreal area = 0;

	for (int i = 0; i < polygon.elementCount(); i++) {
		int j = (i + 1) % polygon.elementCount();
		area += polygon.elementAt(i).x * polygon.elementAt(j).y;
		area -= polygon.elementAt(i).y * polygon.elementAt(j).x;
	}
	area /= 2.0;

	return area;
}

static QPointF centroid(const QPainterPath &polygon)
{
	qreal cx = 0, cy = 0;
	qreal factor = 1.0 / (6.0 * area(polygon));

	for (int i = 0; i < polygon.elementCount(); i++) {
		int j = (i + 1) % polygon.elementCount();
		qreal f = (polygon.elementAt(i).x * polygon.elementAt(j).y
		  - polygon.elementAt(j).x * polygon.elementAt(i).y);
		cx += (polygon.elementAt(i).x + polygon.elementAt(j).x) * f;
		cy += (polygon.elementAt(i).y + polygon.elementAt(j).y) * f;
	}

	return QPointF(cx * factor, cy * factor);
}

static QString *pointLabel(const Estilo::TextRender *ri, DatoMapa::Point &point)
{
	for (int i = 0; i < point.tags.size(); i++) {
		if (point.tags.at(i).key == ri->key()) {
			if (point.tags.at(i).value.isEmpty())
				return 0;
			else {
				point.label = point.tags.at(i).value;
				return &point.label;
			}
		}
	}

	return 0;
}

static QString *pathLabel(const Estilo::TextRender *ri, DatoMapa::Path &path,
  bool *limit = 0)
{
	for (int i = 0; i < path.tags.size(); i++) {
		if (path.tags.at(i).key == ri->key()) {
			if (path.tags.at(i).value.isEmpty())
				return 0;
			else {
				path.label = path.tags.at(i).value;
				if (limit)
					*limit = (path.tags.at(i).key == "ref");
				return &path.label;
			}
		}
	}

	return 0;
}

static const QColor *haloColor(const Estilo::TextRender *ti)
{
	return (ti->strokeColor() != ti->fillColor() && ti->strokeWidth() > 0)
	  ? &ti->strokeColor() : 0;
}


void MosaicoTrama::processPointLabels(QList<TextItem*> &textItems)
{
	const Estilo &s = style(_ratio);
	QList<const Estilo::TextRender*> labels(s.pointLabels(_zoom));
	QList<const Estilo::Symbol*> symbols(s.pointSymbols(_zoom));

	for (int i = 0; i < _points.size(); i++) {
		DatoMapa::Point &point = _points[i];
		QString *label = 0;
		const Estilo::TextRender *ti = 0;
		const Estilo::Symbol *si = 0;

		for (int j = 0; j < labels.size(); j++) {
			const Estilo::TextRender *ri = labels.at(j);
			if (ri->rule().match(point.tags)) {
				if ((label = pointLabel(ri, point))) {
					ti = ri;
					break;
				}
			}
		}

		for (int j = 0; j < symbols.size(); j++) {
			const Estilo::Symbol *ri = symbols.at(j);
			if (ri->rule().match(point.tags)) {
				si = ri;
				break;
			}
		}

		if (!ti && !si)
			continue;

		const QImage *img = si ? &si->img() : 0;
		const QFont *font = ti ? &ti->font() : 0;
		const QColor *color = ti ? &ti->fillColor() : 0;
		const QColor *hColor = ti ? haloColor(ti) : 0;

		TextPointItem *item = new TextPointItem(
		  ll2xy(point.coordinates).toPoint(), label, font, img, color,
		    hColor, 0, false);
		if (item->isValid() && !item->collides(textItems))
			textItems.append(item);
		else
			delete item;
	}
}

void MosaicoTrama::processAreaLabels(QList<TextItem*> &textItems)
{
	const Estilo &s = style(_ratio);
	QList<const Estilo::TextRender*> labels(s.areaLabels(_zoom));
	QList<const Estilo::Symbol*> symbols(s.areaSymbols(_zoom));

	for (int i = 0; i < _paths.size(); i++) {
		DatoMapa::Path &path = _paths[i];
		QString *label = 0;
		const Estilo::TextRender *ti = 0;
		const Estilo::Symbol *si = 0;

		if (!path.closed)
			continue;

		for (int j = 0; j < labels.size(); j++) {
			const Estilo::TextRender *ri = labels.at(j);
			if (ri->rule().match(path.closed, path.tags)) {
				if ((label = pathLabel(ri, path))) {
					ti = ri;
					break;
				}
			}
		}

		for (int j = 0; j < symbols.size(); j++) {
			const Estilo::Symbol *ri = symbols.at(j);
			if (ri->rule().match(path.tags)) {
				si = ri;
				break;
			}
		}

		if (!ti && !si)
			continue;

		if (!path.path.elementCount())
			path.path = painterPath(path.poly);

		const QImage *img = si ? &si->img() : 0;
		const QFont *font = ti ? &ti->font() : 0;
		const QColor *color = ti ? &ti->fillColor() : 0;
		const QColor *hColor = ti ? haloColor(ti) : 0;
		QPointF pos = path.labelPos.isNull()
		  ? centroid(path.path) : ll2xy(path.labelPos);

		TextPointItem *item = new TextPointItem(pos.toPoint(), label, font, img,
		  color, hColor, 0, false);
		if (item->isValid() && _rect.contains(item->boundingRect().toRect())
		  && !item->collides(textItems))
			textItems.append(item);
		else
			delete item;
	}
}

void MosaicoTrama::processLineLabels(QList<TextItem*> &textItems)
{
	const Estilo &s = style(_ratio);
	QList<const Estilo::TextRender*> instructions(s.pathLabels(_zoom));
	QSet<QString> set;

	for (int i = 0; i < instructions.size(); i++) {
		const Estilo::TextRender *ri = instructions.at(i);

		for (int j = 0; j < _paths.size(); j++) {
			DatoMapa::Path &path = _paths[j];
			QString *label = 0;
			bool limit = false;

			if (!path.path.elementCount())
				continue;
			if (!ri->rule().match(path.closed, path.tags))
				continue;
			if (!(label = pathLabel(ri, path, &limit)))
				continue;
			if (limit && set.contains(path.label))
				continue;

			TextPathItem *item = new TextPathItem(path.path, label, _rect,
			  &ri->font(), &ri->fillColor(), haloColor(ri));
			if (item->isValid() && !item->collides(textItems)) {
				textItems.append(item);
				if (limit)
					set.insert(path.label);
			} else
				delete item;
		    }
	}
}

void MosaicoTrama::drawTextItems(QPainter *painter,
  const QList<TextItem*> &textItems)
{
	for (int i = 0; i < textItems.size(); i++)
		textItems.at(i)->paint(painter);
}

QPainterPath MosaicoTrama::painterPath(const Polygon &polygon) const
{
	QPainterPath path;

	for (int i = 0; i < polygon.size(); i++) {
		const QVector<Coordinates> &subpath = polygon.at(i);

		path.moveTo(ll2xy(subpath.first()));
		for (int j = 1; j < subpath.size(); j++)
			path.lineTo(ll2xy(subpath.at(j)));
	}

	return path;
}

QVector<MosaicoTrama::PathInstruction> MosaicoTrama::pathInstructions()
{
	QCache<Key, QVector<const Estilo::PathRender *> > cache(1024);
	QVector<PathInstruction> instructions;
	const Estilo &s = style(_ratio);
	QVector<const Estilo::PathRender*> *ri;

	for (int i = 0 ; i < _paths.size(); i++) {
		DatoMapa::Path &path = _paths[i];

		Key key(_zoom, path.closed, path.tags);
		QVector<const Estilo::PathRender*> *cached = cache.object(key);
		if (!cached) {
			ri = new QVector<const Estilo::PathRender*>(s.paths(_zoom,
			  path.closed, path.tags));
			for (int j = 0; j < ri->size(); j++)
				instructions.append(PathInstruction(ri->at(j), &path));
			cache.insert(key, ri);
		} else {
			for (int j = 0; j < cached->size(); j++)
				instructions.append(PathInstruction(cached->at(j), &path));
		}
	}

	std::sort(instructions.begin(), instructions.end());

	return instructions;
}

void MosaicoTrama::drawPaths(QPainter *painter)
{
	QVector<PathInstruction> instructions(pathInstructions());
	const Estilo::PathRender *lri = 0;

	QPixmap layer(_pixmap.size());
	layer.setDevicePixelRatio(_ratio);
	layer.fill(Qt::transparent);

	QPainter lp(&layer);
	lp.setRenderHint(QPainter::Antialiasing);
	lp.translate(-_rect.x(), -_rect.y());
	lp.setCompositionMode(QPainter::CompositionMode_Source);

	for (int i = 0; i < instructions.size(); i++) {
		PathInstruction &is = instructions[i];
		const Estilo::PathRender *ri = is.render();

		if (lri && lri != ri) {
			painter->drawPixmap(_rect.topLeft(), layer);
			lp.fillRect(QRect(_rect.topLeft(), _pixmap.size()), Qt::transparent);
		}

		if (!is.path()->path.elementCount())
			is.path()->path = painterPath(is.path()->poly);

		if (ri->area()) {
			lp.setPen(ri->pen(_zoom));
			lp.setBrush(ri->brush());
			lp.drawPath(is.path()->path);
			lri = ri;
		} else {
			painter->setPen(ri->pen(_zoom));
			painter->setBrush(ri->brush());
			painter->drawPath(is.path()->path);
			lri = 0;
		}
	}

	if (lri)
		painter->drawPixmap(_rect.topLeft(), layer);
}

void MosaicoTrama::render()
{
	std::sort(_points.begin(), _points.end());

	QList<TextItem*> textItems;

	_pixmap.setDevicePixelRatio(_ratio);
	_pixmap.fill(Qt::transparent);

	QPainter painter(&_pixmap);
	painter.setRenderHint(QPainter::Antialiasing);
	painter.translate(-_rect.x(), -_rect.y());

	drawPaths(&painter);

	processPointLabels(textItems);
	processAreaLabels(textItems);
	processLineLabels(textItems);
	drawTextItems(&painter, textItems);

	//painter.setPen(Qt::red);
	//painter.setBrush(Qt::NoBrush);
	//painter.drawRect(QRect(_rect.topLeft(), _pixmap.size()));

	qDeleteAll(textItems);
}
