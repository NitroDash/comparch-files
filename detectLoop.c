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
 * inc2add.c
 *
 * Performs a dynamic optimization: converts the "inc" instruction to
 * "add 1" whenever worthwhile and feasible without perturbing the
 * target application's behavior.  Illustrates a
 * microarchitecture-specific optimization best performed at runtime
 * when the underlying processor is known.
 */

#include "dr_api.h"

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

/* Replaces inc with add 1, dec with sub 1.
 * Returns true if successful, false if not.
 */
static bool
fix_div_by_zero(void *drcontext, instr_t *inst, instrlist_t *trace);

static dr_emit_flags_t
event_instruction_change(void *drcontext, void *tag, instrlist_t *bb, bool for_trace,
                         bool translating);

static void
event_exit(void);

DR_EXPORT void
dr_client_main(client_id_t id, int argc, const char *argv[])
{
    dr_set_client_name("Loop detection (hopefully)",
                       "http://dynamorio.org/issues");

    /* Register for our events: process exit, and code transformation.
     * We're changing the app's code, rather than just inserting observational
     * instrumentation.
     */
    dr_register_exit_event(event_exit);
    dr_register_bb_event(event_instruction_change);

    /* Long ago, this optimization would target the Pentium 4 (identified via
     * "proc_get_family() == FAMILY_PENTIUM_4"), where an add of 1 is faster
     * than an inc.  For illustration purposes we leave a boolean controlling it
     * but we turn it on all the time in this sample and leave it for future
     * work to determine whether to disable it on certain microarchitectures.
     */
    enable = true;

    /* Make it easy to tell by looking at the log file which client executed. */
    dr_log(NULL, DR_LOG_ALL, 1, "Client 'detectLoop' initializing\n");
#ifdef SHOW_RESULTS
    /* Also give notification to stderr */
    if (dr_is_notify_on()) {
#    ifdef WINDOWS
        /* Ask for best-effort printing to cmd window.  Must be called at init. */
        dr_enable_console_printing();
#    endif
        dr_fprintf(STDERR, "Client detectLoop is running\n");
    }
#endif
    /* Initialize our global variables. */
    num_examined = 0;
    num_converted = 0;
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
    dr_unregister_bb_event(event_instruction_change);
}

/* Replaces inc with add 1, dec with sub 1.
 * If cannot replace (eflags constraints), leaves original instruction alone.
 */
static dr_emit_flags_t
event_instruction_change(void *drcontext, void *tag, instrlist_t *bb, bool for_trace,
                         bool translating)
{
    int opcode;
    instr_t *instr, *next_instr, *currentInstr, *prevInstr, *temp;
    instr_t *loads[5];
    int validLoads[5];
    int numLoads = 0;
    /* Only bother replacing for hot code, i.e., when for_trace is true, and
     * when the underlying microarchitecture calls for it.
     */
    if (!for_trace || !enable)
        return DR_EMIT_DEFAULT;
    dr_fprintf(STDERR, "Found a new block, containing:\n");
    instr = instrlist_first_app(bb);
    while (instr != NULL) {
	    dr_print_instr(drcontext, STDERR, instr, "\t");
	    instr = instr_get_next_app(instr);
    }
    // instr = instrlist_first_app(bb);
    instr = instrlist_last_app(bb);
    prevInstr = instrlist_first_app(bb);
    currentInstr = instrlist_first_app(bb);
    app_pc blockStart = instr_get_app_pc(prevInstr);
    // int count = 0;
    while (instr_get_next_app(currentInstr) != NULL) {
        if (instr_get_opcode(currentInstr) == 57) {
            if (numLoads < 5) {
                loads[numLoads] = currentInstr;
                validLoads[numLoads] = 1;
                numLoads++;
            }
        } // else {
        //     for (int k = 0; k < numLoads; k++) {
        //         if (instr_get_dst(currentInstr, 0) == instr_get_src(loads[k], 0)) {
        //             validLoads[k] = 0;
        //         }
        //     }
        // }
        temp = currentInstr;
        // prevInstr = currentInstr;
        currentInstr = instr_get_next_app(prevInstr);
        prevInstr = temp;
        // count++;
    }
    if (blockStart <= instr_get_branch_target_pc(instr) && instr_get_branch_target_pc(instr) < instr_get_app_pc(instr)) {
        // check instr directly above loop for a compare
        // prevInstr = instr_get_app_pc(instr) - 4;
        //pcode = instr_get_opcode(instr);
        // if (instr_get_app_opcode(prevInstr) == OP_cmp) {

        // }
        instrlist_t* loopCopy = instrlist_clone(drcontext, bb);
        instr_t *newInstr, *prevInstr;
        newInstr = instrlist_first_app(loopCopy);
	app_pc nextPC = instr_get_app_pc(newInstr)+1;
        int loadCount = 0;
        while (instr_get_next_app(newInstr) != NULL) {
        // dr_fprintf(STDERR, "NextPC: %d\n", nextPC);
		instr_t* clone = instr_clone(drcontext, newInstr);
		instr_set_translation(clone, nextPC);
		nextPC += instr_length(drcontext, clone);
        if (loadCount < numLoads) {
                if ((instr_get_app_pc(loads[loadCount]) == instr_get_app_pc(newInstr)) && (validLoads[loadCount] == 1)) {
                    // puts the load in the "old" section of the list instead of the new section
                    dr_fprintf(STDERR, "inserting: %d\n", instr_get_opcode(clone));
                    instrlist_postinsert(bb, loads[loadCount], clone);
                    instr_set_translation(clone, instr_get_app_pc(loads[loadCount])+1);
                    loadCount++;
                } else {
                    dr_fprintf(STDERR, "appending: %d\n", instr_get_opcode(clone));
                    // instrlist_append(bb, clone);
                    // instr_t* temp = instr_get_next(instrlist_last_app(bb));
                    instrlist_postinsert(bb, instrlist_last_app(bb), clone);
                    // instr_set_next(instrlist_last_app(bb), clone);
                    // instr_set_next(temp, clone);
                }
            } else {
                dr_fprintf(STDERR, "appending: %d\n", instr_get_opcode(clone));
                // instrlist_append(bb, clone);
                // instr_t* temp = instr_get_next(instrlist_last_app(bb));
                instrlist_postinsert(bb, instrlist_last_app(bb), clone);
                    // instr_set_next(instrlist_last_app(bb), clone);
                    // instr_set_next(clone, temp);
            }
            // newInstr = instr_get_next_app(newInstr);
            // instrlist_append(bb, clone);
            prevInstr = newInstr;
            newInstr = instr_get_next_app(newInstr);
        }
        instr_t* clone = instr_clone(drcontext, newInstr);
		// instr_set_translation(clone, nextPC);
        // instrlist_append(bb, clone);
        instr_t* temp = instr_get_next(instrlist_last_app(bb));
                    instr_set_next(instrlist_last_app(bb), clone);
                    instr_set_next(clone, temp);
        // instr_set_next(prevInstr, clone);
        instr_set_translation(instr, nextPC);
        app_pc fallThrough = instr_get_app_pc(clone)+instr_length(drcontext, clone);
        instrlist_set_fall_through_target(bb, fallThrough);
	// instr_set_translation(newInstr, nextPC);
	// instrlist_append(bb, newInstr);
	dr_fprintf(STDERR, "Before opcode: %d\n", instr_get_opcode(instr));
        instr_set_opcode(instr, instr_get_opcode(instr) ^ 1);
	/*switch (instr_get_opcode(instr)) {
            case OP_jo :
            instr_set_opcode(instr, OP_jno);
            break;
            case OP_jno :
            instr_set_opcode(instr, OP_jo);
            break;
            case OP_jb :
            instr_set_opcode(instr, OP_jnb);
            break;
            case OP_jnb :
            instr_set_opcode(instr, OP_jb);
            break;
            case OP_jz :
            instr_set_opcode(instr, OP_jnz);
            break;
            case OP_jnz :
            instr_set_opcode(instr, OP_jz);
            break;
            case OP_jbe :
            instr_set_opcode(instr, OP_jnbe);
            break;
            case OP_jnbe :
            instr_set_opcode(instr, OP_jbe);
            break;
            case OP_js :
            instr_set_opcode(instr, OP_jns);
            break;
            case OP_jns :
            instr_set_opcode(instr, OP_js);
            break;
            case OP_jp :
            instr_set_opcode(instr, OP_jnp);
            break;
            case OP_jnp :
            instr_set_opcode(instr, OP_jp);
            break;
            case OP_jl :
            instr_set_opcode(instr, OP_jnl);
            break;
            case OP_jnl :
            instr_set_opcode(instr, OP_jl);
            break;
            case OP_jle :
            instr_set_opcode(instr, OP_jnle);
            break;
            case OP_jnle :
            instr_set_opcode(instr, OP_jle);
            break;
        }*/
	dr_fprintf(STDERR, "Opcode after fix: %d\n", instr_get_opcode(instr));
	dr_print_instr(drcontext, STDERR, instr, "Loop thingy: ");
        instr_set_branch_target_pc(instr, instr_get_app_pc(newInstr));
        instr_set_branch_target_pc(newInstr, blockStart);
        // instr_set_opcode(instr);
    }
	dr_print_instr(drcontext, STDERR, instr, "Last in small loop:\n\t");

    dr_fprintf(STDERR, "New block:\n");
    instr = instrlist_first_app(bb);
    while (instr != NULL) {
	    dr_print_instr(drcontext, STDERR, instr, "\t");
	    instr = instr_get_next_app(instr);
    }
    return DR_EMIT_DEFAULT;

    for (instr = instrlist_first_app(bb); instr != NULL; instr = next_instr) {
        /* We're deleting some instrs, so get the next first. */
        next_instr = instr_get_next_app(instr);
        opcode = instr_get_opcode(instr);
        if ((opcode == OP_add) || true) {
            if (!translating)
                ATOMIC_INC(num_examined);
            if (fix_div_by_zero(drcontext, instr, bb)) {
                if (!translating)
                    ATOMIC_INC(num_converted);
            }
        }
    }

    return DR_EMIT_DEFAULT;
}

static bool fix_div_by_zero(void *drcontext, instr_t *instr, instrlist_t *bb) {
    instr_t *new_instr;
    uint eflags;
    int opcode = instr_get_opcode(instr);
    dr_print_instr(drcontext, STDERR, instr, "Found a new add instruction:\n\t");
    dr_print_opnd(drcontext, STDERR, instr_get_src(instr,0), "First operand: ");
    /*new_instr = instr_create_1dst_2src(drcontext,
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
    dr_print_instr(drcontext, STDOUT, new_instr, "Replaced instruction with:\n\t");*/
    return true;
}

