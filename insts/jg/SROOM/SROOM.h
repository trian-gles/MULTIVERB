#define NTAPS 10

class SROOM : public Instrument {
   int    inchan, insamps, skip;
   int    deltabs[2];
   float  ovamp;
   float  del[NTAPS], amp[NTAPS];
   float  *in, *delayline, *rvbarrayl, *rvbarrayr, *amparray, amptabs[2];

public:
   SROOM();
   virtual ~SROOM();
   int init(double p[], int n_args);
   int run();
private:
   float distndelset(float, float, float, float, float, float);
};

