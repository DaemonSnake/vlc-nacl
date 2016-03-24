
# We should only be called from `compile.sh`
ifeq ($(IN_COMPILE_SH),)
$(error "Use 'compile'.")
endif

SOURCES := 				\
	bin/ppapi.c 			\
	modules/ppapi-access.cpp 	\
	modules/ppapi-aout.c 		\
	modules/control/control.cpp	\
	modules/ppapi-vout-gl.c		\
	modules/ppapi-vout-graphics3d.c	\
	modules/ppapi-vout-window.c 	\
	src/ppapi.c

OBJ_DIR := $(BUILD_DIR)/obj

OBJS := $(SOURCES:%.c=$(OBJ_DIR)/%.o)
OBJS := $(OBJS:%.cpp=$(OBJ_DIR)/%.o)

all: vlc.pexe vlc.nexe

$(OBJ_DIR)/%.o: %.c
	mkdir -p $(shell dirname $@)
	$(CC) $(CFLAGS) -c $< -o $@
$(OBJ_DIR)/%.o: %.cpp
	mkdir -p $(shell dirname $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

ifeq ($(PNACL),1)
vlc.bugged.pexe: $(OBJS)
	$(CXX) $(LDFLAGS) $< -o $@

vlc.debug.pexe: vlc.bugged.pexe
	$(OPT) -S $< | sed s/@memcpy/@__memcpy/g | $(OPT) - -o $@

vlc.pexe: vlc.debug.pexe
	$(STRIP) $< -o $@.tmp1
	$(FREEZE) $@.tmp1 -o $@.tmp2
	rm $@.tmp1
	$(BCCOMPRESS) $@.tmp2 -o $@
	rm $@.tmp2

vlc.nexe: vlc.debug.pexe
	$(TRANS) -arch $(MACHINE) --allow-llvm-bitcode-input -threads=auto \
		$(if $(filter $(RELEASE),1),-O3,-O0) $< -o $@
else
vlc.pexe:
# nothing


endif
