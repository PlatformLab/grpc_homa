/* Copyright (c) 2014-2021 Stanford University
 *
 * Permission to use, copy, modify, and distribute this software for any purpose
 * with or without fee is hereby granted, provided that the above copyright
 * notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR(S) DISCLAIM ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL AUTHORS BE LIABLE FOR ANY
 * SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER
 * RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF
 * CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
 * CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <string.h>
#include <sys/time.h>

#include <mutex>

#include "time_trace.h"

thread_local TimeTrace::Buffer TimeTrace::tb;
std::vector<TimeTrace::Buffer*> TimeTrace::threadBuffers;
int TimeTrace::frozen = 0;

// Synchronizes accesses to ThreadBuffers.
static std::mutex mutex;

/**
 * Frees all of the thread-local buffers that are no longer in use
 * (they don't get freed when the ThreadBuffer objects are deleted, in
 * order to allow timetraces to be dumped after threads have exited).
 */
void TimeTrace::cleanup()
{
	std::lock_guard<std::mutex> guard(mutex);
	for (int i = (int) (threadBuffers.size() - 1); i >= 0; i--) {
		Buffer *buffer = threadBuffers[i];
		if (buffer->refCount == 0) {
			delete buffer;
			threadBuffers.erase(threadBuffers.begin() + i);
		}
	}
}

/**
 * Stop all recording of trace events until they have been printed.
 */
void TimeTrace::freeze()
{
	frozen = 1;
}

/**
 * Returns the number of rdtsc clock ticks per second.
 */
double TimeTrace::getCyclesPerSec()
{
	static double cps = 0;
	if (cps != 0) {
		return cps;
	}
	
	// Take parallel time readings using both rdtsc and gettimeofday.
	// After 10ms have elapsed, take the ratio between these readings.

	struct timeval startTime, stopTime;
	uint64_t startCycles, stopCycles, micros;
	double oldCps;

	// There is one tricky aspect, which is that we could get interrupted
	// between calling gettimeofday and reading the cycle counter, in which
	// case we won't have corresponding readings.  To handle this (unlikely)
	// case, compute the overall result repeatedly, and wait until we get
	// two successive calculations that are within 0.1% of each other.
	oldCps = 0;
	while (1) {
		if (gettimeofday(&startTime, NULL) != 0) {
			printf("count_cycles_per_sec couldn't read clock: %s",
					strerror(errno));
			exit(1);
		}
		startCycles = rdtsc();
		while (1) {
			if (gettimeofday(&stopTime, NULL) != 0) {
				printf("count_cycles_per_sec couldn't read clock: %s",
						strerror(errno));
				exit(1);
			}
			stopCycles = rdtsc();
			micros = (stopTime.tv_usec - startTime.tv_usec) +
				(stopTime.tv_sec - startTime.tv_sec)*1000000;
			if (micros > 10000) {
				cps = (double)(stopCycles - startCycles);
				cps = 1000000.0*cps/(double)(micros);
				break;
			}
		}
		double delta = cps/1000.0;
		if ((oldCps > (cps - delta)) && (oldCps < (cps + delta))) {
			return cps;
		}
		oldCps = cps;
	}
}

/**
 * Return a string containing all of the trace records from all of the
 * thread-local buffers.
 */
std::string TimeTrace::getTrace()
{
	std::string s;
	TimeTrace::printInternal(&s, NULL);
	return s;
}

/**
 * Does most of the work for both printToFile and getTrace.
 * \param s
 *      If non-NULL, refers to a string that will hold a printout of the
 *      time trace.
 * \param f
 *      If non-NULL, refers to an open file on which the trace will be
 *      printed.
 */
void
TimeTrace::printInternal(std::string *s, FILE *f)
{
	std::vector<Buffer*> buffers;
	
	freeze();
	
	/* Make a copy of ThreadBuffers in order to avoid potential
	 * synchronization issues with new threads modifying it.
	 */
	{
		std::lock_guard<std::mutex> guard(mutex);
		buffers = threadBuffers;
	}

	/* The index of the next event to consider from each buffer. */
	std::vector<int> current;

	/* Find the first (oldest) event in each trace. This will be events[0]
	 * if we never completely filled the buffer, otherwise events[next_index+1].
	 * This means we don't print the entry at next_index; this is convenient
	 * because it simplifies boundary conditions in the code below. Also,
     * compute two times: the most recent of the oldest times in all the
     * traces that have wrapped, and the oldest time in all the traces.
	 */
	uint64_t latestWrapStart = 0;
    uint64_t earliestNoWrapStart = ~0;
	for (uint32_t i = 0; i < buffers.size(); i++) {
		Buffer* buffer = buffers[i];
		int index = (buffer->nextIndex + 1) % Buffer::BUFFER_SIZE;
		if (buffer->events[index].format != NULL) {
			current.push_back(index);
            if (buffer->events[index].timestamp > latestWrapStart) {
                latestWrapStart = buffer->events[index].timestamp;
            }
		} else {
			current.push_back(0);
            if (buffer->events[0].timestamp < earliestNoWrapStart) {
                earliestNoWrapStart = buffer->events[0].timestamp;
            }
		}
	}

	/* Decide on the time of the first event to be included in the output.
     * If none of the traces has wrapped, then this is just the oldest time
     * in any trace. But, if a trace has wrapped, then we don't want to
     * include any data earlier than the oldest record in that trace, since
     * this could result in incomplete output. Thus, in this case the time
     * of the first even is the most recent of the oldest times in all the
     * traces that have wrapped.
	 */
    uint64_t startTime;
    if (latestWrapStart != 0) {
        startTime = latestWrapStart;
    } else {
        startTime = earliestNoWrapStart;
    }

	// Skip all events before the starting time.
	for (uint32_t i = 0; i < buffers.size(); i++) {
		TimeTrace::Buffer* buffer = buffers[i];
		while ((buffer->events[current[i]].format != NULL) &&
				(buffer->events[current[i]].timestamp
				< startTime) &&
				(current[i] != buffer->nextIndex)) {
		    current[i] = (current[i] + 1) % Buffer::BUFFER_SIZE;
		}
	}

	/* Each iteration through this loop processes one event (the one with
	 * the earliest timestamp).
	 */
	double prevMicros = 0.0;
    bool first = true;
	while (1) {
		TimeTrace::Buffer* buffer;
		Event* event;

		/* Check all the traces to find the earliest available event. */
		uint32_t curBuf = ~0;
		uint64_t earliestTime = ~0;
		for (uint32_t i = 0; i < buffers.size(); i++) {
			buffer = buffers[i];
			event = &buffer->events[current[i]];
			if ((current[i] != buffer->nextIndex)
					&& (event->format != NULL)
					&& (event->timestamp < earliestTime)) {
				curBuf = i;
				earliestTime = event->timestamp;
			}
		}
		if (curBuf == ~0U)
			break;
		buffer = buffers[curBuf];
		event = &buffer->events[current[curBuf]];
		current[curBuf] = (current[curBuf] + 1) % Buffer::BUFFER_SIZE;
        
		char message[1000];
		char core_id[20];
        
        if (first) {
            // Output an initial (synthetic) record with the starting time.
            snprintf(core_id, sizeof(core_id), "[%s]", buffer->name.c_str());
            if (s != NULL) {
                    first = false;
                snprintf(message, sizeof(message),
                        "%9.3f us (+%8.3f us) %-6s First event "
                        "has timestamp %lu (cpu_ghz %.15f)",
                        0.0, 0.0, core_id, startTime,
                        getCyclesPerSec()*1e-9);
                s->append(message);
            }
            if (f != NULL) {
                fprintf(f, "%9.3f us (+%8.3f us) %-6s First event "
                        "has timestamp %lu (cpu_ghz %.15f)\n",
                        0.0, 0.0, core_id, startTime,
                        getCyclesPerSec()*1e-9);
            }
            first = false;
        }

		snprintf(core_id, sizeof(core_id), "[%s]",
				buffer->name.c_str());
		double micros = toSeconds(event->timestamp - startTime) *1e6;
		if (s != NULL) {
			snprintf(message, sizeof(message),
					"\n%9.3f us (+%8.3f us) %-6s ",
					micros, micros - prevMicros, core_id);
			s->append(message);
			snprintf(message, sizeof(message), event->format,
					event->arg0, event->arg1, event->arg2,
					event->arg3);
			s->append(message);
		}
		if (f != NULL) {
			fprintf(f, "%9.3f us (+%8.3f us) %-6s ", micros,
					micros - prevMicros, core_id);
			fprintf(f, event->format, event->arg0, event->arg1,
					event->arg2, event->arg3);
			fprintf(f, "\n");
		}
		prevMicros = micros;
	}
	frozen = 0;
}

/**
 * Print all of the accumulated time trace entries to a given file.
 * \param name
 *      Name of the file in which to print the entries.
 * \return
 *      Zero means success. Nonzero means that the given name couldn't be
 *      opened, and the return value is the errno describing the problem.
 */
int
TimeTrace::printToFile(const char *name)
{
	FILE *f = fopen(name, "w");
	if (f == NULL)
		return errno;
	printInternal(NULL, f);
	fclose(f);
	return 0;
}

/**
 * Given an elapsed time measured in rdtsc cycles, return a
 * floating-point number giving the corresponding time in seconds.
 * \param cycles
 *      Difference between the results of two calls to rdtsc().
 * \return
 *      The time in seconds corresponding to cycles.
 */
double TimeTrace::toSeconds(uint64_t cycles)
{
    return ((double) (cycles))/getCyclesPerSec();
}

/**
 * Construct a TimeTrace::Buffer.
 */
TimeTrace::Buffer::Buffer()
	: name()
	, nextIndex(0)
    , refCount(0)
	, events()
{
    std::lock_guard<std::mutex> guard(mutex);
    
    // Choose a name for this thread.
    static int nextId = 1;
    char buffer[100];
    snprintf(buffer, sizeof(buffer), "t%d", nextId);
    nextId++;
    name.append(buffer);
    
	// Mark all of the events invalid.
	for (uint32_t i = 0; i < BUFFER_SIZE; i++) {
		events[i].format = NULL;
	}
    
    threadBuffers.push_back(this);
}

/**
 * TimeTrace::Buffer::~Buffer() - Destructor for TimeTrace::Buffers.
 */
TimeTrace::Buffer::~Buffer()
{
    printf("TimeTrace::Buffer destroyed\n");
}

/**
 * Same as TimeTrace::record, except this method isn't inlined.
 */
void TimeTrace::record2(const char* format, uint64_t arg0, uint64_t arg1,
        uint64_t arg2, uint64_t arg3) {
#if ENABLE_TIME_TRACE
    tb.record(rdtsc(), format, arg0, arg1, arg2, arg3);
#endif
}

/**
 * Record an event. Same API as for TimeTrace::record.
 * \param timestamp
 *      The time at which the event occurred (as returned by rdtsc()).
 * \param format
 *      A format string for snprintf that will be used, along with arg0..arg3,
 *      to generate a human-readable message describing what happened, when
 *      the time trace is printed. The message is generated by calling
 *      snprintf as follows: snprintf(buffer, size, format, arg0, arg1, arg2,
 *      arg3) where format and arg0..arg3 are the corresponding arguments
 *      to this method. This pointer is stored in the time trace, so the
 *      caller must ensure that its contents will not change over its lifetime
 *      in the trace.
 * \param arg0
 *      Argument to use when printing a message about this event.
 * \param arg1
 *      Argument to use when printing a message about this event.
 * \param arg2
 *      Argument to use when printing a message about this event.
 * \param arg3
 *      Argument to use when printing a message about this event.
 */
void TimeTrace::Buffer::record(uint64_t timestamp, const char* format,
        uint64_t arg0, uint64_t arg1, uint64_t arg2, uint64_t arg3)
{
	Event* event = &events[nextIndex];
	if (frozen)
		return;
	nextIndex = (nextIndex + 1) & BUFFER_MASK;

	event->timestamp = timestamp;
	event->format = format;
	event->arg0 = arg0;
	event->arg1 = arg1;
	event->arg2 = arg2;
	event->arg3 = arg3;
}
