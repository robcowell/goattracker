#
# Linux makefile
#

CFLAGS+=`sdl2-config --cflags`
GTK_CFLAGS=`pkg-config --cflags gtk4 libadwaita-1 alsa`
GTK_LIBS=`pkg-config --libs gtk4 libadwaita-1 alsa`
PREFIX=../linux/
SUFFIX=
LIBS+=`sdl2-config --libs`

include makefile.common

install: $(PREFIX)goattrk2 $(PREFIX)sidmonkey $(PREFIX)gt2reloc $(PREFIX)ins2snd2 $(PREFIX)mod2sng $(PREFIX)sngspli2
	install $? /usr/local/bin
	install $(PREFIX)goattracker.1 /usr/local/man/man1
