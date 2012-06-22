
SRCDIR                = .
SUBDIRS               =
DLLS                  = d3dx8.dll
LIBS                  =
EXES                  =

### Common settings

TARGETFLAGS           = -m32
CEXTRA                = $(TARGETFLAGS) -g -O2 -fPIC -Wall -pipe -fno-strict-aliasing -Wdeclaration-after-statement -Wempty-body -Wignored-qualifiers -Wstrict-prototypes -Wtype-limits -Wunused-but-set-parameter -Wwrite-strings -fno-omit-frame-pointer -Wpointer-arith -Wlogical-op
CXXEXTRA              =
RCEXTRA               =
DEFINES               = -D__WINESRC__ -D_REENTRANT
INCLUDE_PATH          = -Iinclude
DLL_PATH              =
DLL_IMPORTS           =
LIBRARY_PATH          =
LIBRARIES             =

### d3dx8.dll sources and settings

d3dx8_dll_MODULE      = d3dx8.dll
d3dx8_dll_C_SRCS      = core.c \
			d3dx8_main.c \
			math.c \
			mesh.c
d3dx8_dll_CXX_SRCS    =
d3dx8_dll_RC_SRCS     =
d3dx8_dll_LDFLAGS     = -shared $(TARGETFLAGS) -fasynchronous-unwind-tables $(d3dx8_dll_MODULE:.dll=.spec)
d3dx8_dll_ARFLAGS     =
d3dx8_dll_DLL_PATH    =
d3dx8_dll_DLLS        = dxguid uuid d3d8
d3dx8_dll_LIBRARY_PATH=
d3dx8_dll_LIBRARIES   =
d3dx8_dll_OBJS        = $(d3dx8_dll_C_SRCS:.c=.o) $(d3dx8_dll_CXX_SRCS:.cpp=.o) $(d3dx8_dll_RC_SRCS:.rc=.res)

### Global source lists

C_SRCS                = $(d3dx8_dll_C_SRCS)
CXX_SRCS              = $(d3dx8_dll_CXX_SRCS)
RC_SRCS               = $(d3dx8_dll_RC_SRCS)

### Tools

WINEBUILD             = winebuild
CC                    = winegcc
CXX                   = wineg++
RC                    = wrc
AR                    = ar

### Generic targets

all: $(SUBDIRS) $(DLLS:%=%.so) lib$(d3dx8_dll_MODULE:.dll=.def) $(LIBS) $(EXES)

### Build rules

.PHONY: all clean dummy

$(SUBDIRS): dummy
	@cd $@ && $(MAKE)

# Implicit rules

.SUFFIXES: .cpp .cxx .rc .res
DEFINCL = $(INCLUDE_PATH) $(DEFINES) $(OPTIONS)

.c.o:
	$(CC) -c $(CFLAGS) $(CEXTRA) $(DEFINCL) -o $@ $<

.cpp.o:
	$(CXX) -c $(CXXFLAGS) $(CXXEXTRA) $(DEFINCL) -o $@ $<

.cxx.o:
	$(CXX) -c $(CXXFLAGS) $(CXXEXTRA) $(DEFINCL) -o $@ $<

.rc.res:
	$(RC) $(RCFLAGS) $(RCEXTRA) $(DEFINCL) -fo$@ $<

# Rules for cleaning

#CLEAN_FILES     = y.tab.c y.tab.h lex.yy.c core *.orig *.rej \
#                  \\\#*\\\# *~ *% .\\\#*

clean:: $(SUBDIRS:%=%/__clean__) $(EXTRASUBDIRS:%=%/__clean__)
	$(RM) $(CLEAN_FILES) $(RC_SRCS:.rc=.res) $(C_SRCS:.c=.o) $(CXX_SRCS:.cpp=.o)
	$(RM) $(DLLS:%=%.so) $(LIBS) $(EXES) $(EXES:%=%.so)
	$(RM) lib$(d3dx8_dll_MODULE:.dll=.def)

$(SUBDIRS:%=%/__clean__): dummy
	cd `dirname $@` && $(MAKE) clean

$(EXTRASUBDIRS:%=%/__clean__): dummy
	-cd `dirname $@` && $(RM) $(CLEAN_FILES)

### Target specific build rules
DEFLIB = $(LIBRARY_PATH) $(LIBRARIES) $(DLL_PATH) $(DLL_IMPORTS:%=-l%)

lib$(d3dx8_dll_MODULE:.dll=.def): $(d3dx8_dll_MODULE:.dll=.spec)
	$(WINEBUILD) $(TARGETFLAGS) -w --def -o $@ --export $<

$(d3dx8_dll_MODULE).so: $(d3dx8_dll_OBJS) $(d3dx8_dll_MODULE:.dll=.spec)
	$(CC) $(d3dx8_dll_LDFLAGS) -o $@ $(d3dx8_dll_OBJS) $(DEFLIB) $(d3dx8_dll_DLL_PATH) $(d3dx8_dll_DLLS:%=-l%) $(d3dx8_dll_LIBRARY_PATH) $(d3dx8_dll_LIBRARIES)

