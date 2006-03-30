/****************************************************************************

  KOM RSVP Engine (release version 3.0f)
  Copyright (C) 1999-2004 Martin Karsten

  This program is free software; you can redistribute it and/or
  modify it under the terms of the GNU General Public License
  as published by the Free Software Foundation; either version 2
  of the License, or (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.

  Contact:	Martin Karsten
		TU Darmstadt, FG KOM
		Merckstr. 25
		64283 Darmstadt
		Germany
		Martin.Karsten@KOM.tu-darmstadt.de

  Other copyrights might apply to parts of this package and are so
  noted when applicable. Please see file COPYRIGHT.other for details.

****************************************************************************/
#if !defined(NS2)

#include "RSVP_ConfigFileReader.h"
#include "RSVP.h"
#include "RSVP_BaseTimer.h"
#include "RSVP_Log.h"
#include "RSVP_LogicalInterface.h"
#include "RSVP_MPLS.h"
#include "RSVP_IntServObjects.h"
#include "RSVP_RoutingService.h"
#include "RSVP_TrafficControl.h"
#include "RSVP_SchedulerCBQ.h"
#include "RSVP_SchedulerHFSC.h"
#include "RSVP_SchedulerRate.h"
#include "RSVP_UNI.h"

void ConfigFileReader::setTimerTotal( uint32 x ) {
	TimerSystem::totalPeriod = TimeValue( x, 0 );
}

void ConfigFileReader::setTimerSlots( uint32 x ) {
	TimerSystem::slotCount = x;
}

void ConfigFileReader::setSessionHash( uint32 x ) {
	RSVP_Global::sessionHashCount = x;
}

void ConfigFileReader::setApiHash( uint32 x ) {
	RSVP_Global::apiHashCount = x;
}

void ConfigFileReader::setIdHashSend( uint32 x ) {
	RSVP_Global::idHashCountSend = x;
}

void ConfigFileReader::setIdHashRecv( uint32 x ) {
	RSVP_Global::idHashCountRecv = x;
}

void ConfigFileReader::setListAlloc( uint32 x ) {
	RSVP_Global::listAlloc = x;
}

void ConfigFileReader::setSB_Alloc( uint32 x ) {
	RSVP_Global::sbAlloc = x;
}

void ConfigFileReader::setGlobalMPLS( bool m ) {
	RSVP_Global::mplsDefault = m;
	LogicalInterfaceList::Iterator iter = tmpLifList.begin();
	for ( ;iter != tmpLifList.end(); ++iter ) {
		(*iter)->setMPLS( m );
	}
}

void ConfigFileReader::setLabelHash( uint32 l ) {
	RSVP_Global::labelHashCount = l;
}

void ConfigFileReader::createInterface() {
	LogicalInterface* lif = NULL;
	// find or potentially create the interface
	LogicalInterfaceList::Iterator lifIter = tmpLifList.begin();
	for ( ; lifIter != tmpLifList.end(); ++lifIter ) {
		if ( (*lifIter)->getName() == interfaceName ) {
			lif = *lifIter;
	break;
		}
	}
	if ( encap ) {
#if defined(VIRT_NETWORK)
		if ( !lif ) {
			lif = new LogicalInterfaceUDP( interfaceName, virtAddress, virtMTU );
			tmpLifList.push_back( lif );
			if ( virt ) {
				reinterpret_cast<LogicalInterfaceUDP*>(lif)->configureUDP( localPort, remoteAddress, remotePortList );
			}
			LOG(2)( Log::Config, "Created interface:", *lif );
			if (lossProb != 0) reinterpret_cast<LogicalInterfaceUDP*>(lif)->setLossProbability( (uint32)(LogicalInterfaceUDP::lossProbabilityScale * lossProb) );
		} else
#endif
		{
			LOG(2)( Log::Config, "Ignoring encap settings in config file for interface: ", interfaceName );
		}
	}
	if ( !lif ) {
		LOG(3)( Log::Config, "Interface:", interfaceName, "does not exist -> ignoring config settings" );
		if (tc) delete tc;
	return;
	}
	if (disable) lif->disable();
	lif->setMPLS( mpls );
#if defined(REFRESH_REDUCTION)
	if (rapidRefreshRate) lif->setRapidRefreshInterval( rapidRefreshRate );
#endif
	if (refresh) lif->configureRefresh( refreshRate/1000 );
	if (tc) lif->configureTC( tc );
}

void ConfigFileReader::createTC_NONE() {
	tc = new TrafficControl( NULL );
}

void ConfigFileReader::createTC_CBQ() {
	tc = new TrafficControl( new SchedulerCBQ( bandwidth, latency ) );
}

void ConfigFileReader::createTC_HFSC() {
	tc = new TrafficControl( new SchedulerHFSC( bandwidth, latency ) );
}

void ConfigFileReader::createTC_Rate() {
	tc = new TrafficControl( new SchedulerRate( bandwidth, latency ) );
}

void ConfigFileReader::createRoute() {
	const LogicalInterface* lif = NULL;
	LogicalInterfaceList::Iterator lifIter = tmpLifList.begin();
	for ( ; lifIter != tmpLifList.end(); ++lifIter ) {
		if ( (*lifIter)->getName() == interfaceName ) {
			lif = *lifIter;
		}
	}
	if (lif) {
		rsvp.getRoutingService().addRoute( RoutingEntry(dest,mask,gateway,lif) );
	}
}

void ConfigFileReader::addExplicitRouteHop( const NetAddress& a ) {
	explicitRouteHops.push_back( a );
}

void ConfigFileReader::setExplicitRoute() {
	NetAddress dest = explicitRouteHops.back();
	explicitRouteHops.pop_back();
	if ( !explicitRouteHops.empty() ) {
		RSVP_Global::rsvp->getMPLS().addExplicitRoute( dest, explicitRouteHops );
	}
}

void ConfigFileReader::addHop( uint32 hc ) {
	Hop hop;
	hop.iface = localAddress;
	hop.addr = remoteAddress;
	hop.hopCount = hc;
	hopList.push_back( hop );
}

void ConfigFileReader::setApiPort( uint16 apiPort ) {
#if defined(WITH_API)
	RSVP::configureAPI( apiPort );
#endif
}

void ConfigFileReader::setNarbApiClient(String host, int port) {
    NARB_APIClient::setHostPort(host.chars(), port);
}

void ConfigFileReader::addSlot(String slot_type, uint16 slot_num) {
	slot_entry se;
	if (slot_type == "gi") {
		se.slot_type = SLOT_TYPE_GIGE;
	}
	else if (slot_type == "te") {
		se.slot_type = SLOT_TYPE_TENGIGE;
	}
	else
		return;
	se.slot_num = slot_num;
	RSVP_Global::switchController->addSlotEntry(se);
}

void ConfigFileReader::addUNI(uint32 type) {
	LogicalInterface* lif = NULL;
	LogicalInterfaceList::Iterator lifIter = tmpLifList.begin();
	for ( ; lifIter != tmpLifList.end(); ++lifIter ) {
		if ( (*lifIter)->getName() == interfaceName ) {
			lif = *lifIter;
		}
	}

	if (!lif) {
		ERROR(3)( Log::Config, "UNI Control Channel Interface:", interfaceName, "does not exist -> ignoring config settings" );
		return;
	}

	UNI* uni = NULL;
	switch (type) {
	case UNI::UNI_C:
		uni = new UNI((UNI::Type)type, localAddress,  remoteAddress, lif, loopbackAddress);
		break;
	case UNI::UNI_N:
		uni = new UNI((UNI::Type)type, remoteAddress, localAddress, lif, loopbackAddress);
		break;
	default:
		return;
	}
	if (uni) rsvp.addUNI(uni);
}


void ConfigFileReader::cleanup() {
	interfaceName = "";
	localAddress = remoteAddress = virtAddress = 0;
	virtMTU = localPort = 0;
	bandwidth = lossProb = 0;
	refreshRate = latency = rapidRefreshRate = 0;
	disable = virt = encap = refresh = false;
	mpls = RSVP_Global::mplsDefault;
	dest = mask = gateway = 0;
	remotePortList.clear();
	explicitRouteHops.clear();
	tc = NULL;
}

void ConfigFileReader::warn( const String& filename ) {
	FATAL(2)( Log::Fatal, "ERROR: cannot access config file", filename );
	abortProcess();
}

#endif /* NS2 */
