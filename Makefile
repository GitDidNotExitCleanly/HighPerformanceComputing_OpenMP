# compiler
CC = pgcc

# source
SRC = loop2.c affinity_scheduler.c

# object
OBJ = $(SRC:.c=.o)

# output executable file name
OUTPUT = affinity_scheduler

$(OUTPUT) : $(OBJ)
	$(CC) -mp -O3 -tp=px -o $@ $^

%.o : %.c
	$(CC) -mp -O3 -tp=px -c $^

.PHONY : clean
clean :
	rm $(OUTPUT) $(OBJ)
