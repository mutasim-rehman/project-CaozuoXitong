# Mini CPU Scheduler - build everything with a single `make`.

CC      = gcc
CFLAGS  = -Wall -Wextra -pthread -O2 -std=c11
LDFLAGS = -pthread
LDLIBS  = -lrt

SRC     = src
OBJ     = build

SCHED_OBJS  = $(OBJ)/common.o $(OBJ)/jobs.o $(OBJ)/sched.o $(OBJ)/output.o \
              $(OBJ)/logger.o $(OBJ)/shm.o $(OBJ)/main.o
REPORT_OBJS = $(OBJ)/common.o $(OBJ)/output.o $(OBJ)/report.o

INPUTS  = inputs/jobs1.txt inputs/jobs2.txt inputs/jobs3.txt inputs/jobs4.txt \
          inputs/starve.txt inputs/prio.txt
QUANTUM = 2

.PHONY: all clean run results csdemo agingdemo

all: scheduler report

scheduler: $(SCHED_OBJS)
	$(CC) $(LDFLAGS) -o $@ $^ $(LDLIBS)

report: $(REPORT_OBJS)
	$(CC) $(LDFLAGS) -o $@ $^ $(LDLIBS)

$(OBJ)/%.o: $(SRC)/%.c $(wildcard $(SRC)/*.h) | $(OBJ)
	$(CC) $(CFLAGS) -c -o $@ $<

$(OBJ):
	mkdir -p $(OBJ)

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

# Bonus: effect of a context-switch cost on the averages for different quanta.
csdemo: all
	@for q in 1 2 4; do for c in 0 1; do \
		echo "=== quantum $$q, context-switch cost $$c ==="; \
		./scheduler inputs/jobs4.txt all $$q --cs $$c --quiet | sed -n '/^Comparison (/,/^$$/p'; \
	done; done

# Bonus: priority scheduling with and without aging.
agingdemo: all
	@for a in 0 4; do \
		echo "=== priority, aging $$a ==="; \
		./scheduler inputs/prio.txt prio --aging $$a --quiet | sed -n '/^Gantt/,/^Context/p'; \
	done

clean:
	rm -rf $(OBJ) scheduler report schedule.log
