#include <unistd.h>
#include <stdio.h>
#include <ugens.h>
#include <mixerr.h>
#include <Instrument.h>
//#include <globals.h>
#include <rt.h>
#include <rtdefs.h>
#include "HOLD.h"
#include <rtupdate.h>

float *temp_buff[MAX_AUD_IDX];
static Bool first_run = YES;
int samp_marker;
int hold_samps;
int hold_start;
float hold_dur;
Bool stop_hold;

HOLD::HOLD() : Instrument()
{
  int i;
  if (first_run) {
	for (i=0;i<MAX_AUD_IDX;i++) 
	  temp_buff[i] = NULL;
	first_run = NO;
  }
  in = NULL;
}

HOLD::~HOLD()
{
	delete [] in;
}

int HOLD::init(float p[], int n_args)
{
// p0 = start; p1 = duration (-endtime); p2 = hold dur; p3 = inchan; p4 = audio index

	int i;

	if (p[1] < 0.0) p[1] = -p[1] - p[0];

	nsamps = rtsetoutput(p[0], p[1], this);
	rtsetinput(p[0], this);

	dur = p[1];
	hold_dur = p[2];
	inchan = (int)p[3];
	aud_idx = (int)p[4];
	t_samp = 0;
	hold_samps = (int)(hold_dur*SR);
	hold_start = (int)(p[0]*SR);
	stop_hold = NO;

	if (inchan > inputchans) {
			die("HOLD", "You wanted input channel %d, but have only specified "
							"%d input channels", p[3], inputchans);
	}
	if (aud_idx >= MAX_AUD_IDX) {
	  die("HOLD", "You wanted to use audio index %d, but your RTcmix version"
		  "has only been compiled for %d", aud_idx, MAX_AUD_IDX);
	}

	return(nsamps);
}

int HOLD::run()
{
	int i,j,k,rsamps;
	float sig;

	Instrument::run();

	// Allocate some RAM to store audio in
	if (temp_buff[aud_idx] == NULL)
		temp_buff[aud_idx] = new float [hold_dur * SR];

	if (in == NULL)    /* first time, so allocate it */
	  in = new float [RTBUFSAMPS * inputchans];

	rsamps = chunksamps*inputchans;

	rtgetin(in, this, rsamps);
	
	for (i = 0; i < rsamps; i += inputchans)  {

	  sig = in[i + (int)inchan];
	  if (!stop_hold)
		  temp_buff[aud_idx][t_samp] = sig;
      else
		  i = rsamps;

	  cursamp++;
	  t_samp++;

	  if (t_samp > hold_samps) {
		t_samp = 0;
	  }

	}
	samp_marker = t_samp;

	return i;
}

Instrument*
makeHOLD()
{
	HOLD *inst;

	inst = new HOLD();
	inst->set_bus_config("HOLD");

	return inst;
}




