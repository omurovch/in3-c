
#include "../eth_nano/eth_nano.h"
#include "../eth_nano/merkle.h"
#include "../eth_nano/rlp.h"
#include "../eth_nano/serialize.h"
#include "eth_basic.h"
#include <client/context.h>
#include <client/keys.h>
#include <crypto/bignum.h>
#include <crypto/ecdsa.h>
#include <crypto/secp256k1.h>
#include <string.h>
#include <util/data.h>
#include <util/mem.h>
#include <util/utils.h>

static uint8_t* EMPTY_HASH = (uint8_t*) "\xc5\xd2\x46\x01\x86\xf7\x23\x3c\x92\x7e\x7d\xb2\xdc\xc7\x03\xc0\xe5\x00\xb6\x53\xca\x82\x27\x3b\x7b\xfa\xd8\x04\x5d\x85\xa4\x70";

static int is_not_existened(d_token_t* account) {
  d_token_t* t;
  // TODO how do I determine the default nonce? It is in the chain-config
  return ((t = d_get(account, K_BALANCE)) && d_type(t) == T_INTEGER && d_int(t) == 0 && (t = d_get(account, K_CODE_HASH)) && memcmp(t->data, EMPTY_HASH, 32) == 0 && d_get_longk(account, K_NONCE) == 0);
}

int verify_proof(in3_vctx_t* vc, bytes_t* header, d_token_t* account) {
  d_token_t *     t, *storage_proof, *p;
  int             i;
  uint8_t         hash[32], val[36];
  bytes_t**       proof;
  bytes_t *       tmp, root, *account_raw, path = {.data = hash, .len = 32};
  bytes_builder_t bb = {.bsize = 36, .b = {.data = val, .len = 0}};

  // verify the account proof
  if (rlp_decode_in_list(header, BLOCKHEADER_STATE_ROOT, &root) != 1) return vc_err(vc, "no state root in the header");

  if ((tmp = d_get_bytesk(account, K_ADDRESS)))
    sha3_to(tmp, hash);
  else
    return vc_err(vc, "no address in the account");

  proof = d_create_bytes_vec(d_get(account, K_ACCOUNT_PROOF));
  if (!proof) return vc_err(vc, "no merkle proof for the account");
  account_raw = serialize_account(account);

  if (!trie_verify_proof(&root, &path, proof, is_not_existened(account) ? NULL : account_raw)) {
    _free(proof);
    b_free(account_raw);
    return vc_err(vc, "invalid account proof");
  }
  _free(proof);
  b_free(account_raw);

  // now we verify the storage proofs
  if (!(storage_proof = d_get(account, K_STORAGE_PROOF))) return vc_err(vc, "no stortage-proof found!");

  if ((t = d_get(account, K_STORAGE_HASH)))
    root = *d_bytes(t);
  else
    return vc_err(vc, "no storage-hash found!");

  for (i = 0, p = storage_proof + 1; i < d_len(storage_proof); i++, p = d_next(p)) {
    d_bytes_to(d_get(p, K_KEY), hash, 32);
    sha3_to(&path, hash);

    proof = d_create_bytes_vec(d_get(account, K_PROOF));
    if (!proof) return vc_err(vc, "no merkle proof for the storage");

    // rlp encode the value.
    if ((bb.b.len = d_bytes_to(d_get(p, K_VALUE), val, 32)))
      rlp_encode_to_item(&bb);

    if (!trie_verify_proof(&root, &path, proof, bb.b.len ? &bb.b : NULL)) {
      _free(proof);
      return vc_err(vc, "invalid account proof");
    }
    _free(proof);
  }

  return 0;
}

int eth_verify_account_proof(in3_vctx_t* vc) {

  d_token_t *t, *accounts, *contract = NULL, *proofed_account = NULL;
  char*      method = d_get_stringk(vc->request, K_METHOD);
  bytes_t    tmp;
  uint8_t    hash[32];
  int        i;

  // no result -> nothing to verify
  if (!vc->result) return 0;
  if (!vc->proof) return vc_err(vc, "no proof");

  // verify header
  bytes_t* header = d_get_bytesk(vc->proof, K_BLOCK);
  if (!header) return vc_err(vc, "no blockheader");
  if (eth_verify_blockheader(vc, header, NULL)) return vc_err(vc, "invalid blockheader");

  // make sure we blockheader is based on the right blocknumber (unless it is a nodelist or a 'latest'-string)
  t = d_get(vc->request, K_PARAMS);
  if (strcmp(method, "in3_nodeList") && (t = d_get_at(t, d_len(t) - 1)) && d_type(t) == T_INTEGER && rlp_decode_in_list(header, BLOCKHEADER_NUMBER, &tmp) == 1 && bytes_to_long(tmp.data, tmp.len) != d_long(t))
    return vc_err(vc, "the blockheader has the wrong blocknumber");

  // get the account this proof is based on
  if (!(contract = strcmp(method, "in3_nodeList") == 0 ? d_get(vc->result, K_CONTRACT) : d_get_at(d_get(vc->request, K_PARAMS), 0)))
    return vc_err(vc, "no account found in request");

  //now check the results
  if (!(accounts = d_get(vc->proof, K_ACCOUNTS))) return vc_err(vc, "no accounts");
  for (i = 0, t = accounts + 1; i < d_len(accounts); i++, t = d_next(t)) {
    if (verify_proof(vc, header, t))
      return vc_err(vc, "failed verifying the account");
    else if (proofed_account == NULL && d_eq(contract, d_get(t, K_ADDRESS)))
      proofed_account = t;
  }

  if (!proofed_account) return vc_err(vc, "the contract this proof is based on was not part of the proof");

  if (strcmp(method, "eth_getBalance") == 0) {
    if (!d_eq(vc->result, d_get(proofed_account, K_BALANCE)))
      return vc_err(vc, "the balance in the proof is different");
  } else if (strcmp(method, "eth_getCode") == 0) {
    if (d_type(vc->result) != T_BYTES || sha3_to(d_bytes(vc->result), hash) != 0 || memcmp(d_get_bytesk(proofed_account, K_CODE_HASH)->data, hash, 32))
      return vc_err(vc, "the codehash in the proof is different");
  }

  /*

    switch (request.method) {
    case 'eth_getBalance':
      if (!toBN(value).eq(toBN(accountProof.balance))) throw new Error('The Balance does not match the one in the proof')
      break
    case 'eth_getStorageAt':
      checkStorage(accountProof, bytes32(request.params[1]), bytes32(value))
      break
    case 'eth_getCode':
      if (!bytes32(accountProof.codeHash).equals(util.keccak(value))) throw new Error('The codehash in the proof does not match the code')
      break
    case 'eth_getTransactionCount':
      if (!toBN(accountProof.nonce).eq(toBN(value))) throw new Error('The nonce in the proof does not match the returned')
      break
    case 'in3_nodeList':
      verifyNodeListData(value as ServerList, headerProof.proof, block, request)
      // the contract must be checked later in the updateList -function
      break
    default:
      throw new Error('Unsupported Account-Proof for ' + request.method)
  }

*/

  return 0;
}