class MYINST : public Instrument {
   int     inchan, skip, branch;
   float   amp, pctleft;
   float   *in, *amparray, amptabs[2];

public:
   MYINST();
   virtual ~MYINST();
   int init(float *, int);
   int run();
};

