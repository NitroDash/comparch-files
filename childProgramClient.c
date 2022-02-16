/* **********************************************************
 * Copyright (c) 2014-2018 Google, Inc.  All rights reserved.
 * Copyright (c) 2002-2008 VMware, Inc.  All rights reserved.
 * **********************************************************/

/*
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * * Redistributions of source code must retain the above copyright notice,
 *   this list of conditions and the following disclaimer.
 *
 * * Redistributions in binary form must reproduce the above copyright notice,
 *   this list of conditions and the following disclaimer in the documentation
 *   and/or other materials provided with the distribution.
 *
 * * Neither the name of VMware, Inc. nor the names of its contributors may be
 *   used to endorse or promote products derived from this software without
 *   specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL VMWARE, INC. OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH
 * DAMAGE.
 */

/* Code Manipulation API Sample:
 * childProgramClient.c
 *
 * Reports back to a parent program with each block and asks for optimization.
 */

#include "dr_api.h"
#include "drmgr.h"
#include "drreg.h"

#include<unistd.h>
#include<stdlib.h>

#ifdef WINDOWS
#    define DISPLAY_STRING(msg) dr_messagebox(msg)
#    define ATOMIC_INC(var) _InterlockedIncrement((volatile LONG *)(&(var)))
#else
#    define DISPLAY_STRING(msg) dr_printf("%s\n", msg);
#    define ATOMIC_INC(var) __asm__ __volatile__("lock incl %0" : "=m"(var) : : "memory")
#endif

static bool enable;

/* Use atomic operations to increment these to avoid the hassle of locking. */
static int num_examined, num_converted;

/* Pipes */
static int readPipe, writePipe;
//static sem_t pipeLock;

static dr_emit_flags_t
event_instruction_change(void *drcontext, void *tag, instrlist_t *bb, bool for_trace,
                         bool translating);

static void
event_exit(void);

DR_EXPORT void
dr_client_main(client_id_t id, int argc, const char *argv[])
{
    /* We only used drreg for liveness, not for spilling, so we need no slots. */
    drreg_options_t ops = { sizeof(ops), 0 /*no slots needed*/, false };
    dr_set_client_name("ChildProgram remote optimizer",
                       "http://dynamorio.org/issues");
    if (!drmgr_init() || drreg_init(&ops) != DRREG_SUCCESS)
        DR_ASSERT(false);

    /* Register for our events: process exit, and code transformation.
     * We're changing the app's code, rather than just inserting observational
     * instrumentation.
     */
    dr_register_exit_event(event_exit);
    if (!drmgr_register_bb_app2app_event(event_instruction_change, NULL))
        DR_ASSERT(false);

    /* Long ago, this optimization would target the Pentium 4 (identified via
     * "proc_get_family() == FAMILY_PENTIUM_4"), where an add of 1 is faster
     * than an inc.  For illustration purposes we leave a boolean controlling it
     * but we turn it on all the time in this sample and leave it for future
     * work to determine whether to disable it on certain microarchitectures.
     */
    enable = true;

    /* Make it easy to tell by looking at the log file which client executed. */
    dr_log(NULL, DR_LOG_ALL, 1, "Client 'childProgram' initializing\n");
#ifdef SHOW_RESULTS
    /* Also give notification to stderr */
    if (dr_is_notify_on()) {
#    ifdef WINDOWS
        /* Ask for best-effort printing to cmd window.  Must be called at init. */
        dr_enable_console_printing();
#    endif
        dr_fprintf(STDERR, "Client childProgram is running\n");
    }
#endif
    /* Initialize our global variables. */
    num_examined = 0;
    num_converted = 0;
    //sem_init(&pipeLock, 0, 1);
    readPipe = atoi(argv[1]);
    writePipe = atoi(argv[2]);
}

static void
event_exit(void)
{
#ifdef SHOW_RESULTS
    char msg[256];
    int len;
    if (enable) {
        len = dr_snprintf(msg, sizeof(msg) / sizeof(msg[0]),
                          "converted %d out of %d inc/dec to add/sub\n", num_converted,
                          num_examined);
    } else {
        len = dr_snprintf(msg, sizeof(msg) / sizeof(msg[0]),
                          "decided to keep all original inc/dec\n");
    }
    DR_ASSERT(len > 0);
    msg[sizeof(msg) / sizeof(msg[0]) - 1] = '\0';
    DISPLAY_STRING(msg);
#endif /* SHOW_RESULTS */
    if (!drmgr_unregister_bb_app2app_event(event_instruction_change) ||
        drreg_exit() != DRREG_SUCCESS)
        DR_ASSERT(false);
    drmgr_exit();
    int buf = -1;
    int err = write(writePipe, &buf, sizeof(int));
    err = read(readPipe, &buf, sizeof(int));
    close(readPipe);
    close(writePipe);
}

typedef struct {
	int type;
	int64_t longParam;
	int p1;
	int p2;
} instr_opnd_t;

void parse_opnd(instr_opnd_t* dest, opnd_t src) {
	if (opnd_is_null(src)) {
		dest->type = 0;
	} else if (opnd_is_reg(src)) {
		dest->type = 1;
		dest->p1 = opnd_get_reg(src);
	} else if (opnd_is_reg_partial(src)) {
		dest->type = 2;
	} else if (opnd_is_immed_int(src)) {
		dest->type = 4;
		dest->p1 = opnd_get_immed_int(src);
	} else if (opnd_is_immed_int64(src)) {
		dest->type = 5;
		dest->longParam = opnd_get_immed_int64(src);
	} else if (opnd_is_immed_float(src)) {
		dest->type = 6;
	} else if (opnd_is_near_pc(src)) {
		dest->type = 7;
		dest->longParam = (int64_t) opnd_get_pc(src);
	} else if (opnd_is_far_pc(src)) {
		dest->type = 8;
	} else if (opnd_is_abs_addr(src)) {
		dest->type = 9;
	} else {
		dest->type = -1;
	}
}

typedef struct {
	unsigned char* app_pc;
	 int opcode;
	 int numSrc;
	 int numDst;
	 int length;
} instr_data_t;

void replace_src(instr_t* instr, int index, int type, int64_t longParam, int p1, int p2) {
	// Only supports near pc's, since that's what the optimizer uses
	if (type != 7) return;
	instr_set_src(instr, index, opnd_create_pc((app_pc) longParam));
}

void print_instrlist(instrlist_t* list, void* drcontext, char* prefix) {
	dr_fprintf(STDERR, "%s", prefix);
	instr_t* current = instrlist_first_app(list);
	while (current != NULL) {
		dr_print_instr(drcontext, STDERR, current, "\t");
		current = instr_get_next_app(current);
	}
}

/* Asks its parent for optimizations to run.
 */
static dr_emit_flags_t
event_instruction_change(void *drcontext, void *tag, instrlist_t *bb, bool for_trace,
                         bool translating)
{
    int opcode;
    instr_t *instr, *next_instr;
    /* Only bother replacing for hot code, i.e., when for_trace is true, and
     * when the underlying microarchitecture calls for it.
     */
    if (!for_trace || !enable)
        return DR_EMIT_DEFAULT;
    
    int numInstrs = 0;
    for (instr = instrlist_first_app(bb); instr != NULL; instr = next_instr) {
	    next_instr = instr_get_next_app(instr);
	    numInstrs++;
    }
    //print_instrlist(bb, drcontext, "Before change:\n");
    int err = write(writePipe, &numInstrs, sizeof(int));
    unsigned char* buf = malloc(2000); 
    err = read(readPipe, buf, 2000);
    for (instr = instrlist_first_app(bb); instr != NULL; instr = next_instr) {
        /* We're deleting some instrs, so get the next first. */
        next_instr = instr_get_next_app(instr);
	instr_data_t* iData = (instr_data_t*) buf;
        iData->app_pc = instr_get_app_pc(instr);
	iData->opcode = instr_get_opcode(instr);
	iData->numSrc = instr_num_srcs(instr);
	iData->numDst = instr_num_dsts(instr);
	iData->length = instr_length(drcontext, instr);
	int totalOps = iData->numSrc + iData->numDst;
	instr_opnd_t* oData = (instr_opnd_t*) (buf + sizeof(instr_data_t));
	for (int i = 0; i < iData->numSrc; i++) {
		parse_opnd(oData, instr_get_src(instr, i));
		oData++;
	}
	for (int i = 0; i < iData->numDst; i++) {
		parse_opnd(oData, instr_get_dst(instr, i));
		oData++;
	}
	err = write(writePipe, buf, sizeof(instr_data_t) + totalOps*sizeof(instr_opnd_t));
	err = read(readPipe, buf, sizeof(int));
    }
    err = write(writePipe, buf, sizeof(int));
    instrlist_t* newInsts = instrlist_create(drcontext);
    while (1) {
	err = read(readPipe, buf, 2000);
	int baseIndex = *((int*) buf);
	if (baseIndex == -1) {
		break;
	}
	instr_t* baseInst = instrlist_first_app(bb);
	while (baseIndex > 0) {
		baseInst = instr_get_next_app(baseInst);
		baseIndex--;
	}
	instr_t* newInst = instr_clone(drcontext, baseInst);
	unsigned char* bufRead = buf + sizeof(int);
	int temp = *((int*) bufRead);
	bufRead += sizeof(int);
	if (temp) {
		//Dirty, need to make changes
		temp = *((int*) bufRead);
		bufRead += sizeof(int);
		if (temp) {
			// Dirty pc/opcode
			instr_set_translation(newInst, *((app_pc*) bufRead));
			bufRead += sizeof(app_pc);
			instr_set_opcode(newInst, *((int*) bufRead));
			bufRead += sizeof(int);
		}
		// Dirty sources
		for (int s = 0; s < instr_num_srcs(newInst); s++) {
			temp = *((int*) bufRead);
			bufRead += sizeof(int);
			if (temp) {
				int type = *((int*) bufRead);
				bufRead += sizeof(int);
				int64_t longParam = *((int64_t*) bufRead);
				bufRead += sizeof(int64_t);
				int p1 = *((int*) bufRead);
				bufRead += sizeof(int);
				int p2 = *((int*) bufRead);
				bufRead += sizeof(int);
				replace_src(newInst, s, type, longParam, p1, p2);
			}
		}
	}
	instrlist_append(newInsts, newInst);
	err = write(writePipe, buf, sizeof(int));
    }
    err = write(writePipe, buf, sizeof(int));
    err = read(readPipe, buf, sizeof(app_pc));
    app_pc new_fallthrough = *((app_pc*) buf);
    instrlist_clear(drcontext, bb);
    instr_t* copyInst = instrlist_first_app(newInsts);
    while (copyInst != NULL) {
	    instrlist_append(bb, instr_clone(drcontext, copyInst));
	    copyInst = instr_get_next_app(copyInst);
    }
    instrlist_clear_and_destroy(drcontext, newInsts);
    free(buf);
    if (new_fallthrough != NULL) {
	    instrlist_set_fall_through_target(bb, new_fallthrough);
    }
    //print_instrlist(bb, drcontext, "After change:\n");
    return DR_EMIT_DEFAULT;
}

static bool remove_loads(void *drcontext, instr_t *instr, instrlist_t *bb) {
    instr_t *new_instr;
    instr_t *next_instr;
    uint eflags;
    /*if (!opnd_is_memory_reference(instr_get_src(instr, 0))) {
	    return false;
    }*/
    dr_print_instr(drcontext, STDERR, instr, "Load: ");
    return true;
    next_instr = instr_get_next_app(instr);
    if ((next_instr != NULL) && (instr_get_opcode(next_instr)==OP_add)) {
	dr_print_instr(drcontext, STDERR, instr, "Found two adds in a row:\n\t");
	dr_print_instr(drcontext, STDERR, next_instr, "\t");
    } else {
	    return false;
    }
    if (!opnd_is_immed_int(instr_get_src(instr,0))) {
	    dr_fprintf(STDERR, "But not an immediate add.\n");
	    return false;
    }
    if (!opnd_is_immed_int(instr_get_src(next_instr,0))) {
	    return false;
    }
    opnd_t src1, src2, dst1, dst2, c1, c2;
    src1 = instr_get_src(instr, 1);
    src2 = instr_get_src(next_instr, 1);
    dst1 = instr_get_dst(instr, 0);
    dst2 = instr_get_dst(next_instr, 0);
    c1 = instr_get_src(instr, 0);
    c2 = instr_get_src(next_instr, 0);
    if (!opnd_is_reg(src1) || !opnd_is_reg(src2) || !opnd_is_reg(dst1) || !opnd_is_reg(dst2)) {
	    return false;
    }
    if (opnd_get_reg(src1) != opnd_get_reg(src2)) {
	    dr_fprintf(STDERR, "Whoops, not all increments\n");
	    return false;
    }
    if (opnd_get_reg(src1) != opnd_get_reg(dst1)) {
	    dr_fprintf(STDERR, "Whoops, not all increments\n");
	    return false;
    }
    if (opnd_get_reg(src1) != opnd_get_reg(dst2)) {
	    dr_fprintf(STDERR, "Whoops, not all increments\n");
	    return false;
    }
    dr_fprintf(STDERR, "All increments.\n");
    opnd_t newConst = opnd_create_immed_int(opnd_get_immed_int(c1)+opnd_get_immed_int(c2),opnd_get_size(c1));
    instr_set_src(next_instr, 0, newConst);
    instrlist_remove(bb, instr);
    instr_destroy(drcontext, instr);
    dr_print_instr(drcontext, STDERR, next_instr, "New instruction:\n\t");
    return true;
    new_instr = instr_create_1dst_2src(drcontext,
		    OP_idiv,
		    instr_get_dst(instr,0),
		    opnd_create_immed_float(1),
		    opnd_create_immed_float(0));
    new_instr = instr_create_0dst_0src(drcontext,
		    OP_ud2a);
    if (instr_get_prefix_flag(instr, PREFIX_LOCK))
        instr_set_prefix_flag(new_instr, PREFIX_LOCK);
    instr_set_translation(new_instr, instr_get_app_pc(instr));
    instrlist_replace(bb, instr, new_instr);
    instr_destroy(drcontext, instr);
    dr_print_instr(drcontext, STDOUT, new_instr, "Replaced instruction with:\n\t");
    return true;
}

/* Replaces inc with add 1, dec with sub 1.
 * Returns true if successful, false if not.
 */
static bool
replace_inc_with_add(void *drcontext, instr_t *instr, instrlist_t *bb)
{
    instr_t *new_instr;
    uint eflags;
    int opcode = instr_get_opcode(instr);

    DR_ASSERT(opcode == OP_inc || opcode == OP_dec);
#ifdef VERBOSE
    dr_print_instr(drcontext, STDOUT, instr, "in replace_inc_with_add:\n\t");
#endif

    /* Add/sub writes CF, inc/dec does not, so we make sure that's ok.
     * We use drreg's liveness analysis, which includes the rest of this block.
     * To be more sophisticated, we could examine instructions at target of each
     * direct exit instead of assuming CF is live across any branch.
     */
    /*if (drreg_aflags_liveness(drcontext, instr, &eflags) != DRREG_SUCCESS ||
        (eflags & EFLAGS_READ_CF) != 0) {
#ifdef VERBOSE
        dr_printf("\tCF is live, cannot replace inc with add ");
#endif
        return false;
    }
    if (opcode == OP_inc) {
#ifdef VERBOSE
        dr_printf("\treplacing inc with add\n");
#endif
        new_instr =
            INSTR_CREATE_add(drcontext, instr_get_dst(instr, 0), OPND_CREATE_INT8(1));
    } else if (opcode == OP_dec) {
#ifdef VERBOSE
        dr_printf("\treplacing dec with sub\n");
#endif
        new_instr =
            INSTR_CREATE_sub(drcontext, instr_get_dst(instr, 0), OPND_CREATE_INT8(1));
    }*/
    if (instr_get_prefix_flag(instr, PREFIX_LOCK))
        instr_set_prefix_flag(new_instr, PREFIX_LOCK);
    instr_set_translation(new_instr, instr_get_app_pc(instr));
    instrlist_replace(bb, instr, new_instr);
    instr_destroy(drcontext, instr);
    return true;
}
