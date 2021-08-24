#ifndef MAPSFORGE_ESTILO_H
#define MAPSFORGE_ESTILO_H

#include <QString>
#include <QList>
#include <QPen>
#include <QFont>
#include "datomapa.h"

class QXmlStreamReader;

inline bool wcmp(const QByteArray &b1, const QByteArray &b2)
{
	int len = b1.length();

	if (!len)
		return true;
	if (len != b2.length())
		return false;
	return !memcmp(b1.constData(), b2.constData(), len);
}

class Estilo
{
public:
	class Rule {
	public:
		Rule() : _type(AnyType), _closed(AnyClosed), _zooms(0, 127) {}

		bool match(const QVector<DatoMapa::Tag> &tags) const;
		bool match(bool closed, const QVector<DatoMapa::Tag> &tags) const;
		bool match(int zoom, bool closed,
		  const QVector<DatoMapa::Tag> &tags) const;

	private:
		enum Type {
			AnyType = 0,
			NodeType = 1,
			WayType = 2,
			InvalidType = 3
		};

		enum Closed {
			AnyClosed = 0,
			YesClosed = 1,
			NoClosed = 2,
			InvalidClosed = 3
		};

		class Filter {
		public:
			Filter() : _neg(false) {}
			Filter(const QList<QByteArray> &keys, const QList<QByteArray> &vals)
			  : _neg(false)
			{
				_keys = list(keys);

				QList<QByteArray> vc(vals);
				if (vc.removeAll("~"))
					_neg = true;
				_vals = list(vc);
			}

			bool match(const QVector<DatoMapa::Tag> &tags) const
			{
				if (_neg) {
					if (!keyMatches(tags))
						return true;
					return valueMatches(tags);
				} else
					return (keyMatches(tags) && valueMatches(tags));
			}

			bool isTautology() const
			{
				return (!_neg && _keys.contains(QByteArray())
				  && _vals.contains(QByteArray()));
			}

		private:
			static QList<QByteArray> list(const QList<QByteArray> &in)
			{
				QList<QByteArray> out;

				for (int i = 0; i < in.size(); i++) {
					if (in.at(i) == "*")
						out.append(QByteArray());
					else
						out.append(in.at(i));
				}

				return out;
			}

			bool keyMatches(const QVector<DatoMapa::Tag> &tags) const
			{
				for (int i = 0; i < _keys.size(); i++)
					for (int j = 0; j < tags.size(); j++)
						if (wcmp(_keys.at(i), tags.at(j).key))
							return true;

				return false;
			}

			bool valueMatches(const QVector<DatoMapa::Tag> &tags) const
			{
				for (int i = 0; i < _vals.size(); i++)
					for (int j = 0; j < tags.size(); j++)
						if (wcmp(_vals.at(i), tags.at(j).value))
							return true;

				return false;
			}

			QList<QByteArray> _keys;
			QList<QByteArray> _vals;
			bool _neg;
		};

		void setType(Type type)
		{
			_type = static_cast<Type>(static_cast<int>(type)
			  | static_cast<int>(_type));
		}
		void setMinZoom(int zoom) {_zooms.setMin(qMax(zoom, _zooms.min()));}
		void setMaxZoom(int zoom) {_zooms.setMax(qMin(zoom, _zooms.max()));}
		void setClosed(Closed closed)
		{
			_closed = static_cast<Closed>(static_cast<int>(closed)
			  | static_cast<int>(_closed));
		}
		void addFilter(const Filter &filter)
		{
			if (!filter.isTautology())
				_filters.append(filter);
		}
		bool match(int zoom, Type type, Closed closed,
		  const QVector<DatoMapa::Tag> &tags) const;

		friend class Estilo;

		Type _type;
		Closed _closed;
		Range _zooms;
		QVector<Filter> _filters;
	};

	class Render
	{
	public:
		Render(const Rule &rule) : _rule(rule) {}

		const Rule &rule() const {return _rule;}

	private:
		Rule _rule;
	};

	class PathRender : public Render
	{
	public:
		PathRender(const Rule &rule, int zOrder) : Render(rule),
		  _zOrder(zOrder), _strokeWidth(0), _strokeCap(Qt::RoundCap),
		  _strokeJoin(Qt::RoundJoin), _area(false) {}

		int zOrder() const {return _zOrder;}
		QPen pen(int zoom) const;
		QBrush brush() const;
		bool area() const {return _area;}

	private:
		friend class Estilo;

		int _zOrder;
		QColor _fillColor, _strokeColor;
		qreal _strokeWidth;
		QVector<qreal> _strokeDasharray;
		Qt::PenCapStyle _strokeCap;
		Qt::PenJoinStyle _strokeJoin;
		QImage _fillImage;
		bool _area;
	};

	class TextRender : public Render
	{
	public:
		TextRender(const Rule &rule)
		  : Render(rule), _fillColor(Qt::black), _strokeColor(Qt::black),
		  _strokeWidth(0) {}

		const QFont &font() const {return _font;}
		const QColor &fillColor() const {return _fillColor;}
		const QColor &strokeColor() const {return _strokeColor;}
		qreal strokeWidth() const {return _strokeWidth;}
		const QByteArray &key() const {return _key;}

	private:
		friend class Estilo;

		QColor _fillColor, _strokeColor;
		qreal _strokeWidth;
		QFont _font;
		QByteArray _key;
	};

	class Symbol : public Render
	{
	public:
		Symbol(const Rule &rule) : Render(rule) {}

		const QImage &img() const {return _img;}

	private:
		friend class Estilo;

		QImage _img;
	};

	Estilo(const QString &path, qreal ratio);

	QVector<const PathRender *> paths(int zoom, bool closed,
	  const QVector<DatoMapa::Tag> &tags) const;
	QList<const TextRender*> pathLabels(int zoom) const;
	QList<const TextRender*> pointLabels(int zoom) const;
	QList<const TextRender*> areaLabels(int zoom) const;
	QList<const Symbol*> pointSymbols(int zoom) const;
	QList<const Symbol*> areaSymbols(int zoom) const;

private:
	QList<PathRender> _paths;
	QList<TextRender> _pathLabels, _pointLabels, _areaLabels;
	QList<Symbol> _symbols;

	bool loadXml(const QString &path, qreal ratio);
	void rendertheme(QXmlStreamReader &reader, const QString &dir, qreal ratio);
	void layer(QXmlStreamReader &reader, QSet<QString> &cats);
	void stylemenu(QXmlStreamReader &reader, QSet<QString> &cats);
	void cat(QXmlStreamReader &reader, QSet<QString> &cats);
	void rule(QXmlStreamReader &reader, const QString &dir, qreal ratio,
	  const QSet<QString> &cats, const Rule &parent);
	void area(QXmlStreamReader &reader, const QString &dir, qreal ratio,
	  const Rule &rule);
	void line(QXmlStreamReader &reader, const Rule &rule);
	void text(QXmlStreamReader &reader, const Rule &rule,
	  QList<QList<TextRender> *> &lists);
	void symbol(QXmlStreamReader &reader, const QString &dir, qreal ratio,
	  const Rule &rule);
};

#endif // MAPSFORGE_ESTILO_H
