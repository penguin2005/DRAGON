/****************************************************************************

Switch Control Module source file SwitchCtrl_Global.cc
Created by Xi Yang @ 01/12/2006
Extended from SNMP_Global.cc by Aihua Guo and Xi Yang, 2004-2005
To be incorporated into KOM-RSVP-TE package

****************************************************************************/

#include "SwitchCtrl_Global.h"
#include "RSVP_Log.h"
#include "RSVP_Message.h"
#include "SNMP_Session.h"
#include "CLI_Session.h"
#include "SwitchCtrl_Session_Force10E600.h"
#include "SwitchCtrl_Session_RaptorER1010.h"

#include <signal.h>

////////////////Global Definitions//////////////

LocalIdList SwitchCtrl_Global::localIdList;


/////////////////////////////////////////////////////////////
////////////////SwitchCtrl_Session Implementation////////////////
/////////////////////////////////////////////////////////////

bool SwitchCtrl_Session::connectSwitch() 
{
    if (!snmp_enabled)
        return false;

    if (!SwitchCtrl_Global::static_connectSwitch(snmpSessionHandle, switchInetAddr))
        return false;

    //Read Ethernet switch vendor info and verify against the compilation configuration
    if (!(getSwitchVendorInfo() && readVLANFromSwitch())){ 
    	disconnectSwitch();
    	return false;
    }

    active = true;
    return true;
}

void SwitchCtrl_Session::disconnectSwitch() 
{ 
    if (snmp_enabled)
        SwitchCtrl_Global::static_disconnectSwitch(snmpSessionHandle); 
}

bool SwitchCtrl_Session::getSwitchVendorInfo()
{ 
    bool ret;
    if (!snmp_enabled)
        return false;

    ret = SwitchCtrl_Global::static_getSwitchVendorInfo(snmpSessionHandle, vendor, venderSystemDescription); 
    switch (vendor) {
    case RFC2674:
    case IntelES530:
        rfc2674_compatible = snmp_enabled = true;
        break;
    }

    return ret;
}

bool SwitchCtrl_Session::createVLAN(uint32 &vlanID)
{
    vlanPortMapList::ConstIterator iter;

    //@@@@ vlanID == 0 is supposed to create an arbitrary new VLAN and re-assign the vlanID.
    //@@@@ For now, we igore this case and only create VLAN for a specified vlanID > 0.
    if (vlanID == 0)
        return false;

    //check if the VLAN has already been existing
    for (iter = vlanPortMapListAll.begin(); iter != vlanPortMapListAll.end(); ++iter) {
        if ((*iter).vid == vlanID)
            return false;
    }

    //otherwise, create it
    if (!hook_createVLAN(vlanID))
        return false;

    //add the new *empty* vlan into PortMapListAll and portMapListUntagged
    vlanPortMap vpm;
    memset(&vpm, 0, sizeof(vlanPortMap));
    vpm.vid = vlanID;
    vlanPortMapListAll.push_back(vpm);
    vlanPortMapListUntagged.push_back(vpm);

    return true;
}

bool SwitchCtrl_Session::removeVLAN(const uint32 vlanID)
{
    vlanPortMapList::ConstIterator iter;

    if (vlanID == 0)
        return false;

    if (!hook_removeVLAN(vlanID))
        return false;

    //remove the vlan from vlanPortMapLists
    for (iter = vlanPortMapListAll.begin(); iter != vlanPortMapListAll.end(); ++iter) {
        if ((*iter).vid == vlanID) {
            vlanPortMapListAll.erase(iter);
            break;
        }
    }

    for (iter = vlanPortMapListUntagged.begin(); iter != vlanPortMapListUntagged.end(); ++iter) {
        if ((*iter).vid == vlanID) {
            vlanPortMapListUntagged.erase(iter);
            break;
        }
    }        

    return true;
}

bool SwitchCtrl_Session::isVLANEmpty(const uint32 vlanID)
{
    vlanPortMapList::ConstIterator iter;
    for (iter = vlanPortMapListAll.begin(); iter != vlanPortMapListAll.end(); ++iter) {
        if ((*iter).vid == vlanID && hook_isVLANEmpty(*iter))
            return true;
    }
    return false;
}

const uint32 SwitchCtrl_Session::findEmptyVLAN()
{
    vlanPortMapList::ConstIterator iter;
    for (iter = vlanPortMapListAll.begin(); iter != vlanPortMapListAll.end(); ++iter) {
        if (hook_isVLANEmpty(*iter))
            return (*iter).vid;
    }
    return 0;
}

void SwitchCtrl_Session::readVlanPortMapBranch(const char* oid_str, vlanPortMapList &vpmList)
{
    struct snmp_pdu *pdu;
    struct snmp_pdu *response;
    netsnmp_variable_list *vars;
    oid anOID[MAX_OID_LEN];
    oid root[MAX_OID_LEN];
    size_t anOID_len = MAX_OID_LEN;
    int status;
    vlanPortMap portmap;
    bool running = true;
    size_t rootlen;

    if (!rfc2674_compatible || !snmp_enabled)
        return;

    status = read_objid(oid_str, anOID, &anOID_len);
    rootlen = anOID_len;
    memcpy(root, anOID, rootlen*sizeof(oid));
    vpmList.clear();
    while (running) {
        // Create the PDU for the data for our request.
        pdu = snmp_pdu_create(SNMP_MSG_GETNEXT);
        snmp_add_null_var(pdu, anOID, anOID_len);
        // Send the Request out.
        status = snmp_synch_response(snmpSessionHandle, pdu, &response);
        if (status == STAT_SUCCESS && response->errstat == SNMP_ERR_NOERROR) {
            for (vars = response->variables; vars; vars = vars->next_variable) {
                if ((vars->name_length < rootlen) || (memcmp(anOID, vars->name, rootlen * sizeof(oid)) != 0)) {
                    running = false;
                    continue;
                }

                hook_getPortMapFromSnmpVars(portmap, vars);
                 
                vpmList.push_back(portmap);
                if ((vars->type != SNMP_ENDOFMIBVIEW) &&
                    (vars->type != SNMP_NOSUCHOBJECT) &&
                    (vars->type != SNMP_NOSUCHINSTANCE)) {
                    memcpy((char *)anOID, (char *)vars->name, vars->name_length * sizeof(oid));
                    anOID_len = vars->name_length;
                }
                else {
                    running = 0;
                }
            }
        }
        else {
            running = false;
        }
        if(response) snmp_free_pdu(response);
    }
}

bool SwitchCtrl_Session::readVLANFromSwitch()
{
    bool ret = true;;

    if (!hook_createVlanInterfaceToIDRefTable(vlanRefIdConvList))
        return false;

    readVlanPortMapBranch(".1.3.6.1.2.1.17.7.1.4.3.1.2", vlanPortMapListAll);
    if (vlanPortMapListAll.size() == 0)
        ret = false;

    readVlanPortMapBranch(".1.3.6.1.2.1.17.7.1.4.3.1.4", vlanPortMapListUntagged);
    if (vlanPortMapListUntagged.size() == 0)
        ret = false;

    return ret;
}

uint32 SwitchCtrl_Session::getVLANbyPort(uint32 port){
    vlanPortMapList::Iterator iter;
    for (iter = vlanPortMapListAll.begin(); iter != vlanPortMapListAll.end(); ++iter) {
        if (hook_hasPortinVlanPortMap(*iter, port))
            return (*iter).vid;
    }

    return 0;
}

uint32 SwitchCtrl_Session::getVLANListbyPort(uint32 port, SimpleList<uint32> &vlan_list){
    vlan_list.clear();
    vlanPortMapList::Iterator iter;
    for (iter = vlanPortMapListAll.begin(); iter != vlanPortMapListAll.end(); ++iter) {
        if (hook_hasPortinVlanPortMap(*iter, port))
            vlan_list.push_back((*iter).vid);
    }

    return vlan_list.size();
}

uint32 SwitchCtrl_Session::getVLANbyUntaggedPort(uint32 port){
    vlanPortMapList::Iterator iter;
    for (iter = vlanPortMapListUntagged.begin(); iter != vlanPortMapListUntagged.end(); ++iter) {
        if (hook_hasPortinVlanPortMap(*iter, port))	                
            return (*iter).vid;
    }

    return 0;
}

bool SwitchCtrl_Session::verifyVLAN(uint32 vlanID)
{
    struct snmp_pdu *pdu;
    struct snmp_pdu *response;
    oid anOID[MAX_OID_LEN];
    size_t anOID_len = MAX_OID_LEN;
    char oid_str[128];
    int status;
    String tag_oid_str = ".1.3.6.1.2.1.17.7.1.4.3.1.2";

    if (!active || !rfc2674_compatible || !snmp_enabled)
        return false;

    vlanID = hook_convertVLANIDToInterface(vlanID);

    if (vlanID == 0)
        return false;

    pdu = snmp_pdu_create(SNMP_MSG_GET);
    // vlan port list 
    sprintf(oid_str, "%s.%d", tag_oid_str.chars(), vlanID);
    status = read_objid(oid_str, anOID, &anOID_len);
    snmp_add_null_var(pdu, anOID, anOID_len);
    // Send the Request out. 
    status = snmp_synch_response(snmpSessionHandle, pdu, &response);
    if (status == STAT_SUCCESS && response->errstat == SNMP_ERR_NOERROR) {
        	snmp_free_pdu(response);
                return true;
    	}
    if(response) 
        snmp_free_pdu(response);
    return false;
}


bool SwitchCtrl_Session::VLANHasTaggedPort(uint32 vlanID)
{
    struct snmp_pdu *pdu;
    struct snmp_pdu *response;
    oid anOID[MAX_OID_LEN];
    size_t anOID_len = MAX_OID_LEN;
    char oid_str[128];
    netsnmp_variable_list *vars;
    int status;
    vlanPortMap portmap_all, portmap_untagged;
    String oid_str_all = ".1.3.6.1.2.1.17.7.1.4.3.1.2";    
    String oid_str_untagged = ".1.3.6.1.2.1.17.7.1.4.3.1.4";

    if (!active || !rfc2674_compatible || !snmp_enabled)
        return false;

    memset(&portmap_all, 0, sizeof(vlanPortMap));

    portmap_all.vid = hook_convertVLANIDToInterface(vlanID);
    portmap_untagged = portmap_all;

    pdu = snmp_pdu_create(SNMP_MSG_GET);
    // all vlan ports list 
    sprintf(oid_str, "%s.%d", oid_str_all.chars(), vlanID);
    status = read_objid(oid_str, anOID, &anOID_len);
    snmp_add_null_var(pdu, anOID, anOID_len);
    // Send the Request out. 
    status = snmp_synch_response(snmpSessionHandle, pdu, &response);
    if (status == STAT_SUCCESS && response->errstat == SNMP_ERR_NOERROR) {
        vars = response->variables;

        hook_getPortMapFromSnmpVars(portmap_all, vars);
        if (portmap_all.vid == 0)
            return false;
    }
    else 
        return false;
    if(response) 
      snmp_free_pdu(response);

    pdu = snmp_pdu_create(SNMP_MSG_GET);
    // untagged vlan ports list 
    sprintf(oid_str, "%s.%d", oid_str_untagged.chars(), vlanID);
    status = read_objid(oid_str, anOID, &anOID_len);
    snmp_add_null_var(pdu, anOID, anOID_len);
    // Send the Request out. 
    status = snmp_synch_response(snmpSessionHandle, pdu, &response);
    if (status == STAT_SUCCESS && response->errstat == SNMP_ERR_NOERROR) {

        vars = response->variables;

        hook_getPortMapFromSnmpVars(portmap_untagged, vars);
        if (portmap_untagged.vid == 0)
            return false;
    }
    else 
        return false;
    if(response) 
      snmp_free_pdu(response);

    if (memcmp(&portmap_all, &portmap_untagged, sizeof(vlanPortMap)) == 0)
        return false;

    return true;
}

bool SwitchCtrl_Session::setVLANPortsTagged(uint32 taggedPorts, uint32 vlanID)
{
    struct snmp_pdu *pdu;
    struct snmp_pdu *response;
    oid anOID[MAX_OID_LEN];
    size_t anOID_len = MAX_OID_LEN;
    char type, value[128], oid_str[128];
    int status;
    String tag_oid_str = ".1.3.6.1.2.1.17.7.1.4.3.1.4";
    uint32 ports = 0;

    //not initialized or session has been disconnected; RFC2674 only!
    if (!active || !rfc2674_compatible || !snmp_enabled)
    	return false;

    pdu = snmp_pdu_create(SNMP_MSG_GET);
    // vlan port list 
    sprintf(oid_str, "%s.%d", tag_oid_str.chars(), vlanID);
    status = read_objid(oid_str, anOID, &anOID_len);
    snmp_add_null_var(pdu, anOID, anOID_len);
    // Send the Request out. 
    status = snmp_synch_response(snmpSessionHandle, pdu, &response);
    if (status == STAT_SUCCESS && response->errstat == SNMP_ERR_NOERROR) {
    	if (response->variables->val.integer){
    		ports = ntohl(*(response->variables->val.integer));
    		if (response->variables->val_len < 4){
    			uint32 mask = (uint32)0xFFFFFFFF << ((4-response->variables->val_len)*8); 
    			ports &= mask; 
    		}
    	}
    }
    if(response) 
        snmp_free_pdu(response);

    ports &= (~taggedPorts) & 0xFFFFFFFF; //take port away from those untagged
    pdu = snmp_pdu_create(SNMP_MSG_SET);
    // vlan port list 
    type='x';   
    sprintf(value, "%.8lx", 0); // set all ports tagged
    status = snmp_add_var(pdu, anOID, anOID_len, type, value);
    sprintf(value, "%.8lx", ports); //restore those originally untagged ports execept the 'taggedPorts'
    status = snmp_add_var(pdu, anOID, anOID_len, type, value);
    // Send the Request out. 
    status = snmp_synch_response(snmpSessionHandle, pdu, &response);
    if (status == STAT_SUCCESS && response->errstat == SNMP_ERR_NOERROR) {
    	snmp_free_pdu(response);
    }
    else {
    	if (status == STAT_SUCCESS){
    	LOG(2)( Log::MPLS, "VLSR: SNMP: Setting VLAN Tag failed. Reason : ", snmp_errstring(response->errstat));
    	}
    	else
      		snmp_sess_perror("snmpset", snmpSessionHandle);
    	if(response)
        	snmp_free_pdu(response);
        return false;
    }
    return true;
}



/////////////////////////////////////////////////////////////
//////////////////SwitchCtrl_Global Implementation///////////////
/////////////////////////////////////////////////////////////

SwitchCtrl_Global&	SwitchCtrl_Global::instance() {
	static SwitchCtrl_Global controller;
	return controller;
}

SwitchCtrl_Global::SwitchCtrl_Global() 
{
	static bool first = true;
	sessionList.clear(); 
	init_snmp("VLSRCtrl");
	if (first)
	{
		//read preserved local ids in case  dragon CLI update localids while RSVPD restarts 
		readPreservedLocalIds();
		first = false;
	}
}

SwitchCtrl_Global::~SwitchCtrl_Global() {
	SwitchCtrlSessionList::Iterator sessionIter = sessionList.begin();
	for ( ; sessionIter != sessionList.end(); ++sessionIter){
		(*sessionIter)->disconnectSwitch();
		sessionList.erase(sessionIter);
	}
}

bool SwitchCtrl_Global::refreshSessions()
{
	bool ret = true;
	SwitchCtrlSessionList::Iterator sessionIter = sessionList.begin();
	for ( ; sessionIter != sessionList.end(); ++sessionIter){
		ret &= (*sessionIter)->refresh();
	}
	return ret;
}
bool SwitchCtrl_Global::static_connectSwitch(struct snmp_session* &sessionHandle, NetAddress& switchAddr)
{
	char str[128];
	char* community = "dragon";
        snmp_session session;
	 // Initialize a "session" that defines who we're going to talk to   
	 snmp_sess_init(&session);
	 // set up defaults   
	 strcpy(str, convertAddressToString(switchAddr).chars());
	 session.peername = str;  
	 // set the SNMP version number   
	 session.version = SNMP_VERSION_1;  
	 // set the SNMPv1 community name used for authentication   
	 session.community = (u_char*)community;  
	 session.community_len = strlen((const char*)session.community);  
	 // Open the session   
	 if (!(sessionHandle = snmp_open(&session))){
		snmp_perror("snmp_open");
		return false;  
	 }
	return true;
}

void SwitchCtrl_Global::static_disconnectSwitch(struct snmp_session* &sessionHandle) 
{
    snmp_close(sessionHandle);
}

bool SwitchCtrl_Global::static_getSwitchVendorInfo(struct snmp_session* &sessionHandle, uint32 &vendor, String &venderSystemDescription) 
{
    struct snmp_pdu *pdu;
    struct snmp_pdu *response;
    oid anOID[MAX_OID_LEN];
    size_t anOID_len = MAX_OID_LEN;
    int status;

    // Create the PDU for the data for our request. 
    pdu = snmp_pdu_create(SNMP_MSG_GET);

    // vlan port list 
    char* oid_str = "system.sysDescr.0";
    if (!snmp_parse_oid(oid_str, anOID, &anOID_len)) {
        	snmp_perror(oid_str);
        	return false;
    }
    else
      snmp_add_null_var(pdu, anOID, anOID_len);

    // Send the Request out. 
    status = snmp_synch_response(sessionHandle, pdu, &response);

    if (status == STAT_SUCCESS && response->errstat == SNMP_ERR_NOERROR) {
        char vname[128];
        strncpy(vname, (const char*)response->variables->val.string, response->variables->val_len);
        vname[response->variables->val_len] = 0;

        venderSystemDescription = vname;
        snmp_free_pdu(response);
        if (String("PowerConnect 5224") == venderSystemDescription)
        	vendor = RFC2674;
        else if (String("Intel(R) Express 530T Switch ") == venderSystemDescription)
        	vendor = IntelES530;
        else if (String("Ethernet Switch") == venderSystemDescription)
        	vendor = RFC2674;
        else if (String("Ethernet Routing Switch") == venderSystemDescription) // Dell PowerConnect 6024/6024F
        	vendor = RFC2674;
        else if (venderSystemDescription.leftequal("Summit1i") || venderSystemDescription.leftequal("Summit5i")) 
        	vendor = RFC2674;
        else if (venderSystemDescription.leftequal("Ether-Raptor"))
        	vendor = RFC2674;
        else if (venderSystemDescription.leftequal("Spectra")) 
        	vendor = LambdaOptical;
        else if (venderSystemDescription.leftequal("Force10 Networks Real Time Operating System Software")) 
        	vendor = Force10E600;
        else if (venderSystemDescription.leftequal("Ether-Raptor")) 
        	vendor = RaptorER1010;
        else{
        	vendor = Illegal;
         	return false;
        }
    }
    else {
        if (status == STAT_SUCCESS){
            LOG(2)( Log::MPLS, "VLSR: SNMP: Reading vendor info failed. Reason : ", snmp_errstring(response->errstat));
        }
        else
            snmp_sess_perror("snmpget", sessionHandle);
        if(response) snmp_free_pdu(response);
        return false;
    }

    return true;
}

SwitchCtrl_Session* SwitchCtrl_Global::createSession(NetAddress& switchAddr)
{
    return createSession(SWITCH_VENDOR_MODEL, switchAddr);
}

SwitchCtrl_Session* SwitchCtrl_Global::createSession(uint32 vendor_model, NetAddress& switchAddr)
{
    String vendor_desc;
    SwitchCtrl_Session* ssNew = NULL;

    if (vendor_model == AutoDetect) {
        snmp_session *snmp_handle;
        if (!SwitchCtrl_Global::static_connectSwitch(snmp_handle, switchAddr))
            return NULL;
        if (!SwitchCtrl_Global::static_getSwitchVendorInfo(snmp_handle, vendor_model, vendor_desc))
            return NULL;
        SwitchCtrl_Global::static_disconnectSwitch(snmp_handle);
    }

    switch(vendor_model) {
        case Force10E600:
            ssNew = new SwitchCtrl_Session_Force10E600("VLSR-Force10", switchAddr);
            break;                                        
        case RaptorER1010:
            ssNew = new SwitchCtrl_Session_RaptorER1010("VLSR-Raptor", switchAddr);
            break;
        case Illegal:
            return NULL;
        default:
            ssNew = new SNMP_Session("VLSR-SNMP", switchAddr);
            break;
    }

    return ssNew;
}
void SwitchCtrl_Global::addLocalId(uint16 type, uint16 value, uint16  tag) 
{
	LocalIdList::Iterator it;
	LocalId lid;

	for (it = localIdList.begin(); it != localIdList.end(); ++it) {
	lid = *it;
	if (lid.type == type && lid.value == value) {
	    if (type == LOCAL_ID_TYPE_GROUP || type == LOCAL_ID_TYPE_TAGGED_GROUP)  {
	        SimpleList<uint16>::Iterator it_uint16;
	        for (it_uint16 = lid.group->begin(); it_uint16 != lid.group->end(); ++it_uint16) {
	            if (*it_uint16 == tag)
	                return;
	            }
	        lid.group->push_back(tag);
	        return;
	        }
	    else
	        return;
	    }
	}
	lid.type = type;
	lid.value = value;
	localIdList.push_back(lid);
	localIdList.back().group = new SimpleList<uint16>;
	if ((type == LOCAL_ID_TYPE_GROUP || type == LOCAL_ID_TYPE_TAGGED_GROUP) && tag != 0)
	    localIdList.back().group->push_back(tag);
}

void SwitchCtrl_Global::deleteLocalId(uint16 type, uint16 value, uint16  tag) 
{
	LocalIdList::Iterator it;
	LocalId lid;
	if (type == 0xffff && value == 0xffff) {
	        //for (it = localIdList.begin(); it != localIdList.end(); ++it)
	         //   if (lid.group)
	         //       delete lid.group;
	        localIdList.clear();
	        return;
	    }
	for (it = localIdList.begin(); it != localIdList.end(); ++it) {
	    lid = *it;
	    if (lid.type == type && lid.value == value) {
	        if ((type == LOCAL_ID_TYPE_GROUP || type == LOCAL_ID_TYPE_TAGGED_GROUP)) {
	            if (tag == 0 && lid.group) {
	                delete lid.group;
	                localIdList.erase(it);
	                }
	            else {
	                SimpleList<uint16>::Iterator it_uint16;
	                for (it_uint16 = lid.group->begin(); it_uint16 != lid.group->end(); ++it_uint16) {
	                    if (*it_uint16 == tag)
	                        lid.group->erase(it_uint16);
	                    }
	                if (lid.group->size() == 0) {
	                    delete lid.group;
	                    localIdList.erase(it);
	                    }
	                }
	            return;
	            }
	        else {
	                delete lid.group;
	                localIdList.erase(it);
	                return;
	            }
	        }
	    }
}

void SwitchCtrl_Global::readPreservedLocalIds() 
{
	ifstream inFile;
	char line[100], *str;
	u_int32_t type, value, tag;

	inFile.open ("/var/preserve/dragon.localids", ifstream::in);
       if (!inFile  || inFile.bad()) 
       {
		LOG(1)(Log::Error, "Failed to open the /var/preserve/dragon.localids...");
		return;
       }
	while (inFile >> line)
	{
		str = strtok(line, " ");
		if(!str) break;
		sscanf(str, "%d:%d", &type, &value);
		if (type == LOCAL_ID_TYPE_GROUP || type == LOCAL_ID_TYPE_TAGGED_GROUP)
		{
			while (str = strtok(NULL, " "))
			{
				if (str) sscanf(str, "%d", &tag);
				else break;
				addLocalId(type, value, tag);
			}
		}
		else
		{
			addLocalId(type, value);
		}
			
	}
}
    
//One unique session per switch
bool SwitchCtrl_Global::addSession(SwitchCtrl_Session* addSS)
{
	SwitchCtrlSessionList::Iterator iter = sessionList.begin();
	for (; iter != sessionList.end(); ++iter ) {
		if ((*(*iter))==(*addSS))
			return false;
	}
	//adding new session
	sessionList.push_back(addSS);
	return  true;
}

void SwitchCtrl_Global::processLocalIdMessage(uint8 msgType, LocalId& lid)
{
    switch(msgType)
    {
    case Message::AddLocalId:
        if (lid.group->size() > 0)
            while (lid.group->size()) {
                addLocalId(lid.type, lid.value, lid.group->front());
                lid.group->pop_front();
            }
        else
            addLocalId(lid.type, lid.value);
        break;
    case Message::DeleteLocalId:
        if (lid.group->size() > 0)
            while (lid.group->size()) {
                deleteLocalId(lid.type, lid.value, lid.group->front());
                lid.group->pop_front();
            }
        else
            deleteLocalId(lid.type, lid.value);
        break;
    default:
        break;
    }
}

void SwitchCtrl_Global::getPortsByLocalId(SimpleList<uint32>&portList, uint32 port)
{
    portList.clear();
    uint16 type = (uint16)(port >> 16);
    uint16 value =(uint16)(port & 0xffff) ;
    if (!hasLocalId(type, value))
        return;
    if (type == LOCAL_ID_TYPE_PORT)
    {
        portList.push_back(value);
        return;
    }
    else if (type != LOCAL_ID_TYPE_GROUP && type != LOCAL_ID_TYPE_TAGGED_GROUP)
        return;
            
    LocalIdList::Iterator it;
    LocalId lid;
    for (it = localIdList.begin(); it != localIdList.end(); ++it) 
    {
        lid = *it;
        if (lid.type == type && lid.value == value) 
        {
            if (!lid.group || lid.group->size() == 0)
                return;
            SimpleList<uint16>::Iterator it_uint16;
            for (it_uint16 = lid.group->begin(); it_uint16 != lid.group->end(); ++it_uint16) 
                   portList.push_back(*it_uint16); //add ports
             return;
        }
    }
}

bool SwitchCtrl_Global::hasLocalId(uint16 type, uint16 value, uint16  tag)
{
    LocalIdList::Iterator it;
    LocalId lid;
    for (it = localIdList.begin(); it != localIdList.end(); ++it) {
        lid = *it;
        if (lid.type == type && lid.value == value) {
            if ((type == LOCAL_ID_TYPE_GROUP || type == LOCAL_ID_TYPE_TAGGED_GROUP) && tag != 0) {
                SimpleList<uint16>::Iterator it_uint16;
                for (it_uint16 = lid.group->begin(); it_uint16 != lid.group->end(); ++it_uint16) {
                    if (*it_uint16 == tag)
                        return true;;
                    }
                return false;
                }
            else
                return true;
            }
        }
        return false;
}

uint16 SwitchCtrl_Global::getSlotType(uint16 slot_num)
{
    SimpleList<slot_entry>::Iterator it = slotList.begin();
    for (; it != slotList.end(); ++it) {
        if ((*it).slot_num == slot_num)
            return (*it).slot_type;
    }
    return SLOT_TYPE_ILLEGAL;
}


//End of file : SwitchCtrl_Global.cc
