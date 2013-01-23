#include "tageditor.h"
#include "filehelper.h"
#include "library/libraryitem.h"
#include "settings.h"

#include <tpropertymap.h>
#include <MediaSource>
#include <QDir>

#include <QtDebug>

using namespace TagLib;

QStringList TagEditor::genres = (QStringList() << "Blues" << "Classic Rock" << "Country" << "Dance" << "Disco" << "Funk" << "Grunge" << "Hip-Hop" << "Jazz"
	<< "Metal" << "New Age" << "Oldies" << "Other" << "Pop" << "R&B" << "Rap" << "Reggae" << "Rock" << "Techno"
	<< "Industrial" << "Alternative" << "Ska" << "Death Metal" << "Pranks" << "Soundtrack" << "Euro-Techno" << "Ambient"
	<< "Trip-Hop" << "Vocal" << "Jazz+Funk" << "Fusion" << "Trance" << "Classical" << "Instrumental" << "Acid" << "House"
	<< "Game" << "Sound Clip" << "Gospel" << "Noise" << "AlternRock" << "Bass" << "Soul" << "Punk" << "Space" << "Meditative"
	<< "Instrumental Pop" << "Instrumental Rock" << "Ethnic" << "Gothic" << "Darkwave" << "Techno-Industrial" << "Electronic"
	<< "Pop-Folk" << "Eurodance" << "Dream" << "Southern Rock" << "Comedy" << "Cult" << "Gangsta" << "Top 40"
	<< "Christian Rap" << "Pop/Funk" << "Jungle" << "Native American" << "Cabaret" << "New Wave" << "Psychadelic" << "Rave"
	<< "Showtunes" << "Trailer" << "Lo-Fi" << "Tribal" << "Acid Punk" << "Acid Jazz" << "Polka" << "Retro" << "Musical"
	<< "Rock & Roll" << "Hard Rock");

TagEditor::TagEditor(QWidget *parent) :
	QWidget(parent), atLeastOneItemChanged(false)
{
	setupUi(this);

	tagConverter = new TagConverter(this);

	int i = 1;
	combos.insert(++i, titleComboBox);
	combos.insert(++i, artistComboBox);
	combos.insert(++i, artistAlbumComboBox);
	combos.insert(++i, albumComboBox);
	combos.insert(++i, trackComboBox);
	combos.insert(++i, discComboBox);
	combos.insert(++i, yearComboBox);
	combos.insert(++i, genreComboBox);
	combos.insert(++i, commentComboBox);

	foreach (QComboBox *combo, combos.values()) {
		combo->addItem(tr("(Keep)"));
		combo->addItem(tr("(Delete)"));
		combo->setCurrentIndex(-1);
	}
	// Quit this widget when a request was send from this button
	connect(closeTagEditorButton, SIGNAL(clicked()), this, SLOT(close()));

	connect(saveChangesButton, SIGNAL(clicked()), this, SLOT(commitChanges()));
	connect(cancelButton, SIGNAL(clicked()), this, SLOT(rollbackChanges()));

	// General case: when one is selecting multiple items
	connect(tagEditorWidget, SIGNAL(itemSelectionChanged()), this, SLOT(displayTagsAndCover()));

	// Open the TagConverter to help tagging from Tag to File, or vice-versa
	connect(convertPushButton, SIGNAL(toggled(bool)), this, SLOT(toggleTagConverter(bool)));

	connect(albumCover, SIGNAL(aboutToRemoveCoverFromTag()), this, SLOT(removeCoverFromTag()));
	connect(albumCover, SIGNAL(aboutToApplyCoverToAll(bool)), this, SLOT(applyCoverToAll(bool)));

	albumCover->installEventFilter(this);
}

/** Redefined to filter context menu event for the cover album object. */
bool TagEditor::eventFilter(QObject *obj, QEvent *event)
{
	if (obj == albumCover && event->type() == QEvent::ContextMenu) {
		return tagEditorWidget->selectedItems().isEmpty();
	} else {
		return QWidget::eventFilter(obj, event);
	}
}

/** Splits tracks into columns to be able to edit metadatas. */
void TagEditor::addItemsToEditor(const QList<QPersistentModelIndex> &indexes)
{
	this->clear();
	saveChangesButton->setEnabled(false);
	cancelButton->setEnabled(false);

	// It's possible to edit single items by double-clicking in the table
	// So, temporarily disconnect this signal
	disconnect(tagEditorWidget, SIGNAL(itemChanged(QTableWidgetItem*)), this, SLOT(recordSingleItemChange(QTableWidgetItem*)));
	bool onlyOneAlbumIsSelected = tagEditorWidget->addItemsToEditor(indexes);
	albumCover->setCoverForUniqueAlbum(onlyOneAlbumIsSelected);
	connect(tagEditorWidget, SIGNAL(itemChanged(QTableWidgetItem*)), this, SLOT(recordSingleItemChange(QTableWidgetItem*)));

	// Sort by path
	tagEditorWidget->setSortingEnabled(true);
	tagEditorWidget->sortItems(0);
	tagEditorWidget->sortItems(1);
	tagEditorWidget->resizeColumnsToContents();
}

/** Clears all rows and comboboxes. */
void TagEditor::clear()
{
	// Reset the cover
	albumCover->resetCover();

	// Delete text contents, not the combobox itself
	foreach (QComboBox *combo, combos.values()) {
		combo->clear();
	}
	tagEditorWidget->clear();
}

void TagEditor::removeCoverFromTag()
{
	foreach (QModelIndex index, tagEditorWidget->selectionModel()->selectedIndexes()) {
		if (index.column() == TagEditorTableWidget::COVER_COL) {
			QTableWidgetItem *item = tagEditorWidget->item(index.row(), index.column());
			item->setData(Qt::EditRole, QVariant());
			item->setData(TagEditorTableWidget::MODIFIED, true);
		}
	}
	saveChangesButton->setEnabled(true);
}

void TagEditor::applyCoverToAll(bool isAll)
{
	// Checks if there's only one album selected by the user
	QList<QTableWidgetItem*> list = tagEditorWidget->selectedItems();
	QSet<QString> albums;
	foreach (QTableWidgetItem *item, list) {
		if (item->column() == TagEditorTableWidget::ALBUM_COL) {
			albums.insert(item->text());
		}
	}
	if (albums.size() != 1) {

	}
	qDebug() << "applyCoverToAll" << isAll;
}

/** Closes this Widget and tells its parent to switch views. */
void TagEditor::close()
{
	emit closeTagEditor(false);
	saveChangesButton->setEnabled(false);
	cancelButton->setEnabled(false);
	atLeastOneItemChanged = false;
	this->clear();
	QWidget::close();
}

/** Saves all fields in the media. */
void TagEditor::commitChanges()
{
	// Create a subset of all modified tracks that needs to be rescanned by the model afterwards.
	QList<QPersistentModelIndex> tracksToRescan;

	// Detect changes
	for (int i = 0; i < tagEditorWidget->rowCount(); i++) {
		// A physical and unique file per row
		QTableWidgetItem *itemFileName = tagEditorWidget->item(i, 0);
		QTableWidgetItem *itemPath = tagEditorWidget->item(i, 1);
		QString absPath(itemPath->text() + QDir::separator() + itemFileName->text());
		FileRef file(QFile::encodeName(absPath).data());
		FileHelper fh(file, itemFileName->data(LibraryItem::SUFFIX).toInt());
		bool trackWasModified = false;
		for (int j = 2; j < tagEditorWidget->columnCount(); j++) {

			// Check for every field if we have any changes
			QTableWidgetItem *item = tagEditorWidget->item(i, j);

			if (item && item->data(TagEditorTableWidget::MODIFIED).toBool()) {

				// Replace the field by using a key stored in the header (one key per column)
				QString key = tagEditorWidget->horizontalHeaderItem(j)->data(TagEditorTableWidget::KEY).toString();
				//qDebug() << key;
				if (file.tag()) {
					PropertyMap pm = file.tag()->properties();

					// The map doesn't always contain all keys, like ArtistAlbum (not standard)
					if (pm.contains(key.toStdString())) {
						bool b = pm.replace(key.toStdString(), String(item->text().toStdString()));
						if (b) {
							file.tag()->setProperties(pm);
							if (file.tag()) {
								trackWasModified = true;
							}
						}
					} else {
						qDebug() << key << item->text();
						trackWasModified = fh.insert(key, item->text());
					}
				} else {
					qDebug() << "no valid tag for this file";
				}
			}
		}
		// Save changes if at least one field was modified
		// Also, tell the model to rescan the file because the artist or the album might have changed:
		// The Tree structure could be modified
		if (trackWasModified) {
			bool b = fh.save();
			if (!b) {
				qDebug() << "tag wasn't saved :(";
			}
			tracksToRescan.append(tagEditorWidget->index(absPath));
		}
	}

	if (!tracksToRescan.isEmpty()) {
		emit rebuildTreeView(tracksToRescan);
	}

	// Reset buttons state
	saveChangesButton->setEnabled(false);
	cancelButton->setEnabled(false);
	atLeastOneItemChanged = false;
}

/** Display a cover only if the selected items have exactly the same cover. */
void TagEditor::displayCover()
{
	QMap<QByteArray, QString> covers;
	// The last column is hidden, so we have to go through the selectionModel().
	foreach (QModelIndex item, tagEditorWidget->selectionModel()->selectedRows(TagEditorTableWidget::COVER_COL)) {
		// For the last column, use QPixmap
		if (!item.data(Qt::EditRole).toByteArray().isEmpty()) {
			covers.insert(item.data(Qt::EditRole).toByteArray(),
						  tagEditorWidget->item(item.row(), TagEditorTableWidget::ALBUM_COL)->text());
		}
	}

	// Void items are excluded, so it will display something (if a cover is missing, for example).
	// Beware: if a cover is shared between multiple albums, only the first album name will appear in the context menu.
	if (covers.size() == 1) {
		albumCover->displayFromAttachedPicture(covers.keys().first(), covers.values().first());
	} else {
		albumCover->resetCover();
	}
}

/** Displays tags in separate QComboBoxes. */
void TagEditor::displayTagsAndCover()
{
	this->displayCover();

	// Information in the table is split into columns, using column index
	QMap<int, QStringList> datas;
	foreach (QTableWidgetItem *item, tagEditorWidget->selectedItems()) {
		QStringList stringList = datas.value(item->column());
		stringList << item->text();
		datas.insert(item->column(), stringList);
	}

	// To avoid redondancy, overwrite data for the same key
	QMapIterator<int, QStringList> it(datas);
	while (it.hasNext()) {
		it.next();
		QStringList stringList = datas.value(it.key());
		stringList.removeDuplicates();

		// Beware: there are no comboBox for every column in the edit area below the table
		QComboBox *combo = combos.value(it.key());
		if (combo) {

			disconnect(combo, SIGNAL(editTextChanged(QString)), this, SLOT(updateCells(QString)));
			combo->clear();
			combo->insertItem(0, tr("(Keep)"));
			// Map the combobox object with the number of the column in the table to dynamically reflect changes
			// Arbitrarily adds the column number to the first item (Keep)
			combo->setItemData(0, combos.key(combo), Qt::UserRole+1);
			combo->insertItem(1, tr("(Delete)"));

			// Special case for Genre, it's better to have them all in the combobox
			if (combo == genreComboBox) {
				combo->addItems(genres);
				foreach (QString genre, stringList) {
					if (!genres.contains(genre)) {
						combo->addItem(genre);
					}
				}
			} else {
				combo->addItems(stringList);
			}

			// Special case for Tracknumber and Year: it's better to have a numerical order
			if (combo == genreComboBox || combo == trackComboBox || combo == yearComboBox) {
				combo->model()->sort(0);
			}

			// No item: nothing is selected
			// 1 item: select this item
			// 2 or more: select (Keep)
			if (stringList.isEmpty()) {
				combo->setCurrentIndex(-1);
			} else if (stringList.count() == 1) {
				if (combo == genreComboBox || combo == trackComboBox || combo == yearComboBox) {
					int result = combo->findText(stringList.first());
					combo->setCurrentIndex(result);
				} else {
					combo->setCurrentIndex(2);
				}
			} else {
				combo->setCurrentIndex(0);
			}
			connect(combo, SIGNAL(editTextChanged(QString)), this, SLOT(updateCells(QString)));
		}
	}
}

void TagEditor::recordSingleItemChange(QTableWidgetItem *item)
{
	saveChangesButton->setEnabled(true);
	cancelButton->setEnabled(true);
	item->setData(TagEditorTableWidget::MODIFIED, true);
}

/** Cancels all changes made by the user. */
void TagEditor::rollbackChanges()
{
	tagEditorWidget->resetTable();
	saveChangesButton->setEnabled(false);
	cancelButton->setEnabled(false);
	atLeastOneItemChanged = false;

	// Then, reload info a second time
	this->displayTagsAndCover();
}

void TagEditor::toggleTagConverter(bool b)
{
	QPoint p = mapToGlobal(convertPushButton->pos());
	p.setX(p.x() - (tagConverter->width() - convertPushButton->width()) / 2);
	p.setY(p.y() + convertPushButton->height() + 5);
	tagConverter->move(p);
	tagConverter->setVisible(b);
}

/** When one is changing a field, updates all rows in the table (the Artist for example). */
void TagEditor::updateCells(QString text)
{
	QComboBox *combo = findChild<QComboBox*>(sender()->objectName());
	QVariant v = combo->itemData(0, Qt::UserRole+1);
	int idxColumnInTable = v.toInt();

	disconnect(combo, SIGNAL(editTextChanged(QString)), this, SLOT(updateCells(QString)));

	// Special behaviour for "Keep" and "Delete" items
	// Keep
	if (combo->currentIndex() == 0) {

		QModelIndexList list = tagEditorWidget->selectionModel()->selectedRows(idxColumnInTable);
		for (int i = 0; i < list.size(); i++) {
			QModelIndex index = list.at(i);
			// Fill the table with one item per combobox
			if (combo == titleComboBox || combo == trackComboBox) {
				tagEditorWidget->item(index.row(), idxColumnInTable)->setText(combo->itemText(2 + i));
			// For unique attribute like "Artist" or "year" copy-paste this item to every cells in the table
			} else if (combo == artistComboBox || combo == artistAlbumComboBox || combo == albumComboBox || combo == yearComboBox
					   || combo == discComboBox || combo == genreComboBox || combo == commentComboBox) {
				tagEditorWidget->item(index.row(), idxColumnInTable)->setText(combo->itemText(2));
			}
			tagEditorWidget->item(index.row(), idxColumnInTable)->setData(TagEditorTableWidget::MODIFIED, true);
		}

	// Delete
	} else if (combo->currentIndex() == 1) {
		tagEditorWidget->updateColumnData(idxColumnInTable, "");
	// A regular item
	} else {
		tagEditorWidget->updateColumnData(idxColumnInTable, text);
	}
	saveChangesButton->setEnabled(true);
	cancelButton->setEnabled(true);

	connect(combo, SIGNAL(editTextChanged(QString)), this, SLOT(updateCells(QString)));
}
