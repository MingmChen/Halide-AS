include ../support/Makefile.inc
include ../support/autoscheduler.inc

HL_TARGET=host-cuda

GRADIENT_AUTOSCHED_BIN=../gradient_autoscheduler/bin
CXXFLAGS += -I../support/

PROCESS_DEPS=$(BIN)/%/local_laplacian.a

ifndef NO_AUTO_SCHEDULE
	PROCESS_DEPS += $(BIN)/%/local_laplacian_auto_schedule.a $(BIN)/%/local_laplacian_gradient_auto_schedule.a
else
	CXXFLAGS += -DNO_AUTO_SCHEDULE
endif

all: $(BIN)/$(HL_TARGET)/process

$(GENERATOR_BIN)/local_laplacian.generator: local_laplacian_generator.cpp $(GENERATOR_DEPS)
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) $(filter %.cpp,$^) -o $@ $(GENERATOR_LDFLAGS)

$(BIN)/%/local_laplacian.a: $(GENERATOR_BIN)/local_laplacian.generator
	@mkdir -p $(@D)
	$^ -g local_laplacian -e $(GENERATOR_OUTPUTS) -o $(@D) -f local_laplacian target=$* auto_schedule=false

$(BIN)/%/local_laplacian_auto_schedule.a: $(GENERATOR_BIN)/local_laplacian.generator $(AUTOSCHED_BIN)/libauto_schedule.so
	@mkdir -p $(@D)
	HL_PERMIT_FAILED_UNROLL=1 \
	HL_WEIGHTS_DIR=$(AUTOSCHED_SRC)/gpu.weights \
	HL_DEBUG_CODEGEN=1 \
	HL_SHARED_MEMORY_LIMIT=48 \
	$< -g local_laplacian -e $(GENERATOR_OUTPUTS) -o $(@D) -f local_laplacian_auto_schedule target=$*-no_runtime auto_schedule=true -p $(AUTOSCHED_BIN)/libauto_schedule.so -s Adams2019 machine_params=$(HL_MACHINE_PARAMS) 2> stderr.txt > stdout.txt

$(BIN)/%/local_laplacian_gradient_auto_schedule.a: $(GENERATOR_BIN)/local_laplacian.generator
	@mkdir -p $(@D)
	$^ -g local_laplacian -e $(GENERATOR_OUTPUTS) -o $(@D) -f local_laplacian_gradient_auto_schedule target=$*-no_runtime auto_schedule=true -p $(GRADIENT_AUTOSCHED_BIN)/libgradient_autoscheduler.so -s Li2018

$(BIN)/%/process: process.cpp $(PROCESS_DEPS)
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -I$(@D) -Wall $^ -o $@ $(LDFLAGS) $(IMAGE_IO_FLAGS) $(CUDA_LDFLAGS) $(OPENCL_LDFLAGS) $(OPENGL_LDFLAGS)

$(BIN)/%/out.png: $(BIN)/%/process
	@mkdir -p $(@D)
	$< $(IMAGES)/rgb.png 8 1 1 10 $@
	rm $@

$(BIN)/%/out.tiff: $(BIN)/%/process
	@mkdir -p $(@D)
	$< $(IMAGES)/rgb.png 8 1 1 10 $@
	rm $@

$(BIN)/%/process_viz: process.cpp $(BIN)/%-trace_all/local_laplacian.a
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -DNO_AUTO_SCHEDULE -I$(BIN)/$*-trace_all -Wall $^ -o $@ $(LDFLAGS) $(IMAGE_IO_FLAGS) $(CUDA_LDFLAGS) $(OPENCL_LDFLAGS) $(OPENGL_LDFLAGS)

../../bin/HalideTraceViz: ../../util/HalideTraceViz.cpp
	$(MAKE) -C ../../ bin/HalideTraceViz

$(BIN)/%/local_laplacian.mp4: $(BIN)/%/process_viz ../../bin/HalideTraceViz
	@mkdir -p $(@D)
	HL_AVCONV=$(HL_AVCONV) bash viz.sh $(<D)

clean:
	rm -rf $(BIN)

test: $(BIN)/$(HL_TARGET)/out.png $(BIN)/$(HL_TARGET)/out.tiff

viz: $(BIN)/$(HL_TARGET)/local_laplacian.mp4
	$(HL_VIDEOPLAYER) $^

$(BIN)/%/viz_auto.mp4: $(BIN)/%/process_viz ../support/viz_auto.sh ../../bin/HalideTraceViz
	@mkdir -p $(@D)
	HL_AVCONV=$(HL_AVCONV) bash ../support/viz_auto.sh "$(BIN)/$*/process_viz $(IMAGES)/rgb_small.png 4 1 1 0 $(BIN)/$*/out_small.png" ../../bin/HalideTraceViz $@

viz_auto: $(BIN)/$(HL_TARGET)/viz_auto.mp4
	$(HL_VIDEOPLAYER) $^

BATCH_ID ?= 0
TRAIN_ONLY ?= 0
SAMPLES_DIR ?= autotuned_samples
autotune: $(GENERATOR_BIN)/local_laplacian.generator $(AUTOSCHED_BIN)/featurization_to_sample $(AUTOSCHED_BIN)/retrain_cost_model $(AUTOSCHED_BIN)/libauto_schedule.so $(AUTOSCHED_SRC)/autotune_loop.sh
	HL_TARGET=host-cuda \
	HL_MACHINE_PARAMS=80,1,1 \
	SAMPLES_DIR=$(SAMPLES_DIR) \
	HL_WEIGHTS_DIR=$(AUTOSCHED_SRC)/gpu.weights \
	HL_DEBUG_CODEGEN=0 \
	HL_SHARED_MEMORY_LIMIT=48 \
	bash $(AUTOSCHED_SRC)/autotune_loop.sh \
		$(GENERATOR_BIN)/local_laplacian.generator \
		local_laplacian \
		host-cuda \
		$(AUTOSCHED_SRC)/gpu.weights \
		$(AUTOSCHED_BIN) \
		$(BATCH_ID) \
		$(TRAIN_ONLY)
