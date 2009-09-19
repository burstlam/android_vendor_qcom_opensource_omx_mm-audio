/*--------------------------------------------------------------------------
Copyright (c) 2009, Code Aurora Forum. All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:
    * Redistributions of source code must retain the above copyright
      notice, this list of conditions and the following disclaimer.
    * Redistributions in binary form must reproduce the above copyright
      notice, this list of conditions and the following disclaimer in the
      documentation and/or other materials provided with the distribution.
    * Neither the name of Code Aurora nor
      the names of its contributors may be used to endorse or promote
      products derived from this software without specific prior written
      permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
NON-INFRINGEMENT ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR
CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
--------------------------------------------------------------------------*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <fcntl.h>
#include <errno.h>

#include <adec_svr.h>

#define DEBUG_PRINT       printf

/**
 @brief This function processes posted messages

 Once thread is being spawned, this function is run to
 start processing commands posted by client

 @param _svr pointer to command server context

 */
void *adec_message_thread(void *_svr)
{
    struct adec_cmd_svr *svr = (struct adec_cmd_svr*)_svr;
    unsigned char id;
    int n;

    DEBUG_PRINT("\n%s: message thread start\n", __FUNCTION__);
    while (!svr->dead) {
        n = read(svr->pipe_in, &id, 1);
        if (n == 0) break;
        if (n == 1) {
          DEBUG_PRINT("\n%s: process next event\n", __FUNCTION__);
          svr->process_msg_cb(svr->client_data, id);
        }
        if ((n < 0) && (errno != EINTR)) break;
    }

    DEBUG_PRINT("%s: message thread stop\n", __FUNCTION__);

    return 0;
}

void *adec_message_output_thread(void *_cln)
{
    struct adec_cmd_cln *cln = (struct adec_cmd_cln*)_cln;
    unsigned char id;
    int n;

    DEBUG_PRINT("\n%s: message thread start\n", __FUNCTION__);
    while (!cln->dead) {
        n = read(cln->pipe_in, &id, 1);
        if (n == 0) break;
        if (n == 1) {
          DEBUG_PRINT("\n%s: process next event\n", __FUNCTION__);
          cln->process_msg_output_cb(cln->client_data, id);
        }
        if ((n < 0) && (errno != EINTR)) break;
    }

    DEBUG_PRINT("%s: message thread stop\n", __FUNCTION__);

    return 0;
}


/**
 @brief This function starts command server

 @param cb pointer to callback function from the client
 @param client_data reference client wants to get back
  through callback
 @return handle to command server
 */
struct adec_cmd_svr *adec_svr_start(process_message_func cb,
                                    void* client_data)
{
  int r;
  int fds[2];
  struct adec_cmd_svr *svr;

  DEBUG_PRINT("%s: start server\n", __FUNCTION__);
  svr = calloc(1, sizeof(struct adec_cmd_svr));
  if (!svr) return 0;

  svr->client_data = client_data;
  svr->process_msg_cb = cb;

  if (pipe(fds)) {
    DEBUG_PRINT("\n%s: pipe creation failed\n", __FUNCTION__);
    goto fail_pipe;
  }

  svr->pipe_in = fds[0];
  svr->pipe_out = fds[1];


  r = pthread_create(&svr->thr, 0, adec_message_thread, svr);
  if (r < 0) goto fail_thread;

  return svr;



fail_thread:
  close(svr->pipe_in);
  close(svr->pipe_out);

fail_pipe:
  free(svr);

  return 0;
}

/**
 @brief This function stop command server

 @param svr handle to command server
 @return none
 */
void adec_svr_stop(struct adec_cmd_svr *svr) {
  DEBUG_PRINT("%s stop server\n", __FUNCTION__);
  close(svr->pipe_in);
  close(svr->pipe_out);
  free(svr);
}

/**
 @brief This function post message in the command server

 @param svr handle to command server
 @return none
 */
void adec_svr_post_msg(struct adec_cmd_svr *svr, unsigned char id) {
  DEBUG_PRINT("\n%s id=%d\n", __FUNCTION__,id);
  write(svr->pipe_out, &id, 1);
}

void adec_output_post_msg(struct adec_cmd_cln *cln, unsigned char id) {
  DEBUG_PRINT("\n%s id=%d\n", __FUNCTION__,id);
  write(cln->pipe_out, &id, 1);
}

/**
 @brief This function starts command server

 @param cb pointer to callback function from the client
 @param client_data reference client wants to get back
  through callback
 @return handle to command server
 */
struct adec_cmd_cln *adec_cln_start(process_message_func cb,
                                    void* client_data)
{
  int r;
  int fds[2];
  struct adec_cmd_cln *cln;

  DEBUG_PRINT("%s: start server\n", __FUNCTION__);
  cln = calloc(1, sizeof(struct adec_cmd_cln));
  if (!cln) return 0;

  cln->client_data = client_data;
  cln->process_msg_output_cb = cb;

  if (pipe(fds)) {
    DEBUG_PRINT("\n%s: pipe creation failed\n", __FUNCTION__);
    goto fail_pipe;
  }

  cln->pipe_in = fds[0];
  cln->pipe_out = fds[1];


  r = pthread_create(&cln->thr, 0, adec_message_output_thread, cln);
  if (r < 0) goto fail_thread;

  return cln;



fail_thread:
  close(cln->pipe_in);
  close(cln->pipe_out);

fail_pipe:
  free(cln);

  return 0;
}

void adec_cln_stop(struct adec_cmd_cln *cln) {
  DEBUG_PRINT("%s stop server\n", __FUNCTION__);
  close(cln->pipe_in);
  close(cln->pipe_out);
  free(cln);
}
