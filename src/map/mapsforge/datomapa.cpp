#include <cstring>
#include <QtEndian>
#include <QFile>
#include <QDataStream>
#include <QColor>
#include "map/osm.h"
#include "subficher.h"
#include "datomapa.h"


#define MAGIC "mapsforge binary OSM"
#define MD(val) ((val) / 1e6)
#define OFFSET_MASK 0x7FFFFFFFFFL

static uint pointType(const QVector<DatoMapa::Tag> &tags)
{
	for (int i = 0; i < tags.size(); i++) {
		const DatoMapa::Tag &tag = tags.at(i);
		if (tag.key == "place") {
			if (tag.value == "country")
				return 4;
			else if (tag.value == "city")
				return 3;
			else if (tag.value == "town")
				return 2;
			else if (tag.value == "village")
				return 1;
			else
				return 0;
		}
	}

	return 0;
}

static void setPointId(DatoMapa::Point &p)
{
	uint hash = (uint)qHash(QPair<double,double>((uint)qHash(
	  QPair<double, double>(p.coordinates.lon(), p.coordinates.lat())),
	  (uint)qHash(p.label)));
	uint type = pointType(p.tags);

	p.id = ((quint64)type)<<32 | hash;
}

static void copyPaths(const RectC &rect, const QList<DatoMapa::Path> *src,
  QList<DatoMapa::Path> *dst)
{
	for (int i = 0; i < src->size(); i++)
		if (rect.intersects(src->at(i).poly.boundingRect()))
			dst->append(src->at(i));
}

static void copyPoints(const RectC &rect, const QList<DatoMapa::Point> *src,
  QList<DatoMapa::Point> *dst)
{
	for (int i = 0; i < src->size(); i++)
		if (rect.contains(src->at(i).coordinates))
			dst->append(src->at(i));
}

static double distance(const Coordinates &c1, const Coordinates &c2)
{
	return hypot(c1.lon() - c2.lon(), c1.lat() - c2.lat());
}

static bool isClosed(const Polygon &poly)
{
	if (poly.isEmpty() || poly.first().isEmpty())
		return false;
	return (distance(poly.first().first(), poly.first().last()) < 0.000000001);
}

static bool readTags(SubFicher &subfile, int count,
  const QVector<DatoMapa::Tag> &tags, QVector<DatoMapa::Tag> &list)
{
	QVector<quint32> ids(count);

	list.resize(count);

	for (int i = 0; i < count; i++) {
		if (!subfile.readVUInt32(ids[i]))
			return false;
		if (ids[i] >= (quint32)tags.size())
			return false;
	}

	for (int i = 0; i < count; i++) {
		const DatoMapa::Tag &tag = tags.at(ids.at(i));

		if (tag.value.length() == 2 && tag.value.at(0) == '%') {
			QByteArray value;

			if (tag.value.at(1) == 'b') {
				quint8 b;
				if (!subfile.readByte(b))
					return false;
				value.setNum(b);
			} else if (tag.value.at(1) == 'i') {
				qint32 u;
				if (!subfile.readInt32(u))
					return false;

				if (tag.key.contains(":colour"))
					value = QColor((quint32)u).name().toLatin1();
				else
					value.setNum(u);
			} else if (tag.value.at(1) == 'f') {
				quint32 u;
				if (!subfile.readUInt32(u))
					return false;
				float *f = (float *)&u;
				value.setNum(*f);
			} else if (tag.value.at(1) == 'h') {
				quint16 s;
				if (!subfile.readUInt16(s))
					return false;
				value.setNum(s);
			} else if (tag.value.at(1) == 's') {
				if (!subfile.readString(value))
					return false;
			} else
				value = tag.value;

			list[i] = DatoMapa::Tag(tag.key, value);
		} else
			list[i] = tag;
	}

	return true;
}

static bool readSingleDelta(SubFicher &subfile, const Coordinates &c,
  int count, QVector<Coordinates> &nodes)
{
	qint32 mdLat, mdLon;

	if (!(subfile.readVInt32(mdLat) && subfile.readVInt32(mdLon)))
		return false;

	double lat = c.lat() + MD(mdLat);
	double lon = c.lon() + MD(mdLon);

	nodes.reserve(count);
	nodes.append(Coordinates(lon, lat));

	for (int i = 1; i < count; i++) {
		if (!(subfile.readVInt32(mdLat) && subfile.readVInt32(mdLon)))
			return false;

		lat = lat + MD(mdLat);
		lon = lon + MD(mdLon);

		nodes.append(Coordinates(lon, lat));
	}

	return true;
}

static bool readDoubleDelta(SubFicher &subfile, const Coordinates &c,
  int count, QVector<Coordinates> &nodes)
{
	qint32 mdLat, mdLon;

	if (!(subfile.readVInt32(mdLat) && subfile.readVInt32(mdLon)))
		return false;

	double lat = c.lat() + MD(mdLat);
	double lon = c.lon() + MD(mdLon);
	double prevLat = 0;
	double prevLon = 0;

	nodes.reserve(count);
	nodes.append(Coordinates(lon, lat));

	for (int i = 1; i < count; i++) {
		if (!(subfile.readVInt32(mdLat) && subfile.readVInt32(mdLon)))
			return false;

		double singleLat = MD(mdLat) + prevLat;
		double singleLon = MD(mdLon) + prevLon;

		lat += singleLat;
		lon += singleLon;

		nodes.append(Coordinates(lon, lat));

		prevLat = singleLat;
		prevLon = singleLon;
	}

	return true;
}

static bool readPolygon(SubFicher &subfile, const Coordinates &c,
  bool doubleDelta, Polygon &polygon)
{
	quint32 blocks, nodes;

	if (!subfile.readVUInt32(blocks))
		return false;

	polygon.reserve(blocks);
	for (quint32 i = 0; i < blocks; i++) {
		if (!subfile.readVUInt32(nodes) || !nodes)
			return false;

		QVector<Coordinates> path;

		if (doubleDelta) {
			if (!readDoubleDelta(subfile, c, nodes, path))
				return false;
		} else {
			if (!readSingleDelta(subfile, c, nodes, path))
				return false;
		}

		polygon.append(path);
	}

	return true;
}

static bool readOffset(QDataStream &stream, quint64 &offset)
{
	quint8 b0, b1, b2, b3, b4;

	stream >> b0 >> b1 >> b2 >> b3 >> b4;
	offset = b4 | ((quint64)b3) << 8 | ((quint64)b2) << 16
	  | ((quint64)b1) << 24 | ((quint64)b0) << 32;

	return (stream.status() == QDataStream::Ok);
}

bool DatoMapa::readSubFiles()
{
	QDataStream stream(&_file);

	for (int i = 0; i < _subFiles.size(); i++) {
		const SubFileInfo &f = _subFiles.at(i);
		quint64 offset, nextOffset;

		stream.device()->seek(f.offset);

		QPoint tl(OSM::ll2tile(_bounds.topLeft(), f.base));
		QPoint br(OSM::ll2tile(_bounds.bottomRight(), f.base));

		if (!readOffset(stream, offset) || (offset & OFFSET_MASK) > f.size)
			return false;

		_tiles.append(new TileTree());

		for (int h = tl.y(); h <= br.y(); h++) {
			for (int w = tl.x(); w <= br.x(); w++) {
				if (!(h == br.y() && w == br.x())) {
					if (!readOffset(stream, nextOffset)
					  || (nextOffset & OFFSET_MASK) > f.size)
						return false;
					if (nextOffset == offset)
						continue;
				}

				Coordinates ttl(OSM::tile2ll(QPoint(w, h), f.base));
				ttl = Coordinates(ttl.lon(), -ttl.lat());
				Coordinates tbr(OSM::tile2ll(QPoint(w + 1, h + 1), f.base));
				tbr = Coordinates(tbr.lon(), -tbr.lat());
				RectC bounds(ttl, tbr);

				double min[2], max[2];
				min[0] = bounds.left();
				min[1] = bounds.bottom();
				max[0] = bounds.right();
				max[1] = bounds.top();
				_tiles.last()->Insert(min, max, new VectorTile(offset, bounds));

				offset = nextOffset;
			}
		}
	}

	return true;
}

bool DatoMapa::readHeader()
{
	char magic[sizeof(MAGIC) - 1];
	quint32 hdrSize, version;
	quint64 fileSize, date;
	qint32 minLat, minLon, maxLat, maxLon;
	quint16 tags;
	quint8 flags, zooms;
	QByteArray projection, tag;


	if (_file.read(magic, sizeof(magic)) < (int)sizeof(magic)
	  || memcmp(magic, MAGIC, sizeof(magic)))
		return false;
	if (_file.read((char*)&hdrSize, sizeof(hdrSize)) < (qint64)sizeof(hdrSize))
		return false;

	SubFicher subfile(_file, sizeof(magic) + sizeof(hdrSize),
	  qFromBigEndian(hdrSize));
	if (!subfile.seek(0))
		return false;
	if (!(subfile.readUInt32(version) && subfile.readUInt64(fileSize)
	  && subfile.readUInt64(date) && subfile.readInt32(minLat)
	  && subfile.readInt32(minLon) && subfile.readInt32(maxLat)
	  && subfile.readInt32(maxLon) && subfile.readUInt16(_tileSize)
	  && subfile.readString(projection) && subfile.readByte(flags)))
		return false;

	if (projection != "Mercator") {
		_errorString = projection + ": invalid/unsupported projection";
		return false;
	}
	if (flags & 0x80) {
		_errorString = "DEBUG maps not supported";
		return false;
	}

	if (flags & 0x40) {
		qint32 startLon, startLat;
		if (!(subfile.readInt32(startLat) && subfile.readInt32(startLon)))
			return false;
	}
	if (flags & 0x20) {
		quint8 startZoom;
		if (!subfile.readByte(startZoom))
			return false;
	}
	if (flags & 0x10) {
		QByteArray lang;
		if (!subfile.readString(lang))
			return false;
	}
	if (flags & 0x08) {
		QByteArray comment;
		if (!subfile.readString(comment))
			return false;
	}
	if (flags & 0x04) {
		QByteArray createdBy;
		if (!subfile.readString(createdBy))
			return false;
	}

	if (!subfile.readUInt16(tags))
		return false;
	_pointTags.resize(tags);
	for (quint16 i = 0; i < tags; i++) {
		if (!subfile.readString(tag))
			return false;
		_pointTags[i] = tag;
	}

	if (!subfile.readUInt16(tags))
		return false;
	_pathTags.resize(tags);
	for (quint16 i = 0; i < tags; i++) {
		if (!subfile.readString(tag))
			return false;
		_pathTags[i] = tag;
	}

	if (!subfile.readByte(zooms))
		return false;
	_subFiles.resize(zooms);
	for (quint8 i = 0; i < zooms; i++) {
		if (!(subfile.readByte(_subFiles[i].base)
		  && subfile.readByte(_subFiles[i].min)
		  && subfile.readByte(_subFiles[i].max)
		  && subfile.readUInt64(_subFiles[i].offset)
		  && subfile.readUInt64(_subFiles[i].size)))
			return false;
	}

	_bounds = RectC(Coordinates(MD(minLon), MD(maxLat)),
	  Coordinates(MD(maxLon), MD(minLat)));
	_bounds &= OSM::BOUNDS;

	return true;
}

DatoMapa::DatoMapa(const QString &fileName) : _file(fileName)
{
	if (!_file.open(QFile::ReadOnly)) {
		_errorString = _file.errorString();
		return;
	}

	if (!readHeader())
		return;

	_file.close();

	_pathCache.setMaxCost(256);
	_pointCache.setMaxCost(256);

	_valid = true;
}

DatoMapa::~DatoMapa()
{
	clearTiles();
}

RectC DatoMapa::bounds() const
{
	/* Align the map bounds with the OSM tiles to prevent area overlap artifacts
	   at least when using EPSG:3857 projection. */
	int zoom = _subFiles.last().base;
	QPoint tl(OSM::mercator2tile(OSM::ll2m(_bounds.topLeft()), zoom));
	Coordinates ctl(OSM::tile2ll(tl, zoom));
	ctl.rlat() = -ctl.lat();

	return RectC(ctl, _bounds.bottomRight());
}

void DatoMapa::load()
{
	if (_file.open(QIODevice::ReadOnly))
		readSubFiles();
}

void DatoMapa::clear()
{
	_file.close();

	_pathCache.clear();
	_pointCache.clear();

	clearTiles();
}

void DatoMapa::clearTiles()
{
	TileTree::Iterator it;

	for (int i = 0; i < _tiles.size(); i++) {
		TileTree *t = _tiles.at(i);
		for (t->GetFirst(it); !t->IsNull(it); t->GetNext(it))
			delete t->GetAt(it);
	}

	qDeleteAll(_tiles);
	_tiles.clear();
}

bool DatoMapa::pathCb(VectorTile *tile, void *context)
{
	PathCTX *ctx = (PathCTX*)context;
	ctx->data->paths(tile, ctx->rect, ctx->zoom, ctx->list);
	return true;
}

bool DatoMapa::pointCb(VectorTile *tile, void *context)
{
	PointCTX *ctx = (PointCTX*)context;
	ctx->data->points(tile, ctx->rect, ctx->zoom, ctx->list);
	return true;
}

int DatoMapa::level(int zoom) const
{
	for (int i = 0; i < _subFiles.size(); i++)
		if (zoom <= _subFiles.at(i).max)
			return i;

	return _subFiles.size() - 1;
}

void DatoMapa::points(const RectC &rect, int zoom, QList<Point> *list)
{
	int l(level(zoom));
	PointCTX ctx(this, rect, zoom, list);
	double min[2], max[2];

	min[0] = rect.left();
	min[1] = rect.bottom();
	max[0] = rect.right();
	max[1] = rect.top();

	_tiles.at(l)->Search(min, max, pointCb, &ctx);
}

void DatoMapa::points(const VectorTile *tile, const RectC &rect, int zoom,
  QList<Point> *list)
{
	Key key(tile, zoom);
	QList<Point> *cached = _pointCache.object(key);

	if (!cached) {
		QList<Point> *p = new QList<Point>();
		if (readPoints(tile, zoom, p)) {
			copyPoints(rect, p, list);
			_pointCache.insert(key, p);
		} else
			delete p;
	} else
		copyPoints(rect, cached, list);
}

void DatoMapa::paths(const RectC &rect, int zoom, QList<Path> *list)
{
	int l(level(zoom));
	PathCTX ctx(this, rect, zoom, list);
	double min[2], max[2];

	min[0] = rect.left();
	min[1] = rect.bottom();
	max[0] = rect.right();
	max[1] = rect.top();

	_tiles.at(l)->Search(min, max, pathCb, &ctx);
}

void DatoMapa::paths(const VectorTile *tile, const RectC &rect, int zoom,
  QList<Path> *list)
{
	Key key(tile, zoom);
	QList<Path> *cached = _pathCache.object(key);

	if (!cached) {
		QList<Path> *p = new QList<Path>();
		if (readPaths(tile, zoom, p)) {
			copyPaths(rect, p, list);
			_pathCache.insert(key, p);
		} else
			delete p;
	} else
		copyPaths(rect, cached, list);
}

bool DatoMapa::readPaths(const VectorTile *tile, int zoom, QList<Path> *list)
{
	const SubFileInfo &info = _subFiles.at(level(zoom));
	SubFicher subfile(_file, info.offset, info.size);
	int rows = info.max - info.min + 1;
	QVector<unsigned> paths(rows);
	quint32 blocks, unused, val, cnt = 0;
	quint16 bitmap;
	quint8 sb, flags;
	QByteArray name, houseNumber, reference;


	if (!subfile.seek(tile->offset & OFFSET_MASK))
		return false;

	for (int i = 0; i < rows; i++) {
		if (!(subfile.readVUInt32(unused) && subfile.readVUInt32(val)))
			return false;
		cnt += val;
		paths[i] = cnt;
	}

	if (!subfile.readVUInt32(val))
		return false;
	if (!subfile.seek(subfile.pos() + val))
		return false;

	for (unsigned i = 0; i < paths[zoom - info.min]; i++) {
		Path p;
		qint32 lon = 0, lat = 0;

		if (!(subfile.readVUInt32(unused) && subfile.readUInt16(bitmap)
		  && subfile.readByte(sb)))
			return false;

		p.layer = sb >> 4;
		int tags = sb & 0x0F;
		if (!readTags(subfile, tags, _pathTags, p.tags))
			return false;

		if (!subfile.readByte(flags))
			return false;
		if (flags & 0x80) {
			if (!subfile.readString(name))
				return false;
			name = name.split('\r').first();
			p.tags.append(Tag("name", name));
		}
		if (flags & 0x40) {
			if (!subfile.readString(houseNumber))
				return false;
			p.tags.append(Tag("addr:housenumber", houseNumber));
		}
		if (flags & 0x20) {
			if (!subfile.readString(reference))
				return false;
			p.tags.append(Tag("ref", reference));
		}
		if (flags & 0x10) {
			if (!(subfile.readVInt32(lat) && subfile.readVInt32(lon)))
				return false;
		}
		if (flags & 0x08) {
			if (!subfile.readVUInt32(blocks))
				return false;
		} else
			blocks = 1;

		Q_ASSERT(blocks);
		for (unsigned j = 0; j < blocks; j++) {
			if (!readPolygon(subfile, tile->pos, flags & 0x04, p.poly))
				return false;
			p.closed = isClosed(p.poly);
			if (flags & 0x10)
				p.labelPos = Coordinates(p.poly.first().first().lon() + MD(lon),
				  p.poly.first().first().lat() + MD(lat));

			list->append(p);
		}
	}

	return true;
}

bool DatoMapa::readPoints(const VectorTile *tile, int zoom, QList<Point> *list)
{
	const SubFileInfo &info = _subFiles.at(level(zoom));
	SubFicher subfile(_file, info.offset, info.size);
	int rows = info.max - info.min + 1;
	QVector<unsigned> points(rows);
	quint32 val, unused, cnt = 0;
	quint8 sb, flags;
	QByteArray name, houseNumber;


	if (!subfile.seek(tile->offset & OFFSET_MASK))
		return false;

	for (int i = 0; i < rows; i++) {
		if (!(subfile.readVUInt32(val) && subfile.readVUInt32(unused)))
			return false;
		cnt += val;
		points[i] = cnt;
	}

	if (!subfile.readVUInt32(unused))
		return false;

	for (unsigned i = 0; i < points[zoom - info.min]; i++) {
		qint32 lat, lon;

		if (!(subfile.readVInt32(lat) && subfile.readVInt32(lon)))
			return false;
		Point p(Coordinates(tile->pos.lon() + MD(lon),
		  tile->pos.lat() + MD(lat)));

		if (!subfile.readByte(sb))
			return false;
		p.layer = sb >> 4;
		int tags = sb & 0x0F;
		if (!readTags(subfile, tags, _pointTags, p.tags))
			return false;

		if (!subfile.readByte(flags))
			return false;
		if (flags & 0x80) {
			if (!subfile.readString(name))
				return false;
			name = name.split('\r').first();
			p.tags.append(Tag("name", name));
		}
		if (flags & 0x40) {
			if (!subfile.readString(houseNumber))
				return false;
			p.tags.append(Tag("addr:housenumber", houseNumber));
		}
		if (flags & 0x20) {
			qint32 elevation;
			if (!subfile.readVInt32(elevation))
				return false;
			p.tags.append(Tag("ele", QByteArray::number(elevation)));
		}

		setPointId(p);
		list->append(p);
	}

	return true;
}

bool DatoMapa::isMapsforge(const QString &path)
{
	QFile file(path);
	char magic[sizeof(MAGIC) - 1];

	if (!file.open(QFile::ReadOnly))
		return false;
	if (file.read(magic, sizeof(magic)) < (qint64)sizeof(magic))
		return false;

	return !memcmp(magic, MAGIC, sizeof(magic));
}

#ifndef QT_NO_DEBUG
QDebug operator<<(QDebug dbg, const DatoMapa::Tag &tag)
{
	dbg.nospace() << "Tag(" << tag.key << ", " << tag.value << ")";
	return dbg.space();
}

QDebug operator<<(QDebug dbg, const DatoMapa::Path &path)
{
	dbg.nospace() << "Path(" << path.poly.boundingRect() << ", " << path.label
	  << ", " << path.tags << ")";
	return dbg.space();
}

QDebug operator<<(QDebug dbg, const DatoMapa::Point &point)
{
	dbg.nospace() << "Point(" << point.coordinates << "," << point.label
	  << ", " << point.tags << ")";
	return dbg.space();
}
#endif // QT_NO_DEBUG
