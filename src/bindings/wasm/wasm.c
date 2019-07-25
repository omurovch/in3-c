// example.cpp
#include "../../core/client/client.h"
#include "../../core/client/context.h"
#include "../../core/client/send.h"
#include "../../verifier/eth1/full/eth_full.h"
#include <emscripten.h>

// clang-format off
EM_JS(void, transport_send, (in3_response_t* result,  char* url, char* payload), {
  Module.transport(UTF8ToString(url),UTF8ToString(payload))
     .then(res => Module.ccall('request_set_result','void',['number','string'],[result,res]) )
     .catch(res => Module.ccall('request_set_error','void',['number','string'],[result,res.message || res]) );
})
// clang-format on

int in3_fetch(char** urls, int urls_len, char* payload, in3_response_t* result) {
  for (int i = 0; i < urls_len; i++)
    transport_send(result + i, urls[i], payload);
  while (true) {
    emscripten_sleep(50);
    int done = 0, err = 0;
    for (int i = 0; i < urls_len; i++) {
      if (result[i].error.len) err++;
      if (result[i].error.len || result[i].result.len) done++;
    }
    if (done == urls_len) return err ? IN3_ETRANS : IN3_OK;
  }
  return IN3_OK;
}

static char* last_error = NULL;

static void in3_set_error(char* data) {
  if (last_error) free(last_error);
  last_error = data ? _strdupn(data, -1) : NULL;
}

in3_t* EMSCRIPTEN_KEEPALIVE in3_create() {
  // register a chain-verifier for full Ethereum-Support
  in3_register_eth_full();

  in3_t* c     = in3_new();
  c->transport = in3_fetch;
  in3_set_error(NULL);
  return c;
}
/* frees the references of the client */
void EMSCRIPTEN_KEEPALIVE in3_dispose(in3_t* a) {
  in3_free(a);
  in3_set_error(NULL);
}

char* EMSCRIPTEN_KEEPALIVE in3_last_error() {
  return last_error;
}

in3_ctx_t* EMSCRIPTEN_KEEPALIVE in3_create_request(in3_t* c, char* payload) {
  char* src_data = _strdupn(payload, -1);
  free(payload);
  in3_ctx_t* ctx = new_ctx(c, src_data);
  if (ctx->error) {
    in3_set_error(ctx->error);
    free_ctx(ctx);
    return NULL;
  }
  return ctx;
}

void EMSCRIPTEN_KEEPALIVE in3_send_request(in3_ctx_t* ctx) {
  in3_set_error(NULL);
  in3_send_ctx(ctx);
  ctx->client = NULL;
}

void EMSCRIPTEN_KEEPALIVE in3_free_request(in3_ctx_t* ctx) {
  if (ctx->request_context && ctx->request_context->c) free(ctx->request_context->c);
  free_ctx(ctx);
}

bool EMSCRIPTEN_KEEPALIVE request_is_done(in3_ctx_t* r) {
  return r->client == NULL;
}

char* EMSCRIPTEN_KEEPALIVE request_get_result(in3_ctx_t* r) {
  if (r->error) return NULL;
  // we have a result and copy it
  str_range_t s = d_to_json(r->responses[0]);
  s.data[s.len] = 0;
  return s.data;
}

char* EMSCRIPTEN_KEEPALIVE request_get_error(in3_ctx_t* r) {
  return r->error;
}

void EMSCRIPTEN_KEEPALIVE request_set_result(in3_response_t* r, char* data) {
  sb_add_chars(&r->result, data);
  free(data);
}

void EMSCRIPTEN_KEEPALIVE request_set_error(in3_response_t* r, char* data) {
  sb_add_chars(&r->error, data);
  free(data);
}
