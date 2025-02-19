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

#ifndef LITTLENAVMAP_PROFILEWIDGET_H
#define LITTLENAVMAP_PROFILEWIDGET_H

#include "fs/sc/simconnectdata.h"

#include <QFutureWatcher>
#include <QWidget>

namespace atools {
namespace geo {
class LineString;
}
namespace fs {
namespace perf {
class AircraftPerf;
}
}
}

namespace Marble {
class ElevationModel;
class GeoDataLineString;
}

class RouteController;
class QTimer;
class QRubberBand;
class ProfileScrollArea;
class JumpBack;
class Route;
class RouteLeg;
struct ElevationLegList;

/*
 * Loads and displays the flight plan elevation profile. The elevation data is
 * calculated in a background thread that is triggered when new elevation data
 * arrives from the Marble widget.
 *
 * This widget is the full drawing area that is covered by the scroll view.
 */
class ProfileWidget :
  public QWidget
{
  Q_OBJECT

public:
  ProfileWidget(QWidget *parent);
  virtual ~ProfileWidget() override;

  ProfileWidget(const ProfileWidget& other) = delete;
  ProfileWidget& operator=(const ProfileWidget& other) = delete;

  /* If geometry has changed the elevation calculation is started after a short delay */
  void windUpdated();
  void routeChanged(bool geometryChanged, bool newFlightPlan);
  void routeAltitudeChanged(int altitudeFeet);

  /* Update user aircraft on profile display */
  void simDataChanged(const atools::fs::sc::SimConnectData& simulatorData);

  /* Track was shortened and needs a full update */
  void aircraftTrackPruned();

  void simulatorStatusChanged();

  /* Deletes track */
  /* Stops showing the user aircraft */
  void connectedToSimulator();
  void disconnectedFromSimulator();

  /* Disables or enables aircraft and/or track display */
  void updateProfileShowFeatures();

  /* Notification after track deletion */
  void deleteAircraftTrack();

  /* Stops thread and disables all udpates */
  void preDatabaseLoad();

  /* Restarts updates */
  void postDatabaseLoad();

  /* Delta settings for option */
  struct SimUpdateDelta
  {
    int manhattanLengthDelta;
    float altitudeDelta;
  };

  void optionsChanged();
  void styleChanged();

  void saveState();
  void restoreState();
  /* Bring splitter to a resonable size after first start */
  void restoreSplitter();

  void preRouteCalc();

  void mainWindowShown();

  /* Pair of screen y and altitude in feet to display and label the scale */
  QVector<std::pair<int, int> > calcScaleValues();

  float getMinSafeAltitudeFt() const
  {
    return minSafeAltitudeFt;
  }

  /* Widget coordinates for red line */
  int getMinSafeAltitudeY() const;

  /* Widget coordinates for flight plan line */
  int getFlightplanAltY() const;

  float getMaxWindowAlt() const
  {
    return maxWindowAlt;
  }

  /* true if converted route is valid and can be shown on map. This also includes routes without valid TOC and TOD */
  bool hasValidRouteForDisplay() const;

  bool hasTrackPoints() const
  {
    return !aircraftTrackPoints.isEmpty();
  }

  /* Call by this and profile label widget class. Point in screen coordinates. */
  void showContextMenu(const QPoint& globalPoint);

  /* From center button */
  void jumpBackToAircraftCancel();

  void aircraftPerformanceChanged(const atools::fs::perf::AircraftPerf *);

signals:
  /* Emitted when the mouse cursor hovers over the map profile.
   * @param pos Position on the map display.
   */
  void highlightProfilePoint(const atools::geo::Pos& pos);

  /* Show flight plan waypoint or user position on map */
  void showPos(const atools::geo::Pos& pos, float zoom, bool doubleClick);

private:
  /* Show position at x ordinate on profile on the map */
  void showPosAlongFlightplan(int x, bool doubleClick);

  virtual void paintEvent(QPaintEvent *) override;
  virtual void showEvent(QShowEvent *) override;
  virtual void hideEvent(QHideEvent *) override;
  virtual void resizeEvent(QResizeEvent *) override;
  virtual void leaveEvent(QEvent *) override;

  /* Mouse events*/
  virtual void mouseMoveEvent(QMouseEvent *mouseEvent) override;
  virtual void contextMenuEvent(QContextMenuEvent *event) override;

  bool fetchRouteElevations(atools::geo::LineString& elevations, const atools::geo::LineString& geometry) const;
  ElevationLegList fetchRouteElevationsThread(ElevationLegList legs) const;
  void elevationUpdateAvailable();
  void updateTimeout();
  void updateThreadFinished();

  /* Update all screen coordinates and scale factors */
  void updateScreenCoords();

  /* Calculate the left and right margin based on font size and airport elevation text */
  void calcLeftMargin();

  void terminateThread();
  float calcGroundBuffer(float maxElevation);

  void updateLabel();

  /* Calculate map position on flight plan for x screen/widget position on profile.
   *  Additionally gives index into route, distances from/to and altitude at x. maxElev is minimum elevation for leg */
  void calculateDistancesAndPos(int x, atools::geo::Pos& pos, int& routeIndex, float& distance, float& distanceToGo,
                                float& groundElevation, float& maxElev);

  /* Calculate map position on flight plan for x screen/widget position on profile. */
  atools::geo::Pos calculatePos(int x);

  /* Calulate screen x and y position on map */
  int distanceX(float distanceNm) const;
  int altitudeY(float altitudeFt) const;

  /* Convert points (x = distance and y = altitude) to screen coordinates x/y */
  QPoint toScreen(const QPointF& pt) const;
  QPolygon toScreen(const QPolygonF& leg) const;

  void hideRubberBand();

  /* Paint slopes at destination if an approach is selected. */
  void paintIls(QPainter& painter, const Route& route);
  void paintVasi(QPainter& painter, const Route& route);

  void jumpBackToAircraftStart();
  void jumpBackToAircraftTimeout();

  void updateErrorLabel();

  /* Load and save track separately */
  void saveAircraftTrack();
  void loadAircraftTrack();

  void updateTooltip();

  void buildTooltip(int x, bool force);

  /* Get either indicated or real */
  float aircraftAlt(const atools::fs::sc::SimConnectUserAircraft& aircraft);

  /* Get text and text color for a leg. procSymbol is true if only the generic procedure waypoint should be drawn */
  void textsAndColorForLeg(QStringList& texts, QColor& color, bool& procSymbol, const RouteLeg& leg, bool procedureDisplayText);

  /* Scale levels to test for display */
  static Q_DECL_CONSTEXPR int NUM_SCALE_STEPS = 5;
  const int SCALE_STEPS[NUM_SCALE_STEPS] = {500, 1000, 2000, 5000, 10000};
  /* Scales should be at least this amount of pixels apart */
  static Q_DECL_CONSTEXPR int MIN_SCALE_SCREEN_DISTANCE = 25;
  int left = 30; /* Left margin inside widget - calculated depending on font and text size in paint */
  const int TOP = 16; /* Top margin inside widget */

  /* Thread will start after this delay if route was changed */
  static Q_DECL_CONSTEXPR int ROUTE_CHANGE_UPDATE_TIMEOUT_MS = 200;
  static Q_DECL_CONSTEXPR int ROUTE_CHANGE_OFFLINE_UPDATE_TIMEOUT_MS = 100;

  /* Thread will start after this delay if an elevation update arrives */
  static Q_DECL_CONSTEXPR int ELEVATION_CHANGE_ONLINE_UPDATE_TIMEOUT_MS = 5000;
  static Q_DECL_CONSTEXPR int ELEVATION_CHANGE_OFFLINE_UPDATE_TIMEOUT_MS = 100;

  /* Do not calculate a profile for legs longer than this value */
  static Q_DECL_CONSTEXPR int ELEVATION_MAX_LEG_NM = 2000;

  /* User aircraft data */
  atools::fs::sc::SimConnectData simData, lastSimData;

  /* Track x = distance from start in NM and y = altitude in feet */
  QPolygonF aircraftTrackPoints;

  float aircraftDistanceFromStart;
  float lastAircraftDistanceFromStart;
  bool movingBackwards = false;
  ElevationLegList *legList;

  JumpBack *jumpBack = nullptr;
  bool contextMenuActive = false;

  /* Calls updateTimeout which will start the update thread in background */
  QTimer *updateTimer = nullptr;

  /* Used to fetch result from thread */
  QFuture<ElevationLegList> future;
  /* Sends signal once thread is finished */
  QFutureWatcher<ElevationLegList> watcher;
  bool terminateThreadSignal = false;

  bool databaseLoadStatus = false;

  QRubberBand *rubberBand = nullptr;

  int lastTooltipX = -999;
  QString lastTooltipString;
  atools::geo::Pos lastTooltipPos;
  QPoint lastTooltipScreenPos;

  QString fixedLabelText;

  bool widgetVisible = false, showAircraft = false, showAircraftTrack = false;
  QVector<int> waypointX; /* Flight plan waypoint screen coordinates - does contain the dummy
                           * from airport to runway but not missed legs */
  QPolygon landPolygon; /* Green landmass polygon */
  float minSafeAltitudeFt = 0.f, /* Red line */
        maxWindowAlt = 1.f; /* Maximum altitude at top of widget */

  ProfileScrollArea *scrollArea = nullptr;

  float verticalScale = 1.f /* Factor to convert altitude in feet to screen coordinates*/,
        horizontalScale = 1.f /* Factor to convert distance along flight plan in nautical miles to screen coordinates*/;

  /* Numbers for aircraft track */
  static Q_DECL_CONSTEXPR quint32 FILE_MAGIC_NUMBER = 0x6B7C2A3C;
  static Q_DECL_CONSTEXPR quint16 FILE_VERSION = 1;
};

Q_DECLARE_TYPEINFO(ProfileWidget::SimUpdateDelta, Q_PRIMITIVE_TYPE);

#endif // LITTLENAVMAP_PROFILEWIDGET_H
