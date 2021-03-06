/*
This file is part of Telegram Desktop,
the official desktop version of Telegram messaging app, see https://telegram.org

Telegram Desktop is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

It is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
GNU General Public License for more details.

In addition, as a special exception, the copyright holders give permission
to link the code of portions of this program with the OpenSSL library.

Full license: https://github.com/telegramdesktop/tdesktop/blob/master/LICENSE
Copyright (c) 2014-2016 John Preston, https://desktop.telegram.org
*/
#include "stdafx.h"
#include "ui/widgets/multi_select.h"

#include "styles/style_widgets.h"
#include "ui/buttons/icon_button.h"
#include "lang.h"

namespace Ui {
namespace {

constexpr int kWideScale = 3;

} // namespace

class MultiSelect::Inner::Item {
public:
	Item(const style::MultiSelectItem &st, uint64 id, const QString &text, const style::color &color, PaintRoundImage paintRoundImage);

	uint64 id() const {
		return _id;
	}
	int getWidth() const {
		return _width;
	}
	QRect rect() const {
		return QRect(_x, _y, _width, _st.height);
	}
	bool isOverDelete() const {
		return _overDelete;
	}
	void setActive(bool active) {
		_active = active;
	}
	void setPosition(int x, int y, int outerWidth, int maxVisiblePadding);
	QRect paintArea(int outerWidth) const;

	void setUpdateCallback(base::lambda_wrap<void()> updateCallback) {
		_updateCallback = std_::move(updateCallback);
	}
	void setText(const QString &text);
	void paint(Painter &p, int outerWidth, uint64 ms);

	void mouseMoveEvent(QPoint point);
	void leaveEvent();

	void showAnimated() {
		setVisibleAnimated(true);
	}
	void hideAnimated() {
		setVisibleAnimated(false);
	}
	bool hideFinished() const {
		return (_hiding && !_visibility.animating());
	}


private:
	void setOver(bool over);
	void paintOnce(Painter &p, int x, int y, int outerWidth, uint64 ms);
	void paintDeleteButton(Painter &p, int x, int y, int outerWidth, float64 overOpacity);
	bool paintCached(Painter &p, int x, int y, int outerWidth);
	void prepareCache();
	void setVisibleAnimated(bool visible);

	const style::MultiSelectItem &_st;

	uint64 _id;
	struct SlideAnimation {
		SlideAnimation(base::lambda_wrap<void()> updateCallback, int fromX, int toX, int y, float64 duration)
			: fromX(fromX)
			, toX(toX)
			, y(y) {
			x.start(std_::move(updateCallback), fromX, toX, duration);
		}
		IntAnimation x;
		int fromX, toX;
		int y;
	};
	std_::vector_of_moveable<SlideAnimation> _copies;
	int _x = -1;
	int _y = -1;
	int _width = 0;
	Text _text;
	const style::color &_color;
	bool _over = false;
	QPixmap _cache;
	FloatAnimation _visibility;
	FloatAnimation _overOpacity;
	bool _overDelete = false;
	bool _active = false;
	PaintRoundImage _paintRoundImage;
	base::lambda_wrap<void()> _updateCallback;
	bool _hiding = false;

};

MultiSelect::Inner::Item::Item(const style::MultiSelectItem &st, uint64 id, const QString &text, const style::color &color, PaintRoundImage paintRoundImage)
: _st(st)
, _id(id)
, _color(color)
, _paintRoundImage(std_::move(paintRoundImage)) {
	setText(text);
}

void MultiSelect::Inner::Item::setText(const QString &text) {
	_text.setText(_st.font, text, _textNameOptions);
	_width = _st.height + _st.padding.left() + _text.maxWidth() + _st.padding.right();
	accumulate_min(_width, _st.maxWidth);
}

void MultiSelect::Inner::Item::paint(Painter &p, int outerWidth, uint64 ms) {
	if (!_cache.isNull() && !_visibility.animating(ms)) {
		if (_hiding) {
			return;
		} else {
			_cache = QPixmap();
		}
	}
	if (_copies.empty()) {
		paintOnce(p, _x, _y, outerWidth, ms);
	} else {
		for (auto i = _copies.begin(), e = _copies.end(); i != e;) {
			auto x = i->x.current(getms(), _x);
			auto y = i->y;
			auto animating = i->x.animating();
			if (animating || (y == _y)) {
				paintOnce(p, x, y, outerWidth, ms);
			}
			if (animating) {
				++i;
			} else {
				i = _copies.erase(i);
				e = _copies.end();
			}
		}
	}
}

void MultiSelect::Inner::Item::paintOnce(Painter &p, int x, int y, int outerWidth, uint64 ms) {
	if (!_cache.isNull()) {
		paintCached(p, x, y, outerWidth);
		return;
	}

	auto radius = _st.height / 2;
	auto inner = rtlrect(x + radius, y, _width - radius, _st.height, outerWidth);

	auto clipEnabled = p.hasClipping();
	auto clip = clipEnabled ? p.clipRegion() : QRegion();
	p.setRenderHint(QPainter::HighQualityAntialiasing);
	p.setClipRect(inner);

	p.setPen(Qt::NoPen);
	p.setBrush(_active ? _st.textActiveBg : _st.textBg);
	p.drawRoundedRect(rtlrect(x, y, _width, _st.height, outerWidth), radius, radius);

	if (clipEnabled) {
		p.setClipRegion(clip);
	} else {
		p.setClipping(false);
	}
	p.setRenderHint(QPainter::HighQualityAntialiasing, false);

	auto overOpacity = _overOpacity.current(ms, _over ? 1. : 0.);
	if (overOpacity < 1.) {
		_paintRoundImage(p, x, y, outerWidth, _st.height);
	}
	if (overOpacity > 0.) {
		paintDeleteButton(p, x, y, outerWidth, overOpacity);
	}

	auto textLeft = _st.height + _st.padding.left();
	auto textWidth = _width - textLeft - _st.padding.right();
	p.setPen(_active ? _st.textActiveFg : _st.textFg);
	_text.drawLeftElided(p, x + textLeft, y + _st.padding.top(), textWidth, outerWidth);
}

void MultiSelect::Inner::Item::paintDeleteButton(Painter &p, int x, int y, int outerWidth, float64 overOpacity) {
	p.setOpacity(overOpacity);
	p.setRenderHint(QPainter::HighQualityAntialiasing);
	p.setPen(Qt::NoPen);
	p.setBrush(_color);
	p.drawEllipse(rtlrect(x, y, _st.height, _st.height, outerWidth));

	auto deleteScale = overOpacity + _st.minScale * (1. - overOpacity);
	auto deleteSkip = deleteScale * _st.deleteLeft + (1. - deleteScale) * (_st.height / 2);
	auto sqrt2 = sqrt(2.);
	auto deleteLeft = rtlpoint(x + deleteSkip, 0, outerWidth).x() + 0.;
	auto deleteTop = y + deleteSkip + 0.;
	auto deleteWidth = _st.height - 2 * deleteSkip;
	auto deleteHeight = _st.height - 2 * deleteSkip;
	auto deleteStroke = _st.deleteStroke / sqrt2;
	QPointF pathDelete[] = {
		{ deleteLeft, deleteTop + deleteStroke },
		{ deleteLeft + deleteStroke, deleteTop },
		{ deleteLeft + (deleteWidth / 2.), deleteTop + (deleteHeight / 2.) - deleteStroke },
		{ deleteLeft + deleteWidth - deleteStroke, deleteTop },
		{ deleteLeft + deleteWidth, deleteTop + deleteStroke },
		{ deleteLeft + (deleteWidth / 2.) + deleteStroke, deleteTop + (deleteHeight / 2.) },
		{ deleteLeft + deleteWidth, deleteTop + deleteHeight - deleteStroke },
		{ deleteLeft + deleteWidth - deleteStroke, deleteTop + deleteHeight },
		{ deleteLeft + (deleteWidth / 2.), deleteTop + (deleteHeight / 2.) + deleteStroke },
		{ deleteLeft + deleteStroke, deleteTop + deleteHeight },
		{ deleteLeft, deleteTop + deleteHeight - deleteStroke },
		{ deleteLeft + (deleteWidth / 2.) - deleteStroke, deleteTop + (deleteHeight / 2.) },
	};
	if (overOpacity < 1.) {
		auto alpha = -(overOpacity - 1.) * M_PI_2;
		auto cosalpha = cos(alpha);
		auto sinalpha = sin(alpha);
		auto shiftx = deleteLeft + (deleteWidth / 2.);
		auto shifty = deleteTop + (deleteHeight / 2.);
		for (auto &point : pathDelete) {
			auto x = point.x() - shiftx;
			auto y = point.y() - shifty;
			point.setX(shiftx + x * cosalpha - y * sinalpha);
			point.setY(shifty + y * cosalpha + x * sinalpha);
		}
	}
	QPainterPath path;
	path.moveTo(pathDelete[0]);
	for (int i = 1; i != base::array_size(pathDelete); ++i) {
		path.lineTo(pathDelete[i]);
	}
	p.fillPath(path, _st.deleteFg);

	p.setRenderHint(QPainter::HighQualityAntialiasing, false);
	p.setOpacity(1.);
}

bool MultiSelect::Inner::Item::paintCached(Painter &p, int x, int y, int outerWidth) {
	auto opacity = _visibility.current(_hiding ? 0. : 1.);
	auto scale = opacity + _st.minScale * (1. - opacity);
	auto height = opacity * _cache.height() / _cache.devicePixelRatio();
	auto width = opacity * _cache.width() / _cache.devicePixelRatio();

	p.setOpacity(opacity);
	p.setRenderHint(QPainter::SmoothPixmapTransform, true);
	p.drawPixmap(rtlrect(x + (_width - width) / 2., y + (_st.height - height) / 2., width, height, outerWidth), _cache);
	p.setRenderHint(QPainter::SmoothPixmapTransform, false);
	p.setOpacity(1.);
	return true;
}

void MultiSelect::Inner::Item::mouseMoveEvent(QPoint point) {
	if (!_cache.isNull()) return;
	_overDelete = QRect(0, 0, _st.height, _st.height).contains(point);
	setOver(true);
}

void MultiSelect::Inner::Item::leaveEvent() {
	_overDelete = false;
	setOver(false);
}

void MultiSelect::Inner::Item::setPosition(int x, int y, int outerWidth, int maxVisiblePadding) {
	if (_x >= 0 && _y >= 0 && (_x != x || _y != y)) {
		// Make an animation if it is not the first setPosition().
		auto found = false;
		auto leftHidden = -_width - maxVisiblePadding;
		auto rightHidden = outerWidth + maxVisiblePadding;
		for (auto i = _copies.begin(), e = _copies.end(); i != e;) {
			if (i->x.animating()) {
				if (i->y == y) {
					i->x.start(_updateCallback, i->toX, x, _st.duration);
					found = true;
				} else {
					i->x.start(_updateCallback, i->fromX, (i->toX > i->fromX) ? rightHidden : leftHidden, _st.duration);
				}
				++i;
			} else {
				i = _copies.erase(i);
				e = _copies.end();
			}
		}
		if (_copies.empty()) {
			if (_y == y) {
				auto copy = SlideAnimation(_updateCallback, _x, x, _y, _st.duration);
				_copies.push_back(std_::move(copy));
			} else {
				auto copyHiding = SlideAnimation(_updateCallback, _x, (y > _y) ? rightHidden : leftHidden, _y, _st.duration);
				_copies.push_back(std_::move(copyHiding));
				auto copyShowing = SlideAnimation(_updateCallback, (y > _y) ? leftHidden : rightHidden, x, y, _st.duration);
				_copies.push_back(std_::move(copyShowing));
			}
		} else if (!found) {
			auto copy = SlideAnimation(_updateCallback, (y > _y) ? leftHidden : rightHidden, x, y, _st.duration);
			_copies.push_back(std_::move(copy));
		}
	}
	_x = x;
	_y = y;
}

QRect MultiSelect::Inner::Item::paintArea(int outerWidth) const {
	if (_copies.empty()) {
		return rect();
	}
	auto yMin = 0, yMax = 0;
	for_const (auto &copy, _copies) {
		accumulate_max(yMax, copy.y);
		if (yMin) {
			accumulate_min(yMin, copy.y);
		} else {
			yMin = copy.y;
		}
	}
	return QRect(0, yMin, outerWidth, yMax - yMin + _st.height);
}

void MultiSelect::Inner::Item::prepareCache() {
	if (!_cache.isNull()) return;

	t_assert(!_visibility.animating());
	auto cacheWidth = _width * kWideScale * cIntRetinaFactor();
	auto cacheHeight = _st.height * kWideScale * cIntRetinaFactor();
	auto data = QImage(cacheWidth, cacheHeight, QImage::Format_ARGB32_Premultiplied);
	data.fill(Qt::transparent);
	data.setDevicePixelRatio(cRetinaFactor());
	{
		Painter p(&data);
		paintOnce(p, _width * (kWideScale - 1) / 2, _st.height  * (kWideScale - 1) / 2, cacheWidth, getms());
	}
	_cache = App::pixmapFromImageInPlace(std_::move(data));
}

void MultiSelect::Inner::Item::setVisibleAnimated(bool visible) {
	_hiding = !visible;
	prepareCache();
	auto from = visible ? 0. : 1.;
	auto to = visible ? 1. : 0.;
	auto transition = visible ? anim::bumpy<1125, 1000> : anim::linear;
	_visibility.start(_updateCallback, from, to, _st.duration, transition);
}

void MultiSelect::Inner::Item::setOver(bool over) {
	if (over != _over) {
		_over = over;
		_overOpacity.start(_updateCallback, _over ? 0. : 1., _over ? 1. : 0., _st.duration);
	}
}

MultiSelect::MultiSelect(QWidget *parent, const style::MultiSelect &st, const QString &placeholder) : TWidget(parent)
, _st(st)
, _scroll(this, _st.scroll)
, _inner(this, st, placeholder, [this](int activeTop, int activeBottom) { scrollTo(activeTop, activeBottom); }) {
	_scroll->setOwnedWidget(_inner);
	_scroll->installEventFilter(this);
	_inner->setResizedCallback([this](int innerHeightDelta) {
		auto newHeight = resizeGetHeight(width());
		if (innerHeightDelta > 0) {
			_scroll->scrollToY(_scroll->scrollTop() + innerHeightDelta);
		}
		if (newHeight != height()) {
			resize(width(), newHeight);
			if (_resizedCallback) {
				_resizedCallback();
			}
		}
	});
	_inner->setQueryChangedCallback([this](const QString &query) {
		_scroll->scrollToY(_scroll->scrollTopMax());
		if (_queryChangedCallback) {
			_queryChangedCallback(query);
		}
	});

	setAttribute(Qt::WA_OpaquePaintEvent);
}

bool MultiSelect::eventFilter(QObject *o, QEvent *e) {
	if (o == _scroll && e->type() == QEvent::KeyPress) {
		e->ignore();
		return true;
	}
	return false;
}

void MultiSelect::scrollTo(int activeTop, int activeBottom) {
	auto scrollTop = _scroll->scrollTop();
	auto scrollHeight = _scroll->height();
	auto scrollBottom = scrollTop + scrollHeight;
	if (scrollTop > activeTop) {
		_scroll->scrollToY(activeTop);
	} else if (scrollBottom < activeBottom) {
		_scroll->scrollToY(activeBottom - scrollHeight);
	}
}

void MultiSelect::setQueryChangedCallback(base::lambda_unique<void(const QString &query)> callback) {
	_queryChangedCallback = std_::move(callback);
}

void MultiSelect::setSubmittedCallback(base::lambda_unique<void(bool ctrlShiftEnter)> callback) {
	_inner->setSubmittedCallback(std_::move(callback));
}

void MultiSelect::setResizedCallback(base::lambda_unique<void()> callback) {
	_resizedCallback = std_::move(callback);
}

void MultiSelect::setInnerFocus() {
	if (_inner->setInnerFocus()) {
		_scroll->scrollToY(_scroll->scrollTopMax());
	}
}

void MultiSelect::clearQuery() {
	_inner->clearQuery();
}

QString MultiSelect::getQuery() const {
	return _inner->getQuery();
}

void MultiSelect::addItem(uint64 itemId, const QString &text, const style::color &color, PaintRoundImage paintRoundImage, AddItemWay way) {
	_inner->addItem(std_::make_unique<Inner::Item>(_st.item, itemId, text, color, std_::move(paintRoundImage)), way);
}

void MultiSelect::setItemRemovedCallback(base::lambda_unique<void(uint64 itemId)> callback) {
	_inner->setItemRemovedCallback(std_::move(callback));
}

void MultiSelect::removeItem(uint64 itemId) {
	_inner->removeItem(itemId);
}

int MultiSelect::resizeGetHeight(int newWidth) {
	if (newWidth != _inner->width()) {
		_inner->resizeToWidth(newWidth);
	}
	auto newHeight = qMin(_inner->height(), _st.maxHeight);
	_scroll->setGeometryToLeft(0, 0, newWidth, newHeight);
	return newHeight;
}

MultiSelect::Inner::Inner(QWidget *parent, const style::MultiSelect &st, const QString &placeholder, ScrollCallback callback) : ScrolledWidget(parent)
, _st(st)
, _scrollCallback(std_::move(callback))
, _field(this, _st.field, placeholder)
, _cancel(this, _st.fieldCancel) {
	_field->customUpDown(true);
	connect(_field, SIGNAL(focused()), this, SLOT(onFieldFocused()));
	connect(_field, SIGNAL(changed()), this, SLOT(onQueryChanged()));
	connect(_field, SIGNAL(submitted(bool)), this, SLOT(onSubmitted(bool)));
	_cancel->hide();
	_cancel->setClickedCallback([this] {
		clearQuery();
		_field->setFocus();
	});
	setMouseTracking(true);
}

void MultiSelect::Inner::onQueryChanged() {
	auto query = getQuery();
	_cancel->setVisible(!query.isEmpty());
	updateFieldGeometry();
	if (_queryChangedCallback) {
		_queryChangedCallback(query);
	}
}

QString MultiSelect::Inner::getQuery() const {
	return _field->getLastText().trimmed();
}

bool MultiSelect::Inner::setInnerFocus() {
	if (_active >= 0) {
		setFocus();
	} else if (!_field->hasFocus()) {
		_field->setFocus();
		return true;
	}
	return false;
}

void MultiSelect::Inner::clearQuery() {
	_field->setText(QString());
}

void MultiSelect::Inner::setQueryChangedCallback(base::lambda_unique<void(const QString &query)> callback) {
	_queryChangedCallback = std_::move(callback);
}

void MultiSelect::Inner::setSubmittedCallback(base::lambda_unique<void(bool ctrlShiftEnter)> callback) {
	_submittedCallback = std_::move(callback);
}

void MultiSelect::Inner::updateFieldGeometry() {
	auto fieldFinalWidth = _fieldWidth;
	if (!_cancel->isHidden()) {
		fieldFinalWidth -= _st.fieldCancelSkip;
	}
	_field->resizeToWidth(fieldFinalWidth);
	_field->moveToLeft(_st.padding.left() + _fieldLeft, _st.padding.top() + _fieldTop);
}

void MultiSelect::Inner::updateHasAnyItems(bool hasAnyItems) {
	_field->setPlaceholderHidden(hasAnyItems);
	updateCursor();
	_iconOpacity.start([this] {
		rtlupdate(_st.padding.left(), _st.padding.top(), _st.fieldIcon.width(), _st.fieldIcon.height());
	}, hasAnyItems ? 1. : 0., hasAnyItems ? 0. : 1., _st.item.duration);
}

void MultiSelect::Inner::updateCursor() {
	setCursor(_items.empty() ? style::cur_text : (_overDelete ? style::cur_pointer : style::cur_default));
}

void MultiSelect::Inner::setActiveItem(int active, ChangeActiveWay skipSetFocus) {
	if (_active == active) return;

	if (_active >= 0) {
		t_assert(_active < _items.size());
		_items[_active]->setActive(false);
	}
	_active = active;
	if (_active >= 0) {
		t_assert(_active < _items.size());
		_items[_active]->setActive(true);
	}
	if (skipSetFocus != ChangeActiveWay::SkipSetFocus) {
		setInnerFocus();
	}
	if (_scrollCallback) {
		auto rect = (_active >= 0) ? _items[_active]->rect() : _field->geometry().translated(-_st.padding.left(), -_st.padding.top());
		_scrollCallback(rect.y(), rect.y() + rect.height() + _st.padding.top() + _st.padding.bottom());
	}
	update();
}

void MultiSelect::Inner::setActiveItemPrevious() {
	if (_active > 0) {
		setActiveItem(_active - 1);
	} else if (_active < 0 && !_items.empty()) {
		setActiveItem(_items.size() - 1);
	}
}

void MultiSelect::Inner::setActiveItemNext() {
	if (_active >= 0 && _active + 1 < _items.size()) {
		setActiveItem(_active + 1);
	} else {
		setActiveItem(-1);
	}
}

int MultiSelect::Inner::resizeGetHeight(int newWidth) {
	computeItemsGeometry(newWidth);
	updateFieldGeometry();

	auto cancelLeft = _fieldLeft + _fieldWidth + _st.padding.right() - _cancel->width();
	auto cancelTop = _fieldTop - _st.padding.top();
	_cancel->moveToLeft(_st.padding.left() + cancelLeft, _st.padding.top() + cancelTop);

	return _field->y() + _field->height() + _st.padding.bottom();
}

void MultiSelect::Inner::paintEvent(QPaintEvent *e) {
	Painter p(this);

	auto ms = getms();
	_height.step(ms);
	_iconOpacity.step(ms);

	auto paintRect = e->rect();
	p.fillRect(paintRect, st::windowBg);

	auto offset = QPoint(rtl() ? _st.padding.right() : _st.padding.left(), _st.padding.top());
	p.translate(offset);
	paintRect.translate(-offset);

	auto outerWidth = width() - _st.padding.left() - _st.padding.right();
	auto iconOpacity = _iconOpacity.current(_items.empty() ? 1. : 0.);
	if (iconOpacity > 0.) {
		p.setOpacity(iconOpacity);
		_st.fieldIcon.paint(p, 0, 0, outerWidth);
		p.setOpacity(1.);
	}

	auto checkRect = myrtlrect(paintRect);
	auto paintMargins = itemPaintMargins();
	for (auto i = _removingItems.begin(), e = _removingItems.end(); i != e;) {
		auto item = *i;
		auto itemRect = item->paintArea(outerWidth);
		itemRect = itemRect.marginsAdded(paintMargins);
		if (checkRect.intersects(itemRect)) {
			item->paint(p, outerWidth, ms);
		}
		if (item->hideFinished()) {
			i = _removingItems.erase(i);
			e = _removingItems.end();
		} else {
			++i;
		}
	}
	for_const (auto item, _items) {
		auto itemRect = item->paintArea(outerWidth);
		itemRect = itemRect.marginsAdded(paintMargins);
		if (checkRect.y() + checkRect.height() <= itemRect.y()) {
			break;
		} else if (checkRect.intersects(itemRect)) {
			item->paint(p, outerWidth, ms);
		}
	}
}

QMargins MultiSelect::Inner::itemPaintMargins() const {
	return {
		qMax(_st.itemSkip, _st.padding.left()),
		_st.itemSkip,
		qMax(_st.itemSkip, _st.padding.right()),
		_st.itemSkip,
	};
}

void MultiSelect::Inner::leaveEvent(QEvent *e) {
	clearSelection();
}

void MultiSelect::Inner::mouseMoveEvent(QMouseEvent *e) {
	updateSelection(e->pos());
}

void MultiSelect::Inner::keyPressEvent(QKeyEvent *e) {
	if (_active >= 0) {
		t_assert(_active < _items.size());
		if (e->key() == Qt::Key_Delete || e->key() == Qt::Key_Backspace) {
			auto itemId = _items[_active]->id();
			setActiveItemNext();
			removeItem(itemId);
		} else if (e->key() == Qt::Key_Left) {
			setActiveItemPrevious();
		} else if (e->key() == Qt::Key_Right) {
			setActiveItemNext();
		} else if (e->key() == Qt::Key_Escape) {
			setActiveItem(-1);
		} else {
			e->ignore();
		}
	} else if (e->key() == Qt::Key_Left || e->key() == Qt::Key_Backspace) {
		setActiveItemPrevious();
	} else {
		e->ignore();
	}
}

void MultiSelect::Inner::onFieldFocused() {
	setActiveItem(-1, ChangeActiveWay::SkipSetFocus);
}

void MultiSelect::Inner::updateSelection(QPoint mousePosition) {
	auto point = myrtlpoint(mousePosition) - QPoint(_st.padding.left(), _st.padding.right());
	auto selected = -1;
	for (auto i = 0, size = _items.size(); i != size; ++i) {
		auto itemRect = _items[i]->rect();
		if (itemRect.y() > point.y()) {
			break;
		} else if (itemRect.contains(point)) {
			point -= itemRect.topLeft();
			selected = i;
			break;
		}
	}
	if (_selected != selected) {
		if (_selected >= 0) {
			t_assert(_selected < _items.size());
			_items[_selected]->leaveEvent();
		}
		_selected = selected;
		update();
	}
	auto overDelete = false;
	if (_selected >= 0) {
		_items[_selected]->mouseMoveEvent(point);
		overDelete = _items[_selected]->isOverDelete();
	}
	if (_overDelete != overDelete) {
		_overDelete = overDelete;
		updateCursor();
	}
}

void MultiSelect::Inner::mousePressEvent(QMouseEvent *e) {
	if (_overDelete) {
		t_assert(_selected >= 0);
		t_assert(_selected < _items.size());
		removeItem(_items[_selected]->id());
	} else if (_selected >= 0) {
		setActiveItem(_selected);
	} else {
		setInnerFocus();
	}
}

void MultiSelect::Inner::addItem(std_::unique_ptr<Item> item, AddItemWay way) {
	auto wasEmpty = _items.empty();
	item->setUpdateCallback([this, item = item.get()] {
		auto itemRect = item->paintArea(width() - _st.padding.left() - _st.padding.top());
		itemRect = itemRect.translated(_st.padding.left(), _st.padding.top());
		itemRect = itemRect.marginsAdded(itemPaintMargins());
		rtlupdate(itemRect);
	});
	_items.push_back(item.release());
	updateItemsGeometry();
	if (wasEmpty) {
		updateHasAnyItems(true);
	}
	if (way != AddItemWay::SkipAnimation) {
		_items.back()->showAnimated();
	} else {
		_field->finishPlaceholderAnimation();
		finishHeightAnimation();
	}
}

void MultiSelect::Inner::computeItemsGeometry(int newWidth) {
	newWidth -= _st.padding.left() + _st.padding.right();

	auto itemLeft = 0;
	auto itemTop = 0;
	auto widthLeft = newWidth;
	auto maxVisiblePadding = qMax(_st.padding.left(), _st.padding.right());
	for_const (auto item, _items) {
		auto itemWidth = item->getWidth();
		t_assert(itemWidth <= newWidth);
		if (itemWidth > widthLeft) {
			itemLeft = 0;
			itemTop += _st.item.height + _st.itemSkip;
			widthLeft = newWidth;
		}
		item->setPosition(itemLeft, itemTop, newWidth, maxVisiblePadding);
		itemLeft += itemWidth + _st.itemSkip;
		widthLeft -= itemWidth + _st.itemSkip;
	}

	auto fieldMinWidth = _st.fieldMinWidth + _st.fieldCancelSkip;
	t_assert(fieldMinWidth <= newWidth);
	if (fieldMinWidth > widthLeft) {
		_fieldLeft = 0;
		_fieldTop = itemTop + _st.item.height + _st.itemSkip;
	} else {
		_fieldLeft = itemLeft + (_items.empty() ? _st.fieldIconSkip : 0);
		_fieldTop = itemTop;
	}
	_fieldWidth = newWidth - _fieldLeft;
}

void MultiSelect::Inner::updateItemsGeometry() {
	computeItemsGeometry(width());
	updateFieldGeometry();
	auto newHeight = resizeGetHeight(width());
	if (newHeight == _newHeight) return;

	_newHeight = newHeight;
	_height.start([this] { updateHeightStep(); }, height(), _newHeight, _st.item.duration);
}

void MultiSelect::Inner::updateHeightStep() {
	auto newHeight = _height.current(_newHeight);
	if (auto heightDelta = newHeight - height()) {
		resize(width(), newHeight);
		if (_resizedCallback) {
			_resizedCallback(heightDelta);
		}
		update();
	}
}

void MultiSelect::Inner::finishHeightAnimation() {
	_height.finish();
	updateHeightStep();
}

void MultiSelect::Inner::setItemText(uint64 itemId, const QString &text) {
	for (int i = 0, count = _items.size(); i != count; ++i) {
		auto item = _items[i];
		if (item->id() == itemId) {
			item->setText(text);
			updateItemsGeometry();
			return;
		}
	}
}

void MultiSelect::Inner::setItemRemovedCallback(base::lambda_unique<void(uint64 itemId)> callback) {
	_itemRemovedCallback = std_::move(callback);
}

void MultiSelect::Inner::setResizedCallback(base::lambda_unique<void(int heightDelta)> callback) {
	_resizedCallback = std_::move(callback);
}

void MultiSelect::Inner::removeItem(uint64 itemId) {
	for (int i = 0, count = _items.size(); i != count; ++i) {
		auto item = _items[i];
		if (item->id() == itemId) {
			clearSelection();
			_items.removeAt(i);
			if (_active == i) {
				_active = -1;
			} else if (_active > i) {
				--_active;
			}
			_removingItems.insert(item);
			item->hideAnimated();

			updateItemsGeometry();
			if (_items.empty()) {
				updateHasAnyItems(false);
			}
			auto point = QCursor::pos();
			if (auto parent = parentWidget()) {
				if (parent->rect().contains(parent->mapFromGlobal(point))) {
					updateSelection(mapFromGlobal(point));
				}
			}
			break;
		}
	}
	if (_itemRemovedCallback) {
		_itemRemovedCallback(itemId);
	}
	setInnerFocus();
}

MultiSelect::Inner::~Inner() {
	for (auto item : base::take(_items)) {
		delete item;
	}
	for (auto item : base::take(_removingItems)) {
		delete item;
	}
}

} // namespace Ui
