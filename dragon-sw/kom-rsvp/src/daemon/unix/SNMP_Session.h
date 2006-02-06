/****************************************************************************

SNMP Based Switch Control Module header file SNMP_Session.h
Created by Xi Yang @ 01/17/2006
Extended from SNMP_Global.h by Aihua Guo and Xi Yang, 2004-2005
To be incorporated into KOM-RSVP-TE package

****************************************************************************/

#ifndef _SNMP_SESSION_H_
#define _SNMP_SESSION_H_

#include "SwitchCtrl_Global.h"

class SNMP_Session: public SwitchCtrl_Session
{
	
public:
	SNMP_Session(): SwitchCtrl_Session() { }
	SNMP_Session(const RSVP_String& sName, const NetAddress& swAddr): SwitchCtrl_Session(sName, swAddr) { }
	virtual ~SNMP_Session() { }

	////////// Below are vendor specific functions/////////
	virtual bool movePortToVLANAsTagged(uint32 port, uint32 vlanID);
	virtual bool movePortToVLANAsUntagged(uint32 port, uint32 vlanID);
	virtual bool removePortFromVLAN(uint32 port, uint32 vlanID);

	///////////------QoS Functions ------/////////
	virtual bool policeInputBandwidth(bool do_undo, uint32 input_port, uint32 vlan_id, float committed_rate, int burst_size=0, float peak_rate=0.0,  int peak_burst_size=0) { return false; }
	virtual bool limitOutputBandwidth(bool do_undo,  uint32 output_port, uint32 vlan_id, float committed_rate, int burst_size=0, float peak_rate=0.0,  int peak_burst_size=0) { return false; }

	////////-----Vendor/Model specific hook functions------//////
	virtual bool hook_createVLAN(const uint32 vlanID) { return false; }
	virtual bool hook_removeVLAN(const uint32 vlanID) { return false; }
	virtual bool hook_isVLANEmpty(const vlanPortMap &vpm);
       virtual void hook_getPortMapFromSnmpVars(vlanPortMap &vpm, netsnmp_variable_list *vars);
	virtual bool hook_hasPortinVlanPortMap(vlanPortMap &vpm, uint32  port);

	///////------Vendor/model specific functions--------///////
	bool setVLANPVID(uint32 port, uint32 vlanID); // A hack to Dell 5324 switch
	bool setVLANPortTag(uint32 portListNew, uint32 vlanID); // A hack to Dell 5324 switch
	// RFC2674 and IntelES530
	bool setVLANPort(uint32 portListNew, uint32 vlanID); // vendor specific
	// RFC2674 (DELL and Extreme)
	bool movePortToDefaultVLAN(uint32 port); 
};


#endif //ifndef _SNMP_SESSION_H_
