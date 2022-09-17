include package.conf

NAME = MULTIVERB

OBJS = $(NAME).o allpass.o comb.o delay.o revmodel.o
CMIXOBJS += $(PROFILE_O)
CXXFLAGS += -I. -Wall 
PROGS = $(NAME) lib$(NAME).so

all: lib$(NAME).so

standalone: $(NAME)

lib$(NAME).so: $(OBJS) $(GENLIB)
	$(CXX) $(SHARED_LDFLAGS) -o $@ $(OBJS) $(GENLIB) $(SYSLIBS)

lib$(NAME2).so: $(GENLIB)
	$(CXX) $(SHARED_LDFLAGS) -o $@ $(GENLIB) $(SYSLIBS)

$(NAME): $(OBJS) $(CMIXOBJS)
	$(CXX) -o $@ $(OBJS) $(CMIXOBJS) $(LDFLAGS)

$(OBJS): $(INSTRUMENT_H) $(NAME).hpp allpass.hpp comb.hpp delay.hpp revmodel.hpp

clean:
	$(RM) $(OBJS) $(PROGS)