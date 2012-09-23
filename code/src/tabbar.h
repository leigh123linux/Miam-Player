#ifndef TABBAR_H
#define TABBAR_H

#include <QLineEdit>
#include <QMouseEvent>
#include <QTabBar>

#include "playlist.h"
#include "tabplaylist.h"

class TabBar : public QTabBar
{
	Q_OBJECT
private:
	QLineEdit *lineEdit;

	TabPlaylist *tabPlaylist;

public:
	TabBar(TabPlaylist *parent);

	/** Redefined to validate new tab name if the focus is lost. */
	bool eventFilter(QObject *, QEvent *);

protected:
	void dropEvent(QDropEvent *event);

	void dragEnterEvent(QDragEnterEvent *event);

	void dragMoveEvent(QDragMoveEvent *event);

	/** Redefined to display an editable area. */
	void mouseDoubleClickEvent(QMouseEvent *);

	/** Redefined to validate new tab name without pressing return. */
	void mousePressEvent(QMouseEvent *);

private slots:
	/** Rename a tab. */
	void renameTab();
};

#endif // TABBAR_H
