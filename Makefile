# Mini CPU Scheduler - build everything with a single `make`.

CXX      = g++
CXXFLAGS = -Wall -Wextra -pthread -O2
LDLIBS   = -lrt

INPUTS  = inputs/jobs1.txt inputs/jobs2.txt inputs/jobs3.txt inputs/jobs4.txt \
          inputs/starve.txt
QUANTUM = 2

.PHONY: all clean run results starvedemo

all: scheduler report

scheduler: src/scheduler.cpp src/logger.cpp src/common.h
	$(CXX) $(CXXFLAGS) -o $@ src/scheduler.cpp src/logger.cpp $(LDLIBS)

report: src/report.cpp src/common.h
	$(CXX) $(CXXFLAGS) -o $@ src/report.cpp $(LDLIBS)

run: all
	./scheduler inputs/jobs1.txt rr $(QUANTUM)

# Regenerate results.txt: "all" mode comparison table for every input file.
results: all
	@echo "Mini CPU Scheduler - comparison tables (all mode, quantum $(QUANTUM))" > results.txt
	@for f in $(INPUTS); do \
		echo "" >> results.txt; \
		./scheduler $$f all $(QUANTUM) --quiet | sed -n '/^Comparison (/,/^$$/p' >> results.txt; \
	done
	@cat results.txt

# Bonus: starvation demo under SJF/SRTF vs FCFS.
starvedemo: all
	./scheduler inputs/starve.txt all $(QUANTUM) --quiet

clean:
	rm -f scheduler report schedule.log
