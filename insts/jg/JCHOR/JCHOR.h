typedef struct {
   int    index;
   float  left_amp;
   float  right_amp;
   float  overall_amp;
} Voice;

class JCHOR : public Instrument {
   int     grainsamps, nvoices, skip, maintain_indur, grain_done, inchan;
   float   inskip, indur, transpose, minamp, ampdiff, minwait, waitdiff, seed;
   float   *grain, *amparray, amptabs[2], *in;
   Voice   *voices;

public:
   JCHOR();
   virtual ~JCHOR();
   int init(float *, short);
   int run();
private:
   int setup_voices();
   int grain_input_and_transpose();
};

