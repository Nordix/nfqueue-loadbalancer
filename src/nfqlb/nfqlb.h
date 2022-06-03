#pragma once
/*
  SPDX-License-Identifier: Apache-2.0
  Copyright (c) 2022 Nordix Foundation
*/

#include <stdio.h>

/*
  Remote Commands;

  The "lb" process listen for remote commands, for now only flow or
  trace-flow commands.

  The protocol is for now a simple one-way text protocol, but may be
  replaced with something else in the future (e.g grpc).

  The protocol;

	# Request;
	action:<set|delete|list|...>\n
	<param>:<value>\n   (repeated)
	eoc:\n

	# Response;
	<any text>

  There is no sustaining connection. All commands are "one-shot".

	Server procedure; accept-read_request-execute-write_response-close
	Client procedure; is connect-write_request-read_response-close

  NOTE: Remote commands are only used on flow-updates and
        flow-trace. These should be very rare under normal
        circumstances.
 */

/* ----------------------------------------------------------------------
   Server functions;
 */

/*
  FlowCmd defines a flow command.
  Since remote commands are only used for flow's this is the only
  defined structure.
 */
struct FlowCmd {
	char const* action;
	char const* name;
	char const* target;
	int priority;
	char const** protocols;
	char const* dports;
	char const* sports;
	char const** dsts;
	char const** srcs;
	char const** match;
	unsigned short udpencap;
};

/*
  readFlowCmd reads a request from "in" and fills the passed FlowCmd
  structure.
  Return: 0 - OK.
 */
int readFlowCmd(FILE* in, struct FlowCmd* cmd);

/*
  freeFlowCmd must be called after a succesful readFlowCmd to free
  allocated memory.
 */
void freeFlowCmd(struct FlowCmd* cmd);

/* ----------------------------------------------------------------------
   Client functions;
 */

// The only constraint is the command line for the client
#define MAX_CMD_LINE 1024




/* ----------------------------------------------------------------------
   Trace
 */

#define TRACE_PACKET	2
#define TRACE_FRAG		4
#define TRACE_FLOW_CONF	8
#define TRACE_SCTP		16
#define TRACE_TARGET	32

#define TRACE_SHM "nfqlb-trace"
#define TRACE_UNIX_SOCK "nfqlb-trace"
