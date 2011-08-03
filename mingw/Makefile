VERSION=

SRCDIR=../src
SRCS=$(wildcard $(SRCDIR)/*.c)
OBJS=$(notdir $(SRCS:.c=$(VERSION).o))
EXE=../avs2pipe$(VERSION)_gcc.exe

CC=mingw32-gcc
CFLAGS=-Wall -O2 -DA2P_AVS$(VERSION)
LDFLAGS=
LIBS=-L$(SRCDIR)/avisynth$(VERSION) -lavisynth
STRIP=strip
RM=rm

.PHONY : all
all: $(EXE)

.PHONY : clean
clean:
	-$(RM) $(OBJS)

$(EXE): $(OBJS)
	$(CC) $(LDFLAGS) $(OBJS) $(LIBS) -o $@
	$(STRIP) $@

%$(VERSION).o: $(SRCDIR)/%.c
	$(CC) $(CFLAGS) -c -o $@ $<
