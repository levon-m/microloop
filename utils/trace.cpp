/**
 * trace.cpp - Implementation of trace buffer storage
 */

#include "trace.h"

#if TRACE_ENABLED

// Static member definitions
TraceEvent Trace::s_buffer[Trace::BUFFER_SIZE];
volatile size_t Trace::s_writeIdx = 0;

#endif
