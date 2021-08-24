#ifndef IMG_HUFFMANSTREAM_H
#define IMG_HUFFMANSTREAM_H

#include "bitstream.h"
#include "huffmantable.h"

namespace IMG {

template <class BitStream>
class HuffmanStream {
public:
	HuffmanStream(BitStream &bitstream, const HuffmanTable &table)
	  : _bs(bitstream), _table(table), _symbolDataSize(0), _symbolData(0),
	  _lonSign(0), _latSign(0) {}

	bool read(int bits, quint32 &val);
	bool readSymbol(quint32 &symbol);
	bool readNext(qint32 &lonDelta, qint32 &latDelta)
	{
		if (!(readDelta(_lonSign, lonDelta) && readDelta(_latSign, latDelta)))
			return false;

		return (lonDelta || latDelta);
	}

	bool atEnd() const
	  {return _symbolDataSize + _bs.bitsAvailable() < _table.maxSymbolSize();}
	bool flush() {return _bs.flush();}

protected:
	bool sign(int &val);
	bool readDelta(int sign, qint32 &delta);

	BitStream &_bs;
	const HuffmanTable &_table;
	quint32 _symbolDataSize;
	quint32 _symbolData;
	int _lonSign, _latSign;
};

template <class BitStream>
bool HuffmanStream<BitStream>::sign(int &val)
{
	quint32 bit;
	val = 0;

	if (!_bs.read(1, bit))
		return false;
	if (bit) {
		if (!_bs.read(1, bit))
			return false;
		val = bit ? -1 : 1;
	}

	return true;
}

template <class BitStream>
bool HuffmanStream<BitStream>::read(int bits, quint32 &val)
{
	if (_symbolDataSize < (quint32)bits) {
		quint32 next;
		quint8 nextSize = qMin((quint64)(32 - _symbolDataSize),
		  _bs.bitsAvailable());

		if (!_bs.read(nextSize, next))
			return false;

		_symbolData = (_symbolData << nextSize) | next;
		_symbolDataSize += nextSize;
	}

	if (_symbolDataSize < (quint32)bits)
		return false;

	val = (_symbolData << (32-_symbolDataSize)) >> (32 - bits);
	_symbolDataSize -= bits;

	return true;
}

template <class BitStream>
bool HuffmanStream<BitStream>::readSymbol(quint32 &symbol)
{
	quint8 size;
	quint32 next;
	quint8 nextSize = qMin((quint64)(32 - _symbolDataSize), _bs.bitsAvailable());

	if (!_bs.read(nextSize, next))
		return false;

	_symbolData = (_symbolData << nextSize) | next;
	_symbolDataSize += nextSize;

	symbol = _table.symbol(_symbolData << (32 - _symbolDataSize), size);
	if (size > _symbolDataSize)
		return false;

	_symbolDataSize -= size;

	return true;
}

template <class BitStream>
bool HuffmanStream<BitStream>::readDelta(int sign, qint32 &delta)
{
	quint32 symbol;
	if (!readSymbol(symbol))
		return false;

	if (symbol && !sign) {
		if (!_symbolDataSize)
			return false;
		else {
			sign = ((1U << (_symbolDataSize - 1)) & _symbolData) ? -1 : 1;
			_symbolDataSize--;
		}
	}
	delta = sign * symbol;

	return true;
}

class HuffmanStreamF : public HuffmanStream<BitStream4F> {
public:
	HuffmanStreamF(BitStream4F &bitstream, const HuffmanTable &table)
	  : HuffmanStream(bitstream, table) {}

	bool init(bool line);
	bool readOffset(qint32 &lonDelta, qint32 &latDelta)
	  {return (readDelta(1, lonDelta) && readDelta(1, latDelta));}
};

class HuffmanStreamR : public HuffmanStream<BitStream4R> {
public:
	HuffmanStreamR(BitStream4R &bitstream, const HuffmanTable &table)
	  : HuffmanStream(bitstream, table) {}

	bool init();
	bool init(int lonSign, int latSign, quint32 data, quint32 dataSize);
};

}

#endif // IMG_HUFFMANSTREAM_H
