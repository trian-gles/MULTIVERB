#include <objlib.h>

#define MAXFILTS 5

class FILTSWEEP : public Instrument {
   bool    do_balance, bypass;
   int     nargs, inchan, skip, branch, insamps, nfilts;
   float   amp, pctleft, scale, cf, bw, *in;
   float   amptabs[2], cftabs[2], bwtabs[2];
   double  *amparray, *cfarray, *bwarray;
   BiQuad  *filt[MAXFILTS];
   Balance *balancer;

   void doupdate();
public:
   FILTSWEEP();
   virtual ~FILTSWEEP();
   virtual int init(double p[], int n_args);
   virtual int configure();
   virtual int run();
};

// update flags (shift amount is pfield index)
enum {
	kAmp = 1 << 3,
	kPan = 1 << 8,
	kBypass = 1 << 9,
	kFreq = 1 << 10,
	kBandwidth = 1 << 11
};

