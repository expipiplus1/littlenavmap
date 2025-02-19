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

#ifndef LITTLENAVMAP_APPROACHQUERY_H
#define LITTLENAVMAP_APPROACHQUERY_H

#include "common/proctypes.h"
#include "fs/fspaths.h"

#include <QCache>
#include <QApplication>
#include <functional>

namespace atools {
namespace sql {
class SqlDatabase;
class SqlQuery;
}
}

class MapQuery;
class AirportQuery;

/* Loads and caches approaches and transitions. Approaches is here a synonym for all procedures which include
 * final approaches, SID and STAR but excludes transitions.
 *
 * The corresponding approach is also loaded and cached if a
 * transition is loaded since legs depend on each other.
 *
 * All navaids and procedure are taken from the nav database which might contain data from nav or simulator.
 * All structs of MapAirport are converted to simulator database airports when passed in.
 *
 * This class does not contain MapWidget related caches.
 */
class ProcedureQuery
{
  Q_DECLARE_TR_FUNCTIONS(ProcedureQuery)

public:
  /*
   * @param sqlDb database for simulator scenery data
   * @param sqlDbNav for updated navaids
   */
  ProcedureQuery(atools::sql::SqlDatabase *sqlDbNav);
  ~ProcedureQuery();

  /* Do not allow copying */
  ProcedureQuery(const ProcedureQuery& other) = delete;
  ProcedureQuery& operator=(const ProcedureQuery& other) = delete;

  /* Get procedure legs from cached procedures */
  const proc::MapProcedureLeg *getApproachLeg(const map::MapAirport& airport, int approachId, int legId);
  const proc::MapProcedureLeg *getTransitionLeg(const map::MapAirport& airport, int legId);

  /* Get either procedure or procedurer with transition */
  const proc::MapProcedureLegs *getProcedureLegs(const map::MapAirport& airport, int approachId, int transitionId);

  /* Get all legs of an approach */
  const proc::MapProcedureLegs *getApproachLegs(map::MapAirport airport, int approachId);

  /* Get transition and its approach */
  const proc::MapProcedureLegs *getTransitionLegs(map::MapAirport airport, int transitionId);

  /* Get all available transitions for the given procedure ID (approach.approach_id in database */
  QVector<int> getTransitionIdsForProcedure(int procedureId);

  /* Resolves all procedures based on given properties and loads them from the database.
   * Procedures are partially resolved in a fuzzy way. */
  void getLegsForFlightplanProperties(const QHash<QString, QString> properties,
                                      const map::MapAirport& departure,
                                      const map::MapAirport& destination,
                                      proc::MapProcedureLegs& arrivalLegs, proc::MapProcedureLegs& starLegs,
                                      proc::MapProcedureLegs& sidLegs, QStringList& errors);

  /* Get dot-separated SID/STAR and the respective transition from the properties */
  static QString getSidAndTransition(QHash<QString, QString>& properties);
  static QString getStarAndTransition(QHash<QString, QString>& properties);

  /* Populate the property list for given procedures */
  static void fillFlightplanProcedureProperties(QHash<QString, QString>& properties,
                                                const proc::MapProcedureLegs& arrivalLegs,
                                                const proc::MapProcedureLegs& starLegs,
                                                const proc::MapProcedureLegs& sidLegs);

  /* Removes properties from the given map based on given types */
  static void clearFlightplanProcedureProperties(QHash<QString, QString>& properties,
                                                 const proc::MapProcedureTypes& type);

  int getSidId(map::MapAirport departure, const QString& sid, const QString& runway = QString(), bool strict = false);
  int getSidTransitionId(map::MapAirport departure, const QString& sidTrans, int sidId, bool strict = false);

  int getStarId(map::MapAirport destination, const QString& star, const QString& runway = QString(),
                bool strict = false);
  int getStarTransitionId(map::MapAirport destination, const QString& starTrans, int starId, bool strict = false);

  /* Creates a user defined VFR approach procedure */
  void createCustomApproach(proc::MapProcedureLegs& procedure, const map::MapAirport& airportSim,
                            const map::MapRunwayEnd& runwayEndSim, float distance, float altitude, float offsetAngle);
  void createCustomDeparture(proc::MapProcedureLegs& procedure, const map::MapAirport& airportSim,
                             const map::MapRunwayEnd& runwayEndSim, float distance);

  /* Flush the cache to update units */
  void clearCache();

  /* Create all queries */
  void initQueries();

  /* Delete all queries */
  void deInitQueries();

  /* Change procedure to insert runway from flight plan as departure or start for arinc names "ALL" or "RW10B".
   * Only for SID or STAR.
   *  Should only be used on a copy of a procedure object and not the cached object.*/
  void insertSidStarRunway(proc::MapProcedureLegs& legs, const QString& runway);

  /* Stitch manual legs between either STAR and airport or STAR and approach together.
   * This will modify the procedures.*/
  void postProcessLegsForRoute(proc::MapProcedureLegs& starLegs, const proc::MapProcedureLegs& arrivalLegs,
                               const map::MapAirport& airport);

private:
  proc::MapProcedureLeg buildTransitionLegEntry(const map::MapAirport& airport);
  proc::MapProcedureLeg buildApproachLegEntry(const map::MapAirport& airport);
  void buildLegEntry(atools::sql::SqlQuery *query, proc::MapProcedureLeg& leg, const map::MapAirport& airport);

  void createCustomApproach(proc::MapProcedureLegs& procedure, const map::MapAirport& airport, const QString& runwayEnd,
                            float distance, float altitude, float offsetAngle);
  void createCustomDeparture(proc::MapProcedureLegs& procedure, const map::MapAirport& airport, const QString& runwayEnd,
                             float distance);

  /* See comments in postProcessLegs about the steps below */
  void postProcessLegs(const map::MapAirport& airport, proc::MapProcedureLegs& legs, bool addArtificialLegs) const;
  void processLegs(proc::MapProcedureLegs& legs) const;
  void processLegErrors(proc::MapProcedureLegs& legs) const;
  void processAltRestrictions(proc::MapProcedureLegs& procedure) const;
  void processLegsFafAndFacf(proc::MapProcedureLegs& legs) const;

  /* Fill the courese and heading to intercept legs after all other lines are calculated */
  void processCourseInterceptLegs(proc::MapProcedureLegs& legs) const;

  /* Fill calculatedDistance, geometry from line and calculated course fields */
  void processLegsDistanceAndCourse(proc::MapProcedureLegs& legs) const;

  /* Add an artificial (not in the database) runway leg if no connection to the end is given */
  void processArtificialLegs(const map::MapAirport& airport, proc::MapProcedureLegs& legs,
                             bool addArtificialLegs) const;

  /* Adjust conflicting altitude restrictions where a transition ends with "A2000" and is the same as the following
   * initial fix having "2000". Also corrects final altitude restriction if below airport. */
  void processLegsFixRestrictions(const map::MapAirport& airport, proc::MapProcedureLegs& legs) const;

  /* Assign magnetic variation from the navaids */
  void updateMagvar(const map::MapAirport& airport, proc::MapProcedureLegs& legs) const;
  void updateBounding(proc::MapProcedureLegs& legs) const;

  void assignType(proc::MapProcedureLegs& procedure) const;

  /* Check if procedure has hard errors. Fills error list if any and resets id to -1*/
  bool procedureValid(const proc::MapProcedureLegs *legs, QStringList& errors);

  /* Create artificial legs, i.e. legs which are not official ones */
  proc::MapProcedureLeg createRunwayLeg(const proc::MapProcedureLeg& leg,
                                        const proc::MapProcedureLegs& legs) const;
  proc::MapProcedureLeg createStartLeg(const proc::MapProcedureLeg& leg,
                                       const proc::MapProcedureLegs& legs, const QStringList& displayText) const;

  proc::MapProcedureLegs *buildApproachLegs(const map::MapAirport& airport, int approachId);
  proc::MapProcedureLegs *fetchApproachLegs(const map::MapAirport& airport, int approachId);
  proc::MapProcedureLegs *fetchTransitionLegs(const map::MapAirport& airport, int approachId,
                                              int transitionId);
  int approachIdForTransitionId(int transitionId);
  void mapObjectByIdent(map::MapResult& result, map::MapTypes type, const QString& ident,
                        const QString& region, const QString& airport,
                        const atools::geo::Pos& sortByDistancePos = atools::geo::EMPTY_POS);

  int findTransitionId(const map::MapAirport& airport, atools::sql::SqlQuery *query,
                       bool strict);
  int findApproachId(const map::MapAirport& airport, atools::sql::SqlQuery *query, const QString& suffix,
                     const QString& runway, bool strict);
  int findProcedureLegId(const map::MapAirport& airport, atools::sql::SqlQuery *query,
                         const QString& suffix, const QString& runway, bool transition,
                         bool strict);

  /* Get runway end and try lower and higher numbers if nothing was found - adds a dummy entry with airport
   * position if no runway ends were found */
  void runwayEndByName(map::MapResult& result, const QString& name, const map::MapAirport& airport);
  void runwayEndByNameSim(map::MapResult& result, const QString& name, const map::MapAirport& airport);

  /* Check if a runway matches an SID/STAR "ALL" or e.g. "RW10B" pattern or matches exactly */
  bool doesRunwayMatch(const QString& runway, const QString& runwayFromQuery, const QString& arincName,
                       const QStringList& airportRunways, bool matchEmptyRunway) const;

  /* Check if a runway matches an SID/STAR "ALL" or e.g. "RW10B" pattern - otherwise false */
  bool doesSidStarRunwayMatch(const QString& runway, const QString& arincName, const QStringList& airportRunways) const;

  /* Get first runway of an airport which matches an SID/STAR "ALL" or e.g. "RW10B" pattern. */
  QString anyMatchingRunwayForSidStar(const QString& arincName, const QStringList& airportRunways) const;

  QString runwayErrorString(const QString& runway);

  atools::sql::SqlDatabase *dbNav;
  atools::sql::SqlQuery *approachLegQuery = nullptr, *transitionLegQuery = nullptr,
                        *transitionIdForLegQuery = nullptr, *approachIdForTransQuery = nullptr,
                        *runwayEndIdQuery = nullptr, *transitionQuery = nullptr, *approachQuery = nullptr,
                        *transitionIdByNameQuery = nullptr, *approachIdByNameQuery = nullptr,
                        *approachIdByArincNameQuery = nullptr, *transitionIdsForApproachQuery = nullptr;

  /* approach ID and transition ID to full lists
   * The approach also has to be stored for transitions since the handover can modify approach legs (CI legs, etc.) */
  QCache<int, proc::MapProcedureLegs> procedureCache, transitionCache;

  /* maps leg ID to approach/transition ID and index in list */
  QHash<int, std::pair<int, int> > procedureLegIndex, transitionLegIndex;

  AirportQuery *airportQueryNav = nullptr;

  /* Dummy used for custom approaches. */
  Q_DECL_CONSTEXPR static int CUSTOM_APPROACH_ID = 1000000000;
  Q_DECL_CONSTEXPR static int CUSTOM_DEPARTURE_ID = 1000000001;

  /* Use this value as an id base for the artifical vector legs. */
  Q_DECL_CONSTEXPR static int VECTOR_LEG_ID_BASE = 1250000000;

  /* Base id for artificial transition/approach connections */
  Q_DECL_CONSTEXPR static int TRANS_CONNECT_LEG_ID_BASE = 1000000000;

  /* Use this value as an id base for the artifical runway legs. Add id of the predecessor to it to be able to find the
   * leg again */
  Q_DECL_CONSTEXPR static int RUNWAY_LEG_ID_BASE = 750000000;

  /* Base id for artificial start legs */
  Q_DECL_CONSTEXPR static int START_LEG_ID_BASE = 500000000;

};

#endif // LITTLENAVMAP_APPROACHQUERY_H
