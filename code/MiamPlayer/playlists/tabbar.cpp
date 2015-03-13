#include "tabbar.h"
#include "settingsprivate.h"
#include "playlist.h"
#include "../treeview.h"

#include <QtDebug>

#include <QIcon>

TabBar::TabBar(TabPlaylist *parent) :
	QTabBar(parent), lineEdit(new QLineEdit(this)), tabPlaylist(parent)
{
	this->setTabsClosable(true);
	this->setAcceptDrops(true);
	this->setDocumentMode(true);
	this->setUsesScrollButtons(true);

	lineEdit->setVisible(false);
	lineEdit->setAlignment(Qt::AlignCenter);
	lineEdit->setFrame(false);
	lineEdit->installEventFilter(this);
	lineEdit->setAttribute(Qt::WA_MacShowFocusRect, false);

	_timer = new QTimer(this);
	_timer->setInterval(300);
	_timer->setSingleShot(true);

	connect(lineEdit, &QLineEdit::returnPressed, this, &TabBar::renameTab);
	// Switch between tabs if the Drag & Drop isn't finished
	connect(_timer, &QTimer::timeout, [=]() {
		this->setCurrentIndex(tabAt(_targetRect.center()));
		_targetRect = tabRect(currentIndex());
	});

	connect(SettingsPrivate::instance(), &SettingsPrivate::fontHasChanged, [=](SettingsPrivate::FontFamily ff, const QFont &newFont) {
		if (ff == SettingsPrivate::FF_Playlist) {
			QFont font = newFont;
			font.setPointSizeF(font.pointSizeF() * 0.8);
			this->setFont(font);
			int h = fontMetrics().height() * 1.25;
			if (h >= 30) {
				this->setMinimumHeight(h);
				this->setMaximumHeight(h);
			} else {
				this->setMinimumHeight(30);
				this->setMaximumHeight(30);
			}
		}
	});
}

/** Trigger a double click to rename a tab. */
void TabBar::editTab(int indexTab)
{
	QMouseEvent dbClickEvent(QEvent::MouseButtonDblClick, tabRect(indexTab).center(), Qt::LeftButton, 0, 0);
	QApplication::sendEvent(this, &dbClickEvent);
}

/** Redefined to accept D&D from another playlist or the library. */
void TabBar::dragEnterEvent(QDragEnterEvent *event)
{
	int idx = tabAt(event->pos());
	if (idx < count() - 1) {
		_targetRect = tabRect(idx);
	} else {
		_targetRect = QRect();
	}
	event->source() ? event->acceptProposedAction() : event->ignore();
}

/** Redefined to accept D&D from another playlist or the library. */
void TabBar::dragMoveEvent(QDragMoveEvent *event)
{
	int idx = tabAt(event->pos());
	// Exclude current tab and last one
	if (idx < count() - 1) {
		_targetRect = tabRect(idx);
	}
	Playlist *playlist = qobject_cast<Playlist*>(event->source());
	if (playlist) {
		// Exclude current tab and last one?
		if (idx < count() - 1) {
			_timer->start();
		}
	}
	event->source() ? event->acceptProposedAction() : event->ignore();
}

/** Redefined to accept D&D from another playlist or the library. */
void TabBar::dropEvent(QDropEvent *event)
{
	int tab = this->tabAt(event->pos());
	if (Playlist *origin = qobject_cast<Playlist*>(event->source())) {
		Playlist *target = tabPlaylist->playlist(tab);

		// Copy tracks in the target
		QList<QMediaContent> medias;
		foreach (QPersistentModelIndex index, origin->selectionModel()->selectedRows()) {
			medias.append(origin->mediaPlaylist()->media(index.row()));
		}
		// Append tracks at the end
		target->insertMedias(target->model()->rowCount(), medias);

		// Remove tracks from the current playlist if necessary
		if (!SettingsPrivate::instance()->copyTracksFromPlaylist()) {
			origin->removeSelectedTracks();
		}
	} else if (TreeView *origin = qobject_cast<TreeView*>(event->source())) {
		tabPlaylist->setCurrentIndex(tab);
		origin->appendToPlaylist();
	}
}

/** Redefined to validate new tab name if the focus is lost. */
bool TabBar::eventFilter(QObject *obj, QEvent *event)
{
	// Accept the escape key
	if (obj == lineEdit && event->type() == QEvent::KeyPress) {
		QKeyEvent *keyEvent = static_cast<QKeyEvent *>(event);
		if (keyEvent->key() == Qt::Key_Escape) {
			lineEdit->releaseMouse();
			lineEdit->close();
		}
	} else if (obj == lineEdit && event->type() == QEvent::FocusOut) {
		lineEdit->releaseMouse();
		renameTab();
	} else if (obj == lineEdit && event->type() == QEvent::MouseButtonPress) {
		QMouseEvent *me = static_cast<QMouseEvent*>(event);
		if (!lineEdit->rect().contains(me->pos())) {
			lineEdit->releaseMouse();
			lineEdit->close();
		}
	}
	return false;
}

/** Redefined to display an editable area. */
void TabBar::mouseDoubleClickEvent(QMouseEvent *event)
{
	int tabIndex = tabAt(event->pos());
	int c = currentIndex();
	if (-1 < tabIndex && tabIndex < count() && c == tabIndex) {
		QRect visualRect = tabRect(tabIndex);
		SettingsPrivate *settings = SettingsPrivate::instance();
		if (settings->isRectTabs()) {
			visualRect.setLeft(visualRect.left() + 1);
			visualRect.setRight(visualRect.right() - 1);
		} else {
			visualRect.setLeft(visualRect.left() + 3 + settings->tabsOverlappingLength() * 1.25 + height() / 2);
			visualRect.setRight(visualRect.right() - settings->tabsOverlappingLength());
		}
		visualRect.setTop(visualRect.top() + 1);

		// Disable close buttons in case of unfortunate click
		for (int t = 0; t < count(); t++) {
			QWidget *button = tabButton(t, QTabBar::RightSide);
			if (button && t != tabIndex) {
				button->setEnabled(false);
			}
		}

		// Feed the QLineEdit with current tab text
		lineEdit->setText(tabText(tabIndex));
		lineEdit->selectAll();
		lineEdit->setFocus();
		lineEdit->setGeometry(visualRect);
		lineEdit->setVisible(true);
		lineEdit->grabMouse();
	}
	QTabBar::mouseDoubleClickEvent(event);
}

/** Redefined to validate new tab name without pressing return. */
void TabBar::mousePressEvent(QMouseEvent *event)
{
	int tabIndex = tabAt(event->pos());
	if (lineEdit->isVisible() && ((currentIndex() != tabIndex && lineEdit->isVisible()) || !lineEdit->geometry().contains(event->pos()))) {
		lineEdit->close();
	} else {
		QTabBar::mousePressEvent(event);
	}
}

void TabBar::paintEvent(QPaintEvent *)
{
	SettingsPrivate *settings = SettingsPrivate::instance();
	QStylePainter p(this);
	int dist = 0;
	if (settings->isRectTabs()) {
		paintRectTabs(p);
	} else {
		dist = settings->tabsOverlappingLength();
		paintRoundedTabs(p, dist);
	}

	// Global bottom frame border
	int h = tabRect(0).height() - 1;
	p.setPen(QApplication::palette().mid().color());
	if (count() > 1) {
		if (currentIndex() == 0) {
			p.drawLine(tabRect(0).right() + 1 + dist, h, rect().right(), h);
		} else {
			p.drawLine(rect().left(), h, tabRect(currentIndex()).left(), h);
			p.drawLine(tabRect(currentIndex()).right() + 1 + dist, h, rect().right(), h);
		}
	} else {
		if (isLeftToRight()) {
			p.drawLine(tabRect(0).width() + dist, h, rect().right(), h);
		} else {
			p.drawLine(rect().left(), h, tabRect(0).left(), h);
		}
	}
}

/** Redefined to return a square for the last tab which is the [+] button. */
QSize TabBar::tabSizeHint(int index) const
{
	QSize s = QTabBar::tabSizeHint(index);
	if (!SettingsPrivate::instance()->isRectTabs()) {
		s.setWidth(s.width() + SettingsPrivate::instance()->tabsOverlappingLength() * 2);
	}
	// Adjust height to the minimum otherwise a small gap might appear between tab and header (depending of font size)
	if (s.height() > height()) {
		s.setHeight(height());
	}
	qDebug() << sizeHint();
	return s;
}

void TabBar::paintRectTabs(QStylePainter &p)
{
	static const qreal penScaleFactor = 0.2;

	for (int i = 0; i < count(); i++) {
		QStyleOptionTab o;
		initStyleOption(&o, i);

		// Background color
		p.save();
		if (i != currentIndex()) {
			o.rect.adjust(0, 2, 0, 0);
		} else if (i == count()) {
			o.rect.adjust(2, 2, -4, -4);
		}

		// Highlight the tab under the cursor
		if (o.state.testFlag(QStyle::State_MouseOver) && i != currentIndex()) {
			p.setPen(QPen(o.palette.highlight(), penScaleFactor));
			p.fillRect(o.rect, o.palette.highlight().color().lighter());
		} else {
			p.setPen(QPen(o.palette.mid(), penScaleFactor));
			if (i == currentIndex()) {
				/// XXX
				if (SettingsPrivate::instance()->isCustomColors()) {
					p.fillRect(o.rect, o.palette.base().color().lighter(110));
				} else {
					p.fillRect(o.rect, o.palette.base());
				}
			} else {
				p.fillRect(o.rect, o.palette.window());
			}
		}

		if (o.state.testFlag(QStyle::State_MouseOver)) {
			p.setPen(o.palette.highlight().color());
		} else {
			p.setPen(o.palette.mid().color());
		}
		// Frame tab, it is not a rectangle but only 3 lines
		p.drawLine(o.rect.topLeft(), o.rect.bottomLeft());
		p.drawLine(o.rect.topRight(), o.rect.bottomRight());
		p.drawLine(o.rect.topLeft(), o.rect.topRight());
		//}
		p.restore();

		// Icon
		QRect r = tabRect(i);
		r.setHeight(fontMetrics().ascent());
		r.translate(10, (height() - r.height()) / 2);
		r.setWidth(r.height() / 2);
		p.setRenderHint(QPainter::SmoothPixmapTransform);
		o.icon.paint(&p, r, Qt::AlignLeft | Qt::AlignVCenter);

		// Playlist name
		if (i == currentIndex()) {
			p.setPen(o.palette.windowText().color());
		} else if (o.state.testFlag(QStyle::State_MouseOver)) {
			p.setPen(o.palette.windowText().color());
		} else {
			p.setPen(o.palette.mid().color());
		}
		//o.rect.adjust(r.x() + r.width() + 10, 0, 0, 0);
		QRect rText(r.x() + r.width() + 10, r.y(), o.rect.width() - r.width() - 10, r.height());
		p.drawText(rText, Qt::AlignLeft | Qt::AlignVCenter, o.text);
	}
}

void TabBar::paintRoundedTabs(QStylePainter &p, int dist)
{
	// Draw all tabs before the selected tab
	QList<int> tabs;
	for (int i = 0; i < count(); i++)
		if (currentIndex() != i) tabs.append(i);
	tabs.append(currentIndex());

	for (int idx = 0; idx < count(); idx++) {
		int i = tabs.at(idx);
		QStyleOptionTab o;
		initStyleOption(&o, i);

		// Background color
		if (i != currentIndex()) {
			o.rect.adjust(0, 2, 0, 0);
		} else if (i == count()) {
			o.rect.adjust(2, 2, -4, -4);
		}

		/// Adjust parameters to tighten tabs
		//o.rect.adjust(-dist / 2, 0, dist / 2, 0);

		// Rounded frame tab
		QPainterPath pp;
		pp.moveTo(o.rect.x() + dist * 0, o.rect.y() + o.rect.height());
		pp.cubicTo(o.rect.x() + dist * 1, o.rect.y() + o.rect.height(),
				   o.rect.x() + dist * 1, o.rect.y(),
				   o.rect.x() + dist * 2, o.rect.y());
		pp.lineTo(o.rect.x() + o.rect.width() - dist * 1, o.rect.y());
		pp.cubicTo(o.rect.x() + o.rect.width() - dist * 0, o.rect.y(),
				   o.rect.x() + o.rect.width() - dist * 0, o.rect.y() + o.rect.height(),
				   o.rect.x() + o.rect.width() + dist * 1, o.rect.y() + o.rect.height());

		p.save();
		if (o.state.testFlag(QStyle::State_MouseOver)) {
			p.setPen(o.palette.highlight().color());
		} else {
			p.setPen(o.palette.mid().color());
		}
		if (i == currentIndex()) {
			/// XXX
			if (SettingsPrivate::instance()->isCustomColors()) {
				p.fillPath(pp, o.palette.base().color().lighter(110));
			} else {
				p.fillPath(pp, o.palette.base());
			}
		} else if (o.state.testFlag(QStyle::State_MouseOver)) {
			p.fillPath(pp, o.palette.highlight().color().lighter(170));
		} else {
			p.fillPath(pp, o.palette.window());
		}
		p.setRenderHint(QPainter::Antialiasing, true);
		p.drawPath(pp);
		p.setRenderHint(QPainter::Antialiasing, false);
		p.restore();

		// Icon
		QRect r = tabRect(i);
		r.setHeight(fontMetrics().ascent());
		r.translate(3 + dist * 1.25, (height() - r.height()) / 2);
		r.setWidth(r.height() / 2);
		p.setRenderHint(QPainter::SmoothPixmapTransform);
		o.icon.paint(&p, r, Qt::AlignLeft | Qt::AlignVCenter);

		// Playlist name
		if (i == currentIndex()) {
			p.setPen(o.palette.windowText().color());
		} else if (o.state.testFlag(QStyle::State_MouseOver)) {
			p.setPen(o.palette.windowText().color());
		} else {
			p.setPen(o.palette.mid().color());
		}
		QRect rText(r.x() + r.width() + 5, this->rect().y(),
					o.rect.width() - (r.width() + 5), this->height() - 2);
		p.drawText(rText, Qt::AlignLeft | Qt::AlignVCenter, o.text);
	}
}

/** Rename a tab. */
void TabBar::renameTab()
{
	this->setTabText(currentIndex(), lineEdit->text());
	lineEdit->close();
	for (int t = 0; t < count(); t++) {
		QWidget *button = tabButton(t, QTabBar::RightSide);
		if (button) {
			button->setEnabled(true);
		}
	}
}
