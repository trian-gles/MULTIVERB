#include <objlib.h>

class REV : public Instrument {
   int     inchan, insamps, skip;
   float   amp, rvbtime, rvbpct;
   float   *in, *amparray, amptabs[2];
   Reverb  *reverb;

public:
   REV();
   virtual ~REV();
   int init(double p[], int n_args);
   int run();
};

