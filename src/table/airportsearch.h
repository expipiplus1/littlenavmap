/*****************************************************************************
* Copyright 2015-2016 Alexander Barthel albar965@mailbox.org
*
* This program is free software: you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation, either version 3 of the License, or
* (at your option) any later version.
*
* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
* GNU General Public License for more details.
*
* You should have received a copy of the GNU General Public License
* along with this program.  If not, see <http://www.gnu.org/licenses/>.
*****************************************************************************/

#ifndef APSEARCHPANE_H
#define APSEARCHPANE_H

#include "geo/pos.h"

#include "table/search.h"

#include <QObject>

class QWidget;
class QTableView;
class Controller;
class ColumnList;
class QAction;
class MainWindow;
class Column;
class QSqlRecord;
class AirportIconDelegate;

namespace atools {
namespace sql {
class SqlDatabase;
}
}

class AirportSearch :
  public Search
{
  Q_OBJECT

public:
  explicit AirportSearch(MainWindow *parent, QTableView *tableView, ColumnList *columnList,
                         atools::sql::SqlDatabase *sqlDb, int tabWidgetIndex);
  virtual ~AirportSearch();

  virtual void saveState() override;
  virtual void restoreState() override;
  virtual void getSelectedMapObjects(maptypes::MapSearchResult& result) const override;

  virtual void connectSlots() override;

private:
  QVariant modelDataHandler(int colIndex, int rowIndex, const Column *col, const QVariant& value,
                            const QVariant& dataValue, Qt::ItemDataRole role) const;
  QString modelFormatHandler(const Column *col, const QVariant& value, const QVariant& dataValue) const;

  QList<QObject *> airportSearchWidgets;
  QList<QAction *> airportSearchMenuActions;

  static const QStringList ratings;
  static const QSet<QString> boolColumns;
  static const QSet<QString> numberColumns;

  void fillSearchResult(const QSqlRecord& record, maptypes::MapSearchResult *result) const;

  AirportIconDelegate *iconDelegate = nullptr;

};

#endif // APSEARCHPANE_H
