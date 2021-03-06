/* ---------------------------------------------------------------------
 *
 * Giada - Your Hardcore Loopmachine
 *
 * KernelMidi
 *
 * ---------------------------------------------------------------------
 *
 * Copyright (C) 2010-2013 Giovanni A. Zuliani | Monocasual
 *
 * This file is part of Giada - Your Hardcore Loopmachine.
 *
 * Giada - Your Hardcore Loopmachine is free software: you can
 * redistribute it and/or modify it under the terms of the GNU General
 * Public License as published by the Free Software Foundation, either
 * version 3 of the License, or (at your option) any later version.
 *
 * Giada - Your Hardcore Loopmachine is distributed in the hope that it
 * will be useful, but WITHOUT ANY WARRANTY; without even the implied
 * warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Giada - Your Hardcore Loopmachine. If not, see
 * <http://www.gnu.org/licenses/>.
 *
 * ------------------------------------------------------------------ */


#include <stdio.h>
#include "kernelMidi.h"
#include "glue.h"
#include "mixer.h"
#include "channel.h"
#include "sampleChannel.h"
#include "pluginHost.h"
#include "conf.h"


extern bool  G_midiStatus;
extern Conf  G_Conf;
extern Mixer G_Mixer;

#ifdef WITH_VST
extern PluginHost G_PluginHost;
#endif


namespace kernelMidi {


int        api         = 0;      // one api for both in & out
RtMidiOut *midiOut     = NULL;
RtMidiIn  *midiIn      = NULL;
unsigned   numOutPorts = 0;
unsigned   numInPorts  = 0;

cb_midiLearn *cb_learn = NULL;
void         *cb_data  = NULL;

std::vector<unsigned char> msg(3, 0x00);


/* ------------------------------------------------------------------ */


void startMidiLearn(cb_midiLearn *cb, void *data) {
	cb_learn = cb;
	cb_data  = data;
}


/* ------------------------------------------------------------------ */


void stopMidiLearn() {
	cb_learn = NULL;
	cb_data  = NULL;
}


/* ------------------------------------------------------------------ */


void setApi(int _api) {
	api = api;
	printf("[KM] using system 0x%x\n", api);
}


/* ------------------------------------------------------------------ */


int openOutDevice(int port) {

	try {
		midiOut = new RtMidiOut((RtMidi::Api) api, "Giada MIDI Output");
		G_midiStatus = true;
  }
  catch (RtError &error) {
    printf("[KM] MIDI out device error: %s\n", error.getMessage().c_str());
    G_midiStatus = false;
    return 0;
  }

	/* print output ports */

	numOutPorts = midiOut->getPortCount();
  printf("[KM] %d output MIDI ports found\n", numOutPorts);
  for (unsigned i=0; i<numOutPorts; i++)
		printf("  %d) %s\n", i, getOutPortName(i));

	/* try to open a port, if enabled */

	if (port != -1 && numOutPorts > 0) {
		try {
			midiOut->openPort(port, getOutPortName(port));
			printf("[KM] MIDI out port %d open\n", port);
			return 1;
		}
		catch (RtError &error) {
			printf("[KM] unable to open MIDI out port %d: %s\n", port, error.getMessage().c_str());
			G_midiStatus = false;
			return 0;
		}
	}
	else
		return 2;
}


/* ------------------------------------------------------------------ */


int openInDevice(int port) {

	try {
		midiIn = new RtMidiIn((RtMidi::Api) api, "Giada MIDI input");
		G_midiStatus = true;
  }
  catch (RtError &error) {
    printf("[KM] MIDI in device error: %s\n", error.getMessage().c_str());
    G_midiStatus = false;
    return 0;
  }

	/* print input ports */

	numInPorts = midiIn->getPortCount();
  printf("[KM] %d input MIDI ports found\n", numInPorts);
  for (unsigned i=0; i<numInPorts; i++)
		printf("  %d) %s\n", i, getInPortName(i));

	/* try to open a port, if enabled */

	if (port != -1 && numInPorts > 0) {
		try {
			midiIn->openPort(port, getInPortName(port));
			midiIn->ignoreTypes(true, true, true); // ignore all system/time msgs, for now
			printf("[KM] MIDI in port %d open\n", port);
			midiIn->setCallback(&callback);
			return 1;
		}
		catch (RtError &error) {
			printf("[KM] unable to open MIDI in port %d: %s\n", port, error.getMessage().c_str());
			G_midiStatus = false;
			return 0;
		}
	}
	else
		return 2;
}


/* ------------------------------------------------------------------ */


bool hasAPI(int API) {
	std::vector<RtMidi::Api> APIs;
	RtMidi::getCompiledApi(APIs);
	for (unsigned i=0; i<APIs.size(); i++)
		if (APIs.at(i) == API)
			return true;
	return false;
}


/* ------------------------------------------------------------------ */


const char *getOutPortName(unsigned p) {
	try { return midiOut->getPortName(p).c_str(); }
	catch (RtError &error) { return NULL; }
}

const char *getInPortName(unsigned p) {
	try { return midiIn->getPortName(p).c_str(); }
	catch (RtError &error) { return NULL; }
}


/* ------------------------------------------------------------------ */


void send(uint32_t data) {
	if (!G_midiStatus)
		return;
	msg[0] = getB1(data);
	msg[1] = getB2(data);
	msg[2] = getB3(data);
	midiOut->sendMessage(&msg);
	printf("[KM] send msg=0x%X\n", data);
}


/* ------------------------------------------------------------------ */


void send(int b1, int b2, int b3) {
	if (!G_midiStatus)
		return;
		msg[0] = b1;
		msg[1] = b2;
		msg[2] = b3;
		midiOut->sendMessage(&msg);
	printf("[KM] send msg=0x%X\n", getIValue(b1, b2, b3));
}


/* ------------------------------------------------------------------ */


void callback(double t, std::vector<unsigned char> *msg, void *data) {

	/* 0.8.0 - for now we handle other midi signals (common and real-time
	 * messages) as unknown, for debugging purposes */

	if (msg->size() < 3) {
		printf("[KM] MIDI received - unkown signal - size=%d, value=0x", (int) msg->size());
		for (unsigned i=0; i<msg->size(); i++)
			printf("%X", (int) msg->at(i));
		printf("\n");
		return;
	}

	/* in this place we want to catch two things: a) note on/note off
	 * from a keyboard and b) knob/wheel/slider movements from a
	 * controller */

	uint32_t input = getIValue(msg->at(0), msg->at(1), msg->at(2));
	uint32_t chan  = input & 0x0F000000;
	uint32_t value = input & 0x0000FF00;
	uint32_t pure  = input & 0xFFFF0000;   // input without 'value' byte

	printf("[KM] MIDI received - 0x%X (chan %d)", input, chan >> 24);

	/* start dispatcher. If midi learn is on don't parse channels, just
	 * learn incoming midi signal. Otherwise process master events first,
	 * then each channel in the stack. This way incoming signals don't
	 * get processed by glue_* when midi learning is on. */

	if (cb_learn)	{
		printf("\n");
		cb_learn(pure, cb_data);
	}
	else {

		/* process master events */

		if      (pure == G_Conf.midiInRewind) {
			printf(" >>> rewind (global) (pure=0x%X)", pure);
			glue_rewindSeq();
		}
		else if (pure == G_Conf.midiInStartStop) {
			printf(" >>> startStop (global) (pure=0x%X)", pure);
			glue_startStopSeq();
		}
		else if (pure == G_Conf.midiInActionRec) {
			printf(" >>> actionRec (global) (pure=0x%X)", pure);
			glue_startStopActionRec();
		}
		else if (pure == G_Conf.midiInInputRec) {
			printf(" >>> inputRec (global) (pure=0x%X)", pure);
			glue_startStopInputRec(false, false);   // update gui, no popup messages
		}
		else if (pure == G_Conf.midiInMetronome) {
			printf(" >>> metronome (global) (pure=0x%X)", pure);
			glue_startStopMetronome(false);
		}
		else if (pure == G_Conf.midiInVolumeIn) {
			float vf = (value >> 8)/127.0f;
			printf(" >>> input volume (global) (pure=0x%X, value=%d, float=%f)", pure, value >> 8, vf);
			glue_setInVol(vf, false);
		}
		else if (pure == G_Conf.midiInVolumeOut) {
			float vf = (value >> 8)/127.0f;
			printf(" >>> output volume (global) (pure=0x%X, value=%d, float=%f)", pure, value >> 8, vf);
			glue_setOutVol(vf, false);
		}

		/* process channels */

		for (unsigned i=0; i<G_Mixer.channels.size; i++) {

			Channel *ch = (Channel*) G_Mixer.channels.at(i);

			if (!ch->midiIn) continue;

			if      (pure == ch->midiInKeyPress) {
				printf(" >>> keyPress, ch=%d (pure=0x%X)", ch->index, pure);
				glue_keyPress(ch, false, false);
			}
			else if (pure == ch->midiInKeyRel) {
				printf(" >>> keyRel ch=%d (pure=0x%X)", ch->index, pure);
				glue_keyRelease(ch, false, false);
			}
			else if (pure == ch->midiInMute) {
				printf(" >>> mute ch=%d (pure=0x%X)", ch->index, pure);
				glue_setMute(ch, false);
			}
			else if (pure == ch->midiInSolo) {
				printf(" >>> solo ch=%d (pure=0x%X)", ch->index, pure);
				ch->solo ? glue_setSoloOn(ch, false) : glue_setSoloOff(ch, false);
			}
			else if (pure == ch->midiInVolume) {
				float vf = (value >> 8)/127.0f;
				printf(" >>> volume ch=%d (pure=0x%X, value=%d, float=%f)", ch->index, pure, value >> 8, vf);
				glue_setChanVol(ch, vf, false);
			}
			else if (pure == ((SampleChannel*)ch)->midiInPitch) {
				float vf = (value >> 8)/(127/4.0f); // [0-127] ~> [0.0 4.0]
				printf(" >>> pitch ch=%d (pure=0x%X, value=%d, float=%f)", ch->index, pure, value >> 8, vf);
				glue_setPitch(NULL, (SampleChannel*)ch, vf, false);
			}
			else if (pure == ((SampleChannel*)ch)->midiInReadActions) {
				printf(" >>> start/stop read actions ch=%d (pure=0x%X)", ch->index, pure);
				glue_startStopReadingRecs((SampleChannel*)ch, false);
			}
		}
		printf("\n");
	}
}

}  // namespace


