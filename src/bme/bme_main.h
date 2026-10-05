// BME main definitions header file

#define GFX_SCANLINES 1
#define GFX_DOUBLESIZE 2
#define GFX_USE1PAGE 0
#define GFX_USE2PAGES 4
#define GFX_USE3PAGES 8
#define GFX_WAITVBLANK 16
#define GFX_FULLSCREEN 32
#define GFX_WINDOW 64
#define GFX_NOSWITCHING 128
#define GFX_USEDIBSECTION 256

#define MOUSE_ALWAYS_VISIBLE 0
#define MOUSE_FULLSCREEN_HIDDEN 1
#define MOUSE_ALWAYS_HIDDEN 2

#define MOUSEB_LEFT 1
#define MOUSEB_RIGHT 2
#define MOUSEB_MIDDLE 4

#define JOY_LEFT 1
#define JOY_RIGHT 2
#define JOY_UP 4
#define JOY_DOWN 8
#define JOY_FIRE1 16
#define JOY_FIRE2 32
#define JOY_FIRE3 64
#define JOY_FIRE4 128

#define LEFT 0
#define MIDDLE 128
#define RIGHT 255

#define B_OFF 0
#define B_SOLID 1
#define B_NOTSOLID 2

#define MONO 0
#define STEREO 1
#define EIGHTBIT 0
#define SIXTEENBIT 2

#define VM_OFF 0
#define VM_ON 1
#define VM_ONESHOT 0
#define VM_LOOP 2
#define VM_16BIT 4

// Raw key codes index win_keytable[]. Printable keys use the ASCII value of
// the unshifted character; other keys use 256 + the low byte of their X11 /
// GDK keysym (all of which lie in 0xff00-0xffff), keeping codes below MAX_KEYS.
#define BME_XKEY(keysym) (256 + ((keysym) & 0xff))

#define KEY_BACKSPACE    8
#define KEY_CAPSLOCK     BME_XKEY(0xffe5)
#define KEY_ENTER        13
#define KEY_ESC          27
#define KEY_ALT          BME_XKEY(0xffe9)
#define KEY_CTRL         BME_XKEY(0xffe3)
#define KEY_LEFTCTRL     BME_XKEY(0xffe3)
#define KEY_RIGHTALT     BME_XKEY(0xffea)
#define KEY_RIGHTCTRL    BME_XKEY(0xffe4)
#define KEY_LEFTSHIFT    BME_XKEY(0xffe1)
#define KEY_RIGHTSHIFT   BME_XKEY(0xffe2)
#define KEY_NUMLOCK      BME_XKEY(0xff7f)
#define KEY_SCROLLLOCK   BME_XKEY(0xff14)
#define KEY_SPACE        ' '
#define KEY_TAB          9
#define KEY_F1           BME_XKEY(0xffbe)
#define KEY_F2           BME_XKEY(0xffbf)
#define KEY_F3           BME_XKEY(0xffc0)
#define KEY_F4           BME_XKEY(0xffc1)
#define KEY_F5           BME_XKEY(0xffc2)
#define KEY_F6           BME_XKEY(0xffc3)
#define KEY_F7           BME_XKEY(0xffc4)
#define KEY_F8           BME_XKEY(0xffc5)
#define KEY_F9           BME_XKEY(0xffc6)
#define KEY_F10          BME_XKEY(0xffc7)
#define KEY_F11          BME_XKEY(0xffc8)
#define KEY_F12          BME_XKEY(0xffc9)
#define KEY_A            'a'
#define KEY_N            'n'
#define KEY_B            'b'
#define KEY_O            'o'
#define KEY_C            'c'
#define KEY_P            'p'
#define KEY_D            'd'
#define KEY_Q            'q'
#define KEY_E            'e'
#define KEY_R            'r'
#define KEY_F            'f'
#define KEY_S            's'
#define KEY_G            'g'
#define KEY_T            't'
#define KEY_H            'h'
#define KEY_U            'u'
#define KEY_I            'i'
#define KEY_V            'v'
#define KEY_J            'j'
#define KEY_W            'w'
#define KEY_K            'k'
#define KEY_X            'x'
#define KEY_L            'l'
#define KEY_Y            'y'
#define KEY_M            'm'
#define KEY_Z            'z'
#define KEY_1            '1'
#define KEY_2            '2'
#define KEY_3            '3'
#define KEY_4            '4'
#define KEY_5            '5'
#define KEY_6            '6'
#define KEY_7            '7'
#define KEY_8            '8'
#define KEY_9            '9'
#define KEY_0            '0'
#define KEY_MINUS        '-'
#define KEY_EQUAL        '='
#define KEY_BRACKETL     '['
#define KEY_BRACKETR     ']'
#define KEY_SEMICOLON    ';'
#define KEY_APOST1       '\''
#define KEY_APOST2       '`'
#define KEY_COMMA        ','
#define KEY_COLON        '.'
#define KEY_PERIOD       '.'
#define KEY_SLASH        '/'
#define KEY_BACKSLASH    '\\'
#define KEY_DEL          127
#define KEY_DOWN         BME_XKEY(0xff54)
#define KEY_END          BME_XKEY(0xff57)
#define KEY_HOME         BME_XKEY(0xff50)
#define KEY_INS          BME_XKEY(0xff63)
#define KEY_LEFT         BME_XKEY(0xff51)
#define KEY_PGDN         BME_XKEY(0xff56)
#define KEY_PGUP         BME_XKEY(0xff55)
#define KEY_RIGHT        BME_XKEY(0xff53)
#define KEY_UP           BME_XKEY(0xff52)
#define KEY_WINDOWSL     BME_XKEY(0xffeb)
#define KEY_WINDOWSR     BME_XKEY(0xffec)
#define KEY_MENU         BME_XKEY(0xff67)
#define KEY_PAUSE        BME_XKEY(0xff13)
#define KEY_KPDIVIDE     BME_XKEY(0xffaf)
#define KEY_KPMULTIPLY   BME_XKEY(0xffaa)
#define KEY_KPPLUS       BME_XKEY(0xffab)
#define KEY_KPMINUS      BME_XKEY(0xffad)
#define KEY_KP0          BME_XKEY(0xffb0)
#define KEY_KP1          BME_XKEY(0xffb1)
#define KEY_KP2          BME_XKEY(0xffb2)
#define KEY_KP3          BME_XKEY(0xffb3)
#define KEY_KP4          BME_XKEY(0xffb4)
#define KEY_KP5          BME_XKEY(0xffb5)
#define KEY_KP6          BME_XKEY(0xffb6)
#define KEY_KP7          BME_XKEY(0xffb7)
#define KEY_KP8          BME_XKEY(0xffb8)
#define KEY_KP9          BME_XKEY(0xffb9)
#define KEY_KPUP         BME_XKEY(0xffb8)
#define KEY_KPDOWN       BME_XKEY(0xffb2)
#define KEY_KPLEFT       BME_XKEY(0xffb4)
#define KEY_KPRIGHT      BME_XKEY(0xffb6)
#define KEY_KPENTER      BME_XKEY(0xff8d)
#define KEY_KPEQUALS     BME_XKEY(0xffbd)
#define KEY_KPPERIOD     BME_XKEY(0xffae)


typedef struct
{
	Sint8 *start;
	Sint8 *repeat;
	Sint8 *end;
	unsigned char voicemode;
} SAMPLE;

typedef struct
{
	volatile Sint8 *pos;
	Sint8 *repeat;
	Sint8 *end;
	SAMPLE *smp;
	unsigned freq;
	volatile unsigned fractpos;
	int vol;
	int mastervol;
	unsigned panning;
	volatile unsigned voicemode;
} CHANNEL;

typedef struct
{
  unsigned rawcode;
  char *name;
} KEY;

typedef struct
{
  Sint16 xsize;
  Sint16 ysize;
  Sint16 xhot;
  Sint16 yhot;
  Uint32 offset;
} SPRITEHEADER;

typedef struct
{
  Uint32 type;
  Uint32 offset;
} BLOCKHEADER;

typedef struct
{
  Uint8 blocksname[13];
  Uint8 palettename[13];
} MAPHEADER;

typedef struct
{
  Sint32 xsize;
  Sint32 ysize;
  Uint8 xdivisor;
  Uint8 ydivisor;
  Uint8 xwrap;
  Uint8 ywrap;
} LAYERHEADER;

extern int bme_error;
