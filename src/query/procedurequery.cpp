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

#include "query/procedurequery.h"
#include "navapp.h"
#include "sql/sqlrecord.h"
#include "query/mapquery.h"
#include "query/airportquery.h"
#include "geo/calculations.h"
#include "sql/sqldatabase.h"
#include "common/unit.h"
#include "common/constants.h"
#include "geo/line.h"
#include "fs/pln/flightplan.h"
#include "fs/util/fsutil.h"

#include "sql/sqlquery.h"

#include <QStringBuilder>

using atools::sql::SqlQuery;
using atools::geo::Pos;
using atools::geo::Line;
using atools::geo::Rect;
using atools::geo::LineString;
using atools::contains;
using atools::geo::meterToNm;
using atools::geo::opposedCourseDeg;
using atools::geo::nmToMeter;
using atools::geo::normalizeCourse;
using proc::MapProcedureLegs;
using proc::MapProcedureLeg;
using proc::MapAltRestriction;
using proc::MapSpeedRestriction;

namespace pln = atools::fs::pln;
namespace ageo = atools::geo;

ProcedureQuery::ProcedureQuery(atools::sql::SqlDatabase *sqlDbNav)
  : dbNav(sqlDbNav)
{
}

ProcedureQuery::~ProcedureQuery()
{
  deInitQueries();
}

const proc::MapProcedureLegs *ProcedureQuery::getApproachLegs(map::MapAirport airport, int approachId)
{
  NavApp::getMapQueryGui()->getAirportNavReplace(airport);
  return fetchApproachLegs(airport, approachId);
}

const proc::MapProcedureLegs *ProcedureQuery::getTransitionLegs(map::MapAirport airport, int transitionId)
{
  NavApp::getMapQueryGui()->getAirportNavReplace(airport);
  return fetchTransitionLegs(airport, approachIdForTransitionId(transitionId), transitionId);
}

int ProcedureQuery::approachIdForTransitionId(int transitionId)
{
  int approachId = -1;
  approachIdForTransQuery->bindValue(":id", transitionId);
  approachIdForTransQuery->exec();
  if(approachIdForTransQuery->next())
    approachId = approachIdForTransQuery->value("approach_id").toInt();
  approachIdForTransQuery->finish();
  return approachId;
}

const proc::MapProcedureLeg *ProcedureQuery::getApproachLeg(const map::MapAirport& airport, int approachId, int legId)
{
#ifndef DEBUG_APPROACH_NO_CACHE
  if(procedureLegIndex.contains(legId))
  {
    // Already in index
    std::pair<int, int> val = procedureLegIndex.value(legId);

    // Ensure it is in the cache - reload if needed
    const MapProcedureLegs *legs = getApproachLegs(airport, val.first);
    if(legs != nullptr)
      return &legs->at(procedureLegIndex.value(legId).second);
  }
  else
#endif
  {
    // Ensure it is in the cache - reload if needed
    const MapProcedureLegs *legs = getApproachLegs(airport, approachId);
    if(legs != nullptr && procedureLegIndex.contains(legId))
      // Use index to get leg
      return &legs->at(procedureLegIndex.value(legId).second);
  }
  qWarning() << "approach leg with id" << legId << "not found";
  return nullptr;
}

const proc::MapProcedureLeg *ProcedureQuery::getTransitionLeg(const map::MapAirport& airport, int legId)
{
#ifndef DEBUG_APPROACH_NO_CACHE
  if(transitionLegIndex.contains(legId))
  {
    // Already in index
    std::pair<int, int> val = transitionLegIndex.value(legId);

    // Ensure it is in the cache - reload if needed
    const MapProcedureLegs *legs = getTransitionLegs(airport, val.first);

    if(legs != nullptr)
      return &legs->at(transitionLegIndex.value(legId).second);
  }
  else
#endif
  {
    // Get transition ID for leg
    transitionIdForLegQuery->bindValue(":id", legId);
    transitionIdForLegQuery->exec();
    if(transitionIdForLegQuery->next())
    {
      const MapProcedureLegs *legs = getTransitionLegs(airport, transitionIdForLegQuery->value("id").toInt());
      if(legs != nullptr && transitionLegIndex.contains(legId))
        return &legs->at(transitionLegIndex.value(legId).second);
    }
    transitionIdForLegQuery->finish();
  }
  qWarning() << "transition leg with id" << legId << "not found";
  return nullptr;
}

const MapProcedureLegs *ProcedureQuery::getProcedureLegs(const map::MapAirport& airport, int approachId, int transitionId)
{
  if(transitionId > 0)
    return getTransitionLegs(airport, transitionId);
  else
    return getApproachLegs(airport, approachId);
}

proc::MapProcedureLeg ProcedureQuery::buildApproachLegEntry(const map::MapAirport& airport)
{
  MapProcedureLeg leg;
  leg.legId = approachLegQuery->value("approach_leg_id").toInt();
  leg.missed = approachLegQuery->value("is_missed").toBool();
  buildLegEntry(approachLegQuery, leg, airport);
  return leg;
}

proc::MapProcedureLeg ProcedureQuery::buildTransitionLegEntry(const map::MapAirport& airport)
{
  MapProcedureLeg leg;

  leg.legId = transitionLegQuery->value("transition_leg_id").toInt();

  // entry.dmeNavId = transitionLegQuery->value("dme_nav_id").toInt();
  // entry.dmeRadial = transitionLegQuery->value("dme_radial").toFloat();
  // entry.dmeDistance = transitionLegQuery->value("dme_distance").toFloat();
  // entry.dmeIdent = transitionLegQuery->value("dme_ident").toString();
  // if(!transitionLegQuery->isNull("dme_nav_id"))
  // {
  // entry.dme = mapQuery->getVorById(entry.dmeNavId);
  // entry.dmePos = entry.dme.position;
  // }

  leg.missed = false;
  buildLegEntry(transitionLegQuery, leg, airport);
  return leg;
}

void ProcedureQuery::buildLegEntry(atools::sql::SqlQuery *query, proc::MapProcedureLeg& leg,
                                   const map::MapAirport& airport)
{
  leg.type = proc::procedureLegEnum(query->valueStr("type"));

  leg.turnDirection = query->valueStr("turn_direction");
  leg.arincDescrCode = query->valueStr("arinc_descr_code", QString()).toUpper();

  leg.fixType = query->valueStr("fix_type");
  leg.fixIdent = query->valueStr("fix_ident");
  leg.fixRegion = query->valueStr("fix_region");
  leg.fixPos.setLonX(query->valueFloat("fix_lonx", Pos::INVALID_VALUE));
  leg.fixPos.setLatY(query->valueFloat("fix_laty", Pos::INVALID_VALUE));
  if(leg.fixPos.isNull(Pos::POS_EPSILON_1M)) // In case field is present but null
    leg.fixPos = Pos();

  // query->value("fix_airport_ident");
  leg.recFixType = query->valueStr("recommended_fix_type");
  leg.recFixIdent = query->valueStr("recommended_fix_ident");
  leg.recFixRegion = query->valueStr("recommended_fix_region");
  leg.recFixPos.setLonX(query->valueFloat("recommended_fix_lonx", Pos::INVALID_VALUE));
  leg.recFixPos.setLatY(query->valueFloat("recommended_fix_laty", Pos::INVALID_VALUE));
  if(leg.recFixPos.isNull(Pos::POS_EPSILON_1M)) // In case field is present but null
    leg.recFixPos = Pos();

  leg.flyover = query->valueBool("is_flyover");
  leg.trueCourse = query->valueBool("is_true_course");
  leg.course = query->valueFloat("course");
  leg.distance = query->valueFloat("distance");
  leg.time = query->valueFloat("time");
  leg.theta = query->valueFloat("theta");
  leg.rho = query->valueFloat("rho");

  leg.calculatedDistance = 0.f;
  leg.calculatedTrueCourse = 0.f;
  leg.disabled = false;
  leg.malteseCross = false;
  leg.intercept = false;

  float alt1 = query->valueFloat("altitude1");
  float alt2 = query->valueFloat("altitude2");

  if(!query->isNull("alt_descriptor") && (alt1 > 0.f || alt2 > 0.f))
  {
    QString descriptor = query->value("alt_descriptor").toString();

    if(descriptor == "A")
    {
      if(alt2 < alt1 && alt2 > 0.f)
      {
        // Adjust ILS glide slope - workaround for missing G and I indicators

        // G Glide Slope altitude (MSL) specified in the second “Altitude” field and
        // “at” altitude specified in the first “Altitude” field on the FAF Waypoint in Precision Approach Coding
        // with electronic Glide Slope.
        // I Glide Slope Intercept Altitude specified in second “Altitude” field and
        // “at” altitude specified in first “Altitude” field on the FACF Waypoint in Precision Approach Coding
        // with electronic Glide Slope
        std::swap(alt1, alt2);
        leg.altRestriction.descriptor = MapAltRestriction::ILS_AT;
      }
      else
        leg.altRestriction.descriptor = MapAltRestriction::AT;
    }
    else if(descriptor == "+")
    {
      if(alt2 < alt1 && alt2 > 0.f)
      {
        // Adjust ILS glide slope - workaround for missing H and J indicators

        // H Glide Slope Altitude (MSL) specified in second “Altitude” field and
        // “at or above” altitude specified in first “Altitude” field on the FAF Waypoint in Precision Approach Coding
        // with electronic Glide Slope
        // J Glide Slope Intercept Altitude specified in second “Altitude” field and
        // “at or above” altitude J specified in first “Altitude” field on the FACF Waypoint in Precision Approach Coding
        // with electronic Glide Slope “At” altitude on the coded vertical angle in the
        std::swap(alt1, alt2);
        leg.altRestriction.descriptor = MapAltRestriction::ILS_AT_OR_ABOVE;
      }
      else
        leg.altRestriction.descriptor = MapAltRestriction::AT_OR_ABOVE;
    }
    else if(descriptor == "-")
      leg.altRestriction.descriptor = MapAltRestriction::AT_OR_BELOW;
    else if(descriptor == "B")
      leg.altRestriction.descriptor = MapAltRestriction::BETWEEN;
    else
      leg.altRestriction.descriptor = MapAltRestriction::AT;

    leg.altRestriction.alt1 = alt1;
    leg.altRestriction.alt2 = alt2;
  }
  else
  {
    leg.altRestriction.descriptor = MapAltRestriction::NONE;
    leg.altRestriction.alt1 = 0.f;
    leg.altRestriction.alt2 = 0.f;
  }

  if(query->hasField("speed_limit"))
  {
    float speedLimit = query->value("speed_limit").toFloat();

    if(speedLimit > 1.f)
    {
      QString type = query->value("speed_limit_type").toString();

      leg.speedRestriction.speed = speedLimit;

      if(type == "+") // Minimum speed
        leg.speedRestriction.descriptor = MapSpeedRestriction::MIN;
      else if(type == "-") // Maximum speed
        leg.speedRestriction.descriptor = MapSpeedRestriction::MAX;
      else
        leg.speedRestriction.descriptor = MapSpeedRestriction::AT;
    }
  }
  else
  {
    leg.speedRestriction.descriptor = MapSpeedRestriction::NONE;
    leg.speedRestriction.speed = 0.f;
  }

  if(query->hasField("vertical_angle") && !query->isNull("vertical_angle"))
    leg.verticalAngle = query->valueFloat("vertical_angle");
  else
    leg.verticalAngle = map::INVALID_ANGLE_VALUE;

  leg.magvar = map::INVALID_MAGVAR;

  // Use fix position if present - otherwise use airport position to get nearest fix
  Pos fixPos = leg.fixPos.isValid() ? leg.fixPos : airport.position;
  Pos recFixPos = leg.recFixPos.isValid() ? leg.recFixPos : airport.position;

  // ============================================================================================
  // Load full navaid information for fix and set fix position
  if(leg.fixType == "W" || leg.fixType == "TW")
  {
    mapObjectByIdent(leg.navaids, map::WAYPOINT, leg.fixIdent, leg.fixRegion, QString(), fixPos);
    if(!leg.navaids.waypoints.isEmpty())
    {
      leg.fixPos = leg.navaids.waypoints.first().position;
      leg.magvar = leg.navaids.waypoints.first().magvar;
      leg.navId = leg.navaids.waypoints.first().id;
    }
  }
  else if(leg.fixType == "V")
  {
    // Get both VOR with region and ILS without region
    mapObjectByIdent(leg.navaids, map::VOR, leg.fixIdent, leg.fixRegion, QString(), fixPos);
    mapObjectByIdent(leg.navaids, map::ILS, leg.fixIdent, QString(), airport.ident, fixPos);

    if(leg.navaids.hasVor() && leg.navaids.hasIls())
    {
      // Remove the one with is farther away from the airport or fix position
      if(leg.navaids.vors.first().position.distanceMeterTo(leg.recFixPos) <
         leg.navaids.ils.first().position.distanceMeterTo(leg.recFixPos))
        leg.navaids.clear(map::ILS); // VOR is closer
      else
        leg.navaids.clear(map::VOR); // ILS is closer
    }

    if(leg.navaids.hasVor())
    {
      leg.fixPos = leg.navaids.vors.first().position;
      leg.magvar = leg.navaids.vors.first().magvar;
      leg.navId = leg.navaids.vors.first().id;

      // Also update region and type if missing
      if(leg.fixRegion.isEmpty())
        leg.fixRegion = leg.navaids.vors.first().region;
      if(leg.fixType.isEmpty())
        leg.fixType = "V";
    }
    else if(leg.navaids.hasIls())
    {
      leg.fixPos = leg.navaids.ils.first().position;
      leg.magvar = leg.navaids.ils.first().magvar;
      leg.navId = leg.navaids.ils.first().id;

      // Also update region and type if missing
      if(leg.fixRegion.isEmpty())
        leg.fixRegion = leg.navaids.ils.first().region;
      if(leg.fixType.isEmpty())
        leg.fixType = "L";
    }
  }
  else if(leg.fixType == "N" || leg.fixType == "TN")
  {
    mapObjectByIdent(leg.navaids, map::NDB, leg.fixIdent, leg.fixRegion, QString(), fixPos);
    if(!leg.navaids.ndbs.isEmpty())
    {
      leg.fixPos = leg.navaids.ndbs.first().position;
      leg.magvar = leg.navaids.ndbs.first().magvar;
      leg.navId = leg.navaids.ndbs.first().id;
    }
  }
  else if(leg.fixType == "R")
  {
    runwayEndByName(leg.navaids, leg.fixIdent, airport);
    leg.fixPos = leg.navaids.runwayEnds.isEmpty() ? airport.position : leg.navaids.runwayEnds.first().position;
    leg.navId = -1;
  }
  else if(leg.fixType == "A")
  {
    mapObjectByIdent(leg.navaids, map::AIRPORT, leg.fixIdent, QString(), airport.ident, fixPos);

    // Try to workaround the 4/3 three letter airport idents (K1G5 vs 1G5)
    if(leg.navaids.airports.isEmpty() && leg.fixIdent.size() == 4 &&
       (leg.fixIdent.startsWith("X") || leg.fixIdent.startsWith("K") || leg.fixIdent.startsWith("C")))
      mapObjectByIdent(leg.navaids, map::AIRPORT, leg.fixIdent.right(3), QString(), airport.ident, fixPos);

    if(!leg.navaids.airports.isEmpty())
    {
      leg.fixIdent = leg.navaids.airports.first().ident;
      leg.fixPos = leg.navaids.airports.first().position;
      leg.magvar = leg.navaids.airports.first().magvar;
      leg.navId = leg.navaids.airports.first().id;
    }
  }
  else if(leg.fixType == "L" || leg.fixType.isEmpty() /* Workaround for missing navaid type in DFD */)
  {
    mapObjectByIdent(leg.navaids, map::ILS, leg.fixIdent, QString(), airport.ident, fixPos);
    if(!leg.navaids.ils.isEmpty())
    {
      leg.fixPos = leg.navaids.ils.first().position;
      leg.magvar = leg.navaids.ils.first().magvar;
      leg.navId = leg.navaids.ils.first().id;

      if(leg.fixRegion.isEmpty())
        leg.fixRegion = leg.navaids.ils.first().region;
      if(leg.fixType.isEmpty())
        leg.fixType = "L";
    }
    else
    {
      // Use a VOR or DME as fallback
      leg.navaids.clear();
      mapObjectByIdent(leg.navaids, map::VOR, leg.fixIdent, QString(), airport.ident, fixPos);
      if(!leg.navaids.vors.isEmpty())
      {
        leg.fixPos = leg.navaids.vors.first().position;
        leg.magvar = leg.navaids.vors.first().magvar;
        leg.navId = leg.navaids.vors.first().id;

        if(leg.fixRegion.isEmpty())
          leg.fixRegion = leg.navaids.vors.first().region;
        if(leg.fixType.isEmpty())
          leg.fixType = "V";
      }
      else
      {
        // Use a NDB as second fallback
        leg.navaids.clear();
        mapObjectByIdent(leg.navaids, map::NDB, leg.fixIdent, QString(), airport.ident, fixPos);
        if(!leg.navaids.ndbs.isEmpty())
        {
          leg.fixPos = leg.navaids.ndbs.first().position;
          leg.magvar = leg.navaids.ndbs.first().magvar;
          leg.navId = leg.navaids.ndbs.first().id;

          if(leg.fixRegion.isEmpty())
            leg.fixRegion = leg.navaids.ndbs.first().region;
          if(leg.fixType.isEmpty())
            leg.fixType = "N";
        }
      }
    }
  }

  // ============================================================================================
  // Load navaid information for recommended fix and set fix position
  // Also update magvar if not already set
  map::MapResult recResult;
  if(leg.recFixType == "W" || leg.recFixType == "TW")
  {
    mapObjectByIdent(recResult, map::WAYPOINT, leg.recFixIdent, leg.recFixRegion, QString(), recFixPos);
    if(!recResult.waypoints.isEmpty())
    {
      leg.recFixPos = recResult.waypoints.first().position;
      leg.recNavId = recResult.waypoints.first().id;

      if(!(leg.magvar < map::INVALID_MAGVAR))
        leg.magvar = recResult.waypoints.first().magvar;
    }
  }
  else if(leg.recFixType == "V")
  {
    // Get both VOR with region and ILS without region
    mapObjectByIdent(recResult, map::VOR, leg.recFixIdent, leg.recFixRegion, QString(), recFixPos);
    mapObjectByIdent(recResult, map::ILS, leg.recFixIdent, QString(), airport.ident, recFixPos);

    if(recResult.hasVor() && recResult.hasIls())
    {
      // Remove the one with is farther away from the airport or fix position
      if(recResult.vors.first().position.distanceMeterTo(leg.recFixPos) <
         recResult.ils.first().position.distanceMeterTo(leg.recFixPos))
        recResult.clear(map::ILS); // VOR is closer
      else
        recResult.clear(map::VOR); // ILS is closer
    }

    if(recResult.hasVor())
    {
      leg.recFixPos = recResult.vors.first().position;
      leg.recNavId = recResult.vors.first().id;

      if(!(leg.magvar < map::INVALID_MAGVAR))
        leg.magvar = recResult.vors.first().magvar;

      // Also update region and type if missing
      if(leg.recFixRegion.isEmpty())
        leg.recFixRegion = recResult.vors.first().region;

      if(leg.recFixType.isEmpty())
        leg.recFixType = "V";
    }
    else if(recResult.hasIls())
    {
      leg.recFixPos = recResult.ils.first().position;
      leg.recNavId = recResult.ils.first().id;

      if(!(leg.magvar < map::INVALID_MAGVAR))
        leg.magvar = recResult.ils.first().magvar;

      // Also update region and type if missing
      if(leg.recFixRegion.isEmpty())
        leg.recFixRegion = recResult.ils.first().region;

      if(leg.recFixType.isEmpty())
        leg.recFixType = "L";
    }
  }
  else if(leg.recFixType == "N" || leg.recFixType == "TN")
  {
    mapObjectByIdent(recResult, map::NDB, leg.recFixIdent, leg.recFixRegion, QString(), recFixPos);
    if(!recResult.ndbs.isEmpty())
    {
      leg.recFixPos = recResult.ndbs.first().position;
      leg.recNavId = recResult.ndbs.first().id;

      if(!(leg.magvar < map::INVALID_MAGVAR))
        leg.magvar = recResult.ndbs.first().magvar;
    }
  }
  else if(leg.recFixType == "R")
  {
    runwayEndByName(recResult, leg.recFixIdent, airport);
    leg.recFixPos = recResult.runwayEnds.isEmpty() ? airport.position : recResult.runwayEnds.first().position;
    leg.recNavId = -1;
  }
  else if(leg.recFixType == "L" || leg.recFixType.isEmpty() /* Workaround for missing navaid type in DFD */)
  {
    mapObjectByIdent(recResult, map::ILS, leg.recFixIdent, QString(), airport.ident, recFixPos);
    if(!recResult.ils.isEmpty())
    {
      leg.recFixPos = recResult.ils.first().position;
      leg.recNavId = recResult.ils.first().id;

      if(!(leg.magvar < map::INVALID_MAGVAR))
        leg.magvar = recResult.ils.first().magvar;

      if(leg.recFixRegion.isEmpty())
        leg.recFixRegion = recResult.ils.first().region;

      if(leg.recFixType.isEmpty())
        leg.recFixType = "L";
    }
    else
    {
      // Use a VOR or DME as fallback
      recResult.clear();
      mapObjectByIdent(recResult, map::VOR, leg.recFixIdent, QString(), airport.ident, recFixPos);
      if(!recResult.vors.isEmpty())
      {
        leg.recFixPos = recResult.vors.first().position;
        leg.recNavId = recResult.vors.first().id;

        if(!(leg.magvar < map::INVALID_MAGVAR))
          leg.magvar = recResult.vors.first().magvar;

        if(leg.recFixRegion.isEmpty())
          leg.recFixRegion = recResult.vors.first().region;

        if(leg.recFixType.isEmpty())
          leg.recFixType = "V";
      }
      else
      {
        // Use a NDB as second fallback
        recResult.clear();
        mapObjectByIdent(recResult, map::NDB, leg.recFixIdent, QString(), airport.ident, recFixPos);
        if(!recResult.ndbs.isEmpty())
        {
          leg.recFixPos = recResult.ndbs.first().position;
          leg.recNavId = recResult.ndbs.first().id;

          if(!(leg.magvar < map::INVALID_MAGVAR))
            leg.magvar = recResult.ndbs.first().magvar;

          if(leg.recFixRegion.isEmpty())
            leg.recFixRegion = recResult.ndbs.first().region;

          if(leg.recFixType.isEmpty())
            leg.recFixType = "N";
        }
      }
    }
  }
}

void ProcedureQuery::runwayEndByName(map::MapResult& result, const QString& name, const map::MapAirport& airport)
{
  Q_ASSERT(airport.navdata);

  NavApp::getMapQueryGui()->getRunwayEndByNameFuzzy(result.runwayEnds, name, airport, true /* navdata */);
}

void ProcedureQuery::runwayEndByNameSim(map::MapResult& result, const QString& name,
                                        const map::MapAirport& airport)
{
  Q_ASSERT(!airport.navdata);
  NavApp::getMapQueryGui()->getRunwayEndByNameFuzzy(result.runwayEnds, name, airport, false /* navdata */);
}

void ProcedureQuery::mapObjectByIdent(map::MapResult& result, map::MapTypes type,
                                      const QString& ident, const QString& region, const QString& airport,
                                      const Pos& sortByDistancePos)
{
  MapQuery *mapQuery = NavApp::getMapQueryGui();

  mapQuery->getMapObjectByIdent(result, type, ident, region, airport, sortByDistancePos,
                                nmToMeter(1000.f), true /* airport from nav database */);
  if(result.isEmpty(type))
    // Try again in 200 nm radius by excluding the region - result sorted by distance
    mapQuery->getMapObjectByIdent(result, type, ident, QString(), airport, sortByDistancePos,
                                  nmToMeter(1000.f), true /* airport from nav database */);
}

void ProcedureQuery::updateMagvar(const map::MapAirport& airport, proc::MapProcedureLegs& legs) const
{
  // Calculate average magvar for all legs
  float avgMagvar = 0.f;
  float num = 0.f;
  for(int i = 0; i < legs.size(); i++)
  {
    if(legs.at(i).magvar < map::INVALID_MAGVAR)
    {
      avgMagvar += legs.at(i).magvar;
      num++;
    }
  }

  if(num > 0)
    avgMagvar /= num;
  else
    // Use magnetic variance of the airport if nothing found
    avgMagvar = airport.magvar;

  // Assign average to legs with no magvar
  for(MapProcedureLeg& leg : legs.approachLegs)
  {
    if(!(leg.magvar < map::INVALID_MAGVAR))
      leg.magvar = avgMagvar;
  }

  for(MapProcedureLeg& leg : legs.transitionLegs)
  {
    if(!(leg.magvar < map::INVALID_MAGVAR))
      leg.magvar = avgMagvar;
  }
}

void ProcedureQuery::updateBounding(proc::MapProcedureLegs& legs) const
{
  for(int i = 0; i < legs.size(); i++)
  {
    const proc::MapProcedureLeg& leg = legs.at(i);
    if(leg.isHold())
    {
      // Simply extend bounding by a rectangle with the radius of hold distance - assume 250 kts if time is used
      legs.bounding.extend(Rect(leg.fixPos, leg.distance > 0 ?
                                nmToMeter(leg.distance * 2.f) :
                                nmToMeter(leg.time > 0.f ? leg.time / 60.f * 250.f : 10.f)));
      legs.bounding.extend(leg.holdLine.boundingRect());
    }
    else if(leg.isProcedureTurn())
    {
      legs.bounding.extend(leg.procedureTurnPos);

      // Approximate the extension of the turn section
      legs.bounding.extend(leg.procedureTurnPos.endpoint(atools::geo::nmToMeter(8.f), leg.legTrueCourse()));
    }

    legs.bounding.extend(leg.fixPos);
    legs.bounding.extend(leg.interceptPos);
    legs.bounding.extend(leg.line.boundingRect());
    legs.bounding.extend(leg.geometry);
  }
}

proc::MapProcedureLegs *ProcedureQuery::fetchApproachLegs(const map::MapAirport& airport, int approachId)
{
  Q_ASSERT(airport.navdata);

#ifndef DEBUG_APPROACH_NO_CACHE
  if(procedureCache.contains(approachId))
    return procedureCache.object(approachId);
  else
#endif
  {
#ifdef DEBUG_INFORMATION
    qDebug() << "buildApproachEntries" << airport.ident << "approachId" << approachId;
#endif

    MapProcedureLegs *legs = buildApproachLegs(airport, approachId);
    postProcessLegs(airport, *legs, true /*addArtificialLegs*/);

    for(int i = 0; i < legs->size(); i++)
      procedureLegIndex.insert(legs->at(i).legId, std::make_pair(approachId, i));

    procedureCache.insert(approachId, legs);
    return legs;
  }
}

proc::MapProcedureLegs *ProcedureQuery::fetchTransitionLegs(const map::MapAirport& airport,
                                                            int approachId, int transitionId)
{
  Q_ASSERT(airport.navdata);

#ifndef DEBUG_APPROACH_NO_CACHE
  if(transitionCache.contains(transitionId))
    return transitionCache.object(transitionId);
  else
#endif
  {
#ifdef DEBUG_INFORMATION
    qDebug() << "buildApproachEntries" << airport.ident << "approachId" << approachId
             << "transitionId" << transitionId;
#endif

    transitionLegQuery->bindValue(":id", transitionId);
    transitionLegQuery->exec();

    proc::MapProcedureLegs *legs = new proc::MapProcedureLegs;
    legs->ref.airportId = airport.id;
    legs->ref.approachId = approachId;
    legs->ref.transitionId = transitionId;
    legs->ref.mapType = legs->mapType;

    while(transitionLegQuery->next())
    {
      legs->transitionLegs.append(buildTransitionLegEntry(airport));
      legs->transitionLegs.last().airportId = airport.id;
      legs->transitionLegs.last().approachId = approachId;
      legs->transitionLegs.last().transitionId = transitionId;
    }

    // Add a full copy of the approach because approach legs will be modified for different transitions
    proc::MapProcedureLegs *approach = buildApproachLegs(airport, approachId);
    legs->approachLegs = approach->approachLegs;
    legs->runwayEnd = approach->runwayEnd;
    legs->procedureRunway = approach->procedureRunway;
    legs->approachType = approach->approachType;
    legs->approachSuffix = approach->approachSuffix;
    legs->approachFixIdent = approach->approachFixIdent;
    legs->approachArincName = approach->approachArincName;
    legs->gpsOverlay = approach->gpsOverlay;
    legs->circleToLand = approach->circleToLand;

    delete approach;

    transitionQuery->bindValue(":id", transitionId);
    transitionQuery->exec();
    if(transitionQuery->next())
    {
      legs->transitionType = transitionQuery->value("type").toString();
      legs->transitionFixIdent = transitionQuery->value("fix_ident").toString();
    }
    transitionQuery->finish();

    postProcessLegs(airport, *legs, true /*addArtificialLegs*/);

    for(int i = 0; i < legs->size(); ++i)
      transitionLegIndex.insert(legs->at(i).legId, std::make_pair(transitionId, i));

    transitionCache.insert(transitionId, legs);
    return legs;
  }
}

proc::MapProcedureLegs *ProcedureQuery::buildApproachLegs(const map::MapAirport& airport, int approachId)
{
  Q_ASSERT(airport.navdata);

  approachLegQuery->bindValue(":id", approachId);
  approachLegQuery->exec();

  proc::MapProcedureLegs *legs = new proc::MapProcedureLegs;
  legs->ref.airportId = airport.id;
  legs->ref.approachId = approachId;
  legs->ref.transitionId = -1;
  legs->ref.mapType = legs->mapType;

  // Populated when processing artifical legs
  legs->circleToLand = false;

  // Load all legs ======================
  while(approachLegQuery->next())
  {
    legs->approachLegs.append(buildApproachLegEntry(airport));
    legs->approachLegs.last().airportId = airport.id;
    legs->approachLegs.last().approachId = approachId;
  }

  // Load basic approach information ======================
  approachQuery->bindValue(":id", approachId);
  approachQuery->exec();
  if(approachQuery->next())
  {
    legs->approachType = approachQuery->value("type").toString();
    legs->approachSuffix = approachQuery->value("suffix").toString();
    legs->approachFixIdent = approachQuery->value("fix_ident").toString();
    legs->approachArincName = approachQuery->valueStr("arinc_name", QString());
    legs->gpsOverlay = approachQuery->value("has_gps_overlay").toBool();
    legs->procedureRunway = approachQuery->value("runway_name").toString();
  }
  approachQuery->finish();

  // Get all runway ends if they are in the database
  bool runwayFound = false;
  runwayEndIdQuery->bindValue(":id", approachId);
  runwayEndIdQuery->exec();
  if(runwayEndIdQuery->next())
  {
    if(!runwayEndIdQuery->isNull("runway_end_id"))
    {
      legs->runwayEnd = airportQueryNav->getRunwayEndById(runwayEndIdQuery->value("runway_end_id").toInt());

      // Add altitude to position since it is needed to display the first point in the SID
      legs->runwayEnd.position.setAltitude(airport.getPosition().getAltitude());
      runwayFound = true;
    }
  }
  runwayEndIdQuery->finish();

  if(!runwayFound)
  {
    // Nothing found in the database - search by name fuzzy or add a dummy entry if nothing was found by name
#ifdef DEBUG_INFORMATION
    qWarning() << "Runway end for approach" << approachId << "not found";
#endif
    map::MapResult result;
    runwayEndByName(result, legs->procedureRunway, airport);

    if(!result.runwayEnds.isEmpty())
      legs->runwayEnd = result.runwayEnds.first();
  }

  return legs;
}

void ProcedureQuery::postProcessLegs(const map::MapAirport& airport, proc::MapProcedureLegs& legs,
                                     bool addArtificialLegs) const
{
  // Clear lists so this method can run twice on a legs object
  for(MapProcedureLeg& leg : legs.approachLegs)
  {
    leg.displayText.clear();
    leg.remarks.clear();
    leg.geometry.clear();
    leg.line.setPos1(Pos());
    leg.line.setPos2(Pos());
  }

  for(MapProcedureLeg& leg : legs.transitionLegs)
  {
    leg.displayText.clear();
    leg.remarks.clear();
    leg.geometry.clear();
    leg.line.setPos1(Pos());
    leg.line.setPos2(Pos());
  }

  // Update the mapTypes
  assignType(legs);

  // Set the force altitude flag for FAF and FACF
  processAltRestrictions(legs);

  updateMagvar(airport, legs);

  // Prepare all leg coordinates and fill line
  processLegs(legs);

  // Add artificial legs (not in the database) that end at the runway
  processArtificialLegs(airport, legs, addArtificialLegs);

  // Calculate intercept terminators
  processCourseInterceptLegs(legs);

  // fill distance and course as well as geometry field
  processLegsDistanceAndCourse(legs);

  // Correct overlapping conflicting altitude restrictions
  processLegsFixRestrictions(airport, legs);

  // Update bounding rectangle
  updateBounding(legs);

  // Collect leg errors to procedure error
  processLegErrors(legs);

  // Check which leg is used to draw the Maltesian cross
  processLegsFafAndFacf(legs);

  // qDebug() << legs;
}

void ProcedureQuery::processArtificialLegs(const map::MapAirport& airport, proc::MapProcedureLegs& legs,
                                           bool addArtificialLegs) const
{
  if(!legs.isEmpty() && addArtificialLegs)
  {
    if(!legs.transitionLegs.isEmpty() && !legs.approachLegs.isEmpty())
    {
      // ====================================================================================
      // Insert leg between approach and transition and add new one to approach
      if(legs.mapType & proc::PROCEDURE_SID_ALL)
      {
        MapProcedureLeg& firstTransLeg = legs.transitionLegs.first();
        MapProcedureLeg& lastApprLeg = legs.approachLegs.last();

        if((firstTransLeg.fixIdent != lastApprLeg.fixIdent || firstTransLeg.fixRegion != lastApprLeg.fixRegion) &&
           firstTransLeg.isInitialFix() &&
           contains(lastApprLeg.type, {proc::COURSE_TO_FIX, proc::CUSTOM_APP_RUNWAY, proc::CUSTOM_DEP_END,
                                       proc::DIRECT_TO_FIX, proc::TRACK_TO_FIX}))
        {
          // Correct previous and last position
          firstTransLeg.line.setPos1(firstTransLeg.fixPos);
          lastApprLeg.line.setPos2(lastApprLeg.fixPos);

          // Insert a new leg
          MapProcedureLeg leg = firstTransLeg;
          leg.type = proc::DIRECT_TO_FIX;
          leg.mapType = proc::PROCEDURE_SID;
          leg.line = Line(lastApprLeg.line.getPos2(), firstTransLeg.line.getPos1());
          leg.turnDirection.clear();
          leg.displayText.clear();
          leg.remarks.clear();

          leg.transitionId = -1;
          leg.legId = TRANS_CONNECT_LEG_ID_BASE + leg.legId;

          legs.approachLegs.append(leg);
        }
      }

      // ====================================================================================
      // Insert between transition and approach and add new one to transition
      if(legs.mapType & proc::PROCEDURE_STAR_ALL)
      {
        MapProcedureLeg& lastTransLeg = legs.transitionLegs.last();
        MapProcedureLeg& firstApprLeg = legs.approachLegs.first();

        if((lastTransLeg.fixIdent != firstApprLeg.fixIdent || lastTransLeg.fixRegion != firstApprLeg.fixRegion) &&
           firstApprLeg.isInitialFix() &&
           contains(lastTransLeg.type, {proc::COURSE_TO_FIX, proc::CUSTOM_APP_RUNWAY, proc::CUSTOM_DEP_END,
                                        proc::DIRECT_TO_FIX, proc::TRACK_TO_FIX}))
        {
          // Correct previous and last position
          lastTransLeg.line.setPos2(lastTransLeg.fixPos);
          firstApprLeg.line.setPos1(firstApprLeg.fixPos);

          // Insert a new leg - create a copy of the successor
          MapProcedureLeg leg = firstApprLeg;
          leg.type = proc::DIRECT_TO_FIX;
          leg.mapType = proc::PROCEDURE_STAR_TRANSITION;
          leg.line = Line(lastTransLeg.line.getPos2(), firstApprLeg.line.getPos1());
          leg.turnDirection.clear();
          leg.displayText.clear();
          leg.remarks.clear();

          leg.transitionId = legs.transitionLegs.last().transitionId;
          leg.legId = TRANS_CONNECT_LEG_ID_BASE + leg.legId;

          legs.transitionLegs.append(leg);
        }
      } // if(legs.mapType & proc::PROCEDURE_STAR_ALL)
    } // if(!legs.transitionLegs.isEmpty() && !legs.approachLegs.isEmpty())

    // ====================================================================================
    // Add legs that connect airport center to departure runway
    if(legs.mapType & proc::PROCEDURE_DEPARTURE)
    {
      if(legs.runwayEnd.isValid())
      {
        QVector<MapProcedureLeg>& legList = legs.approachLegs.isEmpty() ? legs.transitionLegs : legs.approachLegs;

        if(!legList.isEmpty())
        {
          if(legList.first().isInitialFix() && legList.first().fixType != "R")
          {
            // Convert IF back into a point
            legList.first().line.setPos1(legList.first().line.getPos2());

            // Connect runway and initial fix
            proc::MapProcedureLeg sleg = createStartLeg(legs.first(), legs, {});
            sleg.type = proc::VECTORS;
            sleg.line = Line(legs.runwayEnd.position, legs.first().line.getPos1());
            sleg.mapType = legs.approachLegs.isEmpty() ? proc::PROCEDURE_SID_TRANSITION : proc::PROCEDURE_SID;
            legList.prepend(sleg);
          }

          // Add runway fix to departure
          proc::MapProcedureLeg rwleg = createRunwayLeg(legList.first(), legs);
          rwleg.type = proc::DIRECT_TO_RUNWAY;
          rwleg.altRestriction.alt1 = airport.position.getAltitude(); // At 50ft above threshold
          rwleg.line = Line(legs.runwayEnd.position);
          rwleg.mapType = legs.approachLegs.isEmpty() ? proc::PROCEDURE_SID_TRANSITION : proc::PROCEDURE_SID;
          rwleg.distance = 0.f;
          rwleg.course = map::INVALID_COURSE_VALUE;

          legList.prepend(rwleg);
        }
      }
    } // if(legs.mapType & proc::PROCEDURE_DEPARTURE)
    else
    {
      // ====================================================================================
      // Add an artificial initial fix if first leg is no intital fix to keep all consistent ========================
      // if(!proc::procedureLegFixAtStart(legs.first().type) && !legs.first().line.isPoint())
      if(!legs.first().isInitialFix() && !legs.first().line.isPoint())
      {
        proc::MapProcedureLeg sleg = createStartLeg(legs.first(), legs, {tr("Start")});
        sleg.type = proc::START_OF_PROCEDURE;
        sleg.line = Line(legs.first().line.getPos1());

        if(legs.mapType & proc::PROCEDURE_STAR_TRANSITION)
        {
          sleg.mapType = proc::PROCEDURE_STAR_TRANSITION;
          legs.approachLegs.prepend(sleg);
        }
        else if(legs.mapType & proc::PROCEDURE_STAR)
        {
          sleg.mapType = proc::PROCEDURE_STAR;
          legs.approachLegs.prepend(sleg);
        }
        else if(legs.mapType & proc::PROCEDURE_TRANSITION)
        {
          sleg.mapType = proc::PROCEDURE_TRANSITION;
          legs.transitionLegs.prepend(sleg);
        }
        else if(legs.mapType & proc::PROCEDURE_APPROACH)
        {
          sleg.mapType = proc::PROCEDURE_APPROACH;
          legs.approachLegs.prepend(sleg);
        }
      }
    } // if(legs.mapType & proc::PROCEDURE_DEPARTURE) else

    // ====================================================================================
    // Add circle to land or straight in leg
    if(legs.mapType & proc::PROCEDURE_APPROACH_ALL_MISSED)
    {
      for(int i = 0; i < legs.size() - 1; i++)
      {
        // Look for the transition from approach to missed ====================================
        proc::MapProcedureLeg& leg = legs[i];
        if(leg.isApproach() && legs.at(i + 1).isMissed())
        {
          if(leg.fixType != "R") // Not a runway?
          {
            // Airport but no runway if name is empty - set to CTL approach
            legs.circleToLand = legs.runwayEnd.name.isEmpty();

            // Not a runway fix and runway reference is valid - add own runway fix
            // This is a circle to land approach
            proc::MapProcedureLeg rwleg = createRunwayLeg(leg, legs);
            rwleg.type = legs.circleToLand ? proc::CIRCLE_TO_LAND : proc::STRAIGHT_IN;

            // At 50ft above threshold
            // TODO this does not consider displaced thresholds
            rwleg.altRestriction.alt1 = airport.position.getAltitude() + 50.f;

            rwleg.line = Line(leg.line.getPos2(), legs.runwayEnd.position);
            rwleg.mapType = proc::PROCEDURE_APPROACH;

            int insertPosition = i + 1 - legs.transitionLegs.size();

            if(atools::inRange(legs.approachLegs, insertPosition - 1))
            {
              // Fix threshold altitude since it might be above the last altitude restriction
              const proc::MapAltRestriction& lastAltRestr = legs.approachLegs.at(insertPosition - 1).altRestriction;
              if(lastAltRestr.descriptor == proc::MapAltRestriction::AT)
                rwleg.altRestriction.alt1 = std::min(rwleg.altRestriction.alt1, lastAltRestr.alt1);
            }

            atools::insertInto(legs.approachLegs, insertPosition, rwleg);

            // Coordinates for missed after CTL legs are already correct since this new leg is missing when the
            // coordinates are calculated
            // proc::MapProcedureLeg& mapLeg = legs[insertPosition + 1];
            // mapLeg.line.setPos1(rwleg.line.getPos1());
          }
          break;
        }
      }
    }

    // ====================================================================================
    // Add vector legs between manual and next one that do not overlap

    // Process in flying order
    for(int i = legs.size() - 2; i >= 0; i--)
    {
      // Look for the transition from approach to missed
      proc::MapProcedureLeg& prevLeg = legs[i];
      proc::MapProcedureLeg& nextLeg = legs[i + 1];

      if(nextLeg.isInitialFix() &&
         (prevLeg.type == proc::COURSE_TO_ALTITUDE ||
          prevLeg.type == proc::FIX_TO_ALTITUDE ||
          prevLeg.type == proc::HEADING_TO_ALTITUDE_TERMINATION ||
          prevLeg.type == proc::FROM_FIX_TO_MANUAL_TERMINATION ||
          prevLeg.type == proc::HEADING_TO_MANUAL_TERMINATION))
      {
        qDebug() << Q_FUNC_INFO << prevLeg;
        proc::MapProcedureLeg vectorLeg;
        vectorLeg.airportId = legs.ref.airportId;
        vectorLeg.approachId = legs.ref.approachId;
        vectorLeg.transitionId = legs.ref.transitionId;
        vectorLeg.navId = nextLeg.navId;

        vectorLeg.mapType = legs.mapType;
        vectorLeg.type = proc::VECTORS;

        // Use a generated id base on the previous leg id
        vectorLeg.legId = VECTOR_LEG_ID_BASE + nextLeg.legId;

        vectorLeg.altRestriction.descriptor = proc::MapAltRestriction::NONE;
        // geometry is populated later

        vectorLeg.fixPos = nextLeg.fixPos;
        vectorLeg.line = Line(prevLeg.line.getPos2(), nextLeg.line.getPos2());
        nextLeg.line.setPos1(nextLeg.line.getPos2());
        vectorLeg.time = vectorLeg.theta = vectorLeg.rho = 0.f;
        vectorLeg.magvar = nextLeg.magvar;
        vectorLeg.missed = vectorLeg.flyover =
          vectorLeg.trueCourse = vectorLeg.intercept =
            vectorLeg.disabled = vectorLeg.malteseCross = false;

        atools::insertInto(legs.approachLegs, i + 1, vectorLeg);
      } // if(nextLeg.type == proc::INITIAL_FIX && ...
    } // for(int i = legs.size() - 2; i >= 0; i--)
  } // if(!legs.isEmpty() && addArtificialLegs)
}

void ProcedureQuery::postProcessLegsForRoute(proc::MapProcedureLegs& starLegs,
                                             const proc::MapProcedureLegs& arrivalLegs,
                                             const map::MapAirport& airport)
{
  bool changed = false;

  // From procedure start to end
  for(int i = 0; i < starLegs.size(); i++)
  {
    proc::MapProcedureLeg& curLeg = starLegs[i];
    const proc::MapProcedureLeg *nextLeg = i < starLegs.size() - 1 ? &starLegs.at(i + 1) : nullptr;

    if(nextLeg == nullptr && !arrivalLegs.isEmpty())
      // Attach manual leg to arrival - otherwise to airport
      nextLeg = &arrivalLegs.first();

    if(contains(curLeg.type, {proc::FROM_FIX_TO_MANUAL_TERMINATION, proc::HEADING_TO_MANUAL_TERMINATION}))
    {
      qDebug() << Q_FUNC_INFO << "Correcting manual termination";
      if(nextLeg != nullptr)
        // Adjust geometry and attach it to the next approach leg
        curLeg.line = Line(curLeg.line.getPos1(), nextLeg->line.getPos1());
      else
      {
        // if(starLegs.runwayEnd.isValid())
        // curLeg.line = Line(curLeg.line.getPos1(), starLegs.runwayEnd.position);
        if(airport.isValid())
          // Attach end to airport
          curLeg.line = Line(curLeg.line.getPos1(), airport.position);
        else
          // Use fix position as last resort
          curLeg.line = Line(curLeg.line.getPos1(), curLeg.fixPos);
      }
      // geometry is updated in processLegsDistanceAndCourse
      // curLeg.geometry.clear();
      // curLeg.geometry << curLeg.line.getPos1() << curLeg.line.getPos2();

      // Clear ident to avoid display
      curLeg.fixIdent.clear();
      curLeg.fixRegion.clear();
      curLeg.fixType.clear();

      changed = true;

      qDebug() << Q_FUNC_INFO << "Corrected manual termination" << curLeg.line << curLeg.geometry;
    }
  }

  if(changed)
  {
    // Update distances and bounding rectangle
    processLegsDistanceAndCourse(starLegs);
    updateBounding(starLegs);
  }
}

void ProcedureQuery::processLegErrors(proc::MapProcedureLegs& legs) const
{
  legs.hasError = legs.hasHardError = false;
  for(int i = 1; i < legs.size(); i++)
  {
    legs.hasError |= legs.at(i).hasErrorRef();
    legs.hasHardError |= legs.at(i).hasHardErrorRef();
  }
}

void ProcedureQuery::processLegsFixRestrictions(const map::MapAirport& airport, proc::MapProcedureLegs& legs) const
{
  for(int i = 1; i < legs.size(); i++)
  {
    proc::MapProcedureLeg& leg = legs[i];
    proc::MapProcedureLeg& prevLeg = legs[i - 1];

    if(prevLeg.isTransition() && leg.isApproach() && leg.isInitialFix() && leg.fixIdent == prevLeg.fixIdent)
    {
      // Found the connection between transition and approach

      if(leg.altRestriction.isValid() && prevLeg.altRestriction.isValid() &&
         atools::almostEqual(leg.altRestriction.alt1, prevLeg.altRestriction.alt1))
        // Use restriction of the initial fix - erase restriction of the transition leg
        prevLeg.altRestriction.descriptor = proc::MapAltRestriction::NONE;

      if(leg.speedRestriction.isValid() && prevLeg.speedRestriction.isValid() &&
         atools::almostEqual(leg.speedRestriction.speed, prevLeg.speedRestriction.speed))
        // Use speed of the initial fix - erase restriction of the transition leg
        prevLeg.speedRestriction.descriptor = proc::MapSpeedRestriction::NONE;
    }

    if(leg.isFinalEndpointFix())
      // FEP has altitude above TDZ - ignore this here
      leg.altRestriction.descriptor = proc::MapAltRestriction::NONE;

    if(prevLeg.isApproach() && leg.isMissed() && prevLeg.altRestriction.isValid())
    {
      // Last leg before missed approach - usually runway
      // Correct restriction where it is wrongly below airport altitude for some

      if(prevLeg.altRestriction.alt1 < airport.position.getAltitude())
      {
        prevLeg.altRestriction.alt1 = std::ceil(airport.position.getAltitude());
        qWarning() << Q_FUNC_INFO << "Final leg altitude below airport altitude" << airport.ident;
      }
    }
  }
}

void ProcedureQuery::processLegsFafAndFacf(proc::MapProcedureLegs& legs) const
{
  if(legs.mapType & proc::PROCEDURE_APPROACH_ALL_MISSED)
  {
    int fafIndex = map::INVALID_INDEX_VALUE, facfIndex = map::INVALID_INDEX_VALUE;

    for(int i = 0; i < legs.size(); i++)
    {
      const proc::MapProcedureLeg& leg = legs.at(i);
      if(leg.isFinalApproachCourseFix() && leg.altRestriction.descriptor != proc::MapAltRestriction::ILS_AT &&
         leg.altRestriction.descriptor != proc::MapAltRestriction::ILS_AT_OR_ABOVE)
        facfIndex = i;
      if(leg.isFinalApproachFix() && leg.altRestriction.descriptor != proc::MapAltRestriction::ILS_AT &&
         leg.altRestriction.descriptor != proc::MapAltRestriction::ILS_AT_OR_ABOVE)
        fafIndex = i;
    }

    if(fafIndex < map::INVALID_INDEX_VALUE && facfIndex < map::INVALID_INDEX_VALUE)
      legs[fafIndex].malteseCross = true;
    else if(facfIndex < map::INVALID_INDEX_VALUE)
      legs[facfIndex].malteseCross = true;
    else if(fafIndex < map::INVALID_INDEX_VALUE)
      legs[fafIndex].malteseCross = true;
  }
}

void ProcedureQuery::processLegsDistanceAndCourse(proc::MapProcedureLegs& legs) const
{
  legs.transitionDistance = 0.f;
  legs.approachDistance = 0.f;
  legs.missedDistance = 0.f;

  for(int i = 0; i < legs.size(); i++)
  {
    proc::MapProcedureLeg& leg = legs[i];
    proc::ProcedureLegType type = leg.type;
    const proc::MapProcedureLeg *prevLeg = i > 0 ? &legs[i - 1] : nullptr;

    leg.geometry.clear();

    if(!leg.line.isValid())
      qWarning() << "leg line for leg is invalid" << leg;

    // ===========================================================
    else if(leg.isInitialFix())
    {
      leg.calculatedDistance = 0.f;
      leg.calculatedTrueCourse = map::INVALID_COURSE_VALUE;
      leg.geometry << leg.line.getPos1() << leg.line.getPos2();
    }
    else if(type == proc::START_OF_PROCEDURE)
    {
      if(leg.mapType & proc::PROCEDURE_DEPARTURE)
      {
        // START_OF_PROCEDURE is an actual leg for departure where it connects runway and initial fix
        leg.calculatedDistance = meterToNm(leg.line.lengthMeter());
        leg.calculatedTrueCourse = normalizeCourse(leg.line.angleDeg());
      }
      else
      {
        leg.calculatedDistance = 0.f;
        leg.calculatedTrueCourse = map::INVALID_COURSE_VALUE;
      }
      leg.geometry << leg.line.getPos1() << leg.line.getPos2();
    }
    else if(contains(type, {proc::ARC_TO_FIX, proc::CONSTANT_RADIUS_ARC}))
    {
      if(leg.recFixPos.isValid())
      {
        Line line;
        if(leg.correctedArc && type == proc::ARC_TO_FIX && prevLeg != nullptr)
          // Arc entry with stub
          line = Line(leg.interceptPos, leg.line.getPos2());
        else
          line = leg.line;

        // Build geometry
        ageo::calcArcLength(line, leg.recFixPos, leg.turnDirection == "L", &leg.calculatedDistance, &leg.geometry);

        leg.calculatedDistance = meterToNm(leg.calculatedDistance);
        leg.calculatedTrueCourse = map::INVALID_COURSE_VALUE;

        if(leg.correctedArc && type == proc::ARC_TO_FIX && prevLeg != nullptr)
        {
          // Corrected first position of DME arc - adjust geometry and distance for entry segment
          leg.geometry.prepend(prevLeg->line.getPos2());
          leg.calculatedDistance += meterToNm(prevLeg->line.getPos2().distanceMeterTo(leg.interceptPos));
        }
      }
      else
      {
        leg.calculatedDistance = meterToNm(leg.line.lengthMeter());
        leg.calculatedTrueCourse = normalizeCourse(leg.line.angleDeg());
        leg.geometry << leg.line.getPos1() << leg.line.getPos2();
        qWarning() << "ARC_TO_FIX or CONSTANT_RADIUS_ARC has invalid recommended fix" << leg;
      }
    }
    // ===========================================================
    else if(type == proc::COURSE_TO_FIX || type == proc::CUSTOM_APP_RUNWAY || type == proc::CUSTOM_DEP_END)
    {
      if(leg.interceptPos.isValid())
      {
        leg.geometry << leg.line.getPos1() << leg.interceptPos << leg.line.getPos2();
        leg.calculatedDistance = meterToNm(leg.geometry.lengthMeter());
        leg.calculatedTrueCourse = normalizeCourse(leg.interceptPos.angleDegTo(leg.line.getPos2()));
      }
      else
      {
        leg.geometry << prevLeg->line.getPos2() << leg.line.getPos1() << leg.line.getPos2();
        leg.calculatedDistance = meterToNm(leg.geometry.lengthMeter());
        leg.calculatedTrueCourse = normalizeCourse(leg.line.angleDeg());
      }
    }
    // ===========================================================
    else if(type == proc::PROCEDURE_TURN)
    {
      // Distance is towards turn point
      leg.calculatedDistance = meterToNm(leg.line.getPos1().distanceMeterTo(leg.procedureTurnPos));

      // if(nextLeg != nullptr)
      // leg.calculatedDistance += meterToNm(leg.procedureTurnPos.distanceMeterTo(nextLeg->line.getPos1()));

      // Course from fix to turn point
      leg.calculatedTrueCourse = normalizeCourse(leg.course + (leg.turnDirection == "L" ? -45.f : 45.f) + leg.magvar);

      leg.geometry << leg.line.getPos1() << leg.procedureTurnPos;
    }
    // ===========================================================
    else if(contains(type, {proc::COURSE_TO_ALTITUDE, proc::FIX_TO_ALTITUDE,
                            proc::HEADING_TO_ALTITUDE_TERMINATION,
                            proc::FROM_FIX_TO_MANUAL_TERMINATION, proc::HEADING_TO_MANUAL_TERMINATION}))
    {
      leg.calculatedDistance = meterToNm(leg.line.lengthMeter());
      leg.calculatedTrueCourse = normalizeCourse(leg.line.angleDeg());
      leg.geometry << leg.line.getPos1() << leg.line.getPos2();
    }
    // ===========================================================
    else if(type == proc::TRACK_FROM_FIX_FROM_DISTANCE)
    {
      leg.calculatedDistance = leg.distance;
      leg.calculatedTrueCourse = normalizeCourse(leg.line.angleDeg());
      leg.geometry << leg.line.getPos1() << leg.line.getPos2();
    }
    // ===========================================================
    else if(contains(type, {proc::HOLD_TO_MANUAL_TERMINATION, proc::HOLD_TO_FIX, proc::HOLD_TO_ALTITUDE}))
    {
      leg.calculatedDistance = meterToNm(leg.line.lengthMeter());
      leg.calculatedTrueCourse = leg.legTrueCourse();
      leg.geometry << leg.line.getPos1() << leg.line.getPos2();

      float segmentLength;
      if(leg.time > 0.f)
        // 3.5 nm per minute
        segmentLength = leg.time * 3.5f;
      else if(leg.distance > 0.f)
        segmentLength = leg.distance;
      else
        segmentLength = 3.5f;

      leg.holdLine.setPos2(leg.line.getPos1());
      leg.holdLine.setPos1(leg.line.getPos1().endpoint(nmToMeter(segmentLength), opposedCourseDeg(leg.calculatedTrueCourse)));
    }
    else if(contains(type, {proc::TRACK_FROM_FIX_TO_DME_DISTANCE, proc::COURSE_TO_DME_DISTANCE,
                            proc::HEADING_TO_DME_DISTANCE_TERMINATION,
                            proc::COURSE_TO_RADIAL_TERMINATION, proc::HEADING_TO_RADIAL_TERMINATION,
                            proc::DIRECT_TO_FIX, proc::TRACK_TO_FIX, proc::VECTORS,
                            proc::COURSE_TO_INTERCEPT, proc::HEADING_TO_INTERCEPT,
                            proc::DIRECT_TO_RUNWAY, proc::CUSTOM_DEP_RUNWAY, proc::CIRCLE_TO_LAND, proc::STRAIGHT_IN}))
    {
      leg.calculatedDistance = meterToNm(leg.line.lengthMeter());
      leg.calculatedTrueCourse = normalizeCourse(leg.line.angleDeg());
      leg.geometry << leg.line.getPos1() << leg.line.getPos2();
    }

    if(prevLeg != nullptr && !leg.intercept && leg.isInitialFix())
    {
      // Add distance from any existing gaps, bows or turns except for intercept legs
      // Use first position (MAP) of last leg for circle-to-land approaches
      Pos lastPos = (prevLeg->isCircleToLand() || prevLeg->isStraightIn()) &&
                    leg.isMissed() ? prevLeg->line.getPos1() : prevLeg->line.getPos2();
      leg.calculatedDistance += meterToNm(lastPos.distanceMeterTo(leg.line.getPos1()));
    }

    if(leg.calculatedDistance >= map::INVALID_DISTANCE_VALUE / 2)
      leg.calculatedDistance = 0.f;
    if(leg.calculatedTrueCourse >= map::INVALID_COURSE_VALUE / 2)
      leg.calculatedTrueCourse = map::INVALID_COURSE_VALUE;

    if(leg.isAnyTransition())
      legs.transitionDistance += leg.calculatedDistance;

    if(leg.isApproach() || leg.isStar() || leg.isSid())
      legs.approachDistance += leg.calculatedDistance;

    if(leg.isMissed())
      legs.missedDistance += leg.calculatedDistance;

    leg.geometry.removeDuplicates();
  }
}

void ProcedureQuery::processLegs(proc::MapProcedureLegs& legs) const
{
  // Assumptions: 3.5 nm per min
  // Climb 500 ft/min
  // Intercept 30 for localizers and 30-45 for others

  // Leg will be drawn from lastPos to curPos
  Pos lastPos;

  // Iterate from start of procedure to end. E.g. from IF to airport for approaches and STAR and
  // From airport to last fix for SID
  for(int i = 0; i < legs.size(); ++i)
  {
    if(legs.mapType & proc::PROCEDURE_DEPARTURE && i == 0)
      lastPos = legs.runwayEnd.position;

    Pos curPos;
    proc::MapProcedureLeg& leg = legs[i];
    proc::ProcedureLegType type = leg.type;

    // ===========================================================
    if(type == proc::ARC_TO_FIX)
    {
      curPos = leg.fixPos;

      // Check if first leg position matches distance to navaid - modify entry point to match distance if not
      if(lastPos.isValid() && atools::almostNotEqual(leg.rho, meterToNm(lastPos.distanceMeterTo(leg.recFixPos)), 0.5f))
      {
        // Get point at correct distance to navaid between fix and recommended fix
        leg.interceptPos = leg.recFixPos.endpoint(nmToMeter(leg.rho), leg.recFixPos.angleDegTo(lastPos));
        leg.correctedArc = true;
      }

      leg.displayText << (leg.recFixIdent % tr("/") % Unit::distNm(leg.rho, true, 20, true) % tr("/") %
                          QLocale().toString(leg.theta, 'f', 0) % tr("°M"));
      leg.remarks << tr("DME %1").arg(Unit::distNm(leg.rho, true, 20, true));
    }
    // ===========================================================
    else if(type == proc::COURSE_TO_FIX || type == proc::CUSTOM_APP_RUNWAY || type == proc::CUSTOM_DEP_END)
    {
      // Calculate the leading extended position to the fix - this is the from position
      Pos extended = leg.fixPos.endpoint(nmToMeter(leg.distance > 0 ? leg.distance : 1.f /* Fix for missing dist */),
                                         opposedCourseDeg(leg.legTrueCourse()));

      ageo::LineDistance result;
      lastPos.distanceMeterToLine(extended, leg.fixPos, result);

      // Check if lines overlap or are close to each other.
      // Connect if close to each other - if not calculate an intercept position
      if(std::abs(result.distance) > nmToMeter(1.f))
      {
        // Extended position leading towards the fix which is far away from last fix - calculate an intercept position
        float legCourse = leg.legTrueCourse();

        // Calculate course difference between last leg and this one
        const proc::MapProcedureLeg *lastLeg = i > 0 ? &legs.at(i - 1) : nullptr;
        float lastLegCourse = map::INVALID_COURSE_VALUE;

        if(lastLeg != nullptr)
        {
          if(lastLeg->isInitialFix())
          {
            // Initial fix has no valid course - assume course to center of next leg line
            atools::geo::Pos center = extended.interpolate(leg.fixPos, 0.5f);
            lastLegCourse = lastLeg->fixPos.angleDegTo(center);
          }
          else if(lastLeg->isCircular())
          {
            // Calculate an geometry approximation and get the course from the last line in the geometry
            ageo::LineString lastGeometry;
            ageo::calcArcLength(lastLeg->line, lastLeg->recFixPos, lastLeg->turnDirection == "L",
                                nullptr, &lastGeometry);
            if(lastGeometry.size() >= 2)
              lastLegCourse =
                lastGeometry.at(lastGeometry.size() - 2).angleDegTo(lastGeometry.at(lastGeometry.size() - 1));
          }

          if(!(lastLegCourse < map::INVALID_COURSE_VALUE))
            // No circular or too small geometry - use default line
            lastLegCourse = lastLeg->line.angleDeg();
        }
        float courseDiff = map::INVALID_COURSE_VALUE;
        if(lastLegCourse < map::INVALID_COURSE_VALUE / 2)
          courseDiff = ageo::angleAbsDiff(legCourse, lastLegCourse);

        Pos intersect;

#ifndef DEBUG_DRAW_ALWAYS_BOW
        // Check if this is a reversal maneuver which should be connected with a bow instead of an intercept
        // Always intercept if course could not be calculated (e.g. first procedure leg)
        // Everything bigger than 150 degree is considered a reversal - draw bow instead of intercept
        if(courseDiff < 150. || !(courseDiff < map::INVALID_COURSE_VALUE))
        {
          // Try left or right intercept
          Pos intr1 = Pos::intersectingRadials(extended, legCourse, lastPos, legCourse - 45.f);
          Pos intr2 = Pos::intersectingRadials(extended, legCourse, lastPos, legCourse + 45.f);

          // Use whatever course is shorter - calculate distance to interception candidates
          float dist1 = intr1.distanceMeterTo(lastPos);
          float dist2 = intr2.distanceMeterTo(lastPos);
          if(dist1 < dist2)
            intersect = intr1;
          else
            intersect = intr2;
        }
#endif

        if(intersect.isValid())
        {
          intersect.distanceMeterToLine(extended, leg.fixPos, result);

          if(result.status == ageo::ALONG_TRACK)
          {
            // Leg intercepted - set point for drawing
            leg.interceptPos = intersect;
          }
          else if(result.status == ageo::AFTER_END)
          {
            // Fly to fix - end of leg

            if(contains(leg.turnDirection, {"L", "R"}))
            {
              float extDist = extended.distanceMeterTo(lastPos);
              if(extDist > nmToMeter(1.f))
                // Draw large bow to extended position or allow intercept of leg
                lastPos = extended;
            }
            // else turn
          }
          else if(result.status == ageo::BEFORE_START)
          {
            // Fly to start of leg
            lastPos = extended;
          }
          else
            qWarning() << "leg line type" << leg.type << "fix" << leg.fixIdent
                       << "invalid cross track"
                       << "approachId" << leg.approachId
                       << "transitionId" << leg.transitionId << "legId" << leg.legId;
        } // intersect.isValid()
        else
        {
          // No intercept point in reasonable distance found
          float extDist = extended.distanceMeterTo(lastPos);
          if(extDist > nmToMeter(1.f))
            // Draw large bow to extended position or allow intercept of leg
            lastPos = extended;
        }
      } // if(std::abs(result.distance) > nmToMeter(1.f))

      if(leg.interceptPos.isValid())
        // Add intercept for display
        leg.displayText << tr("Intercept") << tr("Course to Fix");

      curPos = leg.fixPos;
    }
    // ===========================================================
    else if(leg.isInitialFix() || contains(type, {proc::DIRECT_TO_FIX, proc::START_OF_PROCEDURE, proc::TRACK_TO_FIX,
                                                  proc::CONSTANT_RADIUS_ARC, proc::VECTORS, proc::DIRECT_TO_RUNWAY, proc::CUSTOM_DEP_RUNWAY,
                                                  proc::CIRCLE_TO_LAND, proc::STRAIGHT_IN}))
    {
      curPos = leg.fixPos;
    }
    // ===========================================================
    else if(type == proc::PROCEDURE_TURN)
    {
      float course;
      if(leg.turnDirection == "L")
        // Turn right and then turn 180 deg left
        course = leg.legTrueCourse() - 45.f;
      else
        // Turn left and then turn 180 deg right
        course = leg.legTrueCourse() + 45.f;

      leg.procedureTurnPos = leg.fixPos.endpoint(nmToMeter(leg.distance), course);
      lastPos = leg.fixPos;
      curPos = leg.procedureTurnPos;
    }
    // ===========================================================
    else if(contains(type, {proc::COURSE_TO_ALTITUDE, proc::FIX_TO_ALTITUDE,
                            proc::HEADING_TO_ALTITUDE_TERMINATION}))
    {
      // TODO calculate distance by altitude
      Pos start = lastPos.isValid() ? lastPos : leg.fixPos;

      if(!start.isValid() && legs.mapType & proc::PROCEDURE_DEPARTURE && legs.runwayEnd.isValid())
        start = legs.runwayEnd.position;

      if(!lastPos.isValid())
        lastPos = start;
      curPos = start.endpoint(nmToMeter(2.f), leg.legTrueCourse());
      leg.displayText << tr("Altitude");
    }
    // ===========================================================
    else if(contains(type, {proc::COURSE_TO_RADIAL_TERMINATION, proc::HEADING_TO_RADIAL_TERMINATION}))
    {
      // Distance to recommended fix for radial
      float distToRecMeter = lastPos.distanceMeterTo(leg.recFixPos);

      // Create a course line from start position with the given course
      Line crsLine(lastPos, distToRecMeter * 10.f, leg.legTrueCourse());

      // Create base distance for parallel line after turn to leg course
      float parallelDist = 0.f;
      if(leg.turnDirection == "L")
        parallelDist = nmToMeter(2.f);
      else if(leg.turnDirection == "R")
        parallelDist = nmToMeter(-2.f);

      // Now create parallel lines based on the course line until a valid intersection with the radial can be found
      // Move parallel one step away from course line and lengthen parallel a bit for each iteration
      Pos intersect;
      Line parallel;
      bool valid = false;
      float ext = 0.f;
      for(int j = 0; j < 5; j++)
      {
        parallel = crsLine.parallel(parallelDist).extended(ext, ext);
        intersect = Pos::intersectingRadials(parallel.getPos1(), parallel.angleDeg(), leg.recFixPos, leg.theta + leg.magvar);

        // Need maximum of 200 NM and minimum of 1.5 NM distance to the navaid from the intersection point
        if(intersect.isValid() && intersect.distanceMeterTo(leg.recFixPos) < nmToMeter(200.f) &&
           intersect.distanceMeterTo(leg.recFixPos) > nmToMeter(1.5f))
        {
          valid = true;
          break;
        }
        // Move away from base
        parallelDist *= 1.2f;

        // Make half a NM longer
        ext += 0.5f;
      }

      if(valid)
      {
        lastPos = parallel.getPos1();
        curPos = intersect;
        leg.displayText << (leg.recFixIdent % "/" % QLocale().toString(leg.theta, 'f', 0) % tr("°M"));
      }
      else
      {
        curPos = lastPos;
        qWarning() << "leg line type" << type << "fix" << leg.fixIdent << "no intersectingRadials found"
                   << "approachId" << leg.approachId << "transitionId" << leg.transitionId << "legId" << leg.legId;
      }
    }
    // ===========================================================
    else if(type == proc::TRACK_FROM_FIX_FROM_DISTANCE)
    {
      if(!lastPos.isValid())
        lastPos = leg.fixPos;

      curPos = leg.fixPos.endpoint(nmToMeter(leg.distance), leg.legTrueCourse());

      leg.displayText << (leg.fixIdent % "/" % Unit::distNm(leg.distance, true, 20, true) % "/" %
                          QLocale().toString(leg.course, 'f', 0) % (leg.trueCourse ? tr("°T") : tr("°M")));
    }
    // ===========================================================
    else if(contains(type, {proc::TRACK_FROM_FIX_TO_DME_DISTANCE, proc::COURSE_TO_DME_DISTANCE,
                            proc::HEADING_TO_DME_DISTANCE_TERMINATION}))
    {
      Pos start, center;
      if(type == proc::TRACK_FROM_FIX_TO_DME_DISTANCE)
      {
        // Leg that has a fix as origin
        start = leg.fixPos;
        center = leg.recFixPos.isValid() ? leg.recFixPos : leg.fixPos;
      }
      else
      {
        // These legs usually have no fix - use last position
        start = lastPos.isValid() ? lastPos : (leg.fixPos.isValid() ? leg.fixPos : leg.recFixPos);

        // Recommended is DME navaid - use if available
        center = leg.recFixPos.isValid() ? leg.recFixPos : leg.fixPos;
      }

      // Distance from fix to navaid
      float distMeter = start.distanceMeterTo(center);

      // Requested DME distance
      float legDistMeter = nmToMeter(leg.distance);

      // Create a extended line to calculate the intersection with the DME distance
      Line line(start, start.endpoint(distMeter + legDistMeter * 4, leg.legTrueCourse()));

      if(!lastPos.isValid())
        lastPos = start;

      Pos intersect = line.intersectionWithCircle(center, legDistMeter, 10.f);
      if(intersect.isValid())
        curPos = intersect;
      else
      {
        curPos = center;
        qWarning() << "leg line type" << type << "fix" << leg.fixIdent << "no intersectionWithCircle found"
                   << "approachId" << leg.approachId << "transitionId" << leg.transitionId << "legId" << leg.legId;
      }

      leg.displayText << (leg.recFixIdent % "/" % Unit::distNm(leg.distance, true, 20, true) % "/" %
                          QLocale().toString(leg.course, 'f', 0) % (leg.trueCourse ? tr("°T") : tr("°M")));
    }
    // ===========================================================
    else if(contains(type, {proc::FROM_FIX_TO_MANUAL_TERMINATION, proc::HEADING_TO_MANUAL_TERMINATION}))
    {
      if(leg.fixPos.isValid())
      {
        if(leg.course > 0)
          // Use an extended line from fix with the given course as geometry
          curPos = leg.fixPos.endpoint(nmToMeter(leg.distance > 0.f ? leg.distance : 3.f),
                                       leg.legTrueCourse());
        else
          curPos = leg.fixPos;
      }
      else
        // Use an extended line from last position with the given course as geometry
        curPos = lastPos.endpoint(nmToMeter(leg.distance > 0.f ? leg.distance : 3.f), leg.legTrueCourse());

      // Geometry might be changed later in postProcessLegsForRoute()

      // Do not draw ident for manual legs
      leg.fixIdent.clear();
      leg.fixRegion.clear();
      leg.fixType.clear();

      leg.displayText << tr("Manual");
    }
    // ===========================================================
    else if(type == proc::HOLD_TO_ALTITUDE)
    {
      curPos = leg.fixPos;
      leg.displayText << tr("Altitude");
    }
    // ===========================================================
    else if(type == proc::HOLD_TO_FIX)
    {
      curPos = leg.fixPos;
      leg.displayText << tr("Single");
    }
    // ===========================================================
    else if(type == proc::HOLD_TO_MANUAL_TERMINATION)
    {
      curPos = leg.fixPos;
      leg.displayText << tr("Manual");
    }

    // Processed later: COURSE_TO_INTERCEPT

    if(legs.mapType & proc::PROCEDURE_DEPARTURE && i == 0)
      // First leg of a SID start at runway end
      leg.line = Line(legs.runwayEnd.position, curPos);
    else
      leg.line = Line(lastPos.isValid() ? lastPos : curPos, curPos);

    if(!leg.line.isValid())
      qWarning() << "leg line type" << type << "fix" << leg.fixIdent << "invalid line"
                 << "approachId" << leg.approachId << "transitionId" << leg.transitionId << "legId" << leg.legId;
    lastPos = curPos;
  }
}

void ProcedureQuery::processCourseInterceptLegs(proc::MapProcedureLegs& legs) const
{
  for(int i = 0; i < legs.size(); ++i)
  {
    proc::MapProcedureLeg& leg = legs[i];
    proc::MapProcedureLeg *prevLeg = i > 0 ? &legs[i - 1] : nullptr;
    proc::MapProcedureLeg *nextLeg = i < legs.size() - 1 ? &legs[i + 1] : nullptr;
    proc::MapProcedureLeg *secondNextLeg = i < legs.size() - 2 ? &legs[i + 2] : nullptr;

    if(contains(leg.type, {proc::COURSE_TO_INTERCEPT, proc::HEADING_TO_INTERCEPT}))
    {
      if(nextLeg != nullptr)
      {
        proc::MapProcedureLeg *next = nextLeg->isInitialFix() ? secondNextLeg : nextLeg;

        if(nextLeg->isInitialFix())
          // Do not show the cut-off initial fix
          nextLeg->disabled = true;

        if(next != nullptr)
        {
          bool nextIsCircular = next->isCircular();
          Pos start = prevLeg != nullptr ? prevLeg->line.getPos2() : leg.fixPos;

          if(prevLeg != nullptr && (prevLeg->isCircleToLand() || prevLeg->isStraightIn()) && leg.isMissed())
            // Use first position (MAP) of last leg for circle-to-land approaches
            start = prevLeg->line.getPos1();

          Pos intersect;
          if(nextIsCircular)
          {
            Line line(start, start.endpoint(nmToMeter(200), leg.legTrueCourse()));
            intersect = line.intersectionWithCircle(next->recFixPos, nmToMeter(next->rho), 20);
          }
          else
            intersect =
              Pos::intersectingRadials(start, leg.legTrueCourse(), next->line.getPos1(),
                                       // Leg might have no course and calculated is not available yet
                                       atools::almostEqual(next->course, 0.f) ||
                                       !(next->course < map::INVALID_COURSE_VALUE) ?
                                       next->line.angleDeg() : next->legTrueCourse());

          leg.line.setPos1(start);

          if(intersect.isValid() && intersect.distanceMeterTo(start) < nmToMeter(200.f))
          {
            ageo::LineDistance result;

            next->line.distanceMeterToLine(intersect, result);

            if(result.status == ageo::ALONG_TRACK)
            {
              // Intercepting the next leg
              next->intercept = true;
              next->line.setPos1(intersect);

              leg.line.setPos2(intersect);
              leg.displayText << tr("Intercept");

              if(nextIsCircular)
                leg.displayText << (next->recFixIdent % "/" % Unit::distNm(next->rho, true, 20, true));
              else
                leg.displayText << tr("Leg");
            }
            else if(result.status == ageo::BEFORE_START)
            {
              // Link directly to start of next leg
              next->intercept = true;
              leg.line.setPos2(next->line.getPos1());
              leg.displayText << tr("Intercept");
            }
            else if(result.status == ageo::AFTER_END)
            {
              // Link directly to end of next leg
              next->intercept = true;
              leg.line.setPos2(next->line.getPos2());
              next->line.setPos1(next->line.getPos2());
              next->line.setPos2(next->line.getPos2());
              leg.displayText << tr("Intercept");
            }
            else
              qWarning() << "leg line type" << leg.type << "fix" << leg.fixIdent
                         << "invalid cross track"
                         << "approachId" << leg.approachId
                         << "transitionId" << leg.transitionId << "legId" << leg.legId;

          }
          else
          {
            qWarning() << "leg line type" << leg.type << "fix" << leg.fixIdent
                       << "no intersectingRadials/intersectionWithCircle found"
                       << "approachId" << leg.approachId << "transitionId" << leg.transitionId << "legId" << leg.legId;
            leg.displayText << tr("Intercept") << tr("Leg");
            leg.line.setPos2(next->line.getPos1());
          }
        }
      }
    }
  }
}

void ProcedureQuery::initQueries()
{
  airportQueryNav = NavApp::getAirportQueryNav();

  deInitQueries();

  approachLegQuery = new SqlQuery(dbNav);
  approachLegQuery->prepare("select * from approach_leg where approach_id = :id "
                            "order by approach_leg_id");

  transitionLegQuery = new SqlQuery(dbNav);
  transitionLegQuery->prepare("select * from transition_leg where transition_id = :id "
                              "order by transition_leg_id");

  transitionIdForLegQuery = new SqlQuery(dbNav);
  transitionIdForLegQuery->prepare("select transition_id as id from transition_leg where transition_leg_id = :id");

  approachIdForTransQuery = new SqlQuery(dbNav);
  approachIdForTransQuery->prepare("select approach_id from transition where transition_id = :id");

  runwayEndIdQuery = new SqlQuery(dbNav);
  runwayEndIdQuery->prepare("select e.runway_end_id from approach a "
                            "join runway_end e on a.runway_end_id = e.runway_end_id where approach_id = :id");

  transitionQuery = new SqlQuery(dbNav);
  transitionQuery->prepare("select type, fix_ident from transition where transition_id = :id");

  approachQuery = new SqlQuery(dbNav);
  approachIdByNameQuery = new SqlQuery(dbNav);

  if(dbNav->record("approach").contains("arinc_name"))
  {
    approachQuery->prepare("select type, arinc_name, suffix, has_gps_overlay, fix_ident, runway_name "
                           "from approach where approach_id = :id");

    approachIdByNameQuery->prepare("select approach_id, arinc_name, suffix, runway_name from approach "
                                   "where fix_ident like :fixident and type like :type and airport_ident = :apident");

    approachIdByArincNameQuery = new SqlQuery(dbNav);
    approachIdByArincNameQuery->prepare("select approach_id, suffix, arinc_name, runway_name from approach "
                                        "where arinc_name like :arincname and airport_ident = :apident");
  }
  else
  {
    approachQuery->prepare("select type, suffix, has_gps_overlay, fix_ident, runway_name "
                           "from approach where approach_id = :id");

    approachIdByNameQuery->prepare("select approach_id, suffix, runway_name from approach "
                                   "where fix_ident like :fixident and type like :type and airport_ident = :apident");

    // Leave ARINC name query as null
  }

  transitionIdByNameQuery = new SqlQuery(dbNav);
  transitionIdByNameQuery->prepare("select transition_id from transition where fix_ident like :fixident and "
                                   "type like :type and approach_id = :apprid");

  transitionIdsForApproachQuery = new SqlQuery(dbNav);
  transitionIdsForApproachQuery->prepare("select transition_id from transition where approach_id = :id");
}

void ProcedureQuery::deInitQueries()
{
  procedureCache.clear();
  transitionCache.clear();
  procedureLegIndex.clear();
  transitionLegIndex.clear();

  delete approachLegQuery;
  approachLegQuery = nullptr;

  delete transitionLegQuery;
  transitionLegQuery = nullptr;

  delete transitionIdForLegQuery;
  transitionIdForLegQuery = nullptr;

  delete approachIdForTransQuery;
  approachIdForTransQuery = nullptr;

  delete runwayEndIdQuery;
  runwayEndIdQuery = nullptr;

  delete transitionQuery;
  transitionQuery = nullptr;

  delete approachQuery;
  approachQuery = nullptr;

  delete approachIdByNameQuery;
  approachIdByNameQuery = nullptr;

  delete approachIdByArincNameQuery;
  approachIdByArincNameQuery = nullptr;

  delete transitionIdByNameQuery;
  transitionIdByNameQuery = nullptr;

  delete transitionIdsForApproachQuery;
  transitionIdsForApproachQuery = nullptr;
}

void ProcedureQuery::clearFlightplanProcedureProperties(QHash<QString, QString>& properties,
                                                        const proc::MapProcedureTypes& type)
{
  if(type & proc::PROCEDURE_SID)
  {
    properties.remove(pln::SIDAPPR);
    properties.remove(pln::SIDAPPRRW);
    properties.remove(pln::SIDTYPE);
    properties.remove(pln::DEPARTURE_CUSTOM_DISTANCE);
  }

  if(type & proc::PROCEDURE_SID_TRANSITION)
    properties.remove(pln::SIDTRANS);

  if(type & proc::PROCEDURE_STAR)
  {
    properties.remove(pln::STAR);
    properties.remove(pln::STARRW);
  }

  if(type & proc::PROCEDURE_STAR_TRANSITION)
    properties.remove(pln::STARTRANS);

  if(type & proc::PROCEDURE_TRANSITION)
  {
    properties.remove(pln::TRANSITION);
    properties.remove(pln::TRANSITIONTYPE);
  }

  if(type & proc::PROCEDURE_APPROACH)
  {
    properties.remove(pln::APPROACH);
    properties.remove(pln::APPROACH_ARINC);
    properties.remove(pln::APPROACHTYPE);
    properties.remove(pln::APPROACHRW);
    properties.remove(pln::APPROACHSUFFIX);
    properties.remove(pln::APPROACH_CUSTOM_DISTANCE);
    properties.remove(pln::APPROACH_CUSTOM_ALTITUDE);
    properties.remove(pln::APPROACH_CUSTOM_OFFSET);
  }
}

void ProcedureQuery::fillFlightplanProcedureProperties(QHash<QString, QString>& properties,
                                                       const proc::MapProcedureLegs& arrivalLegs,
                                                       const proc::MapProcedureLegs& starLegs,
                                                       const proc::MapProcedureLegs& sidLegs)
{
  if(!sidLegs.isEmpty())
  {
    if(sidLegs.approachType == "CUSTOMDEPART")
    {
      properties.insert(pln::DEPARTURE_CUSTOM_DISTANCE, QString::number(sidLegs.customDistance, 'f', 2));
      properties.insert(pln::SIDTYPE, sidLegs.approachType);
    }

    if(!sidLegs.transitionFixIdent.isEmpty())
      properties.insert(pln::SIDTRANS, sidLegs.transitionFixIdent);

    if(!sidLegs.approachFixIdent.isEmpty())
      properties.insert(pln::SIDAPPR, sidLegs.approachFixIdent);

    if(!sidLegs.procedureRunway.isEmpty())
      properties.insert(pln::SIDAPPRRW, sidLegs.procedureRunway);
  }

  if(!starLegs.isEmpty())
  {
    if(!starLegs.transitionFixIdent.isEmpty())
      properties.insert(pln::STARTRANS, starLegs.transitionFixIdent);

    if(!starLegs.isEmpty() && !starLegs.approachFixIdent.isEmpty())
    {
      properties.insert(pln::STAR, starLegs.approachFixIdent);
      properties.insert(pln::STARRW, starLegs.runwayEnd.name);
    }
  }

  if(!arrivalLegs.isEmpty())
  {
    if(arrivalLegs.approachType == "CUSTOM")
    {
      properties.insert(pln::APPROACH_CUSTOM_DISTANCE, QString::number(arrivalLegs.customDistance, 'f', 2));
      properties.insert(pln::APPROACH_CUSTOM_ALTITUDE, QString::number(arrivalLegs.customAltitude, 'f', 2));
      properties.insert(pln::APPROACH_CUSTOM_OFFSET, QString::number(arrivalLegs.customOffset, 'f', 2));
    }

    if(!arrivalLegs.transitionFixIdent.isEmpty())
    {
      properties.insert(pln::TRANSITION, arrivalLegs.transitionFixIdent);
      properties.insert(pln::TRANSITIONTYPE, arrivalLegs.transitionType);
    }

    if(!arrivalLegs.approachFixIdent.isEmpty())
    {
      properties.insert(pln::APPROACH, arrivalLegs.approachFixIdent);
      properties.insert(pln::APPROACH_ARINC, arrivalLegs.approachArincName);
      properties.insert(pln::APPROACHTYPE, arrivalLegs.approachType);
      properties.insert(pln::APPROACHRW, arrivalLegs.procedureRunway);
      properties.insert(pln::APPROACHSUFFIX, arrivalLegs.approachSuffix);
    }
  }
}

int ProcedureQuery::getSidId(map::MapAirport departure, const QString& sid,
                             const QString& runway, bool strict)
{
  NavApp::getMapQueryGui()->getAirportNavReplace(departure);

  int sidApprId = -1;
  // Get a SID id =================================================================
  if(!sid.isEmpty())
  {
    approachIdByNameQuery->bindValue(":fixident", sid);
    approachIdByNameQuery->bindValue(":type", "GPS");
    approachIdByNameQuery->bindValue(":apident", departure.ident);

    sidApprId = findApproachId(departure, approachIdByNameQuery, "D", runway, strict);

    if(sidApprId == -1)
      // Try again without runway
      sidApprId = findApproachId(departure, approachIdByNameQuery, "D", QString(), strict);

    if(sidApprId == -1)
      qWarning() << "Loading of SID" << sid << "failed";
  }
  return sidApprId;
}

int ProcedureQuery::getSidTransitionId(map::MapAirport departure, const QString& sidTrans, int sidId, bool strict)
{
  NavApp::getMapQueryGui()->getAirportNavReplace(departure);

  int sidTransId = -1;
  // Get a SID transition id =================================================================
  if(!sidTrans.isEmpty() && sidId != -1)
  {
    transitionIdByNameQuery->bindValue(":fixident", sidTrans);
    transitionIdByNameQuery->bindValue(":type", "%");
    transitionIdByNameQuery->bindValue(":apprid", sidId);

    sidTransId = findTransitionId(departure, transitionIdByNameQuery, strict);
    if(sidTransId == -1)
      qWarning() << "Loading of SID transition" << sidTrans << "failed";
  }

  return sidTransId;
}

int ProcedureQuery::getStarId(map::MapAirport destination, const QString& star, const QString& runway, bool strict)
{
  NavApp::getMapQueryGui()->getAirportNavReplace(destination);

  int starId = -1;
  // Get a STAR id =================================================================
  if(!star.isEmpty())
  {
    approachIdByNameQuery->bindValue(":fixident", star);
    approachIdByNameQuery->bindValue(":type", "GPS");
    approachIdByNameQuery->bindValue(":apident", destination.ident);

    starId = findApproachId(destination, approachIdByNameQuery, "A", runway, strict);

    if(starId == -1)
      // Try again without runway
      starId = findApproachId(destination, approachIdByNameQuery, "A", QString(), strict);

    if(starId == -1)
      qWarning() << "Loading of STAR" << star << "failed";
  }
  return starId;
}

int ProcedureQuery::getStarTransitionId(map::MapAirport destination, const QString& starTrans, int starId, bool strict)
{
  NavApp::getMapQueryGui()->getAirportNavReplace(destination);

  int starTransId = -1;
  // Get a STAR transition id =================================================================
  if(!starTrans.isEmpty() && starId != -1)
  {
    transitionIdByNameQuery->bindValue(":fixident", starTrans);
    transitionIdByNameQuery->bindValue(":type", "%");
    transitionIdByNameQuery->bindValue(":apprid", starId);

    starTransId = findTransitionId(destination, transitionIdByNameQuery, strict);
    if(starTransId == -1)
      qWarning() << "Loading of STAR transition" << starTrans << "failed";
  }
  return starTransId;
}

void ProcedureQuery::createCustomApproach(proc::MapProcedureLegs& procedure, const map::MapAirport& airportSim,
                                          const map::MapRunwayEnd& runwayEndSim, float distance, float altitude, float offsetAngle)
{
  float finalCourseTrue = runwayEndSim.heading + offsetAngle;
  Pos initialFixPos = runwayEndSim.position.endpoint(ageo::nmToMeter(distance), ageo::opposedCourseDeg(finalCourseTrue));

  // Create procedure ========================================
  procedure.ref.runwayEndId = runwayEndSim.id;
  procedure.ref.airportId = airportSim.id;
  procedure.ref.approachId = CUSTOM_APPROACH_ID;
  procedure.ref.mapType = proc::PROCEDURE_APPROACH;
  procedure.approachFixIdent = airportSim.ident + runwayEndSim.name;
  procedure.approachType = "CUSTOM";
  procedure.runwayEnd = runwayEndSim;
  procedure.procedureRunway = runwayEndSim.name;
  procedure.mapType = proc::PROCEDURE_APPROACH;
  procedure.approachDistance = distance;
  procedure.customDistance = distance;
  procedure.customAltitude = altitude;
  procedure.customOffset = offsetAngle;
  procedure.gpsOverlay = procedure.hasError = procedure.hasHardError = procedure.circleToLand = false;
  procedure.transitionDistance = procedure.missedDistance = 0.f;
  procedure.bounding = Rect(initialFixPos);
  procedure.bounding.extend(runwayEndSim.position);

  // Create an initial fix leg at the given distance =======================
  proc::MapProcedureLeg startLeg;
  startLeg.fixType = "CST";
  startLeg.fixIdent = QObject::tr("RW%1+%2").arg(runwayEndSim.name).arg(atools::roundToInt(distance));
  startLeg.fixRegion = airportSim.region;
  startLeg.fixPos = initialFixPos;
  startLeg.line = Line(initialFixPos);
  startLeg.geometry = LineString(initialFixPos);
  startLeg.altRestriction.descriptor = proc::MapAltRestriction::AT;
  startLeg.altRestriction.alt1 = airportSim.position.getAltitude() + altitude;
  startLeg.type = proc::CUSTOM_APP_START;
  startLeg.mapType = proc::PROCEDURE_APPROACH;
  startLeg.course = 0.f;
  startLeg.calculatedTrueCourse = map::INVALID_COURSE_VALUE;
  startLeg.distance = startLeg.calculatedDistance = 0.f;
  startLeg.theta = startLeg.rho = startLeg.time = 0.f;
  startLeg.magvar = airportSim.magvar;
  startLeg.missed = startLeg.flyover = startLeg.trueCourse = startLeg.intercept = startLeg.disabled = startLeg.malteseCross = false;
  procedure.approachLegs.append(startLeg);

  // Create the runway leg ================================================
  proc::MapProcedureLeg runwayLeg;
  runwayLeg.fixType = "R";
  runwayLeg.fixIdent = "RW" % runwayEndSim.name;
  runwayLeg.fixRegion = airportSim.region;
  runwayLeg.fixPos = runwayEndSim.position;
  runwayLeg.line = Line(initialFixPos, runwayEndSim.position);
  runwayLeg.geometry = LineString(initialFixPos, runwayEndSim.position);
  runwayLeg.navaids.runwayEnds.append(runwayEndSim);
  runwayLeg.navId = -1;
  runwayLeg.altRestriction.descriptor = proc::MapAltRestriction::AT;
  runwayLeg.altRestriction.alt1 = airportSim.position.getAltitude();
  runwayLeg.type = proc::CUSTOM_APP_RUNWAY;
  runwayLeg.mapType = proc::PROCEDURE_APPROACH;
  runwayLeg.course = ageo::normalizeCourse(finalCourseTrue - airportSim.magvar);
  runwayLeg.calculatedTrueCourse = finalCourseTrue;
  runwayLeg.distance = runwayLeg.calculatedDistance = distance;
  runwayLeg.theta = runwayLeg.rho = runwayLeg.time = 0.f;
  runwayLeg.magvar = airportSim.magvar;
  runwayLeg.missed = runwayLeg.flyover = runwayLeg.trueCourse = runwayLeg.intercept = runwayLeg.disabled = runwayLeg.malteseCross = false;
  procedure.approachLegs.append(runwayLeg);
}

void ProcedureQuery::createCustomDeparture(proc::MapProcedureLegs& procedure, const map::MapAirport& airportSim,
                                           const map::MapRunwayEnd& runwayEndSim, float distance)
{
  Pos endFixPos = runwayEndSim.position.endpoint(ageo::nmToMeter(distance), runwayEndSim.heading);

  // Create procedure ========================================
  procedure.ref.runwayEndId = runwayEndSim.id;
  procedure.ref.airportId = airportSim.id;
  procedure.ref.approachId = CUSTOM_DEPARTURE_ID;
  procedure.ref.mapType = proc::PROCEDURE_SID;
  procedure.approachFixIdent = airportSim.ident + runwayEndSim.name;
  procedure.approachType = "CUSTOMDEPART";
  procedure.runwayEnd = runwayEndSim;
  procedure.procedureRunway = runwayEndSim.name;
  procedure.mapType = proc::PROCEDURE_SID;
  procedure.approachDistance = distance;
  procedure.customDistance = distance;
  procedure.customAltitude = 0.f;
  procedure.customOffset = 0.f;
  procedure.gpsOverlay = procedure.hasError = procedure.hasHardError = procedure.circleToLand = false;
  procedure.transitionDistance = procedure.missedDistance = 0.f;
  procedure.bounding = Rect(endFixPos);
  procedure.bounding.extend(runwayEndSim.position);

  // Create the runway leg ================================================
  proc::MapProcedureLeg runwayLeg;
  runwayLeg.fixType = "R";
  runwayLeg.fixIdent = "RW" % runwayEndSim.name;
  runwayLeg.fixRegion = airportSim.region;
  runwayLeg.fixPos = runwayEndSim.position;
  runwayLeg.line = Line(runwayEndSim.position);
  runwayLeg.geometry = LineString(runwayEndSim.position);
  runwayLeg.navaids.runwayEnds.append(runwayEndSim);
  runwayLeg.navId = -1;
  runwayLeg.altRestriction.descriptor = proc::MapAltRestriction::AT;
  runwayLeg.altRestriction.alt1 = airportSim.position.getAltitude();
  runwayLeg.type = proc::CUSTOM_DEP_RUNWAY;
  runwayLeg.mapType = proc::PROCEDURE_SID;
  runwayLeg.course = 0.f;
  runwayLeg.calculatedTrueCourse = map::INVALID_COURSE_VALUE;
  runwayLeg.distance = runwayLeg.calculatedDistance = 0.f;
  runwayLeg.theta = runwayLeg.rho = runwayLeg.time = 0.f;
  runwayLeg.magvar = airportSim.magvar;
  runwayLeg.missed = runwayLeg.flyover = runwayLeg.trueCourse = runwayLeg.intercept = runwayLeg.disabled = runwayLeg.malteseCross = false;
  procedure.approachLegs.append(runwayLeg);

  // Create an initial fix leg at the given distance =======================
  proc::MapProcedureLeg endLeg;
  endLeg.fixType = "CST";
  endLeg.fixIdent = QObject::tr("RW%1+%2").arg(runwayEndSim.name).arg(atools::roundToInt(distance));
  endLeg.fixRegion = airportSim.region;
  endLeg.fixPos = endFixPos;
  endLeg.line = Line(runwayEndSim.position, endFixPos);
  endLeg.geometry = LineString(runwayEndSim.position, endFixPos);
  endLeg.altRestriction.descriptor = proc::MapAltRestriction::NONE;
  endLeg.altRestriction.alt1 = 0.f;
  endLeg.type = proc::CUSTOM_DEP_END;
  endLeg.mapType = proc::PROCEDURE_SID;
  endLeg.course = ageo::normalizeCourse(runwayEndSim.heading - airportSim.magvar);
  endLeg.calculatedTrueCourse = runwayEndSim.heading;
  endLeg.distance = endLeg.calculatedDistance = distance;
  endLeg.theta = endLeg.rho = endLeg.time = 0.f;
  endLeg.magvar = airportSim.magvar;
  endLeg.missed = endLeg.flyover = endLeg.trueCourse = endLeg.intercept = endLeg.disabled = endLeg.malteseCross = false;
  procedure.approachLegs.append(endLeg);
}

void ProcedureQuery::createCustomApproach(proc::MapProcedureLegs& procedure, const map::MapAirport& airport,
                                          const QString& runwayEnd, float distance, float altitude, float offsetAngle)
{
  // Custom approaches use the simulator airport
  map::MapResult result;
  runwayEndByNameSim(result, runwayEnd, airport);
  if(!result.runwayEnds.isEmpty())
    createCustomApproach(procedure, airport, result.runwayEnds.first(), distance, altitude, offsetAngle);
}

void ProcedureQuery::createCustomDeparture(proc::MapProcedureLegs& procedure, const map::MapAirport& airport,
                                           const QString& runwayEnd, float distance)
{
  // Custom approaches use the simulator airport
  map::MapResult result;
  runwayEndByNameSim(result, runwayEnd, airport);
  if(!result.runwayEnds.isEmpty())
    createCustomDeparture(procedure, airport, result.runwayEnds.first(), distance);
}

void ProcedureQuery::clearCache()
{
  qDebug() << Q_FUNC_INFO;

  procedureCache.clear();
  transitionCache.clear();
  procedureLegIndex.clear();
  transitionLegIndex.clear();
}

QVector<int> ProcedureQuery::getTransitionIdsForProcedure(int procedureId)
{
  QVector<int> transitionIds;

  transitionIdsForApproachQuery->bindValue(":id", procedureId);
  transitionIdsForApproachQuery->exec();

  while(transitionIdsForApproachQuery->next())
    transitionIds.append(transitionIdsForApproachQuery->value("transition_id").toInt());
  return transitionIds;
}

QString ProcedureQuery::runwayErrorString(const QString& runway)
{
  return runway.isEmpty() ? tr("no runway") : tr("runway %1").arg(runway);
}

void ProcedureQuery::getLegsForFlightplanProperties(const QHash<QString, QString> properties,
                                                    const map::MapAirport& departure,
                                                    const map::MapAirport& destination,
                                                    proc::MapProcedureLegs& arrivalLegs,
                                                    proc::MapProcedureLegs& starLegs,
                                                    proc::MapProcedureLegs& sidLegs,
                                                    QStringList& errors)
{
  errors.clear();
  MapQuery *mapQuery = NavApp::getMapQueryGui();
  map::MapAirport departureNav = mapQuery->getAirportNav(departure);
  map::MapAirport destinationNav = mapQuery->getAirportNav(destination);

  // Fetch ids by various (fuzzy) queries from database ==========================================================
  int sidApprId = -1, sidTransId = -1, approachId = -1, starId = -1, starTransId = -1, transitionId = -1;

  if(properties.contains(pln::DEPARTURE_CUSTOM_DISTANCE) && properties.value(pln::SIDTYPE) == "CUSTOMDEPART")
  {
    map::MapAirport departureSim = mapQuery->getAirportSim(departure);

    if(departureSim.isValid())
      createCustomDeparture(sidLegs, departureSim, properties.value(pln::SIDAPPRRW),
                            properties.value(pln::DEPARTURE_CUSTOM_DISTANCE).toFloat());
    sidApprId = sidLegs.isEmpty() ? -1 : CUSTOM_DEPARTURE_ID;
  }
  else
  {
    // Get a SID id (approach and transition) =================================================================
    // Get a SID id =================================================================
    if(properties.contains(pln::SIDAPPR))
    {
      if(departureNav.isValid())
        sidApprId = getSidId(departureNav, properties.value(pln::SIDAPPR), properties.value(pln::SIDAPPRRW), true);

      if(sidApprId == -1)
      {
        qWarning() << "Loading of SID" << properties.value(pln::SIDAPPR) << "failed";
        errors.append(tr("SID %1 from %2").
                      arg(properties.value(pln::SIDAPPR)).
                      arg(runwayErrorString(properties.value(pln::SIDAPPRRW))));
      }
    }

    // Get a SID transition id =================================================================
    if(properties.contains(pln::SIDTRANS) && sidApprId != -1)
    {
      if(departureNav.isValid())
        sidTransId = getSidTransitionId(departureNav, properties.value(pln::SIDTRANS), sidApprId, true);
      if(sidTransId == -1)
      {
        qWarning() << "Loading of SID transition" << properties.value(pln::SIDTRANS) << "failed";
        errors.append(tr("SID transition %1").arg(properties.value(pln::SIDTRANS)));
      }
    }
  }

  // Get an approach id by ARINC name =================================================================
  if(properties.contains(pln::APPROACH_ARINC) && !properties.value(pln::APPROACH_ARINC).isEmpty() &&
     approachIdByArincNameQuery != nullptr)
  {
    // Use ARINC name which is more specific - potential source is new X-Plane FMS file
    QString arincName = properties.value(pln::APPROACH_ARINC);
    approachIdByArincNameQuery->bindValue(":arincname", arincName);
    approachIdByArincNameQuery->bindValue(":apident", destinationNav.ident);

    if(destinationNav.isValid())
    {
      approachId = findApproachId(destinationNav, approachIdByArincNameQuery,
                                  QString(), properties.value(pln::APPROACHRW), false);

      if(approachId == -1)
      {
        // Try again with variants of the ARINC approach name in case the runway was renamed
        QStringList variants = atools::fs::util::arincNameNameVariants(arincName);
        if(!variants.isEmpty())
          variants.removeFirst();

        qDebug() << Q_FUNC_INFO << "Nothing found for ARINC" << arincName << "trying" << variants;

        for(const QString& variant : variants)
        {
          approachIdByArincNameQuery->bindValue(":arincname", variant);

          approachId = findApproachId(destinationNav, approachIdByArincNameQuery,
                                      QString(), properties.value(pln::APPROACHRW), false);
          if(approachId != -1)
            break;
        }
      }
    }

    if(approachId == -1)
    {
      qWarning() << "Loading of approach by ARINC name" << properties.value(pln::APPROACH_ARINC) << "failed";
      errors.append(tr("Approach %1 to %2").
                    arg(properties.value(pln::APPROACH_ARINC)).
                    arg(runwayErrorString(properties.value(pln::APPROACHRW))));
    }
  }

  if(approachId == -1 && (properties.contains(pln::APPROACH) || properties.contains(pln::APPROACHTYPE)))
  {
    // Nothing found by ARINC id but type and fix name given try this next

    // Get an approach id by name or type =================================================================

    // Use approach name
    QString type = properties.value(pln::APPROACHTYPE);
    if(type == "CUSTOM")
    {
      map::MapAirport destinationSim = mapQuery->getAirportSim(destination);

      if(destinationSim.isValid())
        createCustomApproach(arrivalLegs, destinationSim, properties.value(pln::APPROACHRW),
                             properties.value(pln::APPROACH_CUSTOM_DISTANCE).toFloat(),
                             properties.value(pln::APPROACH_CUSTOM_ALTITUDE).toFloat(),
                             properties.value(pln::APPROACH_CUSTOM_OFFSET).toFloat());
      approachId = arrivalLegs.isEmpty() ? -1 : CUSTOM_APPROACH_ID;
    }
    else
    {
      QString appr = properties.value(pln::APPROACH);

      if(appr.isEmpty())
        appr = "%";

      if(type.isEmpty())
        type = "%";

      approachIdByNameQuery->bindValue(":fixident", appr);
      approachIdByNameQuery->bindValue(":type", type);
      approachIdByNameQuery->bindValue(":apident", destinationNav.ident);

      if(destinationNav.isValid())
        approachId = findApproachId(destinationNav, approachIdByNameQuery,
                                    properties.value(pln::APPROACHSUFFIX),
                                    properties.value(pln::APPROACHRW), false);
    }

    if(approachId == -1)
    {
      qWarning() << "Loading of approach" << properties.value(pln::APPROACH) << "failed";
      errors.append(tr("Approach %1 to %2").
                    arg(properties.value(pln::APPROACH)).
                    arg(runwayErrorString(properties.value(pln::APPROACHRW))));

    }
  }

  // Get a transition id =================================================================
  if(properties.contains(pln::TRANSITION) && approachId != -1)
  {
    QString type = properties.value(pln::TRANSITIONTYPE);
    if(type.isEmpty())
      type = "%";
    transitionIdByNameQuery->bindValue(":fixident", properties.value(pln::TRANSITION));
    transitionIdByNameQuery->bindValue(":type", type);
    transitionIdByNameQuery->bindValue(":apprid", approachId);

    if(destinationNav.isValid())
      transitionId = findTransitionId(destinationNav, transitionIdByNameQuery, false);
    if(transitionId == -1)
    {
      qWarning() << "Loading of transition" << properties.value(pln::TRANSITION) << "failed";
      errors.append(tr("Transition %1").arg(properties.value(pln::TRANSITION)));
    }
  }

  // Get a STAR id =================================================================
  if(properties.contains(pln::STAR))
  {
    if(destinationNav.isValid())
      starId = getStarId(destinationNav, properties.value(pln::STAR), properties.value(pln::STARRW), false);
    if(starId == -1)
    {
      qWarning() << "Loading of STAR " << properties.value(pln::STAR) << "failed";
      errors.append(tr("STAR %1 to %2").
                    arg(properties.value(pln::STAR)).
                    arg(runwayErrorString(properties.value(pln::STARRW))));

    }
  }

  // Get a STAR transition id =================================================================
  if(properties.contains(pln::STARTRANS) && starId != -1)
  {
    if(destinationNav.isValid())
      starTransId = getStarTransitionId(destinationNav, properties.value(pln::STARTRANS), starId);
    if(starTransId == -1)
    {
      qWarning() << "Loading of STAR transition" << properties.value(pln::STARTRANS) << "failed";
      errors.append(tr("STAR transition %1").arg(properties.value(pln::STARTRANS)));
    }
  }

  // Fetch procedure structures by id from database  =============================================

  // Fetch SID and  transition ========================================================================
  if(sidTransId != -1) // Fetch and copy SID and transition together (here from cache)
  {
    const proc::MapProcedureLegs *legs = getTransitionLegs(departureNav, sidTransId);
    if(procedureValid(legs, errors))
    {
      sidLegs = *legs;
      // Assign runway to the legs copy if procedure has parallel or all runway reference
      insertSidStarRunway(sidLegs, properties.value(pln::SIDAPPRRW));
    }
    else
      qWarning() << Q_FUNC_INFO << "legs not found for" << departureNav.id << sidTransId;
  }
  else if(sidApprId != -1 && sidApprId != CUSTOM_DEPARTURE_ID) // Fetch and copy SID only from cache
  {
    const proc::MapProcedureLegs *legs = getApproachLegs(departureNav, sidApprId);
    if(procedureValid(legs, errors))
    {
      sidLegs = *legs;
      // Assign runway to the legs copy if procedure has parallel or all runway reference
      insertSidStarRunway(sidLegs, properties.value(pln::SIDAPPRRW));
    }
    else
      qWarning() << Q_FUNC_INFO << "legs not found for" << departureNav.id << sidApprId;
  }

  // Fetch Approach and  transition ========================================================================
  if(transitionId != -1) // Fetch and copy transition together with approach (here from cache)
  {
    const proc::MapProcedureLegs *legs = getTransitionLegs(destinationNav, transitionId);
    if(procedureValid(legs, errors))
      arrivalLegs = *legs;
    else
      qWarning() << Q_FUNC_INFO << "legs not found for" << destinationNav.id << transitionId;
  }
  else if(approachId != -1 && approachId != CUSTOM_APPROACH_ID) // Fetch and copy approach only from cache
  {
    const proc::MapProcedureLegs *legs = getApproachLegs(destinationNav, approachId);
    if(procedureValid(legs, errors))
      arrivalLegs = *legs;
    else
      qWarning() << Q_FUNC_INFO << "legs not found for" << destinationNav.id << approachId;
  }

  // Fetch STAR and transition ========================================================================
  if(starTransId != -1)
  {
    const proc::MapProcedureLegs *legs = getTransitionLegs(destinationNav, starTransId);
    if(procedureValid(legs, errors))
    {
      starLegs = *legs;
      // Assign runway if procedure has parallel or all runway reference
      insertSidStarRunway(starLegs, properties.value(pln::STARRW));
    }
    else
      qWarning() << Q_FUNC_INFO << "legs not found for" << destinationNav.id << starTransId;
  }
  else if(starId != -1)
  {
    const proc::MapProcedureLegs *legs = getApproachLegs(destinationNav, starId);
    if(procedureValid(legs, errors))
    {
      starLegs = *legs;
      // Assign runway if procedure has parallel or all runway reference
      insertSidStarRunway(starLegs, properties.value(pln::STARRW));
    }
    else
      qWarning() << Q_FUNC_INFO << "legs not found for" << destinationNav.id << starId;
  }
}

bool ProcedureQuery::procedureValid(const proc::MapProcedureLegs *legs, QStringList& errors)
{
  if(legs != nullptr)
  {
    if(legs->hasHardError)
      errors.append(tr("Procedure %1 %2 in scenery library has errors").
                    arg(legs->approachType).arg(legs->approachFixIdent));
    else
      // Usable
      return true;
  }
  else
    errors.append(tr("Procedure not found in scenery library"));
  return false;
}

QString ProcedureQuery::getSidAndTransition(QHash<QString, QString>& properties)
{
  QString retval;
  if(properties.contains(pln::SIDAPPR))
    retval += properties.value(pln::SIDAPPR);

  if(properties.contains(pln::SIDTRANS))
    retval.append("." % properties.value(pln::SIDTRANS));
  return retval;
}

QString ProcedureQuery::getStarAndTransition(QHash<QString, QString>& properties)
{
  QString retval;
  if(properties.contains(pln::STAR))
    retval += properties.value(pln::STAR);

  if(properties.contains(pln::STARTRANS))
    retval.append("." % properties.value(pln::STARTRANS));
  return retval;
}

int ProcedureQuery::findTransitionId(const map::MapAirport& airport, atools::sql::SqlQuery *query, bool strict)
{
  return findProcedureLegId(airport, query, QString(), QString(), true, strict);
}

int ProcedureQuery::findApproachId(const map::MapAirport& airport, atools::sql::SqlQuery *query,
                                   const QString& suffix, const QString& runway, bool strict)
{
  int id = findProcedureLegId(airport, query, suffix, runway, false, strict);
  if(id == -1 && !runway.isEmpty())
  {
    // Try again with runway variants in case runway was renamed and runway is required
    QStringList variants = atools::fs::util::runwayNameVariants(runway);

    if(!variants.isEmpty())
      // Remove original since this was already queried
      variants.removeFirst();

    qDebug() << Q_FUNC_INFO << "Nothing found for runway" << runway << "trying" << variants;

    for(const QString& rw : variants)
    {
      if((id = findProcedureLegId(airport, query, suffix, rw, false, strict)) != -1)
        return id;
    }
  }
  return id;
}

bool ProcedureQuery::doesRunwayMatch(const QString& runway, const QString& runwayFromQuery,
                                     const QString& arincName, const QStringList& airportRunways,
                                     bool matchEmptyRunway) const
{
  if(runway.isEmpty() && !matchEmptyRunway)
    // Nothing to match - get all procedures
    return true;

  if(atools::fs::util::runwayEqual(runway, runwayFromQuery))
    return true;

  return doesSidStarRunwayMatch(runway, arincName, airportRunways);
}

bool ProcedureQuery::doesSidStarRunwayMatch(const QString& runway, const QString& arincName,
                                            const QStringList& airportRunways) const
{
  if(atools::fs::util::hasSidStarAllRunways(arincName))
    // SID or STAR for all runways - otherwise arinc name will not match anyway
    return true;

  if(atools::fs::util::hasSidStarParallelRunways(arincName))
  {
    // Check which runways are assigned from values like "RW12B"
    QString rwBaseName = arincName.mid(2, 2);
    if(airportRunways.contains(runway) && atools::fs::util::runwayEqual(runway, rwBaseName % "L"))
      return true;

    if(airportRunways.contains(runway) && atools::fs::util::runwayEqual(runway, rwBaseName % "R"))
      return true;

    if(airportRunways.contains(runway) && atools::fs::util::runwayEqual(runway, rwBaseName % "C"))
      return true;
  }

  return false;
}

QString ProcedureQuery::anyMatchingRunwayForSidStar(const QString& arincName, const QStringList& airportRunways) const
{
  if(atools::fs::util::hasSidStarParallelRunways(arincName))
  {
    // Check which runways are assigned from values like "RW12B"
    QString rwBaseName = arincName.mid(2, 2);

    for(const QString& aprw:airportRunways)
    {
      if(aprw == rwBaseName % "L")
        return aprw;

      if(aprw == rwBaseName % "R")
        return aprw;

      if(aprw == rwBaseName % "C")
        return aprw;
    }
  }

  return airportRunways.first();
}

void ProcedureQuery::insertSidStarRunway(proc::MapProcedureLegs& legs, const QString& runway)
{
  if(legs.hasSidOrStarMultipleRunways())
  {
    QStringList runwayNames = airportQueryNav->getRunwayNames(legs.ref.airportId);
    if(runway.isEmpty())
      // Assign first matching runway for this sid if not assigned yet
      legs.procedureRunway = anyMatchingRunwayForSidStar(legs.approachArincName, runwayNames);
    else
      // Assign given runway
      legs.procedureRunway = runway;

    legs.runwayEnd = airportQueryNav->getRunwayEndByName(legs.ref.airportId, legs.procedureRunway);

    if(legs.runwayEnd.isValid())
    {
      // Check for any legs of type runway and assign runway.
      for(int i = 0; i < legs.approachLegs.size(); i++)
      {
        MapProcedureLeg& leg = legs.approachLegs[i];
        if(leg.fixType == "R" && leg.fixIdent == "RW")
        {
          // Update data for unknown runway to known runway
          leg.fixIdent = "RW" % legs.procedureRunway;
          leg.fixPos = legs.runwayEnd.position;
          leg.line = Line(legs.runwayEnd.position);
          leg.geometry = LineString(legs.runwayEnd.position);
          leg.navaids.runwayEnds.clear();
          leg.navaids.runwayEnds.append(legs.runwayEnd);
        }
      }

      // Re-calculate all legs, positions and distances again
      postProcessLegs(airportQueryNav->getAirportById(legs.ref.airportId), legs, false /*addArtificialLegs*/);
    }
    else
      qWarning() << Q_FUNC_INFO << "Cannot get runway for" << legs.procedureRunway;
  }
}

int ProcedureQuery::findProcedureLegId(const map::MapAirport& airport, atools::sql::SqlQuery *query,
                                       const QString& suffix, const QString& runway,
                                       bool transition, bool strict)
{
  QStringList airportRunways = airportQueryNav->getRunwayNames(airport.id);

  int procedureId = -1;
  QVector<int> ids;
  query->exec();
  while(query->next())
  {
    // Compare the suffix manually since the ifnull function makes the query unstable (did not work with undo)
    if(!transition && (suffix != query->value("suffix").toString() ||
                       // Runway will be compared directly to the approach and not the airport runway
                       !doesRunwayMatch(runway,
                                        query->valueStr("runway_name"),
                                        query->valueStr("arinc_name", QString()),
                                        airportRunways, false /* Match empty rw */)))
      continue;

    ids.append(query->value(transition ? "transition_id" : "approach_id").toInt());
  }
  query->finish();

  if(!strict)
  {
    if(ids.isEmpty())
    {
      // Nothing found - try again ignoring the suffix
      query->exec();
      while(query->next())
      {
        // Compare the suffix manually since the ifnull function makes the query unstable (did not work with undo)
        if(!transition && // Runway will be compared directly to the approach and not the airport runway
           !doesRunwayMatch(runway,
                            query->valueStr("runway_name"),
                            query->valueStr("arinc_name"),
                            airportRunways, false /* Match empty rw */))
          continue;

        ids.append(query->value(transition ? "transition_id" : "approach_id").toInt());
      }
      query->finish();
    }
  }

  if(ids.size() > 1 && runway.isEmpty())
  {
    // Runway is empty and found more than one - try to load a circle-to-land approach or SID/STAR without runway
    bool found = false;
    query->exec();
    while(query->next())
    {
      // Compare the suffix manually since the ifnull function makes the query unstable (did not work with undo)
      if(!transition && (suffix != query->value("suffix").toString() ||
                         // Runway will be compared directly to the approach and not the airport runway
                         // The method will check here if the runway in the query result is empty
                         !doesRunwayMatch(runway,
                                          query->valueStr("runway_name"),
                                          query->valueStr("arinc_name", QString()),
                                          airportRunways, true /* Match empty rw */)))
        continue;

      if(!found)
      {
        // Found something - clear all previous results
        ids.clear();
        found = true;
      }

      ids.append(query->value(transition ? "transition_id" : "approach_id").toInt());
    }
    query->finish();
  }

  // Choose first procedure
  if(procedureId == -1 && !ids.isEmpty())
    procedureId = ids.first();

  return procedureId;
}

void ProcedureQuery::processAltRestrictions(proc::MapProcedureLegs& procedure) const
{
  if(procedure.mapType & proc::PROCEDURE_APPROACH)
  {
    bool force = false;
    // Start at end of procedure (runway)
    for(int i = procedure.approachLegs.size() - 1; i >= 0; i--)
    {
      const MapProcedureLeg& prev = procedure.approachLegs.value(i - 1);
      MapProcedureLeg& leg = procedure.approachLegs[i];
      if(prev.verticalAngle < -0.1f)
        // Do not force altitude if previous leg has a required vertical angle
        // Real altitude is calculated by angle and not by altitude restriction
        leg.altRestriction.forceFinal = false;
      else
      {
        // Force altitude down to lowest of altitude restriction
        force |= (leg.isFinalApproachCourseFix() || leg.isFinalApproachFix()) && !leg.isMissed() &&
                 leg.mapType == proc::PROCEDURE_APPROACH && leg.altRestriction.isValid() &&
                 contains(leg.altRestriction.descriptor, {proc::MapAltRestriction::AT,
                                                          proc::MapAltRestriction::AT_OR_ABOVE,
                                                          proc::MapAltRestriction::ILS_AT,
                                                          proc::MapAltRestriction::ILS_AT_OR_ABOVE});

        // Force lowest restriction altitude for FAF and FACF
        leg.altRestriction.forceFinal = force;
      }

      if(force)
        // Stop if there was already a fix - this will prefer FAF before FACF
        break;
    }
  }
}

void ProcedureQuery::assignType(proc::MapProcedureLegs& procedure) const
{
  if(NavApp::hasSidStarInDatabase() && procedure.approachType == "GPS" &&
     (procedure.approachSuffix == "A" || procedure.approachSuffix == "D") && procedure.gpsOverlay)
  {
    if(procedure.approachSuffix == "A")
    {
      if(!procedure.approachLegs.isEmpty())
      {
        procedure.mapType = proc::PROCEDURE_STAR;
        for(MapProcedureLeg& leg : procedure.approachLegs)
          leg.mapType = proc::PROCEDURE_STAR;
      }

      if(!procedure.transitionLegs.isEmpty())
      {
        procedure.mapType |= proc::PROCEDURE_STAR_TRANSITION;
        for(MapProcedureLeg& leg : procedure.transitionLegs)
          leg.mapType = proc::PROCEDURE_STAR_TRANSITION;
      }
    }
    else if(procedure.approachSuffix == "D")
    {
      if(!procedure.approachLegs.isEmpty())
      {
        procedure.mapType = proc::PROCEDURE_SID;
        for(MapProcedureLeg& leg : procedure.approachLegs)
          leg.mapType = proc::PROCEDURE_SID;
      }

      if(!procedure.transitionLegs.isEmpty())
      {
        procedure.mapType |= proc::PROCEDURE_SID_TRANSITION;
        for(MapProcedureLeg& leg : procedure.transitionLegs)
          leg.mapType = proc::PROCEDURE_SID_TRANSITION;
      }
    }
  }
  else
  {
    if(!procedure.approachLegs.isEmpty())
    {
      procedure.mapType = proc::PROCEDURE_APPROACH;
      for(MapProcedureLeg& leg : procedure.approachLegs)
        leg.mapType = leg.missed ? proc::PROCEDURE_MISSED : proc::PROCEDURE_APPROACH;
    }

    if(!procedure.transitionLegs.isEmpty())
    {
      procedure.mapType |= proc::PROCEDURE_TRANSITION;
      for(MapProcedureLeg& leg : procedure.transitionLegs)
        leg.mapType = proc::PROCEDURE_TRANSITION;
    }
  }
  procedure.ref.mapType = procedure.mapType;
}

/* Create proceed to runway entry based on information in given leg and the runway end information
 *  in the given legs */
proc::MapProcedureLeg ProcedureQuery::createRunwayLeg(const proc::MapProcedureLeg& leg, const proc::MapProcedureLegs& legs) const
{
  proc::MapProcedureLeg rwleg;
  rwleg.airportId = legs.ref.airportId;
  rwleg.approachId = legs.ref.approachId;
  rwleg.transitionId = legs.ref.transitionId;
  rwleg.navId = leg.navId;

  // Use a generated id base on the previous leg id
  rwleg.legId = RUNWAY_LEG_ID_BASE + leg.legId;

  rwleg.altRestriction.descriptor = proc::MapAltRestriction::AT;
  rwleg.altRestriction.alt2 = 0.f;
  // geometry is populated later
  rwleg.fixType = "R";
  rwleg.fixIdent = "RW" % legs.runwayEnd.name;
  rwleg.fixPos = legs.runwayEnd.position;
  rwleg.time = 0.f;
  rwleg.theta = 0.f;
  rwleg.rho = 0.f;
  rwleg.magvar = leg.magvar;
  rwleg.distance = meterToNm(rwleg.line.lengthMeter());
  rwleg.course = normalizeCourse(rwleg.line.angleDeg() - rwleg.magvar);
  rwleg.navaids.runwayEnds.append(legs.runwayEnd);
  rwleg.missed = rwleg.flyover = rwleg.trueCourse = rwleg.intercept = rwleg.disabled = rwleg.malteseCross = false;

  return rwleg;
}

/* Create start of procedure entry based on information in given leg */
proc::MapProcedureLeg ProcedureQuery::createStartLeg(const proc::MapProcedureLeg& leg,
                                                     const proc::MapProcedureLegs& legs,
                                                     const QStringList& displayText) const
{
  proc::MapProcedureLeg sleg;
  sleg.airportId = legs.ref.airportId;
  sleg.approachId = legs.ref.approachId;
  sleg.transitionId = legs.ref.transitionId;

  // Use a generated id base on the previous leg id
  sleg.legId = START_LEG_ID_BASE + leg.legId;
  sleg.displayText.append(displayText);
  // geometry is populated later

  sleg.fixPos = leg.fixPos;
  sleg.fixIdent = leg.fixIdent;
  sleg.fixRegion = leg.fixRegion;
  sleg.fixType = leg.fixType;
  sleg.navId = leg.navId;
  sleg.navaids = leg.navaids;

  // Correct distance is calculated in the RouteLeg to get a transition from route to procedure
  sleg.time = 0.f;
  sleg.theta = 0.f;
  sleg.rho = 0.f;
  sleg.magvar = leg.magvar;
  sleg.distance = 0.f;
  sleg.course = 0.f;
  sleg.missed = sleg.flyover = sleg.trueCourse = sleg.intercept = sleg.disabled = sleg.malteseCross = false;

  return sleg;
}
