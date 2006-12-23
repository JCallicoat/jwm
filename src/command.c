/**
 * @file command.c
 * @author Joe Wingbermuehle
 * @date 2004-2006
 *
 * @brief Handle running startup, shutdown, and restart commands.
 *
 */

#include "jwm.h"
#include "command.h"
#include "root.h"
#include "misc.h"
#include "main.h"

/** Structure to represent a list of commands. */
typedef struct CommandNode {
	char *command;             /**< The command. */
	struct CommandNode *next;  /**< The next command in the list. */
} CommandNode;

static CommandNode *startupCommands;
static CommandNode *shutdownCommands;
static CommandNode *restartCommands;

static void RunCommands(CommandNode *commands);
static void ReleaseCommands(CommandNode **commands);
static void AddCommand(CommandNode **commands, const char *command);

/** Initialize the command lists. */
void InitializeCommands() {
	startupCommands = NULL;
	shutdownCommands = NULL;
	restartCommands = NULL;
}

/** Process startup/restart commands. */
void StartupCommands() {

	if(isRestarting) {
		RunCommands(restartCommands);
	} else {
		RunCommands(startupCommands);
	}

}

/** Process shutdown commands. */
void ShutdownCommands() {

	if(!shouldRestart) {
		RunCommands(shutdownCommands);
	}

}

/** Destroy the command lists. */
void DestroyCommands() {
	ReleaseCommands(&startupCommands);
	ReleaseCommands(&shutdownCommands);
	ReleaseCommands(&restartCommands);
}

/** Run the commands in a command list. */
void RunCommands(CommandNode *commands) {

	CommandNode *cp;

	for(cp = commands; cp; cp = cp->next) {
		RunCommand(cp->command);
	}

}

/** Release a command list. */
void ReleaseCommands(CommandNode **commands) {

	CommandNode *cp;

	Assert(commands);

	while(*commands) {
		cp = (*commands)->next;
		Release((*commands)->command);
		Release(*commands);
		*commands = cp;
	}

}

/** Add a command to a command list. */
void AddCommand(CommandNode **commands, const char *command) {

	CommandNode *cp;

	Assert(commands);

	if(!command) {
		return;
	}

	cp = Allocate(sizeof(CommandNode));
	cp->next = *commands;
	*commands = cp;

	cp->command = CopyString(command);

}

/** Add a startup command. */
void AddStartupCommand(const char *command) {
	AddCommand(&startupCommands, command);
}

/** Add a shutdown command. */
void AddShutdownCommand(const char *command) {
	AddCommand(&shutdownCommands, command);
}

/** Add a restart command. */
void AddRestartCommand(const char *command) {
	AddCommand(&restartCommands, command);
}

