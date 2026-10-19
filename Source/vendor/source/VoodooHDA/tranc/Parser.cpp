#include "License.h"

#include "VoodooHDADevice.h"
#include "VoodooHDAEngine.h"
#include "Private.h"
#include "Tables.h"
#include "Models.h"
#include "Common.h"
#include "Verbs.h"

#ifdef TIGER
#include "TigerAdditionals.h"
#endif

const char * const gColorTypes[16] = { "Unknown", "Black", "Grey", "Blue", "Green", "Red",
	"Orange", "Yellow", "Purple", "Pink", "Res.A", "Res.B", "Res.C", "Res.D",
	"White", "Other" };

extern __attribute__((visibility("hidden")))
const char * const gDeviceTypes[16] = { "Line-out", "Speaker", "Headphones", "CD",
	"SPDIF-out", "Digital-out", "Modem-line", "Modem-handset", "Line-in",
	"AUX", "Microphone", "Telephony", "SPDIF-in", "Digital-in", "Res.E", "Other" };

extern __attribute__((visibility("hidden")))
const char * const gConnTypes[4] = { "Jack", "None", "Fixed", "Both" };

const char * const gJacks[16] = {"Unknown", "1/8", "1/4", "ATAPI", "RCA", "Optic", "Digital", "Analog",
	"Multi", "XLR", "RJ-11", "Combo", "Res.F", "Res.G", "Res.H", "Other"};

const char *HDA_LOCS[64] = {
  "0x00", "Rear", "Front", "Left", "Right", "Top", "Bottom", "Rear-panel",
  "Drive-bay", "0x09", "0x0a", "0x0b", "0x0c", "0x0d", "0x0e", "0x0f",
  "Internal", "0x11", "0x12", "0x13", "0x14", "0x15", "0x16", "Riser",
  "0x18", "Onboard", "0x1a", "0x1b", "0x1c", "0x1d", "0x1e", "0x1f",
  "External", "Ext-Rear", "Ext-Front", "Ext-Left", "Ext-Right", "Ext-Top", "Ext-Bottom", "0x07",
  "0x28", "0x29", "0x2a", "0x2b", "0x2c", "0x2d", "0x2e", "0x2f",
  "Other", "0x31", "0x32", "0x33", "0x34", "0x35", "Other-Bott", "Lid-In",
  "Lid-Out", "0x39", "0x3a", "0x3b", "0x3c", "0x3d", "0x3e", "0x3f" };

const char *HDA_GPIO_ACTIONS[8] = {
  "keep", "set", "clear", "disable", "input", "0x05", "0x06", "0x07"};

const char *HDA_HDMI_CODING_TYPES[18] = {
  "undefined", "LPCM", "AC-3", "MPEG1", "MP3", "MPEG2", "AAC-LC", "DTS",
  "ATRAC", "DSD", "E-AC-3", "DTS-HD", "MLP", "DST", "WMAPro", "HE-AAC",
  "HE-AACv2", "MPEG-Surround"
};

const UInt32 gAFMT[] = { AFMT_STEREO | AFMT_S16_LE, 0 };
const ChannelCaps gDefaultChanCaps = { 48000, 48000, &gAFMT[0], 0, 2};

/*
 * Scan the bus for available codecs, starting with num.
 */
void VoodooHDADevice::scanCodecs()
{
	UInt16 stateStatus;

	stateStatus = readData16(HDAC_STATESTS);
	writeData16(HDAC_STATESTS, stateStatus);
	for (int i = 0; i < HDAC_CODEC_MAX; i++) {
		if (HDAC_STATESTS_SDIWAKE(stateStatus, i)) {
			/* We have found a codec. */
			Codec *codec = (Codec *) allocMem(sizeof (*codec));
			if (!codec) {
				errorMsg("error: couldn't allocate memory for codec\n");
				continue;
			}
			codec->commands = NULL;
			codec->numRespReceived = 0;
			codec->numVerbsSent = 0;
			codec->numFuncGroups = 0;
			codec->cad = i;
			mCodecs[i] = codec;
			probeCodec(codec);
		}
	}
}

__attribute__((visibility("hidden")))
const char *VoodooHDADevice::findCodecName(Codec *codec)
{
	UInt32 id;

	id = CODEC_ID(codec);

	for (int n = 0; gCodecList[n].name; n++)
		if (HDA_DEV_MATCH(gCodecList[n].id, id))
			return gCodecList[n].name;

	return ((id == 0) ? "NULL Codec" : "Unknown Codec");
}

/*
 * Probe a the given codec_id for available function groups.
 */
void VoodooHDADevice::probeCodec(Codec *codec)
{
	UInt32 vendorId, revisionId, subNode;
	int startNode, endNode;
	nid_t cad = codec->cad;

	dumpMsg("\nProbing codec #%d...\n", cad);
	vendorId = sendCommand(HDA_CMD_GET_PARAMETER(cad, 0, HDA_PARAM_VENDOR_ID), cad);
	revisionId = sendCommand(HDA_CMD_GET_PARAMETER(cad, 0, HDA_PARAM_REVISION_ID), cad);
	codec->vendorId = HDA_PARAM_VENDOR_ID_VENDOR_ID(vendorId);
	codec->deviceId = HDA_PARAM_VENDOR_ID_DEVICE_ID(vendorId);
	codec->revisionId = HDA_PARAM_REVISION_ID_REVISION_ID(revisionId);
	codec->steppingId = HDA_PARAM_REVISION_ID_STEPPING_ID(revisionId);

	if ((vendorId == HDAC_INVALID) && (revisionId == HDAC_INVALID)) {
		errorMsg("error: codec #%d is not responding, probing aborted\n", cad);
		return;
	}

	dumpMsg(" HDA Codec #%d: %s\n", cad, findCodecName(codec));
	dumpMsg(" HDA Codec ID: 0x%08lx\n", (long unsigned int)CODEC_ID(codec));
	dumpMsg("       Vendor: 0x%04x\n", codec->vendorId);
	dumpMsg("       Device: 0x%04x\n", codec->deviceId);
	dumpMsg("     Revision: 0x%02x\n", codec->revisionId);
	dumpMsg("     Stepping: 0x%02x\n", codec->steppingId);
	dumpMsg("PCI Subvendor: 0x%08lx\n", (long unsigned int)mSubDeviceId);

	subNode = sendCommand(HDA_CMD_GET_PARAMETER(cad, 0, HDA_PARAM_SUB_NODE_COUNT), cad);
	startNode = HDA_PARAM_SUB_NODE_COUNT_START(subNode);
	endNode = startNode + HDA_PARAM_SUB_NODE_COUNT_TOTAL(subNode);
	dumpMsg("\tstartNode=%d endNode=%d\n", startNode, endNode);
	int numNodes = endNode - startNode;
	if (numNodes < 0 || numNodes > 50) {
		errorMsg("wrong nodes number: start=%d end=%d\n", startNode, endNode);
		return;
	}

	codec->funcGroups = (FunctionGroup *) allocMem(sizeof (FunctionGroup) * numNodes);
	if (!codec->funcGroups) {
		errorMsg("error: couldn't allocate memory for function groups\n");
		return;
	}

	for (int i = startNode; i < endNode; i++)
		probeFunction(codec, i);

	return;
}

/*
 * Probe codec function and add it to the list.
 */
void VoodooHDADevice::probeFunction(Codec *codec, nid_t nid)
{
	FunctionGroup *funcGroup = &codec->funcGroups[codec->numFuncGroups];
	UInt32 funcGroupType;
	UInt32 res;
	nid_t cad = codec->cad;
//	AudioControl *control;

	funcGroupType = HDA_PARAM_FCT_GRP_TYPE_NODE_TYPE(sendCommand(HDA_CMD_GET_PARAMETER(cad, nid,
			HDA_PARAM_FCT_GRP_TYPE), cad));

	funcGroup->nid = nid;
	funcGroup->nodeType = funcGroupType;
	funcGroup->codec = codec;

	res = sendCommand(HDA_CMD_GET_PARAMETER(cad, nid, HDA_PARAM_SUB_NODE_COUNT), cad);

	funcGroup->numNodes = HDA_PARAM_SUB_NODE_COUNT_TOTAL(res);
	funcGroup->startNode = HDA_PARAM_SUB_NODE_COUNT_START(res);
	funcGroup->endNode = funcGroup->startNode + funcGroup->numNodes;

	dumpMsg("\tFound %s FG nid=%d startNode=%d endNode=%d total=%d\n",
			(funcGroupType == HDA_PARAM_FCT_GRP_TYPE_NODE_TYPE_AUDIO) ? "audio" :
			(funcGroupType == HDA_PARAM_FCT_GRP_TYPE_NODE_TYPE_MODEM) ? "modem" : "unknown",
			nid, funcGroup->startNode, funcGroup->endNode, funcGroup->numNodes);

	if (funcGroup->numNodes > 0 && funcGroup->numNodes < 50)
		funcGroup->widgets = (Widget *) allocMem(sizeof (*(funcGroup->widgets)) * funcGroup->numNodes);
	else {
		funcGroup->widgets = NULL;
		errorMsg("error: no nodes present in function group\n");
		return;
	}

	if (!funcGroup->widgets) { //allocMem contains own check
		errorMsg("error: couldn't allocate memory for widgets\n");
		funcGroup->endNode = funcGroup->startNode;
		funcGroup->numNodes = 0;
		return;
	}

	codec->numFuncGroups++;

	dumpMsg("\n");
	dumpMsg("Processing %s FG cad=%d nid=%d...\n",
			(funcGroup->nodeType == HDA_PARAM_FCT_GRP_TYPE_NODE_TYPE_AUDIO) ? "audio" :
			(funcGroup->nodeType == HDA_PARAM_FCT_GRP_TYPE_NODE_TYPE_MODEM) ? "modem" : "unknown",
			funcGroup->codec->cad, funcGroup->nid);
	if (funcGroup->nodeType != HDA_PARAM_FCT_GRP_TYPE_NODE_TYPE_AUDIO) {
		dumpMsg("Powering down...\n");
		sendCommand(HDA_CMD_SET_POWER_STATE(codec->cad, funcGroup->nid, HDA_CMD_POWER_STATE_D3), codec->cad);
		return;
	}

	dumpMsg("Powering up...\n");
	powerup(funcGroup);
	dumpMsg("Parsing audio FG...\n");
	audioParse(funcGroup);
	dumpMsg("Parsing vendor patch...\n");
	vendorPatchParse(funcGroup);
	funcGroup->audio.quirks |= mQuirksOn;
	funcGroup->audio.quirks &= ~mQuirksOff;
//Slice - move audioCtlParse after patch!!!
	/*
	 * AudioControls must be created before various audioDisable in order to mute
	 *   amps for all disabled widgets, connections and pin directions!
	 */
	dumpMsg("Parsing Ctls...\n");
	audioCtlParse(funcGroup);
	dumpMsg("Disabling nonaudio...\n");
	audioDisableNonAudio(funcGroup);
	dumpMsg("Disabling useless...\n");
	audioDisableUseless(funcGroup);
	dumpMsg("Patched pins configuration:\n");
	dumpPinConfigs(funcGroup);
	dumpMsg("Parsing pin associations...\n");
	audioAssociationParse(funcGroup);
	dumpMsg("Building AFG tree...\n");
	audioBuildTree(funcGroup);
	dumpMsg("Disabling unassociated widgets...\n");
	audioDisableUnassociated(funcGroup);
	dumpMsg("Disabling nonselected inputs...\n");
	audioDisableNonSelected(funcGroup);
	dumpMsg("Disabling useless...\n");
	audioDisableUseless(funcGroup);
	dumpMsg("Disabling crossassociated connections...\n");
	audioDisableCrossAssociations(funcGroup);
	dumpMsg("Disabling useless...\n");
	audioDisableUseless(funcGroup);
	dumpMsg("Binding associations to channels...\n");
	audioBindAssociation(funcGroup);
	dumpMsg("Assigning names to signal sources...\n");
	audioAssignNames(funcGroup);
//	dumpMsg("Parsing Ctls...\n");
//	audioCtlParse(funcGroup);
	dumpMsg("Assigning mixers to the tree...\n");
	audioAssignMixers(funcGroup);
	dumpMsg("Preparing pin controls...\n");
	audioPreparePinCtrl(funcGroup);
	dumpMsg("AFG commit...\n");
	audioCommit(funcGroup);
//	dumpMsg("HP switch init...\n");
//	switchInit(funcGroup);  //Slice - move below

	if ((funcGroup->audio.quirks & HDA_QUIRK_DMAPOS) && !mDmaPosMemAllocated) {
		errorMsg("XXX\nXXX dma pos quirk untested\nXXX\n");
		mDmaPosMem = allocateDmaMemory((mInStreamsSup + mOutStreamsSup + mBiStreamsSup) * 8, "dmaPosMem", 0 /* kIOMapInhibitCache */);
		if (!mDmaPosMem)
			errorMsg("error: failed to allocate DMA pos buffer (non-fatal)\n");
		else
			mDmaPosMemAllocated = true;
	}

	dumpMsg("Creating PCM devices...\n");
	createPcms(funcGroup);

	if (funcGroup->audio.quirks != 0) {
		dumpMsg("FG config/quirks:");
		for (int i = 0; gQuirkTypes[i].key; i++)
			if ((funcGroup->audio.quirks & gQuirkTypes[i].value) == gQuirkTypes[i].value)
				dumpMsg(" %s", gQuirkTypes[i].key);
		dumpMsg("\n");
	}
	//Slice - move here
	dumpMsg("HP switch init...\n");
	switchInit(funcGroup);

	dumpMsg("\n");
	dumpMsg("+-------------------+\n");
	dumpMsg("| DUMPING HDA NODES |\n");
	dumpMsg("+-------------------+\n");
	dumpNodes(funcGroup);

	dumpMsg("\n");
/*	dumpMsg("+------------------------+\n");
	dumpMsg("| DUMPING HDA AMPLIFIERS |\n");
	dumpMsg("+------------------------+\n");
	dumpMsg("\n");
	for (int i = 0; (control = audioCtlEach(funcGroup, i)); i++) {
		dumpMsg("%3d: nid %3d %s (%s) index %d", i, (control->widget) ? control->widget->nid : -1,
				(control->ndir == HDA_CTL_IN) ? "in " : "out",
				(control->dir == HDA_CTL_IN) ? "in " : "out", control->index);
		if (control->childWidget)
			dumpMsg(" cnid %3d", control->childWidget->nid);
		else
			dumpMsg("         ");
		//dumpMsg(" ossmask=0x%08lx bindMask=0x%08lx\n", (long unsigned int)control->ossmask, (long unsigned int)control->widget->bindSeqMask);
		dumpMsg("       mute: %d step: %3d size: %3d off: %3d%s\n", control->mute, control->step,
				control->size, control->offset, (control->enable == 0) ? " [DISABLED]" :
				((control->ossmask == 0) ? " [UNUSED]" : ""));
	}
*/	
	createPrefPanelMemoryBuf(funcGroup);
}

void VoodooHDADevice::powerup(FunctionGroup *funcGroup)
{
	nid_t cad = funcGroup->codec->cad;
	//logMsg("VoodooHDADevice::PowerUp\n");
	sendCommand(HDA_CMD_SET_POWER_STATE(cad, funcGroup->nid, HDA_CMD_POWER_STATE_D0), cad);
	IODelay(100);

	for (int i = funcGroup->startNode; i < funcGroup->endNode; i++)
		sendCommand(HDA_CMD_SET_POWER_STATE(cad, i, HDA_CMD_POWER_STATE_D0), cad);
	IODelay(1000);
}

void VoodooHDADevice::audioParse(FunctionGroup *funcGroup)
{
	UInt32 res;
	nid_t cad, nid;

	cad = funcGroup->codec->cad;
	nid = funcGroup->nid;

	res = sendCommand(HDA_CMD_GET_PARAMETER(cad, nid, HDA_PARAM_GPIO_COUNT), cad);
	funcGroup->audio.gpio = res;

	dumpMsg("GPIO: 0x%08lx NumGPIO=%ld NumGPO=%ld NumGPI=%ld GPIWake=%ld GPIUnsol=%ld\n",
			(long unsigned int)funcGroup->audio.gpio,
			(long int)HDA_PARAM_GPIO_COUNT_NUM_GPIO(funcGroup->audio.gpio),
			(long int)HDA_PARAM_GPIO_COUNT_NUM_GPO(funcGroup->audio.gpio),
			(long int)HDA_PARAM_GPIO_COUNT_NUM_GPI(funcGroup->audio.gpio),
			(long int)HDA_PARAM_GPIO_COUNT_GPI_WAKE(funcGroup->audio.gpio),
			(long int)HDA_PARAM_GPIO_COUNT_GPI_UNSOL(funcGroup->audio.gpio));

	res = sendCommand(HDA_CMD_GET_PARAMETER(cad, nid, HDA_PARAM_SUPP_STREAM_FORMATS), cad);
	funcGroup->audio.supStreamFormats = res;

	res = sendCommand(HDA_CMD_GET_PARAMETER(cad, nid, HDA_PARAM_SUPP_PCM_SIZE_RATE), cad);
	funcGroup->audio.supPcmSizeRates = res;

	res = sendCommand(HDA_CMD_GET_PARAMETER(cad, nid, HDA_PARAM_OUTPUT_AMP_CAP), cad);
	funcGroup->audio.outAmpCap = res;

	res = sendCommand(HDA_CMD_GET_PARAMETER(cad, nid, HDA_PARAM_INPUT_AMP_CAP), cad);
	funcGroup->audio.inAmpCap = res;

	for (int i = funcGroup->startNode; i < funcGroup->endNode; i++) {
		Widget *widget = widgetGet(funcGroup, i);
		if (!widget)
			dumpMsg("Ghost widget! nid=%d!\n", i);
		else {
			widget->funcGroup = funcGroup;
			widget->nid = i;
			widget->enable = 1;
			widget->selconn = -1;
			widget->pflags = 0;
			widget->ossdev = -1;
			widget->bindAssoc = -1;
			widget->traceDir = TRACE_DIR_NONE;
			widget->params.eapdBtl = HDAC_INVALID;
			widget->favoritDAC = 0;
			widgetParse(widget);
		}
	}
}

void VoodooHDADevice::audioCtlParse(FunctionGroup *funcGroup)
{
	AudioControl *controls;
	int max;

	/* XXX This is redundant */
	//Определяем общее число регуляторов
	max = 0;
	for (int i = funcGroup->startNode; i < funcGroup->endNode; i++) {
		Widget *widget = widgetGet(funcGroup, i);
		//Если widget отключен, то он не добавит регуляторов
		if (!widget || (widget->enable == 0))
			continue;
		//Каждый выходной усилитель - это один регулятор
		if (widget->params.outAmpCap != 0)
			max++;
		//Теперь считаем число входных усилителей
		if (widget->params.inAmpCap != 0) {
			switch (widget->type) {
			case HDA_PARAM_AUDIO_WIDGET_CAP_TYPE_AUDIO_SELECTOR:
			case HDA_PARAM_AUDIO_WIDGET_CAP_TYPE_AUDIO_MIXER:
				for (int j = 0; j < widget->nconns; j++) {
					Widget *childWidget = widgetGet(funcGroup, widget->conns[j]);
					if (!childWidget || (childWidget->enable == 0))
						continue;
					max++;
				}
				break;
			default:
				max++;
				break;
			}
		}
	}

  funcGroup->audio.numControls = max;
	if (max < 1) {
		dumpMsg("no controls in the codec\n");
		return;
	}
  
	controls = (AudioControl *) allocMem(sizeof (*controls) * max); //if we are here then max >=1
	if (!controls) {
		errorMsg("error: unable to allocate controls\n");
		funcGroup->audio.numControls = 0;
		return;
	}

	for (int i = funcGroup->startNode, cnt = 0; (cnt < max) && (i < funcGroup->endNode); i++) {
		Widget *widget;
		int outAmpCap, inAmpCap;
		if (cnt >= max) {
			dumpMsg("Ctl overflow!\n");
			break;
		}
		widget = widgetGet(funcGroup, i);
		if (!widget || (widget->enable == 0))
			continue;
		outAmpCap = widget->params.outAmpCap;
		inAmpCap = widget->params.inAmpCap;
		if (outAmpCap != 0) {
			int mute, offset, step, size;
			mute = HDA_PARAM_OUTPUT_AMP_CAP_MUTE_CAP(outAmpCap);
			step = HDA_PARAM_OUTPUT_AMP_CAP_NUMSTEPS(outAmpCap);
			size = HDA_PARAM_OUTPUT_AMP_CAP_STEPSIZE(outAmpCap);
			offset = HDA_PARAM_OUTPUT_AMP_CAP_OFFSET(outAmpCap);
			/* if (offset > step) {
				dumpMsg("BUGGY outamp: nid=%d [offset=%d > step=%d]\n", widget->nid, offset, step);
				offset = step;
			} */
			controls[cnt].enable = 1;
			controls[cnt].widget = widget;
			controls[cnt].mute = mute;
			controls[cnt].step = step;
			controls[cnt].size = size;
			controls[cnt].offset = offset;
			controls[cnt].left = offset;
			controls[cnt].right = offset;
			
			//Определяем к входной или выходной цепочке принадлежит регулятор
			if ((widget->type == HDA_PARAM_AUDIO_WIDGET_CAP_TYPE_PIN_COMPLEX) || //Если этот widget - разъем
				widget->waspin) //???
				controls[cnt].ndir = HDA_CTL_IN;
			else 
				controls[cnt].ndir = HDA_CTL_OUT;
			
			controls[cnt++].dir = HDA_CTL_OUT;
		}

		if (inAmpCap != 0) {
			int mute, offset, step, size;
			mute = HDA_PARAM_OUTPUT_AMP_CAP_MUTE_CAP(inAmpCap);
			step = HDA_PARAM_OUTPUT_AMP_CAP_NUMSTEPS(inAmpCap);
			size = HDA_PARAM_OUTPUT_AMP_CAP_STEPSIZE(inAmpCap);
			offset = HDA_PARAM_OUTPUT_AMP_CAP_OFFSET(inAmpCap);
			/* if (offset > step) {
				dumpMsg("BUGGY inamp: nid=%d [offset=%d > step=%d]\n", widget->nid, offset, step);
				offset = step;
			} */
			switch (widget->type) {
			case HDA_PARAM_AUDIO_WIDGET_CAP_TYPE_AUDIO_SELECTOR:
			case HDA_PARAM_AUDIO_WIDGET_CAP_TYPE_AUDIO_MIXER:
				for (int j = 0; j < widget->nconns; j++) {
					Widget *childWidget;
					childWidget = widgetGet(funcGroup, widget->conns[j]);
					if (!childWidget || (childWidget->enable == 0)) {
						/*
						 * Mute amps for connections from child widgets disabled by vendorPatchParse
						 *   for which no AudioControl is created.
						 */
						audioCtlAmpSetInternal(funcGroup->codec->cad, widget->nid, j, 1, 1, 0, 0, 1);
						continue;
					}
					if (cnt >= max) {
						dumpMsg("Ctl overflow!\n");
						break;
					}
					controls[cnt].enable = 1;
					controls[cnt].widget = widget;
					controls[cnt].childWidget = childWidget;
					controls[cnt].index = j;
					controls[cnt].mute = mute;
					controls[cnt].step = step;
					controls[cnt].size = size;
					controls[cnt].offset = offset;
					controls[cnt].left = offset;
					controls[cnt].right = offset;
	    			controls[cnt].ndir = HDA_CTL_IN; // xxx
					controls[cnt++].dir = HDA_CTL_IN;
				}
				break;
			default:
				if (cnt >= max) {
					dumpMsg("Ctl overflow!\n");
					break;
				}
				controls[cnt].enable = 1;
				controls[cnt].widget = widget;
				controls[cnt].mute = mute;
				controls[cnt].step = step;
				controls[cnt].size = size;
				controls[cnt].offset = offset;
				controls[cnt].left = offset;
				controls[cnt].right = offset;
				if (widget->type == HDA_PARAM_AUDIO_WIDGET_CAP_TYPE_PIN_COMPLEX)
					controls[cnt].ndir = HDA_CTL_OUT;
				else 
					controls[cnt].ndir = HDA_CTL_IN;
				controls[cnt++].dir = HDA_CTL_IN;
				break;
			}
		}
	}

	funcGroup->audio.controls = controls;
}

#if 0
/* This function surely going to make its way into upper level someday. */
static void
hdac_config_fetch(UInt32 *on, UInt32 *off)
{
	const char *res = NULL;
	int i = 0, j, k, len, inv;

	if (on)
		*on = 0;
	if (off)
		*off = 0;
	if (resource_string_value(device_get_name(sc->dev), device_get_unit(sc->dev), "config", &res) != 0)
		return;
	if (!(res && strlen(res) > 0))
		return;
	dumpMsg("HDA Config:");
	for (;;) {
		while (res[i] != '\0' && (res[i] == ',' || isspace(res[i]) != 0))
			i++;
		if (res[i] == '\0') {
			dumpMsg("\n");
			return;
		}
		j = i;
		while (res[j] != '\0' && !(res[j] == ',' || isspace(res[j]) != 0))
			j++;
		len = j - i;
		if (len > 2 && strncmp(res + i, "no", 2) == 0)
			inv = 2;
		else
			inv = 0;
		for (k = 0; len > inv && gQuirkTypes[k].key; k++) {
			if (strncmp(res + i + inv, gQuirkTypes[k].key, len - inv) != 0)
				continue;
			if (len - inv != strlen(gQuirkTypes[k].key))
				continue;
			dumpMsg(" %s%s", (inv != 0) ? "no" : "", gQuirkTypes[k].key);
			if (inv == 0 && on)
				*on |= gQuirkTypes[k].value;
			else if (inv != 0 && off)
				*off |= gQuirkTypes[k].value;
			break;
		}
		i = j;
	}
}
#endif

void VoodooHDADevice::vendorPatchParse(FunctionGroup *funcGroup)
{
	Widget *widget;
	UInt32 id;
	int N;

	id = CODEC_ID(funcGroup->codec);

	/*
	 * Quirks
	 */
	for (int i = 0; gQuirkList[i].model; i++) {
		if (!(HDA_DEV_MATCH(gQuirkList[i].model, mSubDeviceId) && HDA_DEV_MATCH(gQuirkList[i].id, id)))
			continue;
		if (gQuirkList[i].set != 0)
			funcGroup->audio.quirks |= gQuirkList[i].set;
		if (gQuirkList[i].unset != 0)
			funcGroup->audio.quirks &= ~(gQuirkList[i].unset);
	}
//Slice -- begin patching
	dumpMsg("Nodes patching. Codec = %d \n", (int)(funcGroup->codec->cad));
	dumpMsg("NumNodes = %d\n", NumNodes);
	for (int i = 0; i<NumNodes; i++){
		N = NodesToPatchArray[i].Node;
		if (!N) 
			continue;
		if(funcGroup->codec->cad != (int)NodesToPatchArray[i].cad)
			continue;
		widget = widgetGet(funcGroup, N);
		if (!widget || (widget->enable == 0))
			continue;
		if (NodesToPatchArray[i].Enable & 0x1) {
			widget->pin.config = NodesToPatchArray[i].Config;
			//Change widget name
			catPinName(widget);
		}
		if (NodesToPatchArray[i].Enable & 0x2){
			dumpMsg("Patching nid (%d) with conns = %d\n", N, (int)NodesToPatchArray[i].nConns);
			if(NodesToPatchArray[i].nConns){
				for(unsigned int connsIndex = 0; connsIndex < NodesToPatchArray[i].nConns; connsIndex++) {
					widget->conns[connsIndex] = NodesToPatchArray[i].Conns[connsIndex];	
					widget->connsenable[connsIndex] = 1; //Slice
				}
				widget->nconns = NodesToPatchArray[i].nConns;
			} else {
				widget->nconns = 0;
				widget->connsenable[0] = 0;
			}
			widget->connsenabled = widget->nconns; 
		}
		if (NodesToPatchArray[i].Enable & 0x4)
			widget->type = NodesToPatchArray[i].Type;
		if (NodesToPatchArray[i].Enable & 0x8)
			widget->pin.cap = NodesToPatchArray[i].Cap;
		if (NodesToPatchArray[i].Enable & 0x10) {
			if(NodesToPatchArray[i].bEnabledWidget == 0){
				widget->enable = 0;
			}else{
				//widget->enable = 2;
			}
		}
		if(NodesToPatchArray[i].Enable & 0x40) {
			widgetConnectionSelect(widget, NodesToPatchArray[i].nSel);
			widget->selconn = NodesToPatchArray[i].nSel;
		}
		if (NodesToPatchArray[i].Enable & 0x20) {
			widget->pin.ctrl = NodesToPatchArray[i].Control;
		}
		if (NodesToPatchArray[i].Enable & 0x80) {
			widget->favoritDAC = NodesToPatchArray[i].favoritDAC;
		}
	}
	// log after patch
	for (int i = funcGroup->startNode; i < funcGroup->endNode; i++) {
		widget = widgetGet(funcGroup, i);
		if (!widget || (widget->enable == 0)) // || !(widget->type == 4))
			continue;
		if (mDisableInputMonitor) {
			if ((funcGroup->codec->vendorId == REALTEK_VENDORID) && (i == 11)) {
				widget->enable = 0;
				dumpMsg("VHDevice NID=11 disabled for Realtek by user info.list\n");
				continue;
			} else if ((funcGroup->codec->vendorId == IDT_VENDORID) &&  (i == 27)) {
				widget->enable = 0;
				dumpMsg("VHDevice NID=27 disabled for IDT by user info.list\n");
				continue;
			} else if ((funcGroup->codec->vendorId == ANALOGDEVICES_VENDORID) &&  (i == 32)) {
				widget->enable = 0;
				dumpMsg("VHDevice NID=32 disabled for AD by user info.list\n");
				continue;
			}
			/* else if ((funcGroup->codec->vendorId == VIA_VENDORID) &&  (i == 22)) {
			 widget->enable = 0;
			 dumpMsg("VHDevice NID=22 disabled for VIA by user info.list\n");
			 continue;
			 } */
		}

		dumpMsg("VHDevice NID=%2d Config=%08lx (%-14s) Cap=%08lx Ctrl=%08lx", i, (long unsigned int)widget->pin.config,
				&widget->name[0], (long unsigned int)widget->pin.cap, (long unsigned int)widget->pin.ctrl);
		dumpMsg(" -- Conns:");
		widget->connsenabled = 0;
		for (int j = 0; j < widget->nconns; j++){
			if (widget->connsenable[j] == 0)
				continue;
			widget->connsenabled++;
			dumpMsg(" %d=%d", j, widget->conns[j]);

			if(widget->selconn == j)
				dumpMsg( "(select)");
		}
		dumpMsg("\n");
	}
//
//Slice - disable any predefined patches
#if 0	
	switch (id) {
	case HDA_CODEC_ALC883:
		/*
		 * nid: 24/25 = External (jack) or Internal (fixed) Mic.
		 *              Clear vref cap for jack connectivity.
		 */
		widget = widgetGet(funcGroup, 24);
		if (widget && (widget->enable != 0) && (widget->type == HDA_PARAM_AUDIO_WIDGET_CAP_TYPE_PIN_COMPLEX) &&
		    	((widget->pin.config & HDA_CONFIG_DEFAULTCONF_CONNECTIVITY_MASK) ==
				HDA_CONFIG_DEFAULTCONF_CONNECTIVITY_JACK))
			widget->pin.cap &= ~(HDA_PARAM_PIN_CAP_VREF_CTRL_100_MASK | HDA_PARAM_PIN_CAP_VREF_CTRL_80_MASK |
					HDA_PARAM_PIN_CAP_VREF_CTRL_50_MASK);
		widget = widgetGet(funcGroup, 25);
		if (widget && (widget->enable != 0) && (widget->type == HDA_PARAM_AUDIO_WIDGET_CAP_TYPE_PIN_COMPLEX) &&
				((widget->pin.config & HDA_CONFIG_DEFAULTCONF_CONNECTIVITY_MASK) ==
				HDA_CONFIG_DEFAULTCONF_CONNECTIVITY_JACK))
			widget->pin.cap &= ~(HDA_PARAM_PIN_CAP_VREF_CTRL_100_MASK | HDA_PARAM_PIN_CAP_VREF_CTRL_80_MASK |
					HDA_PARAM_PIN_CAP_VREF_CTRL_50_MASK);
		/*
		 * nid: 26 = Line-in, leave it alone.
		 */
		break;
	case HDA_CODEC_AD1983:
		/*
		 * This codec has several possible usages, but none
		 * fit the parser best. Help parser to choose better.
		 */
		/* Disable direct unmixed playback to get pcm volume. */
		widget = widgetGet(funcGroup, 5);
		if (widget)
			widget->connsenable[0] = 0;
		widget = widgetGet(funcGroup, 6);
		if (widget)
			widget->connsenable[0] = 0;
		widget = widgetGet(funcGroup, 11);
		if (widget)
			widget->connsenable[0] = 0;
		/* Disable mic and line selectors. */
		widget = widgetGet(funcGroup, 12);
		if (widget)
			widget->connsenable[1] = 0;
		widget = widgetGet(funcGroup, 13);
		if (widget)
			widget->connsenable[1] = 0;
		/* Disable recording from mono playback mix. */
		widget = widgetGet(funcGroup, 20);
		if (widget)
			widget->connsenable[3] = 0;
		break;
	case HDA_CODEC_AD1986A:
		/*
		 * This codec has overcomplicated input mixing. Make some cleaning there.
		 */
		/* Disable input mono mixer. Not needed and not supported. */
		widget = widgetGet(funcGroup, 43);
		if (widget)
			widget->enable = 0;
		/* Disable any with any input mixing mesh. Use separately. */
		widget = widgetGet(funcGroup, 39);
		if (widget)
			widget->enable = 0;
		widget = widgetGet(funcGroup, 40);
		if (widget)
			widget->enable = 0;
		widget = widgetGet(funcGroup, 41);
		if (widget)
			widget->enable = 0;
		widget = widgetGet(funcGroup, 42);
		if (widget)
			widget->enable = 0;
		/* Disable duplicate mixer node connector. */
		widget = widgetGet(funcGroup, 15);
		if (widget)
			widget->connsenable[3] = 0;
		/* There is only one mic preamplifier, use it effectively. */
		widget = widgetGet(funcGroup, 31);
		if (widget) {
			if ((widget->pin.config & HDA_CONFIG_DEFAULTCONF_DEVICE_MASK) ==
					HDA_CONFIG_DEFAULTCONF_DEVICE_MIC_IN) {
				widget = widgetGet(funcGroup, 16);
				if (widget)
				    widget->connsenable[2] = 0;
			} else {
				widget = widgetGet(funcGroup, 15);
				if (widget)
				    widget->connsenable[0] = 0;
			}
		}
		widget = widgetGet(funcGroup, 32);
		if (widget) {
			if ((widget->pin.config & HDA_CONFIG_DEFAULTCONF_DEVICE_MASK) ==
					HDA_CONFIG_DEFAULTCONF_DEVICE_MIC_IN) {
				widget = widgetGet(funcGroup, 16);
				if (widget)
				    widget->connsenable[0] = 0;
			} else {
				widget = widgetGet(funcGroup, 15);
				if (widget)
				    widget->connsenable[1] = 0;
			}
		}

		if (mSubDeviceId == ASUS_A8X_SUBVENDOR) {
			/*
			 * This is just plain ridiculous.. There are several A8 series that share the same
			 * pci id but works differently (EAPD).
			 */
			widget = widgetGet(funcGroup, 26);
			if (widget && (widget->type == HDA_PARAM_AUDIO_WIDGET_CAP_TYPE_PIN_COMPLEX) &&
					((widget->pin.config & HDA_CONFIG_DEFAULTCONF_CONNECTIVITY_MASK) !=
					HDA_CONFIG_DEFAULTCONF_CONNECTIVITY_NONE))
				funcGroup->audio.quirks &= ~HDA_QUIRK_EAPDINV;
		}
		break;
	case HDA_CODEC_AD1981HD:
		/*
		 * This codec has very unusual design with several points inappropriate for the present parser.
		 */
		/* Disable recording from mono playback mix. */
		widget = widgetGet(funcGroup, 21);
		if (widget)
			widget->connsenable[3] = 0;
		/* Disable rear to front mic mixer, use separately. */
		widget = widgetGet(funcGroup, 31);
		if (widget)
			widget->enable = 0;
		/* Disable playback mixer, use direct bypass. */
		widget = widgetGet(funcGroup, 14);
		if (widget)
			widget->enable = 0;
		break;
	}
  if (id == HDA_CODEC_ALC269) {
    if (subid == 0x16e31043 || subid == 0x831a1043 ||
        subid == 0x834a1043 || subid == 0x83981043 ||
         subid == 0x83ce1043) {
       /*
        * The digital mics on some Asus laptops produce
        * differential signals instead of expected stereo.
        * That results in silence if downmix it to mono.
        * To workaround, make codec to handle signal as mono.
        */
      hda_command(dev, HDA_CMD_SET_COEFF_INDEX(0, 0x20, 0x07));
      val = hda_command(dev, HDA_CMD_GET_PROCESSING_COEFF(0, 0x20));
      hda_command(dev, HDA_CMD_SET_COEFF_INDEX(0, 0x20, 0x07));
      hda_command(dev, HDA_CMD_SET_PROCESSING_COEFF(0, 0x20, val|0x80));
    }
  }
#endif	
}

void VoodooHDADevice::audioDisableNonAudio(FunctionGroup *funcGroup)
{
	/* Disable power and volume widgets. */
	for (int i = funcGroup->startNode; i < funcGroup->endNode; i++) {
		Widget *widget = widgetGet(funcGroup, i);
		if (!widget || widget->enable == 0)
			continue;
		if ((widget->type == HDA_PARAM_AUDIO_WIDGET_CAP_TYPE_POWER_WIDGET) ||
		    	(widget->type == HDA_PARAM_AUDIO_WIDGET_CAP_TYPE_VOLUME_WIDGET)) {
			widget->enable = 0;
			dumpMsg(" Disabling nid %d due to it's non-audio type.\n", widget->nid);
		}
	}
}

void VoodooHDADevice::audioDisableUseless(FunctionGroup *funcGroup)
{
	int done;

	/* Disable useless pins. */
	for (int i = funcGroup->startNode; i < funcGroup->endNode; i++) {
		Widget *widget = widgetGet(funcGroup, i);
		if (!widget || (widget->enable == 0))
			continue;
		if (widget->type == HDA_PARAM_AUDIO_WIDGET_CAP_TYPE_PIN_COMPLEX) {
			if ((widget->pin.config & HDA_CONFIG_DEFAULTCONF_CONNECTIVITY_MASK) ==
			    	HDA_CONFIG_DEFAULTCONF_CONNECTIVITY_NONE) {
				widget->enable = 0;
//				dumpMsg(" Disabling pin nid %d due to None connectivity.\n", widget->nid);
			} else if ((widget->pin.config & HDA_CONFIG_DEFAULTCONF_ASSOCIATION_MASK) == 0) {
				widget->enable = 0;
//				dumpMsg(" Disabling unassociated pin nid %d.\n", widget->nid);
			}
		}
	}
	do {
		AudioControl *control;
		done = 1;
		/* Disable and mute controls for disabled widgets. */
		for (int i = 0; (control = audioCtlEach(funcGroup, i)); i++) {
			if (control->enable == 0)
				continue;
			if (control->widget->enable == 0 || (control->childWidget && (control->childWidget->enable == 0 || !control->widget->connsenable[control->index]))) {
				control->forcemute = 1;
				control->muted = HDA_AMP_MUTE_ALL;
				control->left = 0;
				control->right = 0;
				control->enable = 0;
				if (control->widget->enable &&
					control->widget->connsenable[control->index]) {
					control->widget->connsenable[control->index] = 0;
					control->widget->connsenabled--;
				}
				done = 0;
				dumpMsg(" Disabling control %d nid %d cnid %d due to disabled widget.\n", i,
						control->widget->nid, control->childWidget ? control->childWidget->nid : -1);
			}
		}
		/* Disable useless widgets. */
		for (int i = funcGroup->startNode; i < funcGroup->endNode; i++) {
			Widget *widget;
			int found;
			widget = widgetGet(funcGroup, i);
			if (!widget || widget->enable == 0)
				continue;
//      dumpMsg(" widget %d is type=%x ossdev=%x \n", widget->nid, widget->type, widget->ossdev);
			/* Disable inputs with disabled child widgets. */
			for (int j = 0; j < widget->nconns; j++) {
				if (widget->connsenable[j]) {
					Widget *childWidget = widgetGet(funcGroup, widget->conns[j]);
					if (!childWidget || (childWidget->enable == 0)) {
						widget->connsenable[j] = 0;
						widget->connsenabled--;
						dumpMsg(" Disabling nid %d connection %d due to disabled child widget.\n", i, j);
					}
				}
			}
			if ((widget->type != HDA_PARAM_AUDIO_WIDGET_CAP_TYPE_AUDIO_SELECTOR) &&
			    	(widget->type != HDA_PARAM_AUDIO_WIDGET_CAP_TYPE_AUDIO_MIXER))
				continue;
      // Disable connections from InputMonitor to Input Mixer
 /*     if (widget->ossdev == SOUND_MIXER_IMIX) {
        dumpMsg(" widget %d is input mixer\n", i);
        for (int j = 0; j < widget->nconns; j++) {
          if (widget->connsenable[j]) {
            Widget *childWidget = widgetGet(funcGroup, widget->conns[j]);
            dumpMsg(" childWidget %d has flag 0x%lx\n", widget->conns[j], childWidget->pflags);
            if (childWidget && ((childWidget->pflags & HDA_ADC_MONITOR) != 0)) {
              widget->connsenable[j] = 0;
              widget->connsenabled--;
              dumpMsg(" Disabling nid %d connection %d due to input monitor child widget.\n", i, j);
            }
          }
        }
      }
*/      
			/* Disable mixers and selectors without inputs. */
/*			found = 0;
			for (int j = 0; j < widget->nconns; j++) {
				if (widget->connsenable[j]) {
					found = 1;
					break;
				}
			}
			if (found == 0) {
				widget->enable = 0;
				done = 0;
				dumpMsg(" Disabling nid %d due to all it's inputs disabled.\n", widget->nid);
			}*/
			if (widget->connsenabled < 1) {
				widget->enable = 0;
				done = 0;
				dumpMsg(" Disabling nid %d due to all it's inputs disabled.\n", widget->nid);
			}
			/* Disable nodes without consumers. */
		/*	if ((widget->type != HDA_PARAM_AUDIO_WIDGET_CAP_TYPE_AUDIO_SELECTOR) &&
			    	(widget->type != HDA_PARAM_AUDIO_WIDGET_CAP_TYPE_AUDIO_MIXER))
				continue;*/
			found = 0;
			for (int k = funcGroup->startNode; k < funcGroup->endNode; k++) {
				Widget *childWidget = widgetGet(funcGroup, k);
				if (!childWidget || childWidget->enable == 0)
					continue;
				for (int j = 0; j < childWidget->nconns; j++) {
					if (childWidget->connsenable[j] && childWidget->conns[j] == i) {
						found = 1;
						break;
					}
				}
			}
			if (found == 0) {
				widget->enable = 0;
				done = 0;
				dumpMsg(" Disabling nid %d due to all it's consumers disabled.\n", widget->nid);
			}
		}
	} while (done == 0);
}

void VoodooHDADevice::audioAssociationParse(FunctionGroup *funcGroup)
{
	AudioAssoc *assocs;
	int max;

	/* Count present associations */
	max = 0;
	for (int j = 1; j < 16; j++) {
		for (int i = funcGroup->startNode; i < funcGroup->endNode; i++) {
			Widget *widget = widgetGet(funcGroup, i);
			if (!widget || (widget->enable == 0))
				continue;
			if (widget->type != HDA_PARAM_AUDIO_WIDGET_CAP_TYPE_PIN_COMPLEX)
				continue;
			if (HDA_CONFIG_DEFAULTCONF_ASSOCIATION(widget->pin.config) != (UInt32) j)
				continue;
			max++;
			if (j != 15)  /* There could be many 1-pin assocs #15 */
				break;
		}
	}

	funcGroup->audio.numAssocs = max;

	if (max < 1 || max > 50) {
		errorMsg("wrong assocs number! %d\n", max);
		return;
	}

	assocs = (AudioAssoc *) allocMem(sizeof (*assocs) * max);
	if (!assocs) {
		/* Blekh! */
		errorMsg("unable to allocate assocs!\n");
		funcGroup->audio.numAssocs = 0;
		return;
	}

	for (int i = 0; i < max; i++) {
		assocs[i].hpredir = -1;
		assocs[i].chan = -1;
		assocs[i].digital = 1;
		assocs[i].dirty = 0;
	}

	/* Scan associations skipping as=0. */
	for (int j = 1, cnt = 0; j < 16 && cnt < max; j++) {  //assocs
		int first = 16;
		int hpredir = 0;
		for (int i = funcGroup->startNode; i < funcGroup->endNode; i++) { //nodes in assocs[cnt]
			Widget *widget;
			int type, dir, assoc, seq;
			widget = widgetGet(funcGroup, i);
			if (!widget || (widget->enable == 0))
				continue;
			if (widget->type != HDA_PARAM_AUDIO_WIDGET_CAP_TYPE_PIN_COMPLEX)  // only pins
				continue;
			assoc = HDA_CONFIG_DEFAULTCONF_ASSOCIATION(widget->pin.config);
			seq = HDA_CONFIG_DEFAULTCONF_SEQUENCE(widget->pin.config);
			if (assoc != j)
				continue;
			if (cnt >= max) {
				errorMsg("associations overflow"); // xxx
				goto double_break;
			}
			type = widget->pin.config & HDA_CONFIG_DEFAULTCONF_DEVICE_MASK;
			/* Get pin direction. */
			if ((type == HDA_CONFIG_DEFAULTCONF_DEVICE_LINE_OUT) ||
					(type == HDA_CONFIG_DEFAULTCONF_DEVICE_SPEAKER) ||
					(type == HDA_CONFIG_DEFAULTCONF_DEVICE_HP_OUT) ||
					(type == HDA_CONFIG_DEFAULTCONF_DEVICE_SPDIF_OUT) ||
					(type == HDA_CONFIG_DEFAULTCONF_DEVICE_DIGITAL_OTHER_OUT))
				dir = HDA_CTL_OUT;
			else
				dir = HDA_CTL_IN;

			/* If this is a first pin - create new association. */
			if (assocs[cnt].pincnt == 0) {
				assocs[cnt].enable = 1;
				assocs[cnt].index = j;
				assocs[cnt].dir = dir;
				assocs[cnt].hpredir = -1;
				assocs[cnt].defaultPin = -1;
				assocs[cnt].jackPin = -1;
				assocs[cnt].digital = 0;
			}
			if (seq < first)
				first = seq;
			/* Check association correctness. */
			if (assocs[cnt].pins[seq] != 0) {
				dumpMsg("Duplicate pin %d (%d) in association %d! Disabling association.\n",
						seq, widget->nid, j);
				assocs[cnt].enable = 0;
			}
			if (dir != assocs[cnt].dir) {
				dumpMsg("Pin %d has wrong direction for association %d! Disabling association.\n",
						widget->nid, j);
				assocs[cnt].enable = 0;
			}
			if (HDA_PARAM_AUDIO_WIDGET_CAP_DIGITAL(widget->params.widgetCap)) {
				if (HDA_PARAM_PIN_CAP_DP(widget->pin.cap))
					assocs[cnt].digital = 3;
				else if (HDA_PARAM_PIN_CAP_HDMI(widget->pin.cap))
					assocs[cnt].digital = 2;
				else
					assocs[cnt].digital = 1;
			}

			if (HDA_CONFIG_DEFAULTCONF_MISC(widget->pin.config) & 1 ) {
				assocs[cnt].defaultPin = seq;
			} else {
				assocs[cnt].jackPin = seq; //Last seq will be jack
				/* Redirection only for headphones. */
				hpredir = (type == HDA_CONFIG_DEFAULTCONF_DEVICE_HP_OUT);
			}

			assocs[cnt].pins[seq] = widget->nid;
			assocs[cnt].pincnt++;
			/* Association 15 is a multiple unassociated pins. */
			if (j == 15)
				cnt++;
		}
		if ((j != 15) && (assocs[cnt].pincnt > 0)) {
			if (hpredir && (assocs[cnt].pincnt > 1))
				assocs[cnt].hpredir = first;  //Slice - dunno if it needed
			if (assocs[cnt].defaultPin < 0) // && (assocs[cnt].pincnt > 1))
				assocs[cnt].defaultPin = first;
			cnt++;
		}
	}
double_break:

	dumpMsg("%d associations found:\n", max);
	for (int i = 0; i < max; i++) {
		dumpMsg("Association %d (%d) %s%s:\n", i, assocs[i].index, (assocs[i].dir == HDA_CTL_IN) ?
				"in" : "out", assocs[i].enable ? "" : " (disabled)");
		for (int j = 0; j < 16; j++) {
			if (assocs[i].pins[j] == 0)
				continue;
			dumpMsg(" Pin nid=%d seq=%d\n", assocs[i].pins[j], j);
		}
		dumpMsg("   Redir type=%d jack=%d def=%d\n", assocs[i].hpredir, assocs[i].jackPin, assocs[i].defaultPin);
	}

	funcGroup->audio.assocs = assocs;
}

void VoodooHDADevice::audioBuildTree(FunctionGroup *funcGroup)
{
	AudioAssoc *assocs = funcGroup->audio.assocs;
//  audioTraceAssociationExtra(funcGroup);

	/* Trace all associations in order of their numbers, */
	for (int j = 0; j < funcGroup->audio.numAssocs; j++) {
		int res;
		if (assocs[j].enable == 0)
			continue;
		dumpMsg("Tracing association %d (%d)\n", j, assocs[j].index);
		if (assocs[j].dir == HDA_CTL_OUT) {
			do {
				res = audioTraceAssociationOut(funcGroup, j, 0);
				if ((res == 0) && (assocs[j].hpredir >= 0) && (assocs[j].fakeredir == 0)) {
					/* If codec can't do analog HP redirection
					   try to make it using one more DAC. */
					assocs[j].fakeredir = 1;
					continue;
				}
				break;
			} while (true);
		} else {
			res = audioTraceAssociationIn(funcGroup, j);
		}
		if (res)
			dumpMsg("Association %d (%d) trace succeeded\n", j, assocs[j].index);
		else {
			dumpMsg("Association %d (%d) trace failed\n", j, assocs[j].index);
			assocs[j].enable = 0;
		}
	}

	/* Trace mixer and beeper pseudo associations. */
	audioTraceAssociationExtra(funcGroup);
}

void VoodooHDADevice::audioDisableUnassociated(FunctionGroup *funcGroup)
{
	AudioAssoc *assocs = funcGroup->audio.assocs;

	/* Disable unassosiated widgets. */
	for (int i = funcGroup->startNode; i < funcGroup->endNode; i++) {
		Widget *widget = widgetGet(funcGroup, i);
		if (!widget || (widget->enable == 0))
			continue;
		if (widget->bindAssoc == -1) {
			widget->enable = 0;
//				dumpMsg(" Disabling unassociated nid %d.\n", widget->nid);
		}
	}
	/* Disable input connections on input pin and output on output. */
	for (int i = funcGroup->startNode; i < funcGroup->endNode; i++) {
		Widget *widget = widgetGet(funcGroup, i);
		if (!widget || (widget->enable == 0))
			continue;
		if (widget->type != HDA_PARAM_AUDIO_WIDGET_CAP_TYPE_PIN_COMPLEX)
			continue;
		if (widget->bindAssoc < 0)
			continue;
		if (assocs[widget->bindAssoc].dir == HDA_CTL_IN) {
			AudioControl *control;
			for (int j = 0; j < widget->nconns; j++) {
				if (widget->connsenable[j] == 0)
					continue;
				widget->connsenable[j] = 0;
				widget->connsenabled--;
//				dumpMsg(" Disabling connection to input pin nid %d conn %d.\n", i, j);
			}
			control = audioCtlAmpGet(funcGroup, widget->nid, HDA_CTL_IN, -1, 1);
			if (control && control->enable) {
				control->forcemute = 1;
				control->muted = HDA_AMP_MUTE_ALL;
				control->left = 0;
				control->right = 0;
				control->enable = 0;
			}
		} else {
			AudioControl *control = audioCtlAmpGet(funcGroup, widget->nid, HDA_CTL_OUT, -1, 1);
			if (control && control->enable) {
				control->forcemute = 1;
				control->muted = HDA_AMP_MUTE_ALL;
				control->left = 0;
				control->right = 0;
				control->enable = 0;
			}
			for (int k = funcGroup->startNode; k < funcGroup->endNode; k++) {
				Widget *childWidget = widgetGet(funcGroup, k);
				if (!childWidget || (childWidget->enable == 0))
					continue;
				for (int j = 0; j < childWidget->nconns; j++) {
					if (childWidget->connsenable[j] && (childWidget->conns[j] == i)) {
						childWidget->connsenable[j] = 0;
						childWidget->connsenabled--;
//						dumpMsg(" Disabling connection from output pin nid %d conn %d cnid %d.\n", k, j, i);
						if ((childWidget->type == HDA_PARAM_AUDIO_WIDGET_CAP_TYPE_PIN_COMPLEX ||
							 childWidget->type == HDA_PARAM_AUDIO_WIDGET_CAP_TYPE_AUDIO_INPUT) &&
						    	(childWidget->nconns > 1))
							continue;
						control = audioCtlAmpGet(funcGroup, k, HDA_CTL_IN, j, 1);
						if (control && control->enable) {
							control->forcemute = 1;
							control->muted = HDA_AMP_MUTE_ALL;
							control->left = 0;
							control->right = 0;
							control->enable = 0;
						}
					}
				}
			}
		}
	}
}

void VoodooHDADevice::audioDisableNonSelected(FunctionGroup *funcGroup)
{
	AudioAssoc *assocs = funcGroup->audio.assocs;

	/* On playback path we can safely disable all unselected inputs. */
	for (int i = funcGroup->startNode; i < funcGroup->endNode; i++) {
		Widget *widget = widgetGet(funcGroup, i);
		if (!widget || (widget->enable == 0))
			continue;
		if (widget->nconns <= 1)
			continue;
		if (widget->type == HDA_PARAM_AUDIO_WIDGET_CAP_TYPE_AUDIO_MIXER)
			continue;
		if ((widget->bindAssoc < 0) || (assocs[widget->bindAssoc].dir == HDA_CTL_IN))
			continue;
		int k = 0; //widget->connsenabled;
		for (int j = 0; j < widget->nconns; j++) {
			if (widget->connsenable[j] == 0)
				continue;
			k++;
			if ((widget->selconn < 0) || (widget->selconn == j))
				continue;
			widget->connsenable[j] = 0;
			k--;
//			dumpMsg(" Disabling unselected connection nid %d conn %d.\n", i, j);
		}
		widget->connsenabled = k;
	}
}

static
bool isFirstPin(int bindSeqMask, AudioAssoc* assoc)
{
	int j, first_pin;

	if (assoc->hpredir >= 0 && (bindSeqMask & (1U << assoc->hpredir)))
		return true;
	if (!bindSeqMask)
		return false;
	first_pin = __builtin_ctz(static_cast<unsigned>(bindSeqMask));
	if (assoc->pins[first_pin] <= 0)
		return false;
	for (j = 0; j < first_pin; ++j)
		if (assoc->pins[j] > 0)
			return false;
	return true;
}

void VoodooHDADevice::audioDisableCrossAssociations(FunctionGroup *funcGroup)
{
	AudioControl *control;
	AudioAssoc *assocs = funcGroup->audio.assocs;

	/* Disable crossassociated and unwanted crosschannel connections. */
	/* ... using selectors */
	for (int i = funcGroup->startNode; i < funcGroup->endNode; i++) {
		Widget *widget = widgetGet(funcGroup, i);
		if (!widget || (widget->enable == 0))
			continue;
		if (widget->nconns <= 1)
			continue;
		if (widget->type == HDA_PARAM_AUDIO_WIDGET_CAP_TYPE_AUDIO_MIXER) {
      for (int j = 0; j < widget->nconns; j++) {
        Widget *childWidget;
        if (widget->connsenable[j] == 0)
          continue;
        childWidget = widgetGet(funcGroup, widget->conns[j]);
        if (!childWidget || (childWidget->enable == 0))
          continue;
        if (((childWidget->pflags & HDA_ADC_MONITOR) &&
			 (widget->bindAssoc >= 0) &&
             (assocs[widget->bindAssoc].dir == HDA_CTL_IN))) {
          widget->connsenable[j] = 0;
          widget->connsenabled--;
          dumpMsg(" Disabling connection input mixer nid %d conn %d to monitor %d.\n", i, j,
        					childWidget->nid);
          continue;
        }
        /*
         * In a multichannel analog output association, disable connection from input monitor
         *   to all pins other than first.
         */
		if (((childWidget->pflags & HDA_ADC_MONITOR) &&
             (widget->bindAssoc >= 0) &&
             (assocs[widget->bindAssoc].dir == HDA_CTL_OUT) &&
             (widget->bindAssoc != childWidget->bindAssoc) &&
             (!isFirstPin(widget->bindSeqMask, &assocs[widget->bindAssoc])))) {
          widget->connsenable[j] = 0;
          widget->connsenabled--;
          dumpMsg(" Disabling connection output mixer nid %d conn %d to monitor %d.\n", i, j,
                            childWidget->nid);
		}
      }
			continue;
    }
		if (widget->bindAssoc == -2)
			continue;
		for (int j = 0; j < widget->nconns; j++) {
			Widget *childWidget;
			if (widget->connsenable[j] == 0)
				continue;
			childWidget = widgetGet(funcGroup, widget->conns[j]);
			if (!childWidget || (childWidget->enable == 0))
				continue;
			if ((childWidget->bindAssoc == -2) ||
				((widget->pflags & HDA_ADC_MONITOR) &&
			     (childWidget->bindAssoc >= 0) &&
			     (assocs[childWidget->bindAssoc].dir == HDA_CTL_IN)))
				continue;
			if ((widget->bindAssoc == childWidget->bindAssoc) &&
					((widget->bindSeqMask & childWidget->bindSeqMask) != 0))
				continue;
			widget->connsenable[j] = 0;
//			dumpMsg(" Disabling crossassociated connection nid %d conn %d cnid %d.\n", i, j,
//					childWidget->nid);
		}
		int k = 0;
		for (int j = 0; j < widget->nconns; j++) {
			if (widget->connsenable[j] != 0) k++;
		}
		widget->connsenabled = k;
	}

	/* ... using controls */
	for (int i = 0; (control = audioCtlEach(funcGroup, i)); i++) {
		if ((control->enable == 0) || !control->childWidget)
			continue;
/*		if ((control->widget->bindAssoc == -2) || (control->childWidget->bindAssoc == -2))
			continue;*/
		if (control->widget->bindAssoc == -2)
			continue;
		if (control->childWidget->bindAssoc == -2 ||
		    ((control->widget->pflags & HDA_ADC_MONITOR) &&
		     control->childWidget->bindAssoc >= 0 &&
		     assocs[control->childWidget->bindAssoc].dir == HDA_CTL_IN))
			continue;
		if ((control->widget->bindAssoc != control->childWidget->bindAssoc) ||
		    	((control->widget->bindSeqMask & control->childWidget->bindSeqMask) == 0)) {
			control->forcemute = 1;
			control->muted = HDA_AMP_MUTE_ALL;
			control->left = 0;
			control->right = 0;
			control->enable = 0;
			if (/* control->ndir == HDA_CTL_IN && */ /* (control->childWidget != NULL) implies (control->ndir == HDA_CTL_IN), see audioCtlParse */
				control->widget->connsenable[control->index]) {
				control->widget->connsenable[control->index] = 0;
				control->widget->connsenabled--;
			}
//			dumpMsg(" Disabling crossassociated connection control %d nid %d cnid %d.\n", i,
//					control->widget->nid, control->childWidget->nid);
		}
	}

}

/*
 * Trace path from DAC to pin.
 */
nid_t VoodooHDADevice::audioTraceDac(FunctionGroup *funcGroup, int assocNum,
									 int seq, nid_t nid,
									 int dupseq, int min, int only, int depth)
{
	Widget *widget;
	int im = -1;
	nid_t m = 0, ret;
//	nid_t pinNid;
	nid_t favoritDAC = 0;
	
	if (depth > HDA_PARSE_MAXDEPTH)
		return 0;
	
	//Получаю номер DAC к которому желательно привести данный поиск
//	pinNid = funcGroup->audio.assocs[assocNum].pins[seq];
	widget = widgetGet(funcGroup, nid);
	if(widget)
		favoritDAC = widget->favoritDAC;
	
//	widget = widgetGet(funcGroup, nid);
	if (!widget || (widget->enable == 0))
		return 0;
//	if (!only)
//		dumpMsg(" %*stracing via nid %d\n", depth + 1, "", widget->nid);
	/* Use only unused widgets */
	if ((widget->bindAssoc >= 0) && (widget->bindAssoc != assocNum)) {
		if (!only)
			dumpMsg(" %*snid %d busy by association %d\n", depth + 1, "", widget->nid, widget->bindAssoc);
		return 0;
	}
	if (dupseq < 0) {
		if (widget->bindSeqMask != 0) {
			if (!only)
				dumpMsg(" %*snid %d busy by seqmask %x\n", depth + 1, "", widget->nid, widget->bindSeqMask);
			return 0;
		}
	} else {
		/* If this is headphones - allow duplicate first pin. */
		if ((widget->bindSeqMask != 0) && ((widget->bindSeqMask & (1 << dupseq)) == 0)) {
			dumpMsg(" %*snid %d busy by seqmask %x\n", depth + 1, "", widget->nid, widget->bindSeqMask);
			return 0;
		}
	}

	switch (widget->type) {
	case HDA_PARAM_AUDIO_WIDGET_CAP_TYPE_AUDIO_INPUT:
		/* Do not traverse input. AD1988 has digital monitor for which we are not ready. */
		break;
	case HDA_PARAM_AUDIO_WIDGET_CAP_TYPE_AUDIO_OUTPUT:
		/* If we are tracing HP take only dac of first pin. */
		if (((only == 0) || (only == widget->nid)) && (widget->nid >= min) &&
				((dupseq < 0) || (widget->nid == funcGroup->audio.assocs[assocNum].dacs[dupseq])))
			m = widget->nid;
		break;
	case HDA_PARAM_AUDIO_WIDGET_CAP_TYPE_PIN_COMPLEX:
		if (depth > 0)
			break;
		/* Fall */
	default:
		/* Find reachable DACs with smallest nid respecting constraints. */
		for (int i = 0; i < widget->nconns; i++) {
			if (widget->connsenable[i] == 0)
				continue;
			if ((widget->selconn != -1) && (widget->selconn != i))
				continue;
			if ((ret = audioTraceDac(funcGroup, assocNum, seq, widget->conns[i], dupseq, min, only,
					depth + 1)) != 0) {
//Slice - not sure for multichannel
				//Если найден желаемый DAC, то прерываем поиск
				if(favoritDAC && ret == favoritDAC) {
					m = ret;
					im = i;
					break;
				}
				//Если до сих пор не найдено ни одного DAC, или найденный DAC имеет меньший номер, то сохраняем найденный DAC				
				if ((m == 0) || (ret < m)) {
					m = ret;  
					im = i;
				}
				if (only || (dupseq >= 0))
					break;
			}
		}
		if (m && only && (((widget->nconns > 1) &&
				(widget->type != HDA_PARAM_AUDIO_WIDGET_CAP_TYPE_AUDIO_MIXER)) ||
				(widget->type == HDA_PARAM_AUDIO_WIDGET_CAP_TYPE_AUDIO_SELECTOR)))
			widget->selconn = im;
		break;
	}
	if (m && only) {
		widget->traceDir |= TRACE_DIR_OUT;
		widget->bindAssoc = assocNum;
		widget->bindSeqMask |= (1 << seq);
	}
	if (!only && m)
		dumpMsg(" %*snid %d returned %d\n", depth + 1, "", widget->nid, m);
	return m;
}

/*
 * Trace path from widget to ADC.
 */
nid_t VoodooHDADevice::audioTraceAdc(FunctionGroup *funcGroup, int assocNum, int seq, nid_t nid, int only,
		int depth)
{
	Widget *widget;
	nid_t res = 0;

	if (depth > HDA_PARSE_MAXDEPTH)
		return 0;
	widget = widgetGet(funcGroup, nid);
	if (!widget || (widget->enable == 0))
		return 0;
	dumpMsg(" %*stracing via nid %d\n", depth + 1, "", widget->nid);
	/* Use only unused widgets */
	if ((widget->bindAssoc >= 0) && (widget->bindAssoc != assocNum)) {
		dumpMsg(" %*snid %d busy by association %d\n", depth + 1, "", widget->nid, widget->bindAssoc);
		return 0;
	}

	switch (widget->type) {
	case HDA_PARAM_AUDIO_WIDGET_CAP_TYPE_AUDIO_INPUT:
		/* If we are tracing HP take only dac of first pin. */
		if (only == widget->nid)
			res = 1;
		break;
	case HDA_PARAM_AUDIO_WIDGET_CAP_TYPE_PIN_COMPLEX:
		if (depth > 0)
			break;
		/* Fall */
	default:
		/* Try to find reachable ADCs with specified nid. */
		for (int j = funcGroup->startNode; j < funcGroup->endNode; j++) {
			Widget *wc = widgetGet(funcGroup, j);
			if (!wc || (wc->enable == 0))
				continue;
			for (int i = 0; i < wc->nconns; i++) {
				if (wc->connsenable[i] == 0)
					continue;
				if (wc->conns[i] != nid)
					continue;
				if (audioTraceAdc(funcGroup, assocNum, seq, j, only, depth + 1) != 0) {
					res = 1;
					if ((((wc->nconns > 1) && (wc->type != HDA_PARAM_AUDIO_WIDGET_CAP_TYPE_AUDIO_MIXER)) ||
							(wc->type == HDA_PARAM_AUDIO_WIDGET_CAP_TYPE_AUDIO_SELECTOR)) &&
							(wc->selconn == -1))
						wc->selconn = i;
				}
			}
		}
		break;
	}
	if (res) {
		widget->traceDir |= TRACE_DIR_IN;
		widget->bindAssoc = assocNum;
		widget->bindSeqMask |= (1 << seq);
	}
	
	dumpMsg(" %*snid %d returned %d\n", depth + 1, "", widget->nid, res);
	return res;
}

/*
 * Erase trace path of the specified association.
 */
void VoodooHDADevice::audioUndoTrace(FunctionGroup *funcGroup, int assocNum, int seq)
{
	for (int i = funcGroup->startNode; i < funcGroup->endNode; i++) {
		Widget *widget = widgetGet(funcGroup, i);
		if (!widget || (widget->enable == 0))
			continue;
		if (widget->bindAssoc == assocNum) {
			if (seq >= 0) {
				widget->bindSeqMask &= ~(1 << seq);
				if (widget->bindSeqMask == 0) {
					widget->bindAssoc = -1;
					widget->selconn = -1;
					widget->traceDir = TRACE_DIR_NONE;
				}
			} else {
				widget->bindAssoc = -1;
				widget->bindSeqMask = 0;
				widget->selconn = -1;
				widget->traceDir = TRACE_DIR_NONE;
			}
		}
	}
}

/*
 * Trace association path from DAC to output
 */
int VoodooHDADevice::audioTraceAssociationOut(FunctionGroup *funcGroup, int assocNum, int seq)
{
	AudioAssoc *assocs = funcGroup->audio.assocs;
	int i, hpredir;
	nid_t min, res;

	/* Find next pin */
	for (i = seq; (i < 16) && (assocs[assocNum].pins[i] == 0); i++){
	}

	/* Check if there is no any left. If so - we succeeded. */
	if (i == 16)
		return 1;

	hpredir = ((i == assocs[assocNum].jackPin) && (assocs[assocNum].fakeredir == 0)) ? assocs[assocNum].hpredir : -1;
	min = 0;
	res = 0;
	do {
		dumpMsg(" Tracing pin %d with min nid %d", assocs[assocNum].pins[i], min);
		if (hpredir >= 0)
			dumpMsg(" and hpredir %d", hpredir);
		dumpMsg("\n");
		/* Trace this pin taking min nid into account. */
		res = audioTraceDac(funcGroup, assocNum, i, assocs[assocNum].pins[i], hpredir, min, 0, 0);
		if (res == 0) {
			/* If we failed - return to previous and redo it. */
			dumpMsg(" Unable to trace pin %d seq %d with min nid %d", assocs[assocNum].pins[i], i, min);
			if (hpredir >= 0)
				dumpMsg(" and hpredir %d", hpredir);
			dumpMsg("\n");
			return 0;
		}
		dumpMsg(" Pin %d traced to DAC %d", assocs[assocNum].pins[i], res);
		if (hpredir >= 0)
			dumpMsg(" and hpredir %d", hpredir);
		if (assocs[assocNum].fakeredir)
			dumpMsg(" with fake redirection");
		dumpMsg("\n");
		/* Trace again to mark the path */
		//Slice - here I want to set hpredir (?) to make different DACs
		//hpredir = -1;
		audioTraceDac(funcGroup, assocNum, i, assocs[assocNum].pins[i], hpredir, min, res, 0);
		assocs[assocNum].dacs[i] = res;
		/* We succeeded, so call next. */
		if (audioTraceAssociationOut(funcGroup, assocNum, i + 1))
			return 1;
		/* If next failed, we should retry with next min */
		audioUndoTrace(funcGroup, assocNum, i);
		assocs[assocNum].dacs[i] = 0;
		min = res + 1;
	} while (1);
}

/*
 * Trace association path from input to ADC
 */
int VoodooHDADevice::audioTraceAssociationIn(FunctionGroup *funcGroup, int assocNum)
{
	AudioAssoc *assocs = funcGroup->audio.assocs;
	
	for (int j = funcGroup->startNode; j < funcGroup->endNode; j++) {
		Widget *widget;
		int i;

		widget = widgetGet(funcGroup, j);
		if (!widget || (widget->enable == 0))
			continue;
		if (widget->type != HDA_PARAM_AUDIO_WIDGET_CAP_TYPE_AUDIO_INPUT)
			continue;
		if ((widget->bindAssoc >= 0) && (widget->bindAssoc != assocNum))
			continue;

		/* Find next pin */
		for (i = 0; i < 16; i++) {
			if (assocs[assocNum].pins[i] == 0)
				continue;

			dumpMsg(" Tracing pin %d to ADC %d\n", assocs[assocNum].pins[i], j);
			/* Trace this pin taking goal into account. */
			if (audioTraceAdc(funcGroup, assocNum, i, assocs[assocNum].pins[i], j, 0) == 0) {
				/* If we failed - return to previous and redo it. */
				dumpMsg(" Unable to trace pin %d to ADC %d, undo traces\n", assocs[assocNum].pins[i], j);
				audioUndoTrace(funcGroup, assocNum, -1);
				for (int k = 0; k < 16; k++)
					assocs[assocNum].dacs[k] = 0;
				break;
			}
			dumpMsg(" Pin %d traced to ADC %d\n", assocs[assocNum].pins[i], j);
			assocs[assocNum].dacs[i] = j;
		}
		if (i == 16) {
			//AutumnRain
			//audioTraceSwitchNid(funcGroup, assocNum);
			
			return 1;
		}
	}
	return 0;
}

/*
 * Trace input monitor path from mixer to output association.
 */
int VoodooHDADevice::audioTraceToOut(FunctionGroup *funcGroup, nid_t nid, int depth)
{
	AudioAssoc *assocs = funcGroup->audio.assocs;
	Widget *widget;
	nid_t res = 0;

	if (depth > HDA_PARSE_MAXDEPTH)
		return 0;
	widget = widgetGet(funcGroup, nid);
	if (!widget || (widget->enable == 0))
		return 0;
	dumpMsg(" %*stracing via nid %d\n", depth + 1, "", widget->nid);
	/* Use only unused widgets */
	if ((depth > 0) && (widget->bindAssoc != -1)) {
		if ((widget->bindAssoc < 0) || (assocs[widget->bindAssoc].dir == HDA_CTL_OUT)) {
			dumpMsg(" %*snid %d found output association %d\n", depth + 1, "", widget->nid, widget->bindAssoc);
      
			if (widget->bindAssoc >= 0)
				widget->pflags |= HDA_ADC_MONITOR;
			return 1;
		} else {
			dumpMsg(" %*snid %d busy by input association %d\n", depth + 1, "", widget->nid, widget->bindAssoc);
			return 0;
		}
	}

	switch (widget->type) {
	case HDA_PARAM_AUDIO_WIDGET_CAP_TYPE_AUDIO_INPUT:
		/* Do not traverse input. AD1988 has digital monitor
		for which we are not ready. */
		break;
	case HDA_PARAM_AUDIO_WIDGET_CAP_TYPE_PIN_COMPLEX:
		if (depth > 0)
			break;
		/* Fall */
	default:
		/* Try to find reachable ADCs with specified nid. */
		for (int j = funcGroup->startNode; j < funcGroup->endNode; j++) {
			Widget *wc = widgetGet(funcGroup, j);
			if (!wc || (wc->enable == 0))
				continue;
			for (int i = 0; i < wc->nconns; i++) {
				if (wc->connsenable[i] == 0)
					continue;
				if (wc->conns[i] != nid)
					continue;
				if (audioTraceToOut(funcGroup, j, depth + 1) != 0) {
					res = 1;
					if ((wc->type == HDA_PARAM_AUDIO_WIDGET_CAP_TYPE_AUDIO_SELECTOR) && (wc->selconn == -1))
						wc->selconn = i;
				}
			}
		}
		break;
	}
	if (res && (widget->bindAssoc == -1))
		widget->bindAssoc = -2;

	dumpMsg(" %*snid %d returned %d\n", depth + 1, "", widget->nid, res);
	return res;
}

/*
 * Trace extra associations (beeper, monitor)
 */
void VoodooHDADevice::audioTraceAssociationExtra(FunctionGroup *funcGroup)
{
	AudioAssoc *assocs = funcGroup->audio.assocs;

	/* Input monitor */
	/* Find mixer associated with input, but supplying signal
	   for output associations. Hope it will be input monitor. */
	dumpMsg("Tracing input monitor\n");
#if 0
	if (0 && mDisableInputMonitor) {
		dumpMsg(" disabled by Info.plist set\n");
	} else {
#endif
		for (int j = funcGroup->startNode; j < funcGroup->endNode; j++) {
			Widget *widget = widgetGet(funcGroup, j);
			if (!widget || (widget->enable == 0))
				continue;
			if (widget->type != HDA_PARAM_AUDIO_WIDGET_CAP_TYPE_AUDIO_MIXER)
				continue;
			if ((widget->bindAssoc < 0) || (assocs[widget->bindAssoc].dir != HDA_CTL_IN))
				continue;
			dumpMsg(" Tracing nid mix %d to out\n", j);
			if (audioTraceToOut(funcGroup, widget->nid, 0)) {
			//if(mVerbose > 0)
				dumpMsg(" nid %d is input monitor\n", widget->nid);
#if 0
				if (0 && mDisableInputMonitor) {
					audioUndoTrace(funcGroup, widget->bindAssoc, -1);
				} else {
#endif
					widget->pflags |= HDA_ADC_MONITOR;
					widget->ossdev = SOUND_MIXER_IGAIN; //SOUND_MIXER_IMIX;
#if 0
				}
#endif
			}
		}
		/* Other inputs monitor */
		/* Find input pins supplying signal for output associations.
		   Hope it will be input monitoring. */
		dumpMsg("Tracing other input monitors\n");
		for (int j = funcGroup->startNode; j < funcGroup->endNode; j++) {
			Widget *widget = widgetGet(funcGroup, j);
			if (!widget || (widget->enable == 0))
				continue;
			if (widget->type != HDA_PARAM_AUDIO_WIDGET_CAP_TYPE_PIN_COMPLEX)
				continue;
			if (widget->bindAssoc < 0 || assocs[widget->bindAssoc].dir != HDA_CTL_IN)
				continue;
			dumpMsg(" Tracing nid complex %d to out\n", j);

			if (audioTraceToOut(funcGroup, widget->nid, 0)) {
				dumpMsg( " nid %d is input monitor\n", widget->nid);
			}
		}
#if 0
	}
#endif


	/* Beeper */
	dumpMsg("Tracing beeper\n");
	for (int j = funcGroup->startNode; j < funcGroup->endNode; j++) {
		Widget *widget = widgetGet(funcGroup, j);
		if (!widget || (widget->enable == 0))
			continue;
		if (widget->type != HDA_PARAM_AUDIO_WIDGET_CAP_TYPE_BEEP_WIDGET)
			continue;
		dumpMsg(" Tracing nid %d to out\n", j);
		if (audioTraceToOut(funcGroup, widget->nid, 0))
			dumpMsg(" nid %d traced to out\n", j);
		widget->bindAssoc = -2;
	}
}

/*
 * Bind associations to PCM channels
 */
void VoodooHDADevice::audioBindAssociation(FunctionGroup *funcGroup)
{
	AudioAssoc *assocs = funcGroup->audio.assocs;
	int cnt = 0, free_channels;

	for (int j = 0; j < funcGroup->audio.numAssocs; j++) {
		if (assocs[j].enable)
			cnt++;
	}
	if (cnt < 0 || cnt > 50) {
		errorMsg("Wrong channels number %d!\n", cnt);
		return;
	}
	if (mNumChannels == 0) {
		mChannels = (Channel *) allocMem(sizeof (Channel) * cnt);
		if (!mChannels) {
			errorMsg("Channels memory allocation failed!\n");
			return;
		}
	} else {
		mChannels = (Channel *) reallocMem(mChannels, sizeof (Channel) * (mNumChannels + cnt));
		if (!mChannels) {
			mNumChannels = 0;
			errorMsg("Channels memory reallocation failed!\n");
			return;
		}
		/* Fixup relative pointers after realloc */
		for (int j = 0; j < mNumChannels; j++)
			mChannels[j].caps.formats = mChannels[j].formats;
	}
	free_channels = mNumChannels;
	mNumChannels += cnt;

	for (int j = free_channels; j < free_channels + cnt; j++) {
		mChannels[j].funcGroup = funcGroup;
		mChannels[j].assocNum = -1;
	}

	/* Assign associations in order of their numbers, */
	for (int j = 0; j < funcGroup->audio.numAssocs; j++) {
		if (assocs[j].enable == 0)
			continue;

		assocs[j].chan = free_channels;
		mChannels[free_channels].assocNum = j;
		mChannels[free_channels].direction = (assocs[j].dir == HDA_CTL_IN) ? PCMDIR_REC : PCMDIR_PLAY;
		pcmChannelSetup(&mChannels[free_channels]);
		free_channels++;
	}
}

/*
 * Assign OSS names to sound sources
 */
void VoodooHDADevice::audioAssignNames(FunctionGroup *funcGroup)
{
	AudioAssoc *assocs = funcGroup->audio.assocs;
	int used = 0;
	static const int types[7][13] = {
	    { SOUND_MIXER_LINE, SOUND_MIXER_LINE1, SOUND_MIXER_LINE2, SOUND_MIXER_LINE3, -1 },	/* line */
	    { SOUND_MIXER_MONITOR, SOUND_MIXER_MIC, -1 }, /* int mic */ 
	    { SOUND_MIXER_MIC, SOUND_MIXER_MONITOR, -1 }, /* ext mic */
	    { SOUND_MIXER_CD, -1 },	/* cd */
	    { SOUND_MIXER_SPEAKER, -1 },	/* speaker */
	    { SOUND_MIXER_DIGITAL1, SOUND_MIXER_DIGITAL2, SOUND_MIXER_DIGITAL3, -1 },	/* digital */
	    { SOUND_MIXER_LINE, SOUND_MIXER_LINE1, SOUND_MIXER_LINE2, SOUND_MIXER_LINE3,
		  SOUND_MIXER_PHONEIN, SOUND_MIXER_PHONEOUT, SOUND_MIXER_VIDEO, SOUND_MIXER_RADIO,
		  SOUND_MIXER_DIGITAL1, SOUND_MIXER_DIGITAL2, SOUND_MIXER_DIGITAL3,
		  SOUND_MIXER_MONITOR, -1 }	/* others */
	};

	/* Surely known names */
	for (int i = funcGroup->startNode; i < funcGroup->endNode; i++) {
		Widget *widget;
		int j, use = -1, type = -1;
		widget = widgetGet(funcGroup, i);
		if (!widget || (widget->enable == 0))
			continue;
		if (widget->bindAssoc == -1)
			continue;
		switch (widget->type) {
		case HDA_PARAM_AUDIO_WIDGET_CAP_TYPE_PIN_COMPLEX:
			if (assocs[widget->bindAssoc].dir == HDA_CTL_OUT)
				break;
			switch (widget->pin.config & HDA_CONFIG_DEFAULTCONF_DEVICE_MASK) {
			case HDA_CONFIG_DEFAULTCONF_DEVICE_LINE_IN:
				type = 0;
				break;
			case HDA_CONFIG_DEFAULTCONF_DEVICE_MIC_IN:
				if ((widget->pin.config & HDA_CONFIG_DEFAULTCONF_CONNECTIVITY_MASK)
						== HDA_CONFIG_DEFAULTCONF_CONNECTIVITY_JACK)
					type = 2;
				else
					//break;
					type = 1;
				break;
			case HDA_CONFIG_DEFAULTCONF_DEVICE_CD:
				type = 3;
				break;
			case HDA_CONFIG_DEFAULTCONF_DEVICE_SPEAKER:
				type = 4;
				break;
			case HDA_CONFIG_DEFAULTCONF_DEVICE_SPDIF_IN:
			case HDA_CONFIG_DEFAULTCONF_DEVICE_DIGITAL_OTHER_IN:
				type = 5;
				break;
			}
			if (type == -1)
				break;
			j = 0;
			while ((types[type][j] >= 0) && ((used & (1 << types[type][j])) != 0))
				j++;
			if (types[type][j] >= 0)
				use = types[type][j];
			break;
		case HDA_PARAM_AUDIO_WIDGET_CAP_TYPE_AUDIO_OUTPUT:
			use = SOUND_MIXER_PCM;
			break;
		case HDA_PARAM_AUDIO_WIDGET_CAP_TYPE_BEEP_WIDGET:
			use = SOUND_MIXER_SPEAKER;
			break;
		//Slice		
		case HDA_PARAM_AUDIO_WIDGET_CAP_TYPE_AUDIO_MIXER:
			if (!(widget->pflags & HDA_ADC_MONITOR)) {
				use = SOUND_MIXER_IMIX;
			} else  {
				use = SOUND_MIXER_IGAIN;
			}
			break;
	
		default:
			break;
		}
		if (use >= 0) {
			widget->ossdev = use;
			used |= (1 << use);
		}
	}
	/* Semi-known names */
	for (int i = funcGroup->startNode; i < funcGroup->endNode; i++) {
		Widget *widget;
		int type, j;
		widget = widgetGet(funcGroup, i);
		if (!widget || (widget->enable == 0))
			continue;
		if (widget->ossdev >= 0)
			continue;
		if (widget->bindAssoc == -1)
			continue;
		if (widget->type != HDA_PARAM_AUDIO_WIDGET_CAP_TYPE_PIN_COMPLEX)
			continue;
		if (assocs[widget->bindAssoc].dir == HDA_CTL_OUT)
			continue;
		type = -1;
		switch (widget->pin.config & HDA_CONFIG_DEFAULTCONF_DEVICE_MASK) {
		case HDA_CONFIG_DEFAULTCONF_DEVICE_LINE_OUT:
		case HDA_CONFIG_DEFAULTCONF_DEVICE_SPEAKER:
		case HDA_CONFIG_DEFAULTCONF_DEVICE_HP_OUT:
		case HDA_CONFIG_DEFAULTCONF_DEVICE_AUX:
			type = 0;
			break;
		case HDA_CONFIG_DEFAULTCONF_DEVICE_MIC_IN:
			type = 2;
			break;
		case HDA_CONFIG_DEFAULTCONF_DEVICE_SPDIF_OUT:
		case HDA_CONFIG_DEFAULTCONF_DEVICE_DIGITAL_OTHER_OUT:
			type = 5;
			break;
		}
		if (type == -1)
			break;
		j = 0;
		while ((types[type][j] >= 0) && ((used & (1 << types[type][j])) != 0))
			j++;
		if (types[type][j] >= 0) {
			widget->ossdev = types[type][j];
			used |= (1 << types[type][j]);
		}
	}
	/* Others */
	for (int i = funcGroup->startNode; i < funcGroup->endNode; i++) {
		Widget *widget;
		int j;
		widget = widgetGet(funcGroup, i);
		if (!widget || (widget->enable == 0))
			continue;
		if (widget->ossdev >= 0)
			continue;
		if (widget->bindAssoc == -1)
			continue;
		if (widget->type != HDA_PARAM_AUDIO_WIDGET_CAP_TYPE_PIN_COMPLEX)
			continue;
		if (assocs[widget->bindAssoc].dir == HDA_CTL_OUT)
			continue;
		j = 0;
		while ((types[6][j] >= 0) && ((used & (1 << types[6][j])) != 0))
			j++;
		if (types[6][j] >= 0) {
			widget->ossdev = types[6][j];
			used |= (1 << types[6][j]);
		}
	}
}

#define HDA_CTL_GIVE(control)	((control)->step ? 1 : 0)

/*
 * Find controls to control amplification for source.
 */
int VoodooHDADevice::audioCtlSourceAmp(FunctionGroup *funcGroup, nid_t nid, int index, int ossdev,
		int controllable, int depth, int need)
{
	Widget *widget;
	int conns = 0, rneed;
	char buf[64];

	if(depth == 0 && (nid == 35 || nid == 11 || nid == 12))
		if(mVerbose > 1) {
			dumpMsg("nid %d in audioCtlSourceAmp\n", nid);
		}
		
//	if(mVerbose > 1) dumpMsg(" %*strace source, nid %d\n", depth + 1, " ", nid);
	
	if (depth > HDA_PARSE_MAXDEPTH)
		return need;

	widget = widgetGet(funcGroup, nid);
	if (!widget || (widget->enable == 0))
		return need;

	/* Count number of active inputs. */
	if (depth > 0) {
		for (int j = 0; j < widget->nconns; j++)
			if (widget->connsenable[j])
				conns++;
		widget->connsenabled = conns;
	}

	/* If this is not a first step - use input mixer.
	   Pins have common input control so care must be taken. */
	if ((depth > 0) && controllable && ((conns == 1) ||
			(widget->type != HDA_PARAM_AUDIO_WIDGET_CAP_TYPE_PIN_COMPLEX))) {
		AudioControl *control = audioCtlAmpGet(funcGroup, widget->nid, HDA_CTL_IN, index, 1);
		if (control) {
			if(mVerbose > 1) dumpMsg(" %*sadd in ossmask %s\n", depth+1, " ", audioCtlMixerMaskToString(1 << ossdev, buf, sizeof (buf)));
			
			if (HDA_CTL_GIVE(control) & need)
				control->ossmask |= (1 << ossdev);
			else
				control->possmask |= (1 << ossdev);
			need &= ~HDA_CTL_GIVE(control);
		}
	}
	
	/* If widget has own ossdev - not traverse it.
	   It will be traversed on it's own. */
	if ((widget->ossdev >= 0) && (depth > 0))
		return need;

	/* We must not traverse pin */
	if (((widget->type == HDA_PARAM_AUDIO_WIDGET_CAP_TYPE_AUDIO_INPUT) ||
		 (widget->type == HDA_PARAM_AUDIO_WIDGET_CAP_TYPE_PIN_COMPLEX)) && (depth > 0)) {
		return need;
	}

	/* record that this widget exports such signal, */
	widget->ossmask |= (1 << ossdev);
	//Slice - for debug purpose
/*	if(ossdev == SOUND_MIXER_MONITOR) 
		widget->ossmask |= SOUND_MASK_MIC; */

	/* If signals mixed, we can't assign controls farther.
	 * Ignore this on depth zero. Caller must knows why.
	 * Ignore this for static selectors if this input selected.
	 */
	if (conns > 1)
		controllable = 0;

	if (controllable) {
		AudioControl *control = audioCtlAmpGet(funcGroup, widget->nid, HDA_CTL_OUT, -1, 1);
		if (control) {
			
			if(mVerbose > 1) dumpMsg(" %*sadd out ossmask %s\n", depth+1, " ", audioCtlMixerMaskToString(1 << ossdev, buf, sizeof (buf)));
			
			if (HDA_CTL_GIVE(control) & need)
				control->ossmask |= (1 << ossdev);
			else
				control->possmask |= (1 << ossdev);
			need &= ~HDA_CTL_GIVE(control);
		}
	}

	rneed = 0;
	for (int i = funcGroup->startNode; i < funcGroup->endNode; i++) {
		Widget *wc = widgetGet(funcGroup, i);
		if (!wc || (wc->enable == 0))
			continue;
		for (int j = 0; j < wc->nconns; j++) {
			if (wc->connsenable[j] && wc->conns[j] == nid)
				rneed |= audioCtlSourceAmp(funcGroup, wc->nid, j, ossdev, controllable, depth + 1, need);
		}
	}
	rneed &= need;
	
	return rneed;
}

/*
 * Find controls to control amplification for destination.
 */
void VoodooHDADevice::audioCtlDestAmp(FunctionGroup *funcGroup, nid_t nid, int index, int ossdev,
		int depth, int need)
{
	AudioAssoc *assocs = funcGroup->audio.assocs;
	Widget *widget;
	char buf[64];
	if(depth == 0 && (nid == 35 || nid == 11 || nid == 12))
		if(mVerbose > 1) {
			dumpMsg("nid %d is audioCtlDestAmp\n", nid);
		}
	

//	if(mVerbose > 1) dumpMsg(" %*strace dest nid %d\n", depth + 1, " ", nid);
	
	if (depth > HDA_PARSE_MAXDEPTH)
		return;

	widget = widgetGet(funcGroup, nid);
	if (!widget || (widget->enable == 0))
		return;
	
	if (depth > 0) {
		int consumers;
		AudioControl *control;

		/* If this node produce output for several consumers,
		   we can't touch it. */
		consumers = 0;
		for (int i = funcGroup->startNode; i < funcGroup->endNode; i++) {
			Widget *wc = widgetGet(funcGroup, i);
			if (!wc || (wc->enable == 0))
				continue;
			for (int j = 0; j < wc->nconns; j++) {
				if (wc->connsenable[j] && (wc->conns[j] == nid))
					consumers++;
			}
		}
		/* The only exception is if real HP redirection is configured
		   and this is a duplication point.
		   XXX: Actually exception is not completely correct.
		   XXX: Duplication point check is not perfect. */
		if (((consumers == 2) && ((widget->bindAssoc < 0) || (assocs[widget->bindAssoc].hpredir < 0) ||
				assocs[widget->bindAssoc].fakeredir || (widget->bindSeqMask & (1 << 15)) == 0)) ||
				(consumers > 2))
			return;

		/* Else use it's output mixer. */
		
		control = audioCtlAmpGet(funcGroup, widget->nid, HDA_CTL_OUT, -1, 1);
		if (control) {
			if(mVerbose > 1) dumpMsg(" %*sadd out ossmask %s\n", depth+1, " ", audioCtlMixerMaskToString(1 << ossdev, buf, sizeof (buf)));
			if (HDA_CTL_GIVE(control) & need)
				control->ossmask |= (1 << ossdev);
			else
				control->possmask |= (1 << ossdev);
			need &= ~HDA_CTL_GIVE(control);
		}
		
	}
	
	/* We must not traverse pin */
	if ((widget->type == HDA_PARAM_AUDIO_WIDGET_CAP_TYPE_PIN_COMPLEX) && (depth > 0)) {
		if(mVerbose > 1) dumpMsg(" %*strace goed to pin complex\n", depth + 1, " ");
		return;
	}
	
	for (int i = 0; i < widget->nconns; i++) {
		AudioControl *control;
		int tneed = need;
		if (widget->connsenable[i] == 0)
			continue;
		if ((index >= 0) && (i != index))
			continue;
		
		control = audioCtlAmpGet(funcGroup, widget->nid, HDA_CTL_IN, i, 1);
		if (control) {
			if(mVerbose > 1) dumpMsg(" %*sadd out ossmask %s\n", depth+1, " ", audioCtlMixerMaskToString(1 << ossdev, buf, sizeof (buf)));
			if (HDA_CTL_GIVE(control) & tneed)
				control->ossmask |= (1 << ossdev);
			else
				control->possmask |= (1 << ossdev);
			tneed &= ~HDA_CTL_GIVE(control);
		}
		audioCtlDestAmp(funcGroup, widget->conns[i], -1, ossdev, depth + 1, tneed);
	}
}

void VoodooHDADevice::audioAssignMixers(FunctionGroup *funcGroup)
{
	AudioAssoc *assocs = funcGroup->audio.assocs;
	AudioControl *control;
	Widget *childWidget;

	/* Assign mixers to the tree. */
	for (int i = funcGroup->startNode; i < funcGroup->endNode; i++) {
		Widget *widget = widgetGet(funcGroup, i);
		if (!widget || (widget->enable == 0))
			continue;
		if ((widget->type == HDA_PARAM_AUDIO_WIDGET_CAP_TYPE_AUDIO_OUTPUT) ||
				(widget->type == HDA_PARAM_AUDIO_WIDGET_CAP_TYPE_BEEP_WIDGET) ||
				((widget->type == HDA_PARAM_AUDIO_WIDGET_CAP_TYPE_PIN_COMPLEX) &&
				(assocs[widget->bindAssoc].dir == HDA_CTL_IN))) {
			if (widget->ossdev < 0)
				continue;
			audioCtlSourceAmp(funcGroup, widget->nid, -1, widget->ossdev, 1, 0, 1);
/*		} else if ((widget->pflags & HDA_ADC_MONITOR) != 0) {
			if (widget->ossdev < 0)
				continue;
*/			//if (audioCtlSourceAmp(funcGroup, widget->nid, -1, widget->ossdev, 1, 0, 1)) 
				/* If we are unable to control input monitor
				   as source - try to control it as destination. */
//				audioCtlDestAmp(funcGroup, widget->nid, widget->ossdev, 0, 1);
			
		} else if (widget->type == HDA_PARAM_AUDIO_WIDGET_CAP_TYPE_AUDIO_INPUT) {
			audioCtlDestAmp(funcGroup, widget->nid, -1, SOUND_MIXER_RECLEV, 0, 1);
		} else if ((widget->type == HDA_PARAM_AUDIO_WIDGET_CAP_TYPE_PIN_COMPLEX) &&
				(assocs[widget->bindAssoc].dir == HDA_CTL_OUT)) {
			audioCtlDestAmp(funcGroup, widget->nid, -1, SOUND_MIXER_VOLUME, 0, 1);
		} 
		if (widget->ossdev == SOUND_MIXER_IMIX) {
			if (audioCtlSourceAmp(funcGroup, widget->nid, -1,
										  widget->ossdev, 1, 0, 1)) {
				/* If we are unable to control input monitor
				 as source - try to control it as destination. */
				audioCtlDestAmp(funcGroup, widget->nid, -1,
										widget->ossdev, 0, 1);
			}
		}
		if (widget->pflags & HDA_ADC_MONITOR) {
			for (int j = 0; j < widget->nconns; j++) {
				if (!widget->connsenable[j])
				    continue;
				childWidget = widgetGet(funcGroup, widget->conns[j]);
				if (childWidget == NULL || childWidget->enable == 0)
				    continue;
				if (childWidget->bindAssoc == -1)
				    continue;
				if (childWidget->bindAssoc >= 0 &&
				    assocs[childWidget->bindAssoc].dir != HDA_CTL_IN)
					continue;
				audioCtlDestAmp(funcGroup, widget->nid, j, SOUND_MIXER_IGAIN, 0, 1);
			}
		}
	}
	/* Treat unrequired as possible. */
	for (int i = 0; (control = audioCtlEach(funcGroup, i)); i++)
		if (control->ossmask == 0)
			control->ossmask = control->possmask;
}

void VoodooHDADevice::audioPreparePinCtrl(FunctionGroup *funcGroup)
{
	AudioAssoc *assocs = funcGroup->audio.assocs;
	UInt32 pincap;

	for (int i = 0; i < funcGroup->numNodes; i++) {
		Widget *widget = &funcGroup->widgets[i];
		if (!widget)
			continue;
		if (widget->type != HDA_PARAM_AUDIO_WIDGET_CAP_TYPE_PIN_COMPLEX)
			continue;

	// Alejandro
		bool patchEnabled = false;                                  // Alejandro
		for(int n = 0; n < NumNodes; n++) {                         //
			if(widget->nid == (int) NodesToPatchArray[n].Node) {    //
				if(NodesToPatchArray[n].Enable & 0x20) {            //
					patchEnabled = true;                            //
				}                                                   //
				break;                                              //
			}                                                       //
		}                                                           //
		if(patchEnabled)                                            //
			continue;                                               //

		pincap = widget->pin.cap;

		/* Disable everything. */
		widget->pin.ctrl &= ~(HDA_CMD_SET_PIN_WIDGET_CTRL_HPHN_ENABLE |
				HDA_CMD_SET_PIN_WIDGET_CTRL_OUT_ENABLE | HDA_CMD_SET_PIN_WIDGET_CTRL_IN_ENABLE |
				HDA_CMD_SET_PIN_WIDGET_CTRL_VREF_ENABLE_MASK);

		if ((widget->enable == 0) || (widget->bindAssoc < 0) || (assocs[widget->bindAssoc].enable == 0)) {
			/* Pin is unused so left it disabled. */
			continue;
		} else if (assocs[widget->bindAssoc].dir == HDA_CTL_IN) {
			/* Input pin, configure for input. */
			if (HDA_PARAM_PIN_CAP_INPUT_CAP(pincap))
				widget->pin.ctrl |= HDA_CMD_SET_PIN_WIDGET_CTRL_IN_ENABLE;
			if ((funcGroup->audio.quirks & HDA_QUIRK_IVREF100) && HDA_PARAM_PIN_CAP_VREF_CTRL_100(pincap))
				widget->pin.ctrl |= HDA_CMD_SET_PIN_WIDGET_CTRL_VREF_ENABLE(
						HDA_CMD_PIN_WIDGET_CTRL_VREF_ENABLE_100);
			else if ((funcGroup->audio.quirks & HDA_QUIRK_IVREF80) && HDA_PARAM_PIN_CAP_VREF_CTRL_80(pincap))
				widget->pin.ctrl |= HDA_CMD_SET_PIN_WIDGET_CTRL_VREF_ENABLE(
				    	HDA_CMD_PIN_WIDGET_CTRL_VREF_ENABLE_80);
			else if ((funcGroup->audio.quirks & HDA_QUIRK_IVREF50) && HDA_PARAM_PIN_CAP_VREF_CTRL_50(pincap))
				widget->pin.ctrl |= HDA_CMD_SET_PIN_WIDGET_CTRL_VREF_ENABLE(
						HDA_CMD_PIN_WIDGET_CTRL_VREF_ENABLE_50);
		} else {
			/* Output pin, configure for output. */
			if (HDA_PARAM_PIN_CAP_OUTPUT_CAP(pincap))
				widget->pin.ctrl |= HDA_CMD_SET_PIN_WIDGET_CTRL_OUT_ENABLE;
			if (HDA_PARAM_PIN_CAP_HEADPHONE_CAP(pincap) && ((widget->pin.config &
			    	HDA_CONFIG_DEFAULTCONF_DEVICE_MASK) == HDA_CONFIG_DEFAULTCONF_DEVICE_HP_OUT))
				widget->pin.ctrl |= HDA_CMD_SET_PIN_WIDGET_CTRL_HPHN_ENABLE;
			if ((funcGroup->audio.quirks & HDA_QUIRK_OVREF100) && HDA_PARAM_PIN_CAP_VREF_CTRL_100(pincap))
				widget->pin.ctrl |= HDA_CMD_SET_PIN_WIDGET_CTRL_VREF_ENABLE(
				    	HDA_CMD_PIN_WIDGET_CTRL_VREF_ENABLE_100);
			else if ((funcGroup->audio.quirks & HDA_QUIRK_OVREF80) && HDA_PARAM_PIN_CAP_VREF_CTRL_80(pincap))
				widget->pin.ctrl |= HDA_CMD_SET_PIN_WIDGET_CTRL_VREF_ENABLE(
				    	HDA_CMD_PIN_WIDGET_CTRL_VREF_ENABLE_80);
			else if ((funcGroup->audio.quirks & HDA_QUIRK_OVREF50) && HDA_PARAM_PIN_CAP_VREF_CTRL_50(pincap))
				widget->pin.ctrl |= HDA_CMD_SET_PIN_WIDGET_CTRL_VREF_ENABLE(
				    	HDA_CMD_PIN_WIDGET_CTRL_VREF_ENABLE_50);
		}
	}
}

void VoodooHDADevice::audioCtlCommit(FunctionGroup *funcGroup)
{
	AudioControl *control;
	for (int i = 0; (control = audioCtlEach(funcGroup, i)); i++) {
		int z;
		if ((control->enable == 0) || (control->ossmask != 0)) {
			/* Mute disabled and mixer controllable controls.
			 * Last will be initialized by mixer_init().
			 * This expected to reduce click on startup. */
			audioCtlAmpSet(control, HDA_AMP_MUTE_ALL, 0, 0);
			continue;
		}
		/* Init fixed controls to 0dB amplification. */
		z = control->offset;
		if (z > control->step)
			z = control->step;
		audioCtlAmpSet(control, HDA_AMP_MUTE_NONE, z, z);
	}
}

void VoodooHDADevice::audioCommit(FunctionGroup *funcGroup)
{
	nid_t cad;
	UInt32 gdata, gmask, gdir;
	int commitgpio, numgpio;

	cad = funcGroup->codec->cad;

	if (mSubDeviceId == APPLE_INTEL_MAC)
		sendCommand(HDA_CMD_12BIT(cad, funcGroup->nid, 0x7e7, 0), cad);

	/* Commit controls. */
	audioCtlCommit(funcGroup);

	/* Commit selectors, pins and EAPD. */
	for (int i = 0; i < funcGroup->numNodes; i++) {
		Widget *widget = &funcGroup->widgets[i];
		if (!widget)
			continue;
		if (widget->selconn == -1)
			widget->selconn = 0;
		if (widget->nconns > 0)
			widgetConnectionSelect(widget, widget->selconn);
		if (widget->type == HDA_PARAM_AUDIO_WIDGET_CAP_TYPE_PIN_COMPLEX)
			sendCommand(HDA_CMD_SET_PIN_WIDGET_CTRL(cad, widget->nid, widget->pin.ctrl), cad);
		if (widget->params.eapdBtl != HDAC_INVALID) {
		    UInt32 val;
			val = widget->params.eapdBtl;
			if (funcGroup->audio.quirks & HDA_QUIRK_EAPDINV)
				val ^= HDA_CMD_SET_EAPD_BTL_ENABLE_EAPD;
			sendCommand(HDA_CMD_SET_EAPD_BTL_ENABLE(cad, widget->nid, val), cad);
		}
	}

	/* Commit GPIOs. */
	gdata = 0;
	gmask = 0;
	gdir = 0;
	commitgpio = 0;
	numgpio = HDA_PARAM_GPIO_COUNT_NUM_GPIO(funcGroup->audio.gpio);

	if (funcGroup->audio.quirks & HDA_QUIRK_GPIOFLUSH)
		commitgpio = (numgpio > 0) ? 1 : 0;
	else {
		for (int i = 0; i < numgpio && i < HDA_GPIO_MAX; i++) {
			if (!(funcGroup->audio.quirks & (1 << i)))
				continue;
			if (commitgpio == 0) {
				commitgpio = 1;
				gdata = sendCommand(HDA_CMD_GET_GPIO_DATA(cad, funcGroup->nid), cad);
				gmask = sendCommand(HDA_CMD_GET_GPIO_ENABLE_MASK(cad, funcGroup->nid), cad);
				gdir = sendCommand(HDA_CMD_GET_GPIO_DIRECTION(cad, funcGroup->nid), cad);
				dumpMsg("GPIO init: data=0x%08lx mask=0x%08lx dir=0x%08lx\n", (long unsigned int)gdata, (long unsigned int)gmask, (long unsigned int)gdir);
				gdata = 0;
				gmask = 0;
				gdir = 0;
			}
			gdata |= 1 << i;
			gmask |= 1 << i;
			gdir |= 1 << i;
		}
	}

	if (commitgpio != 0) {
		dumpMsg("GPIO commit: data=0x%08lx mask=0x%08lx dir=0x%08lx\n", (long unsigned int)gdata, (long unsigned int)gmask, (long unsigned int)gdir);
		sendCommand(HDA_CMD_SET_GPIO_ENABLE_MASK(cad, funcGroup->nid, gmask), cad);
		sendCommand(HDA_CMD_SET_GPIO_DIRECTION(cad, funcGroup->nid, gdir), cad);
		sendCommand(HDA_CMD_SET_GPIO_DATA(cad, funcGroup->nid, gdata), cad);
	}
}

/********************************************************************************************/
/********************************************************************************************/

void VoodooHDADevice::dumpCtls(PcmDevice *pcmDevice, const char *banner, UInt32 flag)
{
	FunctionGroup *funcGroup = pcmDevice->funcGroup;

	if (flag == 0) {
		flag = ~(SOUND_MASK_VOLUME | SOUND_MASK_PCM | SOUND_MASK_CD | SOUND_MASK_LINE |
				SOUND_MASK_RECLEV | SOUND_MASK_MIC | SOUND_MASK_SPEAKER | SOUND_MASK_OGAIN |
				SOUND_MASK_IGAIN | SOUND_MASK_IMIX | SOUND_MASK_MONITOR);
	}

	for (int j = 0; j < SOUND_MIXER_NRDEVICES; j++) {
		AudioControl *control;
		int printed;
		if ((flag & (1 << j)) == 0)
			continue;
		printed = 0;
		for (int i = 0; (control = audioCtlEach(funcGroup, i)); i++) {
			if ((control->enable == 0) || (control->widget->enable == 0))
				continue;
			if (!(((pcmDevice->playChanId >= 0) &&
					(control->widget->bindAssoc == mChannels[pcmDevice->playChanId].assocNum)) ||
					((pcmDevice->recChanId >= 0) &&
					(control->widget->bindAssoc == mChannels[pcmDevice->recChanId].assocNum)) ||
					((control->widget->bindAssoc == -2) && (pcmDevice->index == 0))))
				continue;
			if ((control->ossmask & (1 << j)) == 0)
				continue;

			if (printed == 0) {
				char buf[64];
				dumpMsg("\n");
				if (banner)
					dumpMsg("%s", banner);
				else
					dumpMsg("Unknown Ctl");
				dumpMsg(" (OSS: %s)\n", audioCtlMixerMaskToString(1 << j, buf, sizeof (buf)));
				dumpMsg("   |\n");
				printed = 1;
			}
			dumpMsg("   +- control %2d (nid %3d %s", i, control->widget->nid, (control->dir == HDA_CTL_IN) ?
					"in " : "out");
			//if ((control->ndir == HDA_CTL_IN) && (control->dir == HDA_CTL_IN))
			if (control->dir == HDA_CTL_IN)
				dumpMsg(" %2d): ", control->index);
			else
				dumpMsg("):    ");
			//Отображаем направление сигнала в widget
			dumpMsg("dir - ");
			switch (control->widget->traceDir) {
				case TRACE_DIR_NONE:
					dumpMsg("none ");
					break;
				case TRACE_DIR_IN:
					dumpMsg("in ");
					break;
				case TRACE_DIR_OUT:
					dumpMsg("out ");
					break;
				case TRACE_DIR_INOUT:
					dumpMsg("inOut ");
					break;
			}
			
			char buf[64];
			dumpMsg("oss: %s ", audioCtlMixerMaskToString(control->ossmask, buf, sizeof (buf)));
			
			if (control->step > 0) {
				dumpMsg("%+d/%+ddB (%d steps)%s\n", (0 - control->offset) * (control->size + 1) / 4,
						(control->step - control->offset) * (control->size + 1) / 4, control->step + 1,
						control->mute ? " + mute" : "");
			} else
				dumpMsg("%s\n", control->mute ? "mute" : "");
		}
	}
}

void VoodooHDADevice::dumpAudioFormats(UInt32 fcap, UInt32 pcmcap)
{
	UInt32 cap;

	cap = fcap;
	if (cap != 0) {
		dumpMsg("     Stream cap: 0x%08lx\n", (long unsigned int)cap);
		dumpMsg("                ");
		if (HDA_PARAM_SUPP_STREAM_FORMATS_AC3(cap))
			dumpMsg(" AC3");
		if (HDA_PARAM_SUPP_STREAM_FORMATS_FLOAT32(cap))
			dumpMsg(" FLOAT32");
		if (HDA_PARAM_SUPP_STREAM_FORMATS_PCM(cap))
			dumpMsg(" PCM");
		dumpMsg("\n");
	}
	cap = pcmcap;
	if (cap != 0) {
		dumpMsg("        PCM cap: 0x%08lx\n", (long unsigned int)cap);
		dumpMsg("                ");
		if (HDA_PARAM_SUPP_PCM_SIZE_RATE_8BIT(cap))
			dumpMsg(" 8");
		if (HDA_PARAM_SUPP_PCM_SIZE_RATE_16BIT(cap))
			dumpMsg(" 16");
		if (HDA_PARAM_SUPP_PCM_SIZE_RATE_20BIT(cap))
			dumpMsg(" 20");
		if (HDA_PARAM_SUPP_PCM_SIZE_RATE_24BIT(cap))
			dumpMsg(" 24");
		if (HDA_PARAM_SUPP_PCM_SIZE_RATE_32BIT(cap))
			dumpMsg(" 32");
		dumpMsg(" bits,");
		if (HDA_PARAM_SUPP_PCM_SIZE_RATE_8KHZ(cap))
			dumpMsg(" 8");
		if (HDA_PARAM_SUPP_PCM_SIZE_RATE_11KHZ(cap))
			dumpMsg(" 11");
		if (HDA_PARAM_SUPP_PCM_SIZE_RATE_16KHZ(cap))
			dumpMsg(" 16");
		if (HDA_PARAM_SUPP_PCM_SIZE_RATE_22KHZ(cap))
			dumpMsg(" 22");
		if (HDA_PARAM_SUPP_PCM_SIZE_RATE_32KHZ(cap))
			dumpMsg(" 32");
		if (HDA_PARAM_SUPP_PCM_SIZE_RATE_44KHZ(cap))
			dumpMsg(" 44");
		dumpMsg(" 48");
		if (HDA_PARAM_SUPP_PCM_SIZE_RATE_88KHZ(cap))
			dumpMsg(" 88");
		if (HDA_PARAM_SUPP_PCM_SIZE_RATE_96KHZ(cap))
			dumpMsg(" 96");
		if (HDA_PARAM_SUPP_PCM_SIZE_RATE_176KHZ(cap))
			dumpMsg(" 176");
		if (HDA_PARAM_SUPP_PCM_SIZE_RATE_192KHZ(cap))
			dumpMsg(" 192");
		if (HDA_PARAM_SUPP_PCM_SIZE_RATE_384KHZ(cap))
			dumpMsg(" 384");
		dumpMsg(" KHz\n");
	}
}

void VoodooHDADevice::dumpPin(Widget *widget)
{
	UInt32 pincap;

	pincap = widget->pin.cap;

	dumpMsg("        Pin cap: 0x%08lx\n", (long unsigned int)pincap);
	dumpMsg("                ");
	if (HDA_PARAM_PIN_CAP_IMP_SENSE_CAP(pincap))
		dumpMsg(" ISC");
	if (HDA_PARAM_PIN_CAP_TRIGGER_REQD(pincap))
		dumpMsg(" TRQD");
	if (HDA_PARAM_PIN_CAP_PRESENCE_DETECT_CAP(pincap))
		dumpMsg(" PDC");
	if (HDA_PARAM_PIN_CAP_HEADPHONE_CAP(pincap))
		dumpMsg(" HP");
	if (HDA_PARAM_PIN_CAP_OUTPUT_CAP(pincap))
		dumpMsg(" OUT");
	if (HDA_PARAM_PIN_CAP_INPUT_CAP(pincap))
		dumpMsg(" IN");
	if (HDA_PARAM_PIN_CAP_BALANCED_IO_PINS(pincap))
		dumpMsg(" BAL");
	if (HDA_PARAM_PIN_CAP_HDMI(pincap))
		dumpMsg(" HDMI");
	if (HDA_PARAM_PIN_CAP_VREF_CTRL(pincap)) {
		dumpMsg(" VREF[");
		if (HDA_PARAM_PIN_CAP_VREF_CTRL_50(pincap))
			dumpMsg(" 50");
		if (HDA_PARAM_PIN_CAP_VREF_CTRL_80(pincap))
			dumpMsg(" 80");
		if (HDA_PARAM_PIN_CAP_VREF_CTRL_100(pincap))
			dumpMsg(" 100");
		if (HDA_PARAM_PIN_CAP_VREF_CTRL_GROUND(pincap))
			dumpMsg(" GROUND");
		if (HDA_PARAM_PIN_CAP_VREF_CTRL_HIZ(pincap))
			dumpMsg(" HIZ");
		dumpMsg(" ]");
	}
	if (HDA_PARAM_PIN_CAP_EAPD_CAP(pincap))
		dumpMsg(" EAPD");
	if (HDA_PARAM_PIN_CAP_DP(pincap))
		dumpMsg(" DP");
	if (HDA_PARAM_PIN_CAP_HBR(pincap))
		dumpMsg(" HBR");
	dumpMsg("\n");
	dumpMsg("     Pin config: 0x%08lx\n", (long unsigned int)widget->pin.config);
	dumpMsg("    Pin control: 0x%08lx", (long unsigned int)widget->pin.ctrl);
	if (widget->pin.ctrl & HDA_CMD_SET_PIN_WIDGET_CTRL_HPHN_ENABLE)
		dumpMsg(" HP");
	if (widget->pin.ctrl & HDA_CMD_SET_PIN_WIDGET_CTRL_IN_ENABLE)
		dumpMsg(" IN");
	if (widget->pin.ctrl & HDA_CMD_SET_PIN_WIDGET_CTRL_OUT_ENABLE)
		dumpMsg(" OUT");
	if (widget->pin.ctrl & HDA_CMD_SET_PIN_WIDGET_CTRL_VREF_ENABLE_MASK)
		dumpMsg(" VREFs");
	dumpMsg("\n");
}

static
char const* location_name(unsigned loc_code, char* buf)
{
	switch (loc_code & 15U) {
		case 0U: strlcpy(buf, "N/A    ", 8); break;
		case 1U: strlcpy(buf, "Rear   ", 8); break;
		case 2U: strlcpy(buf, "Front  ", 8); break;
		case 3U: strlcpy(buf, "Left   ", 8); break;
		case 4U: strlcpy(buf, "Right  ", 8); break;
		case 5U: strlcpy(buf, "Top    ", 8); break;
		case 6U: strlcpy(buf, "Bottom ", 8); break;
		default: strlcpy(buf, "Special", 8); break;
	}
	switch ((loc_code >> 4) & 3U) {
		case 0U: strncat(buf, " External", 20); break;
		case 1U: strncat(buf, " Internal", 20); break;
		case 2U: strncat(buf, " Separate", 20); break;
		default: strncat(buf, " Other", 20); break;
	}
	return buf;
}

void VoodooHDADevice::dumpPinConfig(Widget *widget, UInt32 conf)
{
	char loc_buf[32];
	dumpMsg(" nid %2d 0x%08lx as %2ld seq %2ld %-13s %-5s %-7s %-16s %-7s misc %ld%s%s\n",
			widget->nid, (long unsigned int)conf,
			(long int)HDA_CONFIG_DEFAULTCONF_ASSOCIATION(conf),
			(long int)HDA_CONFIG_DEFAULTCONF_SEQUENCE(conf),
			gDeviceTypes[HDA_CONFIG_DEFAULTCONF_DEVICE(conf)],
			gConnTypes[HDA_CONFIG_DEFAULTCONF_CONNECTIVITY(conf)],
			gJacks[HDA_CONFIG_DEFAULTCONF_CONNECTION_TYPE(conf)],
			location_name(HDA_CONFIG_DEFAULTCONF_LOCATION(conf), &loc_buf[0]),
			gColorTypes[HDA_CONFIG_DEFAULTCONF_COLOR(conf)],
			(long int)HDA_CONFIG_DEFAULTCONF_MISC(conf) >> 1,
			(HDA_CONFIG_DEFAULTCONF_MISC(conf) & 1U) ? " NoPresenceDetect": "",
			(widget->enable == 0) ? " [DISABLED]" : "");
}

void VoodooHDADevice::dumpPinConfigs(FunctionGroup *funcGroup)
{
	for (int i = funcGroup->startNode; i < funcGroup->endNode; i++) {
		Widget *widget = widgetGet(funcGroup, i);
		if (!widget)
			continue;
		if (widget->type != HDA_PARAM_AUDIO_WIDGET_CAP_TYPE_PIN_COMPLEX)
			continue;
		dumpPinConfig(widget, widget->pin.config);
	}
}

void VoodooHDADevice::dumpAmp(UInt32 cap, const char *banner)
{
	dumpMsg("     %s amp: 0x%08lx\n", banner, (long unsigned int)cap);
	dumpMsg("                 mute=%ld step=%ld size=%ld offset=%ld\n",
			(long int)HDA_PARAM_OUTPUT_AMP_CAP_MUTE_CAP(cap),
			(long int)HDA_PARAM_OUTPUT_AMP_CAP_NUMSTEPS(cap),
			(long int)HDA_PARAM_OUTPUT_AMP_CAP_STEPSIZE(cap),
			(long int)HDA_PARAM_OUTPUT_AMP_CAP_OFFSET(cap));
}

void VoodooHDADevice::dumpNodes(FunctionGroup *funcGroup)
{
	static const char * const ossname[] = SOUND_DEVICE_NAMES;

	dumpMsg("\n");
	dumpMsg("Default Parameter\n");
	dumpMsg("-----------------\n");
	dumpAudioFormats(funcGroup->audio.supStreamFormats,
			funcGroup->audio.supPcmSizeRates);
	dumpMsg("         IN amp: 0x%08lx\n", (long unsigned int)funcGroup->audio.inAmpCap);
	dumpMsg("        OUT amp: 0x%08lx\n", (long unsigned int)funcGroup->audio.outAmpCap);
	for (int i = funcGroup->startNode; i < funcGroup->endNode; i++) {
		Widget *widget = widgetGet(funcGroup, i);
		if (!widget) {
			dumpMsg("Ghost widget nid=%d\n", i);
			continue;
		}
		dumpMsg("\n");
		dumpMsg("            nid: %d%s\n", widget->nid, (widget->enable == 0) ? " [DISABLED]" : "");
		dumpMsg("           Name: %s\n", widget->name);
		dumpMsg("     Widget cap: 0x%08lx\n", (long unsigned int)widget->params.widgetCap);
		if (widget->params.widgetCap & 0x000ffeff) {
			dumpMsg("                ");
			if (HDA_PARAM_AUDIO_WIDGET_CAP_DELAY(widget->params.widgetCap))
				dumpMsg(" %dDELAY", static_cast<int>(HDA_PARAM_AUDIO_WIDGET_CAP_DELAY(widget->params.widgetCap)));
			if (HDA_PARAM_AUDIO_WIDGET_CAP_CP(widget->params.widgetCap))
				dumpMsg(" CP");
			if (HDA_PARAM_AUDIO_WIDGET_CAP_LR_SWAP(widget->params.widgetCap))
				dumpMsg(" LRSWAP");
			if (HDA_PARAM_AUDIO_WIDGET_CAP_POWER_CTRL(widget->params.widgetCap))
				dumpMsg(" PWR");
			if (HDA_PARAM_AUDIO_WIDGET_CAP_DIGITAL(widget->params.widgetCap))
				dumpMsg(" DIGITAL");
			if (HDA_PARAM_AUDIO_WIDGET_CAP_UNSOL_CAP(widget->params.widgetCap))
				dumpMsg(" UNSOL");
			if (HDA_PARAM_AUDIO_WIDGET_CAP_PROC_WIDGET(widget->params.widgetCap))
				dumpMsg(" PROC");
			if (HDA_PARAM_AUDIO_WIDGET_CAP_STRIPE(widget->params.widgetCap))
				dumpMsg(" STRIPE(x%d)", (widget->stripecap & 4) ? 4 : ((widget->stripecap & 2) ? 2 : 1));
			if (HDA_PARAM_AUDIO_WIDGET_CAP_FORMAT_OVR(widget->params.widgetCap))
				dumpMsg(" FORMAT_OVR");
			if (HDA_PARAM_AUDIO_WIDGET_CAP_AMP_OVR(widget->params.widgetCap))
				dumpMsg(" AMP_OVR");
			if (HDA_PARAM_AUDIO_WIDGET_CAP_OUT_AMP(widget->params.widgetCap))
				dumpMsg(" OUT_AMP");
			if (HDA_PARAM_AUDIO_WIDGET_CAP_IN_AMP(widget->params.widgetCap))
				dumpMsg(" IN_AMP");
			//			if (HDA_PARAM_AUDIO_WIDGET_CAP_STEREO(widget->params.widgetCap))
			int j = HDA_PARAM_AUDIO_WIDGET_CAP_CC(widget->params.widgetCap);
			if (j == 1)
				dumpMsg(" STEREO");
			else if (j > 1)
			    dumpMsg(" %dCH", j + 1);
			dumpMsg("\n");
		}
		if (widget->bindAssoc != -1)
			dumpMsg("    Association: %d (0x%08x)\n", widget->bindAssoc, widget->bindSeqMask);
		if (widget->ossmask != 0 || widget->ossdev >= 0) {
			char buf[64];
			dumpMsg("            OSS: %s", audioCtlMixerMaskToString(widget->ossmask, buf, sizeof (buf)));
			if (widget->ossdev >= 0)
				dumpMsg(" (%s)", ossname[widget->ossdev]);
			dumpMsg("\n");
		}
		if ((widget->type == HDA_PARAM_AUDIO_WIDGET_CAP_TYPE_AUDIO_OUTPUT) ||
				(widget->type == HDA_PARAM_AUDIO_WIDGET_CAP_TYPE_AUDIO_INPUT)) {
			dumpAudioFormats(widget->params.supStreamFormats, widget->params.supPcmSizeRates);
		} else if (widget->type == HDA_PARAM_AUDIO_WIDGET_CAP_TYPE_PIN_COMPLEX)
			dumpPin(widget);
		if (widget->params.eapdBtl != HDAC_INVALID)
			dumpMsg("           EAPD: 0x%08lx\n", (long unsigned int)widget->params.eapdBtl);
		if (HDA_PARAM_AUDIO_WIDGET_CAP_OUT_AMP(widget->params.widgetCap) && (widget->params.outAmpCap != 0)) {
			dumpAmp(widget->params.outAmpCap, "Output");
			
			int left, right;
			int lmute, rmute;
			AudioControl *control = audioCtlAmpGet(funcGroup, widget->nid, HDA_CTL_OUT, -1, 1);
            if(control != 0 && control->forcemute) {
                dumpMsg("     Output val: [forceMute forceMute]\n");             
            }else{
                audioCtlAmpGetInternal(funcGroup->codec->cad, widget->nid, 0, &lmute, &rmute, &left, &right, 0);
                dumpMsg("     Output val: [0x%02X 0x%02X]\n", (lmute << 7) | left, (rmute << 7) | right);
            }
		}
		if (HDA_PARAM_AUDIO_WIDGET_CAP_IN_AMP(widget->params.widgetCap) && (widget->params.inAmpCap != 0)) {
			dumpAmp(widget->params.inAmpCap, " Input");
			int left, right;
			int lmute, rmute;
			

			int last = (widget->type == HDA_PARAM_AUDIO_WIDGET_CAP_TYPE_AUDIO_MIXER ||
						widget->type == HDA_PARAM_AUDIO_WIDGET_CAP_TYPE_AUDIO_SELECTOR) ? widget->nconns : 1;
            dumpMsg("      Input val: ");
            for (int j = 0; j < last; j++) {
                AudioControl *control = audioCtlAmpGet(funcGroup, widget->nid, HDA_CTL_IN, j, 1);
                if(control != 0 && control->forcemute) {
                    dumpMsg("[forceMute forceMute] ");             
                }else{
                    audioCtlAmpGetInternal(funcGroup->codec->cad, widget->nid, j, &lmute, &rmute, &left, &right, 1);
                    dumpMsg("[0x%02X 0x%02X] ", (lmute << 7) | left, (rmute << 7) | right);
                }
            }
            dumpMsg("\n");
		}
/*		if (HDA_PARAM_AUDIO_WIDGET_CAP_OUT_AMP(widget->params.widgetCap) && (widget->params.outAmpCap != 0))
			dumpAmp(widget->params.outAmpCap, "Output");
		if (HDA_PARAM_AUDIO_WIDGET_CAP_IN_AMP(widget->params.widgetCap) && (widget->params.inAmpCap != 0))
			dumpAmp(widget->params.inAmpCap, " Input"); */
		if (widget->nconns > 0) {
			dumpMsg("    connections: %d enabled %d\n", widget->nconns, widget->connsenabled);
			dumpMsg("          |\n");
		}
		for (int j = 0; j < widget->nconns; j++) {
			Widget *childWidget = widgetGet(funcGroup, widget->conns[j]);
			dumpMsg("          + %s<- nid=%d [%s]", (widget->connsenable[j] == 0) ? "[DISABLED] " : "",
					widget->conns[j], !childWidget ? "GHOST!" : childWidget->name);
			if (!childWidget)
				dumpMsg(" [UNKNOWN]");
			else if (childWidget->enable == 0)
				dumpMsg(" [DISABLED]");
			if ((widget->nconns > 1) && (widget->selconn == j) &&
					(widget->type != HDA_PARAM_AUDIO_WIDGET_CAP_TYPE_AUDIO_MIXER))
				dumpMsg(" (selected)");
			dumpMsg("\n");
		}
	}

}

void VoodooHDADevice::dumpDstNid(PcmDevice *pcmDevice, nid_t nid, int depth)
{
	FunctionGroup *funcGroup = pcmDevice->funcGroup;
	Widget *widget;

	if (depth > HDA_PARSE_MAXDEPTH)
		return;

	widget = widgetGet(funcGroup, nid);
	if (!widget || (widget->enable == 0))
		return;

	if (depth == 0)
		dumpMsg("%*s", 4, "");
	else
		dumpMsg("%*s  + <- ", 4 + (depth - 1) * 7, "");
	dumpMsg("nid=%d [%s]", widget->nid, widget->name);

	if (depth > 0) {
		char buf[64];
	//	if (widget->ossmask == 0) {
		if(widget->bindAssoc < 0 ) {
			dumpMsg("\n");
			return;
		}
		if (widget->ossmask != 0) {
			dumpMsg(" [src: %s]", audioCtlMixerMaskToString(widget->ossmask, buf, sizeof (buf)));
		}
		dumpMsg(" bindSeq=%08lx", (long unsigned int)widget->bindSeqMask);
		//if (widget->ossdev >= 0) {
		if (widget->ossmask != 0) {
			dumpMsg("\n");
			//return;
		}
	}
	dumpMsg("\n");
		
	for (int i = 0, printed = 0; i < widget->nconns; i++) {
		if (widget->connsenable[i] == 0)
			continue;
		Widget *childWidget = widgetGet(funcGroup, widget->conns[i]);
		if (!childWidget || (childWidget->enable == 0) || (childWidget->bindAssoc == -1))
			continue;
		if (printed == 0) {
			dumpMsg("%*s  |\n", 4 + (depth) * 7, "");
			printed = 1;
		}
		dumpDstNid(pcmDevice, widget->conns[i], depth + 1);
	}
}

void VoodooHDADevice::dumpDac(PcmDevice *pcmDevice)
{
	FunctionGroup *funcGroup = pcmDevice->funcGroup;
	AudioAssoc *assoc; // = funcGroup->audio.assocs; //Slice - not sure
	int printed = 0;
	if (pcmDevice->playChanId < 0)
		return;
	assoc = &funcGroup->audio.assocs[mChannels[pcmDevice->playChanId].assocNum]; //Slice ??
	for (int i = 0; i < 16; i++) {
		if (assoc->pins[i] <= 0)
			continue;
		Widget *widget = widgetGet(funcGroup, assoc->pins[i]);
		if (!widget || widget->enable == 0)
			continue;
		if (printed == 0) {
			printed = 1;
			dumpMsg("\n");
			dumpMsg("Playback:\n");
		}
		dumpMsg("\n");
		dumpDstNid(pcmDevice, assoc->pins[i], 0);
/*		
	for (int i = funcGroup->startNode, printed = 0; i < funcGroup->endNode; i++) {
		Widget *widget = widgetGet(funcGroup, i);
		if (!widget || widget->enable == 0)
			continue;
		if (widget->type != HDA_PARAM_AUDIO_WIDGET_CAP_TYPE_PIN_COMPLEX)
			continue;
		if (widget->bindAssoc != mChannels[pcmDevice->playChanId].assocNum)
			continue;
		if (printed == 0) {
			printed = 1;
			dumpMsg("\n");
			dumpMsg("Playback:\n");
		}
		dumpMsg("\n");
		dumpDstNid(pcmDevice, i, 0);
*/ 
	}
}

void VoodooHDADevice::dumpAdc(PcmDevice *pcmDevice)
{
	FunctionGroup *funcGroup = pcmDevice->funcGroup;

	if (pcmDevice->recChanId < 0)
		return;

	for (int i = funcGroup->startNode, printed = 0; i < funcGroup->endNode; i++) {
		Widget *widget = widgetGet(funcGroup, i);
		if (!widget || (widget->enable == 0))
			continue;
		if (widget->type != HDA_PARAM_AUDIO_WIDGET_CAP_TYPE_AUDIO_INPUT)
			continue;
		if (widget->bindAssoc != mChannels[pcmDevice->recChanId].assocNum)
			continue;
		if (printed == 0) {
			printed = 1;
			dumpMsg("\n");
			dumpMsg("Record:\n");
		}
		dumpMsg("\n");
		dumpDstNid(pcmDevice, i, 0);
	}
}

void VoodooHDADevice::dumpMix(PcmDevice *pcmDevice)
{
	FunctionGroup *funcGroup = pcmDevice->funcGroup;

	if (pcmDevice->index != 0)
		return;

	for (int i = funcGroup->startNode, printed = 0; i < funcGroup->endNode; i++) {
		Widget *widget = widgetGet(funcGroup, i);
		if (!widget || (widget->enable == 0))
			continue;
		if (widget->ossdev != SOUND_MIXER_IMIX)
			continue;
		if (printed == 0) {
			printed = 1;
			dumpMsg("\n");
			dumpMsg("Input Mix:\n");
		}
		dumpMsg("\n");
		dumpDstNid(pcmDevice, i, 0);
	}
}

void VoodooHDADevice::dumpPcmChannels(PcmDevice *pcmDevice)
{
	if (pcmDevice->playChanId >= 0) {
		int i = pcmDevice->playChanId;
		dumpMsg("\n");
		dumpMsg("Playback:\n");
		dumpMsg("\n");
		dumpAudioFormats(mChannels[i].supStreamFormats, mChannels[i].supPcmSizeRates);
		dumpMsg("            DAC:");
		for (nid_t *nids = mChannels[i].io; *nids != -1; nids++)
			dumpMsg(" %d", *nids);
		dumpMsg("\n");
	}
	if (pcmDevice->recChanId >= 0) {
		int i = pcmDevice->recChanId;
		dumpMsg("\n");
		dumpMsg("Record:\n");
		dumpMsg("\n");
		dumpAudioFormats(mChannels[i].supStreamFormats, mChannels[i].supPcmSizeRates);
		dumpMsg("            ADC:");
		for (nid_t *nids = mChannels[i].io; *nids != -1; nids++)
			dumpMsg(" %d", *nids);
		dumpMsg("\n");
	}
}

/********************************************************************************************/
/********************************************************************************************/

#define LOCK()		lock(__FUNCTION__)
#define UNLOCK()	unlock(__FUNCTION__)

void VoodooHDADevice::pinDump()
{
	LOCK();

	for (int codecNum = 0; codecNum < HDAC_CODEC_MAX; codecNum++) {
		nid_t cad;
		Codec *codec = mCodecs[codecNum];
		if (!codec)
			continue;
		cad = codec->cad;
		for (int funcGroupNum = 0; funcGroupNum < codec->numFuncGroups; funcGroupNum++) {
			UInt32 result;
			FunctionGroup *funcGroup = &codec->funcGroups[funcGroupNum];
			if (funcGroup->nodeType != HDA_PARAM_FCT_GRP_TYPE_NODE_TYPE_AUDIO)
				continue;
			dumpMsg("Dumping AFG cad=%d nid=%d pins:\n", codecNum, funcGroup->nid);
			for (int i = funcGroup->startNode; i < funcGroup->endNode; i++) {
				UInt32 pinCap;
				Widget *widget = widgetGet(funcGroup, i);
				if (!widget || (widget->type != HDA_PARAM_AUDIO_WIDGET_CAP_TYPE_PIN_COMPLEX))
					continue;
				dumpPinConfig(widget, widget->pin.config);
				pinCap = widget->pin.cap;
				dumpMsg("       Caps: %2s %3s %2s %4s %4s",
						HDA_PARAM_PIN_CAP_INPUT_CAP(pinCap) ? "IN" : "",
						HDA_PARAM_PIN_CAP_OUTPUT_CAP(pinCap) ? "OUT" : "",
						HDA_PARAM_PIN_CAP_HEADPHONE_CAP(pinCap) ? "HP" : "",
						HDA_PARAM_PIN_CAP_EAPD_CAP(pinCap) ? "EAPD" : "",
						HDA_PARAM_PIN_CAP_VREF_CTRL(pinCap) ? "VREF" : "");
				if (HDA_PARAM_PIN_CAP_IMP_SENSE_CAP(pinCap) || HDA_PARAM_PIN_CAP_PRESENCE_DETECT_CAP(pinCap)) {
					UInt32 delay, result;
					if (HDA_PARAM_PIN_CAP_TRIGGER_REQD(pinCap)) {
						delay = 0;
						sendCommand(HDA_CMD_SET_PIN_SENSE(cad, widget->nid, 0), cad);
						for (delay = 0; delay < 10000; delay++) {
							result = sendCommand(HDA_CMD_GET_PIN_SENSE(cad, widget->nid), cad);
							if ((result != 0x7fffffff) && (result != 0xffffffff))
								break;
							IODelay(10);
						}
					} else {
						delay = 0;
						result = sendCommand(HDA_CMD_GET_PIN_SENSE(cad, widget->nid), cad);
					}
					dumpMsg(" Sense: 0x%08lx", (long unsigned int)result);
					if (delay > 0)
						dumpMsg(" delay %ldus", (long int)delay * 10);
				}
				dumpMsg("\n");
			}
			dumpMsg("NumGPIO=%ld NumGPO=%ld NumGPI=%ld GPIWake=%ld GPIUnsol=%ld\n",
					(long int)HDA_PARAM_GPIO_COUNT_NUM_GPIO(funcGroup->audio.gpio),
					(long int)HDA_PARAM_GPIO_COUNT_NUM_GPO(funcGroup->audio.gpio),
					(long int)HDA_PARAM_GPIO_COUNT_NUM_GPI(funcGroup->audio.gpio),
					(long int)HDA_PARAM_GPIO_COUNT_GPI_WAKE(funcGroup->audio.gpio),
					(long int)HDA_PARAM_GPIO_COUNT_GPI_UNSOL(funcGroup->audio.gpio));
			if (HDA_PARAM_GPIO_COUNT_NUM_GPI(funcGroup->audio.gpio) > 0) {
				dumpMsg(" GPI:");
				result = sendCommand(HDA_CMD_GET_GPI_DATA(cad, funcGroup->nid), cad);
				dumpMsg(" data=0x%08lx", (long unsigned int)result);
				result = sendCommand(HDA_CMD_GET_GPI_WAKE_ENABLE_MASK(cad, funcGroup->nid), cad);
				dumpMsg(" wake=0x%08lx", (long unsigned int)result);
				result = sendCommand(HDA_CMD_GET_GPI_UNSOLICITED_ENABLE_MASK(cad, funcGroup->nid), cad);
				dumpMsg(" unsol=0x%08lx", (long unsigned int)result);
				result = sendCommand(HDA_CMD_GET_GPI_STICKY_MASK(cad, funcGroup->nid), cad);
				dumpMsg(" sticky=0x%08lx\n", (long unsigned int)result);
			}
			if (HDA_PARAM_GPIO_COUNT_NUM_GPO(funcGroup->audio.gpio) > 0) {
				dumpMsg(" GPO:");
				result = sendCommand(HDA_CMD_GET_GPO_DATA(cad, funcGroup->nid), cad);
				dumpMsg(" data=0x%08lx\n", (long unsigned int)result);
			}
			if (HDA_PARAM_GPIO_COUNT_NUM_GPIO(funcGroup->audio.gpio) > 0) {
				dumpMsg("GPIO:");
				result = sendCommand(HDA_CMD_GET_GPIO_DATA(cad, funcGroup->nid), cad);
				dumpMsg(" data=0x%08lx", (long unsigned int)result);
				result = sendCommand(HDA_CMD_GET_GPIO_ENABLE_MASK(cad, funcGroup->nid), cad);
				dumpMsg(" enable=0x%08lx", (long unsigned int)result);
				result = sendCommand(HDA_CMD_GET_GPIO_DIRECTION(cad, funcGroup->nid), cad);
				dumpMsg(" direction=0x%08lx\n", (long unsigned int)result);
				result = sendCommand(HDA_CMD_GET_GPIO_WAKE_ENABLE_MASK(cad, funcGroup->nid), cad);
				dumpMsg("      wake=0x%08lx", (long unsigned int)result);
				result = sendCommand(HDA_CMD_GET_GPIO_UNSOLICITED_ENABLE_MASK(cad, funcGroup->nid), cad);
				dumpMsg("  unsol=0x%08lx", (long unsigned int)result);
				result = sendCommand(HDA_CMD_GET_GPIO_STICKY_MASK(cad, funcGroup->nid), cad);
				dumpMsg("    sticky=0x%08lx\n", (long unsigned int)result);
			}
		}
	}

	UNLOCK();
}

/********************************************************************************************/
/********************************************************************************************/

void VoodooHDADevice::widgetConnectionParse(Widget *widget)
{
	UInt32 res;
	int max, ents, entnum;
	nid_t cad = widget->funcGroup->codec->cad;
	nid_t nid = widget->nid;
	nid_t cnid, addcnid, prevcnid;

	widget->nconns = 0;

	res = sendCommand(HDA_CMD_GET_PARAMETER(cad, nid, HDA_PARAM_CONN_LIST_LENGTH), cad);

	ents = HDA_PARAM_CONN_LIST_LENGTH_LIST_LENGTH(res);
	if (ents < 1)
		return;

	entnum = HDA_PARAM_CONN_LIST_LENGTH_LONG_FORM(res) ? 2 : 4;
	max = (sizeof (widget->conns) / sizeof (widget->conns[0])) - 1;
	prevcnid = 0;

#define CONN_RMASK(e)			(1 << ((32 / (e)) - 1))
#define CONN_NMASK(e)			(CONN_RMASK(e) - 1)
#define CONN_RESVAL(r, e, n)	((r) >> ((32 / (e)) * (n)))
#define CONN_RANGE(r, e, n)		(CONN_RESVAL(r, e, n) & CONN_RMASK(e))
#define CONN_CNID(r, e, n)		(CONN_RESVAL(r, e, n) & CONN_NMASK(e))

	for (int i = 0; i < ents; i += entnum) {
		res = sendCommand(HDA_CMD_GET_CONN_LIST_ENTRY(cad, nid, i), cad);
		for (int j = 0; j < entnum; j++) {
			cnid = CONN_CNID(res, entnum, j);
			if (cnid == 0) {
				if (widget->nconns < ents)
					dumpMsg("nid=%d WARNING: zero cnid entnum=%d j=%d index=%d entries=%d found=%d "
							"res=0x%08lx\n", nid, entnum, j, i, ents, widget->nconns, (long unsigned int)res);
				else
					goto getconns_out;
			}
			if ((cnid < widget->funcGroup->startNode) || (cnid >= widget->funcGroup->endNode))
				dumpMsg("GHOST: nid=%d j=%d entnum=%d index=%d res=0x%08lx\n", nid, j, entnum, i, (long unsigned int)res);
			if (CONN_RANGE(res, entnum, j) == 0)
				addcnid = cnid;
			else if ((prevcnid == 0) || (prevcnid >= cnid)) {
				dumpMsg("WARNING: Invalid child range nid=%d index=%d j=%d entnum=%d prevcnid=%d cnid=%d "
						"res=0x%08lx\n", nid, i, j, entnum,prevcnid, cnid, (long unsigned int)res);
				addcnid = cnid;
			} else
				addcnid = prevcnid + 1;
			while (addcnid <= cnid) {
				if (widget->nconns > max) {
					dumpMsg("Adding %d (nid=%d): Max connection reached! max=%d\n", addcnid, nid, max + 1);
					goto getconns_out;
				}
				widget->connsenable[widget->nconns] = 1;
				widget->conns[widget->nconns++] = addcnid++;
			}
			prevcnid = cnid;
		}
	}

getconns_out:
	widget->connsenabled = 0;
	for (int i = 0; i < widget->nconns; i++) {
		if (widget->connsenable[i]) {
			widget->connsenabled++;
		}
	}
	return;
}

// todo: get this out of the driver!

__attribute__((visibility("hidden")))
char *voodoo_strsep(char **stringp, const char *delim)
{
	char *s;
	const char *spanp;
	int c, sc;
	char *tok;

	if (!(s = *stringp))
		return NULL;
	for (tok = s;;) {
		c = *s++;
		spanp = delim;
		do {
			if ((sc = *spanp++) == c) {
				if (c == 0)
					s = NULL;
				else
					s[-1] = 0;
				*stringp = s;
				return tok;
			}
		} while (sc != 0);
	}
	/* NOTREACHED */
}

UInt32 VoodooHDADevice::widgetPinPatch(UInt32 config, const char *str)
{
	char buf[256];
	char *key, *rest, *bad;

#ifdef TIGER
	strncpy(buf, str, sizeof (buf));
#else	
	strlcpy(buf, str, sizeof (buf));
#endif
	rest = buf;
	while ((key = voodoo_strsep(&rest, "="))) {
		char *value;
		int ival;
		value = voodoo_strsep(&rest, " \t");
		if (!value)
			break;
		ival = static_cast<int>(strtol(value, &bad, 10));
		if (strcmp(key, "seq") == 0) {
			config &= ~HDA_CONFIG_DEFAULTCONF_SEQUENCE_MASK;
			config |= ((ival << HDA_CONFIG_DEFAULTCONF_SEQUENCE_SHIFT) & HDA_CONFIG_DEFAULTCONF_SEQUENCE_MASK);
		} else if (strcmp(key, "as") == 0) {
			config &= ~HDA_CONFIG_DEFAULTCONF_ASSOCIATION_MASK;
			config |= ((ival << HDA_CONFIG_DEFAULTCONF_ASSOCIATION_SHIFT) &
					HDA_CONFIG_DEFAULTCONF_ASSOCIATION_MASK);
		} else if (strcmp(key, "misc") == 0) {
			config &= ~HDA_CONFIG_DEFAULTCONF_MISC_MASK;
			config |= ((ival << HDA_CONFIG_DEFAULTCONF_MISC_SHIFT) & HDA_CONFIG_DEFAULTCONF_MISC_MASK);
		} else if (strcmp(key, "color") == 0) {
			config &= ~HDA_CONFIG_DEFAULTCONF_COLOR_MASK;
			if (bad[0] == 0)
				config |= ((ival << HDA_CONFIG_DEFAULTCONF_COLOR_SHIFT) & HDA_CONFIG_DEFAULTCONF_COLOR_MASK);
			for (int i = 0; i < 16; i++) {
				if (strcasecmp(gColorTypes[i], value) == 0) {
					config |= (i << HDA_CONFIG_DEFAULTCONF_COLOR_SHIFT);
					break;
				}
			}
		} else if (strcmp(key, "ctype") == 0) {
			config &= ~HDA_CONFIG_DEFAULTCONF_CONNECTION_TYPE_MASK;
			config |= ((ival << HDA_CONFIG_DEFAULTCONF_CONNECTION_TYPE_SHIFT) &
					HDA_CONFIG_DEFAULTCONF_CONNECTION_TYPE_MASK);
		} else if (strcmp(key, "device") == 0) {
			config &= ~HDA_CONFIG_DEFAULTCONF_DEVICE_MASK;
			if (bad[0] == 0) {
				config |= ((ival << HDA_CONFIG_DEFAULTCONF_DEVICE_SHIFT) & HDA_CONFIG_DEFAULTCONF_DEVICE_MASK);
				continue;
			}
			for (int i = 0; i < 16; i++) {
				if (strcasecmp(gDeviceTypes[i], value) == 0) {
					config |= (i << HDA_CONFIG_DEFAULTCONF_DEVICE_SHIFT);
					break;
				}
			}
		} else if (strcmp(key, "loc") == 0) {
			config &= ~HDA_CONFIG_DEFAULTCONF_LOCATION_MASK;
			config |= ((ival << HDA_CONFIG_DEFAULTCONF_LOCATION_SHIFT) & HDA_CONFIG_DEFAULTCONF_LOCATION_MASK);
		} else if (strcmp(key, "conn") == 0) {
			config &= ~HDA_CONFIG_DEFAULTCONF_CONNECTIVITY_MASK;
			if (bad[0] == 0) {
				config |= ((ival << HDA_CONFIG_DEFAULTCONF_CONNECTIVITY_SHIFT) &
						HDA_CONFIG_DEFAULTCONF_CONNECTIVITY_MASK);
				continue;
			}
			for (int i = 0; i < 4; i++) {
				if (strcasecmp(gConnTypes[i], value) == 0) {
					config |= (i << HDA_CONFIG_DEFAULTCONF_CONNECTIVITY_SHIFT);
					break;
				}
			}
		}
	}
	return config;
}

UInt32 VoodooHDADevice::widgetPinGetConfig(Widget *widget)
{
	UInt32 config, orig, id;
	nid_t cad, nid;
	const char *patch = NULL;

	cad = widget->funcGroup->codec->cad;
	nid = widget->nid;
	id = CODEC_ID(widget->funcGroup->codec);

	config = sendCommand(HDA_CMD_GET_CONFIGURATION_DEFAULT(cad, nid), cad);
	orig = config;

	dumpPinConfig(widget, orig);

	/* XXX: Old patches require complete review. Now they may create more problem then solve due to
	 * incorrect associations. */
#if 0	 
	if ((id == HDA_CODEC_ALC880) && (mSubDeviceId == LG_LW20_SUBVENDOR)) {
		switch (nid) {
		case 26:
			config &= ~HDA_CONFIG_DEFAULTCONF_DEVICE_MASK;
			config |= HDA_CONFIG_DEFAULTCONF_DEVICE_LINE_IN;
			break;
		case 27:
			config &= ~HDA_CONFIG_DEFAULTCONF_DEVICE_MASK;
			config |= HDA_CONFIG_DEFAULTCONF_DEVICE_HP_OUT;
			break;
		default:
			break;
		}
	} else if ((id == HDA_CODEC_ALC880) && ((mSubDeviceId == CLEVO_D900T_SUBVENDOR) ||
			(mSubDeviceId == ASUS_M5200_SUBVENDOR))) {
		/* Super broken BIOS */
		switch (nid) {
		case 24:	/* MIC1 */
			config &= ~HDA_CONFIG_DEFAULTCONF_DEVICE_MASK;
			config |= HDA_CONFIG_DEFAULTCONF_DEVICE_MIC_IN;
			break;
		case 25:	/* XXX MIC2 */
			config &= ~HDA_CONFIG_DEFAULTCONF_DEVICE_MASK;
			config |= HDA_CONFIG_DEFAULTCONF_DEVICE_MIC_IN;
			break;
		case 26:	/* LINE1 */
			config &= ~HDA_CONFIG_DEFAULTCONF_DEVICE_MASK;
			config |= HDA_CONFIG_DEFAULTCONF_DEVICE_LINE_IN;
			break;
		case 27:	/* XXX LINE2 */
			config &= ~HDA_CONFIG_DEFAULTCONF_DEVICE_MASK;
			config |= HDA_CONFIG_DEFAULTCONF_DEVICE_LINE_IN;
			break;
		case 28:	/* CD */
			config &= ~HDA_CONFIG_DEFAULTCONF_DEVICE_MASK;
			config |= HDA_CONFIG_DEFAULTCONF_DEVICE_CD;
			break;
		}
	} else if ((id == HDA_CODEC_ALC883) && ((mSubDeviceId == MSI_MS034A_SUBVENDOR) ||
			HDA_DEV_MATCH(ACER_ALL_SUBVENDOR, mSubDeviceId))) {
		switch (nid) {
		case 25:
			config &= ~(HDA_CONFIG_DEFAULTCONF_DEVICE_MASK | HDA_CONFIG_DEFAULTCONF_CONNECTIVITY_MASK);
			config |= (HDA_CONFIG_DEFAULTCONF_DEVICE_MIC_IN | HDA_CONFIG_DEFAULTCONF_CONNECTIVITY_FIXED);
			break;
		case 28:
			config &= ~(HDA_CONFIG_DEFAULTCONF_DEVICE_MASK | HDA_CONFIG_DEFAULTCONF_CONNECTIVITY_MASK);
			config |= (HDA_CONFIG_DEFAULTCONF_DEVICE_CD | HDA_CONFIG_DEFAULTCONF_CONNECTIVITY_FIXED);
			break;
		}
	} else if ((id == HDA_CODEC_CX20549) && (mSubDeviceId == HP_V3000_SUBVENDOR)) {
		switch (nid) {
		case 18:
			config &= ~HDA_CONFIG_DEFAULTCONF_CONNECTIVITY_MASK;
			config |= HDA_CONFIG_DEFAULTCONF_CONNECTIVITY_NONE;
			break;
		case 20:
			config &= ~(HDA_CONFIG_DEFAULTCONF_DEVICE_MASK | HDA_CONFIG_DEFAULTCONF_CONNECTIVITY_MASK);
			config |= (HDA_CONFIG_DEFAULTCONF_DEVICE_MIC_IN | HDA_CONFIG_DEFAULTCONF_CONNECTIVITY_FIXED);
			break;
		case 21:
			config &= ~(HDA_CONFIG_DEFAULTCONF_DEVICE_MASK | HDA_CONFIG_DEFAULTCONF_CONNECTIVITY_MASK);
			config |= (HDA_CONFIG_DEFAULTCONF_DEVICE_CD | HDA_CONFIG_DEFAULTCONF_CONNECTIVITY_FIXED);
			break;
		}
	} else if ((id == HDA_CODEC_CX20551) && (mSubDeviceId == HP_DV5000_SUBVENDOR)) {
		switch (nid) {
		case 20:
		case 21:
			config &= ~HDA_CONFIG_DEFAULTCONF_CONNECTIVITY_MASK;
			config |= HDA_CONFIG_DEFAULTCONF_CONNECTIVITY_NONE;
			break;
		}
	} else if ((id == HDA_CODEC_ALC861) && (mSubDeviceId == ASUS_W6F_SUBVENDOR)) {
		switch (nid) {
		case 11:
			config &= ~(HDA_CONFIG_DEFAULTCONF_DEVICE_MASK | HDA_CONFIG_DEFAULTCONF_CONNECTIVITY_MASK);
			config |= (HDA_CONFIG_DEFAULTCONF_DEVICE_LINE_OUT | HDA_CONFIG_DEFAULTCONF_CONNECTIVITY_FIXED);
			break;
		case 12:
		case 14:
		case 16:
		case 31:
		case 32:
			config &= ~(HDA_CONFIG_DEFAULTCONF_DEVICE_MASK | HDA_CONFIG_DEFAULTCONF_CONNECTIVITY_MASK);
			config |= (HDA_CONFIG_DEFAULTCONF_DEVICE_MIC_IN | HDA_CONFIG_DEFAULTCONF_CONNECTIVITY_FIXED);
			break;
		case 15:
			config &= ~(HDA_CONFIG_DEFAULTCONF_DEVICE_MASK | HDA_CONFIG_DEFAULTCONF_CONNECTIVITY_MASK);
			config |= (HDA_CONFIG_DEFAULTCONF_DEVICE_HP_OUT | HDA_CONFIG_DEFAULTCONF_CONNECTIVITY_JACK);
			break;
		}
	} else if ((id == HDA_CODEC_ALC861) && (mSubDeviceId == UNIWILL_9075_SUBVENDOR)) {
		switch (nid) {
		case 15:
			config &= ~(HDA_CONFIG_DEFAULTCONF_DEVICE_MASK | HDA_CONFIG_DEFAULTCONF_CONNECTIVITY_MASK);
			config |= (HDA_CONFIG_DEFAULTCONF_DEVICE_HP_OUT | HDA_CONFIG_DEFAULTCONF_CONNECTIVITY_JACK);
			break;
		}
	}

	/* New patches */
	if ((id == HDA_CODEC_AD1986A) && ((mSubDeviceId == ASUS_M2NPVMX_SUBVENDOR) ||
			(mSubDeviceId == ASUS_A8NVMCSM_SUBVENDOR) || (mSubDeviceId == ASUS_P5PL2_SUBVENDOR))) {
		switch (nid) {
		case 26: /* Headphones with redirection */
			patch = "as=1 seq=15";
			break;
		case 28: /* 5.1 out => 2.0 out + 1 input */
			patch = "device=Line-in as=8 seq=1";
			break;
		case 29:
			/* Can't use this as input, as the only available mic
			 * preamplifier is busy by front panel mic (nid 31).
			 * If you want to use this rear connector as mic input,
			 * you have to disable the front panel one. */
			patch = "as=0";
			break;
		case 31: /* Lot of inputs configured with as=15 and unusable */
			patch = "as=8 seq=3";
			break;
		case 32:
			patch = "as=8 seq=4";
			break;
		case 34:
			patch = "as=8 seq=5";
			break;
		case 36:
			patch = "as=8 seq=6";
			break;
		}
	} else if ((id == HDA_CODEC_ALC260) && (HDA_DEV_MATCH(SONY_S5_SUBVENDOR, mSubDeviceId))) {
		switch (nid) {
		case 16:
			patch = "seq=15 device=Headphones";
			break;
		}
	} else if (id == HDA_CODEC_ALC268) {
		if (mSubDeviceId == ACER_T5320_SUBVENDOR) {
			switch (nid) {
			case 20: /* Headphones Jack */
				patch = "as=1 seq=15";
				break;
			}
		}
	}
#endif
	if (patch)
		config = widgetPinPatch(config, patch);

#if 0
	char buf[32];
	const char *res = NULL, *patch = NULL;
	snprintf(buf, sizeof (buf), "cad%u.nid%u.config", cad, nid);
	if (resource_string_value(device_get_name(sc->dev), device_get_unit(sc->dev), buf, &res) == 0) {
		if (strncmp(res, "0x", 2) == 0)
			config = strtol(res + 2, NULL, 16);
		else
			config = widgetPinPatch(config, res);
	}
#endif

	if (config != orig)
		dumpMsg("Patching pin config nid=%u 0x%08lx -> 0x%08lx\n", nid, (long unsigned int)orig, (long unsigned int)config);

	return config;
}

UInt32 VoodooHDADevice::widgetPinGetCaps(Widget *widget)
{
	UInt32 caps, orig, id;
	nid_t cad, nid;

	cad = widget->funcGroup->codec->cad;
	nid = widget->nid;
	id = CODEC_ID(widget->funcGroup->codec);

	caps = sendCommand(HDA_CMD_GET_PARAMETER(cad, nid, HDA_PARAM_PIN_CAP), cad);
	orig = caps;

	if (caps != orig)
		dumpMsg("Patching pin caps nid=%u 0x%08lx -> 0x%08lx\n", nid, (long unsigned int)orig, (long unsigned int)caps);

	return caps;
}

void VoodooHDADevice::widgetPinParse(Widget *widget)
{
	UInt32 config, pincap;
//	const char *devstr;
	nid_t cad = widget->funcGroup->codec->cad;
	nid_t nid = widget->nid;
//	int conn, color;

	config = widgetPinGetConfig(widget);
	widget->pin.config = config;

	pincap = widgetPinGetCaps(widget);
	widget->pin.cap = pincap;

	widget->pin.ctrl = sendCommand(HDA_CMD_GET_PIN_WIDGET_CTRL(cad, nid), cad);

	if (HDA_PARAM_PIN_CAP_EAPD_CAP(pincap)) {
		widget->params.eapdBtl = sendCommand(HDA_CMD_GET_EAPD_BTL_ENABLE(cad, nid), cad);
		widget->params.eapdBtl &= 0x7;
		widget->params.eapdBtl |= HDA_CMD_SET_EAPD_BTL_ENABLE_EAPD;
	} else
		widget->params.eapdBtl = HDAC_INVALID;
			//Slice - more advanced name
	catPinName(widget);
}

UInt32 VoodooHDADevice::widgetGetCaps(Widget *widget, int *waspin)
{
	UInt32 caps, orig, id;
	nid_t cad, nid, beeper = -1;

	cad = widget->funcGroup->codec->cad;
	nid = widget->nid;
	id = CODEC_ID(widget->funcGroup->codec);

	caps = sendCommand(HDA_CMD_GET_PARAMETER(cad, nid, HDA_PARAM_AUDIO_WIDGET_CAP), cad);
	orig = caps;

	/* On some codecs beeper is an input pin, but it is not recordable
	   alone. Also most of BIOSes does not declare beeper pin.
	   Change beeper pin node type to beeper to help parser. */
	*waspin = 0;
	switch (id) {
	case HDA_CODEC_AD1882:
	case HDA_CODEC_AD1883:
	case HDA_CODEC_AD1984:
	case HDA_CODEC_AD1984A:
	case HDA_CODEC_AD1984B:
	case HDA_CODEC_AD1987:
	case HDA_CODEC_AD1988:
	case HDA_CODEC_AD1988B:
	case HDA_CODEC_AD1989B:
		beeper = 26;
		break;
	case HDA_CODEC_ALC260:
		beeper = 23;
		break;
	case HDA_CODEC_STAC9228X:
		beeper = 35;
		break;
	case HDA_CODEC_VT2020:
		beeper = 34;
		break;
	default:
		if (HDA_PARAM_VENDOR_ID_VENDOR_ID(id) == REALTEK_VENDORID)
			beeper = 29;
		if (HDA_PARAM_VENDOR_ID_VENDOR_ID(id) == IDT_VENDORID)
			beeper = 33;
		break;
	}
	if (nid == beeper) {
		caps &= ~HDA_PARAM_AUDIO_WIDGET_CAP_TYPE_MASK;
		caps |= HDA_PARAM_AUDIO_WIDGET_CAP_TYPE_BEEP_WIDGET << HDA_PARAM_AUDIO_WIDGET_CAP_TYPE_SHIFT;
		*waspin = 1;
	}

	/*
	 * Clear "digital" flag from digital mic input, as its signal then goes
	 * to "analog" mixer and this separation just limits functionaity.
	 */
	if (id == HDA_CODEC_AD1984A && nid == 23)
		caps &= ~HDA_PARAM_AUDIO_WIDGET_CAP_DIGITAL_MASK;

	if (caps != orig)
		dumpMsg("Patching widget caps nid=%u 0x%08lx -> 0x%08lx\n", nid, (long unsigned int) orig, (long unsigned int)caps);

	return caps;
}

void VoodooHDADevice::widgetParse(Widget *widget)
{
	UInt32 wcap, cap;
	const char *typestr;
	nid_t cad = widget->funcGroup->codec->cad;
	nid_t nid = widget->nid;

	wcap = widgetGetCaps(widget, &widget->waspin);

	widget->params.widgetCap = wcap;
	widget->type = HDA_PARAM_AUDIO_WIDGET_CAP_TYPE(wcap);

	switch (widget->type) {
	case HDA_PARAM_AUDIO_WIDGET_CAP_TYPE_AUDIO_OUTPUT:
		typestr = "audio output";
		break;
	case HDA_PARAM_AUDIO_WIDGET_CAP_TYPE_AUDIO_INPUT:
		typestr = "audio input";
		break;
	case HDA_PARAM_AUDIO_WIDGET_CAP_TYPE_AUDIO_MIXER:
		typestr = "audio mixer";
		break;
	case HDA_PARAM_AUDIO_WIDGET_CAP_TYPE_AUDIO_SELECTOR:
		typestr = "audio selector";
		break;
	case HDA_PARAM_AUDIO_WIDGET_CAP_TYPE_PIN_COMPLEX:
		typestr = "pin";
		break;
	case HDA_PARAM_AUDIO_WIDGET_CAP_TYPE_POWER_WIDGET:
		typestr = "power widget";
		break;
	case HDA_PARAM_AUDIO_WIDGET_CAP_TYPE_VOLUME_WIDGET:
		typestr = "volume widget";
		break;
	case HDA_PARAM_AUDIO_WIDGET_CAP_TYPE_BEEP_WIDGET:
		typestr = "beep widget";
		break;
	case HDA_PARAM_AUDIO_WIDGET_CAP_TYPE_VENDOR_WIDGET:
		typestr = "vendor widget";
		break;
	default:
		typestr = "unknown type";
		break;
	}

#ifdef TIGER
	strncpy(widget->name, typestr, sizeof (widget->name));
#else
	strlcpy(widget->name, typestr, sizeof (widget->name));
#endif

	widgetConnectionParse(widget);

	if (HDA_PARAM_AUDIO_WIDGET_CAP_OUT_AMP(wcap)) {
		if (HDA_PARAM_AUDIO_WIDGET_CAP_AMP_OVR(wcap))
			widget->params.outAmpCap =
					sendCommand(HDA_CMD_GET_PARAMETER(cad, nid, HDA_PARAM_OUTPUT_AMP_CAP), cad);
		else
			widget->params.outAmpCap = widget->funcGroup->audio.outAmpCap;
	} else
		widget->params.outAmpCap = 0;

	if (HDA_PARAM_AUDIO_WIDGET_CAP_IN_AMP(wcap)) {
		if (HDA_PARAM_AUDIO_WIDGET_CAP_AMP_OVR(wcap))
			widget->params.inAmpCap =
					sendCommand(HDA_CMD_GET_PARAMETER(cad, nid, HDA_PARAM_INPUT_AMP_CAP), cad);
		else
			widget->params.inAmpCap = widget->funcGroup->audio.inAmpCap;
	} else
		widget->params.inAmpCap = 0;

	if ((widget->type == HDA_PARAM_AUDIO_WIDGET_CAP_TYPE_AUDIO_OUTPUT) ||
			(widget->type == HDA_PARAM_AUDIO_WIDGET_CAP_TYPE_AUDIO_INPUT)) {
		if (HDA_PARAM_AUDIO_WIDGET_CAP_FORMAT_OVR(wcap)) {
			cap = sendCommand(HDA_CMD_GET_PARAMETER(cad, nid, HDA_PARAM_SUPP_STREAM_FORMATS), cad);
			widget->params.supStreamFormats = (cap != 0) ? cap : widget->funcGroup->audio.supStreamFormats;
			cap = sendCommand(HDA_CMD_GET_PARAMETER(cad, nid, HDA_PARAM_SUPP_PCM_SIZE_RATE), cad);
			widget->params.supPcmSizeRates = (cap != 0) ? cap : widget->funcGroup->audio.supPcmSizeRates;
		} else {
			widget->params.supStreamFormats = widget->funcGroup->audio.supStreamFormats;
			widget->params.supPcmSizeRates = widget->funcGroup->audio.supPcmSizeRates;
		}
		if (HDA_PARAM_AUDIO_WIDGET_CAP_STRIPE(wcap))
			widget->stripecap = sendCommand(HDA_CMD_GET_STRIPE_CONTROL(cad, nid), cad) >> 20;
		else
			widget->stripecap = 1;
	} else {
		widget->params.supStreamFormats = 0;
		widget->params.supPcmSizeRates = 0;
	}

	if (widget->type == HDA_PARAM_AUDIO_WIDGET_CAP_TYPE_PIN_COMPLEX)
		widgetPinParse(widget);
}

Widget *VoodooHDADevice::widgetGet(FunctionGroup *funcGroup, nid_t nid)
{
	if (!funcGroup || !funcGroup->widgets || (nid < funcGroup->startNode) || (nid >= funcGroup->endNode))
		return NULL;
	return &funcGroup->widgets[nid - funcGroup->startNode];
}

/********************************************************************************************/
/********************************************************************************************/

char *VoodooHDADevice::audioCtlMixerMaskToString(UInt32 mask, char *buf, size_t len)
{
	//Slice - I change (char*) to (const char*) because of warning
	static const char * const ossname[] = SOUND_DEVICE_NAMES;

	bzero(buf, len);
	for (int i = 0, first = 1; i < SOUND_MIXER_NRDEVICES; i++) {
		if (mask & (1 << i)) {
			if (first == 0)
#ifdef TIGER
				strncat(buf, ", ", len);
			strncat(buf, ossname[i], len);
#else
				strlcat(buf, ", ", len);
			strlcat(buf, ossname[i], len);
#endif
			first = 0;
		}
	}
	return buf;
}

AudioControl *VoodooHDADevice::audioCtlEach(FunctionGroup *funcGroup, int index)
{
	if (!funcGroup || (funcGroup->nodeType != HDA_PARAM_FCT_GRP_TYPE_NODE_TYPE_AUDIO) ||
			!funcGroup->audio.controls || (funcGroup->audio.numControls < 1) ||
			(index >= funcGroup->audio.numControls))
		return NULL;
	return &funcGroup->audio.controls[index];
}

AudioControl *VoodooHDADevice::audioCtlAmpGet(FunctionGroup *funcGroup, nid_t nid, int dir, int index, int cnt)
{
	AudioControl *control;

	if (!funcGroup || !funcGroup->audio.controls)
		return NULL;

	for (int i = 0, found = 0; (control = audioCtlEach(funcGroup, i)); i++) {
		if (control->enable == 0)
			continue;
		if (control->widget->nid != nid)
			continue;
		if (dir && (control->ndir != dir))
			continue;
		if ((index >= 0) && (control->ndir == HDA_CTL_IN) && (control->dir == control->ndir) &&
				(control->index != index))
			continue;
		found++;
		if ((found == cnt) || cnt <= 0)
			return control;
	}

	return NULL;
}

void VoodooHDADevice::audioCtlAmpSetInternal(nid_t cad, nid_t nid, int index, int lmute, int rmute, int left,
		int right, int dir)
{
	UInt16 v = 0;
// dir = 0 - out, = 1 -IN
	if ((left != right) || (lmute != rmute)) {
		v = (1 << (15 - dir)) | (1 << 13) | (index << 8) | (lmute << 7) | left;
		sendCommand(HDA_CMD_SET_AMP_GAIN_MUTE(cad, nid, v), cad);
		v = (1 << (15 - dir)) | (1 << 12) | (index << 8) | (rmute << 7) | right;
	} else
		v = (1 << (15 - dir)) | (3 << 12) | (index << 8) | (lmute << 7) | left;

	sendCommand(HDA_CMD_SET_AMP_GAIN_MUTE(cad, nid, v), cad);
}

void VoodooHDADevice::audioCtlAmpSet(AudioControl *control, UInt32 mute, int left, int right)
{
	nid_t nid, cad;
	int lmute, rmute;

	
	cad = control->widget->funcGroup->codec->cad;
	nid = control->widget->nid;

	// Save new values if valid.
	if (mute != HDA_AMP_MUTE_DEFAULT)
		control->muted = mute;
	if (left != HDA_AMP_VOL_DEFAULT)
		control->left = left;
	if (right != HDA_AMP_VOL_DEFAULT)
		control->right = right;
	// Prepare effective values 
	if (control->forcemute) {
		lmute = 1;
		rmute = 1;
		left = 0;
		right = 0;
	} else {
		lmute = HDA_AMP_LEFT_MUTED(control->muted);
		rmute = HDA_AMP_RIGHT_MUTED(control->muted);
		left = control->left;
		right = control->right;
	}
	
	// Apply effective values 
	if (control->dir & HDA_CTL_OUT)
		audioCtlAmpSetInternal(cad, nid, control->index, lmute, rmute, left, right, 0);
	if (control->dir & HDA_CTL_IN)
		audioCtlAmpSetInternal(cad, nid, control->index, lmute, rmute, left, right, 1);
	 
}
//AutumnRain
void VoodooHDADevice::audioCtlAmpGetInternal(nid_t cad, nid_t nid, int index, int *lmute, int *rmute, int *left,
											 int *right, int dir)
{
	UInt16 v = 0;
	UInt32 cmd = 0;
	UInt32 res = 0xFF;
	
	//Если dir = 0 - output
	//           1 - input
	
	if(dir != 0 ) dir = 1;
	
	if(lmute != 0 || left != 0) {
		//Читаем настройки левого канала
		v = ((1 - dir) << 15) | (1 << 13) | index; 
		cmd = HDA_CMD_GET_AMP_GAIN_MUTE(cad, nid, v);
		//logMsg("GetAmp for 0x%x nid - 0x%x left\n", nid, cmd);
		res = sendCommand(HDA_CMD_GET_AMP_GAIN_MUTE(cad, nid, v), cad);
		if(lmute != 0)
			(*lmute) = (res & 0x80) >> 7;
		if(left != 0)
			(*left) = (res & 0x7F);
	}
	
	if(rmute != 0 || right != 0) {
		//Читаем настройки правого канала
		v = ((1 - dir) << 15) | index; 
		cmd = HDA_CMD_GET_AMP_GAIN_MUTE(cad, nid, v);
		//logMsg("GetAmp for 0x%x nid - 0x%x right\n", nid, cmd);
		res = sendCommand(HDA_CMD_GET_AMP_GAIN_MUTE(cad, nid, v), cad);
		if(rmute != 0)
			(*rmute) = (res & 0x80) >> 7;
		if(right != 0)
			(*right) = (res & 0x7F);
	}
	
}

void VoodooHDADevice::audioCtlAmpGetGain(AudioControl *control)
{
	nid_t nid, cad;
	int lmute = 0, rmute = 0;
	
	cad = control->widget->funcGroup->codec->cad;
	nid = control->widget->nid;
	
	/* Get carrent values */
	if (control->dir & HDA_CTL_OUT)
		audioCtlAmpGetInternal(cad, nid, control->index, &lmute, &rmute, &(control->left), &(control->right), 0);
	if (control->dir & HDA_CTL_IN)
		audioCtlAmpGetInternal(cad, nid, control->index, &lmute, &rmute, &(control->left), &(control->right), 1);

	control->muted = (rmute << 1) | lmute;
	
	//logMsg("VoodooHDADevice::audioCtlAmpGetGain for nid %d [%d %d]\n", nid, control->left, control->right);
}

void VoodooHDADevice::widgetConnectionSelect(Widget *widget, UInt8 index)
{
	if (!widget || (widget->nconns < 1) || (index > widget->nconns-1))
		return;
	sendCommand(HDA_CMD_SET_CONNECTION_SELECT_CONTROL(widget->funcGroup->codec->cad, widget->nid, index),
			widget->funcGroup->codec->cad);
	widget->selconn = index;
}

/*
 * Commutate specified record source.
 */
UInt32 VoodooHDADevice::audioCtlRecSelComm(PcmDevice *pcmDevice, UInt32 src, nid_t nid, int depth)
{
	FunctionGroup *funcGroup = pcmDevice->funcGroup;
	Widget *widget;
	UInt32 res = 0;
	char buf[64];

	if (depth > HDA_PARSE_MAXDEPTH)
		return 0;

	widget = widgetGet(funcGroup, nid);
	if (!widget || (widget->enable == 0))
		return 0;

	//dumpMsg("recSelComm widget %d\n", nid);
	
	for (int i = 0; i < widget->nconns; i++) {
		Widget *childWidget;
		if (widget->connsenable[i] == 0)
			continue;
		childWidget = widgetGet(funcGroup, widget->conns[i]);
		if (!childWidget || (childWidget->enable == 0) || (childWidget->bindAssoc == -1))
			continue;
		/* Call recursively to trace signal to it's source if needed. */
		if ((src & childWidget->ossmask) != 0) {
			if (childWidget->ossdev < 0)
				res |= audioCtlRecSelComm(pcmDevice, src, widget->conns[i], depth + 1);
			else
				res |= childWidget->ossmask;
		}
		/* We have two special cases: mixers and others (selectors). */
		if (widget->type == HDA_PARAM_AUDIO_WIDGET_CAP_TYPE_AUDIO_MIXER) {
			AudioControl *control;
			int muted;
			control = audioCtlAmpGet(funcGroup, widget->nid, HDA_CTL_IN, i, 1);
			if (!control) 
				continue;
			/* If we have input control on this node mute them according to requested sources. */
			muted = (src & childWidget->ossmask) ? 0 : 1;
			if (muted != control->forcemute) {
				control->forcemute = muted;
				audioCtlAmpSet(control, HDA_AMP_MUTE_DEFAULT, HDA_AMP_VOL_DEFAULT, HDA_AMP_VOL_DEFAULT);
			}
			dumpMsg("Recsel (%s): nid %d source %d %s\n", audioCtlMixerMaskToString(src, buf, sizeof (buf)), 
					nid, i, muted ? "mute" : "unmute");
		}else if (widget->type == HDA_PARAM_AUDIO_WIDGET_CAP_TYPE_AUDIO_SELECTOR) {
			if (widget->nconns == 0) //Slice - were 1
				break;
			if ((src & childWidget->ossmask) == 0)
			{
				//Slice - for test
				errorMsg("Recsel fails src=%08lx oss=%08lx\n", (long unsigned int)src, (long unsigned int)childWidget->ossmask);
				continue;
			}
				
			/* If we found requested source - select it and exit. */
			widgetConnectionSelect(widget, i);
			dumpMsg("Recsel (%s): nid %d source %d select\n", audioCtlMixerMaskToString(src, buf,
																							sizeof(buf)), nid, i);
			break;
		}
	}
	//dumpMsg("recSelComm widget %d return (%s)\n", nid, audioCtlMixerMaskToString(res, buf,  sizeof(buf)));
	return res;
}

/**************************************************************************************/
/**************************************************************************************/
#if MULTICHANNEL
int VoodooHDADevice::pcmChannelSetup(Channel *channel)
{
	FunctionGroup *funcGroup = channel->funcGroup;
	AudioAssoc *assocs = funcGroup->audio.assocs;
	UInt32 cap, fmtcap, pcmcap;
	int ret, max, channels, onlystereo;
	UInt16 pinset;


	channel->caps = gDefaultChanCaps;
	channel->caps.formats = channel->formats;
	channel->bit16 = 1;
	channel->bit32 = 0;
	channel->pcmRates[0] = 48000;
	channel->pcmRates[1] = 0;
	channel->stripecap = 0xff;

	ret = 0;
	channels = 0;
	onlystereo = 1;
	pinset = 0;
	fmtcap = funcGroup->audio.supStreamFormats;
	pcmcap = funcGroup->audio.supPcmSizeRates;

	max = (sizeof (channel->io) / sizeof (channel->io[0])) - 1;
	for (int i = 0; (i < 16) && (ret < max); i++) {  // (i < 16) && (ret < max) or (i < 16)
	
		int j;
		Widget *widget;

		/* Check as is correct */
		if (channel->assocNum < 0)
			break;
		/* Count only present DACs */
		if (assocs[channel->assocNum].dacs[i] <= 0)
			continue;
		/* Ignore duplicates */
		for (j = 0; j < ret; j++) {
			if (channel->io[j] == assocs[channel->assocNum].dacs[i])
				break;
		}
		if (j < ret)
			continue;

		widget = widgetGet(funcGroup, assocs[channel->assocNum].dacs[i]);
		if (!widget || (widget->enable == 0))
			continue;
			//Multi 
		if (!HDA_PARAM_AUDIO_WIDGET_CAP_STEREO(widget->params.widgetCap))
			continue; 
		cap = widget->params.supStreamFormats;
		/* if (HDA_PARAM_SUPP_STREAM_FORMATS_FLOAT32(cap)) */
		if (!HDA_PARAM_SUPP_STREAM_FORMATS_PCM(cap) && !HDA_PARAM_SUPP_STREAM_FORMATS_AC3(cap))
			continue;
		/* Many codec does not declare AC3 support on SPDIF.
		   I don't beleave that they doesn't support it! */
		if (HDA_PARAM_AUDIO_WIDGET_CAP_DIGITAL(widget->params.widgetCap))
			cap |= HDA_PARAM_SUPP_STREAM_FORMATS_AC3_MASK;
		if (ret == 0) {
			fmtcap = cap;
			pcmcap = widget->params.supPcmSizeRates;
		} else {
			fmtcap &= cap;
			pcmcap &= widget->params.supPcmSizeRates;
		}
		channel->io[ret++] = assocs[channel->assocNum].dacs[i];
		channel->stripecap &= widget->stripecap;

		/* Do not count redirection pin/dac channels. */
		if ((i == assocs[channel->assocNum].jackPin) && (assocs[channel->assocNum].hpredir >= 0))  //Slice exclude (i==15)
			continue;
		channels += HDA_PARAM_AUDIO_WIDGET_CAP_CC(widget->params.widgetCap) + 1;
		if (HDA_PARAM_AUDIO_WIDGET_CAP_CC(widget->params.widgetCap) != 1)
			onlystereo = 0;
		pinset |= (1 << i);
	
	}
	channel->io[ret] = -1;
	if (assocs[channel->assocNum].fakeredir)
		ret--;
	// Standard speaks only about stereo pins and playback, ... 
	if ((!onlystereo) || assocs[channel->assocNum].dir != HDA_CTL_OUT)
		pinset = 0;
	// ..., but there it gives us info about speakers layout. 
	assocs[channel->assocNum].pinset = pinset;
	channel->supStreamFormats = fmtcap;
	channel->supPcmSizeRates = pcmcap;
	//IOLog("pcmSetup: ret=%d pinset=%08x fmtcap=%08x pcmcap=%08x channels=%d\n",
	//	  ret, pinset, (unsigned int)fmtcap, (unsigned int)pcmcap, channels);
	/*
	 *  8bit = 0
	 * 16bit = 1
	 * 20bit = 2
	 * 24bit = 3
	 * 32bit = 4
	 */
	if (ret > 0) {
		int i = 0;
		if (HDA_PARAM_SUPP_STREAM_FORMATS_PCM(fmtcap)) {
			if (HDA_PARAM_SUPP_PCM_SIZE_RATE_16BIT(pcmcap))
				channel->bit16 = 1;
			else if (HDA_PARAM_SUPP_PCM_SIZE_RATE_8BIT(pcmcap))
				channel->bit16 = 0;
			if (HDA_PARAM_SUPP_PCM_SIZE_RATE_32BIT(pcmcap))
				channel->bit32 = 4;
			else if (HDA_PARAM_SUPP_PCM_SIZE_RATE_24BIT(pcmcap))
				channel->bit32 = 3;
			else if (HDA_PARAM_SUPP_PCM_SIZE_RATE_20BIT(pcmcap))
				channel->bit32 = 2;
			if (!(funcGroup->audio.quirks & HDA_QUIRK_FORCESTEREO)) {
				channel->formats[i++] = AFMT_S16_LE;
				if (channel->bit32 == 4)
					channel->formats[i++] = AFMT_S32_LE;
				else if (channel->bit32) channel->formats[i++] = AFMT_S32_LE;
			}
			if (channels >= 2) {
				channel->formats[i++] = AFMT_S16_LE | AFMT_STEREO; 
				if (channel->bit32 == 4)
					channel->formats[i++] = AFMT_S32_LE | AFMT_STEREO;
				else if (channel->bit32) channel->formats[i++] = AFMT_S32_LE  | AFMT_STEREO;
			}
			if (channels == 4 || /* Any 4-channel */
			    pinset == 0x0007 || /* 5.1 */
			    pinset == 0x0013 || /* 5.1 */
			    pinset == 0x0017) {  /* 7.1 */
				channel->formats[i++] = SND_FORMAT(AFMT_S16_LE, 4, 0);
				if (channel->bit32 == 4)
					channel->formats[i++] = SND_FORMAT(AFMT_S32_LE, 4, 0);
				else if (channel->bit32) channel->formats[i++] = SND_FORMAT(AFMT_S32_LE, 4, 0);
			}
			if (channels == 6 || /* Any 6-channel */
			    pinset == 0x0017) {  /* 7.1 */
				channel->formats[i++] = SND_FORMAT(AFMT_S16_LE, 6, 1);
				if (channel->bit32 == 4)
					channel->formats[i++] = SND_FORMAT(AFMT_S32_LE, 6, 1);
				else if (channel->bit32) channel->formats[i++] = SND_FORMAT(AFMT_S32_LE, 6, 1);
			}
			if (channels == 8) { /* Any 8-channel */
				channel->formats[i++] = SND_FORMAT(AFMT_S16_LE, 8, 1);
				if (channel->bit32 == 4)
					channel->formats[i++] = SND_FORMAT(AFMT_S32_LE, 8, 1);
				else if (channel->bit32) channel->formats[i++] = SND_FORMAT(AFMT_S32_LE, 8, 1);
			}
		}
		if (HDA_PARAM_SUPP_STREAM_FORMATS_AC3(fmtcap))
			channel->formats[i++] = AFMT_AC3;
		channel->formats[i] = 0;
		i = 0;
		if (HDA_PARAM_SUPP_PCM_SIZE_RATE_8KHZ(pcmcap))
			channel->pcmRates[i++] = 8000;
		if (HDA_PARAM_SUPP_PCM_SIZE_RATE_11KHZ(pcmcap))
			channel->pcmRates[i++] = 11025;
		if (HDA_PARAM_SUPP_PCM_SIZE_RATE_16KHZ(pcmcap))
			channel->pcmRates[i++] = 16000;
		if (HDA_PARAM_SUPP_PCM_SIZE_RATE_22KHZ(pcmcap))
			channel->pcmRates[i++] = 22050;
		if (HDA_PARAM_SUPP_PCM_SIZE_RATE_32KHZ(pcmcap))
			channel->pcmRates[i++] = 32000;
		if (HDA_PARAM_SUPP_PCM_SIZE_RATE_44KHZ(pcmcap))
			channel->pcmRates[i++] = 44100;
		/* if (HDA_PARAM_SUPP_PCM_SIZE_RATE_48KHZ(pcmcap)) */
		channel->pcmRates[i++] = 48000;
		if (HDA_PARAM_SUPP_PCM_SIZE_RATE_88KHZ(pcmcap))
			channel->pcmRates[i++] = 88200;
		if (HDA_PARAM_SUPP_PCM_SIZE_RATE_96KHZ(pcmcap))
			channel->pcmRates[i++] = 96000;
		if (HDA_PARAM_SUPP_PCM_SIZE_RATE_176KHZ(pcmcap))
			channel->pcmRates[i++] = 176400;
		if (HDA_PARAM_SUPP_PCM_SIZE_RATE_192KHZ(pcmcap))
			channel->pcmRates[i++] = 192000;
		/* if (HDA_PARAM_SUPP_PCM_SIZE_RATE_384KHZ(pcmcap)) */
		channel->pcmRates[i] = 0;
		if (i > 0) {
			channel->caps.minSpeed = channel->pcmRates[0];
			channel->caps.maxSpeed = channel->pcmRates[i - 1];
		}
		channel->caps.channels = channels;
	}

	return ret;
}
#else
#warning not multichannel
int VoodooHDADevice::pcmChannelSetup(Channel *channel)
{
	FunctionGroup *funcGroup = channel->funcGroup;
	AudioAssoc *assocs = funcGroup->audio.assocs;
	UInt32 cap, fmtcap, pcmcap;
	int ret, max;

	channel->caps = gDefaultChanCaps;
	channel->caps.formats = channel->formats;
	channel->bit16 = 1;
	channel->bit32 = 0;
	channel->pcmRates[0] = 48000;
	channel->pcmRates[1] = 0;
	channel->stripecap = 0xff;

	ret = 0;
	fmtcap = funcGroup->audio.supStreamFormats;
	pcmcap = funcGroup->audio.supPcmSizeRates;
	max = (sizeof (channel->io) / sizeof (channel->io[0])) - 1;

	for (int i = 0; (i < 16) && (ret < max); i++) {
		int j;
		Widget *widget;

		/* Check as is correct */
		if (channel->assocNum < 0)
			break;
		/* Count only present DACs */
		if (assocs[channel->assocNum].dacs[i] <= 0)
			continue;
		/* Ignore duplicates */
		for (j = 0; j < ret; j++) {
			if (channel->io[j] == assocs[channel->assocNum].dacs[i])
				break;
		}
		if (j < ret)
			continue;

		widget = widgetGet(funcGroup, assocs[channel->assocNum].dacs[i]);
		if (!widget || (widget->enable == 0))
			continue;
		if (!HDA_PARAM_AUDIO_WIDGET_CAP_STEREO(widget->params.widgetCap))
			continue;
		cap = widget->params.supStreamFormats;
		/* if (HDA_PARAM_SUPP_STREAM_FORMATS_FLOAT32(cap)) */
		if (!HDA_PARAM_SUPP_STREAM_FORMATS_PCM(cap) && !HDA_PARAM_SUPP_STREAM_FORMATS_AC3(cap))
			continue;
		/* Many codecs do not declare AC3 support on SPDIF.
		   I don't beleave that they don't support it! */
		if (HDA_PARAM_AUDIO_WIDGET_CAP_DIGITAL(widget->params.widgetCap))
			cap |= HDA_PARAM_SUPP_STREAM_FORMATS_AC3_MASK;
		if (ret == 0) {
			fmtcap = cap;
			pcmcap = widget->params.supPcmSizeRates;
		} else {
			fmtcap &= cap;
			pcmcap &= widget->params.supPcmSizeRates;
		}
		channel->io[ret++] = assocs[channel->assocNum].dacs[i];
		channel->stripecap &= widget->stripecap;
	}
	channel->io[ret] = -1;

	if (assocs[channel->assocNum].fakeredir)
		ret--;
	channel->supStreamFormats = fmtcap;
	channel->supPcmSizeRates = pcmcap;

	/*
	 *  8bit = 0
	 * 16bit = 1
	 * 20bit = 2
	 * 24bit = 3
	 * 32bit = 4
	 */
	if (ret > 0) {
		int i = 0;
		if (HDA_PARAM_SUPP_STREAM_FORMATS_PCM(fmtcap)) {
			if (HDA_PARAM_SUPP_PCM_SIZE_RATE_16BIT(pcmcap))
				channel->bit16 = 1;
			else if (HDA_PARAM_SUPP_PCM_SIZE_RATE_8BIT(pcmcap))
				channel->bit16 = 0;
			if (HDA_PARAM_SUPP_PCM_SIZE_RATE_32BIT(pcmcap))
				channel->bit32 = 4;
			else if (HDA_PARAM_SUPP_PCM_SIZE_RATE_24BIT(pcmcap))
				channel->bit32 = 3;
			else if (HDA_PARAM_SUPP_PCM_SIZE_RATE_20BIT(pcmcap))
				channel->bit32 = 2;
			if (!(funcGroup->audio.quirks & HDA_QUIRK_FORCESTEREO))
				channel->formats[i++] = AFMT_S16_LE;
			channel->formats[i++] = AFMT_S16_LE | AFMT_STEREO;
			if (channel->bit32 == 4) {
				if (!(funcGroup->audio.quirks & HDA_QUIRK_FORCESTEREO))
					channel->formats[i++] = AFMT_S32_LE;
				channel->formats[i++] = AFMT_S32_LE | AFMT_STEREO;
			} else if (channel->bit32) {
				if (!(funcGroup->audio.quirks & HDA_QUIRK_FORCESTEREO))
					channel->formats[i++] = AFMT_S32_LE;
				channel->formats[i++] = AFMT_S32_LE  | AFMT_STEREO;
			}
		}
		if (HDA_PARAM_SUPP_STREAM_FORMATS_AC3(fmtcap))
			channel->formats[i++] = AFMT_AC3;
		channel->formats[i] = 0;
		i = 0;
		if (HDA_PARAM_SUPP_PCM_SIZE_RATE_8KHZ(pcmcap))
			channel->pcmRates[i++] = 8000;
		if (HDA_PARAM_SUPP_PCM_SIZE_RATE_11KHZ(pcmcap))
			channel->pcmRates[i++] = 11025;
		if (HDA_PARAM_SUPP_PCM_SIZE_RATE_16KHZ(pcmcap))
			channel->pcmRates[i++] = 16000;
		if (HDA_PARAM_SUPP_PCM_SIZE_RATE_22KHZ(pcmcap))
			channel->pcmRates[i++] = 22050;
		if (HDA_PARAM_SUPP_PCM_SIZE_RATE_32KHZ(pcmcap))
			channel->pcmRates[i++] = 32000;
		if (HDA_PARAM_SUPP_PCM_SIZE_RATE_44KHZ(pcmcap))
			channel->pcmRates[i++] = 44100;
		/* if (HDA_PARAM_SUPP_PCM_SIZE_RATE_48KHZ(pcmcap)) */
		channel->pcmRates[i++] = 48000;
		if (HDA_PARAM_SUPP_PCM_SIZE_RATE_88KHZ(pcmcap))
			channel->pcmRates[i++] = 88200;
		if (HDA_PARAM_SUPP_PCM_SIZE_RATE_96KHZ(pcmcap))
			channel->pcmRates[i++] = 96000;
		if (HDA_PARAM_SUPP_PCM_SIZE_RATE_176KHZ(pcmcap))
			channel->pcmRates[i++] = 176400;
		if (HDA_PARAM_SUPP_PCM_SIZE_RATE_192KHZ(pcmcap))
			channel->pcmRates[i++] = 192000;
		/* if (HDA_PARAM_SUPP_PCM_SIZE_RATE_384KHZ(pcmcap)) */
		channel->pcmRates[i] = 0;
		if (i > 0) {
			channel->caps.minSpeed = channel->pcmRates[0];
			channel->caps.maxSpeed = channel->pcmRates[i - 1];
		}
	}

	return ret;
}

#endif
void VoodooHDADevice::createPcms(FunctionGroup *funcGroup)
{
	AudioAssoc *assocs = funcGroup->audio.assocs;
	UInt32 numAnalogPlayDevs = 0, numAnalogRecDevs = 0;
	UInt32 numDigitalPlayDevs = 0, numDigitalRecDevs = 0;

	for (int i = 0; i < funcGroup->audio.numAssocs; i++) {
		if (assocs[i].enable == 0)
			continue;
		if (assocs[i].dir == HDA_CTL_IN) {
			if (assocs[i].digital)
				numDigitalRecDevs++;
			else
				numAnalogRecDevs++;
		} else {
			if (assocs[i].digital)
				numDigitalPlayDevs++;
			else
				numAnalogPlayDevs++;
		}
	}
	funcGroup->audio.numPcmDevices = max(numAnalogRecDevs, numAnalogPlayDevs) +
			max(numDigitalRecDevs, numDigitalPlayDevs);
  if (funcGroup->audio.numPcmDevices == 0) {
      errorMsg("error: numPcmDevices is zero\n");
      return;
  }
	funcGroup->audio.pcmDevices = (PcmDevice *) allocMem(funcGroup->audio.numPcmDevices *
			sizeof (PcmDevice));
	if (!funcGroup->audio.pcmDevices) {
		errorMsg("error: unable to allocate memory for pcm devices\n");
		return;
	}
	for (int i = 0; i < funcGroup->audio.numPcmDevices; i++) {
		funcGroup->audio.pcmDevices[i].index = i;
		funcGroup->audio.pcmDevices[i].funcGroup = funcGroup;
		funcGroup->audio.pcmDevices[i].playChanId = -1;
		funcGroup->audio.pcmDevices[i].recChanId = -1;
		funcGroup->audio.pcmDevices[i].digital = 255;
	}
	for (int i = 0; i < funcGroup->audio.numAssocs; i++) {
		if (assocs[i].enable == 0)
			continue;
		for (int j = 0; j < funcGroup->audio.numPcmDevices; j++) {
			if ((funcGroup->audio.pcmDevices[j].digital != 255) &&
					((!funcGroup->audio.pcmDevices[j].digital) != (!assocs[i].digital)))
				continue;
			if (assocs[i].dir == HDA_CTL_IN) {
				if (funcGroup->audio.pcmDevices[j].recChanId >= 0)
					continue;
				funcGroup->audio.pcmDevices[j].recChanId = assocs[i].chan;
			} else {
				if (funcGroup->audio.pcmDevices[j].playChanId >= 0)
					continue;
				funcGroup->audio.pcmDevices[j].playChanId = assocs[i].chan;
			}
			mChannels[assocs[i].chan].pcmDevice = &funcGroup->audio.pcmDevices[j];
			funcGroup->audio.pcmDevices[j].digital = assocs[i].digital;
			break;
		}
	}

	for (int i = 0; i < funcGroup->audio.numPcmDevices; i++)
		pcmAttach(&funcGroup->audio.pcmDevices[i]);
}
//AutumnRain
void VoodooHDADevice::micSwitchHandlerEnableWidget(FunctionGroup *funcGroup, nid_t nid, int connNum, bool Enabled)
{
	Widget *switchWidget = widgetGet(funcGroup, nid);
	
	if(switchWidget == 0 || switchWidget->enable == 0) 
		return;

	if(switchWidget->type == HDA_PARAM_AUDIO_WIDGET_CAP_TYPE_AUDIO_MIXER) {
		AudioControl *control;
		int muted;
		control = audioCtlAmpGet(funcGroup, nid, HDA_CTL_IN, connNum, 1);
		if (!control) 
			return;
		/* If we have input control on this node mute them according to requested sources. */
		muted = (Enabled) ? 0 : 1;
		if (muted != control->forcemute) {
			control->forcemute = muted;
			audioCtlAmpSet(control, HDA_AMP_MUTE_DEFAULT, HDA_AMP_VOL_DEFAULT, HDA_AMP_VOL_DEFAULT);
		}
	}else if(switchWidget->type == HDA_PARAM_AUDIO_WIDGET_CAP_TYPE_AUDIO_SELECTOR) {
		if(Enabled)
			widgetConnectionSelect(switchWidget, connNum);
	}
	
}

void VoodooHDADevice::SwitchHandlerRename(FunctionGroup *funcGroup, nid_t nid, int assocsNum, UInt32 res)
{
	AudioAssoc *assocs;
	Widget *widget;
	nid_t defNid;
	
	assocs = funcGroup->audio.assocs;
	
	defNid = assocs[assocsNum].pins[assocs[assocsNum].defaultPin];
	
	if(res)
		widget = widgetGet(funcGroup, nid);
	else
		widget = widgetGet(funcGroup, defNid);
	
	if(widget != 0) {
		VoodooHDAEngine* engine = NULL;
		
		for(int channelNum = 0; channelNum < mNumChannels; channelNum++) {
			if( mChannels[channelNum].funcGroup != funcGroup) continue;
			if( mChannels[channelNum].assocNum == assocsNum ) {
				engine = lookupEngine(channelNum);
				if(engine != NULL) {
					logMsg("setDesc  change description %s channel %d assoc %d\n", &widget->name[5], channelNum, assocsNum);
					engine->setPinName(widget->pin.config, &widget->name[5]);
					return;
				}
				logMsg("setDesc  can't find engine for %s channel %d assoc %d\n", &widget->name[5], channelNum, assocsNum);
				return;
			}
		}
		logMsg("setDesc  can't find channel for assoc %d\n", assocsNum);
		return;
	}
	if(res)
		logMsg("setDesc  widget not find for nid %d assoc %d\n", nid, assocsNum);
	else
		logMsg("setDesc  widget not find for default nid %d assoc %d\n", defNid, assocsNum);
}
/********************************************************************************************/
/********************************************************************************************/
/*
 * Jack detection (Mic/Monitor redirection) event handler.
 */
void VoodooHDADevice::micSwitchHandler(FunctionGroup *funcGroup, nid_t nid, UInt32 res)
{
	AudioAssoc *assocs;
	Widget *widget;
	//UInt32 res;
	nid_t defaultNid; //, jackNid;
//	nid_t cad;
	int assocsNum;
	UInt32 maskJack, maskDef;
	
	if (!funcGroup || !funcGroup->codec)
		return;
	
//	cad = funcGroup->codec->cad;		
	assocs = funcGroup->audio.assocs;
	widget = widgetGet(funcGroup, nid); 
	if (!widget || (widget->enable == 0) || (widget->type != HDA_PARAM_AUDIO_WIDGET_CAP_TYPE_PIN_COMPLEX))
		return;
	assocsNum = widget->bindAssoc;
	defaultNid = assocs[assocsNum].pins[assocs[assocsNum].defaultPin];
	if (res) {
		assocs[assocsNum].activeNid = nid;
	} else
		assocs[assocsNum].activeNid = defaultNid;
	//jackNid = assocs[assocsNum].pins[assocs[assocsNum].jackPin];	
	Widget *widgetDef = widgetGet(funcGroup, defaultNid);
	maskJack = widget->bindSeqMask;
	maskDef = widgetDef->bindSeqMask;
	
	// find widget with the same assoc and bindSeq
	for (int j = funcGroup->startNode; j < funcGroup->endNode; j++) {	// all widgets
		widget = widgetGet(funcGroup, j);
		if (!widget || (widget->enable == 0) || (widget->bindAssoc != assocsNum) ||
			(widget->connsenabled < 2)) // || (widget->type == HDA_PARAM_AUDIO_WIDGET_CAP_TYPE_AUDIO_MIXER))
			continue;

		//Slice -- switch on jack and off others
		for (int i=0; i < widget->nconns; i++) {
			if (!widget->connsenable[i]) 
				continue;
			nid_t cnid = widget->conns[i];
			Widget *child = widgetGet(funcGroup, cnid);
			if ((child->type == HDA_PARAM_AUDIO_WIDGET_CAP_TYPE_AUDIO_MIXER) && !(child->pflags & HDA_ADC_MONITOR)) {
				continue; //never off child mixers except Input monitor
			}
			if (child->bindSeqMask & maskJack ) {
				micSwitchHandlerEnableWidget(funcGroup, j, i, res);
				logMsg("   switch nid %d conn %d %s\n", j, i, res?"on":"off"); 				
			} else {
				micSwitchHandlerEnableWidget(funcGroup, j, i, (1-res));
				logMsg("   switch nid %d conn %d %s\n", j, i, (1-res)?"on":"off"); 				
			}
			if (widget->type == HDA_PARAM_AUDIO_WIDGET_CAP_TYPE_AUDIO_MIXER) {
				if (child->bindSeqMask & maskDef) {
					micSwitchHandlerEnableWidget(funcGroup, j, i, (1-res));
					logMsg("   mute nid %d conn %d %s\n", j, i, (1-res)?"on":"off"); 				
				} else {
					micSwitchHandlerEnableWidget(funcGroup, j, i, res);
					logMsg("   mute nid %d conn %d %s\n", j, i, res?"on":"off"); 				
				}
				//			}
			//else	
			}
		}
	}
	//Change device name
//	SwitchHandlerRename(funcGroup, nid ,assocsNum, res);
}
/*
 * Jack detection (Speaker/HP redirection) event handler.
 */
void VoodooHDADevice::hpSwitchHandler(FunctionGroup *funcGroup, int nid, UInt32 res)
{
	AudioAssoc *assocs;
	AudioControl *control;
	Widget *widget;
	UInt32 val;
	nid_t cad;
	
	if (!funcGroup || !funcGroup->codec)
		return;

	cad = funcGroup->codec->cad;	
	assocs = funcGroup->audio.assocs;
	
	widget = widgetGet(funcGroup, nid); //assocs[assocsNum].pins[15]);
	if (!widget || (widget->enable == 0) || (widget->type != HDA_PARAM_AUDIO_WIDGET_CAP_TYPE_PIN_COMPLEX))
		return;
	int assocsNum = widget->bindAssoc;
	//Change device name
//	SwitchHandlerRename(funcGroup, nid ,assocsNum, res);
	
	/* (Un)Mute headphone pin. */
	control = audioCtlAmpGet(funcGroup, nid /* assocs[assocsNum].pins[15]*/, HDA_CTL_IN, -1, 1);
	if (control && control->mute) {
		/* If pin has muter - use it. */
		val = (res != 0) ? 0 : 1;
		if (val != (UInt32) control->forcemute) {
			control->forcemute = val;
			audioCtlAmpSet(control, HDA_AMP_MUTE_DEFAULT, HDA_AMP_VOL_DEFAULT, HDA_AMP_VOL_DEFAULT);
		}
	} else {
		/* If there is no muter - disable pin output. */
	//	widget = widgetGet(funcGroup, nid); // assocs[assocsNum].pins[15]);
	//	if (widget && (widget->type == HDA_PARAM_AUDIO_WIDGET_CAP_TYPE_PIN_COMPLEX)) {
			if (res != 0)
				val = widget->pin.ctrl | HDA_CMD_SET_PIN_WIDGET_CTRL_OUT_ENABLE;
			else
				val = widget->pin.ctrl & ~HDA_CMD_SET_PIN_WIDGET_CTRL_OUT_ENABLE;
			if (val != widget->pin.ctrl) {
				widget->pin.ctrl = val;
				sendCommand(HDA_CMD_SET_PIN_WIDGET_CTRL(cad, widget->nid, widget->pin.ctrl), cad);
			}
	//	}
	}
	/* (Un)Mute other pins. */
	for (int j = 0; j < 16; j++) {
		int pin = assocs[assocsNum].pins[j];
		if ((pin <= 0) || (pin == nid))
			continue;
		control = audioCtlAmpGet(funcGroup, pin, HDA_CTL_IN, -1, 1);
		if (control && control->mute) {
			/* If pin has muter - use it. */
			val = (res != 0) ? 1 : 0;
			if (val == (UInt32) control->forcemute)
				continue;
			control->forcemute = val;
			audioCtlAmpSet(control, HDA_AMP_MUTE_DEFAULT, HDA_AMP_VOL_DEFAULT, HDA_AMP_VOL_DEFAULT);
			continue;
		}
		/* If there is no muter - disable pin output. */
		widget = widgetGet(funcGroup, pin);
		if (widget && (widget->type == HDA_PARAM_AUDIO_WIDGET_CAP_TYPE_PIN_COMPLEX)) {
			if (res != 0)
				val = widget->pin.ctrl & ~HDA_CMD_SET_PIN_WIDGET_CTRL_OUT_ENABLE;
			else
				val = widget->pin.ctrl | HDA_CMD_SET_PIN_WIDGET_CTRL_OUT_ENABLE;
			if (val != widget->pin.ctrl) {
				widget->pin.ctrl = val;
				sendCommand(HDA_CMD_SET_PIN_WIDGET_CTRL(cad, widget->nid, widget->pin.ctrl), cad);
			}
		}
	}
}

void VoodooHDADevice::switchHandler(FunctionGroup *funcGroup, bool first)
{
	AudioAssoc *assocs;
	nid_t cad;
	Widget *widget;
	int type, res;
	
	if (!funcGroup || !funcGroup->codec)
		return;

	cad = funcGroup->codec->cad;
	assocs = funcGroup->audio.assocs;
	int assocNum;
//Slice - new Handler common for any devices
	for (assocNum = 0; assocNum < funcGroup->audio.numAssocs; assocNum++) {
		assocs[assocNum].dirty = 0;
	}
	for (int nid = funcGroup->startNode; nid < funcGroup->endNode; nid++) {	// all widgets
		widget = widgetGet(funcGroup, nid);
		if (!widget || (widget->enable == 0) || (widget->type != HDA_PARAM_AUDIO_WIDGET_CAP_TYPE_PIN_COMPLEX)) //find pinComplex
			continue;
		assocNum = widget->bindAssoc;
		if ((HDA_PARAM_PIN_CAP_PRESENCE_DETECT_CAP(widget->pin.cap) == 0) ||
			((HDA_CONFIG_DEFAULTCONF_MISC(widget->pin.config) & 1) != 0) ||
			(assocs[assocNum].hpredir < 0)) {
			//logMsg("No jack detection support at pin %d\n", assocs[i].pins[jackPin]);
			continue;
		}
		res = sendCommand(HDA_CMD_GET_PIN_SENSE(cad, nid), cad);	
		res = HDA_CMD_GET_PIN_SENSE_PRESENCE_DETECT(res);
		if (funcGroup->audio.quirks & HDA_QUIRK_SENSEINV)
			res ^= 1;
		if(!first && (widget->sense == res)) continue; // nothing changed
		widget->sense = res;
		
		logMsg("Pin sense: cad %d nid=%d res=%d\n", (int)cad,  (int)nid, (int)res);
		type = widget->pin.config & HDA_CONFIG_DEFAULTCONF_DEVICE_MASK;
		/* Get pin direction. */
		if ((type == HDA_CONFIG_DEFAULTCONF_DEVICE_LINE_OUT) ||
			(type == HDA_CONFIG_DEFAULTCONF_DEVICE_SPEAKER) ||
			(type == HDA_CONFIG_DEFAULTCONF_DEVICE_HP_OUT) ||
			(type == HDA_CONFIG_DEFAULTCONF_DEVICE_SPDIF_OUT) ||
			(type == HDA_CONFIG_DEFAULTCONF_DEVICE_DIGITAL_OTHER_OUT)){
			hpSwitchHandler(funcGroup, nid, res);
			//dir = HDA_CTL_OUT;
			//continue;
		}					
		else {
			micSwitchHandler(funcGroup, nid, res);
		}
		if (!assocs[assocNum].dirty) {
			SwitchHandlerRename(funcGroup, nid ,assocNum, res);
		}
		assocs[assocNum].dirty |= res;
	}
}

/*
 * Jack detection initializer.
 */
void VoodooHDADevice::switchInit(FunctionGroup *funcGroup)
{
	AudioAssoc *assocs = funcGroup->audio.assocs;
//    UInt32 id;
    int enable = 0 /* , poll = 0 */;
    nid_t cad;
//	int jackPin;

//	id = CODEC_ID(funcGroup->codec);
	cad = funcGroup->codec->cad;
	for (int j = funcGroup->startNode; j < funcGroup->endNode; j++) {	// all widgets
		Widget *widget;

		widget = widgetGet(funcGroup, j);
		if (!widget || (widget->enable == 0) || (widget->type != HDA_PARAM_AUDIO_WIDGET_CAP_TYPE_PIN_COMPLEX))
			continue;
		if ((HDA_PARAM_PIN_CAP_PRESENCE_DETECT_CAP(widget->pin.cap) == 0) ||
		    	((HDA_CONFIG_DEFAULTCONF_MISC(widget->pin.config) & 1) != 0)) {
			//logMsg("No jack detection support at pin %d\n", assocs[i].pins[jackPin]);
			continue;
		}
		
		if (assocs[widget->bindAssoc].hpredir < 0) {
			continue;
		}
		enable = 1;
		if (HDA_PARAM_AUDIO_WIDGET_CAP_UNSOL_CAP(widget->params.widgetCap)) {
			sendCommand(HDA_CMD_SET_UNSOLICITED_RESPONSE(cad, j,
					HDA_CMD_SET_UNSOLICITED_RESPONSE_ENABLE | HDAC_UNSOLTAG_EVENT_HP), cad);
		} /* else
			poll = 1; */
		//Slice - test initial state
		int res = sendCommand(HDA_CMD_GET_PIN_SENSE(cad, j), cad);	
		res = HDA_CMD_GET_PIN_SENSE_PRESENCE_DETECT(res);
		if (funcGroup->audio.quirks & HDA_QUIRK_SENSEINV)
			res ^= 1;
		widget->sense = res;
		UInt32 type = widget->pin.config & HDA_CONFIG_DEFAULTCONF_DEVICE_MASK;
		/* Get pin direction. */
		if ((type == HDA_CONFIG_DEFAULTCONF_DEVICE_LINE_OUT) ||
			(type == HDA_CONFIG_DEFAULTCONF_DEVICE_SPEAKER) ||
			(type == HDA_CONFIG_DEFAULTCONF_DEVICE_HP_OUT) ||
			(type == HDA_CONFIG_DEFAULTCONF_DEVICE_SPDIF_OUT) ||
			(type == HDA_CONFIG_DEFAULTCONF_DEVICE_DIGITAL_OTHER_OUT))
			logMsg("Enabling output audio routing switching at node %d:\n", j);					
		else {
			logMsg("Enabling input audio routing switching at node %d:\n", j);
		}
	/*	if (res) {
			assocs[widget->bindAssoc].activeNid = j;
	 //	SwitchHandlerRename(funcGroup, widget->bindAssoc, j, res);
		}*/
    if (!HDA_PARAM_PIN_CAP_DP(widget->pin.cap) &&
        !HDA_PARAM_PIN_CAP_HDMI(widget->pin.cap))
      continue;
    hdaa_eld_handler(widget);

	}
	funcGroup->mSwitchEnable = enable;
/*	if (enable) {
			//switchHandler(funcGroup, true);
		
		if (poll)
			errorMsg("XXX\nXXX: poll based jack detection unimplemented\nXXX\n");
	}*/
}

void VoodooHDADevice::hdaa_eld_handler(Widget *widget)
{
  uint32_t res;
  int cad = widget->funcGroup->codec->cad;
  if (!widget || (widget->enable == 0))
    return;
  if ((widget->type != HDA_PARAM_AUDIO_WIDGET_CAP_TYPE_PIN_COMPLEX))
    return;
  if (HDA_PARAM_PIN_CAP_PRESENCE_DETECT_CAP(widget->pin.cap) == 0 ||
      (HDA_CONFIG_DEFAULTCONF_MISC(widget->pin.config) & 1) != 0)
    return;
  res = sendCommand(HDA_CMD_GET_PIN_SENSE(cad, widget->nid), cad);
  if ((widget->eld != 0) == ((res & HDA_CMD_GET_PIN_SENSE_ELD_VALID) != 0))
    return;
  if (widget->eld != NULL) {
    widget->eld_len = 0;
    freeMem(widget->eld);
    widget->eld = NULL;
  }
  if ((res & HDA_CMD_GET_PIN_SENSE_ELD_VALID) == 0)
    return;
  res = sendCommand(HDA_CMD_GET_HDMI_DIP_SIZE(cad, widget->nid, 0x08), cad);
  if (res == HDA_INVALID)
    return;
  widget->eld_len = res & 0xff;
  if (widget->eld_len != 0)
    widget->eld = (uint8_t*)allocMem(widget->eld_len);
  if (widget->eld == NULL) {
    widget->eld_len = 0;
    return;
  }
  
  for (int i = 0; i < widget->eld_len; i++) {
    res = sendCommand(HDA_CMD_GET_HDMI_ELDD(cad, widget->nid, i), cad);
    if (res & 0x80000000)
      widget->eld[i] = res & 0xff;
  }
  if (mVerbose > 0) {
    logMsg("dump eld for nid=%d:\n", widget->nid);
    for (int i = 0; i < widget->eld_len; i++) {
      logMsg("  eld[%d]=0x%02x\n", i, widget->eld[i]);
    }
  }
//hdaa_channels_handler(&funcGroup->audio.assocs[widget->bindAssoc]); //там только дамп коннекторов
}

//Slice
//Change widget name
void VoodooHDADevice::catPinName(Widget *widget)
{
	
	const char *devstr = gDeviceTypes[(widget->pin.config & HDA_CONFIG_DEFAULTCONF_DEVICE_MASK) >>
									  HDA_CONFIG_DEFAULTCONF_DEVICE_SHIFT];
	int conn = (widget->pin.config & HDA_CONFIG_DEFAULTCONF_CONNECTIVITY_MASK) >> HDA_CONFIG_DEFAULTCONF_CONNECTIVITY_SHIFT;
	int color = (widget->pin.config & HDA_CONFIG_DEFAULTCONF_COLOR_MASK) >> HDA_CONFIG_DEFAULTCONF_COLOR_SHIFT;
	//Slice - more advanced name
	int where = (widget->pin.config & HDA_CONFIG_DEFAULTCONF_LOCATION_MASK) >> HDA_CONFIG_DEFAULTCONF_LOCATION_SHIFT;
	int type = (widget->pin.config & HDA_CONFIG_DEFAULTCONF_CONNECTION_TYPE_MASK) >> HDA_CONFIG_DEFAULTCONF_CONNECTION_TYPE_SHIFT;
	
	const char *ConnType;
	if(conn == 0){
	//THeKiNG
		//ConnType = ((where == 2)?"Front":"Rear");
        switch (where) {
            case 0: ConnType = "N/A"; break;
            case 1: ConnType = "Rear"; break;
            case 2: ConnType = "Front"; break;
            case 3: ConnType = "Left"; break;
            case 4: ConnType = "Right"; break;
            case 5: ConnType = "Top"; break;
            case 6: ConnType = "Bottom"; break;
            case 7: ConnType = "Rear Panel"; break;
            case 8: ConnType = "Drive Bay"; break;
            default: ConnType = "Unknown"; break;
        }
	} else if (conn == 2) {
		ConnType = gJacks[type];
	} else
		ConnType = gConnTypes[conn];
	if (where == 0x18) ConnType = "HDMI";
	if (where == 0x19) ConnType = "ATAPI";
	

#ifdef TIGER
//	strncpy(buf, str, sizeof (buf));
#else	
//	strlcpy(buf, str, sizeof (buf));
#endif
	
	
	strlcpy(widget->name, "pin: ", 6);
	strlcat(widget->name, devstr, sizeof (widget->name));
	strlcat(widget->name, " (", sizeof (widget->name));
	if ((conn == 0) && (color != 0) && (color != 15)) {
		strlcat(widget->name, gColorTypes[color], sizeof (widget->name));
		strlcat(widget->name, " ", sizeof (widget->name));
	}
	strlcat(widget->name, ConnType, sizeof (widget->name));
	strlcat(widget->name, ")", sizeof (widget->name));
}

////////////////////////////////////////////////////////////////////////////////////////////////


void VoodooHDADevice::updateExtDump(void)
{
	
	
	lockExtMsgBuffer();
	
	//Очищаю буфер
	mExtMsgBufferPos = 0;
	bzero(mExtMsgBuffer, mExtMsgBufferSize);
	
	unlockExtMsgBuffer();
	
	for(int cad = 0; cad < 15; cad++) {
		Codec *codec = mCodecs[cad];
		if(codec == 0) continue;
	
		dumpExtMsg("\n");
		dumpExtMsg("\n");
		dumpExtMsg("Codec # %d\n", cad);
		if(codec->numFuncGroups == 0) continue;
		updateExtDumpForFunctionGroup(&codec->funcGroups[0]);
	}
	
//done:
	return;
}

void VoodooHDADevice::updateExtDumpForFunctionGroup(FunctionGroup *funcGroup)
{	
	
	extDumpNodes(funcGroup);
	
	
	PcmDevice *pcmDevice;

	dumpExtMsg("\n");
	dumpExtMsg("\n");
	dumpExtMsg("PCM Devices %d count\n", funcGroup->audio.numPcmDevices);
	
	dumpExtMsg("+-------------------------+\n");
	dumpExtMsg("| DUMPING Volume Controls |\n");
	dumpExtMsg("+-------------------------+\n");
	
	
	for(int i = 0; i <  funcGroup->audio.numPcmDevices; i++) {
		pcmDevice = &funcGroup->audio.pcmDevices[i];
		dumpExtMsg("+-------------------------+\n");
		dumpExtMsg("+  PCM  #%d               +\n", i);
		dumpExtMsg("+-------------------------+\n");
		
		extDumpCtls(pcmDevice, "Master Volume", SOUND_MASK_VOLUME);
		extDumpCtls(pcmDevice, "PCM Volume", SOUND_MASK_PCM);
		extDumpCtls(pcmDevice, "CD Volume", SOUND_MASK_CD);
		extDumpCtls(pcmDevice, "Microphone Volume", SOUND_MASK_MIC);
		extDumpCtls(pcmDevice, "Microphone2 Volume", SOUND_MASK_MONITOR);
		extDumpCtls(pcmDevice, "Line-in Volume", SOUND_MASK_LINE);
		extDumpCtls(pcmDevice, "Speaker/Beep Volume", SOUND_MASK_SPEAKER);
		extDumpCtls(pcmDevice, "Recording Level", SOUND_MASK_RECLEV);
		extDumpCtls(pcmDevice, "Input Mix Level", SOUND_MASK_IMIX);
		extDumpCtls(pcmDevice, NULL, 0);
		dumpExtMsg("\n");
	}
	
}

void VoodooHDADevice::extDumpAmp(UInt32 cap, const char *banner)
{
	dumpExtMsg("     %s amp: 0x%08lx\n", banner, (long unsigned int)cap);
	dumpExtMsg("                 mute=%ld step=%ld size=%ld offset=%ld\n",
			(long int)HDA_PARAM_OUTPUT_AMP_CAP_MUTE_CAP(cap),
			(long int)HDA_PARAM_OUTPUT_AMP_CAP_NUMSTEPS(cap),
			(long int)HDA_PARAM_OUTPUT_AMP_CAP_STEPSIZE(cap),
			(long int)HDA_PARAM_OUTPUT_AMP_CAP_OFFSET(cap));
}

void VoodooHDADevice::extDumpNodes(FunctionGroup *funcGroup)
{
	static const char * const ossname[] = SOUND_DEVICE_NAMES;
	
	dumpExtMsg("\n");
	dumpExtMsg("Default Parameter\n");
	dumpExtMsg("-----------------\n");
	/*
	dumpAudioFormats(funcGroup->audio.supStreamFormats,
					 funcGroup->audio.supPcmSizeRates);
	 */
	dumpExtMsg("         IN amp: 0x%08lx\n", (long unsigned int)funcGroup->audio.inAmpCap);
	dumpExtMsg("        OUT amp: 0x%08lx\n", (long unsigned int)funcGroup->audio.outAmpCap);
	for (int i = funcGroup->startNode; i < funcGroup->endNode; i++) {
		Widget *widget = widgetGet(funcGroup, i);
		if (!widget) {
			dumpExtMsg("Ghost widget nid=%d\n", i);
			continue;
		}
		if(widget->nid == 0) {
			continue;
		}
		dumpExtMsg("\n");
		dumpExtMsg("            nid: %d%s\n", widget->nid, (widget->enable == 0) ? " [DISABLED]" : "");
		dumpExtMsg("           Name: %s\n", widget->name);
		dumpExtMsg("     Widget cap: 0x%08lx\n", (long unsigned int)widget->params.widgetCap);
		if (widget->params.widgetCap & 0x000ffeff) {
			dumpExtMsg("                ");
			if (HDA_PARAM_AUDIO_WIDGET_CAP_DELAY(widget->params.widgetCap))
				dumpExtMsg(" %dDELAY", static_cast<int>(HDA_PARAM_AUDIO_WIDGET_CAP_DELAY(widget->params.widgetCap)));
			if (HDA_PARAM_AUDIO_WIDGET_CAP_CP(widget->params.widgetCap))
				dumpExtMsg(" CP");
			if (HDA_PARAM_AUDIO_WIDGET_CAP_LR_SWAP(widget->params.widgetCap))
				dumpExtMsg(" LRSWAP");
			if (HDA_PARAM_AUDIO_WIDGET_CAP_POWER_CTRL(widget->params.widgetCap))
				dumpExtMsg(" PWR");
			if (HDA_PARAM_AUDIO_WIDGET_CAP_DIGITAL(widget->params.widgetCap))
				dumpExtMsg(" DIGITAL");
			if (HDA_PARAM_AUDIO_WIDGET_CAP_UNSOL_CAP(widget->params.widgetCap))
				dumpExtMsg(" UNSOL");
			if (HDA_PARAM_AUDIO_WIDGET_CAP_PROC_WIDGET(widget->params.widgetCap))
				dumpExtMsg(" PROC");
			if (HDA_PARAM_AUDIO_WIDGET_CAP_STRIPE(widget->params.widgetCap))
				dumpExtMsg(" STRIPE(x%d)", (widget->stripecap & 4) ? 4 : ((widget->stripecap & 2) ? 2 : 1));
			if (HDA_PARAM_AUDIO_WIDGET_CAP_FORMAT_OVR(widget->params.widgetCap))
				dumpExtMsg(" FORMAT_OVR");
			if (HDA_PARAM_AUDIO_WIDGET_CAP_AMP_OVR(widget->params.widgetCap))
				dumpExtMsg(" AMP_OVR");
			if (HDA_PARAM_AUDIO_WIDGET_CAP_OUT_AMP(widget->params.widgetCap))
				dumpExtMsg(" OUT_AMP");
			if (HDA_PARAM_AUDIO_WIDGET_CAP_IN_AMP(widget->params.widgetCap))
				dumpExtMsg(" IN_AMP");
			int j = HDA_PARAM_AUDIO_WIDGET_CAP_CC(widget->params.widgetCap);
			if (j == 1)
				dumpExtMsg(" STEREO");
			else if (j > 1)
				dumpExtMsg(" %dCH", j + 1);
			dumpExtMsg("\n");
		}
		if (widget->bindAssoc != -1)
			dumpExtMsg("    Association: %d (0x%08x)\n", widget->bindAssoc, widget->bindSeqMask);
		if (widget->ossmask != 0 || widget->ossdev >= 0) {
			char buf[64];
			dumpExtMsg("            OSS: %s", audioCtlMixerMaskToString(widget->ossmask, buf, sizeof (buf)));
			if (widget->ossdev >= 0)
				dumpExtMsg(" (%s)", ossname[widget->ossdev]);
			dumpExtMsg("\n");
		}
		
		if ((widget->type == HDA_PARAM_AUDIO_WIDGET_CAP_TYPE_AUDIO_OUTPUT) ||
			(widget->type == HDA_PARAM_AUDIO_WIDGET_CAP_TYPE_AUDIO_INPUT)) {
			//dumpAudioFormats(widget->params.supStreamFormats, widget->params.supPcmSizeRates);
		} else if (widget->type == HDA_PARAM_AUDIO_WIDGET_CAP_TYPE_PIN_COMPLEX) {
			extDumpPin(widget);
		}
		
		if (widget->params.eapdBtl != HDAC_INVALID)
			dumpExtMsg("           EAPD: 0x%08lx\n",(long unsigned int) widget->params.eapdBtl);
		if (HDA_PARAM_AUDIO_WIDGET_CAP_OUT_AMP(widget->params.widgetCap) && (widget->params.outAmpCap != 0)) {
			extDumpAmp(widget->params.outAmpCap, "Output");
			
			int left, right;
			int lmute, rmute;
			audioCtlAmpGetInternal(funcGroup->codec->cad, widget->nid, 0, &lmute, &rmute, &left, &right, 0);
			dumpExtMsg("     Output val: [0x%02X 0x%02X]\n", (lmute << 7) | left, (rmute << 7) | right);
		}
		if (HDA_PARAM_AUDIO_WIDGET_CAP_IN_AMP(widget->params.widgetCap) && (widget->params.inAmpCap != 0)) {
			extDumpAmp(widget->params.inAmpCap, " Input");
			int left, right;
			int lmute, rmute;
		
			int last = (widget->type == HDA_PARAM_AUDIO_WIDGET_CAP_TYPE_AUDIO_MIXER ||
						widget->type == HDA_PARAM_AUDIO_WIDGET_CAP_TYPE_AUDIO_SELECTOR) ? widget->nconns : 1;
			dumpExtMsg("      Input val: ");
			for (int j = 0; j < last; j++) {
				audioCtlAmpGetInternal(funcGroup->codec->cad, widget->nid, j, &lmute, &rmute, &left, &right, 1);
				dumpExtMsg("[0x%02X 0x%02X] ", (lmute << 7) | left, (rmute << 7) | right);
			}
			dumpExtMsg("\n");
		}
		if (widget->nconns > 0) {
			dumpExtMsg("    connections: %d\n", widget->nconns);
			dumpExtMsg("          |\n");
		}
		for (int j = 0; j < widget->nconns; j++) {
			Widget *childWidget = widgetGet(funcGroup, widget->conns[j]);
			dumpExtMsg("          + %s<- nid=%d [%s]", (widget->connsenable[j] == 0) ? "[DISABLED] " : "",
					widget->conns[j], !childWidget ? "GHOST!" : childWidget->name);
			if (!childWidget)
				dumpExtMsg(" [UNKNOWN]");
			else if (childWidget->enable == 0)
				dumpExtMsg(" [DISABLED]");
			if ((widget->nconns > 1) && (widget->selconn == j) &&
				(widget->type != HDA_PARAM_AUDIO_WIDGET_CAP_TYPE_AUDIO_MIXER))
				dumpExtMsg(" (selected)");
			dumpExtMsg("\n");
		}
	}
	
}

void VoodooHDADevice::extDumpCtls(PcmDevice *pcmDevice, const char *banner, UInt32 flag)
{
	FunctionGroup *funcGroup = pcmDevice->funcGroup;
	
	if (flag == 0) {
		flag = ~(SOUND_MASK_VOLUME | SOUND_MASK_PCM | SOUND_MASK_CD | SOUND_MASK_LINE |
				 SOUND_MASK_RECLEV | SOUND_MASK_MIC | SOUND_MASK_SPEAKER | SOUND_MASK_OGAIN |
				 SOUND_MASK_IMIX | SOUND_MASK_MONITOR);
	}
	
	for (int j = 0; j < SOUND_MIXER_NRDEVICES; j++) {
		AudioControl *control;
		int printed;
		if ((flag & (1 << j)) == 0)
			continue;
		printed = 0;
		for (int i = 0; (control = audioCtlEach(funcGroup, i)); i++) {
			if ((control->enable == 0) || (control->widget->enable == 0))
				continue;
			if (!(((pcmDevice->playChanId >= 0) &&
				   (control->widget->bindAssoc == mChannels[pcmDevice->playChanId].assocNum)) ||
				  ((pcmDevice->recChanId >= 0) &&
				   (control->widget->bindAssoc == mChannels[pcmDevice->recChanId].assocNum)) ||
				  ((control->widget->bindAssoc == -2) && (pcmDevice->index == 0))))
				continue;
			if ((control->ossmask & (1 << j)) == 0)
				continue;
			
			if (printed == 0) {
				char buf[64];
				dumpExtMsg("\n");
				if (banner)
					dumpExtMsg("%s", banner);
				else
					dumpExtMsg("Unknown Ctl");
				dumpExtMsg(" (OSS: %s)\n", audioCtlMixerMaskToString(1 << j, buf, sizeof (buf)));
				dumpExtMsg("   |\n");
				printed = 1;
			}
			dumpExtMsg("   +- control %2d (nid %3d %s", i, control->widget->nid, (control->ndir == HDA_CTL_IN) ?
					"in " : "out");
			if ((control->ndir == HDA_CTL_IN) && (control->ndir == control->dir))
				dumpExtMsg(" %2d): ", control->index);
			else
				dumpExtMsg("):    ");
			if (control->step > 0) {
				dumpExtMsg("%+d/%+ddB (%d steps)%s\n", (0 - control->offset) * (control->size + 1) / 4,
						(control->step - control->offset) * (control->size + 1) / 4, control->step + 1,
						control->mute ? " + mute" : "");
			} else
				dumpExtMsg("%s\n", control->mute ? "mute" : "");
		}
	}
}

void VoodooHDADevice::extDumpPin(Widget *widget)
{
	UInt32 pincap;
	UInt32 ctrl;
	
	//pincap = widgetPinGetCaps(widget);
	pincap = widget->pin.cap;
	
	dumpExtMsg("        Pin cap: 0x%08lx\n", (long unsigned int)pincap);
	dumpExtMsg("                ");
	if (HDA_PARAM_PIN_CAP_IMP_SENSE_CAP(pincap))
		dumpExtMsg(" ISC");
	if (HDA_PARAM_PIN_CAP_TRIGGER_REQD(pincap))
		dumpExtMsg(" TRQD");
	if (HDA_PARAM_PIN_CAP_PRESENCE_DETECT_CAP(pincap))
		dumpExtMsg(" PDC");
	if (HDA_PARAM_PIN_CAP_HEADPHONE_CAP(pincap))
		dumpExtMsg(" HP");
	if (HDA_PARAM_PIN_CAP_OUTPUT_CAP(pincap))
		dumpExtMsg(" OUT");
	if (HDA_PARAM_PIN_CAP_INPUT_CAP(pincap))
		dumpExtMsg(" IN");
	if (HDA_PARAM_PIN_CAP_BALANCED_IO_PINS(pincap))
		dumpExtMsg(" BAL");
	if (HDA_PARAM_PIN_CAP_VREF_CTRL(pincap)) {
		dumpExtMsg(" VREF[");
		if (HDA_PARAM_PIN_CAP_VREF_CTRL_50(pincap))
			dumpExtMsg(" 50");
		if (HDA_PARAM_PIN_CAP_VREF_CTRL_80(pincap))
			dumpExtMsg(" 80");
		if (HDA_PARAM_PIN_CAP_VREF_CTRL_100(pincap))
			dumpExtMsg(" 100");
		if (HDA_PARAM_PIN_CAP_VREF_CTRL_GROUND(pincap))
			dumpExtMsg(" GROUND");
		if (HDA_PARAM_PIN_CAP_VREF_CTRL_HIZ(pincap))
			dumpExtMsg(" HIZ");
		dumpExtMsg(" ]");
	}
	if (HDA_PARAM_PIN_CAP_EAPD_CAP(pincap))
		dumpExtMsg(" EAPD");
	dumpExtMsg("\n");
	dumpExtMsg("     Pin config: 0x%08lx\n", (long unsigned int)widget->pin.config);
	
	/*
	nid_t cad = widget->funcGroup->codec->cad;
	nid_t nid = widget->nid;
	ctrl = sendCommand(HDA_CMD_GET_PIN_WIDGET_CTRL(cad, nid), cad);
	*/
	ctrl = widget->pin.ctrl;
	
	dumpExtMsg("    Pin control: 0x%08lx",(long unsigned int) ctrl);
	if (ctrl & HDA_CMD_SET_PIN_WIDGET_CTRL_HPHN_ENABLE)
		dumpExtMsg(" HP");
	if (ctrl & HDA_CMD_SET_PIN_WIDGET_CTRL_IN_ENABLE)
		dumpExtMsg(" IN");
	if (ctrl & HDA_CMD_SET_PIN_WIDGET_CTRL_OUT_ENABLE)
		dumpExtMsg(" OUT");
	if (ctrl & HDA_CMD_SET_PIN_WIDGET_CTRL_VREF_ENABLE_MASK)
		dumpExtMsg(" VREFs");
	dumpExtMsg("\n");
}
