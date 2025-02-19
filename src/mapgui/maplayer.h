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

#ifndef LITTLENAVMAP_MAPLAYER_H
#define LITTLENAVMAP_MAPLAYER_H

#include <QDebug>

namespace atools {
namespace  util {
class XmlStream;
}
}

namespace layer {

/* Do not show anything above this zoom distance except user features */
constexpr float DISTANCE_CUT_OFF_LIMIT_KM = 4000.f;

/* Stop all drawing, tooltips and interaction above this limit */
constexpr float NO_DRAW_LIMIT_KM = 8000.f;

/* Ships considered large above this model radius in feet */
constexpr int LARGE_SHIP_SIZE = 150;

/* Aircraft considered large above this model radius in feet */
constexpr int LARGE_AIRCRAFT_SIZE = 75;

}

/*
 * A map layer defines what should be shown on the map for a certain zoom level. It follows the builder pattern.
 */
class MapLayer
{
public:
  /*
   * @param maximumRange create a layer for the maximum zoom distance
   * All features are enabled per default.
   */
  MapLayer(float maximumRangeKm);

  /*
   * create a clone of this layer with the maximum zoom distance
   */
  MapLayer clone(float maximumRangeKm) const;

  /* @return true if a query for this layer will give the same result set */
  bool hasSameQueryParametersAirport(const MapLayer *other) const;
  bool hasSameQueryParametersAirspace(const MapLayer *other) const;
  bool hasSameQueryParametersAirwayTrack(const MapLayer *other) const;
  bool hasSameQueryParametersVor(const MapLayer *other) const;
  bool hasSameQueryParametersNdb(const MapLayer *other) const;
  bool hasSameQueryParametersWaypoint(const MapLayer *other) const;
  bool hasSameQueryParametersWind(const MapLayer *other) const;
  bool hasSameQueryParametersMarker(const MapLayer *other) const;
  bool hasSameQueryParametersIls(const MapLayer *other) const;
  bool hasSameQueryParametersHolding(const MapLayer *other) const;
  bool hasSameQueryParametersAirportMsa(const MapLayer *other) const;

  bool operator<(const MapLayer& other) const;

  float getMaxRange() const
  {
    return maxRange;
  }

  bool isAirport() const
  {
    return layerAirport;
  }

  bool isApproach() const
  {
    return layerApproach;
  }

  bool isApproachDetail() const
  {
    return layerApproachDetail;
  }

  bool isApproachText() const
  {
    return layerApproachText;
  }

  bool isApproachTextDetails() const
  {
    return layerApproachTextDetail;
  }

  bool isAirportOverviewRunway() const
  {
    return layerAirportOverviewRunway;
  }

  bool isAirportDiagram() const
  {
    return layerAirportDiagram;
  }

  bool isAirportDiagramRunway() const
  {
    return layerAirportDiagramRunway;
  }

  bool isAirportDiagramDetail() const
  {
    return layerAirportDiagramDetail;
  }

  bool isAirportDiagramDetail2() const
  {
    return layerAirportDiagramDetail2;
  }

  bool isAirportDiagramDetail3() const
  {
    return layerAirportDiagramDetail3;
  }

  bool isAirportMinor() const
  {
    return layerAirportMinor;
  }

  bool isAirportNoRating() const
  {
    return layerAirportNoRating;
  }

  int getAirportSymbolSize() const
  {
    return layerAirportSymbolSize;
  }

  bool isAirportIdent() const
  {
    return layerAirportIdent;
  }

  bool isAirportName() const
  {
    return layerAirportName;
  }

  bool isAirportInfo() const
  {
    return layerAirportInfo;
  }

  bool isAirportRouteInfo() const
  {
    return layerAirportRouteInfo;
  }

  int getMinRunwayLength() const
  {
    return layerMinRunwayLength;
  }

  bool isWaypoint() const
  {
    return layerWaypoint;
  }

  bool isWaypointName() const
  {
    return layerWaypointName;
  }

  bool isWaypointRouteName() const
  {
    return layerWaypointRouteName;
  }

  bool isVor() const
  {
    return layerVor;
  }

  bool isVorLarge() const
  {
    return layerVorLarge;
  }

  bool isVorIdent() const
  {
    return layerVorIdent;
  }

  bool isVorInfo() const
  {
    return layerVorInfo;
  }

  bool isVorRouteIdent() const
  {
    return layerVorRouteIdent;
  }

  bool isVorRouteInfo() const
  {
    return layerVorRouteInfo;
  }

  bool isNdb() const
  {
    return layerNdb;
  }

  bool isNdbIdent() const
  {
    return layerNdbIdent;
  }

  bool isNdbInfo() const
  {
    return layerNdbInfo;
  }

  bool isNdbRouteIdent() const
  {
    return layerNdbRouteIdent;
  }

  bool isNdbRouteInfo() const
  {
    return layerNdbRouteInfo;
  }

  bool isHolding() const
  {
    return layerHolding;
  }

  bool isHoldingInfo() const
  {
    return layerHoldingInfo;
  }

  bool isHoldingInfo2() const
  {
    return layerHoldingInfo2;
  }

  bool isMarker() const
  {
    return layerMarker;
  }

  bool isMarkerInfo() const
  {
    return layerMarkerInfo;
  }

  bool isUserpoint() const
  {
    return layerUserpoint;
  }

  bool isUserpointInfo() const
  {
    return layerUserpointInfo;
  }

  int getUserPointSymbolSize() const
  {
    return layerUserpointSymbolSize;
  }

  bool isIls() const
  {
    return layerIls;
  }

  bool isIlsIdent() const
  {
    return layerIlsIdent;
  }

  bool isIlsInfo() const
  {
    return layerIlsInfo;
  }

  bool isAirway() const
  {
    return layerAirway;
  }

  bool isAirwayWaypoint() const
  {
    return layerAirwayWaypoint;
  }

  bool isAirwayIdent() const
  {
    return layerAirwayIdent;
  }

  bool isAirwayInfo() const
  {
    return layerAirwayInfo;
  }

  bool isTrack() const
  {
    return layerTrack;
  }

  bool isTrackWaypoint() const
  {
    return layerTrackWaypoint;
  }

  bool isTrackIdent() const
  {
    return layerTrackIdent;
  }

  bool isTrackInfo() const
  {
    return layerTrackInfo;
  }

  int getWaypointSymbolSize() const
  {
    return layerWaypointSymbolSize;
  }

  int getVorSymbolSize() const
  {
    return layerVorSymbolSize;
  }

  int getNdbSymbolSize() const
  {
    return layerNdbSymbolSize;
  }

  int getMarkerSymbolSize() const
  {
    return layerMarkerSymbolSize;
  }

  bool isAirspace() const
  {
    return isAirspaceCenter() || isAirspaceFg() || isAirspaceFirUir() || isAirspaceIcao() || isAirspaceOther() ||
           isAirspaceRestricted() || isAirspaceSpecial();
  }

  bool isAirspaceCenter() const
  {
    return layerAirspaceCenter;
  }

  bool isAirspaceIcao() const
  {
    return layerAirspaceIcao;
  }

  bool isAirspaceFg() const
  {
    return layerAirspaceFg;
  }

  bool isAirspaceFirUir() const
  {
    return layerAirspaceFirUir;
  }

  bool isAirspaceRestricted() const
  {
    return layerAirspaceRestricted;
  }

  bool isAirspaceSpecial() const
  {
    return layerAirspaceSpecial;
  }

  bool isAirspaceOther() const
  {
    return layerAirspaceOther;
  }

  bool isAiAircraftLarge() const
  {
    return layerAiAircraftLarge;
  }

  bool isAiAircraftGround() const
  {
    return layerAiAircraftGround;
  }

  bool isAiAircraftSmall() const
  {
    return layerAiAircraftSmall;
  }

  bool isAiShipLarge() const
  {
    return layerAiShipLarge;
  }

  bool isAiShipSmall() const
  {
    return layerAiShipSmall;
  }

  bool isAiAircraftGroundText() const
  {
    return layerAiAircraftGroundText;
  }

  bool isAiAircraftText() const
  {
    return layerAiAircraftText;
  }

  bool isOnlineAircraft() const
  {
    return layerOnlineAircraft;
  }

  bool isOnlineAircraftText() const
  {
    return layerOnlineAircraftText;
  }

  int getMaxTextLengthAirport() const
  {
    return maximumTextLengthAirport;
  }

  int getMaxTextLengthUserpoint() const
  {
    return maximumTextLengthUserpoint;
  }

  bool isAirportWeather() const
  {
    return layerAirportWeather;
  }

  bool isAirportWeatherDetails() const
  {
    return layerAirportWeatherDetails;
  }

  bool isAirportMsa() const
  {
    return layerAirportMsa;
  }

  bool isAirportMsaDetails() const
  {
    return layerAirportMsaDetails;
  }

  int getAirportMsaSymbolScale() const
  {
    return layerAirportMsaSymbolScale;
  }

  /* minimum off route altitude */
  bool isMora() const
  {
    return layerMora;
  }

  bool isRouteTextAndDetail() const
  {
    return layerRouteTextAndDetail;
  }

  bool isWindBarbs() const
  {
    return layerWindBarbs;
  }

  int getWindBarbsSymbolSize() const
  {
    return layerWindBarbsSymbolSize;
  }

  int getAiAircraftSize() const
  {
    return layerAiAircraftSize;
  }

  bool isAirportMinorIdent() const
  {
    return layerAirportMinorIdent;
  }

  bool isAirportMinorName() const
  {
    return layerAirportMinorName;
  }

  bool isAirportMinorInfo() const
  {
    return layerAirportMinorInfo;
  }

  int getAirportMinorSymbolSize() const
  {
    return layerAirportMinorSymbolSize;
  }

  int getMaximumTextLengthAirportMinor() const
  {
    return maximumTextLengthAirportMinor;
  }

  float getAirportMinorFontScale() const
  {
    return layerAirportMinorFontScale;
  }

  float getAirportFontScale() const
  {
    return layerAirportFontScale;
  }

  /* Load layer data from stream which is already positioned inside a layer element */
  void loadFromXml(atools::util::XmlStream& xmlStream);

private:
  friend QDebug operator<<(QDebug out, const MapLayer& record);

  float maxRange = -1.; /* KM */

  bool layerAirport = true, layerAirportOverviewRunway = true, layerAirportDiagram = true,
       layerAirportDiagramRunway = true, layerAirportDiagramDetail = true, layerAirportDiagramDetail2 = true,
       layerAirportDiagramDetail3 = true, layerAirportNoRating = true,
       layerAirportIdent = true, layerAirportName = true, layerAirportInfo = true, layerApproach = true,

       layerAirportMinor = true, layerAirportMinorIdent = true, layerAirportMinorName = true, layerAirportMinorInfo = true,

       layerApproachDetail = true, layerApproachText = true, layerApproachTextDetail = true, layerRouteTextAndDetail = true,
       layerUserpoint = true;

  bool layerAirportWeather = true, layerAirportWeatherDetails = true;

  bool layerAirportMsa = true, layerAirportMsaDetails = true;

  int layerAirportSymbolSize = 3, layerAirportMinorSymbolSize = 3, layerMinRunwayLength = 0;

  bool layerWindBarbs = true;
  int layerWindBarbsSymbolSize = 6;

  float layerAirportMsaSymbolScale = 6.f;
  float layerAirportMinorFontScale = 1.f, layerAirportFontScale = 1.f;

  bool layerWaypoint = true, layerWaypointName = true, layerVor = true, layerVorIdent = true, layerVorInfo = true,
       layerVorLarge = true, layerNdb = true, layerNdbIdent = true, layerNdbInfo = true, layerMarker = true,
       layerMarkerInfo = true, layerUserpointInfo = true, layerIls = true, layerIlsIdent = true,
       layerIlsInfo = true, layerAirway = true, layerAirwayWaypoint = true, layerAirwayIdent = true,
       layerAirwayInfo = true, layerTrack = true, layerTrackWaypoint = true, layerTrackIdent = true,
       layerTrackInfo = true, layerMora = true,
       layerHolding = true, layerHoldingInfo = true, layerHoldingInfo2 = true;

  bool layerAirportRouteInfo = true;
  bool layerVorRouteIdent = true, layerVorRouteInfo = true;
  bool layerNdbRouteIdent = true, layerNdbRouteInfo = true;
  bool layerWaypointRouteName = true;

  int layerWaypointSymbolSize = 3, layerVorSymbolSize = 3, layerNdbSymbolSize = 4,
      layerMarkerSymbolSize = 8, layerUserpointSymbolSize = 12;

  int maximumTextLengthAirport = 16, maximumTextLengthAirportMinor = 16, maximumTextLengthUserpoint = 10;

  bool layerAirspaceCenter = true, layerAirspaceIcao = true, layerAirspaceFg = true, layerAirspaceFirUir = true,
       layerAirspaceRestricted = true, layerAirspaceSpecial = true, layerAirspaceOther = true;

  int layerAiAircraftSize = 32;
  bool layerAiAircraftGround = true, layerAiAircraftLarge = true, layerAiAircraftSmall = true,
       layerOnlineAircraft = true, layerAiShipLarge = true, layerAiShipSmall = true,
       layerAiAircraftGroundText = true, layerAiAircraftText = true, layerOnlineAircraftText = true;
};

#endif // LITTLENAVMAP_MAPLAYER_H
