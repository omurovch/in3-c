/** @file 
 * handles caching and storage.
 * 
 * handles the request.
 * */

#include "context.h"

#ifndef SEND_H
#define SEND_H

/**
 * executes a request context by  picking nodes and sending it.
 */
in3_error_t in3_send_ctx(in3_ctx_t* ctx);

#endif
