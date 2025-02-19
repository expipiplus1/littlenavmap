/*****************************************************************************
* Copyright 2015-2020 Alexander Barthel alex@littlenavmap.org
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

#ifndef LNM_MAPMARKHANDLER_H
#define LNM_MAPMARKHANDLER_H
#include "common/mapflags.h"

#include <QObject>

class QAction;
class QToolButton;

/*
 * Adds a toolbutton and four actions to it that allow to show or hide user features like holds, range rings and others.
 */
class MapMarkHandler :
  public QObject
{
  Q_OBJECT

public:
  explicit MapMarkHandler(QWidget *parent);
  virtual ~MapMarkHandler() override;

  MapMarkHandler(const MapMarkHandler& other) = delete;
  MapMarkHandler& operator=(const MapMarkHandler& other) = delete;

  /* Initialize, create actions and add button to toolbar */
  void addToolbarButton();

  void saveState();
  void restoreState();

  map::MapTypes getMarkTypes() const
  {
    return markTypes;
  }

  bool isShown(map::MapTypes type) const
  {
    return markTypes & type;
  }

  void showMarkTypes(map::MapTypes types);

  QStringList getMarkTypesText() const;

  void resetSettingsToDefault();

signals:
  /* Redraw map */
  void updateMarkTypes(map::MapTypes types);

private:
  void toolbarActionTriggered();
  void actionAllTriggered();
  void actionNoneTriggered();

  void flagsToActions();
  void actionsToFlags();
  QAction *addButton(const QString& icon, const QString& text, const QString& tooltip);

  /* Actions for toolbar button and menu */
  QAction *actionAll = nullptr, *actionNone = nullptr, *actionRangeRings = nullptr, *actionMeasurementLines = nullptr,
          *actionHolds = nullptr, *actionAirportMsa = nullptr, *actionPatterns = nullptr;

  /* Toolbutton getting all actions for dropdown menu */
  QToolButton *toolButton = nullptr;
  map::MapTypes markTypes = map::MARK_ALL;
};

#endif // LNM_MAPMARKHANDLER_H
