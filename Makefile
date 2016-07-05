
# We should only be called from `compile.sh`
ifeq ($(IN_COMPILE_SH),)
$(error "Use 'compile'.")
endif

SOURCES := 							\
	bin/ppapi.c 						\
	src/ppapi.c

OBJ_DIR := $(BUILD_DIR)/obj

OBJS := $(SOURCES:%.c=$(OBJ_DIR)/%.o)
OBJS := $(OBJS:%.cpp=$(OBJ_DIR)/%.o)

all: $(BUILD_DIR)/vlc.pexe $(BUILD_DIR)/vlc.nexe

-include $(OBJS:.o=.d)

$(OBJ_DIR)/%.o: %.c
	mkdir -p $(shell dirname $@)
	$(CC) -MP -MD $(CFLAGS) -c $< -o $@
$(OBJ_DIR)/%.o: %.cpp
	mkdir -p $(shell dirname $@)
	$(CXX) -MP -MD $(CXXFLAGS) -c $< -o $@

%.a.corrected: %.a
	@./correct_module_list.sh $<
	@touch $@;

ifeq ($(PNACL),1)
$(OBJ_DIR)/vlc.bugged.pexe: $(OBJS)
	$(CXX) -MP -MD $^ -o $@ $(LDFLAGS) -lvlc -lvlccore -lcompat -lglibc-compat -lppapi -lppapi_gles2 -lnacl_io -lc++ -lpthread -lm

$(BUILD_DIR)/vlc.debug.pexe: $(OBJ_DIR)/vlc.bugged.pexe
	$(OPT) -S $< | sed s/@memcpy/@__memcpy/g | $(OPT) - -o $@

$(BUILD_DIR)/vlc.pexe: $(BUILD_DIR)/vlc.debug.pexe
	$(STRIP) $< -o $@.tmp1
	$(FREEZE) $@.tmp1 -o $@.tmp2
	rm $@.tmp1
	$(BCCOMPRESS) $@.tmp2 -o $@
	rm $@.tmp2

$(BUILD_DIR)/vlc.nexe: $(BUILD_DIR)/vlc.debug.pexe
	$(TRANS) -arch $(MACHINE) --allow-llvm-bitcode-input -threads=auto \
		$(if $(filter $(RELEASE),1),-O3,-O0) $< -o $@
else
vlc.pexe:
# nothing


endif
