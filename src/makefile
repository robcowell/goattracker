#
# Linux makefile
#

CFLAGS+=`sdl-config --cflags`
PREFIX=../linux/
SUFFIX=
LIBS+=`sdl-config --libs`

include makefile.common

install: $(PREFIX)goattrk2 $(PREFIX)gt2reloc $(PREFIX)ins2snd2 $(PREFIX)mod2sng $(PREFIX)sngspli2
	install $? /usr/local/bin
	install $(PREFIX)goattracker.1 /usr/local/man/man1
