#pragma once

#include "proc.h"
#include <stdint.h>

typedef struct wasm_state {
	/**
     * The actual process
     */
	wasm_proc_t* proc;

	/**
     * The actual state
     */
	char state[0];
} wasm_state_t;

typedef struct wasm_thread_start_args {
	/**
     * The TID of the thread, 1 is the first thread, 
     * which does not use the special entry point
     */
	uint32_t tid;

	/**
     * The argument to pass to the thread
     */
	uint32_t arg;

	/**
     * The thread's own state base
     */
	wasm_state_t* state;
} wasm_thread_start_args_t;

/**
 * Called by the runtime whenever a new wasm thread has started with its arguments
 */
void wasm_thread_start(wasm_thread_start_args_t* args);

/**
 * Call this to create a new wasm-level thread
 */
err_t wasm_create_thread(wasm_proc_t* proc, uint32_t arg, int32_t* out_tid);
