#define _GNU_SOURCE

#include<stdio.h>
#include<unistd.h>
#include<fcntl.h>
#include<errno.h>
#include<stdlib.h>
#include<string.h>

typedef struct {
	int type;
	int64_t longParam;
	int p1;
	int p2;
} instr_opnd_t;

typedef struct {
	unsigned char* app_pc;
	int opcode;
	int numSrc;
	int numDst;
	int length;
} instr_data_t;

typedef struct Instr {
	instr_data_t iData;
	instr_opnd_t* src;
	instr_opnd_t* dst;
	int origIndex;
	int dirty;
	int dirtyInst;
	int dirtySrc[8];
	int dirtyDst[8];
	struct Instr* next;
	struct Instr* prev;
} instr_t;

typedef struct {
	instr_t* first;
	instr_t* last;
	unsigned char* fall_through;
} instrlist_t;

instr_t* instr_create() {
	instr_t* result = malloc(sizeof(instr_t));
	result->dirty = 1;
	result->dirtyInst = 1;
	result->iData.numSrc = 0;
	result->iData.numDst = 0;
	result->next = NULL;
	result->prev = NULL;
	return result;
}

void instr_srcdst_destroy(instr_t* instr) {
	free(instr->src);
	free(instr->dst);
}

void instr_destroy(instr_t* instr) {
	instr_srcdst_destroy(instr);
	free(instr);
}

instr_t* instr_clone(instr_t* orig) {
	instr_t* result = instr_create();
	memcpy(result, orig, sizeof(instr_t));
	result->next = NULL;
	result->prev = NULL;
	result->src = malloc(result->iData.numSrc * sizeof(instr_opnd_t));
	result->dst = malloc(result->iData.numDst * sizeof(instr_opnd_t));
	memcpy(result->src, orig->src, result->iData.numSrc * sizeof(instr_opnd_t));
	memcpy(result->dst, orig->dst, result->iData.numDst * sizeof(instr_opnd_t));
	return result;
}

instr_t* instr_get_next_app(instr_t* instr) {
	return instr->next;
}

void instr_set_next(instr_t* instr, instr_t* next) {
	//Unused; only used in the optimization as a hacky append
}

instr_t* instr_get_prev_app(instr_t* instr) {
	return instr->prev;
}

int instr_length(instr_t* instr) {
	return instr->iData.length;
}

unsigned char* instr_get_app_pc(instr_t* instr) {
	return instr->iData.app_pc;
}

instr_t* instr_set_translation(instr_t* instr, unsigned char* pc) {
	instr->iData.app_pc = pc;
	instr->dirty = 1;
	instr->dirtyInst = 1;
}

int instr_get_opcode(instr_t* instr) {
	return instr->iData.opcode;
}

void instr_set_opcode(instr_t* instr, int opcode) {
	instr->iData.opcode = opcode;
	instr->dirty = 1;
	instr->dirtyInst = 1;
}

int instr_is_cond_branch(instr_t* instr) {
	int op = instr->iData.opcode;
	if (op >= 152 && op <= 167) return 1;
        if (op >= 26 && op <= 41) return 1;
	return 0;       
}

unsigned char* instr_get_branch_target_pc(instr_t* instr) {
	if (instr->iData.numSrc == 0) return NULL;
	if (instr->src[0].type != 7) return NULL;
	if (!instr_is_cond_branch(instr)) return NULL;
	return (unsigned char*) instr->src[0].longParam;
}

void instr_set_branch_target_pc(instr_t* instr, unsigned char* pc) {
	if (instr->iData.numSrc == 0) return;
	if (instr->src[0].type != 7) return;
	if (!instr_is_cond_branch(instr)) return;
	instr->src[0].longParam = (int64_t) pc;
	instr->dirty = 1;
	instr->dirtySrc[0] = 1;
}

instrlist_t* instrlist_create() {
	instrlist_t* result = malloc(sizeof(instrlist_t));
	result->first = NULL;
	result->last = NULL;
	result->fall_through = NULL;
	return result;
}

void instrlist_node_destroy(instr_t *node) {
	if (node == NULL) {
		return;
	}
	instrlist_node_destroy(node->next);
	instr_srcdst_destroy(node);
	free(node);
}

void instrlist_destroy(instrlist_t *ilist) {
	instrlist_node_destroy(ilist->first);
	free(ilist);
}

void instrlist_append(instrlist_t* ilist, instr_t *instr) {
	if (ilist->first == NULL) {
		ilist->first = instr;
		ilist->last = instr;
	} else {
		ilist->last->next = instr;
		instr->prev = ilist->last;
		ilist->last = instr;
	}
}

//If where isn't in here, silently fails
void instrlist_postinsert(instrlist_t* ilist, instr_t* where, instr_t* instr) {
	if (where == ilist->last) {
		ilist->last = instr;
		instr->prev = where;
		where->next = instr;
		return;
	}
	instr_t* current = ilist->first;
	while (current != NULL) {
		if (current == where) {
			where->next->prev = instr;
			instr->next = where->next;
			instr->prev = where;
			where->next = instr;
			return;
		}
		current = current->next;
	}
}

//The documentation doesn't specify here, so this returns the replaced instruction
// Assumes that instruction is actually in the list
instr_t* instrlist_replace(instrlist_t* ilist, instr_t* oldinst, instr_t* newinst) {
	if (oldinst == ilist->first) {
		if (oldinst == ilist->last) {
			ilist->first = newinst;
			ilist->last = newinst;
			return oldinst;
		} else {
			ilist->first = newinst;
			newinst->next = oldinst->next;
			oldinst->next->prev = newinst;
			return oldinst;
		}
	} else {
		ilist->last = newinst;
		newinst->prev = oldinst->prev;
		oldinst->prev->next = newinst;
		return oldinst;
	}
	newinst->next = oldinst->next;
	newinst->prev = oldinst->prev;
	newinst->next->prev = newinst;
	newinst->prev->next = newinst;
	return oldinst;
}

instrlist_t* instrlist_clone(instrlist_t* old) {
	instrlist_t* result = instrlist_create();
	instr_t* current = old->first;
	while (current != NULL) {
		instrlist_append(result, instr_clone(current));
		current = current->next;
	}
	result->fall_through = old->fall_through;
	return result;
}

instr_t* instrlist_first_app(instrlist_t* ilist) {
	return ilist->first;
}

instr_t* instrlist_last_app(instrlist_t* ilist) {
	return ilist->last;
}

void instrlist_set_fall_through_target(instrlist_t* bb, unsigned char* pc) {
	bb->fall_through = pc;
}

void optimize(instrlist_t* bb);

void busy_read_loop(int fd, unsigned char* buf, int maxLen) {
	int bytesRead = 0;
	while (bytesRead <= 0) {
		bytesRead = read(fd, buf, maxLen);
	}
}

unsigned char* writeIntToBuf(unsigned char* buf, int value) {
	*((int*) buf) = value;
	return buf + sizeof(int);
}

unsigned char* writePtrToBuf(unsigned char* buf, unsigned char* value) {
	*((unsigned char**) buf) = value;
	return buf + sizeof(unsigned char*);
}

int main(int argc, char** argv) {
	if (argc < 4) {
		printf("Usage: parent <drrun location> <client location> <programs>\n");
		return 0;
	}
	//Hardcoded file paths; change these later
	argv[1] = "../DynamoRIO-Linux-9.0.0/bin64/drrun";
	argv[2] = "../rioTools/bin/libchildProgramClient.so";
	int numChildren = argc - 3;
	int* childReadPipes = malloc(numChildren * sizeof(int));
	int* childWritePipes = malloc(numChildren * sizeof(int));
	for (int i = 3; i < argc; i++) {
		int pipeToChild[2];
		int pipeToParent[2];
		if (pipe(pipeToChild) == -1) {
			printf("Error making pipe.\n");
			return 1;
		}
		if (pipe2(pipeToParent, O_NONBLOCK) == -1) {
			printf("Error making pipe 2.\n");
			return 1;
		}
		int pid = fork();
		if (pid == -1) {
			printf("Error, failed to fork\n");
		} else if (pid > 0) {
			//Do parent stuff
			close(pipeToChild[0]);
			close(pipeToParent[1]);
			childReadPipes[i-3] = pipeToParent[0];
			childWritePipes[i-3] = pipeToChild[1];
		} else {
			//Child
			close(pipeToChild[1]);
			close(pipeToParent[0]);
			char pipeWriteBuf[10];
			char pipeReadBuf[10];
			sprintf(pipeReadBuf, "%d", pipeToChild[0]);
			sprintf(pipeWriteBuf, "%d", pipeToParent[1]);
			execl(argv[1], argv[1], "-c", argv[2], pipeReadBuf, pipeWriteBuf, "--", argv[i], (char *) NULL);
			//Whoops, something went wrong
			printf("Error executing program.\n");
			return 1;
		}
	}
	unsigned char* buf = malloc(2000);
	int* isRunning = malloc(numChildren * sizeof(int));
	int childrenLeft = numChildren;
	for (int i = 0; i < numChildren; i++) {
		isRunning[i] = 1;
	}
	while (1) {
	for (int i = 0; i < numChildren; i++) {
		if (!isRunning[i]) continue;
		int bytesRead = read(childReadPipes[i], buf, 2000);
		if (bytesRead == -1) {
			if ((errno == EAGAIN) || (errno == EWOULDBLOCK)) {
				continue;
			} else {
				printf("Error: read failed\n");
				return 1;
			}
		}
		if (bytesRead > 0) {
			int numInstrs = *((int*) buf);
			if (numInstrs == -1) {
				isRunning[i] = 0;
				childrenLeft--;
				int err = write(childWritePipes[i], buf, sizeof(int));
				continue;
			}
			int err = write(childWritePipes[i], buf, sizeof(int));
			instrlist_t* bb = instrlist_create();
			for (int j = 0; j < numInstrs; j++) {
				busy_read_loop(childReadPipes[i], buf, 2000);
				instr_t* newInst = instr_create();
				instr_data_t* iData = (instr_data_t*) buf;
				memcpy(&(newInst->iData), iData, sizeof(instr_data_t));
				instr_opnd_t* oData = (instr_opnd_t*) (buf+sizeof(instr_data_t));
				newInst->src = malloc(iData->numSrc * sizeof(instr_opnd_t));
				memcpy(newInst->src, oData, iData->numSrc * sizeof(instr_opnd_t));
				oData += iData->numSrc;
				newInst->dst = malloc(iData->numDst * sizeof(instr_opnd_t));
				memcpy(newInst->dst, oData, iData->numDst * sizeof(instr_opnd_t));
				for (int op = 0; op < iData->numSrc; op++) {
					newInst->dirtySrc[op] = 0;
				}
				for (int op = 0; op < iData->numDst; op++) {
					newInst->dirtyDst[op] = 0;
				}
				newInst->dirty = 0;
				newInst->dirtyInst = 0;
				newInst->origIndex = j;
				instrlist_append(bb, newInst);
				err = write(childWritePipes[i], buf, sizeof(int));
			}
			optimize(bb);
			busy_read_loop(childReadPipes[i], buf, sizeof(int));
			instr_t* toSend = instrlist_first_app(bb);
			while (toSend != NULL) {
				unsigned char* bufWrite = buf;
				bufWrite = writeIntToBuf(bufWrite, toSend->origIndex);
				bufWrite = writeIntToBuf(bufWrite, toSend->dirty);
				if (toSend->dirty) {
					//Handle dirty stuff
					bufWrite = writeIntToBuf(bufWrite, toSend->dirtyInst);
					if (toSend->dirtyInst) {
						bufWrite = writePtrToBuf(bufWrite, toSend->iData.app_pc);
						bufWrite = writeIntToBuf(bufWrite, toSend->iData.opcode);
					}
					for (int s = 0; s < toSend->iData.numSrc; s++) {
						bufWrite = writeIntToBuf(bufWrite, toSend->dirtySrc[s]);
						if (toSend->dirtySrc[s]) {
							bufWrite = writeIntToBuf(bufWrite, toSend->src[s].type);
							bufWrite = writePtrToBuf(bufWrite, (unsigned char*) toSend->src[s].longParam);
							bufWrite = writeIntToBuf(bufWrite, toSend->src[s].p1);
							bufWrite = writeIntToBuf(bufWrite, toSend->src[s].p2);
						}
					}
				}
				toSend = instr_get_next_app(toSend);
				err = write(childWritePipes[i], buf, (int) (bufWrite - buf));
				busy_read_loop(childReadPipes[i], buf, sizeof(int));
			}
			writeIntToBuf(buf, -1);
			err = write(childWritePipes[i], buf, sizeof(int));
			busy_read_loop(childReadPipes[i], buf, sizeof(int));
			writePtrToBuf(buf, bb->fall_through);
			err = write(childWritePipes[i], buf, sizeof(unsigned char*));
			instrlist_destroy(bb);
		}
	}
	if (childrenLeft == 0) break;
	}
	free(buf);
	free(isRunning);
	return 0;
}

void optimize(instrlist_t* bb) {
	// Put optimization stuff here
}
