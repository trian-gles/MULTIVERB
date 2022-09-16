/* MULTIVERB - a reverberator

   This reverb instrument is based on MULTIVERB, by Jezar
   (http://www.dreampoint.co.uk/~jzracc/MULTIVERB.htm).

   p0  = output start time
   p1  = input start time
   p2  = input duration
   p3  = amplitude multiplier (pre-effect)
   p4  = room size (0-0.97143 ... don't ask)
   p5  = pre-delay time (time between dry signal and onset of reverb)
   p6  = ring-down duration
   p7  = damp (0-100%)
   p8  = dry signal level (0-100%)
   p9  = wet signal level (0-100%)
   p10 = # channels

   p3 (amplitude), p4 (room size), p5 (pre-delay), p7 (damp), p8 (dry),
   p9 (wet) and p10 (stereo width) can receive dynamic updates from a table
   or real-time control source.

   If an old-style gen table 1 is present, its values will be multiplied
   by the p3 amplitude multiplier, even if the latter is dynamic.

   The amplitude multiplier is applied to the input sound *before*
   it enters the reverberator.

   If you enter a room size greater than the maximum, you'll get the
   maximum amount -- which is probably an infinite reverb time.

   Input can be mono or stereo; output can be mono or stereo.

   Be careful with the dry and wet levels -- it's easy to get extreme
   clipping!

   John Gibson <johngibson@virginia.edu>, 2 Feb 2001; rev for v4, 7/11/04
*/
#include <stdio.h>
#include <stdlib.h>
#include <vector>
#include <ugens.h>
#include <math.h>
#include <Instrument.h>
#include "MULTIVERB.h"
#include <rt.h>
#include <rtdefs.h>


MULTIVERB :: MULTIVERB() : Instrument()
{
   in = NULL;
   branch = 0;
   warn_roomsize = true;
   warn_predelay = true;
   warn_damp = true;
   warn_dry = true;
   warn_wet = true;
   warn_width = true;
}


MULTIVERB :: ~MULTIVERB()
{
   delete [] in;
   for (size_t i = 0; i < inputChannels(); i++)
	{
		delete (*rvb_models)[i];
	}
   delete rvb_models;
}


int MULTIVERB :: init(double p[], int n_args)
{
   float outskip = p[0];
   float inskip = p[1];
   float dur = p[2];
   roomsize = p[4];
   predelay_time = p[5];
   ringdur = p[6];
   damp = p[7];
   dry = p[8];
   wet = p[9];

   // Keep reverb comb feedback <= 1.0
   max_roomsize = (1.0 - offsetroom) / scaleroom - 0.1;
   if (roomsize < 0.0)
      return die("MULTIVERB", "Room size must be between 0 and %g.",
                                                               max_roomsize);
   if (roomsize > max_roomsize) {
      roomsize = max_roomsize;
      rtcmix_advise("MULTIVERB", "Room size cannot be greater than %g. Adjusting...",
             max_roomsize);
   }
   int predelay_samps = (int) ((predelay_time * SR) + 0.5);
   if (predelay_samps > max_predelay_samps)
      return die("MULTIVERB", "Pre-delay must be between 0 and %g seconds.",
                                             (float) max_predelay_samps / SR);
   if (damp < 0.0 || damp > 90.0)
      return die("MULTIVERB", "Damp must be between 0 and 90%%.");
   if (dry < 0.0 || dry > 100.0)
      return die("MULTIVERB", "Dry signal level must be between 0 and 100%%.");
   if (wet < 0.0 || wet > 100.0)
      return die("MULTIVERB", "Wet signal level must be between 0 and 100%%.");

   if (rtsetinput(inskip, this) == -1)
      return DONT_SCHEDULE;
   if (rtsetoutput(outskip, dur + ringdur, this) == -1)
      return DONT_SCHEDULE;
   insamps = (int) (dur * SR);

   if (inputChannels() > 16)
      return die("MULTIVERB", "Can't have more than 16 input channels.");
   if (outputChannels() > 16)
      return die("MULTIVERB", "Can't have more than 16 output channels.");

   rvb_models = new std::vector<revmodel*>();


   // these need to be scaled
   for (int i = 0; i < inputChannels(); i++){
      auto rvb = new revmodel();
      rvb->setroomsize(roomsize + ((float) rand() / (RAND_MAX)) / 10);
      rvb->setpredelay(predelay_samps);
      rvb->setdamp((damp) * 0.01 + ((float) rand() / (RAND_MAX)) / 10);

      rvb->setdry(dry * 0.01);
      rvb->setwet(wet * 0.01);
      rvb->setwidth(0);
      rvb_models->push_back(rvb);
   }
   

   amparray = floc(1);
   if (amparray) {
      int lenamp = fsize(1);
      tableset(SR, dur, lenamp, amptabs);
   }

   return nSamps();
}


int MULTIVERB :: configure()
{
   in = new float [RTBUFSAMPS * inputChannels()];
   return in ? 0 : -1;
}

// THIS ALL NEEDS TO BE REWRITTEN
inline void MULTIVERB :: updateRvb(double p[])
{
   if (p[4] != roomsize) {
      roomsize = p[4];
      if (roomsize < 0.0 || roomsize > max_roomsize) {
         if (warn_roomsize) {
            rtcmix_warn("MULTIVERB", "Room size must be between 0 and %g. Adjusting...",
                                                                  max_roomsize);
            warn_roomsize = false;
         }
         roomsize = roomsize < 0.0 ? 0.0 : max_roomsize;
      }
      rvb->setroomsize(roomsize);
   }
   if (p[5] != predelay_time) {
      predelay_time = p[5];
      int predelay_samps = (int) ((predelay_time * SR) + 0.5);
      if (predelay_samps > max_predelay_samps) {
         if (warn_predelay) {
            rtcmix_warn("MULTIVERB", "Pre-delay must be between 0 and %g seconds. "
                             "Adjusting...", (float) max_predelay_samps / SR);
            warn_predelay = false;
         }
         predelay_samps = max_predelay_samps;
      }
      rvb->setpredelay(predelay_samps);
   }
   if (p[7] != damp) {
      damp = p[7];
      if (damp < 0.0 || damp > 100.0) {
         if (warn_damp) {
            rtcmix_warn("MULTIVERB", "Damp must be between 0 and 100%%. Adjusting...");
            warn_damp = false;
         }
         damp = damp < 0.0 ? 0.0 : 100.0;
      }
      rvb->setdamp(damp * 0.01);
   }
   if (p[8] != dry) {
      dry = p[8];
      if (dry < 0.0 || dry > 100.0) {
         if (warn_dry) {
            rtcmix_warn("MULTIVERB", "Dry signal level must be between 0 and 100%%. "
                                                               "Adjusting...");
            warn_dry = false;
         }
         dry = dry < 0.0 ? 0.0 : 100.0;
      }
      rvb->setdry(dry * 0.01);
   }
   if (p[9] != wet) {
      wet = p[9];
      if (wet < 0.0 || wet > 100.0) {
         if (warn_wet) {
            rtcmix_warn("MULTIVERB", "Wet signal level must be between 0 and 100%%. "
                                                               "Adjusting...");
            warn_wet = false;
         }
         wet = wet < 0.0 ? 0.0 : 100.0;
      }
      rvb->setwet(wet * 0.01);
   }
// printf("rmsz=%f, predel=%f, damp=%f, dry=%f, wet=%f, width=%f\n", roomsize, predelay_time, damp, dry, wet, width);
}


int MULTIVERB :: run()
{

   int samps = framesToRun() * inputChannels();

   if (currentFrame() < insamps)
      rtgetin(in, this, samps);

   // Scale input signal by amplitude multiplier and setline curve. 
   for (int i = 0; i < samps; i += inputChannels()) {
      // Ignoring pfields for now.
      if (--branch <= 0) {
         // double p[11];
         //update(p, 11, kRoomSize | kPreDelay | kDamp | kDry | kWet | kWidth);
         ///if (currentFrame() < insamps) {  // amp is pre-effect
         //   amp = update(3, insamps);
         //   if (amparray)
         //      amp *= tablei(currentFrame(), amparray, amptabs);
         }
         //updateRvb(p);
         branch = getSkip();
      }
      if (currentFrame() < insamps) {     // still taking input from file
         for (int j = 0; j < inputChannels(); j++){
            in[i + j] *= amp;
         }
      }
      else {                              // in ringdown phase
         for (int j = 0; j < inputChannels(); j++){
            in[i + j] = 0;
         }
      }
      increment();
   }

   // THIS SHOULD WORK.
   for (int i = 0; i < inputChannels(); i++){
      rvb->processreplace(in + i, in + i, outBuf + i, outBuf + i, framesToRun(), inputChannels(),
                                                         outputChannels());
   }
   

   return framesToRun();
}


Instrument *makeMULTIVERB()
{
   MULTIVERB *inst;

   inst = new MULTIVERB();
   inst->set_bus_config("MULTIVERB");

   return inst;
}

#ifndef EMBEDDED
void rtprofile()
{
   RT_INTRO("MULTIVERB", makeMULTIVERB);
}
#endif

